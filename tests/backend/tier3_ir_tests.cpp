// Tier 3 (src/nvm/tier3.hpp): the IR a function program is built into, the
// verifier's rules, the materialization of frame states, and the IR
// interpreter that runs native programs: every run it makes is observably the
// run tier 0 makes, wherever a limit is reached, a callee traps, or the
// shadow oracle looks. The `tier3` suite (backend-tier3).
#include "graph_test_support.hpp"

#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvm/tier3.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace neura::testing {
namespace {

namespace t3 = nvm::tier3;
using Perturb = nvm::TierShadowHooks::Perturb;

// Lifts every block, forms every program at the first arrival, and runs
// every native program from the first checkpoint on the IR interpreter.
nvm::TierOptions native_options(std::uint32_t native_after = 1) {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    options.programs = nvm::TierOptions::Programs::On;
    options.tier3 = nvm::TierOptions::Tier3::Interp;
    options.native_after = native_after;
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
    auto native_limits = limits_at(2, &cache);
    if (adjust) adjust(native_limits);
    check_same(run_captured(image, native_limits, std::nullopt, arguments), reference, label);
}

void compare_sweep(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                   const std::vector<std::string>& arguments,
                   const std::function<void(nvm::RunLimits&, std::uint64_t)>& set, std::uint64_t first,
                   std::uint64_t last) {
    for (std::uint64_t value = first; value <= last; ++value) {
        compare_with_tier_0(image, cache, label + " with limit " + std::to_string(value), arguments,
                            [&](nvm::RunLimits& limits) { set(limits, value); });
    }
}

fs::path program_in(const Environment& environment, const std::string& directory, const std::string& name) {
    return backend_dir(environment) / directory / (name + ".nra");
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

// A function program and the IR built from it.
struct Built {
    std::uint32_t function = 0;
    std::shared_ptr<const nvm::tier2::Region> region;
    t3::BuildResult result;
};

// Forms the program of every function the cache has lifted blocks for (run
// the image through the cache first) and builds its IR, with the verifier.
std::vector<Built> build_all(const nvr::Image& image, nvm::TierCache& cache) {
    std::vector<Built> list;
    for (std::uint32_t function = 0; function < image.functions.size(); ++function) {
        const auto formation = nvm::tier2::form_program(
            image, function,
            [&](std::uint32_t block) {
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
            },
            false);
        if (!formation.region) continue;
        Built built;
        built.function = function;
        built.region = formation.region;
        built.result = t3::build(image, *formation.region, true);
        list.push_back(std::move(built));
    }
    return list;
}

struct Corpus {
    nvr::Image image;
    std::unique_ptr<nvm::TierCache> cache;
    std::vector<Built> built;
};

Corpus corpus_of(const Environment& environment, const std::string& directory, const std::string& name,
                 const std::vector<std::vector<std::string>>& runs) {
    Corpus corpus;
    corpus.image = build_program({program_in(environment, directory, name)});
    corpus.cache = std::make_unique<nvm::TierCache>(corpus.image, native_options());
    for (const auto& arguments : runs) {
        run_captured(corpus.image, limits_at(2, corpus.cache.get()), std::nullopt, arguments);
    }
    corpus.built = build_all(corpus.image, *corpus.cache);
    return corpus;
}

const std::vector<std::vector<std::string>> shapes_runs{{"sum", "40"}, {"make", "8"}, {"descend", "6"}, {"grow", "30"}};

Corpus shapes_corpus(const Environment& environment) {
    return corpus_of(environment, "tier3", "shapes", shapes_runs);
}

// Every built IR with its image, from the three programs the suite uses.
struct Everything {
    std::vector<Corpus> corpora;
};

Everything everything(const Environment& environment) {
    Everything all;
    all.corpora.push_back(shapes_corpus(environment));
    all.corpora.push_back(corpus_of(environment, "tier", "program-calls",
                                    {{"calls", "4"}, {"overflow", "4"}, {"index", "4"}, {"nil", "4"}, {"field", "4"}}));
    all.corpora.push_back(corpus_of(environment, "tier", "program-helpers",
                                    {{"alloc", "9"}, {"kernel", "5"}, {"text", "5"}, {"utf8", "4"}, {"free", "4"},
                                     {"remove", "4"}, {"stub", "30"}}));
    return all;
}

// The first built IR (and its image) a predicate accepts.
struct Found {
    const nvr::Image* image = nullptr;
    const t3::Function* ir = nullptr;
};

Found find_ir(const Everything& all, const std::function<bool(const t3::Function&)>& wanted) {
    for (const auto& corpus : all.corpora) {
        for (const auto& built : corpus.built) {
            if (built.result.program && wanted(built.result.program->ir)) {
                return {&corpus.image, &built.result.program->ir};
            }
        }
    }
    return {};
}

bool has_op(const t3::Function& ir, t3::Op op) {
    return std::any_of(ir.blocks.begin(), ir.blocks.end(), [&](const t3::Block& block) {
        return std::any_of(block.insts.begin(), block.insts.end(),
                           [&](std::uint32_t id) { return ir.insts[id].op == op; });
    });
}

bool has_state_call(const t3::Function& ir) {
    return std::any_of(ir.insts.begin(), ir.insts.end(), [](const t3::Inst& in) {
        return in.op == t3::Op::StateCall && (in.flags & t3::CheckReference) == 0;
    });
}

bool has_checked_lift(const t3::Function& ir) {
    return std::any_of(ir.insts.begin(), ir.insts.end(),
                       [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
}

// A loop: a Budget block with phis, among the blocks an entry reaches.
bool has_loop_head(const t3::Function& ir) {
    for (const auto& block : ir.blocks) {
        if (block.insts.size() < 2 || ir.insts[block.insts.front()].op != t3::Op::Phi) continue;
        if (std::any_of(block.insts.begin(), block.insts.end(),
                        [&](std::uint32_t id) { return ir.insts[id].op == t3::Op::Budget; })) {
            return true;
        }
    }
    return false;
}

// No phi joins one value (and itself): the builder removes them.
void check_no_trivial_phis(const t3::Function& ir, const std::string& label) {
    for (std::uint32_t id = 0; id < ir.insts.size(); ++id) {
        const auto& in = ir.insts[id];
        if (in.op != t3::Op::Phi) continue;
        std::set<std::uint32_t> distinct;
        for (std::uint32_t i = 0; i < in.count; ++i) {
            if (ir.extras[in.extra + i] != id) distinct.insert(ir.extras[in.extra + i]);
        }
        check(distinct.size() >= 2, label + ": phi %" + std::to_string(id) + " joins a single value");
    }
}

bool starts_with_rule(const std::string& text, const std::string& rule) { return text.rfind(rule + ":", 0) == 0; }

bool reports_rule(const std::vector<std::string>& violations, const std::string& rule) {
    return std::any_of(violations.begin(), violations.end(),
                       [&](const std::string& v) { return starts_with_rule(v, rule); });
}

// The unmodified IR verifies; the corrupted copy is rejected under `rule`.
void expect_rejected(const nvr::Image& image, const t3::Function& base, const std::string& rule,
                     const std::string& label, const std::function<void(t3::Function&)>& corrupt) {
    const auto clean = t3::verify(image, base);
    check(clean.empty(), label + ": the unmodified IR verifies" + (clean.empty() ? "" : ": " + clean.front()));
    auto copy = base;
    corrupt(copy);
    const auto violations = t3::verify(image, copy);
    check(reports_rule(violations, rule), label + ": " + rule + " rejects the corrupted IR" +
                                              (violations.empty() ? ": nothing was reported"
                                                                  : ": it reported " + violations.front()));
}

// Position of the first instruction `wanted` accepts: its block and index.
struct Position {
    std::uint32_t block = t3::none;
    std::size_t index = 0;
    std::uint32_t id = t3::none;
    bool found() const { return block != t3::none; }
};

Position locate(const t3::Function& ir, const std::function<bool(const t3::Inst&)>& wanted) {
    for (std::uint32_t b = 0; b < ir.blocks.size(); ++b) {
        for (std::size_t k = 0; k < ir.blocks[b].insts.size(); ++k) {
            const auto id = ir.blocks[b].insts[k];
            if (wanted(ir.insts[id])) return {b, k, id};
        }
    }
    return {};
}

// Adds an instruction to the function and to `block` at `index`.
std::uint32_t insert(t3::Function& ir, std::uint32_t block, std::size_t index, t3::Inst inst) {
    const auto id = static_cast<std::uint32_t>(ir.insts.size());
    ir.insts.push_back(std::move(inst));
    auto& insts = ir.blocks[block].insts;
    insts.insert(insts.begin() + static_cast<std::ptrdiff_t>(index), id);
    return id;
}

t3::Inst constant_inst(std::uint64_t bits, unsigned width, std::uint32_t origin_block, std::uint32_t origin_function) {
    t3::Inst in;
    in.op = t3::Op::Const;
    in.type = {t3::Type::Int, static_cast<std::uint8_t>(width)};
    in.imm = bits;
    in.origin_block = origin_block;
    in.origin_function = origin_function;
    return in;
}

// --- An image of three frames deep, built by hand -----------------------------

// Function 0 calls function 1, which calls function 2; stopped by a step
// limit after 3 steps, the frames are
//   0: block 0 (the call block), slots {args, 7, uninitialized}
//   1: block 0 (its call block), return slot 2 block 1, slots {7, 9, uninitialized}
//   2: block 1, return slot 2 block 1, slots {7, 11}.
nvr::Image three_frames_image() {
    using Kind = nvr::TypeKind;
    using nvr::ValueKind;
    nvr::Image image;
    // 0 void, 1 int32, 2 text, 3 text[]
    image.types = {{Kind::Void}, {Kind::Signed, 32}, {Kind::Text}, {Kind::Array, 0, 2}};
    const auto number = [](std::uint8_t value) { return nvr::Constant{1, {value, 0, 0, 0}}; };
    image.constants = {number(7), number(9), number(11), number(0)};
    image.graphs = {nvr::Graph{}};
    const auto one_exit = [](nvr::Exit exit) {
        nvr::Block block;
        block.exits.push_back(std::move(exit));
        return block;
    };
    const auto call = [&](std::uint32_t target, nvr::Value argument, std::uint32_t assigned_slot,
                          std::uint32_t assigned_constant, std::uint32_t result_slot) {
        nvr::Exit exit;
        exit.kind = nvr::ExitKind::Call;
        exit.target = target;
        exit.arguments = {argument};
        exit.assignments = {{assigned_slot, {ValueKind::Constant, 1, assigned_constant}}};
        exit.result_slot = result_slot;
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
    entry.blocks = {call(1, {ValueKind::Constant, 1, 0}, 1, 0, 2), leave({ValueKind::Constant, 1, 3})};
    nvr::Function middle;
    middle.parameter_count = 1;
    middle.result_type = 1;
    middle.frame_types = {1, 1, 1};
    middle.blocks = {call(2, {ValueKind::Slot, 1, 0}, 1, 1, 2), leave({ValueKind::Constant, 1, 3})};
    nvr::Function inner;
    inner.parameter_count = 1;
    inner.result_type = 1;
    inner.frame_types = {1, 1};
    nvr::Exit next;
    next.kind = nvr::ExitKind::Continue;
    next.assignments = {{1, {ValueKind::Constant, 1, 2}}};
    next.resume = 1;
    inner.blocks = {one_exit(std::move(next)), leave({ValueKind::Slot, 1, 1})};
    image.functions = {std::move(entry), std::move(middle), std::move(inner)};
    return image;
}

// How tier 1 runs each block of a hand-built image: empty or wired.
nvm::tier2::BlockForm hand_form(const nvr::Image& image, std::uint32_t function, std::uint32_t block) {
    nvm::tier2::BlockForm form;
    const auto& decoded = image.functions[function].blocks[block];
    const auto& graph = image.graphs[decoded.graph];
    if (!graph.outputs.empty()) form.kind = nvm::tier2::BlockForm::Kind::Wired;
    else if (decoded.exits.size() == 1) form.kind = nvm::tier2::BlockForm::Kind::Empty;
    return form;
}

std::shared_ptr<const nvm::tier2::Region> hand_program(const nvr::Image& image, std::uint32_t function) {
    const auto formation = nvm::tier2::form_program(
        image, function, [&](std::uint32_t block) { return hand_form(image, function, block); }, false);
    check(formation.region != nullptr, "the hand-built function forms a program: " + formation.reason);
    return formation.region;
}

// A loop that swaps two slots while its parameter holds, built from wired
// blocks: function 0 has the blocks
//   0: s1 = 0, s2 = 1, then 1
//   1: (loop head) selects on the bool s0: false leaves to 2; true swaps s1 and s2 and goes to 1
//   2: returns s1.
nvr::Image swap_loop_image() {
    using Kind = nvr::TypeKind;
    using nvr::ValueKind;
    nvr::Image image;
    // 0 void, 1 int32, 2 bool
    image.types = {{Kind::Void}, {Kind::Signed, 32}, {Kind::Bool, 1}};
    image.constants = {nvr::Constant{1, {0, 0, 0, 0}}, nvr::Constant{1, {1, 0, 0, 0}}};
    nvr::Graph select;
    select.input_lanes = 1;
    select.outputs = {0};
    image.graphs = {nvr::Graph{}, select};
    nvr::Function loop;
    loop.parameter_count = 1;
    loop.result_type = 1;
    loop.frame_types = {2, 1, 1};
    nvr::Exit start;
    start.kind = nvr::ExitKind::Continue;
    start.assignments = {{1, {ValueKind::Constant, 1, 0}}, {2, {ValueKind::Constant, 1, 1}}};
    start.resume = 1;
    nvr::Block first;
    first.exits.push_back(start);
    nvr::Exit leave;
    leave.kind = nvr::ExitKind::Continue;
    leave.resume = 2;
    nvr::Exit swap;
    swap.kind = nvr::ExitKind::Continue;
    swap.assignments = {{1, {ValueKind::Slot, 1, 2}}, {2, {ValueKind::Slot, 1, 1}}};
    swap.resume = 1;
    nvr::Block head;
    head.graph = 1;
    head.inputs = {{ValueKind::Slot, 2, 0}};
    head.selector_start = 0;
    head.exits = {leave, swap};
    nvr::Exit done;
    done.kind = nvr::ExitKind::Return;
    done.arguments = {{ValueKind::Slot, 1, 1}};
    nvr::Block last;
    last.exits.push_back(done);
    loop.blocks = {first, head, last};
    image.functions = {std::move(loop)};
    return image;
}

// A slot written back and then assigned its entry value again: function 0 has
//   0: r = 9
//   1: s = r, r = 7, allocate a list into t (a helper: its write-back stores r)
//   2: r = s
//   3: a trap exit (a deoptimization), where tier 1 must find r as it was.
nvr::Image overwritten_slot_image() {
    using Kind = nvr::TypeKind;
    using nvr::ValueKind;
    nvr::Image image;
    // 0 void, 1 int32, 2 text, 3 text[], 4 uint8, 5 uint8[list]
    image.types = {{Kind::Void}, {Kind::Signed, 32}, {Kind::Text}, {Kind::Array, 0, 2}, {Kind::Unsigned, 8},
                   {Kind::List, 0, 4}};
    image.constants = {nvr::Constant{1, {9, 0, 0, 0}}, nvr::Constant{4, {0xFF}}, nvr::Constant{1, {7, 0, 0, 0}}};
    image.graphs = {nvr::Graph{}};
    nvr::Function function;
    function.parameter_count = 1;
    function.result_type = 1;
    function.frame_types = {3, 1, 1, 5};
    nvr::Exit first;
    first.kind = nvr::ExitKind::Continue;
    first.assignments = {{1, {ValueKind::Constant, 1, 0}}};
    first.resume = 1;
    nvr::Exit allocate;
    allocate.kind = nvr::ExitKind::State;
    allocate.operation = nvr::StateOp::AllocateCollection;
    allocate.immediate = 5;
    allocate.arguments = {{ValueKind::Constant, 4, 1}};
    allocate.assignments = {{2, {ValueKind::Slot, 1, 1}}, {1, {ValueKind::Constant, 1, 2}}};
    allocate.result_slot = 3;
    allocate.resume = 2;
    nvr::Exit restore;
    restore.kind = nvr::ExitKind::Continue;
    restore.assignments = {{1, {ValueKind::Slot, 1, 2}}};
    restore.resume = 3;
    nvr::Exit trap;
    trap.kind = nvr::ExitKind::Trap;
    trap.reason = 1;
    for (auto exit : {first, allocate, restore, trap}) {
        nvr::Block block;
        block.exits.push_back(exit);
        function.blocks.push_back(block);
    }
    image.functions = {std::move(function)};
    return image;
}

// The program of blocks 1 to 3 of that function, entered at block 1 (which
// reads r), written as the compiler would: s = r, r = 7, the helper, r = s,
// and the trap exit as a deoptimization.
nvm::tier2::Region overwritten_slot_program(const nvr::Image& image) {
    using nvm::tier2::Code;
    nvm::tier2::Region region;
    region.program = true;
    region.function = 0;
    region.parameter_count = 1;
    region.slots = {0, 1, 2, 3};
    region.slot_widths = {64, 32, 32, 64};
    region.register_count = 6;
    region.immediates = {{4, 7}, {5, 255}};
    region.entries = {{1, 0, {1}}};
    region.block_labels = {100, 0, 4, 7};
    region.checkpoints = {{1, 3, 0}};
    region.exits = {&image.functions[0].blocks[1].exits[0]};
    region.operands = {5};
    const auto op = [](Code code) {
        nvm::tier2::Op o;
        o.code = code;
        o.block = 1;
        return o;
    };
    auto store_s = op(Code::Store);
    store_s.dst = 2;
    store_s.a = 1;
    auto store_r = op(Code::Store);
    store_r.dst = 1;
    store_r.a = 4;
    auto helper = op(Code::Helper);
    helper.index = 0;
    helper.other = 0;
    helper.width = 1;
    helper.dst = 3;
    helper.target = 4;
    auto restore = op(Code::Store);
    restore.block = 2;
    restore.dst = 1;
    restore.a = 2;
    auto jump = op(Code::Jump);
    jump.block = 2;
    jump.flags = nvm::tier2::completes;
    jump.target = 7;
    auto deopt = op(Code::Deopt);
    deopt.block = 3;
    // pc: 0 budget, 1-2 stores, 3 helper, 4 restore, 5 jump, 6 unused, 7 deopt
    auto unused = op(Code::Deopt);
    region.code = {op(Code::Budget), store_s, store_r, helper, restore, jump, unused, deopt};
    return region;
}

// An execution of `image` stopped by a step limit after `steps` steps.
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

void check_same_frames(const std::vector<nvm::Execution::Tier3Frame>& actual,
                       const std::vector<nvm::Execution::Tier3Frame>& expected, const std::string& label) {
    check_equal(actual.size(), expected.size(), label + " frame count");
    for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
        const auto where = label + " frame " + std::to_string(i);
        check_equal(actual[i].function, expected[i].function, where + " function");
        check_equal(actual[i].block, expected[i].block, where + " block");
        check_equal(actual[i].return_slot, expected[i].return_slot, where + " return slot");
        check_equal(actual[i].return_block, expected[i].return_block, where + " return block");
        check_equal(actual[i].boundary, expected[i].boundary, where + " boundary");
        check(actual[i].slots == expected[i].slots, where + " slots");
        check(actual[i].initialized == expected[i].initialized, where + " initialized flags");
    }
}

} // namespace

// --- The builder --------------------------------------------------------------

NEURA_TEST(tier3, every_function_program_builds_verified_ir) {
    const auto all = everything(environment);
    std::size_t built = 0;
    std::map<std::string, std::size_t> reasons;
    for (const auto& corpus : all.corpora) {
        for (const auto& item : corpus.built) {
            if (item.result.program) {
                ++built;
                const auto violations = t3::verify(corpus.image, item.result.program->ir);
                check(violations.empty(), "function " + std::to_string(item.function) + " verifies: " +
                                              (violations.empty() ? "" : violations.front()));
                check_equal(item.result.program->ir.entries.size(), item.region->entries.size(),
                            "every program entry is an IR entry block");
                check_no_trivial_phis(item.result.program->ir, "function " + std::to_string(item.function));
            } else {
                ++reasons[item.result.reason];
                check(item.result.reason.rfind("verifier", 0) != 0, "no program fails the verifier: " + item.result.reason);
            }
        }
    }
    check(built > 20, "most function programs build (" + std::to_string(built) + ")");
    for (const auto& [reason, count] : reasons) check(count < built, "refusals are the exception: " + reason);
    // A region that is not a function program is refused, with a reason.
    nvm::tier2::Region region;
    const auto refused = t3::build(all.corpora.front().image, region, true);
    check(!refused.program && !refused.reason.empty(), "a region that is not a program is refused with a reason");
}

NEURA_TEST(tier3, the_builder_makes_exact_ssa_for_a_hand_built_loop_with_a_swap) {
    const auto image = swap_loop_image();
    const auto region = hand_program(image, 0);
    check_equal(region->entries.size(), std::size_t{2}, "the function entry and the loop head are entries");
    const auto built = t3::build(image, *region, true);
    check(built.program != nullptr, "the loop builds and verifies: " + built.reason);
    const auto& ir = built.program->ir;
    const auto count = [&](t3::Op op, std::uint32_t block) {
        return std::count_if(ir.blocks[block].insts.begin(), ir.blocks[block].insts.end(),
                             [&](std::uint32_t id) { return ir.insts[id].op == op; });
    };
    // Entry blocks: one per program entry, each with a single edge.
    check_equal(ir.entries.size(), std::size_t{2}, "one IR entry block per program entry");
    const auto loop_entry = ir.entries[1].ir_block;
    const auto head = ir.blocks[loop_entry].succs.at(0);
    check_equal(ir.blocks[head].origin_block, 1U, "the loop head's entry leads to image block 1");
    // The head is reached from the loop's entry, the code before it, and its back edge.
    check_equal(ir.blocks[head].preds.size(), std::size_t{3}, "three edges reach the loop head");
    check_equal(ir.blocks[ir.blocks[head].preds[0]].entry, 1U, "the first is the loop head's own entry block");
    // Slots 1 and 2 change in the loop: a value phi and a flag phi each, with
    // one operand per edge; the counter is a phi too.
    check_equal(count(t3::Op::Phi, head), std::ptrdiff_t{6},
                "phis for the two slots, their two flags, the block count, and the parameter, which joins the loads of both entries");
    std::map<std::uint32_t, std::uint32_t> loads;
    for (const auto id : ir.blocks[head].insts) {
        const auto& phi = ir.insts[id];
        if (phi.op != t3::Op::Phi) break;
        check_equal(phi.count, 3U, "a phi has an operand for each of the three edges");
        // From the loop head's entry the value is the frame's own.
        const auto from_entry = ir.insts[ir.extras[phi.extra]];
        check(from_entry.op == t3::Op::EntryLoad || from_entry.op == t3::Op::EntryFlag || from_entry.op == t3::Op::EntryCounter,
              "the entry edge carries a load");
        // On the swap's back edge each slot takes the other's value.
        if (from_entry.op == t3::Op::EntryLoad) loads[static_cast<std::uint32_t>(from_entry.imm)] = id;
    }
    check_equal(loads.size(), std::size_t{3}, "a value phi for slot 0 (the parameter), slot 1 and slot 2");
    const auto& slot1 = ir.insts[loads.at(1)];
    const auto& slot2 = ir.insts[loads.at(2)];
    check_equal(ir.extras[slot1.extra + 2], loads.at(2), "the swap: slot 1 takes slot 2's value on the back edge");
    check_equal(ir.extras[slot2.extra + 2], loads.at(1), "the swap: slot 2 takes slot 1's value on the back edge");
    check_equal(ir.insts[ir.extras[slot1.extra + 1]].imm, std::uint64_t{0}, "slot 1 is 0 on the edge from the first block");
    check_equal(ir.insts[ir.extras[slot2.extra + 1]].imm, std::uint64_t{1}, "slot 2 is 1 on the edge from the first block");
    // The selector is the bool's lane, read as it is (the program dropped its
    // mask: a bool slot has one bit), then the selector check and the switch.
    check_equal(count(t3::Op::Mask, head), std::ptrdiff_t{0}, "the selector lane needs no mask");
    check_equal(count(t3::Op::SwitchCheck, head), std::ptrdiff_t{1}, "the selector is checked");
    check_equal(ir.blocks[head].succs.size(), std::size_t{2}, "two exits");
    // Budgets: the first block runs one block before the loop head's checkpoint;
    // the loop head runs itself and the return block before the next one, on the
    // way out, and only itself on the way around.
    std::map<std::uint32_t, std::uint64_t> budgets;
    for (const auto& in : ir.insts) {
        if (in.op == t3::Op::Budget) budgets[in.origin_block] = in.imm;
    }
    check_equal(budgets.size(), std::size_t{2}, "a Budget at each entry");
    check_equal(budgets.at(0), std::uint64_t{1}, "P of the first block");
    check_equal(budgets.at(1), std::uint64_t{2}, "P of the loop head: it and the return block");
    // The check at the head has the state at its entry: the phis, not the swap.
    const auto check_at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::SwitchCheck; });
    const auto& state = ir.states[ir.insts[check_at.id].state];
    check_equal(state.block, 1U, "the head's state is the head's");
    check(state.slots[1].kind == t3::StateSlot::Kind::Value && state.slots[1].value == loads.at(1) &&
              state.slots[2].kind == t3::StateSlot::Kind::Value && state.slots[2].value == loads.at(2),
          "the state names the slots' values at the head, before the swap");
    check(state.slots[0].kind == t3::StateSlot::Kind::Unchanged, "the parameter, never assigned, is the frame's own");
}

NEURA_TEST(tier3, a_call_in_hand_built_ir_has_its_commit_state_write_back_and_counter_reload) {
    const auto image = three_frames_image();
    // Function 1 assigns q = 9 and calls function 2 with its parameter.
    const auto region = hand_program(image, 1);
    const auto built = t3::build(image, *region, true);
    check(built.program != nullptr, "the function builds and verifies: " + built.reason);
    const auto& ir = built.program->ir;
    const auto call = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Call; });
    check(call.found(), "the call is an instruction");
    const auto& in = ir.insts[call.id];
    check_equal(in.imm, std::uint64_t{2}, "the callee");
    check_equal(in.count, 1U, "one argument");
    // The call block's step is counted before the call, in the state and in
    // the call's own count.
    const auto& charged = ir.insts[in.operands[0]];
    check(charged.op == t3::Op::CounterAdd && charged.imm == 1, "the block's step is a counter add");
    const auto& state = ir.states[in.state];
    check_equal(state.blocks, in.operands[0], "the state counts the step");
    check_equal(state.block, 0U, "the call block");
    // The assignment of the call block is in the state: slot 1 is the constant 9,
    // initialized; the parameter and the result slot are not the block's to write.
    check(state.slots[1].kind == t3::StateSlot::Kind::Const && state.slots[1].value == 9, "q = 9 is committed");
    check(state.flags[1].kind == t3::StateFlag::Kind::One, "and initialized");
    check(state.slots[0].kind == t3::StateSlot::Kind::Unchanged, "the parameter is unchanged");
    // Write-back before, counter reload and the edge to the resume block after.
    check(call.index > 0 && ir.insts[ir.blocks[call.block].insts[call.index - 1]].op == t3::Op::WriteBack &&
              ir.insts[ir.blocks[call.block].insts[call.index - 1]].state == in.state,
          "the write-back of the call's state precedes the call");
    check(ir.insts[ir.blocks[call.block].insts[call.index + 1]].op == t3::Op::EntryCounter, "the callee's count is reloaded");
    check(ir.insts[ir.blocks[call.block].insts.back()].op == t3::Op::Jump, "the call block goes on to the resume block");
    // At the resume block (an entry), the result slot is the call's value on the
    // edge from the call and the frame's on the edge from the entry.
    const auto resume = ir.blocks[call.block].succs.at(0);
    std::uint32_t result_phi = t3::none;
    for (const auto id : ir.blocks[resume].insts) {
        const auto& phi = ir.insts[id];
        if (phi.op != t3::Op::Phi) break;
        for (std::uint32_t i = 0; i < phi.count; ++i) {
            if (ir.extras[phi.extra + i] == call.id) result_phi = id;
        }
    }
    check(result_phi != t3::none, "a phi at the resume block takes the call's result");
    const auto& phi = ir.insts[result_phi];
    check_equal(phi.count, 2U, "it joins the call's result with the frame's value from the entry");
    const auto from_entry = ir.insts[ir.extras[phi.extra]];
    check(from_entry.op == t3::Op::EntryLoad && from_entry.imm == 2, "the entry edge carries slot 2's load");
}

