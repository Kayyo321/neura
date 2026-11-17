// Tier 3 inlining (src/nvm/tier3_inline.cpp): calls, dispatch sites and
// closure calls inlined into native programs keep every observable of tier 0:
// traps inside inlined callees with their callers, every step and frame
// limit, function steps under --profile (whose variant inlines nothing), and
// a dispatch whose receivers change after compilation, whose class tests
// miss into the generic dispatch without a deoptimization. Materializing
// inlined frames builds the frames push_frame builds. The `tier3` suite
// (backend-tier3).
#include "graph_test_support.hpp"

#include "jit/tier3_native.hpp"
#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvm/tier3.hpp"
#include "nvr/names.hpp"

#include <algorithm>
#include <functional>
#include <set>

namespace neura::testing {
namespace {

namespace t3 = nvm::tier3;

// Lifts every block and forms every program at the first arrival; a function
// program becomes a native program at its
// `native_after`th checkpoint arrival, after its callees have programs and
// its dispatch sites have recorded tags.
nvm::TierOptions inline_options(std::uint32_t native_after = 3) {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    options.programs = nvm::TierOptions::Programs::On;
    // NEURA_TIER3 picks the back end (native: machine code); with it off, or
    // unset where there is no machine code (the default is then no tier 3),
    // the IR interpreter.
    const bool interpreted = nvm::tier3_environment() == nvm::TierOptions::Tier3::Off ||
                             (nvm::tier3_environment_implicit() && !jit::native::supported());
    options.tier3 = interpreted ? nvm::TierOptions::Tier3::Interp : nvm::TierOptions::Tier3::Environment;
    options.native_after = native_after;
    return options;
}

nvm::RunLimits limits_at(std::uint8_t tier, nvm::TierCache* cache = nullptr, bool profile = false) {
    nvm::RunLimits limits;
    limits.max_steps = 20'000'000;
    limits.tier = tier;
    limits.tier_cache = cache;
    limits.profile = profile;
    return limits;
}

bool programs_run() { return nvm::tier_ceiling() >= 2; }

// Whether the caches keep the thresholds the tests give: NEURA_TIER_EAGER=1
// and NEURA_TIER3_AFTER make every program native at its first checkpoints,
// before its callees have programs or its dispatch sites records, and the
// runs are then compared with tier 0 without inlining to look for.
bool thresholds_kept() { return !nvm::tier_eager() && nvm::tier3_after() == 0; }

void check_same(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
}

using Adjust = std::function<void(nvm::RunLimits&)>;

void compare_with_tier_0(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                         const std::vector<std::string>& arguments, const Adjust& adjust = {}) {
    auto reference_limits = limits_at(0);
    if (adjust) adjust(reference_limits);
    const auto reference = run_captured(image, reference_limits, std::nullopt, arguments);
    auto native_limits = limits_at(2, &cache);
    if (adjust) adjust(native_limits);
    check_same(run_captured(image, native_limits, std::nullopt, arguments), reference, label);
}

nvr::Image inline_image(const Environment& environment) {
    return build_program({backend_dir(environment) / "tier3" / "inline.nra"});
}

// The function of the image whose source name ends in `.name` (or is it).
std::uint32_t function_named(const nvr::Image& image, const std::string& name) {
    const auto names = nvr::decode_names(image);
    if (names) {
        for (std::uint32_t f = 0; f < names->functions.size(); ++f) {
            const auto& full = names->functions[f];
            if (full == name || (full.size() > name.size() && full.ends_with("." + name))) return f;
        }
    }
    check(false, "the image names a function " + name);
    return 0;
}

// The standard native program of `function` in `cache`, or null.
const t3::Function* native_ir(nvm::TierCache& cache, std::uint32_t function) {
    const auto resolved = cache.program(function, std::uint64_t{1} << 40);
    if (resolved.region == nullptr) return nullptr;
    const auto* code = cache.native_code(*resolved.region, t3::Variant::Standard);
    return code == nullptr ? nullptr : &code->ir;
}

// Whether some frame state of `ir` is an inlined frame of `function`.
bool inlines(const t3::Function& ir, std::uint32_t function) {
    return std::any_of(ir.states.begin(), ir.states.end(),
                       [&](const t3::FrameState& state) { return state.caller != t3::none && state.function == function; });
}

std::uint64_t deoptimizations(const nvm::NativeCounts& n) {
    return n.budget + n.checks + n.guards + n.selectors + n.references + n.state_traps + n.exits;
}

} // namespace

// --- Against tier 0 ----------------------------------------------------------------

NEURA_TEST(tier3, traps_inside_inlined_callees_match_tier_0_at_every_step_limit) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    for (const auto* kind : {"overflow", "index", "nil"}) {
        const std::vector<std::string> arguments{kind, "4"};
        nvm::TierCache cache(image, inline_options());
        compare_with_tier_0(image, cache, kind, arguments);
        const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments).result;
        check(!reference.completed && !reference.trap.callers.empty(), std::string(kind) + ": the reference traps below main");
        for (std::uint64_t limit = 1; limit <= reference.steps + 1; ++limit) {
            compare_with_tier_0(image, cache, std::string(kind) + " with " + std::to_string(limit) + " steps", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
        const auto statistics = cache.statistics();
        check(!thresholds_kept() || statistics.native_inlined > 0, std::string(kind) + ": calls were inlined");
        check(statistics.native.steps > 0, std::string(kind) + ": steps ran in native programs");
    }
}

NEURA_TEST(tier3, inlined_calls_match_tier_0_at_every_frame_and_step_limit) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"calls", "12"}, {"spins", "6"}, {"overflow", "3"}, {"descend", "6"}, {"dispatch", "6"}, {"single", "8"},
             {"closure", "8"}}) {
        nvm::TierCache cache(image, inline_options());
        const auto label = arguments[0];
        compare_with_tier_0(image, cache, label, arguments);
        // Deep enough for every frame of the run, and two more.
        for (std::uint32_t frames = 1; frames <= 14; ++frames) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(frames) + " frames", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
        }
        const auto steps = run_captured(image, limits_at(0), std::nullopt, arguments).result.steps;
        for (std::uint64_t limit = 1; limit <= steps + 1; ++limit) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(limit) + " steps", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
        check(!thresholds_kept() || cache.statistics().native_inlined > 0, label + ": calls were inlined");
    }
}

