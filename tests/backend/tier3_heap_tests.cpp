// Tier 3's heap access (src/nvm/tier3.hpp, the heap operations; heap.hpp,
// "Published layout"): field, element, length, tag, version and global
// reads and writes inline, their checks as deoptimizations, a field offset
// that depends on the entry's own field count, narrow and widened element
// storage, and the passes over them. Every run is compared with tier 0,
// wherever a step, heap or handle limit lands. The `tier3` suite
// (backend-tier3); the `tier3_check` suite runs under NEURA_TIER_CHECK=1
// (backend-tier3-check).
#include "graph_test_support.hpp"

#include "jit/regalloc.hpp"
#include "jit/tier3_lower.hpp"
#include "jit/tier3_native.hpp"
#include "nvm/fleet.hpp"
#include "nvm/heap.hpp"
#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvm/tier3.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>

namespace neura::testing {
namespace {

namespace t3 = nvm::tier3;

// Every block lifted and every program formed at its first arrival, run as
// tier 3 IR on the IR interpreter from its first checkpoint.
// The tier-3 mode the heap tests run in: each runs once on the IR
// interpreter and once as machine code (on the interpreter where tier 3 has
// no code generator).
nvm::TierOptions::Tier3 heap_mode = nvm::TierOptions::Tier3::Interp;

template <typename Body>
void in_each_mode(Body&& body) {
    for (const auto mode : {nvm::TierOptions::Tier3::Interp, nvm::TierOptions::Tier3::Native}) {
        heap_mode = mode;
        body();
    }
    heap_mode = nvm::TierOptions::Tier3::Interp;
}

nvm::TierOptions heap_options() {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    options.programs = nvm::TierOptions::Programs::On;
    options.tier3 = heap_mode;
    options.native_after = 1;
    return options;
}

// Without --profile, so that the standard variant runs (the profile variant
// writes and allocates through the reference operations).
nvm::RunLimits heap_limits(std::uint8_t tier, nvm::TierCache* cache = nullptr) {
    nvm::RunLimits limits;
    limits.max_steps = 20'000'000;
    limits.tier = tier;
    limits.tier_cache = cache;
    return limits;
}

using Adjust = std::function<void(nvm::RunLimits&)>;

void check_same_capture(const Captured& actual, const Captured& expected, const std::string& label) {
    check_same_run(actual.result, expected.result, label);
    check_equal(actual.output, expected.output, label + " output");
    check_equal(actual.requests.size(), expected.requests.size(), label + " request count");
}

// Runs `image` at tier 0 and through `cache` under `adjust`; the tier-0 run.
Captured same_as_tier_0(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                        const std::vector<std::string>& arguments, const Adjust& adjust = {}) {
    auto reference_limits = heap_limits(0);
    if (adjust) adjust(reference_limits);
    auto reference = run_captured(image, reference_limits, std::nullopt, arguments);
    auto limits = heap_limits(2, &cache);
    if (adjust) adjust(limits);
    check_same_capture(run_captured(image, limits, std::nullopt, arguments), reference, label);
    return reference;
}

bool machine_code_runs() { return nvm::tier_ceiling() >= 2 && jit::native::supported(); }

nvr::Image heap_image(const Environment& environment) {
    return build_program({backend_dir(environment) / "tier3" / "heap.nra"});
}

// The function program of `function` in `cache` (formed now if no run has).
const nvm::tier2::Region* function_program(nvm::TierCache& cache, std::uint32_t function) {
    return cache.program(function, 0).region;
}

// The native programs a cache made, of every variant, with their IR.
std::vector<const t3::Function*> native_functions(const nvr::Image& image, nvm::TierCache& cache) {
    std::vector<const t3::Function*> list;
    for (std::uint32_t function = 0; function < image.functions.size(); ++function) {
        const auto* program = function_program(cache, function);
        if (!program) continue;
        for (const auto variant : {t3::Variant::Standard, t3::Variant::Profile, t3::Variant::Checkable}) {
            if (const auto* code = cache.native_code(*program, variant)) list.push_back(&code->ir);
        }
    }
    return list;
}

std::size_t count_ops(const t3::Function& ir, t3::Op op) {
    std::size_t n = 0;
    for (const auto& block : ir.blocks) {
        for (const auto id : block.insts) n += ir.insts[id].op == op ? 1 : 0;
    }
    return n;
}

const std::vector<std::vector<std::string>> heap_cases{
    {"objects", "30"}, {"append", "40"},   {"wide", "30"},  {"destroyed", "9"}, {"globals", "20"},
    {"classes", "24"}, {"cast", "11"},     {"names", "12"}, {"overrun", "12"},     {"around", "20"},
};

// --- Every case, and the IR it ran -------------------------------------------------------

void heap_operations_run_inline_and_every_run_matches_tier_0(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    for (const auto& arguments : heap_cases) same_as_tier_0(image, cache, "case " + arguments[0], arguments);
    // With --profile: the profile variant, its function steps the reference's.
    for (const auto& arguments : heap_cases) {
        same_as_tier_0(image, cache, "case " + arguments[0] + " with --profile", arguments,
                       [](nvm::RunLimits& limits) { limits.profile = true; });
    }
    const auto stats = cache.statistics();
    check(stats.native.entered > 0, "native programs were entered");
    std::map<t3::Op, std::size_t> ops;
    for (const auto* ir : native_functions(image, cache)) {
        for (const auto op : {t3::Op::Lookup, t3::Op::CheckKind, t3::Op::CheckInit, t3::Op::LoadField, t3::Op::StoreField,
                              t3::Op::LoadLength, t3::Op::LoadElement, t3::Op::StoreElement, t3::Op::LoadGlobal,
                              t3::Op::StoreGlobal, t3::Op::CheckGlobalInit, t3::Op::TestKind, t3::Op::Select,
                              t3::Op::CheckIndex}) {
            ops[op] += count_ops(*ir, op);
        }
        // No inline state operation is left to state() but text equality
        // (the profile and checkable variants write through it).
        for (const auto& in : ir->insts) {
            if (ir->variant != t3::Variant::Standard) break;
            if (in.op != t3::Op::StateCall || in.aux == t3::none) continue;
            check(ir->exits[in.aux]->operation == nvr::StateOp::TextEqual, "only text equality stays a state call");
        }
    }
    for (const auto& [op, n] : ops) check(n > 0, "the native programs hold heap op " + std::to_string(static_cast<int>(op)));
    check(stats.native.state_traps > 0, "a heap check deoptimized (destroyed, cast, overrun)");
    if (heap_mode == nvm::TierOptions::Tier3::Native && machine_code_runs()) {
        check_equal(stats.native_mode, std::string("native"), "machine code ran the heap cases");
        check(stats.native_rejections.empty(), "every native program got machine code: " + nvm::describe(stats));
    }
}

NEURA_TEST(tier3, heap_operations_run_inline_and_every_run_matches_tier_0) {
    in_each_mode([&] { heap_operations_run_inline_and_every_run_matches_tier_0(environment); });
}

// The traps of the heap checks are tier 1's, at every step limit around them.
void heap_runs_match_tier_0_at_every_step_limit(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"objects", "4"}, {"append", "4"}, {"wide", "4"}, {"destroyed", "4"}, {"classes", "5"}, {"cast", "4"}}) {
        const auto reference = run_captured(image, heap_limits(0), std::nullopt, arguments);
        const auto steps = reference.result.steps;
        for (std::uint64_t limit = 1; limit <= steps + 1; ++limit) {
            same_as_tier_0(image, cache, arguments[0] + " at step limit " + std::to_string(limit), arguments,
                           [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
    }
}

NEURA_TEST(tier3, heap_runs_match_tier_0_at_every_step_limit) {
    in_each_mode([&] { heap_runs_match_tier_0_at_every_step_limit(environment); });
}

// --- Limits of the heap -------------------------------------------------------------------

// Every heap-cell limit from the one the first allocation needs to the
// peak + 2: collections at every allocation, HeapLimit in place in the
// helpers, the program interpreter and native code taking turns.
void heap_runs_match_tier_0_at_every_heap_cell_limit(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    for (const auto& arguments : std::vector<std::vector<std::string>>{{"objects", "12"}, {"append", "16"}, {"wide", "6"}}) {
        const auto reference = run_captured(image, heap_limits(0), std::nullopt, arguments);
        const auto peak = reference.result.peak_heap_cells;
        check(peak > 0, arguments[0] + " allocates");
        for (std::uint64_t limit = 1; limit <= peak + 2; ++limit) {
            same_as_tier_0(image, cache, arguments[0] + " at heap-cell limit " + std::to_string(limit), arguments,
                           [&](nvm::RunLimits& limits) { limits.max_heap_cells = limit; });
        }
    }
}

NEURA_TEST(tier3, heap_runs_match_tier_0_at_every_heap_cell_limit) {
    in_each_mode([&] { heap_runs_match_tier_0_at_every_heap_cell_limit(environment); });
}

// Every handle limit up to two past the last handle a run assigns.
void heap_runs_match_tier_0_at_every_handle_limit(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    for (const auto& arguments : std::vector<std::vector<std::string>>{{"objects", "10"}, {"append", "10"}}) {
        std::uint64_t limit = 0;
        bool trapped = true;
        // Up from no handle at all until a run no longer hits the limit, and
        // two more.
        for (std::uint64_t past = 0; past < 3 && limit < 10'000; ++limit) {
            const auto reference = same_as_tier_0(image, cache, arguments[0] + " at handle limit " + std::to_string(limit),
                                                  arguments, [&](nvm::RunLimits& limits) { limits.max_handles = limit; });
            trapped = !reference.result.completed && reference.result.trap.kind == nvm::TrapKind::HandleLimit;
            if (!trapped) ++past;
        }
        check(!trapped && limit > 3, arguments[0] + " reached a handle limit it does not hit");
    }
}

NEURA_TEST(tier3, heap_runs_match_tier_0_at_every_handle_limit) {
    in_each_mode([&] { heap_runs_match_tier_0_at_every_handle_limit(environment); });
}

// --- Field offsets (an entry's field count decides them) --------------------------------

// A field read and written through a Base reference whose entries are a
// Base, a Small (both one initialized word) and a Wide (70 fields: two
// words): the offset comes from the entry's count, not a constant.
void a_base_field_through_a_reference_to_an_entry_of_more_than_64_fields_uses_its_count(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    same_as_tier_0(image, cache, "wide", {"wide", "30"});
    std::size_t counted = 0;
    std::size_t constant = 0;
    for (const auto* ir : native_functions(image, cache)) {
        for (const auto& block : ir->blocks) {
            for (const auto id : block.insts) {
                const auto& in = ir->insts[id];
                if (in.op != t3::Op::LoadField && in.op != t3::Op::StoreField) continue;
                (in.aux == t3::none ? counted : constant) += 1;
            }
        }
        for (const auto& test : ir->type_tests) {
            if (test.kind != static_cast<std::uint8_t>(nvm::EntryKind::Object) || test.types.size() < 3) continue;
            check(test.words == t3::none, "a set of layouts with one and two initialized words has no common count");
        }
    }
    check(counted >= 3, "the reads and the write through Base take the offset from the count");
    check(constant > 0, "fields of a class without subclasses of another word count use a constant offset");
}

NEURA_TEST(tier3, a_base_field_through_a_reference_to_an_entry_of_more_than_64_fields_uses_its_count) {
    in_each_mode([&] { a_base_field_through_a_reference_to_an_entry_of_more_than_64_fields_uses_its_count(environment); });
}

// --- Widened storage ------------------------------------------------------------------------

// A hand-built image: function 1 makes a uint8 list (or array) of three
// elements, stores a uint16 value 300 at index 1 (wider than the declared
// width: the store widens the storage to eight bytes), reads elements 1 and
// 0 back inline from the widened storage, and returns element 1. Function 0
// calls it nine times (the later calls run its native program) and returns
// the last result.
nvr::Image widening_image(bool array) {
    using Kind = nvr::TypeKind;
    using nvr::ValueKind;
    nvr::Image image;
    // 0 void, 1 int32, 2 uint8, 3 uint8[list], 4 uint16, 5 text, 6 text[], 7 uint8[], 8 uint32
    image.types = {{Kind::Void},        {Kind::Signed, 32}, {Kind::Unsigned, 8}, {Kind::List, 0, 2},
                   {Kind::Unsigned, 16}, {Kind::Text},       {Kind::Array, 0, 5}, {Kind::Array, 0, 2},
                   {Kind::Unsigned, 32}};
    image.constants = {nvr::Constant{2, {1}},          nvr::Constant{2, {2}},          nvr::Constant{2, {3}},
                       nvr::Constant{8, {1, 0, 0, 0}}, nvr::Constant{4, {0x2c, 0x01}}, nvr::Constant{8, {0, 0, 0, 0}}};
    image.graphs = {nvr::Graph{}};
    const std::uint32_t collection = array ? 7 : 3;
    const auto block_of = [](nvr::Exit exit) {
        nvr::Block block;
        block.exits.push_back(std::move(exit));
        return block;
    };
    const auto state = [](nvr::StateOp operation, std::vector<nvr::Value> arguments, std::uint32_t result,
                          std::uint32_t resume) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::State;
        exit.operation = operation;
        exit.arguments = std::move(arguments);
        exit.result_slot = result;
        exit.resume = resume;
        return exit;
    };
    nvr::Function work;
    work.parameter_count = 0;
    work.result_type = 1;
    work.frame_types = {collection, 1, 1};
    auto allocate = state(nvr::StateOp::AllocateCollection,
                          {{ValueKind::Constant, 2, 0}, {ValueKind::Constant, 2, 1}, {ValueKind::Constant, 2, 2}}, 0, 1);
    allocate.immediate = collection;
    const nvr::Value list{ValueKind::Slot, collection, 0};
    nvr::Exit done;
    done.kind = nvr::ExitKind::Return;
    done.arguments = {{ValueKind::Slot, 1, 1}};
    work.blocks = {block_of(allocate),
                   block_of(state(nvr::StateOp::WriteElement, {list, {ValueKind::Constant, 8, 3}, {ValueKind::Constant, 4, 4}},
                                  nvr::absent, 2)),
                   block_of(state(nvr::StateOp::ReadElement, {list, {ValueKind::Constant, 8, 3}}, 1, 3)),
                   block_of(state(nvr::StateOp::ReadElement, {list, {ValueKind::Constant, 8, 5}}, 2, 4)), block_of(done)};
    const auto call = [](std::uint32_t result, std::uint32_t resume) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::Call;
        exit.target = 1;
        exit.result_slot = result;
        exit.resume = resume;
        return exit;
    };
    nvr::Function main;
    main.parameter_count = 1;
    main.result_type = 1;
    main.frame_types = {6, 1, 1};
    nvr::Exit leave;
    leave.kind = nvr::ExitKind::Return;
    leave.arguments = {{ValueKind::Slot, 1, 2}};
    // Nine calls: the last ones run function 1's native program whatever
    // checkpoint arrival makes it native (NEURA_TIER3_AFTER).
    for (std::uint32_t k = 0; k < 9; ++k) main.blocks.push_back(block_of(call(k == 8 ? 2 : 1, k + 1)));
    main.blocks.push_back(block_of(leave));
    image.functions = {std::move(main), std::move(work)};
    image.entry = 0;
    return image;
}