NEURA_TEST(tier3, a_slot_assigned_its_entry_value_after_a_write_back_is_written_again_at_a_deoptimization) {
    const auto image = overwritten_slot_image();
    const auto region = overwritten_slot_program(image);
    const auto built = t3::build(image, region, true);
    check(built.program != nullptr, "the function builds and verifies: " + built.reason);
    const auto& ir = built.program->ir;
    // The deoptimization at block 3 names r as a value: the write-back of
    // block 1 stored 7, and r = s gave it its entry value back.
    const auto deopt = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Deopt; });
    check(deopt.found(), "the trap exit is a deoptimization");
    check(ir.states[ir.insts[deopt.id].state].slots[1].kind == t3::StateSlot::Kind::Value,
          "r is not left to the frame after it was written");
    // From the frame tier 0 has after one block, the IR interpreter leaves
    // the frame tier 0 leaves at the trap.
    nvm::RunLimits limits;
    limits.tier = 0;
    nvm::Execution reference(image, std::vector<std::string>{}, limits);
    check(reference.advance().kind == nvm::Event::Kind::Trapped, "tier 0 traps at the trap exit");
    auto actual = stop_after(image, 1);
    const auto ending = actual.execution->tier3_run(*built.program, 0);
    check_equal(ending.outcome, static_cast<unsigned>(t3::Outcome::Deopt), "the activation deoptimized");
    check_same_frames(actual.execution->tier3_frames(), reference.tier3_frames(), "after the deoptimization");
    // A state that says unchanged after the write is rejected.
    expect_rejected(image, ir, "V3", "unchanged after a write", [&](t3::Function& broken) {
        const auto at = locate(broken, [](const t3::Inst& in) { return in.op == t3::Op::Deopt; });
        broken.states[broken.insts[at.id].state].slots[1] = {};
    });
}