NEURA_TEST(tier3, a_native_activation_is_entered_with_exactly_room_for_its_inlined_frames) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    const std::vector<std::string> arguments{"calls", "40"};
    // The fewest frames the reference completes with: main, calls, mix,
    // twist and add.
    std::uint32_t deepest = 1;
    while (!run_captured(image, [&] {
                auto limits = limits_at(0);
                limits.max_frames = deepest;
                return limits;
            }(), std::nullopt, arguments)
                .result.completed) {
        ++deepest;
    }
    nvm::TierCache cache(image, inline_options());
    compare_with_tier_0(image, cache, "calls", arguments);
    if (!thresholds_kept()) return;
    const auto before = cache.statistics();
    check(before.native_inlined > 0, "calls were inlined");
    compare_with_tier_0(image, cache, "calls at the deepest frame limit", arguments,
                        [&](nvm::RunLimits& limits) { limits.max_frames = deepest; });
    const auto at_limit = cache.statistics();
    check_equal(at_limit.native.refused, before.native.refused, "no entry was refused with room for every inlined frame");
    check(at_limit.native.entered > before.native.entered, "native code ran at the deepest frame limit");
    compare_with_tier_0(image, cache, "calls one frame short", arguments,
                        [&](nvm::RunLimits& limits) { limits.max_frames = deepest - 1; });
    check(cache.statistics().native.refused > at_limit.native.refused,
          "an entry without room for its inlined frames was refused");
}

NEURA_TEST(tier3, a_dispatch_whose_receivers_change_after_compilation_misses_into_the_generic_dispatch) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    const std::vector<std::string> arguments{"dispatch", "10"};
    // The loop of `weigh` becomes native after squares only were weighed.
    nvm::TierCache cache(image, inline_options(4));
    compare_with_tier_0(image, cache, "dispatch", arguments);
    if (thresholds_kept()) {
        const auto statistics = cache.statistics();
        check(statistics.native_class_tests > 0, "a dispatch site got class tests");
        check_equal(deoptimizations(statistics.native), std::uint64_t{0},
                    "sticks and rings took the generic dispatch without deoptimizing");
        const auto weigh = function_named(image, "weigh");
        const auto* ir = native_ir(cache, weigh);
        check(ir != nullptr && ir->class_tested_dispatches == 1, "weigh's dispatch has class tests");
        check(ir != nullptr && inlines(*ir, function_named(image, "Square.weight")), "Square.weight is inlined into weigh");
        check(ir != nullptr && !inlines(*ir, function_named(image, "Stick.weight")) &&
                  !inlines(*ir, function_named(image, "Ring.weight")),
              "the receivers seen after compilation are not inlined");
    }
    // Every later run uses the same native program.
    for (std::uint64_t limit = 1; limit <= 400; limit += 7) {
        compare_with_tier_0(image, cache, "dispatch with " + std::to_string(limit) + " steps", arguments,
                            [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
    }
}

NEURA_TEST(tier3, a_closure_call_and_a_single_implementation_are_inlined) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    for (const auto& [kind, caller] : std::vector<std::pair<std::string, std::string>>{{"closure", "apply"}, {"single", "single"}}) {
        nvm::TierCache cache(image, inline_options());
        compare_with_tier_0(image, cache, kind, {kind, "30"});
        if (!thresholds_kept()) continue;
        const auto* ir = native_ir(cache, function_named(image, caller));
        check(ir != nullptr, kind + ": " + caller + " is a native program");
        if (ir == nullptr) continue;
        // The callee of the dispatch's call exit is an inlined frame.
        const auto& program = *ir->programs.front();
        std::set<std::uint32_t> targets;
        for (const auto& op : program.code) {
            if (op.code != nvm::tier2::Code::Dispatch) continue;
            for (unsigned e = 0; e < op.width; ++e) {
                for (auto pc = program.operands[op.other + e]; pc < program.code.size(); ++pc) {
                    const auto& next = program.code[pc];
                    if (next.code == nvm::tier2::Code::Call) targets.insert(next.a);
                    if (next.code == nvm::tier2::Code::Call || next.code == nvm::tier2::Code::Deopt) break;
                }
            }
        }
        check(!targets.empty(), kind + ": " + caller + " has a dispatch");
        check(std::any_of(targets.begin(), targets.end(), [&](std::uint32_t f) { return inlines(*ir, f); }),
              kind + ": the dispatch's callee is inlined");
    }
}

