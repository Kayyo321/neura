// Floats in tier 3 (design 7: src/nvm/tier3_float.cpp, src/jit/tier3_emit.cpp):
// tests/backend/tier3/floats.nra run with native programs, on machine code
// and on the IR interpreter, compared with tier 0 bit for bit: NaN payloads
// of both signs, quiet and signalling, through every binary64 and binary32
// operation, negation, abs, stores, appends, to_bits, hash codes, comparisons,
// conversions, call arguments and returned values, and kept across a call
// that destroys the vector registers; a NaN native code made in
// the frame of a deoptimization; an entry refused while a float slot holds
// a NaN its code takes as canonical; square roots of special values with the
// kernel's step at every step limit and under --profile; inline square roots
// in inlined code; a rounding mode other than the default, where no native
// float code runs; and nbody, whose square roots run with no kernel helper.
// Part of the `tier3` suite (backend-tier3).
#include "graph_test_support.hpp"

#include "jit/tier3_native.hpp"
#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvm/tier3.hpp"
#include "nvr/names.hpp"

#include <algorithm>
#include <cfenv>
#include <functional>

namespace neura::testing {
namespace {

using Back = nvm::TierOptions::Tier3;

// Lifts every block, forms every program at its `region_after`th arrival
// (at the first, every block not yet run is a stub that deoptimizes; later,
// the blocks of a loop that has run have code), and turns every program
// native at its `native_after`th checkpoint arrival, on the back end `back`.
nvm::TierOptions float_options(Back back, std::uint32_t native_after = 1, std::uint32_t region_after = 1) {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = region_after;
    options.programs = nvm::TierOptions::Programs::On;
    options.tier3 = back;
    options.native_after = native_after;
    return options;
}

const char* name_of(Back back) { return back == Back::Native ? "machine code" : "the IR interpreter"; }

// Whether the back end runs native programs here (machine code needs
// x86-64 Linux).
bool runs(Back back) {
    if (nvm::tier_ceiling() < 2) return false;
    return back == Back::Interp || jit::native::supported();
}

using Adjust = std::function<void(nvm::RunLimits&)>;

Captured run_at(const nvr::Image& image, std::uint8_t tier, nvm::TierCache* cache,
                const std::vector<std::string>& arguments, const Adjust& adjust) {
    nvm::RunLimits limits;
    limits.max_steps = 50'000'000;
    limits.tier = tier;
    limits.tier_cache = cache;
    if (adjust) adjust(limits);
    return run_captured(image, limits, std::nullopt, arguments);
}

void compare(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
             const std::vector<std::string>& arguments, const Adjust& adjust = {}) {
    const auto actual = run_at(image, 2, &cache, arguments, adjust);
    const auto expected = run_at(image, 0, nullptr, arguments, adjust);
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
}

// Every value from `first` to `last` when there are at most `every`; else
// the first `every`, the last `every / 4`, and one in 17 between.
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

// The whole run, then every step limit, with one cache.
void step_sweep(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                const std::vector<std::string>& arguments, bool profile = false) {
    const auto profiled = [&](nvm::RunLimits& limits) { limits.profile = profile; };
    compare(image, cache, label, arguments, profiled);
    const auto steps = run_at(image, 0, nullptr, arguments, profiled).result.steps;
    for (const auto limit : limits_from(1, steps + 1, 3000)) {
        compare(image, cache, label + " with " + std::to_string(limit) + " steps", arguments,
                [&](nvm::RunLimits& limits) {
                    limits.max_steps = limit;
                    limits.profile = profile;
                });
    }
}

nvr::Image floats_image(const Environment& environment) {
    return build_program({backend_dir(environment) / "tier3" / "floats.nra"});
}

} // namespace

NEURA_TEST(tier3, nan_payloads_through_every_float_operation_match_tier_0_bit_for_bit) {
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        for (const auto& arguments : std::vector<std::vector<std::string>>{{"wide", "2"},
                                                                         {"narrow", "2"},
                                                                         {"integers", "2"},
                                                                         {"spilled", "8"},
                                                                         {"appends", "8"},
                                                                         {"kept", "16"}}) {
            nvm::TierCache cache(image, float_options(back));
            compare(image, cache, arguments[0] + on, arguments);
            // Formed once the loops have run: their code is all native.
            nvm::TierCache warm(image, float_options(back, 3, 3));
            compare(image, warm, arguments[0] + " formed late" + on, arguments);
            // --profile: native programs inline nothing, so every call is
            // a call.
            compare(image, cache, arguments[0] + " with --profile" + on, arguments,
                    [](nvm::RunLimits& limits) { limits.profile = true; });
            if (runs(back)) {
                const auto statistics = cache.statistics();
                check(statistics.native.entered > 0 && statistics.native.steps > 0,
                      arguments[0] + on + ": native programs ran");
                const auto late = warm.statistics();
                check(late.native.steps * 2 > run_at(image, 0, nullptr, arguments, {}).result.steps,
                      arguments[0] + " formed late" + on + ": native programs ran most steps");
            }
        }
        // Conversions just outside their ranges trap where tier 0 traps.
        for (int which = 0; which < 6; ++which) {
            nvm::TierCache cache(image, float_options(back));
            compare(image, cache, "outside " + std::to_string(which) + on, {"outside", std::to_string(which)});
            nvm::TierCache warm(image, float_options(back, 2, 2));
            compare(image, warm, "outside " + std::to_string(which) + " formed late" + on,
                    {"outside", std::to_string(which)});
        }
    }
}