void a_value_wider_than_a_narrow_collection_widens_it_and_the_widened_storage_is_read_inline(const Environment&) {
    for (const bool array : {false, true}) {
        const auto image = widening_image(array);
        const auto label = std::string(array ? "array" : "list");
        nvm::TierCache cache(image, heap_options());
        const auto reference = same_as_tier_0(image, cache, label, {});
        check_equal(reference.result.status, 300, label + ": the widened element reads back at tier 0");
        const auto functions = native_functions(image, cache);
        check(!functions.empty(), label + ": native programs were made");
        std::size_t examined = 0;
        for (const auto* ir : functions) {
            if (ir->function != 1) continue;
            ++examined;
            check(count_ops(*ir, t3::Op::Widen) == 1, label + ": the store of a wide value goes through the heap");
            check(count_ops(*ir, t3::Op::LoadElement) == 2, label + ": both reads are inline");
        }
        check_equal(examined, std::size_t{1}, label + ": function 1 has a native program");
        check(cache.statistics().native.entered > 0, label + ": native code ran");
    }
}

NEURA_TEST(tier3, a_value_wider_than_a_narrow_collection_widens_it_and_the_widened_storage_is_read_inline) {
    in_each_mode([&] { a_value_wider_than_a_narrow_collection_widens_it_and_the_widened_storage_is_read_inline(environment); });
}