NEURA_TEST(tier3, a_program_waits_for_callees_that_are_warming_up_and_not_for_callees_that_stopped_running) {
    if (!programs_run() || !thresholds_kept()) return;
    const auto image = inline_image(environment);
    {
        // `twist` has no program of its own when `sparse` reaches its
        // threshold (its blocks arrive once per four iterations, and 40
        // times before a program forms), but it runs steadily: the build waits
        // for it and inlines it.
        auto options = inline_options(100);
        options.region_after = 40;
        nvm::TierCache cache(image, options);
        compare_with_tier_0(image, cache, "sparse calls", {"sparse", "3000"});
        const auto* ir = native_ir(cache, function_named(image, "sparse"));
        check(ir != nullptr, "sparse is a native program");
        check(ir != nullptr && inlines(*ir, function_named(image, "twist")), "twist, still warming up at the threshold, is inlined");
    }
    {
        // `twist` ran once and never again: `cold` waits two checks (an
        // eighth of the threshold each, 200 arrivals) for it, not the longest
        // wait (16 times the threshold, 12800 arrivals; the run makes 1200).
        auto options = inline_options(800);
        options.region_after = 40;
        nvm::TierCache cache(image, options);
        compare_with_tier_0(image, cache, "cold callee", {"cold", "1200"});
        check(native_ir(cache, function_named(image, "cold")) != nullptr,
              "a callee that stopped running delays the native program by two checks");
    }
    {
        // `twist` runs every fourth iteration and has no program of its own
        // for the whole run (its blocks reach the region threshold, 500, only
        // after 2000 iterations; `sparse`'s program forms after 500): each
        // check sees it running, and the build goes ahead at the longest wait
        // (16 times the threshold of 50, 800 arrivals), about iteration 1300.
        // The run makes 1700.
        auto options = inline_options(50);
        options.region_after = 500;
        nvm::TierCache cache(image, options);
        compare_with_tier_0(image, cache, "callee that is still warming up", {"sparse", "1700"});
        check(native_ir(cache, function_named(image, "sparse")) != nullptr,
              "the wait for a callee that keeps running but has no program is bounded");
    }
}

NEURA_TEST(tier3, native_calls_enter_their_callee_from_its_cell_and_a_program_formed_again_clears_the_cell) {
    if (!programs_run() || !thresholds_kept()) return;
    const auto image = inline_image(environment);
    {
        // `descend` calls itself: a call that is no inlined. Machine code
        // enters the callee from its cell, up to the native depth, and past
        // it and at every frame limit through the helper.
        auto options = inline_options();
        options.tier3 = nvm::TierOptions::Tier3::Native;
        nvm::TierCache cache(image, options);
        compare_with_tier_0(image, cache, "descend", {"descend", "2000"});
        for (const std::uint32_t frames : {200U, 255U, 256U, 257U, 258U, 300U, 1000U}) {
            compare_with_tier_0(image, cache, "descend with " + std::to_string(frames) + " frames", {"descend", "2000"},
                                [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
        }
        const auto counts = cache.statistics().native;
        if (cache.machine_code()) {
            check(counts.cell_entries > 0, "descend: native calls entered from the callee's cell");
            check(counts.helper_entries > 0, "descend: the helper entered the callee at first and at the native depth");
        } else {
            check_equal(counts.cell_entries, std::uint64_t{0}, "no cell entries on the IR interpreter");
        }
    }
    {
        // `fall` recurses, and its program is formed again once the branch
        // its first program stubs has run: the cell of the first program is
        // cleared, and the helper enters the new program's native code.
        auto options = inline_options(4);
        options.program_proofs = 0;
        options.tier3 = nvm::TierOptions::Tier3::Native;
        nvm::TierCache cache(image, options);
        compare_with_tier_0(image, cache, "refresh", {"refresh", "3000"});
        const auto statistics = cache.statistics();
        check(statistics.refreshed > 0, "refresh: a program was formed again");
        if (cache.machine_code()) {
            check(statistics.native.cell_entries > 0, "refresh: native calls entered from cells");
            check(statistics.native.helper_entries >= 2, "refresh: the helper entered the callee again after its program changed");
        }
    }
}

NEURA_TEST(tier3, profile_runs_use_a_variant_that_inlines_nothing_and_keep_function_steps) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    for (const auto* kind : {"calls", "closure", "dispatch"}) {
        const std::vector<std::string> arguments{kind, "12"};
        nvm::TierCache profiled(image, inline_options());
        const auto reference = run_captured(image, limits_at(0, nullptr, true), std::nullopt, arguments);
        check(!reference.result.function_steps.empty(), std::string(kind) + ": the reference has function steps");
        check_same(run_captured(image, limits_at(2, &profiled, true), std::nullopt, arguments), reference,
                   std::string(kind) + " with --profile");
        const auto with_profile = profiled.statistics();
        check(with_profile.native_programs > 0 && with_profile.native.steps > 0,
              std::string(kind) + ": native programs ran under --profile");
        check_equal(with_profile.native_inlined, std::uint64_t{0}, std::string(kind) + ": the profile variant inlines nothing");
        // The same cache, without --profile: the standard variant inlines.
        run_captured(image, limits_at(2, &profiled), std::nullopt, arguments);
        check(!thresholds_kept() || profiled.statistics().native_inlined > 0,
              std::string(kind) + ": the standard variant inlines");
        // And --profile again uses its own variant.
        check_same(run_captured(image, limits_at(2, &profiled, true), std::nullopt, arguments), reference,
                   std::string(kind) + " with --profile after the standard variant");
    }
}

NEURA_TEST(tier3, the_statistics_line_counts_inlined_calls_and_dispatch_sites_with_class_tests) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    nvm::TierCache cache(image, inline_options(4));
    run_captured(image, limits_at(2, &cache), std::nullopt, {"dispatch", "10"});
    const auto statistics = cache.statistics();
    check(!thresholds_kept() || (statistics.native_inlined > 0 && statistics.native_class_tests > 0),
          "calls and a dispatch site were inlined");
    const auto text = nvm::describe(statistics);
    check(text.find(std::to_string(statistics.native_inlined) + " calls inlined, " +
                    std::to_string(statistics.native_class_tests) + " dispatch sites with class tests, ") !=
              std::string::npos,
          "the native line counts them: " + text);
    // Without inlining, both are zero.
    auto options = inline_options(4);
    options.native_inlining = false;
    nvm::TierCache plain(image, options);
    run_captured(image, limits_at(2, &plain), std::nullopt, {"dispatch", "10"});
    const auto none = nvm::describe(plain.statistics());
    check(none.find(" bytes of code, 0 calls inlined, 0 dispatch sites with class tests, ") != std::string::npos,
          "the native line counts nothing without inlining: " + none);
}

