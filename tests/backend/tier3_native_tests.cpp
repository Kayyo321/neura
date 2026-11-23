// Tier 3 machine code (src/jit/tier3_native.hpp, NEURA_TIER3=native): every
// program of tests/backend/tier run with native programs as machine code
// from their first checkpoint, with --profile, compared with tier 0 at every
// step limit from 1 to the run's steps (a deoptimization at every
// checkpoint), every frame limit up to the run's depth + 2, and every heap
// limit from the first allocation to the peak + 2 (helpers trap in place);
// a float program refused in a rounding mode other than the default; a
// hoisted guard that fails at an entry in the middle of a loop; and the
// statistics line of native programs. Part of the `tier3` suite
// (backend-tier3).
#include "graph_test_support.hpp"

#include "decompiler/assembly.hpp"
#include "jit/tier3_native.hpp"
#include "nvm/tier1.hpp"
#include "nvm/tier3.hpp"

#include <cfenv>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>

namespace neura::testing {
namespace {

namespace t3 = nvm::tier3;

// Lifts every block, forms every program at the first arrival, and turns
// every program native at its first checkpoint, with machine code.
nvm::TierOptions machine_options() {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    options.programs = nvm::TierOptions::Programs::On;
    options.tier3 = nvm::TierOptions::Tier3::Native;
    options.native_after = 1;
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

bool machine_code_runs() { return nvm::tier_ceiling() >= 2 && jit::native::supported(); }

void check_same(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
    for (std::size_t i = 0; i < std::min(actual.requests.size(), expected.requests.size()); ++i) {
        check_same_request(actual.requests[i], expected.requests[i], label);
    }
}

using Adjust = std::function<void(nvm::RunLimits&)>;

Captured run_at(const nvr::Image& image, std::uint8_t tier, nvm::TierCache* cache,
                const std::vector<std::string>& arguments, const Adjust& adjust) {
    auto limits = limits_at(tier, cache);
    if (adjust) adjust(limits);
    return run_captured(image, limits, std::nullopt, arguments);
}

void compare(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
             const std::vector<std::string>& arguments, const Adjust& adjust = {}) {
    check_same(run_at(image, 2, &cache, arguments, adjust), run_at(image, 0, nullptr, arguments, adjust), label);
}

// The smallest frame limit at which tier 0 runs as without one.
std::uint32_t depth_of(const nvr::Image& image, const std::vector<std::string>& arguments) {
    const auto whole = run_at(image, 0, nullptr, arguments, {}).result;
    for (std::uint32_t frames = 1; frames < 4096; ++frames) {
        const auto limited =
            run_at(image, 0, nullptr, arguments, [&](nvm::RunLimits& limits) { limits.max_frames = frames; }).result;
        if (limited.trap.kind != nvm::TrapKind::FrameLimit && limited.steps == whole.steps) return frames;
    }
    return 4096;
}

// The limits a sweep from `first` to `last` tries: every value, except in
// a range longer than `every`, where it tries every value of the first
// `every` and of the last `every / 4`, and one in 17 between them (a run of
// tests/backend/tier/regions.nra has 9088 steps, and every step at tier 0
// as well would take minutes).
std::vector<std::uint64_t> limits_from(std::uint64_t first, std::uint64_t last, std::uint64_t every) {
    std::vector<std::uint64_t> values;
    for (auto value = first; value <= last; ++value) {
        const auto offset = value - first;
        if (last - first <= every || offset < every || last - value < every / 4 || offset % 17 == 0) {
            values.push_back(value);
        }
    }
    return values;
}

// The full run, then step, frame and heap limits, all with one cache.
void sweep(const nvr::Image& image, const std::string& label, const std::vector<std::string>& arguments,
           bool tasks = false) {
    nvm::TierCache cache(image, machine_options());
    compare(image, cache, label, arguments);
    const auto whole = run_at(image, 0, nullptr, arguments, {}).result;
    for (const auto steps : limits_from(1, whole.steps + 1, 3000)) {
        compare(image, cache, label + " with " + std::to_string(steps) + " steps", arguments,
                [&](nvm::RunLimits& limits) { limits.max_steps = steps; });
    }
    const auto depth = depth_of(image, arguments);
    for (std::uint32_t frames = 1; frames <= depth + 2; ++frames) {
        compare(image, cache, label + " with " + std::to_string(frames) + " frames", arguments,
                [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
    }
    // From the cells the image needs before its first step.
    const auto loaded =
        run_at(image, 0, nullptr, arguments, [](nvm::RunLimits& limits) { limits.max_steps = 1; }).result.peak_heap_cells;
    for (const auto cells : limits_from(loaded > 2 ? loaded - 2 : 1, whole.peak_heap_cells + 2, 400)) {
        compare(image, cache, label + " with " + std::to_string(cells) + " heap cells", arguments,
                [&](nvm::RunLimits& limits) { limits.max_heap_cells = cells; });
    }
    if (!machine_code_runs() || tasks) return;
    const auto statistics = cache.statistics();
    check_equal(statistics.native_mode, std::string("native"), label + ": native programs ran as machine code");
    check(statistics.native_programs > 0 && statistics.native_bytes > 0, label + ": programs have machine code");
    check(statistics.native.entered > 0 && statistics.native.steps > 0, label + ": steps ran in machine code");
}

fs::path tier_program(const Environment& environment, const std::string& name) {
    return backend_dir(environment) / "tier" / (name + ".nra");
}

} // namespace

NEURA_TEST(tier3, machine_code_of_calls_and_traps_matches_tier_0_under_every_limit) {
    const auto image = build_program({tier_program(environment, "program-calls")});
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"calls", "2"}, {"overflow", "3"}, {"index", "3"}, {"nil", "3"}, {"field", "3"}}) {
        sweep(image, "program-calls " + arguments[0], arguments);
    }
}

NEURA_TEST(tier3, machine_code_of_helpers_matches_tier_0_under_every_limit) {
    const auto image = build_program({tier_program(environment, "program-helpers")});
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"alloc", "5"}, {"kernel", "3"}, {"text", "3"}, {"utf8", "3"}, {"free", "3"}, {"remove", "3"},
             {"stub", "30"}}) {
        sweep(image, "program-helpers " + arguments[0], arguments);
    }
}

