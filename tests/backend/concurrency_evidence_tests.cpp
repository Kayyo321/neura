// Scoped tasks from source to the NVM (docs/lang/Concurrency.md section 15,
// "Conversion and NVM"): every concurrent fixture is built with the real
// pipeline, optimized and not, and run on the NVM under each quantum and two
// task limits. The run must match the NeuraIR reference evaluator extended
// with the same scheduler, which shares no code with the converter or the NVM
// (tests/ir-optimization/evaluator.cpp): the same host requests in the same
// order, the same exit status or trap class, and the same schedule, tick for
// tick. A second run from clean state must repeat everything, and the same
// image run as three fleet members must give each member its serial result.
#include "fixtures.hpp"
#include "graph_test_support.hpp"
#include "ir-optimization/concurrency_support.hpp"

#include "backend/converter.hpp"
#include "ir-optimization/optimizer.hpp"
#include "nvm/fleet.hpp"

#include <algorithm>
#include <iostream>
#include <map>

namespace neura::testing {
namespace {

// Records every host request in order, as "console:<line>" and "exit:<status>",
// so that one sequence covers both services.
class LoggedConsole final : public nvm::HostService {
public:
    explicit LoggedConsole(std::vector<std::string>& log) : console_(sink_), log_(log) {}
    const nvm::HostSignature& signature() const override { return console_.signature(); }
    nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
        log_.push_back("console:" + request.arguments.at(0).bytes);
        return console_.invoke(request);
    }

private:
    nvm::StringSink sink_;
    nvm::ConsoleWriteLine console_;
    std::vector<std::string>& log_;
};

class LoggedExit final : public nvm::HostService {
public:
    explicit LoggedExit(std::vector<std::string>& log) : log_(log) {}
    const nvm::HostSignature& signature() const override { return signature_; }
    nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
        const auto status = static_cast<std::int32_t>(request.arguments.at(0).bits);
        log_.push_back("exit:" + std::to_string(status));
        return nvm::HostCompletion::terminate(status);
    }

private:
    using Kind = nvr::TypeKind;
    const nvm::HostSignature signature_{"neura.process.exit", 1, {{Kind::Signed, 32}}, {Kind::Void},
                                        {Kind::Unsigned, 32}};
    std::vector<std::string>& log_;
};

struct Observed {
    nvm::RunResult result;
    std::vector<std::string> events;
    std::vector<std::uint64_t> schedule;
};

Observed run_image(const nvr::Image& image, std::size_t quantum, std::size_t max_tasks) {
    Observed observed;
    LoggedConsole console(observed.events);
    LoggedExit exit(observed.events);
    nvm::HostRegistry registry;
    registry.add(console);
    registry.add(exit);
    nvm::RunLimits limits;
    limits.task_quantum = quantum;
    limits.max_tasks = max_tasks;
    limits.schedule = &observed.schedule;
    limits.profile = true;
    observed.result = nvm::run(image, nvm::bind(image, registry), {}, limits);
    return observed;
}

// The host requests the evaluator's effects stand for.
std::vector<std::string> expected_events(const Outcome& outcome) {
    std::vector<std::string> events;
    for (const auto& effect : outcome.effects) {
        if (effect.callee.ends_with("Console.write_line")) {
            events.push_back("console:" + effect.arguments.at(0).text);
        } else if (effect.callee.ends_with("Process.exit_process")) {
            events.push_back("exit:" + std::to_string(effect.arguments.at(0).magnitude));
        } else {
            fail("an effect the NVM test does not map: " + effect.callee);
        }
    }
    return events;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) text += (text.empty() ? "" : " | ") + line;
    return text;
}

std::string join_ticks(const std::vector<std::uint64_t>& ticks, std::size_t from, std::size_t count) {
    std::string text;
    for (std::size_t i = from; i < ticks.size() && i < from + count; ++i) {
        text += (text.empty() ? "" : " ") + std::to_string(ticks[i]);
    }
    return text;
}