NEURA_TEST(tier3, a_deep_recursion_through_inlined_calls_matches_tier_0) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    nvm::TierCache cache(image, inline_options());
    compare_with_tier_0(image, cache, "descend 2000", {"descend", "2000"});
    for (const std::uint32_t frames : {1000U, 1500U, 2001U, 2002U, 2003U, 2004U}) {
        compare_with_tier_0(image, cache, "descend 2000 with " + std::to_string(frames) + " frames", {"descend", "2000"},
                            [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
    }
    check(!thresholds_kept() || cache.statistics().native_inlined > 0, "calls were inlined");
}

NEURA_TEST(tier3, runs_resume_in_materialized_inlined_frames_and_go_on_under_the_shadow_oracle) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    // Long runs: the oracle syncs every 65536 blocks, so native code
    // deoptimizes at that allowance wherever it is, mostly inside inlined
    // code, and tier 1 goes on in the frames it materialized, returns from
    // them, and runs to the end.
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"calls", "30000"}, {"spins", "8000"}, {"closure", "40000"}, {"single", "40000"}, {"dispatch", "15000"}}) {
        const auto label = arguments[0];
        const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments);
        nvm::TierCache cache(image, inline_options());
        nvm::TierShadowHooks hooks;
        std::vector<std::string> reports;
        hooks.report = [&](const std::string& text) { reports.push_back(text); };
        hooks.notice = [&](const std::string& text) { reports.push_back(text); };
        auto limits = limits_at(2, &cache);
        limits.tier_shadow = &hooks;
        check_same(run_captured(image, limits, std::nullopt, arguments), reference, label + " under the oracle");
        check(reports.empty(), label + ": the oracle saw no difference" + (reports.empty() ? "" : ": " + reports[0]));
        // And without it, with the same native programs.
        compare_with_tier_0(image, cache, label, arguments);
        if (!thresholds_kept()) continue;
        const auto statistics = cache.statistics();
        check(statistics.native_inlined > 0, label + ": calls were inlined");
        check(statistics.native.budget > 0, label + ": native code deoptimized at the oracle's allowance");
        if (label == "spins") {
            check(std::any_of(statistics.native_deopts.begin(), statistics.native_deopts.end(),
                              [&](const auto& entry) { return entry.first.first == function_named(image, "spin"); }),
                  "spins: native code deoptimized inside the inlined spin");
        }
    }
}

NEURA_TEST(tier3, machine_code_pushes_and_pops_frames_in_place_at_the_frame_limit_and_as_the_stack_grows) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    auto options = inline_options();
    options.tier3 = nvm::TierOptions::Tier3::Native;
    // A recursion deep enough to grow the frame arrays several times (from
    // 16 headers and 256 slots), then calls that reuse the popped frames'
    // storage, under every frame limit around the depth.
    for (const auto& arguments : std::vector<std::vector<std::string>>{{"descend", "1500"}, {"calls", "40"}}) {
        const auto label = arguments[0];
        nvm::TierCache cache(image, options);
        if (!cache.machine_code()) return;
        compare_with_tier_0(image, cache, label, arguments);
        const auto steps = run_captured(image, limits_at(0), std::nullopt, arguments).result.steps;
        std::vector<std::uint32_t> frames{1, 2, 3, 4, 5, 6, 7, 8};
        if (label == "descend") frames = {1, 2, 17, 255, 256, 257, 1500, 1501, 1502, 1503, 1504};
        for (const auto limit : frames) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(limit) + " frames", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_frames = limit; });
        }
        for (std::uint64_t limit = 1; limit <= steps + 1; limit += label == "descend" ? 97 : 1) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(limit) + " steps", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
        // The shadow oracle compares every frame, flags included, with
        // tier 1's at its sync points.
        nvm::TierShadowHooks hooks;
        std::vector<std::string> reports;
        hooks.report = [&](const std::string& text) { reports.push_back(text); };
        hooks.notice = [&](const std::string& text) { reports.push_back(text); };
        nvm::TierCache checked(image, options);
        auto limits = limits_at(2, &checked);
        limits.tier_shadow = &hooks;
        check_same(run_captured(image, limits, std::nullopt, arguments),
                   run_captured(image, limits_at(0), std::nullopt, arguments), label + " under the oracle");
        check(reports.empty(), label + ": the oracle saw no difference" + (reports.empty() ? "" : ": " + reports[0]));
        const auto statistics = cache.statistics();
        check_equal(statistics.native_mode, std::string("native"), label + ": machine code ran");
        check(statistics.region_counts.calls > 0 && statistics.region_counts.returns > 0,
              label + ": calls and returns ran in machine code");
    }
}

// --- The IR -----------------------------------------------------------------------

NEURA_TEST(tier3, a_recursive_function_is_not_inlined_into_itself) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    // Native after the recursion has reached its bottom and called the
    // other functions.
    nvm::TierCache cache(image, inline_options(40));
    run_captured(image, limits_at(2, &cache), std::nullopt, {"descend", "30"});
    const auto descend = function_named(image, "descend");
    const auto* ir = native_ir(cache, descend);
    check(ir != nullptr, "descend is a native program");
    if (ir == nullptr) return;
    check(!thresholds_kept() || ir->inlined_calls > 0, "descend's other callees are inlined");
    check(!inlines(*ir, descend), "descend is not inlined into itself");
    check(std::any_of(ir->insts.begin(), ir->insts.end(),
                      [&](const t3::Inst& in) { return in.op == t3::Op::Call && in.imm == descend; }),
          "descend calls itself");
}

