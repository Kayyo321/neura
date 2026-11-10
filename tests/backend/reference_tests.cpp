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
#include "nvr/format.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
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
    std::vector<fs::path> files;
    for (const auto& area : fs::directory_iterator(root)) {
        if (!area.is_directory()) continue;
        for (const auto& entry : fs::directory_iterator(area.path())) {
            if (entry.path().extension() == ".nra") files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    // NEURA_REFERENCE_FILE limits the run to one test file, for diagnosis.
    if (const char* only = std::getenv("NEURA_REFERENCE_FILE")) files = {fs::path(only)};
    std::size_t compared = 0;
    std::size_t total = 0;
    std::map<std::string, std::size_t> not_compared;
    // The first test that stopped for each reason, to locate gaps.
    std::map<std::string, std::string> first_of;
    std::vector<std::string> mismatches;
    Limits limits;
    limits.budget = 3'000'000;
    limits.depth = 400;
    for (const auto& file : files) {
        std::vector<fs::path> roots;
        if (fs::is_directory(file.parent_path() / "support")) roots.push_back(file.parent_path() / "support");
        auto program = lower_files({file}, roots);
        const auto tests = test_functions(program, fs::canonical(file));
        if (tests.empty()) continue;
        total += tests.size();
        materialize_reference_storage(program);
        specialize(program);
        auto optimized = program;
        optimize(optimized);
        backend::ConversionOptions options;
        options.entries = tests;
        auto image = nvr::load(nvr::encode(backend::convert(optimized, options).image));
        Evaluator evaluator(program, limits);
        bool initialized = true;
        for (const auto& outcome : evaluator.initialize()) {
            if (outcome.status != Status::Returned) {
                ++not_compared["initialization: " + describe(outcome.status) + ": " + outcome.reason];
                initialized = false;
                break;
            }
        }
        if (!initialized) continue;
        const auto label = file.lexically_relative(root).generic_string();
        for (std::size_t i = 0; i < tests.size(); ++i) {
            const auto name = label + " " + program.symbols[tests[i]].name;
            const auto expected = evaluator.call(tests[i], {});
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
            image.entry = static_cast<std::uint32_t>(i);
            const auto actual = run_captured(image);
            ++compared;
            const bool returned = expected.status == Status::Returned;
            if (returned != actual.result.completed) {
                mismatches.push_back(name + ": evaluator " + describe(expected) + ", NVM " +
                                     (actual.result.completed ? "completed" : describe_trap(actual.result, image)));
                continue;
            }
            if (*output != actual.output) {
                mismatches.push_back(name + ": console output differs\n  evaluator: " + *output + "\n  NVM: " +
                                     actual.output);
                continue;
            }
            if (expected.status == Status::Trapped && expected.trap_reason == 3 &&
                (actual.result.trap.reason != 3 || actual.result.trap.detail != expected.reason)) {
                mismatches.push_back(name + ": message differs: evaluator '" + expected.reason + "', NVM '" +
                                     actual.result.trap.detail + "'");
            }
        }
    }
    std::cout << "  " << compared << " of " << total << " library tests compared with the evaluator\n";
    std::vector<std::pair<std::size_t, std::string>> reasons;
    for (const auto& [reason, count] : not_compared) reasons.emplace_back(count, reason);
    std::sort(reasons.rbegin(), reasons.rend());
    for (std::size_t i = 0; i < reasons.size() && i < 12; ++i) {
        std::cout << "  not compared: " << reasons[i].first << " x " << reasons[i].second;
        if (const auto found = first_of.find(reasons[i].second); found != first_of.end()) {
            std::cout << " (first: " << found->second << ")";
        }
        std::cout << '\n';
    }
    for (const auto& mismatch : mismatches) std::cout << "  MISMATCH " << mismatch << '\n';
    check(mismatches.empty(), std::to_string(mismatches.size()) + " library tests disagree with the evaluator");
    check(compared > 0, "no library test could be compared with the evaluator");
}

} // namespace neura::testing
