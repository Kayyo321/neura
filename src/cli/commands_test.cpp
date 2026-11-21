#include "commands.hpp"
#ifdef NEURA_HAS_SQL
#include "sql/sql_engine.hpp"
#endif

#include "gpu_run.hpp"
#include "source_loader.hpp"
#include "backend/converter.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/class_hooks.hpp"
#include "neura-ir/containment.hpp"
#include "neura-ir/inline_limit.hpp"
#include "neura-ir/lifetime.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/storage.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/fleet.hpp"
#include "nvm/host.hpp"
#include "nvm/schedule.hpp"
#include "tooling/coverage.hpp"
#include "tooling/json.hpp"
#include "tooling/test_protocol.hpp"
#include "util/source_map.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace neura::cli {

using neura::runner::Host;
using neura::runner::image_costs;
using neura::runner::provide_native_library;
using neura::runner::read_file;
using neura::runner::write_file;

namespace {

// The source location of a block, when the image has a source map entry.
std::optional<neura::nvr::SourceLocation> block_location(const neura::nvr::Image& image, std::uint32_t function,
                                                         std::uint32_t block) {
    if (!image.source_map) return std::nullopt;
    for (const auto& entry : *image.source_map) {
        if (entry.function == function && entry.block == block) return entry;
    }
    return std::nullopt;
}

// A message trap (reason 3) reports its message, such as a failed Std.Test
// assertion's text, at the innermost frame that belongs to one of the listed
// test files: the assertion's call site rather than the library line that
// trapped. Other traps keep their runtime diagnostic.
std::optional<std::string> test_failure_message(const neura::nvm::Trap& trap, const neura::nvr::Image& image,
                                                const std::set<std::string>& test_labels) {
    if (trap.kind != neura::nvm::TrapKind::Program ||
        trap.reason != static_cast<std::uint32_t>(neura::nvr::TrapReason::ProgramFailure) ||
        trap.function == neura::nvr::absent) {
        return std::nullopt;
    }
    auto text = trap.detail;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> frames{{trap.function, trap.block}};
    frames.insert(frames.end(), trap.callers.begin(), trap.callers.end());
    for (const auto& [function, block] : frames) {
        const auto location = block_location(image, function, block);
        if (location && test_labels.contains(location->path)) {
            return text + " at " + location->path + ":" + std::to_string(location->line) + ":" +
                   std::to_string(location->column);
        }
    }
    if (const auto location = block_location(image, trap.function, trap.block)) {
        text += " at " + location->path + ":" + std::to_string(location->line) + ":" +
                std::to_string(location->column);
    }
    return text;
}

} // namespace

