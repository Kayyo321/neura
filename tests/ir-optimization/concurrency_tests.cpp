// Scoped tasks under the optimizer (docs/lang/Concurrency.md sections 10.2
// and 15): the NeuraIR reference evaluator runs every concurrent fixture on
// its own scheduler before and after optimization, and the two runs must
// agree on results, traps, host effects, and the schedule, tick for tick.
// The evaluator shares no code with the optimizer, the converter, or the NVM.
#include "concurrency_support.hpp"

#include "ir-optimization/optimizer.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <chrono>
#include <thread>
#include <set>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace neura::testing {
namespace {

struct Run {
    std::size_t quantum = 0;
    std::size_t max_tasks = 0;
    Outcome before;
    Outcome after;
    Outcome again;
};

struct FixtureRuns {
    ConcurrencyFixture fixture;
    IrProgram unoptimized;
    IrProgram optimized;
    std::vector<Run> runs;
};

// An outcome the evaluator can stand behind: it ran the program to a result,
// a trap, a process exit, or the task limit, rather than giving up.
bool modeled(const Outcome& outcome) {
    return outcome.status == Status::Returned || outcome.status == Status::Trapped ||
           outcome.status == Status::Exited ||
           (outcome.status == Status::LimitReached && outcome.reason == "task limit");
}

// Evaluates every fixture once for all the tests of this suite.
const std::vector<FixtureRuns>& all_runs(const Environment& environment) {
    static std::vector<FixtureRuns> cache;
    static bool built = false;
    if (built) return cache;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        FixtureRuns item;
        item.fixture = fixture;
        item.unoptimized = prepare_concurrent(fixture.path);
        item.optimized = item.unoptimized;
        optimize(item.optimized);
        for (const auto max_tasks : concurrency_task_limits) {
            for (const auto quantum : concurrency_quanta) {
                Run run;
                run.quantum = quantum;
                run.max_tasks = max_tasks;
                run.before = run_concurrent(item.unoptimized, quantum, max_tasks);
                run.after = run_concurrent(item.optimized, quantum, max_tasks);
                run.again = run_concurrent(item.unoptimized, quantum, max_tasks);
                item.runs.push_back(std::move(run));
            }
        }
        cache.push_back(std::move(item));
    }
    built = true;
    return cache;
}

std::string label(const FixtureRuns& item, const Run& run) {
    return item.fixture.name + " (quantum " + std::to_string(run.quantum) + ", at most " +
           std::to_string(run.max_tasks) + " tasks)";
}

// ---- Structural evidence ----------------------------------------------------

// A scheduler operation or a poll, identified by what it is and where it was
// written. Specialized copies of one source function share their spans, so
// the counts below are multisets.
using Key = std::string;
using Counts = std::map<Key, std::size_t>;

struct Census {
    // Every such node of the program.
    Counts all;
    // Those the reference evaluator's own rules can reach: not after a
    // return, break, or continue of the same region, not in the arm of a
    // branch or select whose condition is a closed constant expression that
    // chooses the other arm, not in the right operand of a short circuit its
    // closed left operand decides, and not in a loop whose closed condition
    // is false.
    Counts live;
};

Key key_of(const IrNode& node) {
    return std::to_string(static_cast<int>(node.op)) + (node.poll ? "!" : "") + " '" + node.text + "' " +
           describe(node.span) + "+" + std::to_string(node.span.offset);
}

bool interesting(const IrNode& node) {
    return node.poll || node.op == IrOp::Spawn || node.op == IrOp::TaskScope || node.op == IrOp::SchedulerCall;
}

bool closed_constant(const IrNode& node) {
    switch (node.op) {
    case IrOp::Integer:
    case IrOp::Boolean:
        return true;
    case IrOp::Unary:
    case IrOp::Binary:
    case IrOp::Convert:
        return std::ranges::all_of(node.operands, closed_constant);
    case IrOp::ShortCircuit:
        return std::ranges::all_of(node.operands, closed_constant) && std::ranges::all_of(node.body, closed_constant);
    default:
        return false;
    }
}

struct Walker {
    const IrProgram& program;
    Evaluator evaluator;
    Census census;

    explicit Walker(const IrProgram& p) : program(p), evaluator(p) {}