// The first tick where two schedules differ, with some context, or empty.
std::string schedule_difference(const std::vector<std::uint64_t>& evaluator, const std::vector<std::uint64_t>& nvm) {
    std::size_t tick = 0;
    while (tick < evaluator.size() && tick < nvm.size() && evaluator[tick] == nvm[tick]) ++tick;
    if (tick == evaluator.size() && tick == nvm.size()) return {};
    return "first differing tick " + std::to_string(tick) + " (evaluator " + std::to_string(evaluator.size()) +
           " decisions, NVM " + std::to_string(nvm.size()) + "): evaluator [" + join_ticks(evaluator, tick, 6) +
           "] NVM [" + join_ticks(nvm, tick, 6) + "]";
}

nvm::TrapKind fault_kind(const std::string& reason) {
    if (reason.starts_with("invalid task operation")) return nvm::TrapKind::InvalidTaskOperation;
    if (reason.starts_with("deadlock")) return nvm::TrapKind::Deadlock;
    return nvm::TrapKind::Program;
}

// Requires the NVM run to match the evaluator's outcome.
void expect_same_as_evaluator(const IrProgram& program, const Outcome& expected, const Observed& actual,
                              const nvr::Image& image, const std::string& label) {
    const auto& result = actual.result;
    const auto stopped = [&] { return describe_trap(result, image); };
    switch (expected.status) {
    case Status::Returned:
        check(result.completed, label + ": evaluator returned, NVM " + stopped());
        check_equal(result.status, entry_status(program, expected).value_or(-1), label + " exit status");
        break;
    case Status::Exited:
        check(result.completed, label + ": evaluator exited, NVM " + stopped());
        check_equal(static_cast<std::uint64_t>(result.status), expected.exit_status, label + " exit status");
        break;
    case Status::Trapped:
        check(!result.completed && result.trap.kind == nvm::TrapKind::Program, label + ": evaluator trapped (" +
                                                                                   expected.reason + "), NVM " + stopped());
        check_equal(result.trap.reason, expected.trap_reason, label + " trap reason");
        break;
    case Status::LimitReached:
        check(!result.completed && result.trap.kind == nvm::TrapKind::TaskLimit,
              label + ": evaluator reached the " + expected.reason + ", NVM " + stopped());
        check_equal(nvm::exit_status(result.trap), 137, label + " task limit status");
        break;
    case Status::Faulted:
        check(!result.completed && result.trap.kind == fault_kind(expected.reason),
              label + ": evaluator faulted (" + expected.reason + "), NVM " + stopped());
        break;
    case Status::Unsupported:
        fail(label + ": the evaluator does not model the program: " + expected.reason);
    }
    check(actual.events == expected_events(expected),
          label + ": host requests differ\n  evaluator: " + join_lines(expected_events(expected)) +
              "\n  NVM:       " + join_lines(actual.events));
    const auto ticks = schedule_difference(expected.schedule, actual.schedule);
    check(ticks.empty(), label + ": schedules differ, " + ticks);
}

void expect_same_run(const Observed& actual, const Observed& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.result.peak_tasks, expected.result.peak_tasks, label + " peak tasks");
    check(actual.events == expected.events, label + ": host requests differ");
    check(actual.schedule == expected.schedule, label + ": schedules differ");
}

struct Built {
    ConcurrencyFixture fixture;
    IrProgram program;
    nvr::Image optimized;
    nvr::Image unoptimized;
};

const std::vector<Built>& built_fixtures(const Environment& environment) {
    static std::vector<Built> cache;
    if (!cache.empty()) return cache;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        Built item;
        item.fixture = fixture;
        item.program = prepare_concurrent(fixture.path);
        item.optimized = build_program({fixture.path}, {}, true);
        item.unoptimized = build_program({fixture.path}, {}, false);
        cache.push_back(std::move(item));
    }
    return cache;
}

} // namespace