NEURA_TEST(tier3, a_leave_is_preceded_by_a_write_back_of_the_frame_it_leaves) {
    const auto image = three_frames_image();
    // r0 = args, r1 = q, r2 = r; the program stores 5 into slot 1 and leaves at block 1.
    nvm::tier2::Region region;
    region.program = true;
    region.function = 0;
    region.parameter_count = 1;
    region.slots = {0, 1, 2};
    region.slot_widths = {64, 32, 32};
    region.register_count = 4;
    region.immediates = {{3, 5}};
    region.entries = {{0, 0, {}}};
    region.block_labels = {0, 3};
    region.checkpoints = {{0, 1, 0}};
    const auto op = [](nvm::tier2::Code code) {
        nvm::tier2::Op o;
        o.code = code;
        return o;
    };
    auto budget = op(nvm::tier2::Code::Budget);
    auto store = op(nvm::tier2::Code::Store);
    store.dst = 1;
    store.a = 3;
    auto leave = op(nvm::tier2::Code::Leave);
    leave.flags = nvm::tier2::completes;
    leave.target = 1;
    region.code = {budget, store, leave};
    const auto built = t3::build(image, region, true);
    check(built.program != nullptr, "the program builds and verifies: " + built.reason);
    const auto& ir = built.program->ir;
    const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Leave; });
    check(at.found() && at.index > 0 && ir.insts[ir.blocks[at.block].insts[at.index - 1]].op == t3::Op::WriteBack,
          "a write-back comes right before the leave");
    nvm::RunLimits limits;
    limits.tier = 0;
    nvm::Execution actual(image, std::vector<std::string>{}, limits);
    const auto ending = actual.tier3_run(*built.program, 0);
    check_equal(ending.outcome, static_cast<unsigned>(t3::Outcome::Leave), "the activation left");
    const auto frames = actual.tier3_frames();
    check_equal(frames.size(), std::size_t{1}, "one frame");
    check_equal(frames[0].slots[1], std::uint64_t{5}, "the stored value is in the frame");
    check_equal(unsigned{frames[0].initialized[1]}, 1U, "and initialized");
    check_equal(frames[0].block, 1U, "the frame is at the leave's target");
    // A leave without its write-back is rejected.
    expect_rejected(image, ir, "V3", "a leave without a write-back", [&](t3::Function& broken) {
        const auto leave_at = locate(broken, [](const t3::Inst& in) { return in.op == t3::Op::Leave; });
        auto& insts = broken.blocks[leave_at.block].insts;
        insts.erase(insts.begin() + static_cast<std::ptrdiff_t>(leave_at.index - 1));
    });
}

