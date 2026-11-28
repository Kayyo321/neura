// Runs every standard library test function under the NeuraIR reference
// evaluator and under NVM, and compares the outcomes (Stdlib.md section 17,
// "Reference evaluator"). The evaluator runs the specialized program as
// lowered; NVM runs the image `neura test` builds, which is also optimized,
// so the comparison covers specialization, optimization, and conversion.
//
// Compared: whether the test returns or stops, the console lines it writes,
// and the message of a message trap. Tests that reach a construct the
// evaluator does not model (for example a host service with a result
// payload) or that exceed its budget are counted as not compared.
#include "backend/converter.hpp"
#include "fixtures.hpp"
#include "ir-optimization/evaluator.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "nvm/host.hpp"
#include "nvm/tier1.hpp"
#include "nvr/format.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neura::testing {
namespace {

std::vector<SymbolId> test_functions(const IrProgram& program, const fs::path& file) {
    std::vector<SymbolId> result;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != SymbolKind::Function || !item.exported || !item.has_body || !item.name.starts_with("test_") ||
            !item.parameters.empty() || !item.generics.empty() || item.type.kind != TypeKind::Void ||
            item.owner == no_symbol || program.symbols[item.owner].kind != SymbolKind::Module ||
            !program.symbols[item.owner].generics.empty() || !fs::equivalent(fs::path(item.span.file), file)) {
            continue;
        }
        result.push_back(id);
    }
    return result;
}

// The console output an evaluation recorded, or nothing when it recorded an
// effect other than a console line.
std::optional<std::string> console_output(const Outcome& outcome) {
    std::string output;
    for (const auto& effect : outcome.effects) {
        if (effect.callee != "Console.write_line" || effect.arguments.size() != 1) return std::nullopt;
        output += effect.arguments.front().text + "\n";
    }
    return output;
}