// --- Fast paths: the heap the reference leaves, word for word (A-3) -----------------------

// The heap a run of `image` leaves: Heap::snapshot (every handle, page, entry
// and its words, every free list and bump range, as places among the
// chunks) and the live and peak cells and the collection threshold; and how
// the run ended.
std::pair<std::vector<std::uint64_t>, nvm::Event::Kind> heap_after(const nvr::Image& image, nvm::RunLimits limits) {
    nvm::Execution execution(image, std::vector<std::string>{}, limits);
    const auto event = execution.advance();
    check(event.kind != nvm::Event::Kind::HostRequest, "the allocation run makes no host request");
    return {execution.heap_snapshot(), event.kind};
}

// tests/backend/tier3/alloc.nra allocates objects of three sizes and appends
// to lists of two widths in a loop, with collections and destroyed objects
// between, so the fast paths take blocks from free lists and from bump
// ranges and the slow paths collect, take new pages and chunks, and grow
// the lists. The heap each tier-3 mode leaves is the one tier 0 leaves.
NEURA_TEST(tier3, inline_allocations_and_appends_leave_the_heap_the_reference_leaves_word_for_word) {
    const auto image = build_program({backend_dir(environment) / "tier3" / "alloc.nra"});
    for (const std::uint64_t cells : {std::uint64_t{1} << 26, std::uint64_t{700}, std::uint64_t{3000}}) {
        auto reference_limits = heap_limits(0);
        reference_limits.max_heap_cells = cells;
        const auto reference = heap_after(image, reference_limits);
        std::array<std::uint64_t, 4> interpreted{};
        in_each_mode([&] {
            const auto label = std::string(heap_mode == nvm::TierOptions::Tier3::Native ? "machine code" : "IR interpreter") +
                               " with " + std::to_string(cells) + " cells";
            nvm::TierCache cache(image, heap_options());
            auto limits = heap_limits(2, &cache);
            limits.max_heap_cells = cells;
            const auto actual = heap_after(image, limits);
            check(actual.second == reference.second, label + ": the run ends as tier 0's");
            check_equal(actual.first.size(), reference.first.size(), label + ": heap words");
            const auto differ = std::mismatch(actual.first.begin(), actual.first.end(), reference.first.begin(), reference.first.end());
            check(differ.first == actual.first.end(),
                  label + ": the heaps differ at word " + std::to_string(differ.first - actual.first.begin()));
            const auto stats = cache.statistics();
            // Without the size-class pool (AddressSanitizer builds) every
            // allocation and append is the helper's.
            if (nvm::Heap::native_allocation()) {
                check(stats.native.fast_allocations > 0 && stats.native.fast_appends > 0, label + ": the fast paths ran");
            } else {
                check(stats.native.fast_allocations == 0 && stats.native.fast_appends == 0, label + ": no fast path ran");
            }
            // Machine code takes the fast path exactly where the IR
            // interpreter does.
            const std::array counts{stats.native.fast_allocations, stats.native.fast_appends, stats.native.slow_allocations,
                                    stats.native.slow_appends};
            if (heap_mode == nvm::TierOptions::Tier3::Interp) interpreted = counts;
            else check(counts == interpreted, label + ": the fast and slow paths ran as often as on the IR interpreter");
            check(stats.native.slow_allocations > 0 && stats.native.slow_appends > 0, label + ": the slow paths ran");
            if (heap_mode == nvm::TierOptions::Tier3::Native && machine_code_runs()) {
                check_equal(stats.native_mode, std::string("native"), label + ": machine code ran");
            }
        });
    }
}

