// Tier 2 function programs (src/nvm/tier2.hpp, Region::program): a run
// whose functions run as programs, with calls and returns inside them, is
// observably the same as one at tier 0, wherever a limit is reached, a
// callee traps, a task parks, or the shadow oracle looks. Part of the
// `tier` suite (backend-tier).
#include "graph_test_support.hpp"

#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"

#include <algorithm>
#include <functional>

namespace neura::testing {
namespace {

using Perturb = nvm::TierShadowHooks::Perturb;

// Lifts every block, forms every program the first time a block runs.
nvm::TierOptions eager_programs() {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    options.programs = true;
    return options;
}

nvm::RunLimits limits_at(std::uint8_t tier, nvm::TierCache* cache = nullptr) {
    nvm::RunLimits limits;
    limits.max_steps = 20'000'000;
    limits.tier = tier;
    limits.tier_cache = cache;
    limits.profile = true;
    return limits;
}

bool programs_run() { return nvm::tier_ceiling() >= 2; }

void check_same(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
    for (std::size_t i = 0; i < std::min(actual.requests.size(), expected.requests.size()); ++i) {
        check_same_request(actual.requests[i], expected.requests[i], label);
    }
}

using Adjust = std::function<void(nvm::RunLimits&)>;

// Runs `image` at tier 0 and at tier 2 with `cache` under `adjust`.
void compare_with_tier_0(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                         const std::vector<std::string>& arguments, const Adjust& adjust = {}) {
    auto reference_limits = limits_at(0);
    if (adjust) adjust(reference_limits);
    const auto reference = run_captured(image, reference_limits, std::nullopt, arguments);
    auto program_limits = limits_at(2, &cache);
    if (adjust) adjust(program_limits);
    check_same(run_captured(image, program_limits, std::nullopt, arguments), reference, label);
}

fs::path tier_program(const Environment& environment, const std::string& name) {
    return backend_dir(environment) / "tier" / (name + ".nra");
}

struct Hooks {
    nvm::TierShadowHooks hooks;
    std::vector<std::string> reports;
    std::vector<std::string> notices;