std::vector<fs::path> reference_files(const fs::path& root) {
    // Keep the single-file selector for diagnosing an individual input.
    if (const char* only = std::getenv("NEURA_REFERENCE_FILE")) return {fs::path(only)};
    std::vector<fs::path> files;
    for (const auto& area : fs::directory_iterator(root)) {
        if (!area.is_directory()) continue;
        for (const auto& entry : fs::directory_iterator(area.path())) {
            if (entry.path().extension() == ".nra") files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    const char* setting = std::getenv("NEURA_REFERENCE_SHARD");
    if (!setting) return files;

    const std::string_view shard(setting);
    const auto slash = shard.find('/');
    constexpr auto invalid = "NEURA_REFERENCE_SHARD must be INDEX/COUNT with 1 <= INDEX <= COUNT <= files";
    check(slash != std::string_view::npos, invalid);
    const auto number = [&](std::string_view text) {
        std::size_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        check(error == std::errc{} && end == text.data() + text.size(), invalid);
        return value;
    };
    const auto index = number(shard.substr(0, slash));
    const auto count = number(shard.substr(slash + 1));
    check(index >= 1 && index <= count && count <= files.size(), invalid);
    std::vector<fs::path> selected;
    for (std::size_t i = index - 1; i < files.size(); i += count) selected.push_back(std::move(files[i]));
    return selected;
}

void check_fixture(const fs::path& path) {
    auto program = lower_files({path});
    materialize_reference_storage(program);
    specialize(program);
    auto optimized = program;
    optimize(optimized);
    const auto exercise = find_symbol(program, "App.exercise");
    std::string expected;
    for (const auto* version : {&program, &optimized}) {
        Evaluator evaluator(*version);
        for (const auto& initializer : evaluator.initialize()) {
            check(initializer.status == Status::Returned, describe(initializer));
        }
        const auto outcome = evaluator.call(exercise, {});
        check(outcome.status == Status::Returned, describe(outcome));
        const auto output = console_output(outcome);
        check(output.has_value(), "the fixture should record only console lines");
        if (version == &program) {
            expected = *output;
        } else {
            check_equal(*output, expected, "fixture output after optimization");
        }
    }
    const auto actual = run_captured(build_program({path}));
    check(actual.result.completed, "the fixture should complete on NVM");
    check_equal(actual.output, expected, "fixture output on NVM");
}

} // namespace

NEURA_TEST(reference, overloads_match_the_evaluator) {
    check_fixture(program_path(environment, "overloads"));
}

NEURA_TEST(reference, primitive_references_match_the_evaluator) {
    check_fixture(program_path(environment, "primitive-references"));
}

NEURA_TEST(reference, library_tests_match_the_evaluator) {
    const auto root = environment.source_dir / "tests" / "stdlib";
    const auto files = reference_files(root);
    std::size_t compared = 0;
    std::size_t total = 0;
    std::map<std::string, std::size_t> not_compared;
    // The first test that stopped for each reason, to locate gaps.
    std::map<std::string, std::string> first_of;
    std::vector<std::string> mismatches;
    Limits limits;
    limits.budget = 3'000'000;
    limits.depth = 400;
    using Clock = std::chrono::steady_clock;
    const bool profile = std::getenv("NEURA_REFERENCE_PROFILE") != nullptr;
    const auto now = [profile] { return profile ? Clock::now() : Clock::time_point{}; };
    for (const auto& file : files) {
        const auto started = now();
        const auto compared_before = compared;
        const auto label = file.lexically_relative(root).generic_string();
        std::vector<fs::path> roots;
        if (fs::is_directory(file.parent_path() / "support")) roots.push_back(file.parent_path() / "support");
        auto program = lower_files({file}, roots);
        const auto tests = test_functions(program, fs::canonical(file));
        const auto lowered = now();
        Clock::duration preparation{}, evaluation{}, execution{};
        const auto report = [&] {
            if (!profile) return;
            const auto ms = [](Clock::duration elapsed) {
                return std::chrono::duration<double, std::milli>(elapsed).count();
            };
            std::cout << "  reference-file " << label << " total=" << tests.size()
                      << " compared=" << compared - compared_before << " lowering_ms=" << ms(lowered - started)
                      << " preparation_ms=" << ms(preparation) << " evaluator_ms=" << ms(evaluation)
                      << " nvm_ms=" << ms(execution) << " wall_ms=" << ms(now() - started) << '\n';
        };
        if (tests.empty()) {
            report();
            continue;
        }
        total += tests.size();
        materialize_reference_storage(program);
        specialize(program);
        auto optimized = program;
        optimize(optimized);
        backend::ConversionOptions options;
        options.entries = tests;
        auto image = nvr::load(nvr::encode(backend::convert(optimized, options).image));
        const auto evaluator_started = now();
        preparation = evaluator_started - lowered;
        Evaluator evaluator(program, limits);
        bool initialized = true;
        for (const auto& outcome : evaluator.initialize()) {
            if (outcome.status != Status::Returned) {
                ++not_compared["initialization: " + describe(outcome.status) + ": " + outcome.reason];
                initialized = false;
                break;
            }
        }
        evaluation = now() - evaluator_started;
        if (!initialized) {
            report();
            continue;
        }

        nvm::StringSink sink;
        nvm::ConsoleWriteLine console(sink);
        nvm::HostRegistry registry;
        registry.add(console);
        std::optional<nvm::Bindings> bindings;
        std::optional<std::string> bind_error;
        std::optional<nvm::TierCache> tiers;
        nvm::RunLimits runtime_limits;
        for (std::size_t i = 0; i < tests.size(); ++i) {
            const auto name = label + " " + program.symbols[tests[i]].name;
            const auto call_started = now();
            const auto expected = evaluator.call(tests[i], {});
            evaluation += now() - call_started;
            if (expected.status == Status::Unsupported || expected.status == Status::LimitReached) {
                const auto reason = describe(expected.status) + ": " + expected.reason;
                ++not_compared[reason];
                first_of.emplace(reason, name + " at " + describe(expected.location));
                continue;
            }
            const auto output = console_output(expected);
            if (!output) {
                ++not_compared["an effect other than a console line"];
                continue;
            }
            // The NVM run binds the console alone. An image that imports another
            // host service, such as Std.App's server services, cannot bind there,
            // so its tests are not compared.
            const auto nvm_started = now();
            if (!bindings && !bind_error) {
                try {
                    bindings.emplace(nvm::bind(image, registry));
                } catch (const nvm::BindError& error) {
                    bind_error = error.what();
                }
            }
            if (bind_error) {
                execution += now() - nvm_started;
                const std::string reason = "the image imports a host service other than the console";
                ++not_compared[reason];
                first_of.emplace(reason, name + ": " + *bind_error);
                continue;
            }
            // Like neura test, reuse the image's tier proofs and code while
            // giving every entry a fresh execution and empty console sink.
            if (!tiers && nvm::tier_ceiling() >= 1) {
                tiers.emplace(image);
                runtime_limits.tier_cache = &*tiers;
            }
            image.entry = static_cast<std::uint32_t>(i);
            sink = nvm::StringSink{};
            const auto actual = nvm::run(image, *bindings, {}, runtime_limits);
            execution += now() - nvm_started;
            ++compared;
            const bool returned = expected.status == Status::Returned;
            if (returned != actual.completed) {
                mismatches.push_back(name + ": evaluator " + describe(expected) + ", NVM " +
                                     (actual.completed ? "completed" : describe_trap(actual, image)));
                continue;
            }
            if (*output != sink.contents()) {
                mismatches.push_back(name + ": console output differs\n  evaluator: " + *output + "\n  NVM: " +
                                     sink.contents());
                continue;
            }
            if (expected.status == Status::Trapped && expected.trap_reason == 3 &&
                (actual.trap.reason != 3 || actual.trap.detail != expected.reason)) {
                mismatches.push_back(name + ": message differs: evaluator '" + expected.reason + "', NVM '" +
                                     actual.trap.detail + "'");
            }
        }
        report();
    }
    std::cout << "  " << compared << " of " << total << " library tests compared with the evaluator\n";
    std::vector<std::pair<std::size_t, std::string>> reasons;
    for (const auto& [reason, count] : not_compared) reasons.emplace_back(count, reason);
    std::sort(reasons.rbegin(), reasons.rend());
    for (const auto& [count, reason] : reasons) {
        std::cout << "  not compared: " << count << " x " << reason;
        if (const auto found = first_of.find(reason); found != first_of.end()) {
            std::cout << " (first: " << found->second << ")";
        }
        std::cout << '\n';
    }
    for (const auto& mismatch : mismatches) std::cout << "  MISMATCH " << mismatch << '\n';
    check(mismatches.empty(), std::to_string(mismatches.size()) + " library tests disagree with the evaluator");
    check(compared > 0, "no library test could be compared with the evaluator");
}

} // namespace neura::testing