NEURA_TEST(tier3, inlined_code_keeps_only_the_checkpoints_of_entries_and_loops) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    nvm::TierCache cache(image, inline_options());
    run_captured(image, limits_at(2, &cache), std::nullopt, {"calls", "20"});
    if (!thresholds_kept()) return;
    const auto* ir = native_ir(cache, function_named(image, "calls"));
    check(ir != nullptr && ir->inlined_calls >= 3 && ir->max_inline_depth >= 3, "calls inlines three levels");
    if (ir == nullptr) return;
    // Every Budget is where an entry goes in, or on a cycle.
    for (std::uint32_t b = 0; b < ir->blocks.size(); ++b) {
        const auto& block = ir->blocks[b];
        if (block.entry != t3::none) continue;
        if (std::any_of(block.preds.begin(), block.preds.end(),
                        [&](std::uint32_t p) { return ir->blocks[p].entry != t3::none; })) {
            continue;
        }
        const bool has_budget = std::any_of(block.insts.begin(), block.insts.end(),
                                            [&](std::uint32_t id) { return ir->insts[id].op == t3::Op::Budget; });
        if (!has_budget) continue;
        std::vector<std::uint8_t> seen(ir->blocks.size(), 0);
        std::vector<std::uint32_t> work(block.succs.begin(), block.succs.end());
        bool cycle = false;
        while (!work.empty() && !cycle) {
            const auto next = work.back();
            work.pop_back();
            cycle = next == b;
            if (seen[next]) continue;
            seen[next] = 1;
            for (const auto s : ir->blocks[next].succs) work.push_back(s);
        }
        check(cycle, "the Budget of block " + std::to_string(b) + " is on a loop");
    }
    // No inlined frame's state is at its function's entry block outside a loop.
    check(std::none_of(ir->insts.begin(), ir->insts.end(),
                       [&](const t3::Inst& in) {
                           return in.op == t3::Op::Budget && ir->states[in.state].caller != t3::none &&
                                  ir->states[in.state].block == 0;
                       }),
          "no inlined callee keeps its entry checkpoint");
    check(t3::verify(image, *ir).empty(), "the IR verifies");
}

NEURA_TEST(tier3, dispatch_records_keep_the_first_eight_distinct_tags_then_turn_megamorphic) {
    t3::DispatchRecord record;
    std::vector<std::uint64_t> tags;
    check(record.read(tags) && tags.empty(), "a new record is empty");
    for (const std::uint64_t tag : {5U, 0U, 5U, 9U, 0U}) record.record(tag);
    check(record.read(tags), "not megamorphic");
    check(tags == std::vector<std::uint64_t>{5, 0, 9}, "distinct tags in recording order, tag 0 included");
    for (std::uint64_t tag = 100; tag < 105; ++tag) record.record(tag);
    check(record.read(tags) && tags.size() == 8, "eight tags");
    record.record(9);
    check(record.read(tags), "a tag seen before changes nothing");
    record.record(200);
    check(!record.read(tags), "a ninth distinct tag makes the record megamorphic");
}

NEURA_TEST(tier3, programs_have_dispatch_records_only_while_tier_3_is_on) {
    if (!programs_run()) return;
    const auto image = inline_image(environment);
    for (const auto mode : {nvm::TierOptions::Tier3::Off, nvm::TierOptions::Tier3::Interp}) {
        auto options = inline_options();
        options.tier3 = mode;
        nvm::TierCache cache(image, options);
        run_captured(image, limits_at(2, &cache), std::nullopt, {"dispatch", "6"});
        const auto resolved = cache.program(function_named(image, "weigh"), std::uint64_t{1} << 40);
        check(resolved.region != nullptr && !resolved.region->dispatch_tables.empty(), "weigh has a program with a dispatch");
        if (resolved.region == nullptr || resolved.region->dispatch_tables.empty()) continue;
        const auto* record = cache.dispatch_record(*resolved.region, 0);
        if (mode == nvm::TierOptions::Tier3::Off) {
            check(record == nullptr, "no record with tier 3 off");
        } else {
            std::vector<std::uint64_t> tags;
            check(record != nullptr && record->read(tags) && !tags.empty(), "the program interpreter recorded tags");
        }
    }
}

// --- Materialization of inlined frames ----------------------------------------------