NEURA_TEST(concurrency, nvm_matches_the_reference_scheduler_tick_for_tick) {
    std::size_t runs = 0;
    std::size_t ticks = 0;
    std::map<std::string, std::size_t> kinds;
    for (const auto& item : built_fixtures(environment)) {
        for (const auto max_tasks : concurrency_task_limits) {
            for (const auto quantum : concurrency_quanta) {
                const auto expected = run_concurrent(item.program, quantum, max_tasks);
                const auto base = item.fixture.name + " (quantum " + std::to_string(quantum) + ", at most " +
                                  std::to_string(max_tasks) + " tasks)";
                for (const bool optimized : {true, false}) {
                    const auto label = base + (optimized ? " optimized" : " unoptimized");
                    const auto& image = optimized ? item.optimized : item.unoptimized;
                    const auto first = run_image(image, quantum, max_tasks);
                    expect_same_as_evaluator(item.program, expected, first, image, label);
                    // A second run from clean state repeats everything,
                    // statistics included.
                    expect_same_run(run_image(image, quantum, max_tasks), first, label + " repeated");
                    ++runs;
                    ticks += first.schedule.size();
                }
                ++kinds[describe(expected.status)];
            }
        }
    }
    std::cout << "  " << runs << " NVM runs matched the reference scheduler over " << ticks << " scheduling decisions;"
              << " evaluator outcomes:";
    for (const auto& [kind, count] : kinds) std::cout << ' ' << count << ' ' << kind << ',';
    std::cout << '\n';
}