NEURA_TEST(tier3, a_nan_native_code_made_is_written_canonical_when_the_code_deoptimizes) {
    // The late branch has no code until it first runs: native code
    // deoptimizes there with the hardware's NaN of 0 / 0 in the frame, and
    // tier 1 reads its bits.
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache cache(image, float_options(back));
        step_sweep(image, cache, "late" + on, {"late", "5"});
        if (runs(back)) {
            check(cache.statistics().native.exits > 0, "late" + on + ": native code deoptimized at an exit");
        }
    }
}

NEURA_TEST(tier3, an_entry_whose_float_slot_holds_a_nan_payload_that_code_takes_as_canonical_is_refused) {
    // `accumulate`'s code takes its parameter as no NaN but the canonical one
    // where it joins the sum (Function::clean_slots): called with a NaN with
    // a payload, its entry is refused, and with any other value it runs.
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache cache(image, float_options(back));
        step_sweep(image, cache, "entries" + on, {"entries", "2"});
        if (!runs(back)) continue;
        const auto statistics = cache.statistics();
        check(statistics.native.refused > 0, "entries" + on + ": an entry was refused");
        check(statistics.native.entered > 0, "entries" + on + ": the other entries ran");
    }
}

NEURA_TEST(tier3, square_roots_of_special_values_and_their_steps_match_tier_0_at_every_limit) {
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache cache(image, float_options(back));
        compare(image, cache, "roots" + on, {"roots", "2"});
        step_sweep(image, cache, "root steps" + on, {"root-steps", "3"});
        if (!runs(back)) continue;
        check(cache.statistics().native.steps > 0, "roots" + on + ": native programs ran");
    }
}

NEURA_TEST(tier3, square_roots_under_profile_count_the_kernel_step_where_tier_0_does) {
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache cache(image, float_options(back));
        step_sweep(image, cache, "root steps with --profile" + on, {"root-steps", "3"}, true);
        compare(image, cache, "lengths with --profile" + on, {"lengths", "25"},
                [](nvm::RunLimits& limits) { limits.profile = true; });
    }
}

NEURA_TEST(tier3, inline_square_roots_in_inlined_calls_keep_every_step) {
    // `scaled` (no square root) and `length` (one) are inlined into
    // `lengths`, which has its own and then calls `countdown` (none, not
    // inlined). Inlining `length` into `plain_lengths` gives that caller
    // its first kernel step.
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache cache(image, float_options(back, 3));
        step_sweep(image, cache, "lengths" + on, {"lengths", "25"});
        if (!runs(back) || nvm::tier_eager() || nvm::tier3_after() != 0) continue;
        check(cache.statistics().native_inlined >= 2, "lengths" + on + ": the calls were inlined");
    }
}