NEURA_TEST(tier3, the_builder_makes_ssa_with_a_phi_for_every_value_a_loop_carries) {
    const auto all = everything(environment);
    const auto found = find_ir(all, [](const t3::Function& ir) {
        return has_loop_head(ir) && has_checked_lift(ir) && !has_op(ir, t3::Op::Call) && !has_op(ir, t3::Op::Helper);
    });
    check(found.ir != nullptr, "a loop without calls got IR");
    const auto& ir = *found.ir;
    std::size_t heads = 0;
    bool phi_over_entries = false;
    for (std::uint32_t b = 0; b < ir.blocks.size(); ++b) {
        const auto& block = ir.blocks[b];
        // Every phi has one operand per predecessor, all defined.
        for (const auto id : block.insts) {
            const auto& in = ir.insts[id];
            if (in.op != t3::Op::Phi) break;
            check_equal(in.count, block.preds.size(), "a phi has one operand per predecessor");
        }
        if (block.entry != t3::none) {
            // An entry block holds loads and one edge to its target.
            check_equal(block.succs.size(), std::size_t{1}, "an entry block has one edge");
            check_equal(block.preds.size(), std::size_t{0}, "an entry block has no predecessor");
            for (std::size_t k = 0; k + 1 < block.insts.size(); ++k) {
                const auto op = ir.insts[block.insts[k]].op;
                check(op == t3::Op::EntryLoad || op == t3::Op::EntryFlag || op == t3::Op::EntryCounter,
                      "an entry block holds only entry loads");
            }
            check(ir.insts[block.insts.back()].op == t3::Op::Jump, "an entry block ends in its edge");
            continue;
        }
        if (block.insts.empty() || ir.insts[block.insts.front()].op != t3::Op::Phi) continue;
        if (!std::any_of(block.insts.begin(), block.insts.end(),
                         [&](std::uint32_t id) { return ir.insts[id].op == t3::Op::Budget; })) {
            continue;
        }
        ++heads;
        // A loop head is an entry, so it is reached from an entry block as
        // well as from the code before the loop and its back edge.
        const auto entries_in = std::count_if(block.preds.begin(), block.preds.end(),
                                              [&](std::uint32_t p) { return ir.blocks[p].entry != t3::none; });
        check(entries_in >= 1 && block.preds.size() >= 3, "a loop head is reached from an entry, the code before it, and its back edge");
        for (const auto id : block.insts) {
            const auto& in = ir.insts[id];
            if (in.op != t3::Op::Phi) break;
            std::set<std::uint32_t> loads;
            for (std::uint32_t i = 0; i < in.count; ++i) {
                if (ir.insts[ir.extras[in.extra + i]].op == t3::Op::EntryLoad) loads.insert(ir.extras[in.extra + i]);
            }
            phi_over_entries = phi_over_entries || loads.size() >= 2;
        }
    }
    check(heads >= 1, "the loop head has phis");
    check(phi_over_entries, "a value the loop reads joins the loads of two entries in a phi");
}