// Two heaps, one allocated by the heap's own operations and the executor's
// count (what state() does), the other by the fast paths on its published
// records, the same sequence each: the same words after every step, and at
// the collection threshold the fast path is taken exactly while the cells
// fit (live + cells <= next) and changes nothing when they do not.
struct Published {
    nvm::Heap heap;
    std::uint64_t live = 0, next = 0, peak = 0;
    t3::Context context;
    explicit Published(std::uint64_t threshold) : next(threshold) {
        context.handle_table = heap.handle_table();
        context.allocator = heap.native_allocator();
        context.live_cells = &live;
        context.next_collection = &next;
        context.peak_cells = &peak;
        context.max_handles = std::uint64_t{1} << 32;
        context.fast_allocation = nvm::Heap::native_allocation() ? 1 : 0;
    }
    std::vector<std::uint64_t> words() const {
        auto all = heap.snapshot();
        all.insert(all.end(), {live, next, peak});
        return all;
    }
    // The reference: create_object and the executor's count.
    std::uint64_t reference_object(std::uint32_t layout, std::uint64_t fields) {
        const auto h = heap.create_object(layout, static_cast<std::size_t>(fields));
        live += 1 + fields;
        peak = std::max(peak, live);
        return h;
    }
    void reference_append(std::uint64_t handle, std::uint64_t bits) {
        heap.append(heap.find(handle), bits);
        live += 1;
        peak = std::max(peak, live);
    }
    std::uint64_t entry(std::uint64_t handle) const {
        const auto* table = static_cast<const nvm::Heap::HandleTable*>(context.handle_table);
        const auto page = reinterpret_cast<std::uintptr_t>(table->pages[handle >> nvm::Heap::handle_page_bits()]);
        std::uint64_t address = 0;
        std::memcpy(&address, reinterpret_cast<const void*>(page + nvm::Heap::page_entries_offset() +
                                                            8 * (handle & nvm::Heap::handle_page_mask())), 8);
        return address;
    }
};