NEURA_TEST(tier3, no_native_float_code_runs_in_a_rounding_mode_other_than_the_default) {
    // `roots_only`'s only float operation is the inline square root; rounding
    // upward, sqrtsd would round the roots of 2, 3, 5 ... up, and the kernel
    // (an exact integer method) does not.
    const auto image = floats_image(environment);
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache cache(image, float_options(back));
        for (const auto* kind : {"roots-only", "lengths", "wide"}) {
            compare(image, cache, std::string(kind) + " in the default rounding mode" + on, {kind, "2"});
        }
        const auto before = cache.statistics();
        if (runs(back)) {
            check(before.native.entered > 0 && before.native.refused == 0,
                  "the default rounding mode enters native code and refuses nothing" + on);
        }
        const auto saved = std::fegetround();
        std::fesetround(FE_UPWARD);
        std::vector<std::pair<Captured, Captured>> runs_upward;
        try {
            for (const auto* kind : {"roots-only", "lengths", "wide"}) {
                runs_upward.emplace_back(run_at(image, 2, &cache, {kind, "2"}, {}),
                                         run_at(image, 0, nullptr, {kind, "2"}, {}));
            }
        } catch (...) {
            std::fesetround(saved);
            throw;
        }
        std::fesetround(saved);
        for (const auto& [native, reference] : runs_upward) {
            check_same_run(native.result, reference.result, "rounding upward" + on);
            check_equal(native.output, reference.output, "rounding upward" + on + " output");
        }
        if (runs(back)) {
            check(cache.statistics().native.refused > before.native.refused,
                  "native float code is refused when rounding upward" + on);
        }
    }
}

// A host request can change the floating environment between two native
// chains in one execution. Restore the embedding thread's mode on all exits.
class RoundingConsole final : public nvm::HostService {
public:
    ~RoundingConsole() override { std::fesetround(saved_); }
    const nvm::HostSignature& signature() const override { return console.signature(); }
    nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
        auto completion = console.invoke(request);
        check(std::fesetround(console.requests.size() == 1 ? FE_UPWARD : FE_TONEAREST) == 0,
              "the host changed its rounding mode");
        return completion;
    }
    CapturingConsole console;
private:
    int saved_ = std::fegetround();
};

Captured run_with_rounding_changes(const nvr::Image& image, nvm::TierCache* cache) {
    RoundingConsole host;
    nvm::HostRegistry registry;
    registry.add(host);
    nvm::RunLimits limits;
    limits.tier = cache ? 2 : 0;
    limits.tier_cache = cache;
    auto result = nvm::run(image, nvm::bind(image, registry), {}, limits);
    return {std::move(result), host.console.output(), std::move(host.console.requests)};
}

NEURA_TEST(tier3, float_callee_cells_follow_host_rounding_changes_at_large_frame_boundaries) {
    auto image = build_program({backend_dir(environment) / "tier3" / "float-callee.nra"});
    const auto names = nvr::decode_names(image).value();
    const auto root = std::find_if(names.functions.begin(), names.functions.end(),
                                   [](const auto& name) { return name.ends_with(".root_bits"); });
    check(root != names.functions.end(), "the floating callee is present");
    auto& slots = image.functions.at(root - names.functions.begin()).frame_types;
    for (const auto count : {64u, 65u, 128u, 129u}) {
        slots.resize(count, slots.front());
        const auto padded = nvr::load(nvr::encode(image));
        const auto expected = run_with_rounding_changes(padded, nullptr);
        check(expected.result.completed && expected.requests.size() == 3, "three reference roots");
        auto options = float_options(Back::Native, 2, 2);
        options.native_inlining = false;
        nvm::TierCache cache(padded, options);
        const auto actual = run_with_rounding_changes(padded, &cache);
        const auto label = "floating callee of " + std::to_string(count) + " slots";
        check_same_run(actual.result, expected.result, label);
        check_equal(actual.output, expected.output, label + " across host rounding changes");
        const auto stats = cache.statistics();
        for (const auto depth : {1u, 2u, 3u}) {
            compare(padded, cache, label + " at a frame limit", {},
                    [=](nvm::RunLimits& limits) { limits.max_frames = depth; });
        }
        if (!runs(Back::Native) || nvm::tier_eager() || nvm::tier3_after() != 0) continue;
        check(stats.native.refused > 0, label + " refuses hardware square root while rounding upward");
        if (count <= 128) {
            check(stats.native.cell_entries > 60, label + " reaches the floating callee through a cell");
        } else {
            check(stats.native.cell_entries < 60 && stats.native.helper_entries > 60,
                  label + " uses the slow push above the inline frame bound");
        }
    }
}