NEURA_TEST(tier3, frame_states_are_the_block_entry_for_checks_and_the_commit_for_helpers_and_calls) {
    const auto all = everything(environment);
    std::size_t checked = 0;
    std::size_t helpers = 0;
    std::size_t calls = 0;
    std::size_t inline_ops = 0;
    for (const auto& corpus : all.corpora) {
        for (const auto& item : corpus.built) {
            if (!item.result.program) continue;
            const auto& ir = item.result.program->ir;
            for (const auto& block : ir.blocks) {
                for (std::size_t k = 0; k < block.insts.size(); ++k) {
                    const auto& in = ir.insts[block.insts[k]];
                    if (in.state == t3::none) continue;
                    const auto& state = ir.states[in.state];
                    check_equal(state.slots.size(), ir.slot_count, "a state has a slot per frame slot");
                    check_equal(state.flags.size(), ir.slot_count, "a state has a flag per frame slot");
                    check_equal(state.block, in.origin_block, "a state names the block of its op");
                    const auto& count = ir.insts[state.blocks];
                    const bool commit_state = in.op == t3::Op::Helper || in.op == t3::Op::Call || in.op == t3::Op::WriteBack;
                    if (commit_state) {
                        // After the block's commit: the block's step is in the count.
                        check(count.op == t3::Op::CounterAdd && count.origin_block == in.origin_block,
                              "a helper's and a call's state counts the block's own step");
                        if (in.op != t3::Op::WriteBack) check_equal(state.blocks, in.operands[0], "the call's count is the state's");
                        if (in.op == t3::Op::Helper || in.op == t3::Op::Call) {
                            check(k > 0 && ir.insts[block.insts[k - 1]].op == t3::Op::WriteBack &&
                                      ir.insts[block.insts[k - 1]].state == in.state,
                                  "a write-back of the same state comes right before a helper and a call");
                        }
                        helpers += in.op == t3::Op::Helper;
                        calls += in.op == t3::Op::Call;
                        continue;
                    }
                    // At the entry of the block: the count and every slot are
                    // those at its start, never a value the block computes.
                    check(count.op != t3::Op::CounterAdd || count.origin_block != in.origin_block,
                          "a check's state counts the blocks before its block");
                    for (const auto& slot : state.slots) {
                        if (slot.kind != t3::StateSlot::Kind::Value) continue;
                        const auto& value = ir.insts[slot.value];
                        check(value.op == t3::Op::Phi || value.op == t3::Op::EntryLoad ||
                                  value.origin_block != in.origin_block,
                              "a check's state names no value of its own block");
                    }
                    if (in.op == t3::Op::Lift) ++checked;
                    if (in.op == t3::Op::StateCall && (in.flags & t3::CheckReference) == 0) ++inline_ops;
                }
            }
        }
    }
    check(checked > 5 && helpers > 2 && calls > 2 && inline_ops > 2,
          "lifted checks, helpers, calls and inline state operations were all examined");
}

NEURA_TEST(tier3, a_check_in_a_block_that_assigns_a_slot_names_the_slot_before_the_assignment) {
    // `total = total + i` in a loop: the checked addition's state names the
    // loop's value of `total`, not the sum the block stores.
    const auto all = everything(environment);
    const auto found = find_ir(all, [](const t3::Function& ir) { return has_loop_head(ir) && has_checked_lift(ir); });
    check(found.ir != nullptr, "a loop with a checked operation got IR");
    const auto& ir = *found.ir;
    std::size_t shown = 0;
    for (const auto& in : ir.insts) {
        if (in.op != t3::Op::Lift || (in.flags & t3::Checked) == 0) continue;
        const auto id = static_cast<std::uint32_t>(&in - ir.insts.data());
        for (const auto& slot : ir.states[in.state].slots) {
            if (slot.kind == t3::StateSlot::Kind::Value) check(slot.value != id, "no state names the check's own result");
        }
        ++shown;
    }
    check(shown > 0, "checked operations were examined");
}