NEURA_TEST(concurrency, fixtures_make_the_schedule_depend_on_the_quantum_on_the_nvm) {
    bool differs = false;
    for (const auto& item : built_fixtures(environment)) {
        const auto small = run_image(item.optimized, 1, 65'536);
        const auto large = run_image(item.optimized, 1024, 65'536);
        check(large.schedule.size() > 1, item.fixture.name + ": the NVM schedule has a single entry");
        if (small.schedule != large.schedule) differs = true;
    }
    check(differs, "no fixture's NVM schedule depends on the quantum");
}

NEURA_TEST(concurrency, the_comparison_notices_a_different_quantum) {
    // The schedules are compared tick for tick; shifting the evaluator's
    // quantum by one against the NVM's must therefore fail the comparison on
    // a fixture that interleaves.
    const auto& fixtures = built_fixtures(environment);
    const auto fan_out = std::ranges::find_if(fixtures, [](const Built& item) { return item.fixture.name == "programs/fan-out"; });
    check(fan_out != fixtures.end(), "fan-out is a fixture");
    const auto evaluator = run_concurrent(fan_out->program, 2);
    const auto nvm = run_image(fan_out->optimized, 3, 65'536);
    check(!schedule_difference(evaluator.schedule, nvm.schedule).empty(),
          "quantum 2 and quantum 3 gave the same schedule on fan-out");
    const auto same = run_image(fan_out->optimized, 2, 65'536);
    check(schedule_difference(evaluator.schedule, same.schedule).empty(), "quantum 2 on both gives one schedule");
}

// A handle used after its scope closed is an invalid task operation. Lowering
// never writes that, so the program is edited: a join of task 1 is added after
// the scope of two-tasks closes.
NEURA_TEST(concurrency, a_handle_used_after_its_scope_is_an_invalid_task_operation_in_both) {
    auto program = prepare_concurrent(environment.fixtures() / "concurrency" / "two-tasks.nra");
    auto& body = body_of(program, "Main.main");
    const auto scope = std::ranges::find(body, IrOp::TaskScope, &IrNode::op);
    check(scope != body.end(), "main has a scope");
    const auto site = scope->span;
    const IrType uint64{TypeKind::Integer, "uint64", no_symbol, {}};
    // join(1) after the scope that owned task 1 has closed.
    IrNode number{IrOp::Integer, uint64, site, no_symbol, "1", {}, {}, {}, {}};
    IrNode join{IrOp::SchedulerCall, IrType{TypeKind::Integer, "int32", no_symbol, {}}, site, no_symbol, "join",
                {}, {}, {}, {}};
    join.operands.push_back(std::move(number));
    IrNode stale{IrOp::Discard, IrType{TypeKind::Void, "void", no_symbol, {}}, site, no_symbol, "", {}, {}, {}, {}};
    stale.operands.push_back(std::move(join));
    const auto after_scope = scope - body.begin() + 1;
    body.insert(body.begin() + after_scope, std::move(stale));
    auto optimized = program;
    optimize(optimized);

    for (const auto quantum : concurrency_quanta) {
        const auto expected = run_concurrent(program, quantum);
        check(expected.status == Status::Faulted &&
                  expected.reason.find("task handle is outside its owning scope") != std::string::npos,
              "the evaluator faults: " + describe(expected));
        check(difference(expected, run_concurrent(optimized, quantum)).empty(),
              "optimization changes the fault at quantum " + std::to_string(quantum));
        for (const auto* version : {&program, &optimized}) {
            const auto image = nvr::load(nvr::encode(backend::convert(*version).image));
            const auto actual = run_image(image, quantum, 65'536);
            const auto label = std::string("stale join, quantum ") + std::to_string(quantum);
            expect_same_as_evaluator(*version, expected, actual, image, label);
            check(actual.result.trap.detail.find("task handle is outside its owning scope") != std::string::npos,
                  label + ": the NVM names the same reason: " + actual.result.trap.detail);
        }
    }
}

NEURA_TEST(concurrency, fleet_members_match_serial_runs) {
    std::size_t members = 0;
    for (const auto& item : built_fixtures(environment)) {
        for (const auto quantum : concurrency_quanta) {
            const auto label = item.fixture.name + " quantum " + std::to_string(quantum);
            const auto serial = run_image(item.optimized, quantum, 65'536);
            nvm::RunLimits limits;
            limits.task_quantum = quantum;
            limits.profile = true;
            std::vector<std::uint64_t> shared_schedule;
            limits.schedule = &shared_schedule;
            nvm::Fleet fleet(item.optimized, {limits, nullptr, 4096});
            constexpr std::size_t member_count = 3;
            for (std::size_t member = 0; member < member_count; ++member) {
                check_equal(fleet.add(item.optimized.entry, {}), member, label + " member index");
            }
            std::vector<std::string> events;
            LoggedConsole console(events);
            LoggedExit exit(events);
            nvm::HostRegistry registry;
            registry.add(console);
            registry.add(exit);
            const auto bindings = nvm::bind(item.optimized, registry);
            std::size_t finished = 0;
            nvm::run_fleet(fleet, bindings, [&](std::size_t member, const nvm::RunResult& result) {
                check_equal(member, finished, label + " completion order");
                check_same_run(result, serial.result, label + " member " + std::to_string(member));
                check_equal(result.peak_tasks, serial.result.peak_tasks, label + " member peak tasks");
                ++finished;
            });
            check_equal(finished, member_count, label + " members reported");
            check(fleet.done(), label + " fleet completed");
            // Requests are served in member order: three copies of the serial
            // sequence, one member after another.
            std::vector<std::string> expected;
            for (std::size_t member = 0; member < member_count; ++member) {
                expected.insert(expected.end(), serial.events.begin(), serial.events.end());
            }
            check(events == expected, label + ": fleet host requests differ from three serial runs");
            // The members share one schedule recorder here, so only the number
            // of decisions is comparable with the serial runs.
            check_equal(shared_schedule.size(), member_count * serial.schedule.size(), label + " fleet decisions");
            members += member_count;
        }
    }
    std::cout << "  " << members << " fleet members matched their serial runs\n";
}

} // namespace neura::testing