namespace {

// Function 0 calls 1, which calls 2, which calls 3; stopped by a step limit
// after 4 steps, the frames are
//   0: block 0 (the call block), slots {args, 7, uninitialized}
//   1: block 0, return slot 2 block 1, slots {7, 9, uninitialized}
//   2: block 0, return slot 2 block 1, slots {7, 13, uninitialized}
//   3: block 1, return slot 2 block 1, slots {7, 11}.
nvr::Image four_frames_image() {
    using Kind = nvr::TypeKind;
    using nvr::ValueKind;
    nvr::Image image;
    // 0 void, 1 int32, 2 text, 3 text[]
    image.types = {{Kind::Void}, {Kind::Signed, 32}, {Kind::Text}, {Kind::Array, 0, 2}};
    const auto number = [](std::uint8_t value) { return nvr::Constant{1, {value, 0, 0, 0}}; };
    image.constants = {number(7), number(9), number(11), number(0), number(13)};
    image.graphs = {nvr::Graph{}};
    const auto one_exit = [](nvr::Exit exit) {
        nvr::Block block;
        block.exits.push_back(std::move(exit));
        return block;
    };
    const auto call = [&](std::uint32_t target, nvr::Value argument, std::uint32_t assigned_constant) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::Call;
        exit.target = target;
        exit.arguments = {argument};
        exit.assignments = {{1, {ValueKind::Constant, 1, assigned_constant}}};
        exit.result_slot = 2;
        exit.resume = 1;
        return one_exit(std::move(exit));
    };
    const auto leave = [&](nvr::Value value) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::Return;
        exit.arguments = {value};
        return one_exit(std::move(exit));
    };
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 1;
    entry.frame_types = {3, 1, 1};
    entry.blocks = {call(1, {ValueKind::Constant, 1, 0}, 0), leave({ValueKind::Constant, 1, 3})};
    nvr::Function first;
    first.parameter_count = 1;
    first.result_type = 1;
    first.frame_types = {1, 1, 1};
    first.blocks = {call(2, {ValueKind::Slot, 1, 0}, 1), leave({ValueKind::Constant, 1, 3})};
    nvr::Function second;
    second.parameter_count = 1;
    second.result_type = 1;
    second.frame_types = {1, 1, 1};
    second.blocks = {call(3, {ValueKind::Slot, 1, 0}, 4), leave({ValueKind::Constant, 1, 3})};
    nvr::Function inner;
    inner.parameter_count = 1;
    inner.result_type = 1;
    inner.frame_types = {1, 1};
    nvr::Exit next;
    next.kind = nvr::ExitKind::Continue;
    next.assignments = {{1, {ValueKind::Constant, 1, 2}}};
    next.resume = 1;
    inner.blocks = {one_exit(std::move(next)), leave({ValueKind::Slot, 1, 1})};
    image.functions = {std::move(entry), std::move(first), std::move(second), std::move(inner)};
    return image;
}

// IR that deoptimizes at once with a chain of four frame states: the
// physical frame of function 0 and three inlined frames.
struct Hand {
    t3::Program program;
    nvm::tier2::Region programs[4];
};

std::unique_ptr<Hand> four_level_deopt() {
    auto hand = std::make_unique<Hand>();
    auto& ir = hand->program.ir;
    ir.function = 0;
    ir.slot_count = 3;
    ir.max_inline_depth = 3;
    for (std::uint32_t f = 0; f < 4; ++f) {
        hand->programs[f].function = f;
        ir.programs.push_back(&hand->programs[f]);
    }
    ir.blocks.emplace_back();
    ir.blocks.front().entry = 0;
    ir.entries.push_back({0, 0});
    const auto add = [&](t3::Inst in) {
        ir.insts.push_back(std::move(in));
        ir.blocks.front().insts.push_back(static_cast<std::uint32_t>(ir.insts.size() - 1));
        return static_cast<std::uint32_t>(ir.insts.size() - 1);
    };
    const auto constant = [&](std::uint64_t bits, std::uint8_t width) {
        t3::Inst in;
        in.op = t3::Op::Const;
        in.type = {t3::Type::Int, width};
        in.imm = bits;
        return add(std::move(in));
    };
    const auto seven = constant(7, 32);
    const auto thirteen = constant(13, 32);
    const auto one = constant(1, 1);
    const auto count = constant(4, 64);
    using Slot = t3::StateSlot;
    using Flag = t3::StateFlag;
    t3::FrameState physical;
    physical.function = 0;
    physical.block = 0;
    physical.program = 0;
    physical.slots = {{}, {Slot::Kind::Value, seven}, {}};
    physical.flags = {{}, {Flag::Kind::One, 0}, {}};
    ir.states.push_back(physical);
    const auto level = [&](std::uint32_t function, std::uint32_t block, std::vector<t3::StateSlot> slots,
                           std::vector<t3::StateFlag> flags) {
        t3::FrameState state;
        state.function = function;
        state.block = block;
        state.program = function;
        state.return_slot = 2;
        state.return_block = 1;
        state.slots = std::move(slots);
        state.flags = std::move(flags);
        state.caller = static_cast<std::uint32_t>(ir.states.size() - 1);
        ir.states.push_back(std::move(state));
    };
    level(1, 0, {{Slot::Kind::Value, seven}, {Slot::Kind::Const, 9}, {}}, {{Flag::Kind::Value, one}, {Flag::Kind::One, 0}, {}});
    level(2, 0, {{Slot::Kind::Const, 7}, {Slot::Kind::Value, thirteen}, {}}, {{Flag::Kind::One, 0}, {Flag::Kind::Value, one}, {}});
    level(3, 1, {{Slot::Kind::Value, seven}, {Slot::Kind::Const, 11}}, {{Flag::Kind::One, 0}, {Flag::Kind::One, 0}});
    ir.states.back().blocks = count;
    t3::Inst deopt;
    deopt.op = t3::Op::Deopt;
    deopt.operands[0] = count;
    deopt.state = static_cast<std::uint32_t>(ir.states.size() - 1);
    add(std::move(deopt));
    return hand;
}

struct Stopped {
    std::unique_ptr<nvm::Execution> execution;
    nvm::Event event;
};

Stopped stop_after(const nvr::Image& image, std::uint64_t steps) {
    nvm::RunLimits limits;
    limits.tier = 0;
    limits.max_steps = steps;
    Stopped stopped;
    stopped.execution = std::make_unique<nvm::Execution>(image, std::vector<std::string>{}, limits);
    stopped.event = stopped.execution->advance();
    return stopped;
}

} // namespace