// --- The verifier ---------------------------------------------------------------

NEURA_TEST(tier3, the_verifier_rejects_a_crafted_violation_of_each_rule) {
    const auto all = everything(environment);
    // V1: an operand read before it is defined.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_checked_lift(ir); });
        check(found.ir != nullptr, "an IR with a checked operation");
        expect_rejected(*found.image, *found.ir, "V1", "V1", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
            ir.insts[at.id].operands[0] = at.id;
        });
    }
    // V2: a pointer where an integer is expected.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_checked_lift(ir); });
        expect_rejected(*found.image, *found.ir, "V2", "V2", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
            ir.insts[ir.insts[at.id].operands[0]].type = {t3::Type::Ptr, 0};
        });
    }
    // V3: a frame state with one slot too few.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_checked_lift(ir); });
        expect_rejected(*found.image, *found.ir, "V3", "V3", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
            ir.states[ir.insts[at.id].state].slots.pop_back();
        });
        // ... and a check with no frame state at all.
        expect_rejected(*found.image, *found.ir, "V3", "V3 without a state", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
            ir.insts[at.id].state = t3::none;
        });
    }
    // V4: a write-back after the helper it belongs before.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_op(ir, t3::Op::Helper); });
        check(found.ir != nullptr, "an IR with a helper");
        expect_rejected(*found.image, *found.ir, "V4", "V4", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Helper; });
            auto& insts = ir.blocks[at.block].insts;
            std::swap(insts[at.index], insts[at.index - 1]);
        });
    }
    // V5: a check after the inline state operation of its block.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_state_call(ir); });
        check(found.ir != nullptr, "an IR with an inline state operation");
        expect_rejected(*found.image, *found.ir, "V5", "V5", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) {
                return in.op == t3::Op::StateCall && (in.flags & t3::CheckReference) == 0;
            });
            const auto call = ir.insts[at.id];
            const auto flag = insert(ir, at.block, at.index, constant_inst(1, 1, call.origin_block, call.origin_function));
            t3::Inst guard;
            guard.op = t3::Op::Guard;
            guard.operands[0] = flag;
            guard.flags = t3::Checked;
            guard.state = call.state;
            guard.origin_block = call.origin_block;
            guard.origin_function = call.origin_function;
            guard.origin_seq = call.origin_seq + 1;
            insert(ir, at.block, at.index + 2, std::move(guard));
        });
    }
    // V6: a pointer as an argument of a helper.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) {
            return std::any_of(ir.insts.begin(), ir.insts.end(),
                               [](const t3::Inst& in) { return (in.op == t3::Op::Helper || in.op == t3::Op::Call) && in.count > 0; });
        });
        check(found.ir != nullptr, "an IR with a helper that takes arguments");
        expect_rejected(*found.image, *found.ir, "V6", "V6", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) {
                return (in.op == t3::Op::Helper || in.op == t3::Op::Call) && in.count > 0;
            });
            ir.insts[ir.extras[ir.insts[at.id].extra]].type = {t3::Type::Ptr, 0};
        });
    }
    // V7: a reference live across a call that no frame holds.
    {
        bool done = false;
        for (const auto& corpus : all.corpora) {
            for (const auto& item : corpus.built) {
                if (done || !item.result.program) continue;
                const auto& ir = item.result.program->ir;
                for (std::uint32_t id = 0; id < ir.insts.size() && !done; ++id) {
                    const auto& in = ir.insts[id];
                    if (in.op != t3::Op::Call && in.op != t3::Op::Helper) continue;
                    auto copy = ir;
                    for (auto& slot : copy.states[in.state].slots) slot = {};
                    if (!reports_rule(t3::verify(corpus.image, copy), "V7")) continue;
                    expect_rejected(corpus.image, ir, "V7", "V7", [&](t3::Function& broken) {
                        for (auto& slot : broken.states[in.state].slots) slot = {};
                    });
                    done = true;
                }
            }
        }
        check(done, "a reference is live across some call, so forgetting its slot is rejected");
    }
    // V8: a Budget one block short, and a cycle without one.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_loop_head(ir); });
        expect_rejected(*found.image, *found.ir, "V8", "V8 one block short", [](t3::Function& ir) {
            for (auto& in : ir.insts) {
                if (in.op == t3::Op::Budget && in.imm > 0) {
                    --in.imm;
                    return;
                }
            }
        });
        expect_rejected(*found.image, *found.ir, "V8", "V8 a loop without a Budget", [](t3::Function& ir) {
            for (const auto& block : ir.blocks) {
                if (block.insts.empty() || ir.insts[block.insts.front()].op != t3::Op::Phi) continue;
                for (const auto id : block.insts) {
                    if (ir.insts[id].op == t3::Op::Budget) {
                        ir.insts[id].op = t3::Op::CounterAdd;
                        ir.insts[id].imm = 0;
                        ir.insts[id].type = {t3::Type::Int, 64};
                        return;
                    }
                }
            }
        });
    }
    // V10: a check removed for a guard that does not dominate it, and the
    // same guard placed before the check, which does.
    {
        const auto found = find_ir(all, [](const t3::Function& ir) { return has_loop_head(ir) && has_checked_lift(ir); });
        check(found.ir != nullptr, "a loop with a checked operation");
        expect_rejected(*found.image, *found.ir, "V10", "V10", [](t3::Function& ir) {
            const auto at = locate(ir, [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
            const auto lift = ir.insts[at.id];
            // A guard in an entry block, which does not dominate the loop.
            std::uint32_t entry_block = t3::none;
            for (std::uint32_t b = 0; b < ir.blocks.size(); ++b) {
                if (ir.blocks[b].entry != t3::none) entry_block = b;
            }
            const auto flag = insert(ir, entry_block, 0, constant_inst(1, 1, lift.origin_block, lift.origin_function));
            t3::Inst guard;
            guard.op = t3::Op::Guard;
            guard.operands[0] = flag;
            guard.flags = t3::Checked;
            guard.state = lift.state;
            guard.origin_block = lift.origin_block;
            guard.origin_function = lift.origin_function;
            const auto guard_id = insert(ir, entry_block, 1, std::move(guard));
            ir.insts[at.id].guard = guard_id;
        });
        // The guard right before the check dominates it: accepted.
        auto copy = *found.ir;
        const auto at = locate(copy, [](const t3::Inst& in) { return in.op == t3::Op::Lift && (in.flags & t3::Checked) != 0; });
        const auto lift = copy.insts[at.id];
        const auto flag = insert(copy, at.block, at.index, constant_inst(1, 1, lift.origin_block, lift.origin_function));
        t3::Inst guard;
        guard.op = t3::Op::Guard;
        guard.operands[0] = flag;
        guard.flags = t3::Checked;
        guard.state = lift.state;
        guard.origin_block = lift.origin_block;
        guard.origin_function = lift.origin_function;
        guard.origin_seq = lift.origin_seq;
        const auto guard_id = insert(copy, at.block, at.index + 1, std::move(guard));
        copy.insts[at.id].guard = guard_id;
        const auto violations = t3::verify(*found.image, copy);
        check(!reports_rule(violations, "V10"), "a guard before its check is accepted by V10");
    }
}

// --- Materialization ------------------------------------------------------------

namespace {

// IR that deoptimizes at once with a chain of frame states: `levels` frames of
// the three-frames image, the outermost the physical frame.
struct Hand {
    t3::Program program;
    nvm::tier2::Region programs[3];
};

std::unique_ptr<Hand> three_level_deopt(unsigned levels) {
    auto hand = std::make_unique<Hand>();
    auto& ir = hand->program.ir;
    ir.function = 0;
    ir.slot_count = 3;
    for (auto& program : hand->programs) ir.programs.push_back(&program);
    const auto add = [&](t3::Inst in) {
        ir.insts.push_back(std::move(in));
        ir.blocks.front().insts.push_back(static_cast<std::uint32_t>(ir.insts.size() - 1));
        return static_cast<std::uint32_t>(ir.insts.size() - 1);
    };
    ir.blocks.emplace_back();
    ir.blocks.front().entry = 0;
    ir.entries.push_back({0, 0});
    const auto seven = add(constant_inst(7, 32, 0, 0));
    const auto eleven = add(constant_inst(11, 32, 0, 0));
    const auto one = add(constant_inst(1, 1, 0, 0));
    const auto count = add(constant_inst(3, 64, 0, 0));
    using Slot = t3::StateSlot;
    using Flag = t3::StateFlag;
    t3::FrameState physical;
    physical.function = 0;
    physical.block = 0;
    physical.program = 0;
    physical.slots = {{}, {Slot::Kind::Value, seven}, {}};
    physical.flags = {{}, {Flag::Kind::One, 0}, {}};
    physical.blocks = count;
    ir.states.push_back(physical);
    if (levels > 1) {
        t3::FrameState middle;
        middle.function = 1;
        middle.block = 0;
        middle.program = 1;
        middle.return_slot = 2;
        middle.return_block = 1;
        middle.slots = {{Slot::Kind::Value, seven}, {Slot::Kind::Const, 9}, {}};
        middle.flags = {{Flag::Kind::Value, one}, {Flag::Kind::One, 0}, {}};
        middle.caller = 0;
        middle.blocks = count;
        ir.states.push_back(middle);
        t3::FrameState inner;
        inner.function = 2;
        inner.block = 1;
        inner.program = 2;
        inner.return_slot = 2;
        inner.return_block = 1;
        inner.slots = {{Slot::Kind::Const, 7}, {Slot::Kind::Value, eleven}};
        inner.flags = {{Flag::Kind::One, 0}, {Flag::Kind::Value, one}};
        inner.caller = 1;
        inner.blocks = count;
        ir.states.push_back(inner);
    }
    t3::Inst deopt;
    deopt.op = t3::Op::Deopt;
    deopt.operands[0] = count;
    deopt.state = static_cast<std::uint32_t>(ir.states.size() - 1);
    add(std::move(deopt));
    return hand;
}

} // namespace

NEURA_TEST(tier3, materializing_a_state_of_two_inlined_frames_builds_the_frames_push_frame_builds) {
    const auto image = three_frames_image();
    // The reference: tier 0 stopped with three frames.
    auto reference = stop_after(image, 3);
    check(reference.event.kind == nvm::Event::Kind::Trapped, "the reference run stopped at its step limit");
    const auto expected = reference.execution->tier3_frames();
    check_equal(expected.size(), std::size_t{3}, "three frames at the reference point");
    // The run to materialize into: stopped at its first step, with the
    // physical frame only; the state describes the three frames.
    auto actual = stop_after(image, 0);
    check_equal(actual.execution->tier3_frames().size(), std::size_t{1}, "one physical frame");
    const auto hand = three_level_deopt(3);
    const auto ending = actual.execution->tier3_run(hand->program, 0);
    check_equal(ending.outcome, static_cast<unsigned>(t3::Outcome::Deopt), "the activation deoptimized");
    check_equal(ending.next_block, 1U, "tier 1 continues at the innermost frame's block");
    check_equal(ending.blocks, std::uint64_t{3}, "the blocks the state counts");
    const auto frames = actual.execution->tier3_frames();
    check_same_frames(frames, expected, "two inlined frames");
    check_equal(actual.execution->steps(), reference.execution->steps(), "the steps flushed at the ending");
    // Each inlined frame returns into the program of the frame below it.
    check(frames[0].called_from == nullptr, "the physical frame keeps its called_from");
    check(frames[1].called_from == &hand->programs[0], "the first inlined frame was called from the physical frame's program");
    check(frames[2].called_from == &hand->programs[1], "the second inlined frame was called from the first's program");
}

NEURA_TEST(tier3, materializing_the_physical_frame_writes_its_slots_flags_and_block) {
    const auto image = three_frames_image();
    auto stopped = stop_after(image, 0);
    const auto before = stopped.execution->tier3_frames();
    const auto hand = three_level_deopt(1);
    // Slot 2 gets a value but stays uninitialized (its flag is the constant
    // 0); slot 1 is initialized by the state.
    hand->program.ir.states[0].slots[2] = {t3::StateSlot::Kind::Const, 5};
    hand->program.ir.insts.push_back(constant_inst(0, 1, 0, 0));
    hand->program.ir.blocks.front().insts.insert(hand->program.ir.blocks.front().insts.begin(),
                                                 static_cast<std::uint32_t>(hand->program.ir.insts.size() - 1));
    hand->program.ir.states[0].flags[2] = {t3::StateFlag::Kind::Value,
                                           static_cast<std::uint32_t>(hand->program.ir.insts.size() - 1)};
    hand->program.ir.states[0].block = 1;
    stopped.execution->tier3_run(hand->program, 0);
    const auto after = stopped.execution->tier3_frames();
    check_equal(after.size(), std::size_t{1}, "no frame was pushed");
    check_equal(after[0].block, 1U, "the block");
    check_equal(after[0].slots[0], before[0].slots[0], "an unchanged slot is left alone");
    check_equal(after[0].initialized[0], before[0].initialized[0], "an unchanged flag is left alone");
    check_equal(after[0].slots[1], std::uint64_t{7}, "a value slot");
    check_equal(unsigned{after[0].initialized[1]}, 1U, "a flag set by the state");
    check_equal(after[0].slots[2], std::uint64_t{5}, "a constant slot");
    check_equal(unsigned{after[0].initialized[2]}, 0U, "a flag written from a value that is 0");
}

// --- Native programs against tier 0 ----------------------------------------------

NEURA_TEST(tier3, native_programs_match_tier_0_at_every_step_and_frame_limit) {
    if (!programs_run()) return;
    // The calls, dispatch, closure and trap cases of the function-program
    // tests, run as native programs from their first checkpoint.
    const auto image = build_program({program_in(environment, "tier", "program-calls")});
    for (const auto& arguments : std::vector<std::vector<std::string>>{{"calls", "2"}, {"overflow", "3"}, {"index", "3"},
                                                                       {"nil", "3"}, {"field", "3"}}) {
        nvm::TierCache cache(image, native_options());
        const auto label = arguments[0];
        compare_with_tier_0(image, cache, label, arguments);
        const auto steps = run_captured(image, limits_at(0), std::nullopt, arguments).result.steps;
        const bool whole = label == "calls";
        for (std::uint64_t limit = whole ? 1 : (steps > 40 ? steps - 40 : 1); limit <= steps + 1; ++limit) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(limit) + " steps", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_steps = limit; });
        }
        for (std::uint32_t frames = 1; frames <= (whole ? 16U : 6U); ++frames) {
            compare_with_tier_0(image, cache, label + " with " + std::to_string(frames) + " frames", arguments,
                                [&](nvm::RunLimits& limits) { limits.max_frames = frames; });
        }
        const auto statistics = cache.statistics();
        check(statistics.native_programs > 0, label + ": function programs became native programs");
        check(statistics.native.steps > 0 && statistics.native.entered > 0, label + ": steps ran in native programs");
        check(statistics.region_counts.calls > 0 && statistics.region_counts.returns > 0,
              label + ": calls and returns ran in native programs");
        if (whole) check(statistics.native.traps > 0, "a frame limit trapped in place at a call in native code");
    }
}