NEURA_TEST(tier3, the_fast_paths_change_a_heap_word_for_word_as_its_own_operations_do) {
    if (!nvm::Heap::native_allocation()) return;
    Published reference(1'000'000), fast(1'000'000);
    // The first object of each heap through the reference: its page and
    // chunk exist after it.
    reference.reference_object(0, 3);
    fast.reference_object(0, 3);
    const auto list = [](Published& p) {
        return p.heap.create(nvm::Heap::Builder::collection(nvm::EntryKind::List, 7, nvm::ElementWidth::W1));
    };
    const auto ref_list = list(reference);
    const auto fast_list = list(fast);
    check_equal(ref_list, fast_list, "the lists have one handle");
    std::uint64_t made = 0;
    for (std::uint64_t i = 0; i < 700; ++i) {
        const std::uint64_t fields = i % 5 == 0 ? 70 : i % 3;
        const auto layout = static_cast<std::uint32_t>(i % 4);
        std::uint64_t h = 0;
        if (t3::allocate_inline(fast.context, layout, fields, h)) {
            ++made;
        } else {
            h = fast.reference_object(layout, fields);
        }
        check_equal(reference.reference_object(layout, fields), h, "allocation " + std::to_string(i) + " gets the same handle");
        if (i % 9 == 4) {
            // Destroyed: its block goes on the free list the next one takes.
            reference.live -= reference.heap.cells(reference.heap.find(h));
            reference.heap.free(h);
            fast.live -= fast.heap.cells(fast.heap.find(h));
            fast.heap.free(h);
        }
        const auto bits = (i * 37) % 251;
        if (!t3::append_inline(fast.context, fast.entry(fast_list), bits)) fast.reference_append(fast_list, bits);
        reference.reference_append(ref_list, bits);
        check(fast.words() == reference.words(), "the heaps are the same after step " + std::to_string(i));
        check(fast.heap.check().empty(), "the fast heap is consistent after step " + std::to_string(i) + ": " + fast.heap.check());
    }
    check(made > 500, "most allocations took the fast path");
    // At the threshold: 4 cells fit exactly when live + 4 == next.
    fast.next = fast.live + 4;
    reference.next = fast.next;
    std::uint64_t h = 0;
    check(t3::allocate_inline(fast.context, 1, 3, h), "the cells fit when live + cells == next");
    reference.reference_object(1, 3);
    check(fast.words() == reference.words(), "the heaps are the same after the allocation at the threshold");
    const auto before = fast.words();
    check(!t3::allocate_inline(fast.context, 1, 3, h), "they do not one cell past it");
    check(!t3::append_inline(fast.context, fast.entry(fast_list), 1), "nor does an append at the threshold");
    check(fast.words() == before, "a fast path not taken changes nothing");
    fast.next = fast.live + 1;
    reference.next = fast.next;
    if (!t3::append_inline(fast.context, fast.entry(fast_list), 9)) {
        // The list was full: it grows in the reference.
        fast.reference_append(fast_list, 9);
    }
    reference.reference_append(ref_list, 9);
    check(fast.words() == reference.words(), "the heaps are the same after an append at the threshold");
    // A value wider than the storage is the reference's: it widens.
    check(!t3::append_inline(fast.context, fast.entry(fast_list), 300), "a wide value takes the slow path");
}

// --- Allocations and appends inside inlined code (design 3.6) ----------------------------

// `inlined` calls push (an append) and fresh (an allocation) in a loop; both
// are inlined, so their slow paths run inside inlined code: they materialize
// the callee's frame, run the helper, and leave at its resume block. Without
// --profile (whose variant inlines nothing), at every heap-cell limit (a
// collection or HeapLimit in the helper, the trap's callers those of tier 0)
// and at every step limit.
NEURA_TEST(tier3, slow_paths_inside_inlined_code_materialize_the_frames_and_match_tier_0) {
    // NEURA_TIER_EAGER=1 turns programs native at their first checkpoint,
    // before the callees' programs exist: nothing is inlined then.
    if (nvm::tier_eager()) return;
    const auto image = heap_image(environment);
    const std::vector<std::string> arguments{"inlined", "40"};
    in_each_mode([&] {
        const auto label = std::string(heap_mode == nvm::TierOptions::Tier3::Native ? "machine code" : "IR interpreter");
        // Native at the seventh checkpoint arrival: the callees' programs
        // exist by then, so the caller's inlines them.
        auto options = heap_options();
        options.native_after = 7;
        nvm::TierCache cache(image, options);
        const auto unprofiled = [](nvm::RunLimits& limits) { limits.profile = false; };
        const auto reference = same_as_tier_0(image, cache, label + " inlined", arguments, unprofiled);
        for (std::uint64_t cells = 1; cells <= reference.result.peak_heap_cells + 2; ++cells) {
            same_as_tier_0(image, cache, label + " inlined at heap-cell limit " + std::to_string(cells), arguments,
                           [&](nvm::RunLimits& limits) {
                               limits.profile = false;
                               limits.max_heap_cells = cells;
                           });
        }
        for (std::uint64_t steps = 1; steps <= reference.result.steps + 1; steps += 7) {
            same_as_tier_0(image, cache, label + " inlined at step limit " + std::to_string(steps), arguments,
                           [&](nvm::RunLimits& limits) {
                               limits.profile = false;
                               limits.max_steps = steps;
                           });
        }
        const auto stats = cache.statistics();
        check(stats.native_inlined > 0, label + ": calls were inlined");
        check(stats.native.state_traps == 0 && stats.native.references == 0,
              label + ": no heap check of the inlined code fails (each tests what its callee tested)");
        check(stats.native.slow_allocations > 0 && stats.native.slow_appends > 0, label + ": slow paths ran");
        // An allocation and an append whose state has an inlined level.
        bool inside = false;
        for (const auto* ir : native_functions(image, cache)) {
            for (const auto& block : ir->blocks) {
                for (const auto id : block.insts) {
                    const auto& in = ir->insts[id];
                    if ((in.op == t3::Op::AllocObject || in.op == t3::Op::Append) && ir->states[in.state].caller != t3::none) {
                        inside = true;
                    }
                }
            }
        }
        check(inside, label + ": a fast path sits inside inlined code");
    });
}