// Testing.md: every exported, zero-argument, void function whose name starts
// with test_ in a module of the listed files is a test. Each test runs in a
// fresh execution of one image, so no state carries over between tests.
int run_tests(const Options& options) {
    // Discovery does semantic analysis but never links native code, converts
    // circuits, initializes modules, or executes any user function.
    auto analysis_options = options;
    if (options.list_tests) analysis_options.command = Command::Check;
    auto program = load_program(analysis_options);
    if (options.test_coverage) {
        for (const auto& item : program.symbols) {
            if (item.kind != neura::SymbolKind::File) continue;
            const auto file = item.span.file.empty() ? item.name : item.span.file;
            std::error_code error;
            if (!file.empty() && fs::equivalent(*options.test_coverage, file, error) && !error)
                throw std::runtime_error("coverage output must not replace a source file: " + file);
        }
    }
    neura::require_valid_ir(program, "before specialization");
    auto catalog = neura::tooling::discover_tests(program, options.files,
                                                   options.test_workspace.value_or(fs::current_path()));
    if (!options.test_filters.empty()) {
        const std::set<std::string> selected(options.test_filters.begin(), options.test_filters.end());
        for (const auto& id : selected) {
            if (std::ranges::none_of(catalog, [&](const auto& item) { return item.id == id; }))
                throw std::runtime_error("unknown test id: " + id + "; use test --list --format=json");
        }
        std::erase_if(catalog, [&](const auto& item) { return !selected.contains(item.id); });
    }
    if (options.list_tests) {
        if (options.test_format_json) {
            neura::tooling::Json records = neura::tooling::Json::Array{};
            for (const auto& test : catalog) records.push_back(neura::tooling::test_info_json(test));
            std::cout << neura::tooling::Json{{"version", 1}, {"tests", std::move(records)}}.dump() << '\n';
        } else {
            for (const auto& test : catalog) std::cout << test.id << '\n';
        }
        return 0;
    }
    std::vector<neura::SymbolId> tests;
    std::vector<std::string> names;
    for (const auto& test : catalog) {
        tests.push_back(test.symbol);
        names.push_back(test.label);
    }
    if (tests.empty()) {
        throw std::runtime_error("no tests found: a test is an exported, zero-argument void function named "
                                 "test_* in a module of the listed files");
    }
    neura::specialize(program);
    neura::check_using_results(program);
    neura::check_storage(program, neura::StorageStage::Specialized);
    neura::materialize_reference_storage(program);
    neura::check_containment(program);
    neura::check_inline_limit(program);
    neura::elaborate_class_hooks(program);
    if (options.optimize) {
        neura::optimize(program);
    } else {
        neura::require_valid_ir(program, "before conversion");
    }
    neura::backend::ConversionOptions conversion_options;
    conversion_options.entries = tests;
    if (options.test_coverage) {
        // Emit concrete project functions even when no selected test calls
        // them, so their mapped locations appear as unexecuted coverage.
        for (const auto& body : program.bodies) {
            const auto& item = program.symbols[body.owner];
            auto owner = body.owner;
            while (owner < program.symbols.size() && program.symbols[owner].kind != neura::SymbolKind::File)
                owner = program.symbols[owner].owner;
            if (owner >= program.symbols.size() || !program.symbols[owner].library_path.empty() ||
                !item.generics.empty() || item.span.file.empty() || !fs::is_regular_file(item.span.file)) continue;
            conversion_options.extra_functions.push_back(body.owner);
        }
    }
    auto conversion = neura::backend::convert(program, conversion_options);
    // --tier-stats times the tests from the loading of their built image.
    std::optional<neura::nvm::TierTimes> times;
    if (options.tier_stats) times.emplace(std::chrono::steady_clock::now());
    auto image = neura::nvr::load(neura::nvr::encode(conversion.image));
    std::unique_ptr<neura::tooling::LineCoverage> coverage;
    if (options.test_coverage) {
        std::map<std::string, fs::path> sources;
        std::map<std::string, fs::path> all_labels;
        for (const auto& item : program.symbols) {
            if (item.kind != neura::SymbolKind::File) continue;
            const auto file = item.span.file.empty() ? item.name : item.span.file;
            if (file.empty() || !fs::is_regular_file(file)) continue;
            const auto path = fs::canonical(file);
            const auto label = neura::backend::source_label(program, file);
            const auto [previous, inserted] = all_labels.try_emplace(label, path);
            if (!inserted && previous->second != path)
                throw std::runtime_error("coverage: source-map path '" + label +
                                         "' names more than one file; use separate source roots");
            if (item.library_path.empty()) sources.insert_or_assign(label, path);
        }
        coverage = std::make_unique<neura::tooling::LineCoverage>(image, sources);
    }
    const auto max_jobs = std::min<std::size_t>(tests.size(), neura::nvm::FleetOptions{}.window);
    neura::cli::GpuRun gpu(options.gpu, image, max_jobs);

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::set<std::string> test_labels;
    for (const auto& file : options.files) {
        test_labels.insert(neura::backend::source_label(program, file.generic_string()));
    }
    std::unique_ptr<neura::tooling::TestReporter> reporter;
    if (options.test_reporter_json) {
        reporter = std::make_unique<neura::tooling::TestReporter>();
        reporter->event("runStart", {{"count", options.schedule ? std::size_t{1} : tests.size()}});
    }
    std::unique_ptr<neura::tooling::TestOutputCapture> captured;
    std::vector<std::chrono::steady_clock::time_point> started(tests.size());
    const auto begin = [&](std::size_t i) {
        if (!reporter) return;
        reporter->event("testStart", neura::tooling::test_info_json(catalog[i]));
        started[i] = std::chrono::steady_clock::now();
        captured = std::make_unique<neura::tooling::TestOutputCapture>(
            [&, i](std::string_view stream, std::string_view text) {
                reporter->event("output", neura::tooling::test_output_json(catalog[i].id, stream, text));
            });
    };
#ifdef NEURA_HAS_SQL
    neura::sql::LocalSql sql(options.sql_pools);
    Host host(options.capabilities, nullptr, neura::nvm::default_response_bytes, sql.exchange());
#else
    if (!options.sql_pools.empty()) throw std::runtime_error("this build has no SQL providers");
    Host host(options.capabilities);
#endif
    provide_native_library(image, host.services);
    const auto bindings = neura::nvm::bind(image, host.registry);
    std::size_t failed = 0;
    std::optional<std::size_t> saved_schedule_test;
    // `note` follows the test's name: which schedule failed, or how many ran.
    const auto finished = [&](std::size_t i, const neura::nvm::RunResult& result, const std::string& note = {},
                              const std::string& forced_failure = {}) {
        // Each test is its own run: files and sockets it left open close now.
        host.services.end_run();
        host.secrets.release_all();
        std::string message = forced_failure;
        if (message.empty() && result.completed && result.status != 0)
            message = "the test exited with status " + std::to_string(result.status);
        if (message.empty() && !result.completed) {
            if (const auto assertion = test_failure_message(result.trap, image, test_labels)) message = *assertion;
            else message = neura::nvm::describe(result.trap, image, false);
        }
        const bool success = message.empty();
        if (!success) ++failed;
        if (reporter) {
            if (captured) {
                captured->finish([&](std::string_view stream, std::string_view text) {
                    reporter->event("output", neura::tooling::test_output_json(catalog[i].id, stream, text));
                });
                captured.reset();
            }
            const auto duration = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started[i]).count();
            neura::tooling::Json event{{"id", catalog[i].id}, {"name", catalog[i].name},
                {"state", success ? "passed" : "failed"}, {"duration", duration},
                {"steps", result.steps}, {"peakHeapCells", result.peak_heap_cells},
                {"message", message}, {"note", note}};
            if (neura::nvr::uses_regions(image)) event["peakStackCells"] = result.peak_stack_cells;
            if (!success && !result.completed) {
                std::optional<neura::nvr::SourceLocation> place;
                if (test_failure_message(result.trap, image, test_labels)) {
                    std::vector<std::pair<std::uint32_t, std::uint32_t>> frames{
                        {result.trap.function, result.trap.block}};
                    frames.insert(frames.end(), result.trap.callers.begin(), result.trap.callers.end());
                    for (const auto& [function, block] : frames) {
                        const auto location = block_location(image, function, block);
                        if (location && test_labels.contains(location->path)) { place = location; break; }
                    }
                }
                if (!place) place = block_location(image, result.trap.function, result.trap.block);
                if (place) {
                    std::string file;
                    for (const auto& input : options.files) {
                        if (neura::backend::source_label(program, input.generic_string()) == place->path) {
                            file = input.generic_string(); break;
                        }
                    }
                    event["location"] = neura::tooling::Json{{"path", place->path}, {"file", file},
                                                           {"line", place->line}, {"column", place->column}};
                }
            }
            if (!success && saved_schedule_test == i && options.save_schedule)
                event["savedSchedule"] = fs::absolute(*options.save_schedule).generic_string();
            if (!success && options.schedule)
                event["savedSchedule"] = fs::absolute(*options.schedule).generic_string();
            reporter->event("testResult", std::move(event));
            return;
        }
        std::cout << (success ? "PASS " : "FAIL ") << names[i] << note;
        if (!success) std::cout << ": " << message;
        else if (options.stats)
            std::cout << " (steps " << result.steps << ", peak heap cells " << result.peak_heap_cells
                      << (neura::nvr::uses_regions(image) ? ", peak stack cells " + std::to_string(result.peak_stack_cells)
                                                          : std::string())
                      << ")";
        std::cout << '\n';
    };
    neura::runner::TaskDumps dumps(options.task_dump);
    auto limits = options.limits;
    limits.task_dump = dumps.request();
    if (coverage) limits.block_observer = [&](std::uint32_t function, std::uint32_t block) {
        coverage->block(function, block);
    };
    const bool fleet_device = options.schedule || options.schedules > 1 || options.virtual_time ||
                                      options.save_schedule
                                  ? false
                                  : gpu.device() != nullptr;
    const auto tiers = tier_cache(image, limits);
    // Without a device the tests run one after another on one timeline.
    if (fleet_device || !neura::runner::times_runs(limits)) times.reset();
    if (times) limits.tier_times = &*times;
    // One test, alone, under a chooser (null: the reference scheduler) and
    // its own virtual clock when virtual time is on.
    const auto run_test = [&](std::size_t i, neura::nvm::ScheduleChooser* chooser, std::uint64_t quantum,
                              bool virtual_time) {
        neura::nvm::VirtualTaskClock virtual_clock;
        neura::nvm::TaskClock* clock = virtual_time ? &virtual_clock : nullptr;
        host.services.set_clock(clock);
        auto test_limits = limits;
        test_limits.chooser = chooser;
        test_limits.task_quantum = quantum;
        // The start functions share the entry signature, so any of them can
        // be the entry of a validated image.
        image.entry = static_cast<std::uint32_t>(i);
        auto result = neura::nvm::run(image, bindings, {}, test_limits, clock);
        host.services.set_clock(nullptr);
        return result;
    };
    const auto passed = [](const neura::nvm::RunResult& result) { return result.completed && result.status == 0; };
    std::size_t ran = tests.size();
    if (options.schedule) {
        // Reproduction: the one test of the saved schedule, under exactly
        // its choices, quantum, and clock.
        const auto saved = neura::nvm::read_schedule(*options.schedule);
        const auto found = std::ranges::find(names, saved.test);
        if (found == names.end()) {
            throw std::runtime_error(options.schedule->string() + ": the schedule is for test " + saved.test +
                                     ", which these files do not define");
        }
        const auto i = static_cast<std::size_t>(found - names.begin());
        neura::nvm::RecordedChooser chooser(saved.choices);
        begin(i);
        const auto result = run_test(i, &chooser, saved.quantum, saved.virtual_time || options.virtual_time);
        if (chooser.diverged()) {
            finished(i, result, " [saved schedule]", "the schedule does not fit this program: at decision " +
                     std::to_string(chooser.diverged_tick()) + " fewer tasks could run than its choice needs");
        } else {
            finished(i, result, " [saved schedule]");
        }
        ran = 1;
    } else if (auto* device = options.schedules > 1 || options.virtual_time || options.save_schedule
                                  ? nullptr : gpu.device()) {
        if (reporter) {
            // A single member per fleet preserves GPU behavior while making
            // every native/console byte unambiguously attributable to a test.
            for (std::size_t i = 0; i < tests.size(); ++i) {
                begin(i);
                neura::nvm::Fleet fleet(image, {limits, device, 1});
                fleet.add(static_cast<std::uint32_t>(i), {});
                neura::nvm::run_fleet(fleet, bindings,
                    [&](std::size_t, const neura::nvm::RunResult& result) { finished(i, result); });
            }
        } else {
            neura::nvm::Fleet fleet(image, {limits, device});
            for (std::size_t i = 0; i < tests.size(); ++i) fleet.add(static_cast<std::uint32_t>(i), {});
            neura::nvm::run_fleet(fleet, bindings,
                                  [&](std::size_t i, const neura::nvm::RunResult& result) { finished(i, result); });
        }
    } else {
        // Schedule 1 is the reference scheduler's. Each later one varies
        // which runnable task goes next, from the seed. Exploration is
        // bounded: it says nothing about schedules it did not run.
        std::uint64_t explored = 0;
        bool saved = false;
        for (std::size_t i = 0; i < tests.size(); ++i) {
            begin(i);
            auto result = run_test(i, nullptr, limits.task_quantum, options.virtual_time);
            std::uint64_t count = 1;
            std::string note;
            neura::nvm::SavedSchedule failing{names[i], options.schedule_seed, limits.task_quantum,
                                              options.virtual_time, {}};
            for (std::uint64_t k = 1; passed(result) && k < options.schedules; ++k) {
                host.services.end_run();
                host.secrets.release_all();
                neura::nvm::ExploringChooser chooser(options.schedule_seed + k);
                result = run_test(i, &chooser, limits.task_quantum, options.virtual_time);
                // No decision offered a choice: the test has one schedule.
                if (chooser.offered() == 0) break;
                ++count;
                failing.seed = options.schedule_seed + k;
                failing.choices = chooser.choices();
            }
            explored += count;
            if (options.schedules > 1) {
                note = passed(result) ? " [" + std::to_string(count) + (count == 1 ? " schedule]" : " schedules]")
                                      : " [schedule " + std::to_string(count) + " of " +
                                            std::to_string(options.schedules) + ", seed " +
                                            std::to_string(failing.seed) + "]";
            }
            if (!passed(result) && options.save_schedule && !saved) {
                neura::nvm::write_schedule(*options.save_schedule, failing);
                saved = true;
                saved_schedule_test = i;
                std::cerr << "neura: note: the failing schedule of " << names[i] << " was written to "
                          << options.save_schedule->string() << '\n';
            }
            finished(i, result, note);
        }
        if (options.schedules > 1 && !reporter) {
            std::cout << "explored " << explored << " schedule(s) of " << tests.size() << " test(s), seed "
                      << options.schedule_seed << '\n';
        }
    }
    if (coverage) {
        const auto json = coverage->report().dump() + "\n";
        write_file(*options.test_coverage, std::vector<std::uint8_t>(json.begin(), json.end()));
        const auto path = fs::absolute(*options.test_coverage).generic_string();
        if (reporter) reporter->event("coverage", {{"path", path}, {"granularity", "mapped-block-line"}});
        else std::cerr << "neura: source-map line coverage was written to " << path << '\n';
    }
    if (reporter) reporter->event("summary", {{"total", ran}, {"passed", ran - failed}, {"failed", failed}});
    else std::cout << ran << " test(s), " << ran - failed << " passed, " << failed << " failed\n";
    std::cout.flush();
    report_tiers(options, tiers, times);
    return gpu.finish(failed == 0 ? 0 : 1, options.stats);
}

} // namespace neura::cli