    Hooks() {
        hooks.report = [this](const std::string& text) { reports.push_back(text); };
        hooks.notice = [this](const std::string& text) { notices.push_back(text); };
    }
    Hooks(const Hooks&) = delete;
    Hooks& operator=(const Hooks&) = delete;
};

// A run under the shadow oracle (NEURA_TIER_CHECK=2) with programs: no
// difference, and the same run as without the oracle. Returns the oracle's
// sync points.
std::uint64_t check_clean(const nvr::Image& image, const std::string& label, const std::vector<std::string>& arguments,
                          const Adjust& adjust = {}) {
    nvm::TierCache plain_cache(image, eager_programs());
    auto plain_limits = limits_at(2, &plain_cache);
    if (adjust) adjust(plain_limits);
    const auto plain = run_captured(image, plain_limits, std::nullopt, arguments);
    nvm::TierCache cache(image, eager_programs());
    auto limits = limits_at(2, &cache);
    if (adjust) adjust(limits);
    Hooks hooks;
    limits.tier_shadow = &hooks.hooks;
    const auto checked = run_captured(image, limits, std::nullopt, arguments);
    check(hooks.reports.empty(), label + ": no difference" + (hooks.reports.empty() ? "" : ": " + hooks.reports[0]));
    check(hooks.notices.empty(), label + ": no notice");
    check_same(checked, plain, label + " with the oracle");
    if (programs_run()) check(hooks.hooks.syncs > 0, label + ": the oracle reached sync points");
    return hooks.hooks.syncs;
}

const std::vector<std::string> trap_kinds{"overflow", "index", "nil", "field"};

} // namespace

NEURA_TEST(tier, function_programs_have_their_entries_and_early_reads) {
    const auto image = build_program({tier_program(environment, "program-calls")});
    nvm::TierCache cache(image, eager_programs());
    run_captured(image, limits_at(2, &cache), std::nullopt, {"calls", "3"});
    if (!programs_run()) return;
    const auto statistics = cache.statistics();
    check(statistics.programs > 0, "functions got programs");
    check(statistics.region_counts.calls > 0 && statistics.region_counts.returns > 0,
          "calls and returns ran inside programs");
    std::size_t formed = 0;
    std::size_t later_reads = 0;
    for (std::uint32_t function = 0; function < image.functions.size(); ++function) {
        const auto formation = nvm::tier2::form_program(image, function, [&](std::uint32_t block) {
            nvm::tier2::BlockForm form;
            const auto& decoded = image.functions[function].blocks[block];
            const auto& graph = image.graphs[decoded.graph];
            if (graph.nodes.empty()) {
                if (!graph.outputs.empty()) form.kind = nvm::tier2::BlockForm::Kind::Wired;
                else if (decoded.exits.size() == 1) form.kind = nvm::tier2::BlockForm::Kind::Empty;
                return form;
            }
            const auto settled = cache.lookup(function, block);
            if (!settled.settled) form.pending = true;
            else if (settled.block) {
                form.kind = nvm::tier2::BlockForm::Kind::Lifted;
                form.lifted = settled.block;
            }
            return form;
        }, false);
        if (!formation.region) continue;
        const auto& program = *formation.region;
        ++formed;
        check(program.program && !program.entries.empty() && program.entries.front().block == 0,
              "a program's first entry is the function's entry");
        check(std::is_sorted(program.entries.begin(), program.entries.end(),
                             [](const auto& a, const auto& b) { return a.block < b.block; }),
              "entries ascend by block");
        // At the function's entry only parameters are initialized: an
        // early read there of any other slot could never be entered.
        for (const auto slot : program.entries.front().reads) {
            check(slot < program.parameter_count, "entry 0 reads only parameters");
        }
        for (std::size_t e = 1; e < program.entries.size(); ++e) {
            for (const auto slot : program.entries[e].reads) later_reads += slot >= program.parameter_count;
        }
        for (const auto& op : program.code) {
            if (op.code == nvm::tier2::Code::Call) {
                check(program.entry_at(program.exits[op.index]->resume) != nullptr, "a call's resume is an entry");
            }
        }
        check_equal(program.block_labels.size(), image.functions[function].blocks.size(), "every block has a label");
    }
    check(formed > 3, "the program's functions formed programs");
    check(later_reads > 0, "a loop head or a resume reads a local its entry 0 does not");
}

NEURA_TEST(tier, calls_and_returns_in_programs_match_tier_0_at_every_step_and_frame_limit) {
    // A loop of calls, a console write in a callee, a recursive function, a
    // mutually recursive pair, and interface calls in a loop.
    const auto image = build_program({tier_program(environment, "program-calls")});
    nvm::TierCache cache(image, eager_programs());
    const std::vector<std::string> arguments{"calls", "2"};
    compare_with_tier_0(image, cache, "calls", arguments);
    const auto steps = run_captured(image, limits_at(0), std::nullopt, arguments).result.steps;
    for (std::uint64_t limit = 1; limit <= steps + 1; ++limit) {
        compare_with_tier_0(image, cache, "calls with " + std::to_string(limit) + " steps", arguments,
                            [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
    }
    for (std::uint32_t frames = 1; frames <= 16; ++frames) {
        compare_with_tier_0(image, cache, "calls with " + std::to_string(frames) + " frames", arguments,
                            [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
    }
    if (programs_run()) {
        const auto statistics = cache.statistics();
        check(statistics.region_counts.calls > 0 && statistics.region_counts.returns > 0,
              "calls and returns ran inside programs");
        check(statistics.region_counts.budget > 0, "step limits inside programs went to tier 1");
        check(statistics.region_counts.traps > 0, "a frame limit trapped in place at a call in a program");
    }
}

NEURA_TEST(tier, traps_inside_callees_two_and_three_frames_deep_match_tier_0) {
    const auto image = build_program({tier_program(environment, "program-calls")});
    for (const auto& kind : trap_kinds) {
        nvm::TierCache cache(image, eager_programs());
        const std::vector<std::string> arguments{kind, "4"};
        compare_with_tier_0(image, cache, kind, arguments);
        const auto steps = run_captured(image, limits_at(0), std::nullopt, arguments).result.steps;
        for (std::uint64_t limit = steps > 40 ? steps - 40 : 1; limit <= steps + 1; ++limit) {
            compare_with_tier_0(image, cache, kind + " with " + std::to_string(limit) + " steps", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
        for (std::uint32_t frames = 1; frames <= 6; ++frames) {
            compare_with_tier_0(image, cache, kind + " with " + std::to_string(frames) + " frames", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
        }
        const auto trapped = run_captured(image, limits_at(0), std::nullopt, arguments).result;
        check(!trapped.completed && trapped.trap.callers.size() >= 3, kind + " traps three frames deep");
    }
}

NEURA_TEST(tier, tasks_calling_functions_keep_their_schedules_under_quanta) {
    const auto image = build_program({tier_program(environment, "program-tasks")});
    for (const std::uint64_t quantum : {1ULL, 2ULL, 3ULL, 4ULL}) {
        nvm::TierCache cache(image, eager_programs());
        std::vector<std::uint64_t> reference_schedule;
        std::vector<std::uint64_t> program_schedule;
        const auto label = "tasks with quantum " + std::to_string(quantum);
        compare_with_tier_0(image, cache, label, {}, [&](nvm::RunLimits& limits) {
            limits.task_quantum = quantum;
            limits.schedule = limits.tier == 0 ? &reference_schedule : &program_schedule;
        });
        check(reference_schedule == program_schedule, label + ": the same schedule");
        if (programs_run()) {
            const auto counts = cache.statistics().region_counts;
            // A task parks inside `deliver` (its send) and its return later
            // goes on in `produce`'s program: the frame kept the program
            // it was called from while it waited on the task's stack.
            check(counts.returns > counts.leaves / 4, label + ": returns went on inside programs");
        }
    }
}

NEURA_TEST(tier, the_shadow_oracle_checks_programs_and_reports_a_difference_three_calls_deep) {
    const auto image = build_program({tier_program(environment, "program-calls")});
    std::uint64_t syncs = check_clean(image, "calls", {"calls", "4"});
    for (const auto& kind : trap_kinds) syncs += check_clean(image, kind, {kind, "4"});
    const auto tasks = build_program({tier_program(environment, "program-tasks")});
    for (const std::uint64_t quantum : {1ULL, 2ULL, 3ULL, 4ULL}) {
        syncs += check_clean(tasks, "tasks with quantum " + std::to_string(quantum), {},
                             [&](nvm::RunLimits& limits) { limits.task_quantum = quantum; });
    }
    check(syncs > 0, "sync points");
    if (!programs_run()) return;
    // The console write in `report`, called from `loop` called from main, is
    // a sync point with three frames: a slot flipped there is reported.
    nvm::TierCache cache(image, eager_programs());
    auto limits = limits_at(2, &cache);
    Hooks hooks;
    hooks.hooks.perturb = Perturb::SlotValue;
    hooks.hooks.at_step = run_captured(image, limits_at(0), std::nullopt, {"calls", "4"}).result.steps / 3;
    limits.tier_shadow = &hooks.hooks;
    run_captured(image, limits, std::nullopt, {"calls", "4"});
    check(hooks.hooks.perturbed, "the run was perturbed");
    check_equal(hooks.reports.size(), std::size_t{1}, "one report");
    if (!hooks.reports.empty()) {
        // "...: frame N slot S: ..." with N, counted from the outermost
        // frame, at least 2: three calls deep or more.
        const auto& report = hooks.reports.front();
        const auto at = report.find("): frame ");
        check(at != std::string::npos && report.find(" slot ", at) != std::string::npos &&
                  std::stoul(report.substr(at + 9)) >= 2,
              "the report names a slot of a frame three calls deep: " + report);
    }
}

NEURA_TEST(tier, a_block_observer_runs_below_tier_2) {
    // Regions and programs do not call the observer for their blocks.
    const auto image = build_program({tier_program(environment, "program-calls")});
    std::vector<std::pair<std::uint32_t, std::uint32_t>> reference;
    auto reference_limits = limits_at(0);
    reference_limits.block_observer = [&](std::uint32_t f, std::uint32_t b) { reference.emplace_back(f, b); };
    run_captured(image, reference_limits, std::nullopt, {"calls", "3"});
    nvm::TierCache cache(image, eager_programs());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> observed;
    auto limits = limits_at(2, &cache);
    limits.block_observer = [&](std::uint32_t f, std::uint32_t b) { observed.emplace_back(f, b); };
    run_captured(image, limits, std::nullopt, {"calls", "3"});
    check(!reference.empty() && observed == reference, "the observer sees every block, in order, at tier 2 too");
    check_equal(cache.statistics().steps.region, std::uint64_t{0}, "no block ran in a region or program");
}

} // namespace neura::testing