NEURA_TEST(tier3, native_helpers_match_tier_0_at_every_step_and_heap_limit) {
    if (!programs_run()) return;
    const auto image = build_program({program_in(environment, "tier", "program-helpers")});
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"alloc", "5"}, {"kernel", "3"}, {"text", "3"}, {"utf8", "3"}, {"free", "3"}, {"remove", "3"}}) {
        nvm::TierCache cache(image, native_options());
        const auto label = arguments[0];
        compare_with_tier_0(image, cache, label, arguments);
        const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments).result;
        compare_sweep(image, cache, label + " steps", arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_steps = value; }, 1,
                      reference.steps + 1);
        compare_sweep(image, cache, label + " frames", arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) {
                          limits.max_frames = static_cast<std::uint32_t>(value);
                      },
                      1, 4);
        if (label == "alloc") {
            compare_sweep(image, cache, label + " heap", arguments,
                          [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_heap_cells = value; }, 1,
                          reference.peak_heap_cells + 2);
            compare_sweep(image, cache, label + " handles", arguments,
                          [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_handles = value; }, 1, 30);
        }
        const auto statistics = cache.statistics();
        check(statistics.region_counts.helpers > 0, label + ": helpers ran in native programs");
        check(statistics.native.steps > 0, label + ": steps ran in native programs");
    }
}