    // The value of a closed constant condition, as the reference evaluator
    // computes it.
    std::optional<bool> constant(const IrNode& condition) {
        if (!closed_constant(condition)) return std::nullopt;
        const auto outcome = evaluator.evaluate(condition);
        if (outcome.status != Status::Returned || !outcome.result || outcome.result->kind != Value::Kind::Bool) {
            return std::nullopt;
        }
        return outcome.result->boolean;
    }

    void visit(const IrNode& node, bool live) {
        // A loop's poll is counted when its back edge is taken, which a loop
        // whose closed condition is false never does.
        const bool never_loops = node.op == IrOp::Loop && constant(node.operands.at(0)) == std::optional{false};
        if (interesting(node)) {
            ++census.all[key_of(node)];
            if (live && !never_loops) ++census.live[key_of(node)];
        }
        switch (node.op) {
        case IrOp::Branch: {
            visit_all(node.operands, live);
            const auto taken = constant(node.operands.at(0));
            region(node.body, live && (!taken || *taken));
            region(node.alternative, live && (!taken || !*taken));
            return;
        }
        case IrOp::SelectValue: {
            visit_all(node.operands, live);
            const auto taken = constant(node.operands.at(0));
            region(node.body, live && (!taken || *taken));
            region(node.alternative, live && (!taken || !*taken));
            return;
        }
        case IrOp::Loop: {
            visit_all(node.operands, live);
            const auto taken = constant(node.operands.at(0));
            const bool runs = live && !(taken && !*taken);
            region(node.body, runs);
            region(node.alternative, runs);
            return;
        }
        case IrOp::ShortCircuit: {
            visit_all(node.operands, live);
            const auto left = constant(node.operands.at(0));
            const bool decides = left && (*left == (node.text == "||"));
            region(node.body, live && !decides);
            return;
        }
        default:
            visit_all(node.operands, live);
            region(node.body, live);
            region(node.alternative, live);
        }
    }

    void visit_all(const std::vector<IrNode>& nodes, bool live) {
        for (const auto& node : nodes) visit(node, live);
    }

    // Statements after a return, break, or continue never run.
    void region(const std::vector<IrNode>& nodes, bool live) {
        for (const auto& node : nodes) {
            visit(node, live);
            if (node.op == IrOp::Return || node.op == IrOp::Break || node.op == IrOp::Continue) live = false;
        }
    }
};

Census census_of(const IrProgram& program) {
    Walker walker(program);
    for (const auto& body : program.bodies) walker.region(body.instructions, true);
    return std::move(walker.census);
}

std::string source_line(const IrProgram&, const Key& key) { return key; }

} // namespace

NEURA_TEST(concurrency, optimized_and_unoptimized_agree_under_every_quantum) {
    std::size_t compared = 0;
    for (const auto& item : all_runs(environment)) {
        for (const auto& run : item.runs) {
            const auto name = label(item, run);
            check(modeled(run.before), name + ": the evaluator did not model the program: " + describe(run.before));
            const auto diff = difference(run.before, run.after);
            check(diff.empty(), name + ": " + diff + "\n  unoptimized: " + describe(run.before) +
                                    "\n  optimized:   " + describe(run.after));
            // Evaluating twice from the same state must repeat exactly.
            const auto repeat = difference(run.before, run.again);
            check(repeat.empty(), name + ": a second run differs: " + repeat);
            ++compared;
        }
    }
    std::cout << "  " << compared << " runs compared before and after optimization\n";
}

