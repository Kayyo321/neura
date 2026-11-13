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
#include <set>

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
    options.programs = nvm::TierOptions::Programs::On;
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

// The cases of tests/backend/tier/program-helpers.nra that allocate.
struct HelperCase {
    const char* kind;
    const char* size;
};
const std::vector<HelperCase> helper_cases{{"alloc", "6"}, {"text", "3"}, {"utf8", "4"}};

// An image whose function 1 builds text from the bytes {0xFF} (ConstructText
// of invalid UTF-8, which the reference traps on after the block's step) and
// whose entry function calls it.
nvr::Image invalid_text_image() {
    using Kind = nvr::TypeKind;
    using nvr::ValueKind;
    const auto one_exit = [](nvr::Exit exit) {
        nvr::Block block;
        block.exits.push_back(std::move(exit));
        return block;
    };
    const auto state_block = [&](nvr::StateOp operation, std::uint32_t resume, std::uint32_t result,
                                 std::vector<nvr::Value> arguments, std::uint32_t immediate) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::State;
        exit.operation = operation;
        exit.resume = resume;
        exit.result_slot = result;
        exit.arguments = std::move(arguments);
        exit.immediate = immediate;
        return one_exit(std::move(exit));
    };
    nvr::Image image;
    // 0 void, 1 int32, 2 text, 3 text[], 4 uint8, 5 uint8[list]
    image.types = {{Kind::Void}, {Kind::Signed, 32}, {Kind::Text}, {Kind::Array, 0, 2}, {Kind::Unsigned, 8},
                   {Kind::List, 0, 4}};
    image.constants = {{1, {0, 0, 0, 0}}, {4, {0xFF}}};
    image.graphs = {nvr::Graph{}};

    nvr::Exit call;
    call.kind = nvr::ExitKind::Call;
    call.target = 1;
    call.resume = 1;
    nvr::Exit zero;
    zero.kind = nvr::ExitKind::Return;
    zero.arguments = {{ValueKind::Constant, 1, 0}};
    nvr::Function entry;
    entry.parameter_count = 1;
    entry.result_type = 1;
    entry.frame_types = {3};
    entry.blocks = {one_exit(call), one_exit(zero)};

    nvr::Function build;
    build.frame_types = {5, 2};
    nvr::Exit done;
    done.kind = nvr::ExitKind::Return;
    build.blocks = {
        state_block(nvr::StateOp::AllocateCollection, 1, 0, {{ValueKind::Constant, 4, 1}}, 5),
        state_block(nvr::StateOp::ConstructText, 2, 1, {{ValueKind::Slot, 5, 0}}, 2),
        one_exit(done),
    };
    image.functions = {std::move(entry), std::move(build)};
    return image;
}

// Programs formed while blocks that never ran are still stubs.
nvm::TierOptions stubbing_programs() {
    auto options = eager_programs();
    options.program_proofs = 0;
    return options;
}

// Compares `image` at tier 0 and in programs with `set` applied for every
// value from `first` to `last`.
void compare_sweep(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                   const std::vector<std::string>& arguments,
                   const std::function<void(nvm::RunLimits&, std::uint64_t)>& set, std::uint64_t first,
                   std::uint64_t last) {
    for (std::uint64_t value = first; value <= last; ++value) {
        compare_with_tier_0(image, cache, label + " with limit " + std::to_string(value), arguments,
                            [&](nvm::RunLimits& limits) { set(limits, value); });
    }
}

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
    std::size_t dispatches = 0;
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
            dispatches += op.code == nvm::tier2::Code::Dispatch;
            if (op.code == nvm::tier2::Code::Call) {
                check(program.entry_at(program.exits[op.index]->resume) != nullptr, "a call's resume is an entry");
            }
        }
        check_equal(program.block_labels.size(), image.functions[function].blocks.size(), "every block has a label");
    }
    check(formed > 3, "the program's functions formed programs");
    check(later_reads > 0, "a loop head or a resume reads a local its entry 0 does not");
    // Interface calls and the closure call go through dispatch blocks, which
    // programs run as Dispatch ops.
    check(dispatches >= 2, "dispatch blocks are Dispatch ops in programs");
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