NEURA_TEST(tier3, machine_code_of_region_programs_matches_tier_0_under_every_limit) {
    sweep(build_program({tier_program(environment, "regions")}), "regions", {});
    sweep(build_program({tier_program(environment, "region-gc")}), "region-gc", {});
    const auto counters = build_program({tier_program(environment, "region-counters")});
    sweep(counters, "region-counters to the top", {"3", "1"});
    sweep(counters, "region-counters below the top", {"0", "0"});
    const auto guards = build_program({tier_program(environment, "region-guards")});
    for (int mode = 0; mode <= 8; ++mode) {
        sweep(guards, "region-guards " + std::to_string(mode), {std::to_string(mode), "2"});
    }
}

NEURA_TEST(tier3, task_programs_stay_off_machine_code_and_match_tier_0) {
    // Task images run their programs on the program interpreter (tier 3
    // never runs with a scheduler).
    for (const auto* name : {"program-tasks", "program-task-alloc"}) {
        const auto image = build_program({tier_program(environment, name)});
        nvm::TierCache cache(image, machine_options());
        for (const std::uint64_t quantum : {1ULL, 2ULL, 3ULL, 4ULL}) {
            compare(image, cache, std::string(name) + " with quantum " + std::to_string(quantum), {},
                    [&](nvm::RunLimits& limits) { limits.task_quantum = quantum; });
        }
        check_equal(cache.statistics().native.entered, std::uint64_t{0}, std::string(name) + ": no machine code ran");
    }
}

NEURA_TEST(tier3, a_float_program_is_refused_in_a_rounding_mode_other_than_the_default) {
    const auto image = build_program({tier_program(environment, "regions")});
    // The default mode: the float loop's program gets machine code, which
    // runs.
    nvm::TierCache cache(image, machine_options());
    compare(image, cache, "regions in the default rounding mode", {});
    const auto before = cache.statistics();
    if (machine_code_runs()) {
        check(before.native.entered > 0 && before.native.refused == 0,
              "the default rounding mode enters machine code and refuses nothing");
    }
    // Rounding upward, with the same machine code: every entry of a
    // program that computes floats is refused, and the run is tier 0's in
    // the same mode.
    const auto saved = std::fegetround();
    std::fesetround(FE_UPWARD);
    Captured native;
    Captured reference;
    try {
        native = run_at(image, 2, &cache, {}, {});
        reference = run_at(image, 0, nullptr, {}, {});
    } catch (...) {
        std::fesetround(saved);
        throw;
    }
    std::fesetround(saved);
    check_same(native, reference, "regions rounding upward");
    if (machine_code_runs()) {
        const auto statistics = cache.statistics();
        check(statistics.native.refused > before.native.refused,
              "machine code that computes floats is refused when rounding upward");
    }
}