// The optimizer treats a scheduler operation and a poll as an effect. This
// compares the structure, which the run above only samples: every spawn,
// scope, scheduler call, and polled loop or call that the reference
// evaluator's rules can reach is still there after optimization, and the
// optimizer added none. That is sound as a statement about the schedule
// because a node the evaluator can never reach contributes no tick, no spawn
// number, and no poll count to any run, so removing it cannot change one,
// while every reachable one is kept in the same function with its source
// span (specialized copies of one function share spans, so the comparison
// counts them). It does not show that the kept nodes keep their order; the
// runs above, which compare effects and the schedule, do that.
NEURA_TEST(concurrency, optimizer_keeps_every_reachable_scheduler_operation_and_poll) {
    std::size_t removed_dead = 0;
    std::size_t kept = 0;
    std::set<std::string> changed;
    for (const auto& item : all_runs(environment)) {
        const auto before = census_of(item.unoptimized);
        const auto after = census_of(item.optimized);
        for (const auto& [key, count] : before.live) {
            const auto found = after.all.find(key);
            const std::size_t remaining = found == after.all.end() ? 0 : found->second;
            check(remaining >= count, item.fixture.name + ": the optimizer removed a reachable node: " +
                                          source_line(item.unoptimized, key) + " (" + std::to_string(count) +
                                          " before, " + std::to_string(remaining) + " after)");
            kept += count;
        }
        for (const auto& [key, count] : after.all) {
            const auto found = before.all.find(key);
            const std::size_t original = found == before.all.end() ? 0 : found->second;
            check(count <= original, item.fixture.name + ": the optimizer added a node: " +
                                         source_line(item.optimized, key) + " (" + std::to_string(original) +
                                         " before, " + std::to_string(count) + " after)");
        }
        for (const auto& [key, count] : before.all) {
            const auto live = before.live.find(key);
            const auto found = after.all.find(key);
            const std::size_t original_dead = count - (live == before.live.end() ? 0 : live->second);
            const std::size_t remaining = found == after.all.end() ? 0 : found->second;
            if (remaining < count) {
                // Whatever was removed beyond the reachable ones was dead.
                check(count - remaining <= original_dead, item.fixture.name + ": removed more than the dead nodes");
                removed_dead += count - remaining;
            }
        }
        if (printed(item.unoptimized) != printed(item.optimized)) changed.insert(item.fixture.name);
    }
    std::cout << "  " << kept << " reachable scheduler operations and polls kept, " << removed_dead
              << " unreachable ones removed; the optimizer rewrote " << changed.size() << " fixture(s)\n";
    check(kept > 0, "no reachable scheduler operation was counted");
    check(removed_dead > 0, "the optimizer removed no unreachable scheduler operation or poll, so the check that "
                            "it keeps the reachable ones was never exercised against a removal");
    check(changed.contains("ir/fold-and-dead"), "the optimizer left ir/fold-and-dead unchanged");
    check(changed.contains("ir/short-circuit"), "the optimizer left ir/short-circuit unchanged");
}

// The checks above would pass for programs that never interleave.
NEURA_TEST(concurrency, fixtures_exercise_the_scheduler) {
    bool quantum_matters = false;
    std::set<Status> statuses;
    bool task_limit = false;
    for (const auto& item : all_runs(environment)) {
        const Run* first = nullptr;
        const Run* last = nullptr;
        for (const auto& run : item.runs) {
            statuses.insert(run.before.status);
            if (run.before.status == Status::LimitReached) task_limit = true;
            if (run.max_tasks != concurrency_task_limits.front()) continue;
            check(run.before.schedule.size() > 1, label(item, run) + ": the schedule has " +
                                                      std::to_string(run.before.schedule.size()) + " entry");
            if (run.quantum == concurrency_quanta.front()) first = &run;
            if (run.quantum == concurrency_quanta.back()) last = &run;
        }
        check(first && last, item.fixture.name + ": quantum 1 and the default quantum both ran");
        if (first->before.schedule != last->before.schedule) quantum_matters = true;
    }
    check(quantum_matters, "no fixture's schedule depends on the quantum");
    check(statuses.contains(Status::Returned), "no fixture returned");
    check(statuses.contains(Status::Trapped), "no fixture trapped");
    check(statuses.contains(Status::Exited), "no fixture ended with a process exit");
    check(task_limit, "no fixture reached the task limit");
}

// An expectation derived by hand from section 10.2, not from either
// implementation: at the default quantum nothing polls out, so the root parks
// at its first join, task 1 runs and finishes (waking the root behind task
// 2), task 2 runs and finishes, and the root completes.
NEURA_TEST(concurrency, hand_derived_schedule_of_two_tasks) {
    const auto fixture = environment.fixtures() / "concurrency" / "two-tasks.nra";
    for (const bool optimized : {false, true}) {
        auto program = prepare_concurrent(fixture);
        if (optimized) optimize(program);
        const auto outcome = run_concurrent(program, 1024);
        check_equal(outcome.status == Status::Returned, true, "two-tasks returned: " + describe(outcome));
        check(outcome.schedule == std::vector<std::uint64_t>({1, 2, 0}), "schedule of two-tasks: " + describe(outcome));
        check_equal(outcome.effects.size(), std::size_t{2}, "two console writes");
        check_equal(outcome.effects[0].arguments.at(0).text, std::string("a"), "task 1 writes first");
        check_equal(outcome.effects[1].arguments.at(0).text, std::string("b"), "task 2 writes second");
        check_equal(entry_status(program, outcome).value_or(-1), 0, "ExitCode.Ok");
    }
}