NEURA_TEST(tier, helpers_for_allocation_growth_and_text_match_tier_0_under_every_heap_limit) {
    // Objects, arrays, lists (append and repeat) and text in a loop and in a
    // recursive function; text slices, concatenation, class text and hashes;
    // bytes turned into text, valid and invalid. Each heap limit from 1 cell
    // to the run's peak + 2 traps in place at a different allocation, with
    // a collection at the allocations before it.
    const auto image = build_program({tier_program(environment, "program-helpers")});
    for (const auto& [kind, size] : helper_cases) {
        nvm::TierCache cache(image, eager_programs());
        const std::vector<std::string> arguments{kind, size};
        compare_with_tier_0(image, cache, kind, arguments);
        const auto peak = run_captured(image, limits_at(0), std::nullopt, arguments).result.peak_heap_cells;
        check(peak > 20, std::string(kind) + " allocates");
        compare_sweep(image, cache, kind, arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_heap_cells = value; }, 1, peak + 2);
        if (!programs_run()) continue;
        const auto counts = cache.statistics().region_counts;
        check(counts.helpers > 0, std::string(kind) + ": helpers ran inside programs");
        check(counts.traps > 0, std::string(kind) + ": a heap limit trapped in place at a helper");
    }
}

NEURA_TEST(tier, every_state_operation_a_region_does_not_run_is_a_helper_in_a_program) {
    // After runs that lifted the blocks: the programs of the helpers image
    // contain a Helper for each operation that allocates, grows, shrinks,
    // frees, builds text, or calls a kernel, and none under the region check
    // (NEURA_TIER_CHECK=1), which compiles them as deoptimizations. A
    // kernel's resume block is an entry that begins with a budget check.
    const auto image = build_program({tier_program(environment, "program-helpers")});
    nvm::TierCache cache(image, eager_programs());
    for (const auto& [kind, size] : std::vector<HelperCase>{{"alloc", "6"}, {"kernel", "3"}, {"text", "3"},
                                                           {"utf8", "4"}, {"remove", "5"}, {"free", "5"}}) {
        run_captured(image, limits_at(2, &cache), std::nullopt, {kind, size});
    }
    if (!programs_run()) return;
    std::set<nvr::StateOp> helpers;
    std::size_t kernel_resumes = 0;
    for (const bool checkable : {false, true}) {
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
            }, checkable);
            if (!formation.region) continue;
            const auto& program = *formation.region;
            for (const auto& op : program.code) {
                if (op.code != nvm::tier2::Code::Helper) continue;
                check(!checkable, "no Helper under the region check");
                const auto& exit = *program.exits[op.index];
                helpers.insert(exit.operation);
                check(!nvm::tier2::inline_state_operation(exit), "a helper is a state operation a region does not run");
                if (exit.operation != nvr::StateOp::Kernel) continue;
                ++kernel_resumes;
                const auto* entry = program.entry_at(exit.resume);
                check(entry != nullptr, "a kernel's resume block is an entry");
                if (entry) {
                    check(program.code[entry->label].code == nvm::tier2::Code::Budget,
                          "a kernel's resume block begins with a budget check");
                }
            }
        }
    }
    for (const auto operation :
         {nvr::StateOp::AllocateObject, nvr::StateOp::AllocateCollection, nvr::StateOp::RepeatCollection,
          nvr::StateOp::AppendElement, nvr::StateOp::RemoveElement, nvr::StateOp::FreeObject,
          nvr::StateOp::ClassHash, nvr::StateOp::ClassText, nvr::StateOp::ConstructText, nvr::StateOp::TextSlice,
          nvr::StateOp::TextFromUtf8, nvr::StateOp::ScalarText, nvr::StateOp::Kernel}) {
        check(helpers.contains(operation),
              "state operation " + std::to_string(static_cast<int>(operation)) + " is a Helper in some program");
    }
    check(kernel_resumes > 0, "a kernel call was found");
}

NEURA_TEST(tier, helpers_match_tier_0_under_every_handle_limit) {
    const auto image = build_program({tier_program(environment, "program-helpers")});
    nvm::TierCache cache(image, eager_programs());
    const std::vector<std::string> arguments{"alloc", "4"};
    // Every handle limit up to the first at which the run completes, and a
    // few above it.
    std::uint64_t handles = 1;
    for (; handles < 400; ++handles) {
        auto limits = limits_at(0);
        limits.max_handles = handles;
        if (run_captured(image, limits, std::nullopt, arguments).result.completed) break;
    }
    check(handles < 400, "the run completes under some handle limit");
    compare_sweep(image, cache, "alloc", arguments,
                  [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_handles = value; }, 1, handles + 3);
    if (programs_run()) check(cache.statistics().region_counts.traps > 0, "a handle limit trapped in place");
}