NEURA_TEST(tier3, native_loops_checks_and_traps_match_tier_0) {
    if (!programs_run()) return;
    const auto image = build_program({program_in(environment, "tier3", "shapes")});
    for (const auto& arguments :
         std::vector<std::vector<std::string>>{{"sum", "9"}, {"make", "6"}, {"descend", "7"}, {"grow", "25"}}) {
        nvm::TierCache cache(image, native_options());
        const auto label = arguments[0];
        compare_with_tier_0(image, cache, label, arguments);
        const auto reference = run_captured(image, limits_at(0), std::nullopt, arguments).result;
        compare_sweep(image, cache, label + " steps", arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) { limits.max_steps = value; }, 1,
                      reference.steps + 1);
        compare_sweep(image, cache, label + " frames", arguments,
                      [](nvm::RunLimits& limits, std::uint64_t value) {
                          limits.max_frames = static_cast<std::uint32_t>(value);
                      },
                      1, 8);
        check(cache.statistics().native.steps > 0, label + ": steps ran in native programs");
    }
    // The overflow in `grow` is a deoptimization of a checked operation.
    nvm::TierCache cache(image, native_options());
    compare_with_tier_0(image, cache, "overflow", {"grow", "25"});
    check(cache.statistics().native.checks > 0, "a failed check deoptimized native code");
}

NEURA_TEST(tier3, an_interpreted_loop_is_handed_to_native_code_when_its_count_reaches_the_threshold) {
    if (!programs_run()) return;
    const auto image = build_program({program_in(environment, "tier3", "shapes")});
    // The loop runs 4000 iterations; its program is native after 1000
    // arrivals at its checkpoints, in the middle of the loop.
    nvm::TierCache cache(image, native_options(1000));
    compare_with_tier_0(image, cache, "a loop longer than the threshold", {"sum", "4000"});
    const auto statistics = cache.statistics();
    check(statistics.native_programs > 0, "the program became native");
    check(statistics.native.entered > 0 && statistics.native.steps > 0, "native code ran the rest of the loop");
    // NEURA_TIER_EAGER=1 makes every program native at its first checkpoint.
    if (nvm::tier_eager()) return;
    check(statistics.region_counts.program_steps > statistics.native.steps,
          "the program interpreter ran the first iterations");
    check(statistics.region_counts.leaves > 0, "the interpreter left at a checkpoint for native code");
}

NEURA_TEST(tier3, native_calls_nest_to_a_fixed_depth_and_leave_past_it) {
    if (!programs_run()) return;
    const auto image = build_program({program_in(environment, "tier3", "shapes")});
    // 100 levels of calls nest in one native activation chain.
    nvm::TierCache shallow(image, native_options());
    compare_with_tier_0(image, shallow, "recursion within the native depth", {"descend", "100"});
    // 700 levels cannot nest: native code leaves at the callee past 256, the
    // dispatcher enters native code again, and the returns that unwind the
    // frames leave at their caller's resume block, one dispatcher entry each.
    nvm::TierCache deep(image, native_options());
    compare_with_tier_0(image, deep, "recursion past the native depth", {"descend", "700"});
    const auto entered_shallow = shallow.statistics().native.entered;
    const auto entered_deep = deep.statistics().native.entered;
    check(entered_deep >= entered_shallow + 3, "the dispatcher entered native code again after each depth limit: " +
                                                  std::to_string(entered_deep) + " entries against " +
                                                  std::to_string(entered_shallow));
    compare_with_tier_0(image, deep, "recursion with a frame limit inside the nest", {"descend", "300"},
                        [](nvm::RunLimits& limits) { limits.max_frames = 280; });
}

NEURA_TEST(tier3, tier_3_is_off_unless_asked_for_and_says_so_when_on) {
    const auto image = build_program({program_in(environment, "tier3", "shapes")});
    for (const auto mode : {nvm::TierOptions::Tier3::Off, nvm::TierOptions::Tier3::Interp}) {
        auto options = native_options();
        options.tier3 = mode;
        nvm::TierCache cache(image, options);
        run_captured(image, limits_at(2, &cache), std::nullopt, {"make", "6"});
        const auto statistics = cache.statistics();
        const auto text = nvm::describe(statistics);
        if (mode == nvm::TierOptions::Tier3::Off) {
            check(statistics.native_mode.empty() && statistics.native.entered == 0 && statistics.native_programs == 0,
                  "tier 3 off leaves no native statistics");
            check(text.find("native program") == std::string::npos, "the statistics say nothing of tier 3 when it is off");
        } else if (programs_run()) {
            check(text.find("tier: native programs ") != std::string::npos, "the statistics have the native line when tier 3 is on");
            check(text.find("steps in native code") != std::string::npos, "the line ends with the steps in native code");
        }
    }
}

namespace {

// A run under the shadow oracle with native programs: no difference, and
// the same run as without the oracle. Returns the oracle's sync points.
std::uint64_t check_clean(const nvr::Image& image, const std::string& label, const std::vector<std::string>& arguments) {
    nvm::TierCache plain_cache(image, native_options());
    const auto plain = run_captured(image, limits_at(2, &plain_cache), std::nullopt, arguments);
    nvm::TierCache cache(image, native_options());
    auto limits = limits_at(2, &cache);
    Hooks hooks;
    limits.tier_shadow = &hooks.hooks;
    const auto checked = run_captured(image, limits, std::nullopt, arguments);
    check(hooks.reports.empty(), label + ": no difference" + (hooks.reports.empty() ? "" : ": " + hooks.reports[0]));
    check(hooks.notices.empty(), label + ": no notice");
    check_same(checked, plain, label + " with the oracle");
    if (programs_run()) {
        check(hooks.hooks.syncs > 0, label + ": the oracle reached sync points");
        check(cache.statistics().native.steps > 0, label + ": steps ran in native programs under the oracle");
    }
    return hooks.hooks.syncs;
}

} // namespace

NEURA_TEST(tier3, the_shadow_oracle_checks_native_programs_and_finds_a_planted_difference) {
    const auto calls = build_program({program_in(environment, "tier", "program-calls")});
    std::uint64_t syncs = check_clean(calls, "calls", {"calls", "4"});
    for (const auto* kind : {"overflow", "index", "nil", "field"}) syncs += check_clean(calls, kind, {kind, "4"});
    const auto shapes = build_program({program_in(environment, "tier3", "shapes")});
    for (const auto& arguments : shapes_runs) syncs += check_clean(shapes, arguments[0], arguments);
    const auto helpers = build_program({program_in(environment, "tier", "program-helpers")});
    for (const auto* kind : {"alloc", "kernel", "text", "free"}) syncs += check_clean(helpers, kind, {kind, "6"});
    check(syncs > 0, "sync points");
    if (!programs_run()) return;
    nvm::TierCache cache(calls, native_options());
    auto limits = limits_at(2, &cache);
    Hooks hooks;
    hooks.hooks.perturb = Perturb::SlotValue;
    hooks.hooks.at_step = run_captured(calls, limits_at(0), std::nullopt, {"calls", "4"}).result.steps / 3;
    limits.tier_shadow = &hooks.hooks;
    run_captured(calls, limits, std::nullopt, {"calls", "4"});
    check(hooks.hooks.perturbed, "the run was perturbed");
    check_equal(hooks.reports.size(), std::size_t{1}, "one report");
}

} // namespace neura::testing