NEURA_TEST(tier3, a_count_kept_on_one_path_of_a_loop_matches_tier_0) {
    // The count's phis at the loop heads are no constants, though each
    // joins a constant with a phi that is not one: machine code must keep
    // every increment.
    const auto image = build_program({backend_dir(environment) / "tier3" / "counts.nra"});
    for (const int size : {1, 2, 3, 5, 9}) {
        nvm::TierCache cache(image, machine_options());
        compare(image, cache, "counts " + std::to_string(size), {std::to_string(size)});
        if (machine_code_runs()) check(cache.statistics().native.steps > 0, "the loops ran as machine code");
    }
    sweep(image, "counts", {"4"});
}

NEURA_TEST(tier3, a_loop_head_entry_gets_machine_code_when_the_interpreter_hands_the_loop_over) {
    // The loop runs 4000 iterations; its program becomes native after 1000
    // checkpoint arrivals, at the loop head, whose code (a unit of its own,
    // src/jit/tier3_lower.hpp) is made when the run first enters there.
    const auto image = build_program({backend_dir(environment) / "tier3" / "shapes.nra"});
    auto options = machine_options();
    options.native_after = 1000;
    nvm::TierCache cache(image, options);
    compare(image, cache, "a loop handed to machine code in its middle", {"sum", "4000"});
    if (!machine_code_runs()) return;
    const auto statistics = cache.statistics();
    for (const auto& [reason, count] : statistics.native_rejections) {
        check(reason.rfind("an entry", 0) != 0, "every entry got its code: " + reason);
    }
    check(statistics.native.entered > 0 && statistics.native.steps > 8000, "machine code ran most of the loop");
    if (nvm::tier_eager()) return;
    check(statistics.region_counts.program_steps > statistics.native.steps,
          "the program interpreter ran the first iterations");
}