NEURA_TEST(tier, kernel_calls_in_programs_match_tier_0_at_every_step_limit) {
    // The kernel charges its steps after the block's own: a step limit lands
    // before, inside and after the charge, and the blocks after a kernel run
    // only under a Budget that sees the charge. Function steps (--profile)
    // include the kernel's.
    const auto image = build_program({tier_program(environment, "program-helpers")});
    nvm::TierCache cache(image, eager_programs());
    const std::vector<std::string> arguments{"kernel", "3"};
    compare_with_tier_0(image, cache, "kernel", arguments);
    const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments).result;
    check(reference.steps > 100, "kernels charge steps");
    compare_sweep(image, cache, "kernel", arguments,
                  [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_steps = value; }, 1,
                  reference.steps + 1);
    if (programs_run()) {
        const auto counts = cache.statistics().region_counts;
        check(counts.helpers > 0, "kernels ran as helpers inside programs");
        check(counts.traps > 0, "a step limit inside a kernel's charge trapped in place");
    }
}

NEURA_TEST(tier, helper_traps_match_tier_0_after_the_blocks_step) {
    // A freed object read again, and a list element removed out of bounds,
    // in a loop: the trap is the reference's, with the callers of tier 0, at
    // every step limit and every frame limit.
    const auto image = build_program({tier_program(environment, "program-helpers")});
    for (const auto* kind : {"free", "remove"}) {
        nvm::TierCache cache(image, eager_programs());
        const std::vector<std::string> arguments{kind, "5"};
        compare_with_tier_0(image, cache, kind, arguments);
        const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments).result;
        check(!reference.completed && reference.trap.callers.size() >= 2, std::string(kind) + " traps in a callee");
        compare_sweep(image, cache, std::string(kind) + " steps", arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_steps = value; }, 1,
                      reference.steps + 1);
        compare_sweep(image, cache, std::string(kind) + " frames", arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) {
                          limits.max_frames = static_cast<std::uint32_t>(value);
                      },
                      1, 4);
        if (programs_run()) {
            const auto counts = cache.statistics().region_counts;
            check(counts.helpers > 0, std::string(kind) + ": helpers ran inside programs");
            // The removal's out-of-bounds index traps in the helper; the
            // freed object's next read traps in an inline read, which
            // deoptimizes.
            if (std::string(kind) == "remove") check(counts.traps > 0, "the removal trapped in place");
        }
    }
}

NEURA_TEST(tier, invalid_utf8_in_text_construction_traps_like_tier_0) {
    // ConstructText of the bytes {0xFF}: an invalid-text trap in the helper,
    // the block's step already charged, with its caller listed; the same
    // image under every step limit and small heap limits. Bytes through
    // string.from_utf8 (TextFromUtf8), valid and invalid, run in the utf8
    // case of program-helpers.
    const auto image = nvr::load(nvr::encode(invalid_text_image()));
    nvm::TierCache cache(image, eager_programs());
    compare_with_tier_0(image, cache, "invalid text", {});
    const auto reference = run_captured(image, limits_at(0), std::nullopt, {}).result;
    check(!reference.completed && reference.trap.kind == nvm::TrapKind::InvalidText, "the text is invalid");
    compare_sweep(image, cache, "invalid text steps", {},
                  [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_steps = value; }, 1,
                  reference.steps + 1);
    compare_sweep(image, cache, "invalid text cells", {},
                  [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_heap_cells = value; }, 1, 8);
    if (programs_run()) {
        const auto counts = cache.statistics().region_counts;
        check(counts.helpers > 0 && counts.traps > 0, "the allocation ran as a helper and the text trapped in place");
    }
    const auto helpers = build_program({tier_program(environment, "program-helpers")});
    nvm::TierCache utf8_cache(helpers, eager_programs());
    compare_with_tier_0(helpers, utf8_cache, "utf8", {"utf8", "9"});
}

NEURA_TEST(tier, a_function_with_an_unlifted_block_is_a_program_with_a_stub) {
    // The branch taken from the fifth iteration on has not run when the
    // program forms, so its block is a stub: a deoptimization the program
    // counts, which tier 1 then runs. Once it has, the program is formed
    // again with that block compiled.
    const auto image = build_program({tier_program(environment, "program-helpers")});
    nvm::TierCache cache(image, stubbing_programs());
    const std::vector<std::string> arguments{"stub", "40"};
    compare_with_tier_0(image, cache, "stub", arguments);
    const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments).result;
    compare_sweep(image, cache, "stub steps", arguments,
                  [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_steps = value; }, 1,
                  reference.steps + 1);
    if (!programs_run()) return;
    const auto statistics = cache.statistics();
    check(statistics.programs > 0 && statistics.program_stubs > 0, "a program was formed with a stub");
    check(statistics.region_counts.exits > 0, "the stub's deoptimizations are counted");
    check(statistics.refreshed > 0, "a program formed with pending blocks is formed again");
}

