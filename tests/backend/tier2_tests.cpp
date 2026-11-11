// Tier 2 regions in the executor (docs/runtime/NVM.md, "Execution tiers"):
// a run whose proven blocks run in regions is observably the same as one
// that runs every block at tier 0, wherever a check fails, a limit is
// reached, a collection runs, or the scheduler's quantum ends a turn; and
// regions are formed as src/nvm/tier2.hpp describes. Part of the `tier`
// suite (backend-tier).
#include "graph_test_support.hpp"

#include "nvm/fleet.hpp"
#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvr/format.hpp"

#include <algorithm>

namespace neura::testing {
namespace {

// Lifts every block and forms every region the first time it runs.
nvm::TierOptions eager_regions() {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
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

void check_same(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
    for (std::size_t i = 0; i < std::min(actual.requests.size(), expected.requests.size()); ++i) {
        check_same_request(actual.requests[i], expected.requests[i], label);
    }
}

// Runs `image` at tier 0 and at tier 2 with `cache` (eager regions unless
// given), under `adjust`, and checks the runs agree.
template <typename Adjust>
void compare_with_tier_0(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                         const std::vector<std::string>& arguments, Adjust adjust) {
    auto reference_limits = limits_at(0);
    adjust(reference_limits);
    const auto reference = run_captured(image, reference_limits, std::nullopt, arguments);
    auto region_limits = limits_at(2, &cache);
    adjust(region_limits);
    check_same(run_captured(image, region_limits, std::nullopt, arguments), reference, label);
}

void compare_with_tier_0(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                         const std::vector<std::string>& arguments = {}) {
    compare_with_tier_0(image, cache, label, arguments, [](nvm::RunLimits&) {});
}

fs::path tier_program(const Environment& environment, const std::string& name) {
    return backend_dir(environment) / "tier" / (name + ".nra");
}

bool regions_enabled() { return nvm::tier_ceiling() >= 2; }

// --- A hand-built image ---------------------------------------------------
//
// A walk along a linked array, every block of it without neurons, so the
// test controls each block exactly:
//
//   block 0: flags = [false, ..., true]       (allocation)
//   block 1: next = [1, 2, ..., n - 1, 0]     (allocation, uint64 indices)
//   block 2: i = 0
//   block 3: i = next[i]                      (inline element read)
//   block 4: c = flags[i]                     (inline element read)
//   block 5: selector c: 0 continue at 3, 1 return 0
//
// With `selector_lanes` 2, block 4 reads an int32 from `codes` instead and
// block 5 takes the low two bits of it as the selector of three exits:
// 0 and 1 continue at 3, 2 returns, and 3 is an invalid selector.
struct Walk {
    std::uint32_t length = 8;
    // An index stored at next[bad_at], out of bounds when it is not below
    // the length.
    std::uint32_t bad_at = 0xffffffffU;
    std::uint32_t bad_index = 0;
    // Uses int32 codes and three exits; the code at `invalid_at` is 3.
    bool codes = false;
    std::uint32_t invalid_at = 0xffffffffU;
};

nvr::Image walk_image(const Walk& walk) {
    nvr::Image image;
    // 0 void, 1 bool, 2 int32, 3 text, 4 text[], 5 int32[], 6 bool[], 7 uint64,
    // 8 uint64[]
    image.types = {{nvr::TypeKind::Void, 0, 0},   {nvr::TypeKind::Bool, 0, 0},  {nvr::TypeKind::Signed, 32, 0},
                   {nvr::TypeKind::Text, 0, 0},   {nvr::TypeKind::Array, 0, 3}, {nvr::TypeKind::Array, 0, 2},
                   {nvr::TypeKind::Array, 0, 1},  {nvr::TypeKind::Unsigned, 64, 0},
                   {nvr::TypeKind::Array, 0, 7}};
    const auto int32_constant = [&](std::uint32_t value) {
        nvr::Constant constant{2, {}};
        for (int i = 0; i < 4; ++i) constant.payload.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        image.constants.push_back(constant);
        return nvr::Value{nvr::ValueKind::Constant, 2, static_cast<std::uint32_t>(image.constants.size() - 1)};
    };
    const auto index_constant = [&](std::uint64_t value) {
        nvr::Constant constant{7, {}};
        for (int i = 0; i < 8; ++i) constant.payload.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        image.constants.push_back(constant);
        return nvr::Value{nvr::ValueKind::Constant, 7, static_cast<std::uint32_t>(image.constants.size() - 1)};
    };
    const auto bool_constant = [&](bool value) {
        image.constants.push_back({1, {static_cast<std::uint8_t>(value ? 1 : 0)}});
        return nvr::Value{nvr::ValueKind::Constant, 1, static_cast<std::uint32_t>(image.constants.size() - 1)};
    };
    // Graph 0: no lanes. Graph 1: one bool lane, the selector. Graph 2:
    // the low two lanes of an int32.
    image.graphs.push_back({0, {}, {}});
    image.graphs.push_back({1, {}, {0}});
    image.graphs.push_back({32, {}, {0, 1}});

    nvr::Function function;
    function.parameter_count = 1;
    function.result_type = 2;
    // Slots: 0 arguments, 1 flags, 2 next, 3 i, 4 c (or the code), 5 codes
    function.frame_types = {4, 6, 8, 7, walk.codes ? 2U : 1U, 5};
    const auto slot = [](std::uint32_t index, std::uint32_t type) {
        return nvr::Value{nvr::ValueKind::Slot, type, index};
    };
    const auto allocate = [&](std::uint32_t type, std::vector<nvr::Value> elements, std::uint32_t result,
                              std::uint32_t resume) {
        nvr::Block block;
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::State;
        exit.operation = nvr::StateOp::AllocateCollection;
        exit.immediate = type;
        exit.arguments = std::move(elements);
        exit.result_slot = result;
        exit.resume = resume;
        block.exits.push_back(exit);
        return block;
    };
    std::vector<nvr::Value> flags;
    std::vector<nvr::Value> next;
    std::vector<nvr::Value> codes;
    for (std::uint32_t i = 0; i < walk.length; ++i) {
        flags.push_back(bool_constant(i + 1 == walk.length));
        next.push_back(index_constant(i == walk.bad_at ? walk.bad_index : (i + 1) % walk.length));
        codes.push_back(int32_constant(i == walk.invalid_at ? 3 : i + 1 == walk.length ? 2 : i % 2));
    }
    // Block 0 also allocates the codes when they are used; block 0b keeps
    // the numbering: codes go in slot 5 from a block of their own.
    function.blocks.push_back(allocate(6, flags, 1, walk.codes ? 6 : 1));
    function.blocks.push_back(allocate(8, next, 2, 2));
    {
        nvr::Block block;
        nvr::Exit exit;
        exit.assignments = {{3, index_constant(0)}};
        exit.resume = 3;
        block.exits.push_back(exit);
        function.blocks.push_back(block);
    }
    const auto read = [&](nvr::Value array, std::uint32_t result, std::uint32_t resume) {
        nvr::Block block;
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::State;
        exit.operation = nvr::StateOp::ReadElement;
        exit.arguments = {array, slot(3, 7)};
        exit.result_slot = result;
        exit.resume = resume;
        block.exits.push_back(exit);
        return block;
    };
    function.blocks.push_back(read(slot(2, 8), 3, 4));
    function.blocks.push_back(walk.codes ? read(slot(5, 5), 4, 5) : read(slot(1, 6), 4, 5));
    {
        nvr::Block block;
        block.graph = walk.codes ? 2 : 1;
        block.inputs = {slot(4, walk.codes ? 2U : 1U)};
        block.selector_start = 0;
        nvr::Exit again;
        again.resume = 3;
        nvr::Exit done;
        done.kind = nvr::ExitKind::Return;
        done.arguments = {int32_constant(0)};
        block.exits = walk.codes ? std::vector<nvr::Exit>{again, again, done} : std::vector<nvr::Exit>{again, done};
        function.blocks.push_back(block);
    }
    if (walk.codes) function.blocks.push_back(allocate(5, codes, 5, 1));
    image.functions.push_back(function);
    image.entry = 0;
    image.minor_version = nvr::required_minor_version(image);
    // Every image a test runs goes through the encoder and the loader.
    return nvr::load(nvr::encode(image));
}

// Every block of a walk image is graphless: its tier-1 form is known
// without proofs.
nvm::tier2::BlockForm walk_form(const nvr::Image& image, std::uint32_t block) {
    nvm::tier2::BlockForm form;
    const auto& decoded = image.functions[0].blocks[block];
    const auto& graph = image.graphs[decoded.graph];
    form.kind = graph.outputs.empty() ? (decoded.exits.size() == 1 ? nvm::tier2::BlockForm::Kind::Empty
                                                                   : nvm::tier2::BlockForm::Kind::None)
                                      : nvm::tier2::BlockForm::Kind::Wired;
    return form;
}

} // namespace

NEURA_TEST(tier, regions_are_formed_as_specified_on_a_hand_built_loop) {
    const auto image = walk_image({});
    const auto formed = nvm::tier2::form_region(image, 0, 3, [&](std::uint32_t b) { return walk_form(image, b); });
    check(formed.region != nullptr, "a region starts at the loop's head: " + formed.reason);
    if (!formed.region) return;
    const auto& region = *formed.region;
    check(region.blocks == std::vector<std::uint32_t>({3, 4, 5}), "the loop's three blocks form the region");
    // The head is the entry and the target of the back edge: one
    // checkpoint, covering the three blocks of an iteration.
    check_equal(region.checkpoints.size(), std::size_t{1}, "one checkpoint");
    check_equal(region.checkpoints[0].block, std::uint32_t{3}, "the checkpoint is the head");
    check_equal(region.checkpoints[0].steps, std::uint64_t{3}, "an iteration is three steps");
    // next (slot 2), i (slot 3), and flags (slot 1) are read before any
    // write; c (slot 4) is written before it is read.
    std::vector<std::uint32_t> early;
    for (const auto r : region.entry_reads) early.push_back(region.slots[r]);
    std::sort(early.begin(), early.end());
    check(early == std::vector<std::uint32_t>({1, 2, 3}), "the slots read before a write");
    std::vector<std::uint32_t> written;
    for (const auto r : region.written) written.push_back(region.slots[r]);
    std::sort(written.begin(), written.end());
    check(written == std::vector<std::uint32_t>({3, 4}), "the slots written");
    // Two inline element reads, the return as a deoptimization, and the back
    // edge as a jump that completes block 5.
    std::size_t states = 0;
    std::size_t deopts = 0;
    bool back_edge = false;
    for (const auto& op : region.code) {
        if (op.code == nvm::tier2::Code::State) ++states;
        if (op.code == nvm::tier2::Code::Deopt) {
            ++deopts;
            check_equal(op.block, std::uint32_t{5}, "the return deoptimizes at its block");
        }
        if (op.code == nvm::tier2::Code::Jump && op.target == 0 && (op.flags & nvm::tier2::completes)) back_edge = true;
    }
    check_equal(states, std::size_t{2}, "the element reads run inline");
    check_equal(deopts, std::size_t{1}, "the return leaves through tier 1");
    check(back_edge, "the back edge returns to the checkpoint");
    // Allocations are not inline: no region starts at the allocating
    // blocks, and a region at block 2 ends before them.
    const auto at_allocation =
        nvm::tier2::form_region(image, 0, 0, [&](std::uint32_t b) { return walk_form(image, b); });
    check(!at_allocation.region, "no region starts at an allocation");
    // Run it: tier 2 agrees with tier 0, and the loop ran in the region.
    nvm::TierCache cache(image, eager_regions());
    compare_with_tier_0(image, cache, "walk");
    if (regions_enabled()) {
        const auto statistics = cache.statistics();
        check(statistics.regions >= 1 && statistics.steps.region > 0, "the walk ran in a region");
    }
}

NEURA_TEST(tier, the_step_limit_lands_on_every_block_of_a_region) {
    // A walk of 40 elements: 117 loop steps. Every limit from 1 to past the
    // end stops the run at the same block, with the same counts, at both
    // tiers; inside the loop the region's budget check hands the last
    // blocks to tier 1.
    const auto image = walk_image({40});
    const auto total = run_captured(image, limits_at(0)).result.steps;
    check(total > 100, "the walk is long enough");
    nvm::TierCache cache(image, eager_regions());
    for (std::uint64_t steps = 1; steps <= total + 1; ++steps) {
        compare_with_tier_0(image, cache, "walk with " + std::to_string(steps) + " steps", {},
                            [&](nvm::RunLimits& limits) { limits.max_steps = steps; });
    }
    if (regions_enabled()) {
        check(cache.statistics().region_counts.budget > 0, "the budget sent blocks to tier 1");
    }
}

NEURA_TEST(tier, a_trap_in_an_inline_state_operation_matches_the_reference) {
    // next[k] is out of bounds for every k before the last element (the
    // walk returns there): the element read of flags
    // (block 4) traps inside the region on iteration k + 1.
    for (std::uint32_t k = 0; k < 11; ++k) {
        const auto image = walk_image({12, k, 1000});
        nvm::TierCache cache(image, eager_regions());
        compare_with_tier_0(image, cache, "an index out of bounds at " + std::to_string(k));
        const auto run = run_captured(image, limits_at(2, &cache)).result;
        check(!run.completed && run.trap.kind == nvm::TrapKind::IndexOutOfBounds,
              "the walk traps out of bounds: " + describe_trap(run, image));
        check_equal(run.trap.block, std::uint32_t{4}, "at the element read");
        if (regions_enabled() && k > 1) {
            check(cache.statistics().region_counts.state_traps > 0, "the region deoptimized at the trap");
        }
    }
    // An invalid selector (3 of three exits) in a block whose lanes are
    // its input's: the region's switch deoptimizes and tier 1 traps.
    for (std::uint32_t k = 1; k < 10; ++k) {
        Walk walk;
        walk.length = 12;
        walk.codes = true;
        walk.invalid_at = k;
        const auto image = walk_image(walk);
        nvm::TierCache cache(image, eager_regions());
        compare_with_tier_0(image, cache, "an invalid selector at " + std::to_string(k));
        const auto run = run_captured(image, limits_at(2, &cache)).result;
        check(!run.completed && run.trap.kind == nvm::TrapKind::InvalidSelector, "the walk traps at the selector");
    }
}

NEURA_TEST(tier, every_check_of_a_region_deoptimizes_to_the_same_trap) {
    // Overflow, bounds, a nil field read, modification during iteration,
    // division by zero, a class cast, a shift count, and a narrowing
    // conversion, each failing on every iteration from 0 to 11 of a loop
    // that runs in regions, and the loop without failure: the same trap
    // (kind, reason, text, function, block, callers), steps, and function
    // steps at both tiers.
    const auto image = build_program({tier_program(environment, "region-guards")});
    nvm::TierCache cache(image, eager_regions());
    for (int mode = 0; mode <= 8; ++mode) {
        for (int k = 0; k < 12; ++k) {
            compare_with_tier_0(image, cache, "check " + std::to_string(mode) + " at " + std::to_string(k),
                                {std::to_string(mode), std::to_string(k)});
        }
    }
    if (regions_enabled()) {
        const auto counts = cache.statistics().region_counts;
        check(counts.checks > 0, "regions deoptimized at failed checks");
        check(counts.state_traps > 0, "regions deoptimized at trapping state operations");
    }
}

NEURA_TEST(tier, references_are_written_back_before_a_region_ends_at_an_allocation) {
    // A local read inside a region keeps its cell alive across the
    // allocation where the region ends. Under heap limits from tight to
    // loose, collections run at those allocations; peak heap cells, the
    // result, and any heap-limit trap are those of tier 0, which they would
    // not be if the slot held its old reference when the collector ran.
    const auto image = build_program({tier_program(environment, "region-gc")});
    nvm::TierCache cache(image, eager_regions());
    const auto peak = run_captured(image, limits_at(0)).result.peak_heap_cells;
    for (std::uint64_t cells = 64; cells <= peak + 64; cells += std::max<std::uint64_t>(1, peak / 97)) {
        compare_with_tier_0(image, cache, "region-gc with " + std::to_string(cells) + " heap cells", {},
                            [&](nvm::RunLimits& limits) { limits.max_heap_cells = cells; });
    }
    if (regions_enabled()) {
        check(cache.statistics().steps.region > 0, "the loops ran in regions");
    }
}

NEURA_TEST(tier, nested_loops_with_element_and_field_operations_match_at_every_step_limit) {
    const auto image = build_program({tier_program(environment, "regions")});
    nvm::TierCache cache(image, eager_regions());
    compare_with_tier_0(image, cache, "regions");
    for (std::uint64_t steps = 1; steps <= 1500; ++steps) {
        compare_with_tier_0(image, cache, "regions with " + std::to_string(steps) + " steps", {},
                            [&](nvm::RunLimits& limits) { limits.max_steps = steps; });
    }
    for (const std::uint32_t frames : {1U, 2U, 3U, 4U}) {
        compare_with_tier_0(image, cache, "regions with " + std::to_string(frames) + " frames", {},
                            [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
    }
    if (regions_enabled()) {
        const auto statistics = cache.statistics();
        check(statistics.regions > 0 && statistics.steps.region > 0, "the loops ran in regions");
        check(statistics.region_counts.budget > 0, "step limits inside regions went to tier 1");
    }
}

NEURA_TEST(tier, the_task_quantum_ends_turns_at_the_same_polls) {
    // Concurrent programs under quanta from 1 (every poll ends a turn) up:
    // the same schedule, output, and counts at both tiers.
    for (const auto* name : {"fan-out", "worker-group", "select-rotation", "channel-order"}) {
        const auto path = environment.source_dir / "tests" / "concurrency" / "programs" / (std::string(name) + ".nra");
        const auto image = build_program({path});
        nvm::TierCache cache(image, eager_regions());
        for (const std::uint64_t quantum : {1ULL, 2ULL, 3ULL, 4ULL, 5ULL, 7ULL, 16ULL, 1024ULL}) {
            compare_with_tier_0(image, cache, std::string(name) + " with quantum " + std::to_string(quantum), {},
                                [&](nvm::RunLimits& limits) { limits.task_quantum = quantum; });
        }
    }
}

NEURA_TEST(tier, fleets_keep_their_rounds_at_tier_2) {
    // A fleet counts a round per block: at tier 2 its members reach every
    // event on the same round as at tier 1, so the members stay in step.
    const auto image = build_program({tier_program(environment, "regions")});
    const std::vector<std::vector<std::string>> arguments{{"3"}, {"5"}, {"2"}, {"7"}, {"4"}};
    const auto run = [&](std::uint8_t tier, nvm::TierCache* cache) {
        nvm::Fleet fleet(image, {limits_at(tier, cache), nullptr, 2});
        for (const auto& member : arguments) fleet.add(image.entry, member);
        CapturingConsole console;
        nvm::HostRegistry registry;
        registry.add(console);
        std::vector<std::pair<nvm::RunResult, std::uint64_t>> results;
        nvm::run_fleet(fleet, nvm::bind(image, registry), [&](std::size_t, const nvm::RunResult& result) {
            results.emplace_back(result, fleet.rounds());
        });
        return std::make_pair(results, console.output());
    };
    nvm::TierCache tier1(image, eager_regions());
    nvm::TierCache tier2(image, eager_regions());
    const auto [expected, expected_output] = run(1, &tier1);
    const auto [actual, actual_output] = run(2, &tier2);
    check_equal(actual.size(), expected.size(), "fleet members");
    for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
        check_same_run(actual[i].first, expected[i].first, "fleet member " + std::to_string(i));
        check_equal(actual[i].second, expected[i].second, "round of member " + std::to_string(i) + "'s end");
    }
    check_equal(actual_output, expected_output, "fleet output");
    if (regions_enabled()) check(tier2.statistics().steps.region > 0, "fleet members ran regions");

    // Parallel items share their run's regions.
    const auto parallel = build_program({environment.source_dir / "tests" / "concurrency" / "parallel" / "map.nra"});
    const auto reference = run_captured(parallel, limits_at(0));
    for (const std::uint32_t workers : {1U, 4U}) {
        nvm::TierCache shared(parallel, eager_regions());
        auto limits = limits_at(2, &shared);
        limits.parallel_workers = workers;
        check_same(run_captured(parallel, limits), reference, "parallel map with " + std::to_string(workers) +
                                                                   " workers");
    }
}

} // namespace neura::testing