// tests/backend/tier3/passes.nra's `window` loop gets a guard in its
// preheader (the variable starts at 0 or more) that covers its index check.
// Handed to machine code at its seventh checkpoint arrival, in its middle
// with the variable still negative, the guard fails and the code
// deoptimizes at the loop head; tier 1 runs on, and the run is tier 0's, at
// every step limit. With a start of 0 the guard holds and machine code runs
// the loop; without the passes there is no guard; and a start below the
// table fails the guard before the index trap.
NEURA_TEST(tier3, machine_code_of_a_loop_entered_in_its_middle_deoptimizes_exactly_where_a_hoisted_guard_fails) {
    if (nvm::tier_eager() || !nvm::tier3::passes_enabled()) return;
    const auto image = build_program({backend_dir(environment) / "tier3" / "passes.nra"});
    const auto options = [](std::uint32_t after, bool passes) {
        auto options = machine_options();
        options.native_after = after;
        options.native_passes = passes;
        return options;
    };
    const std::vector<std::string> failing_arguments{"window", "300", "-90"};
    nvm::TierCache failing(image, options(7, true));
    compare(image, failing, "a guard that fails at an entry in the middle", failing_arguments);
    const auto steps = run_at(image, 0, nullptr, failing_arguments, {}).result.steps;
    // A cache of its own for each limit, so that each run enters the loop
    // in its middle.
    for (const auto limit : limits_from(1, steps + 1, 600)) {
        nvm::TierCache fresh(image, options(7, true));
        compare(image, fresh, "the failing guard with " + std::to_string(limit) + " steps", failing_arguments,
                [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
    }
    nvm::TierCache passing(image, options(7, true));
    compare(image, passing, "a guard that holds", {"window", "300", "0"});
    nvm::TierCache off(image, options(7, false));
    compare(image, off, "without the passes", failing_arguments);
    nvm::TierCache traps(image, options(1, true));
    compare(image, traps, "a negative index", {"window", "300", "-101"});
    if (!machine_code_runs()) return;
    const auto& failed = failing.statistics();
    check_equal(failed.native_mode, std::string("native"), "the loop ran as machine code");
    check(failed.native.entered > 0 && failed.native.guards > 0, "machine code ran the guard and it failed");
    check(failed.region_counts.program_steps > 0, "the program interpreter ran the iterations before the entry");
    check_equal(passing.statistics().native.guards, std::uint64_t{0}, "no guard failed");
    check(passing.statistics().native.steps > 0, "machine code ran the loop");
    check_equal(off.statistics().native.guards, std::uint64_t{0}, "no guards without the passes");
    check(off.statistics().native.steps > 0, "machine code ran the loop without the passes");
    // NEURA_TIER3_AFTER makes the program native only after that trap.
    if (nvm::tier3_after() == 0) check(traps.statistics().native.guards > 0, "the guard failed before the trap");
}

NEURA_TEST(tier3, the_affine_heap_guard_runs_at_the_int16_limit_and_replays_width_and_length_failures) {
    const auto image = build_program({backend_dir(environment) / "tier3" / "affine-heap-bounds.nra"});
    const auto serial = [](nvm::RunLimits& limits) { limits.profile = false; };
    const auto overflow = run_at(image, 0, nullptr, {"wide"}, serial);
    check(!overflow.result.completed && overflow.result.trap.kind == nvm::TrapKind::Program &&
              overflow.result.trap.reason == 1, "side 182 traps in the original checked arithmetic");
    const auto grid = overflow.result.trap.function;
    auto options = machine_options();
    // Keep grid as a separate native callee. Its loop-resume program cannot
    // form an affine guard because it does not know the loop's initial state.
    options.native_inlining = false;
    for (const std::string mode : {"ok", "wide", "short"}) {
        nvm::TierCache cache(image, options);
        const auto expected = run_at(image, 0, nullptr, {mode}, serial);
        const auto actual = run_at(image, 2, &cache, {mode}, serial);
        check_same(actual, expected, "affine heap guard " + mode);
        if (mode == "ok") {
            check(actual.result.completed && actual.output == "355897\n", "side 181 fits int16 and the list");
        } else if (mode == "short") {
            check(!actual.result.completed && actual.result.trap.kind == nvm::TrapKind::IndexOutOfBounds,
                  "side 181 with a one-element-short list traps at the original access");
        }
        if (!machine_code_runs() || !t3::passes_enabled()) continue;

        const auto resolved = cache.program(grid, std::uint64_t{1} << 40);
        check(resolved.region != nullptr, "grid has a Tier 2 program");
        if (!resolved.region) continue;
        auto* code = cache.native_code(*resolved.region, t3::Variant::SparseFrames);
        if (!code) code = cache.native_code(*resolved.region, t3::Variant::Standard);
        check(code && code->function_entry && code->function_entry->ir.pass_counts.contains("guard.heap_affine") &&
                  !code->ir.pass_counts.contains("guard.heap_affine"), "grid guards only at its direct entry");
        if (!code || !code->function_entry) continue;
        const auto& states = code->function_entry->ir.states;
        const auto header = std::ranges::find_if(states, [](const auto& state) { return state.loop_preflight; });
        check(header != states.end(), "the affine guard names a loop-header recovery state");
        if (header == states.end()) continue;

        const auto stats = cache.statistics();
        // Grid is the only callee the driver calls in its 100-iteration loop.
        check(stats.native.cell_entries >= 90, "the driver repeatedly enters native callees directly");
        check_equal(stats.native.guards, mode == "ok" ? std::uint64_t{0} : std::uint64_t{1},
                    "affine guard failures for " + mode);
        if (mode != "ok") {
            check(stats.native_deopts.contains({grid, header->block}) && header->block != actual.result.trap.block,
                  "native code fails at the preheader and replays the original trap inside grid");
        }
    }
}

NEURA_TEST(tier3, natural_checks_recover_sparse_frames_at_the_original_loop_traps) {
    std::ifstream fixture(backend_dir(environment) / "tier3" / "sparse-guard-recovery.nvra");
    const std::string source((std::istreambuf_iterator<char>(fixture)), std::istreambuf_iterator<char>());
    const auto prototype = decompiler::assemble(source);
    constexpr std::uint32_t loop_function = 1;
    constexpr std::uint32_t previous_index_slot = 8;
    struct Case {
        const char* label;
        nvr::Value final_object;
        std::uint32_t trap_block;
        nvm::TrapKind trap;
        t3::Op check_op;
        std::uint64_t nvm::NativeCounts::*deopt_count;
    };
    const Case cases[] = {
        {"reference type", {nvr::ValueKind::Slot, 4, 1}, 2, nvm::TrapKind::ReferenceType,
         t3::Op::CheckKind, &nvm::NativeCounts::references},
        {"class kind", {nvr::ValueKind::Slot, 5, 2}, 3, nvm::TrapKind::InvalidCast,
         t3::Op::CheckKind, &nvm::NativeCounts::state_traps},
        {"switch selector", {nvr::ValueKind::Slot, 6, 3}, 5, nvm::TrapKind::InvalidSelector,
         t3::Op::SwitchCheck, &nvm::NativeCounts::selectors},
    };
    auto options = machine_options();
    options.native_inlining = false;
    const auto serial = [](nvm::RunLimits& limits) { limits.profile = false; };
    bool full_frames = false;
#ifdef NEURA_TEST_FRAME_HOOKS
    full_frames = std::getenv("NEURA_TIER3_TEST_FULL_FRAMES") != nullptr;
#endif
    const bool sparse = machine_code_runs() && t3::passes_enabled() && !t3::verify_enabled() &&
                        !nvm::tier_check_enabled() && !nvm::tier_shadow_enabled() && !full_frames;

    for (const auto& scenario : cases) {
        auto model = prototype;
        model.functions[0].blocks[7].exits[0].arguments.back() = scenario.final_object;
        const auto image = nvr::load(nvr::encode(model));
        const auto expected = run_at(image, 0, nullptr, {}, serial);
        const auto label = std::string(scenario.label);
        check(!expected.result.completed && expected.result.trap.kind == scenario.trap &&
                  expected.result.trap.function == loop_function && expected.result.trap.block == scenario.trap_block,
              label + ": the reference faults at the requested check");

        nvm::TierCache cache(image, options);
        nvm::TierTimes timeline(nvm::TierTimes::Clock::now());
        const auto actual = run_at(image, 2, &cache, {}, [&](nvm::RunLimits& limits) {
            serial(limits);
            limits.tier_times = &timeline;
        });
        check_same(actual, expected, label + " with tier statistics");
        if (sparse) {
            const auto stats = cache.statistics();
            check(stats.native_mode == "native" && stats.native.steps != 0, label + ": machine code ran");
            check_equal(stats.native.*scenario.deopt_count, std::uint64_t{1}, label + ": natural deoptimization");
            check(stats.native_deopts.contains({loop_function, scenario.trap_block}), label + ": original recovery block");

            const auto resolved = cache.program(loop_function, std::uint64_t{1} << 40);
            check(resolved.region != nullptr, label + ": the loop has a function program");
            const auto* code = cache.native_code(*resolved.region, t3::Variant::SparseFrames);
            check(code && code->function_entry && code->function_entry->machine, label + ": tier statistics used sparse entry code");
            const auto& ir = code->function_entry->ir;
            const bool reference = scenario.trap == nvm::TrapKind::ReferenceType;
            const auto checked_sparse_state = [&](const t3::Inst& in) {
                return in.op == scenario.check_op && in.state != t3::none &&
                       ((in.flags & t3::ReferenceCheck) != 0) == reference &&
                       ir.states[in.state].block == scenario.trap_block &&
                       ir.states[in.state].slots[previous_index_slot].kind == t3::StateSlot::Kind::Dead;
            };
            check(std::ranges::any_of(ir.insts, checked_sparse_state), label + ": the failed check omitted the dead scalar");
        }
        const auto steps = expected.result.steps;
        for (const auto limit : {std::uint64_t{1}, steps / 2, steps - 2, steps - 1, steps, steps + 1}) {
            compare(image, cache, label + " with " + std::to_string(limit) + " steps", {},
                    [&](nvm::RunLimits& limits) { serial(limits); limits.max_steps = limit; });
        }
    }
}

// The Python driver checks the replacement IDs from NEURA_TIER_DUMP.
NEURA_TEST(tier3_backoff, recursive_backoff_reuses_refreshed_programs) {
    const auto image = build_program({backend_dir(environment) / "tier3" / "preheader-recursive-backoff.nra"});
    auto options = machine_options();
    options.native_after = 4;
    options.program_proofs = 0;
    options.native_inlining = false;
    nvm::TierCache cache(image, options);
    const auto serial = [](nvm::RunLimits& limits) { limits.profile = false; };
    const auto reference = run_at(image, 0, nullptr, {}, serial);
    check(reference.result.completed && !reference.output.empty(), "the recursive reference execution finishes");
    check_same(run_at(image, 2, &cache, {}, serial), reference, "recursive back-off and refresh");
    const auto stats = cache.statistics();
    check_equal(stats.native.loop_preflight_backoffs, std::uint64_t{1}, "the loop backs off once");
    check(stats.refreshed > 0 && stats.native.steps > 0, "a program refreshes and machine code runs");
}

NEURA_TEST(tier3_osr, specialised_heap_preheaders_replay_and_back_off) {
    check(machine_code_runs() && t3::passes_enabled(), "this test requires the native Tier 3 passes");
    const auto image = build_program({backend_dir(environment) / "tier3" / "osr-preheader.nra"});
    auto options = machine_options();
    options.native_after = 7;
    options.native_inlining = false;
    const auto serial = [](nvm::RunLimits& limits) { limits.profile = false; };
    bool full_frames = false;
#ifdef NEURA_TEST_FRAME_HOOKS
    full_frames = std::getenv("NEURA_TIER3_TEST_FULL_FRAMES") != nullptr;
#endif
    const bool sparse = !t3::verify_enabled() && !nvm::tier_check_enabled() && !nvm::tier_shadow_enabled() && !full_frames;
    const auto late = run_at(image, 0, nullptr, {"late"}, serial);
    const auto& trap = late.result.trap;
    check(!late.result.completed && trap.kind == nvm::TrapKind::IndexOutOfBounds, "tier 0 reaches the late read");
    check_equal(trap.detail, std::string("index 32 for length 32"), "the list fails after 32 valid reads");
    const auto& faulting = image.functions.at(trap.function).blocks.at(trap.block);
    check(std::ranges::any_of(faulting.exits, [](const auto& exit) {
        return exit.kind == nvr::ExitKind::State && exit.operation == nvr::StateOp::ReadElement;
    }), "the original trap site reads the list");

    struct Case {
        const char* mode;
        const char* output;
    };
    for (const auto& scenario : {Case{"pass", "32\n"}, {"late", nullptr}, {"backoff", "186\n"},
                                  {"recursive", "96\n"}, {"recursive-late", nullptr}}) {
        const std::string mode = scenario.mode;
        const auto expected = mode == "late" ? late : run_at(image, 0, nullptr, {mode}, serial);
        if (scenario.output) {
            check(expected.result.completed, mode + ": tier 0 completes");
            check_equal(expected.output, std::string(scenario.output), mode + ": checksum");
        } else {
            check(!expected.result.completed && expected.result.trap.kind == trap.kind &&
                  expected.result.trap.detail == trap.detail && expected.result.trap.function == trap.function &&
                  expected.result.trap.block == trap.block, mode + ": tier 0 traps at the original read");
        }

        nvm::TierCache cache(image, options);
        check_same(run_at(image, 2, &cache, {mode}, serial), expected, mode);
        const auto stats = cache.statistics();
        check(stats.native.osr_entries > 0 && stats.native.steps > 0 && stats.region_counts.program_steps > 0,
              mode + ": the Tier 2 interpreter handed the loop to specialised machine code");
        const auto* program = cache.program(trap.function, 0).region;
        check(program && program->entries.size() > 1 && program->entries.front().block == 0,
              mode + ": scan has both a function and a loop entry");
        const auto header = program->entries[1].block;
        check(header != 0 && header != trap.block, mode + ": the preflight has a separate loop header");
        if (!scenario.output || mode == "backoff") {
            check(stats.native.guards > 0 && stats.native_deopts.contains({trap.function, header}),
                  mode + ": a guard deoptimises at the original loop header");
        } else {
            check_equal(stats.native.guards, std::uint64_t{0}, mode + ": valid preflights hold");
        }
        if (mode == "late" && sparse) {
            const auto* code = cache.native_code(*program, t3::Variant::SparseFrames);
            check(code && !code->osr_programs.empty(), "the scan made a sparse OSR program");
            const auto has_sparse_preflight = [header](const auto& osr) {
                if (osr->ir.entries.front().block != header) return false;
                return std::ranges::any_of(osr->ir.states, [header](const auto& state) {
                    return state.block == header && state.loop_preflight &&
                           std::ranges::any_of(state.slots, [](const auto& slot) {
                               return slot.kind == t3::StateSlot::Kind::Dead;
                           });
                });
            };
            check(std::ranges::any_of(code->osr_programs, has_sparse_preflight),
                  "the failed OSR preflight omits a dead scalar at the original header");
        }
        if (mode == "backoff") {
            check_equal(stats.native.guards, std::uint64_t{3}, "three failed guards before replacement");
            check_equal(stats.native.loop_preflight_backoffs, std::uint64_t{1}, "the scan backs off once in the run");
        }
        if (mode.starts_with("recursive")) {
            check(stats.native.helper_entries + stats.native.cell_entries > 0,
                  mode + ": the specialised caller reaches another native activation");
        }
        if (mode == "recursive-late") {
            const auto& callers = expected.result.trap.callers;
            check(!callers.empty() && callers.front().first == trap.function,
                  mode + ": the faulting scan has a caller in the same function");
        }
    }

    // A check after the loop is reachable from its OSR entry but cannot
    // produce a preflight there. The generic entry replays the original trap.
    const auto after_loop = run_at(image, 0, nullptr, {"after-loop"}, serial);
    check(!after_loop.result.completed && after_loop.result.trap.kind == nvm::TrapKind::IndexOutOfBounds,
          "the reference faults on the read after the loop");
    nvm::TierCache fallback_cache(image, options);
    check_same(run_at(image, 2, &fallback_cache, {"after-loop"}, serial), after_loop, "generic OSR fallback");
    const auto* region = fallback_cache.program(after_loop.result.trap.function, 0).region;
    check(region != nullptr, "the looping function has a Tier 2 program");
    const auto header = std::ranges::find_if(region->entries, &nvm::tier2::Region::Entry::loop_header);
    check(header != region->entries.end() && header != region->entries.begin(), "the loop has a nonzero OSR entry");
    const auto index = static_cast<std::uint32_t>(header - region->entries.begin());
    const auto* parent = fallback_cache.native_code(*region, sparse ? t3::Variant::SparseFrames : t3::Variant::Standard);
    check(parent && parent->osr_attempts == 1 && parent->osr_programs.empty(), "the child was tried and refused");
    check(!parent->osr_template, "the final attempt released the template");
    std::uint64_t bytes = 0;
    std::string failure;
    const auto fallback = parent->entry(image, index, bytes, failure);
    check(fallback.program == parent && fallback.address && fallback.index == index && bytes == 0 && failure.empty(),
          "the loop used its cached generic entry");
    check_equal(fallback_cache.statistics().native_osr_programs, std::uint64_t{0}, "the refusal published no OSR code");

    // The induction index is in range. A second load takes its index from
    // the first load and faults only on the final iteration.
    const auto indirect = run_at(image, 0, nullptr, {"indirect"}, serial);
    const auto& body_trap = indirect.result.trap;
    check(!indirect.result.completed && body_trap.kind == nvm::TrapKind::IndexOutOfBounds &&
              body_trap.detail == trap.detail && body_trap.function != trap.function,
          "tier 0 reaches the out-of-bounds indirect read");
    nvm::TierCache indirect_cache(image, options);
    check_same(run_at(image, 2, &indirect_cache, {"indirect"}, serial), indirect, "OSR body fault");
    const auto indirect_stats = indirect_cache.statistics();
    check(indirect_stats.native.osr_entries > 0 && indirect_stats.native.guards == 0,
          "the indirect loop entered specialised code and passed its preflight");
    check_equal(indirect_stats.native.state_traps, std::uint64_t{1}, "the indirect heap check deoptimises once");
    check(indirect_stats.native_deopts.contains({body_trap.function, body_trap.block}),
          "the specialised body deoptimises at the indirect read");

    // An externally advanced execution exposes its full frames at the trap.
    const auto fault_frames = [&](std::uint8_t tier, nvm::TierCache* cache) {
        auto limits = limits_at(tier, cache);
        serial(limits);
        nvm::Execution execution(image, std::vector<std::string>{"indirect"}, limits);
        const auto event = execution.advance();
        check(event.kind == nvm::Event::Kind::Trapped, "the indirect read faults before printing");
        check_same_trap(event.trap, body_trap, "inspectable OSR body fault");
        check_equal(execution.steps(), indirect.result.steps, "inspectable OSR body fault steps");
        return execution.tier3_frames();
    };
    nvm::TierCache inspected_cache(image, options);
    const auto reference_frames = fault_frames(0, nullptr);
    check(!reference_frames.empty() && reference_frames.back().function == body_trap.function,
          "the faulting activation remains inspectable");
    check_same_frames(fault_frames(2, &inspected_cache), reference_frames, "OSR body fault");
    const auto inspected = inspected_cache.statistics();
    check(inspected.native.osr_entries > 0 && inspected.native.guards == 0 &&
              inspected.native_deopts.contains({body_trap.function, body_trap.block}),
          "the inspected frames came from the specialised body fault");

    for (const auto& [mode, steps] : {std::pair{"late", late.result.steps},
                                     std::pair{"indirect", indirect.result.steps}}) {
        for (const auto limit : {steps - 1, steps, steps + 1}) {
            nvm::TierCache cache(image, options);
            compare(image, cache, std::string(mode) + " OSR fault at step limit " + std::to_string(limit), {mode},
                    [limit, &serial](nvm::RunLimits& limits) { serial(limits); limits.max_steps = limit; });
        }
    }
}

NEURA_TEST(tier3, the_statistics_line_reports_machine_code_entries_and_bytes) {
    const auto image = build_program({tier_program(environment, "program-calls")});
    nvm::TierCache cache(image, machine_options());
    run_at(image, 2, &cache, {"calls", "4"}, {});
    const auto statistics = cache.statistics();
    const auto text = nvm::describe(statistics);
    const auto line_start = text.find("tier: native programs ");
    check(nvm::tier_ceiling() < 2 || line_start != std::string::npos, "the statistics have the native line");
    if (!machine_code_runs()) return;
    const auto line = text.substr(line_start, text.find('\n', line_start) - line_start);
    const auto bytes = "(" + std::to_string(statistics.native_bytes) + " bytes of code";
    check(statistics.native_bytes > 0 && line.find(bytes) != std::string::npos, "the line counts bytes: " + line);
    const auto entered = "entered " + std::to_string(statistics.native.entered) + ",";
    check(statistics.native.entered > 0 && line.find(entered) != std::string::npos, "the line counts entries: " + line);
    check(text.find("on the IR interpreter") == std::string::npos, "nothing ran on the IR interpreter");
    // With native code off, the IR interpreter runs the programs and the
    // statistics say why.
    nvm::TierCache off(image, machine_options());
    const auto plain = run_at(image, 0, nullptr, {"calls", "4"}, {});
    const auto interpreted =
        run_at(image, 2, &off, {"calls", "4"}, [](nvm::RunLimits& limits) { limits.jit = jit::Mode::Off; });
    check_same(interpreted, plain, "calls with --jit=off");
    const auto off_text = nvm::describe(off.statistics());
    check(off_text.find("tier: native programs on the IR interpreter: native code is off") != std::string::npos,
          "the statistics say why the IR interpreter ran");
    check(off.statistics().native.entered > 0, "the IR interpreter ran the native programs");
}

// The machine code of a function program's entry 0 takes the initialized
// flag of every slot but a parameter as 0, as a call leaves the callee's
// frame; run_native_program refuses entry 0 in a frame where one is set. The
// back edge of a loop is turned into an edge to block 0 (the compiler's
// block 0 is an empty block that no edge reaches), so the dispatcher arrives
// at block 0 with the loop's temporaries initialized.
NEURA_TEST(tier3, entry_zero_is_refused_where_a_slot_but_a_parameter_is_initialized) {
    if (!machine_code_runs()) return;
    const auto built = build_program({tier_program(environment, "entry-flags")});
    auto text = decompiler::disassemble(built);
    // The function with four int slots and two parameters: spin. Its loop
    // body ends in the block that assigns every slot and continues to the
    // loop head, block 1.
    const std::string header = " parameters 2 result 2 frame [ 2 2 2 2 ]";
    const auto function = text.find(header);
    check(function != std::string::npos, "the function spin is in the image");
    const std::string back_edge = "exit continue resume 1 assign [ 0 =";
    const auto edge = text.find(back_edge, function);
    check(edge != std::string::npos, "its loop body has the back edge");
    text.replace(edge, back_edge.size(), "exit continue resume 0 assign [ 0 =");
    const auto image = nvr::load(nvr::encode(decompiler::assemble(text)));
    // The default thresholds: spin's program forms while a call of it runs in
    // the interpreter, which then arrives at block 0 again.
    nvm::TierOptions options;
    options.programs = nvm::TierOptions::Programs::On;
    options.tier3 = nvm::TierOptions::Tier3::Native;
    options.native_after = 1;
    const std::vector<std::string> arguments{"300"};
    nvm::TierCache cache(image, options);
    compare(image, cache, "entry 0 in a frame with initialized temporaries", arguments);
    const auto statistics = cache.statistics();
    check(statistics.native.entered > 0, "native programs were entered");
    // NEURA_TIER_EAGER=1 forms the programs at the first arrival, so that
    // every call of spin enters entry 0 in a fresh frame and the loop runs
    // in machine code: no run arrives at block 0 with initialized slots.
    if (std::getenv("NEURA_TIER_EAGER") == nullptr) {
        check(statistics.native.refused > 0, "entry 0 was refused in a frame with an initialized temporary");
    }
}

} // namespace neura::testing