// The lowering's spill homes (design 8.3) read the physical level of a
// state, which a helper's materialization writes as a write-back would: a
// value homed in a slot of the physical frame and live across the slow path
// of an append or allocation inside inlined code is one that level names.
NEURA_TEST(tier3, a_value_homed_in_the_physical_frame_is_named_by_an_inlined_slow_path) {
    // NEURA_TIER_EAGER=1 turns programs native at their first checkpoint,
    // before the callees' programs exist: nothing is inlined then.
    if (nvm::tier_eager()) return;
    if (!jit::native::supported()) return;
    const auto image = heap_image(environment);
    auto options = heap_options();
    options.native_after = 7;
    nvm::TierCache cache(image, options);
    auto limits = heap_limits(2, &cache);
    limits.profile = false;
    run_captured(image, limits, std::nullopt, {"inlined", "40"});
    std::size_t examined = 0;
    std::size_t homed = 0;
    for (const auto* ir : native_functions(image, cache)) {
        for (const auto& unit : jit::native::entry_units(*ir)) {
            const auto lowered = jit::native::lower(*ir, unit);
            for (const auto& block : lowered.function.blocks) {
                for (const auto& inst : block.insts) {
                    const auto& step = lowered.steps[inst.opcode];
                    if (!step.inlined) continue;
                    ++examined;
                    const auto physical = [&] {
                        auto s = step.state;
                        while (ir->states[s].caller != t3::none) s = ir->states[s].caller;
                        return s;
                    }();
                    // The SSA values each state use (one virtual register) stands for.
                    std::map<std::uint32_t, std::vector<std::uint32_t>> values_of;
                    for (const auto& [use, value] : step.state_values) values_of[use].push_back(value);
                    for (const auto& [use, values] : values_of) {
                        const auto vreg = inst.state_uses.at(use).vreg.id;
                        const auto home = lowered.function.spill_home.at(vreg);
                        if (home < 0) continue;
                        // The physical level names this register's value for its home.
                        const auto& slot = ir->states[physical].slots.at(static_cast<std::size_t>(home));
                        bool named = slot.kind == t3::StateSlot::Kind::Value &&
                                     std::find(values.begin(), values.end(), static_cast<std::uint32_t>(slot.value)) != values.end();
                        for (const auto value : values) {
                            const auto& source = ir->insts.at(value);
                            named = named || (slot.kind == t3::StateSlot::Kind::Unchanged && source.op == t3::Op::EntryLoad &&
                                              source.imm == static_cast<std::uint64_t>(home));
                        }
                        check(named, "a homed value at an inlined slow path is the physical frame's");
                        ++homed;
                    }
                }
            }
        }
    }
    check(examined > 0, "slow paths inside inlined code were lowered");
    check(homed > 0, "a value homed in a physical slot is live across one");
}