NEURA_TEST(tier, allocation_in_programs_of_a_task_image_charges_frame_cells) {
    // A task image: frame cells are charged to the heap by every push, so a
    // heap limit lands inside a call as well as inside an allocation, and
    // quanta end turns inside programs.
    const auto image = build_program({tier_program(environment, "program-task-alloc")});
    for (const std::uint64_t quantum : {1ULL, 2ULL, 3ULL, 4ULL}) {
        nvm::TierCache cache(image, eager_programs());
        const auto label = "task allocation with quantum " + std::to_string(quantum);
        const auto adjust_quantum = [&](nvm::RunLimits& limits) { limits.task_quantum = quantum; };
        compare_with_tier_0(image, cache, label, {}, adjust_quantum);
        auto reference_limits = limits_at(0);
        adjust_quantum(reference_limits);
        const auto peak = run_captured(image, reference_limits).result.peak_heap_cells;
        for (std::uint64_t cells = std::max<std::uint64_t>(1, peak / 3); cells <= peak + 2; ++cells) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(cells) + " cells", {},
                                [&](nvm::RunLimits& limits) {
                                    adjust_quantum(limits);
                                    limits.max_heap_cells = cells;
                                });
        }
        if (programs_run()) {
            const auto counts = cache.statistics().region_counts;
            check(counts.helpers > 0, label + ": allocations ran as helpers");
            check(counts.traps > 0, label + ": a heap limit trapped in place");
        }
    }
}

NEURA_TEST(tier, the_shadow_oracle_syncs_inside_a_long_loop_of_a_program_and_finds_a_planted_difference) {
    // A loop of about half a million blocks that runs in one program: the
    // oracle looks every shadow_sync_blocks blocks, not once at the end.
    const auto image = build_program({tier_program(environment, "program-helpers")});
    const std::vector<std::string> arguments{"stub", "100000"};
    const auto syncs = check_clean(image, "a long loop", arguments);
    if (!programs_run()) return;
    check(syncs >= 6, "the oracle synced inside the loop: " + std::to_string(syncs));
    // A difference planted in the middle of the run is reported at a sync
    // point inside the loop, not at the end of the run.
    for (const auto perturb : {Perturb::SlotValue, Perturb::SlotInitialized, Perturb::LiveCells, Perturb::Steps}) {
        nvm::TierCache cache(image, eager_programs());
        auto limits = limits_at(2, &cache);
        Hooks hooks;
        hooks.hooks.perturb = perturb;
        hooks.hooks.at_step = 200'000;
        limits.tier_shadow = &hooks.hooks;
        run_captured(image, limits, std::nullopt, arguments);
        check(hooks.hooks.perturbed, "the run was perturbed");
        check_equal(hooks.reports.size(), std::size_t{1}, "one report");
        if (!hooks.reports.empty()) {
            check(hooks.reports.front().find("after compiled code returned") != std::string::npos,
                  "the difference is reported inside the loop: " + hooks.reports.front());
        }
    }
}

NEURA_TEST(tier, the_shadow_oracle_checks_helpers_and_finds_a_difference_planted_in_the_heap) {
    const auto image = build_program({tier_program(environment, "program-helpers")});
    for (const auto& [kind, size] : helper_cases) check_clean(image, kind, {kind, size});
    check_clean(image, "kernel", {"kernel", "3"});
    for (const auto* kind : {"free", "remove"}) check_clean(image, kind, {kind, "5"});
    // A trap in a helper ends the run there: the oracle compares it at the
    // trap event, which has no sync point of its own before it.
    check_clean(image, "alloc with a heap limit", {"alloc", "6"},
                [](nvm::RunLimits& limits) { limits.max_heap_cells = 40; });
    if (!programs_run()) return;
    for (const auto perturb : {Perturb::HeapField, Perturb::HeapInitialized, Perturb::ListVersion}) {
        nvm::TierCache cache(image, eager_programs());
        auto limits = limits_at(2, &cache);
        Hooks hooks;
        hooks.hooks.perturb = perturb;
        hooks.hooks.at_step = 100;
        hooks.hooks.every_heap = true;
        limits.tier_shadow = &hooks.hooks;
        run_captured(image, limits, std::nullopt, {"alloc", "12"});
        check(hooks.hooks.perturbed, "the run was perturbed");
        check_equal(hooks.reports.size(), std::size_t{1}, "one report");
        if (!hooks.reports.empty()) {
            check(hooks.reports.front().find("heap handle ") != std::string::npos,
                  "the report names a heap entry: " + hooks.reports.front());
        }
    }
}

} // namespace neura::testing