NEURA_TEST(tier3, inlining_a_first_kernel_into_an_integer_loop_keeps_the_kernel_steps) {
    const auto image = build_program({backend_dir(environment) / "tier3" / "float-callee.nra"});
    const auto names = nvr::decode_names(image).value();
    const auto caller = std::find_if(names.functions.begin(), names.functions.end(),
                                     [](const auto& name) { return name.ends_with(".repeat"); });
    check(caller != names.functions.end(), "the integer loop is present");
    for (const auto back : {Back::Native, Back::Interp}) {
        nvm::TierCache cache(image, float_options(back, 3, 2));
        compare(image, cache, "promoted integer loop", {});
        const auto stop = run_at(image, 0, nullptr, {}, {}).result.steps;
        for (const auto limit : limits_from(1, stop + 1, 90)) {
            compare(image, cache, "promoted integer loop at step " + std::to_string(limit), {},
                    [=](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
        if (!runs(back) || nvm::tier_eager() || nvm::tier3_after() != 0) continue;
        const auto resolved = cache.program(caller - names.functions.begin(), std::uint64_t{1} << 40);
        check(resolved.region != nullptr, "the integer loop has a function program");
        const auto* code = cache.native_code(*resolved.region, nvm::tier3::Variant::SparseFrames);
        if (!code) code = cache.native_code(*resolved.region, nvm::tier3::Variant::Standard);
        check(code && code->ir.inline_kernels && code->ir.pass_counts.contains("inline.kernel_counter"),
              "inlining the floating callee gave the integer loop a kernel counter");
    }
}

NEURA_TEST(tier3, nbody_at_its_quick_size_calls_no_kernel_helper_in_native_code) {
    // The square roots of nbody's loops are inline: the kernel helpers its
    // native programs call are those of its last lines (formatting the
    // energies), as many at the quick size as at twice it.
    const auto source = backend_dir(environment).parent_path() / "bench-wall" / "neura" / "nbody.nra";
    const auto image = build_program({source});
    for (const auto back : {Back::Native, Back::Interp}) {
        const std::string on = std::string(" on ") + name_of(back);
        nvm::TierCache quick(image, float_options(back));
        compare(image, quick, "nbody 250" + on, {"250"});
        nvm::TierCache twice(image, float_options(back));
        compare(image, twice, "nbody 500" + on, {"500"});
        if (!runs(back)) continue;
        const auto at_quick = quick.statistics();
        const auto at_twice = twice.statistics();
        check(at_quick.native.steps > 0 && at_twice.native.steps > at_quick.native.steps,
              "nbody" + on + ": native programs ran the loops");
        check_equal(at_twice.native.kernel_helpers, at_quick.native.kernel_helpers,
                    "nbody" + on + ": kernel helpers outside the loops only");
        check(at_quick.native.kernel_helpers < 10, "nbody" + on + ": a few kernel helpers");
        const auto line = "tier: native kernel helpers " + std::to_string(at_quick.native.kernel_helpers) + "\n";
        check(nvm::describe(at_quick).find(line) != std::string::npos, "nbody" + on + ": --tier-stats shows them");
    }
}

} // namespace neura::testing