// The slow path of an append or allocation outside inlined code writes the
// frame back from its state and goes on in machine code, so a value in a
// frame slot there (its spill home) must be the value the state gives that
// slot: otherwise the write-back overwrites it, and the code after reads
// the state's value instead. tests/backend/tier3/spill.nra keeps a copy of
// a reassigned variable in memory across an append. Its runs match tier 0
// at every heap-cell limit, and in the code of every unit each frame slot
// an append or allocation reads is one its write-back leaves as it is.
NEURA_TEST(tier3, a_value_spilled_to_a_frame_slot_survives_the_write_back_of_a_slow_path) {
    const auto image = build_program({backend_dir(environment) / "tier3" / "spill.nra"});
    const std::vector<std::string> arguments{"30"};
    in_each_mode([&] {
        const auto label = std::string(heap_mode == nvm::TierOptions::Tier3::Native ? "machine code" : "IR interpreter");
        nvm::TierCache cache(image, heap_options());
        const auto unprofiled = [](nvm::RunLimits& limits) { limits.profile = false; };
        const auto reference = same_as_tier_0(image, cache, label + " spill", arguments, unprofiled);
        for (std::uint64_t cells = 1; cells <= reference.result.peak_heap_cells + 2; ++cells) {
            same_as_tier_0(image, cache, label + " spill at heap-cell limit " + std::to_string(cells), arguments,
                           [&](nvm::RunLimits& limits) {
                               limits.profile = false;
                               limits.max_heap_cells = cells;
                           });
        }
        const auto stats = cache.statistics();
        check(stats.native.entered > 0, label + ": native programs were entered");
        check(stats.native.slow_appends > 0, label + ": slow paths of appends ran");
        if (heap_mode != nvm::TierOptions::Tier3::Native || !machine_code_runs()) return;
        std::size_t in_frame = 0;
        for (const auto* ir : native_functions(image, cache)) {
            for (const auto& unit : jit::native::entry_units(*ir)) {
                const auto lowered = jit::native::lower(*ir, unit);
                const auto allocation = jit::mir::allocate(lowered.function);
                for (std::size_t b = 0; b < lowered.function.blocks.size(); ++b) {
                    const auto& block = lowered.function.blocks[b];
                    for (std::size_t j = 0; j < block.insts.size(); ++j) {
                        const auto& step = lowered.steps[block.insts[j].opcode];
                        if ((step.kind != jit::native::Kind::Append && step.kind != jit::native::Kind::AllocObject) ||
                            step.inlined) {
                            continue;
                        }
                        const auto& uses = allocation.blocks[b].insts[j].uses;
                        // The location the write-back reads each slot's value from.
                        std::map<std::uint32_t, jit::mir::Location> written;
                        auto u = (step.kind == jit::native::Kind::Append ? 2U : 0U) + step.arguments;
                        const auto& state = ir->states[step.state];
                        for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
                            const auto& slot = state.slots[r];
                            if (slot.kind == t3::StateSlot::Kind::Const) {
                                written[r] = {};
                                continue;
                            }
                            if (slot.kind != t3::StateSlot::Kind::Value) continue;
                            const bool known = std::any_of(step.state_constants.begin(), step.state_constants.end(),
                                                           [&](const auto& c) { return c.first == slot.value; });
                            written[r] = known ? jit::mir::Location{} : uses.at(u++);
                        }
                        for (const auto& location : uses) {
                            if (location.kind != jit::mir::LocKind::FrameSlot) continue;
                            ++in_frame;
                            const auto it = written.find(location.index);
                            check(it == written.end() || it->second == location,
                                  label + ": a value in frame slot " + std::to_string(location.index) +
                                      " at a slow path is the one its write-back writes there");
                        }
                    }
                }
            }
        }
        check(in_frame > 0, label + ": an append or allocation reads a value from a frame slot");
    });
}

// --- The checkable variant ----------------------------------------------------------------

NEURA_TEST(tier3, the_checkable_variant_writes_through_state_calls) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    same_as_tier_0(image, cache, "objects", {"objects", "6"});
    std::size_t built = 0;
    for (std::uint32_t function = 0; function < image.functions.size(); ++function) {
        const auto* program = function_program(cache, function);
        if (!program) continue;
        const auto standard = t3::build(image, *program, true, t3::Variant::Standard);
        const auto checkable = t3::build(image, *program, true, t3::Variant::Checkable);
        if (!standard.program || !checkable.program) continue;
        ++built;
        const auto& s = standard.program->ir;
        const auto& c = checkable.program->ir;
        check_equal(count_ops(c, t3::Op::StoreField) + count_ops(c, t3::Op::StoreElement) + count_ops(c, t3::Op::StoreGlobal) +
                        count_ops(c, t3::Op::Widen),
                    std::size_t{0}, "the checkable variant writes nothing inline");
        check_equal(count_ops(c, t3::Op::LoadField), count_ops(s, t3::Op::LoadField), "its reads are inline");
    }
    check(built > 0, "programs were built in both variants");
}

// --- Fleets ---------------------------------------------------------------------------------

// Members of a fleet share the cell count (no inline allocation then); each
// member's run is the serial tier-0 run.
void a_fleet_of_heap_runs_matches_serial_tier_0_runs(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    const std::vector<std::vector<std::string>> members{{"objects", "20"}, {"append", "25"}, {"wide", "9"}, {"destroyed", "6"}};
    std::vector<Captured> reference;
    for (const auto& arguments : members) reference.push_back(run_captured(image, heap_limits(0), std::nullopt, arguments));
    nvm::Fleet fleet(image, {heap_limits(2, &cache), nullptr, 3});
    for (const auto& arguments : members) fleet.add(image.entry, arguments);
    CapturingConsole console;
    nvm::HostRegistry registry;
    registry.add(console);
    const auto bindings = nvm::bind(image, registry);
    std::size_t finished = 0;
    std::string expected_output;
    nvm::run_fleet(fleet, bindings, [&](std::size_t member, const nvm::RunResult& actual) {
        check_same_run(actual, reference[member].result, "fleet member " + std::to_string(member));
        expected_output += reference[member].output;
        ++finished;
    });
    check_equal(finished, members.size(), "every member finished");
    check_equal(console.output(), expected_output, "fleet output");
    check(cache.statistics().native.entered > 0, "fleet members ran native programs");
}

NEURA_TEST(tier3, a_fleet_of_heap_runs_matches_serial_tier_0_runs) {
    in_each_mode([&] { a_fleet_of_heap_runs_matches_serial_tier_0_runs(environment); });
}

// --- Passes over heap operations ------------------------------------------------------------