NEURA_TEST(tier3, materializing_three_inlined_levels_builds_the_frames_push_frame_builds) {
    const auto image = four_frames_image();
    auto reference = stop_after(image, 4);
    check(reference.event.kind == nvm::Event::Kind::Trapped, "the reference run stopped at its step limit");
    const auto expected = reference.execution->tier3_frames();
    check_equal(expected.size(), std::size_t{4}, "four frames at the reference point");
    const auto hand = four_level_deopt();
    check(t3::verify(image, hand->program.ir).empty(), "the hand-built chain verifies");
    auto actual = stop_after(image, 0);
    check_equal(actual.execution->tier3_frames().size(), std::size_t{1}, "one physical frame");
    const auto ending = actual.execution->tier3_run(hand->program, 0);
    check_equal(ending.outcome, static_cast<unsigned>(t3::Outcome::Deopt), "the activation deoptimized");
    check_equal(ending.next_block, 1U, "tier 1 continues at the innermost frame's block");
    check_equal(ending.blocks, std::uint64_t{4}, "the blocks the state counts");
    const auto frames = actual.execution->tier3_frames();
    check_equal(frames.size(), expected.size(), "frame count");
    for (std::size_t i = 0; i < std::min(frames.size(), expected.size()); ++i) {
        const auto where = "frame " + std::to_string(i);
        check_equal(frames[i].function, expected[i].function, where + " function");
        check_equal(frames[i].block, expected[i].block, where + " block");
        check_equal(frames[i].return_slot, expected[i].return_slot, where + " return slot");
        check_equal(frames[i].return_block, expected[i].return_block, where + " return block");
        check_equal(frames[i].boundary, expected[i].boundary, where + " boundary");
        check(frames[i].slots == expected[i].slots, where + " slots");
        check(frames[i].initialized == expected[i].initialized, where + " initialized flags");
    }
    check_equal(actual.execution->steps(), reference.execution->steps(), "the steps flushed at the ending");
    // Each inlined frame returns into the program of the frame below it.
    check(frames[0].called_from == nullptr, "the physical frame keeps its called_from");
    for (std::size_t i = 1; i < frames.size(); ++i) {
        check(frames[i].called_from == &hand->programs[i - 1],
              "inlined frame " + std::to_string(i) + " was called from the program of the frame below it");
    }
}

NEURA_TEST(tier3, the_verifier_rejects_an_inlined_frame_not_called_at_its_callers_block) {
    const auto image = four_frames_image();
    const auto reports = [&](const t3::Function& ir) {
        const auto violations = t3::verify(image, ir);
        return std::any_of(violations.begin(), violations.end(),
                           [](const std::string& v) { return v.starts_with("V3") && v.find("inlined frame") != std::string::npos; });
    };
    {
        auto hand = four_level_deopt();
        hand->program.ir.states[1].block = 1;
        check(reports(hand->program.ir), "a caller level at a block that does not call");
    }
    {
        auto hand = four_level_deopt();
        hand->program.ir.states[2].return_slot = 1;
        check(reports(hand->program.ir), "an inlined frame with another return slot");
    }
    {
        auto hand = four_level_deopt();
        hand->program.ir.states[3].return_block = 0;
        check(reports(hand->program.ir), "an inlined frame with another return block");
    }
    {
        auto hand = four_level_deopt();
        hand->program.ir.states[3].function = 2;
        hand->program.ir.states[3].slots.push_back({});
        hand->program.ir.states[3].flags.push_back({});
        check(reports(hand->program.ir), "an inlined frame of a function its caller does not call there");
    }
}

// --- Multi-word results and aggregate components (NVR 1.7) ------------------------