namespace {

// Clears the poll of every node `which` selects.
template <typename Select>
IrProgram without_polls(IrProgram program, Select which) {
    const std::function<void(IrNode&)> clear = [&](IrNode& node) {
        if (which(node)) node.poll = false;
        for (auto* region : {&node.operands, &node.body, &node.alternative}) {
            for (auto& child : *region) clear(child);
        }
    };
    for (auto& body : program.bodies) {
        for (auto& instruction : body.instructions) clear(instruction);
    }
    return program;
}

} // namespace

// The comparisons above can only fail if the evaluator notices what a lost
// poll does to the schedule. Remove the polls the compiler placed, as a
// buggy pass would, and the schedule at quantum 1 must change.
NEURA_TEST(concurrency, the_comparison_notices_a_removed_poll) {
    const auto program = prepare_concurrent(environment.source_dir / "tests" / "concurrency" / "programs" / "fan-out.nra");
    const auto reference = run_concurrent(program, 1);
    const auto loops = without_polls(program, [](const IrNode& node) { return node.op == IrOp::Loop; });
    const auto calls = without_polls(program, [](const IrNode& node) { return node.op != IrOp::Loop; });
    const auto none = without_polls(program, [](const IrNode&) { return true; });
    for (const auto* mutant : {&loops, &calls, &none}) {
        const auto outcome = run_concurrent(*mutant, 1);
        check(!difference(reference, outcome).empty(), "removing polls did not change the outcome at quantum 1");
    }
    // At the default quantum the fixture does not reach a poll limit, so
    // removing polls changes nothing there; the quantum is what makes polls matter.
    check(difference(run_concurrent(program, 1024), run_concurrent(none, 1024)).empty(),
          "polls changed the schedule at the default quantum");
}

// The evaluator runs each task on its own thread. Every run, whether it ends
// by returning, trapping, a process exit, or the task limit, must leave none
// alive and none unjoined (a std::thread destroyed while joinable terminates
// the process, so an unjoined one would end this test run). Only a thread that
// is still alive shows in /proc/self/task, so that is what is counted.
NEURA_TEST(concurrency, every_task_thread_is_joined_on_every_exit_path) {
#ifdef __APPLE__
    // macOS has no procfs: the kernel lists the task's threads instead.
    const auto threads = [] {
        thread_act_array_t list = nullptr;
        mach_msg_type_number_t count = 0;
        if (::task_threads(::mach_task_self(), &list, &count) != KERN_SUCCESS) return std::optional<std::size_t>{};
        for (mach_msg_type_number_t i = 0; i < count; ++i) ::mach_port_deallocate(::mach_task_self(), list[i]);
        ::vm_deallocate(::mach_task_self(), reinterpret_cast<vm_address_t>(list), count * sizeof(thread_act_t));
        return std::optional<std::size_t>{count};
    };
#else
    const auto threads = [] {
        std::size_t count = 0;
        std::error_code error;
        for (const auto& entry : fs::directory_iterator("/proc/self/task", error)) {
            (void)entry;
            ++count;
        }
        return error ? std::optional<std::size_t>{} : std::optional{count};
    };
#endif
    const auto start = threads();
    std::set<Status> exits;
    for (const auto& fixture : concurrency_fixtures(environment)) {
        const auto program = prepare_concurrent(fixture.path);
        for (const auto max_tasks : concurrency_task_limits) {
            for (const auto quantum : concurrency_quanta) {
                const auto outcome = run_concurrent(program, quantum, max_tasks);
                exits.insert(outcome.status);
                if (start) {
                    // The kernel drops a finished thread's entry a moment after
                    // join returns.
                    for (int attempt = 0; attempt < 200 && *threads() != *start; ++attempt) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    check_equal(*threads(), *start, fixture.name + ": task threads left after the run");
                }
            }
        }
    }
    check(exits.contains(Status::Returned) && exits.contains(Status::Trapped) && exits.contains(Status::Exited) &&
              exits.contains(Status::LimitReached),
          "the runs did not cover returning, trapping, exiting, and the task limit");
    if (!start) std::cout << "  this platform lists no threads: thread counts were not checked\n";
}

} // namespace neura::testing