// poke(item, by): item.b = item.b + by; return item.a + item.b. The read of
// b after the write is a read of its own: the write may alias it.
void value_numbering_keeps_a_field_read_after_a_write_of_the_field(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    same_as_tier_0(image, cache, "wide", {"wide", "30"});
    // poke's (or a program that inlines it) reads of b, one before its write
    // and one after: both stay.
    bool found = false;
    for (const auto* ir : native_functions(image, cache)) {
        std::map<std::uint64_t, std::pair<std::size_t, std::size_t>> by_field;  // stores, reads
        for (const auto& block : ir->blocks) {
            for (const auto id : block.insts) {
                const auto& in = ir->insts[id];
                if (in.op == t3::Op::StoreField) ++by_field[in.imm].first;
                if (in.op == t3::Op::LoadField) ++by_field[in.imm].second;
            }
        }
        for (const auto& [field, counts] : by_field) {
            if (counts.first >= 1 && counts.second >= 2) found = true;
        }
    }
    check(found, "a field read before and after a write of it stays two reads");
}

NEURA_TEST(tier3, value_numbering_keeps_a_field_read_after_a_write_of_the_field) {
    in_each_mode([&] { value_numbering_keeps_a_field_read_after_a_write_of_the_field(environment); });
}

// The lookup, kind and initialized checks of a field read in a loop over an
// invariant entry leave the loop; a bounds check under `i < list.length`
// goes; the statistics count them.
void heap_checks_are_numbered_removed_and_hoisted(const Environment& environment) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    for (const auto& arguments : heap_cases) same_as_tier_0(image, cache, "case " + arguments[0], arguments);
    std::map<std::string, std::uint64_t> totals;
    for (const auto* ir : native_functions(image, cache)) {
        for (const auto& [name, count] : ir->pass_counts) totals[name] += count;
    }
    for (const auto* name : {"gvn.lookup", "gvn.kind", "gvn.nil", "gvn.init", "elim.bounds", "licm.lookup", "licm.kind", "licm.read"}) {
        check(totals[name] > 0, std::string("the passes did ") + name);
    }
    const auto stats = cache.statistics();
    std::uint64_t removed = 0;
    for (const auto& [kind, count] : stats.native_heap_checks) removed += count;
    check(removed > 0, "the statistics count the heap checks the passes removed");
    const auto text = nvm::describe(stats);
    check(text.find("tier: native heap:") != std::string::npos, "the statistics have a heap line: " + text);
}

NEURA_TEST(tier3, heap_checks_are_numbered_removed_and_hoisted) {
    in_each_mode([&] { heap_checks_are_numbered_removed_and_hoisted(environment); });
}

// --- The verifier on heap operations --------------------------------------------------------

NEURA_TEST(tier3, the_verifier_rejects_pointers_where_they_must_not_be) {
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    same_as_tier_0(image, cache, "objects", {"objects", "6"});
    bool ptr_tested = false;
    for (const auto* ir : native_functions(image, cache)) {
        const auto lookup = std::find_if(ir->insts.begin(), ir->insts.end(), [](const t3::Inst& in) { return in.op == t3::Op::Lookup; });
        const auto read = std::find_if(ir->insts.begin(), ir->insts.end(), [](const t3::Inst& in) {
            return in.op == t3::Op::LoadField || in.op == t3::Op::LoadLength;
        });
        if (lookup == ir->insts.end() || read == ir->insts.end()) continue;
        // V2: a read of an entry given an integer.
        {
            auto broken = *ir;
            auto& in = broken.insts[static_cast<std::size_t>(read - ir->insts.begin())];
            in.operands[0] = 0;
            const auto violations = t3::verify(image, broken);
            check(std::any_of(violations.begin(), violations.end(), [](const std::string& v) { return v.starts_with("V2"); }),
                  "V2 rejects an integer where an entry pointer is expected");
        }
        // V6: an entry pointer in a frame state.
        {
            auto broken = *ir;
            const auto id = static_cast<std::uint32_t>(lookup - ir->insts.begin());
            for (auto& in : broken.insts) {
                if (in.state == t3::none || &in == &broken.insts[id]) continue;
                auto& state = broken.states[in.state];
                if (!state.slots.empty()) {
                    state.slots[0] = {t3::StateSlot::Kind::Value, id};
                    break;
                }
            }
            const auto violations = t3::verify(image, broken);
            check(std::any_of(violations.begin(), violations.end(), [](const std::string& v) { return v.starts_with("V6"); }),
                  "V6 rejects an entry pointer in a frame state");
        }
        ptr_tested = true;
        break;
    }
    check(ptr_tested, "an IR with lookups and reads was examined");
}

// --- NEURA_TIER_CHECK=1 ---------------------------------------------------------------------

// Under NEURA_TIER_CHECK=1 (backend-tier3-check sets it, with
// NEURA_TIER3=interp) the region check runs native programs of the
// checkable variant: their writes are logged by state() and undone, tier 1
// runs the same blocks, and every heap write and frame must agree.
void native_programs_run_under_the_region_check(const Environment& environment) {
    const char* check_env = std::getenv("NEURA_TIER_CHECK");
    check(check_env != nullptr && std::string(check_env) == "1", "the suite runs with NEURA_TIER_CHECK=1");
    const auto image = heap_image(environment);
    nvm::TierCache cache(image, heap_options());
    for (const auto& arguments : heap_cases) same_as_tier_0(image, cache, "case " + arguments[0], arguments);
    check(cache.statistics().native.entered > 0, "native programs ran under the region check");
}

NEURA_TEST(tier3_check, native_programs_run_under_the_region_check) {
    in_each_mode([&] { native_programs_run_under_the_region_check(environment); });
}

} // namespace
} // namespace neura::testing