namespace {

std::uint64_t count_ops(const t3::Function& ir, t3::Op op) {
    std::uint64_t count = 0;
    for (const auto& block : ir.blocks) {
        for (const auto id : block.insts) count += ir.insts[id].op == op;
    }
    return count;
}

// Compares native runs of `image` with tier 0 under every step limit up to
// `steps` (in steps of `stride` after the first hundred), and frame limits.
void sweep_limits(const nvr::Image& image, nvm::TierCache& cache, const std::string& label, std::uint64_t steps,
                  std::uint64_t stride) {
    for (std::uint64_t limit = 1; limit <= steps + 1; limit += limit < 100 ? 1 : stride) {
        compare_with_tier_0(image, cache, label + " with " + std::to_string(limit) + " steps", {},
                            [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
    }
    for (std::uint64_t frames = 1; frames <= 12; ++frames) {
        compare_with_tier_0(image, cache, label + " with " + std::to_string(frames) + " frames", {},
                            [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
    }
}

} // namespace

NEURA_TEST(tier3, multi_word_returns_run_through_inlined_and_native_calls_like_tier_0) {
    // tests/backend/tier/multi-result.nvra: F1 loops over add3 (F2), a leaf
    // that returns three words (int64, float64, text), which is inlined; F4
    // rec returns two words and calls itself, and F0 calls it and discards
    // another call's words.
    if (!programs_run()) return;
    const auto image = multi_result_image(environment);
    const auto reference = run_captured(image, limits_at(0));
    check(reference.result.completed, "the image runs at tier 0");
    nvm::TierCache cache(image, inline_options());
    for (int run = 0; run < 4; ++run) {
        check_same(run_captured(image, limits_at(2, &cache)), reference, "run " + std::to_string(run));
    }
    if (thresholds_kept()) {
        const auto* loop = native_ir(cache, 1);
        check(loop != nullptr, "the loop is a native program");
        if (loop != nullptr) {
            check(inlines(*loop, 2), "add3, which returns three words, is inlined into the loop");
            check(count_ops(*loop, t3::Op::Call) == 0 && count_ops(*loop, t3::Op::ResultWord) == 0,
                  "an inlined call has phis for its words, not result words");
        }
        const auto* rec = native_ir(cache, 4);
        check(rec != nullptr, "the recursive function is a native program");
        if (rec != nullptr) {
            check(!inlines(*rec, 4), "rec is not inlined into itself");
            check(count_ops(*rec, t3::Op::Call) >= 1 && count_ops(*rec, t3::Op::ResultWord) >= 1,
                  "rec calls itself and reads the second word of the result");
            const bool returns_two = std::any_of(rec->insts.begin(), rec->insts.end(), [](const t3::Inst& in) {
                return in.op == t3::Op::Return && in.count == 1;
            });
            check(returns_two, "rec returns a second word");
        }
        const auto* entry = native_ir(cache, 0);
        if (entry != nullptr) {
            // F0 discards the words of its second call: the Call has no result
            // words, and the first call's both words are read.
            check(count_ops(*entry, t3::Op::ResultWord) <= 2, "an entry that reads two words");
        }
    }
    if (programs_run() && jit::native::supported() && thresholds_kept()) {
        const auto statistics = cache.statistics();
        check_equal(statistics.native_mode, std::string("native"), "machine code ran");
        check(statistics.region_counts.calls > 0 && statistics.region_counts.returns > 0,
              "calls and returns ran in machine code");
    }
    // Deoptimizations inside the callees that return several words, at every
    // step limit, and frame limits through the recursion.
    sweep_limits(image, cache, "multi-word", reference.result.steps, 41);
    // --profile inlines nothing and keeps every frame the reference's.
    nvm::TierCache profiled(image, inline_options());
    for (int run = 0; run < 3; ++run) {
        auto limits = limits_at(2, &profiled, true);
        auto expected = limits_at(0, nullptr, true);
        check_same(run_captured(image, limits), run_captured(image, expected), "profile run " + std::to_string(run));
    }
    nvm::TierShadowHooks hooks;
    std::vector<std::string> reports;
    hooks.report = [&](const std::string& text) { reports.push_back(text); };
    hooks.notice = [&](const std::string& text) { reports.push_back(text); };
    nvm::TierCache checked(image, inline_options());
    auto limits = limits_at(2, &checked);
    limits.tier_shadow = &hooks;
    check_same(run_captured(image, limits), reference, "under the oracle");
    check(reports.empty(), "the oracle saw no difference" + (reports.empty() ? "" : ": " + reports[0]));
}

NEURA_TEST(tier3, aggregate_components_are_inline_heap_accesses_in_native_programs) {
    // tests/backend/tier/aggregates.nvra: component reads and writes, lengths
    // and versions of collections of aggregates run as heap accesses in the IR
    // (checked against the length in elements), the allocations, appends and
    // removals as helpers; traps are tier 0's.
    if (!programs_run()) return;
    const auto image = aggregate_image(environment);
    const auto reference = run_captured(image, limits_at(0));
    check(reference.result.completed, "the image runs at tier 0");
    nvm::TierCache cache(image, inline_options(1));
    // A program waits for its callees' programs (F7 has none) up to 16 arrivals.
    for (int run = 0; run < 80; ++run) {
        check_same(run_captured(image, limits_at(2, &cache)), reference, "run " + std::to_string(run));
    }
    if (thresholds_kept()) {
        std::uint64_t loads = 0;
        std::uint64_t stores = 0;
        std::uint64_t helpers = 0;
        std::uint64_t state_calls = 0;
        for (std::uint32_t function = 0; function < image.functions.size(); ++function) {
            const auto* ir = native_ir(cache, function);
            if (ir == nullptr) continue;
            for (const auto& in : ir->insts) {
                // A component access names its component in `aux`.
                loads += in.op == t3::Op::LoadElement && in.aux != t3::none;
                stores += in.op == t3::Op::StoreElement && in.aux != t3::none;
                helpers += in.op == t3::Op::Helper && in.aux < ir->exits.size() &&
                           (ir->exits[in.aux]->operation == nvr::StateOp::AppendElement ||
                            ir->exits[in.aux]->operation == nvr::StateOp::RemoveElement ||
                            ir->exits[in.aux]->operation == nvr::StateOp::AllocateCollection ||
                            ir->exits[in.aux]->operation == nvr::StateOp::RepeatCollection);
                state_calls += in.op == t3::Op::StateCall && (in.flags & t3::CheckReference) == 0 &&
                               (ir->exits[in.aux]->operation == nvr::StateOp::ReadComponent ||
                                ir->exits[in.aux]->operation == nvr::StateOp::WriteComponent);
            }
        }
        check(loads > 0 && stores > 0, "components are loaded and stored inline: " + std::to_string(loads) + " loads, " +
                                           std::to_string(stores) + " stores");
        check(helpers > 0, "collections of aggregates are built and changed by helpers");
        check_equal(state_calls, std::uint64_t{0}, "no component access is a reference state call");
    }
    sweep_limits(image, cache, "aggregates", reference.result.steps, 53);
    for (const auto count : {1, 2, 3, 4}) {
        const std::vector<std::string> arguments(count, "x");
        const auto expected = run_captured(image, limits_at(0), std::nullopt, arguments);
        check(!expected.result.completed, "case " + std::to_string(count) + " traps at tier 0");
        nvm::TierCache trapping(image, inline_options(1));
        for (int run = 0; run < 3; ++run) {
            check_same(run_captured(image, limits_at(2, &trapping), std::nullopt, arguments), expected,
                       "case " + std::to_string(count) + " run " + std::to_string(run));
        }
    }
    // Heap limits land inside the aggregate allocations and appends.
    for (std::uint64_t cells = 1; cells <= reference.result.peak_heap_cells + 1; cells += cells < 40 ? 1 : 11) {
        compare_with_tier_0(image, cache, "heap limit " + std::to_string(cells), {},
                            [&](nvm::RunLimits& limits) { limits.max_heap_cells = cells; });
    }
}

} // namespace neura::testing
