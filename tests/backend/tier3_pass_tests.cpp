// Tier 3's passes (src/nvm/tier3_passes.cpp, tier3_range.cpp, tier3_licm.cpp):
// per pass, the IR before and after on functions built by hand, including
// the cases where the rewrite must leave the operation in place; and the
// passes at work on programs, compared with tier 0 and with the passes off at
// every step limit. The `tier3` suite (backend-tier3).
#include "graph_test_support.hpp"

#include "nvm/tier1.hpp"
#include "nvm/tier2.hpp"
#include "nvm/tier3.hpp"
#include "nvm/tier3_passes.hpp"

#include <algorithm>
#include <cfenv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <set>

namespace neura::testing {
namespace {

namespace t3 = nvm::tier3;
namespace d3 = nvm::tier3::detail;
using lift::Opcode;

// A function built by hand, instruction by instruction, with the frame
// states the verifier wants. Every slot has the same integer width.
class Ir {
public:
    explicit Ir(unsigned slots = 2, unsigned width = 32) : slots_(slots), width_(width) {
        image.types = {{nvr::TypeKind::Void}, {nvr::TypeKind::Signed, 32}};
        nvr::Function function;
        function.frame_types.assign(slots, 1);
        image.functions.push_back(std::move(function));
        f.function = 0;
        f.slot_count = slots;
        f.slot_types.assign(slots, {t3::Type::Int, static_cast<std::uint8_t>(width)});
        f.programs.push_back(&region);
        f.region = &region;
    }
    Ir(const Ir&) = delete;
    Ir& operator=(const Ir&) = delete;

    std::uint32_t block(std::uint32_t origin, bool start = true) {
        t3::Block b;
        b.origin_block = origin;
        b.origin_function = 0;
        b.image_start = start;
        f.blocks.push_back(b);
        return static_cast<std::uint32_t>(f.blocks.size() - 1);
    }

    std::uint32_t add(std::uint32_t b, t3::Inst in) {
        in.origin_block = f.blocks[b].origin_block;
        in.origin_function = 0;
        in.origin_seq = seq_++;
        const auto id = static_cast<std::uint32_t>(f.insts.size());
        f.insts.push_back(std::move(in));
        f.blocks[b].insts.push_back(id);
        return id;
    }

    std::uint32_t konst(std::uint32_t b, std::uint64_t bits, unsigned width = 0) {
        t3::Inst in;
        in.op = t3::Op::Const;
        in.type = {t3::Type::Int, static_cast<std::uint8_t>(width != 0 ? width : width_)};
        in.imm = bits;
        return add(b, in);
    }

    std::uint32_t lifted(std::uint32_t b, lift::Instruction li, std::uint32_t x, std::uint32_t y, unsigned result_width,
                         bool checked = false, std::uint32_t state = t3::none) {
        t3::Inst in;
        in.op = t3::Op::Lift;
        in.type = {t3::Type::Int, static_cast<std::uint8_t>(result_width)};
        in.lifted = li;
        in.operands[0] = x;
        in.operands[1] = y;
        in.flags = checked ? t3::Checked : 0;
        in.state = state;
        return add(b, in);
    }

    // A lifted op of the slot width unless `width` says otherwise.
    std::uint32_t lift(std::uint32_t b, Opcode op, bool is_signed, std::uint32_t x, std::uint32_t y = t3::none,
                       bool checked = false, std::uint32_t state = t3::none, unsigned width = 0, unsigned result_width = 0) {
        lift::Instruction li;
        li.op = op;
        li.width = static_cast<std::uint8_t>(width != 0 ? width : width_);
        li.is_signed = is_signed;
        const bool compare = op == Opcode::Equal || op == Opcode::NotEqual || op == Opcode::Less ||
                             op == Opcode::LessEqual || op == Opcode::Greater || op == Opcode::GreaterEqual;
        return lifted(b, li, x, y, result_width != 0 ? result_width : (compare ? 1 : li.width), checked, state);
    }

    std::uint32_t convert(std::uint32_t b, unsigned from, bool from_signed, unsigned to, bool to_signed, std::uint32_t x,
                          bool checked, std::uint32_t state) {
        lift::Instruction li;
        li.op = Opcode::ConvertInteger;
        li.width = static_cast<std::uint8_t>(from);
        li.is_signed = from_signed;
        li.to_width = static_cast<std::uint8_t>(to);
        li.to_signed = to_signed;
        return lifted(b, li, x, t3::none, to, checked, state);
    }

    std::uint32_t phi(std::uint32_t b, unsigned width, std::uint32_t count) {
        t3::Inst in;
        in.op = t3::Op::Phi;
        in.type = {t3::Type::Int, static_cast<std::uint8_t>(width)};
        in.extra = static_cast<std::uint32_t>(f.extras.size());
        in.count = count;
        f.extras.resize(f.extras.size() + count, 0);
        return add(b, in);
    }

    void set_phi(std::uint32_t phi_id, std::uint32_t k, std::uint32_t value) { f.extras[f.insts[phi_id].extra + k] = value; }

    std::uint32_t mask(std::uint32_t b, std::uint32_t x, unsigned width) {
        t3::Inst in;
        in.op = t3::Op::Mask;
        in.type = {t3::Type::Int, static_cast<std::uint8_t>(width)};
        in.operands[0] = x;
        in.imm = width;
        return add(b, in);
    }

    std::uint32_t counter_add(std::uint32_t b, std::uint32_t c, std::uint64_t amount) {
        t3::Inst in;
        in.op = t3::Op::CounterAdd;
        in.type = {t3::Type::Int, 64};
        in.operands[0] = c;
        in.imm = amount;
        return add(b, in);
    }

    void link(std::uint32_t from, std::uint32_t to) {
        f.blocks[from].succs.push_back(to);
        f.blocks[to].preds.push_back(from);
    }

    std::uint32_t jump(std::uint32_t b, std::uint32_t to) {
        t3::Inst in;
        in.op = t3::Op::Jump;
        const auto id = add(b, in);
        link(b, to);
        return id;
    }

    std::uint32_t branch(std::uint32_t b, std::uint32_t cond, std::uint32_t then_block, std::uint32_t else_block) {
        t3::Inst in;
        in.op = t3::Op::Branch;
        in.operands[0] = cond;
        const auto id = add(b, in);
        link(b, then_block);
        link(b, else_block);
        return id;
    }

    std::uint32_t deopt(std::uint32_t b, std::uint32_t c, std::uint32_t state) {
        t3::Inst in;
        in.op = t3::Op::Deopt;
        in.operands[0] = c;
        in.flags = t3::Effect;
        in.state = state;
        return add(b, in);
    }

    std::uint32_t budget(std::uint32_t b, std::uint32_t c, std::uint32_t state) {
        t3::Inst in;
        in.op = t3::Op::Budget;
        in.operands[0] = c;
        in.flags = t3::Checked;
        in.state = state;
        return add(b, in);
    }

    // An effect that is not a check: a write-back of `state`.
    std::uint32_t effect(std::uint32_t b, t3::Op op, std::uint32_t state, std::uint32_t c = t3::none) {
        t3::Inst in;
        in.op = op;
        in.operands[0] = c;
        in.flags = t3::Effect;
        in.state = state;
        if (op == t3::Op::Helper || op == t3::Op::Call) {
            in.type = {t3::Type::Int, static_cast<std::uint8_t>(width_)};
            in.aux = static_cast<std::uint32_t>(f.exits.size());
            exit_storage_.emplace_back();
            f.exits.push_back(&exit_storage_.back());
        }
        return add(b, in);
    }

    // The state at the entry of image block `origin`, with the counter `c`
    // and the given (slot, value) pairs.
    std::uint32_t state(std::uint32_t origin, std::uint32_t c,
                        const std::vector<std::pair<unsigned, std::uint32_t>>& values = {}) {
        t3::FrameState s;
        s.function = 0;
        s.block = origin;
        s.program = 0;
        s.slots.resize(slots_);
        s.flags.resize(slots_);
        s.blocks = c;
        for (const auto& [slot, v] : values) s.slots[slot] = {t3::StateSlot::Kind::Value, v};
        f.states.push_back(std::move(s));
        return static_cast<std::uint32_t>(f.states.size() - 1);
    }

    // An entry block of the activation at image block `origin`: the counter
    // and every slot loaded, one edge to `target`.
    struct Entry {
        std::uint32_t block = 0;
        std::uint32_t counter = 0;
        std::vector<std::uint32_t> slot;
    };

    Entry entry(std::uint32_t origin, std::uint32_t target) {
        Entry e = entry_without_edge(origin);
        jump(e.block, target);
        return e;
    }

    // The same, with no edge yet (the caller links it).
    Entry entry_without_edge(std::uint32_t origin) {
        Entry e;
        e.block = block(origin, false);
        f.blocks[e.block].entry = static_cast<std::uint32_t>(f.entries.size());
        f.entries.push_back({origin, e.block});
        t3::Inst c;
        c.op = t3::Op::EntryCounter;
        c.type = {t3::Type::Int, 64};
        e.counter = add(e.block, c);
        for (unsigned s = 0; s < slots_; ++s) {
            t3::Inst load;
            load.op = t3::Op::EntryLoad;
            load.type = {t3::Type::Int, static_cast<std::uint8_t>(width_)};
            load.imm = s;
            e.slot.push_back(add(e.block, load));
        }
        return e;
    }

    void finish() { d3::compute_budgets(f); }

    std::vector<std::string> violations() const { return t3::verify(image, f); }

    void expect_valid(const std::string& label) const {
        const auto v = violations();
        check(v.empty(), label + ": the IR verifies" + (v.empty() ? "" : ": " + v.front() + "\n" + t3::describe(f)));
    }

    // The block holding value `id`, or none.
    std::uint32_t block_of(std::uint32_t id) const {
        for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
            if (std::find(f.blocks[b].insts.begin(), f.blocks[b].insts.end(), id) != f.blocks[b].insts.end()) return b;
        }
        return t3::none;
    }

    bool checked(std::uint32_t id) const { return (f.insts[id].flags & t3::Checked) != 0; }
    bool present(std::uint32_t id) const { return block_of(id) != t3::none; }
    std::uint64_t count_of(const std::string& name) const {
        const auto found = counts.find(name);
        return found == counts.end() ? 0 : found->second;
    }
    std::vector<std::uint32_t> all_of(t3::Op op) const {
        std::vector<std::uint32_t> ids;
        for (const auto& block : f.blocks) {
            for (const auto id : block.insts) {
                if (f.insts[id].op == op) ids.push_back(id);
            }
        }
        return ids;
    }

    nvr::Image image;
    nvm::tier2::Region region;
    t3::Function f;
    d3::Counts counts;

private:
    unsigned slots_;
    unsigned width_;
    std::uint32_t seq_ = 0;
    std::deque<nvr::Exit> exit_storage_;
};

// A straight-line function: the entry, one block of image block 1 that the
// test fills, and a Deopt at its end.
struct Straight {
    Straight(unsigned slots, unsigned width) : ir(slots, width) {
        body = ir.block(1);
        entry = ir.entry(1, body);
        state = ir.state(1, entry.counter);
    }
    Ir ir;
    std::uint32_t body = 0;
    std::uint32_t state = 0;
    Ir::Entry entry;
};

void end_straight(Straight& s) {
    s.ir.deopt(s.body, s.entry.counter, s.state);
    s.ir.finish();
}

void run_elimination(Ir& ir) {
    d3::eliminate_checks(ir.image, ir.f, ir.counts);
    ir.expect_valid("after check elimination");
}

// A value in [0, mask] from `value`.
std::uint32_t masked(Ir& ir, std::uint32_t b, std::uint32_t value, std::uint64_t mask, unsigned width = 0) {
    const auto m = ir.konst(b, mask, width);
    return ir.lift(b, Opcode::BitAnd, false, value, m, false, t3::none, width);
}

// --- A counted loop ---------------------------------------------------------------------------

// Image blocks: 0 the code before the loop, 1 its head, 2 its body, 3 its
// exit. The loop variable i starts at the first slot (the function's entry
// loads it) or at 0; the loop runs while `i <test> n`, n being the second
// slot. The activation can be entered at the function's start (entry 0) and
// at the loop head (entry 1), as the interpreter hands a loop to native code.
struct Loop {
    explicit Loop(unsigned width) : ir(2, width) {}
    Ir ir;
    std::uint32_t pre = 0, head = 0, body = 0, exit = 0;
    Ir::Entry first, middle;
    std::uint32_t i = 0, n = 0, c = 0, compare = 0, next = 0, c_next = 0, index = 0, latch = 0;
    std::uint32_t head_state = 0, body_state = 0, exit_state = 0;
};

struct LoopOptions {
    Opcode test = Opcode::Less;
    bool start_at_zero = false;
    unsigned width = 32;
    bool is_signed = true;
    // Whether the head can be entered from the interpreter.
    bool middle_entry = true;
    // Whether the body can be entered from the interpreter too (a resume
    // block of the loop): the loop then has two entries.
    bool body_entry = false;
    // Whether the test compares i with n; when not, it compares i with i, so
    // that nothing bounds the variable.
    bool bound = true;
    // A constant defined in the head to compare i with, in place of n (0: none).
    std::uint64_t constant_bound = 0;
};

// `fill` adds the body's instructions (after its phis, which the loop made)
// and must set loop.next, the value i has in the next iteration.
std::unique_ptr<Loop> counted_loop(const LoopOptions& options, const std::function<void(Loop&)>& fill) {
    auto l = std::make_unique<Loop>(options.width);
    auto& ir = l->ir;
    l->pre = ir.block(0);
    l->head = ir.block(1);
    l->body = ir.block(2);
    l->exit = ir.block(3);
    l->first = ir.entry(0, l->pre);
    if (options.middle_entry) l->middle = ir.entry_without_edge(1);
    const auto pre_state = ir.state(0, l->first.counter);
    ir.budget(l->pre, l->first.counter, pre_state);
    const auto start = options.start_at_zero ? ir.konst(l->pre, 0, options.width) : l->first.slot[0];
    const auto c0 = ir.counter_add(l->pre, l->first.counter, 1);
    // The head's predecessors, in order: the code before the loop, the entry
    // at the head (when there is one), and the body.
    ir.jump(l->pre, l->head);
    if (options.middle_entry) ir.jump(l->middle.block, l->head);
    const unsigned preds = options.middle_entry ? 3 : 2;
    l->i = ir.phi(l->head, options.width, preds);
    l->n = ir.phi(l->head, options.width, preds);
    l->c = ir.phi(l->head, 64, preds);
    l->head_state = ir.state(1, l->c, {{0, l->i}, {1, l->n}});
    ir.budget(l->head, l->c, l->head_state);
    const auto bound_value = options.constant_bound != 0 ? ir.konst(l->head, options.constant_bound, options.width)
                                                          : (options.bound ? l->n : l->i);
    l->compare = ir.lift(l->head, options.test, options.is_signed, l->i, bound_value, false, t3::none, options.width);
    ir.branch(l->head, l->compare, l->body, l->exit);
    l->body_state = ir.state(2, l->c, {{0, l->i}, {1, l->n}});
    std::uint32_t body_counter = l->c;
    std::uint32_t body_n = l->n;
    Ir::Entry body_entry;
    std::uint32_t body_phi = 0;
    std::uint32_t body_n_phi = 0;
    if (options.body_entry) {
        body_entry = ir.entry_without_edge(2);
        body_phi = ir.phi(l->body, 64, 2);
        body_n_phi = ir.phi(l->body, options.width, 2);
        body_counter = body_phi;
        body_n = body_n_phi;
    }
    l->exit_state = ir.state(3, l->c, {{0, l->i}, {1, l->n}});
    l->latch = l->body;
    fill(*l);
    l->c_next = ir.counter_add(l->latch, body_counter, 1);
    if (options.body_entry) {
        ir.jump(body_entry.block, l->body);
        ir.set_phi(body_phi, 0, l->c);
        ir.set_phi(body_phi, 1, body_entry.counter);
        ir.set_phi(body_n_phi, 0, l->n);
        ir.set_phi(body_n_phi, 1, body_entry.slot[1]);
    }
    ir.jump(l->latch, l->head);
    ir.deopt(l->exit, l->c, l->exit_state);
    unsigned k = 0;
    ir.set_phi(l->i, k, start);
    ir.set_phi(l->n, k, l->first.slot[1]);
    ir.set_phi(l->c, k, c0);
    ++k;
    if (options.middle_entry) {
        ir.set_phi(l->i, k, l->middle.slot[0]);
        ir.set_phi(l->n, k, l->middle.slot[1]);
        ir.set_phi(l->c, k, l->middle.counter);
        ++k;
    }
    ir.set_phi(l->i, k, l->next);
    ir.set_phi(l->n, k, body_n);
    ir.set_phi(l->c, k, l->c_next);
    ir.finish();
    return l;
}

// i + 1, checked, in the loop body.
void increment(Loop& l, unsigned width, bool is_signed = true) {
    const auto one = l.ir.konst(l.body, 1, width);
    l.next = l.ir.lift(l.body, Opcode::Add, is_signed, l.i, one, true, l.body_state, width);
}

// --- Constant folding ------------------------------------------------------------------------------------

NEURA_TEST(tier3, folding_evaluates_lifted_ops_masks_and_gathers_on_constants_and_leaves_a_failing_one) {
    Straight s(2, 32);
    auto& ir = s.ir;
    const auto a = ir.konst(s.body, 100, 8);
    const auto b = ir.konst(s.body, 100, 8);
    const auto sum = ir.lift(s.body, Opcode::Add, false, a, b, true, s.state, 8);
    const auto again = ir.lift(s.body, Opcode::Add, false, sum, b, true, s.state, 8);
    const auto wide = ir.konst(s.body, 0x1ff, 9);
    const auto cut = ir.mask(s.body, wide, 8);
    end_straight(s);
    ir.expect_valid("before");
    d3::simplify(ir.f, ir.counts);
    ir.expect_valid("after folding");
    check(ir.f.insts[sum].op == t3::Op::Const && ir.f.insts[sum].imm == 200, "100 + 100 at 8 bits unsigned folds to 200");
    check(!ir.checked(sum), "the folded sum has no check");
    check(ir.f.insts[again].op == t3::Op::Lift && ir.checked(again), "200 + 100 overflows 8 bits: the op and its check stay");
    check_equal(ir.f.insts[again].state, s.state, "the failing op keeps its state");
    check(ir.f.insts[cut].op == t3::Op::Const && ir.f.insts[cut].imm == 0xff, "a mask of a constant is the masked constant");
    check(ir.count_of("fold.lift") >= 1 && ir.count_of("fold.mask") == 1, "folds are counted");
}

NEURA_TEST(tier3, a_failing_division_or_shift_on_constants_stays_and_a_valid_one_folds) {
    Straight s(2, 32);
    auto& ir = s.ir;
    const auto zero = ir.konst(s.body, 0);
    const auto ten = ir.konst(s.body, 10);
    const auto by_zero = ir.lift(s.body, Opcode::Divide, true, ten, zero, true, s.state);
    const auto by_two = ir.lift(s.body, Opcode::Divide, true, ten, ir.konst(s.body, 2), true, s.state);
    const auto wide_shift = ir.lift(s.body, Opcode::ShiftLeft, false, ten, ir.konst(s.body, 32), true, s.state);
    end_straight(s);
    d3::simplify(ir.f, ir.counts);
    ir.expect_valid("after folding");
    check(ir.f.insts[by_zero].op == t3::Op::Lift && ir.checked(by_zero), "10 / 0 stays");
    check(ir.f.insts[by_two].op == t3::Op::Const && ir.f.insts[by_two].imm == 5, "10 / 2 folds");
    check(ir.f.insts[wide_shift].op == t3::Op::Lift && ir.checked(wide_shift), "a shift by the width stays");
}

NEURA_TEST(tier3, a_float_op_on_constants_folds_in_the_default_environment_and_not_in_another) {
    const auto build = [](std::unique_ptr<Straight>& s, std::uint32_t& add_id) {
        s = std::make_unique<Straight>(2, 32);
        auto& ir = s->ir;
        const auto bits = [](double d) {
            std::uint64_t u;
            std::memcpy(&u, &d, sizeof u);
            return u;
        };
        t3::Inst a;
        a.op = t3::Op::Const;
        a.type = {t3::Type::F64, 64};
        a.imm = bits(1.0);
        const auto x = ir.add(s->body, a);
        a.imm = bits(2.0);
        const auto y = ir.add(s->body, a);
        lift::Instruction li;
        li.op = Opcode::FloatAdd;
        li.width = 64;
        li.format = lift::FloatType::Binary64;
        add_id = ir.lifted(s->body, li, x, y, 64);
        ir.f.insts[add_id].type = {t3::Type::F64, 64};
        end_straight(*s);
    };
    const auto before = std::fegetround();
    std::uint32_t id = 0;
    std::unique_ptr<Straight> s;
    build(s, id);
    const bool supported = lift::float_environment_is_default();
    d3::simplify(s->ir.f, s->ir.counts);
    if (supported) {
        check(s->ir.f.insts[id].op == t3::Op::Const, "1.0 + 2.0 folds in the default environment");
        double d;
        std::memcpy(&d, &s->ir.f.insts[id].imm, sizeof d);
        check(d == 3.0, "to 3.0");
    }
    build(s, id);
    std::fesetround(FE_UPWARD);
    const bool upward = !lift::float_environment_is_default();
    d3::simplify(s->ir.f, s->ir.counts);
    std::fesetround(before);
    check(std::fegetround() == before, "the rounding mode is restored");
    if (supported) check(upward, "rounding upward is not the default environment");
    check(s->ir.f.insts[id].op == t3::Op::Lift, "the same op is not folded when the rounding mode is not the default");
}

NEURA_TEST(tier3, a_branch_on_a_constant_becomes_a_jump_and_the_dead_edge_leaves_the_phis_and_preds) {
    Ir ir(2);
    const auto head = ir.block(1);
    const auto left = ir.block(2);
    const auto right = ir.block(3);
    const auto join = ir.block(4);
    const auto entry = ir.entry(1, head);
    const auto state = ir.state(1, entry.counter);
    const auto one = ir.konst(head, 1, 1);
    ir.branch(head, one, left, right);
    const auto l = ir.konst(left, 7);
    ir.jump(left, join);
    const auto r = ir.konst(right, 9);
    ir.jump(right, join);
    const auto merged = ir.phi(join, 32, 2);
    ir.set_phi(merged, 0, l);
    ir.set_phi(merged, 1, r);
    ir.deopt(join, entry.counter, state);
    ir.finish();
    ir.expect_valid("before");
    check_equal(ir.f.blocks.size(), std::size_t{5}, "blocks before");
    d3::simplify(ir.f, ir.counts);
    ir.expect_valid("after folding the branch");
    check_equal(ir.count_of("fold.branch"), std::uint64_t{1}, "one branch folded");
    check_equal(ir.f.blocks.size(), std::size_t{4}, "the unreachable block is deleted");
    for (const auto& block : ir.f.blocks) {
        if (block.origin_block == 4) check_equal(block.preds.size(), std::size_t{1}, "the join has one predecessor");
    }
    check(!ir.present(merged), "the phi of one value is replaced by it");
    check(ir.f.insts[ir.f.blocks[head].insts.back()].op == t3::Op::Jump, "the branch is a jump");
}

NEURA_TEST(tier3, a_switch_on_a_constant_takes_its_exit_and_an_index_past_the_exits_stays) {
    const auto build = [](std::uint64_t index) {
        auto ir = std::make_unique<Ir>(2);
        const auto head = ir->block(1);
        const auto a = ir->block(2);
        const auto b = ir->block(3);
        const auto entry = ir->entry(1, head);
        const auto state = ir->state(1, entry.counter);
        const auto k = ir->konst(head, index);
        t3::Inst check_in;
        check_in.op = t3::Op::SwitchCheck;
        check_in.operands[0] = k;
        check_in.imm = 2;
        check_in.flags = t3::Checked;
        check_in.state = state;
        ir->add(head, check_in);
        t3::Inst sw;
        sw.op = t3::Op::Switch;
        sw.operands[0] = k;
        sw.imm = 2;
        ir->add(head, sw);
        ir->link(head, a);
        ir->link(head, b);
        ir->deopt(a, entry.counter, state);
        ir->deopt(b, entry.counter, state);
        ir->finish();
        return ir;
    };
    auto in_range = build(1);
    d3::simplify(in_range->f, in_range->counts);
    in_range->expect_valid("after folding the switch");
    check_equal(in_range->count_of("fold.switch"), std::uint64_t{1}, "a switch on a constant is folded");
    check_equal(in_range->f.blocks.size(), std::size_t{3}, "one exit's block is gone");
    auto past = build(5);
    d3::simplify(past->f, past->counts);
    past->expect_valid("after leaving the switch past its exits");
    check_equal(past->count_of("fold.switch"), std::uint64_t{0}, "an index past the exits is left to its check");
    check_equal(past->f.blocks.size(), std::size_t{4}, "no block is removed");
}

NEURA_TEST(tier3, a_dispatch_on_a_constant_tag_takes_the_table_entry_or_its_default) {
    const auto build = [](std::uint64_t tag) {
        auto ir = std::make_unique<Ir>(2);
        ir->f.dispatch_tables.push_back({{{3, 0}, {8, 1}}, 2});
        const auto head = ir->block(1);
        const auto a = ir->block(2);
        const auto b = ir->block(3);
        const auto d = ir->block(4);
        const auto entry = ir->entry(1, head);
        const auto state = ir->state(1, entry.counter);
        const auto k = ir->konst(head, tag, 8);
        t3::Inst dispatch;
        dispatch.op = t3::Op::Dispatch;
        dispatch.operands[0] = k;
        dispatch.imm = 0;
        ir->add(head, dispatch);
        ir->link(head, a);
        ir->link(head, b);
        ir->link(head, d);
        for (const auto x : {a, b, d}) ir->deopt(x, entry.counter, state);
        ir->finish();
        return ir;
    };
    for (const auto& [tag, remaining_origin] : std::vector<std::pair<std::uint64_t, std::uint32_t>>{{3, 2}, {8, 3}, {99, 4}}) {
        auto ir = build(tag);
        ir->expect_valid("before");
        d3::simplify(ir->f, ir->counts);
        ir->expect_valid("after folding the dispatch");
        check_equal(ir->count_of("fold.dispatch"), std::uint64_t{1}, "folded");
        std::set<std::uint32_t> origins;
        for (const auto& block : ir->f.blocks) origins.insert(block.origin_block);
        check(origins.contains(remaining_origin), "the block the table selects is still there");
        check_equal(ir->f.blocks.size(), std::size_t{3}, "an entry, the dispatch block and the one exit");
    }
}

NEURA_TEST(tier3, counter_adds_merge_when_the_first_has_no_other_reader) {
    Straight s(2, 32);
    auto& ir = s.ir;
    const auto a = ir.counter_add(s.body, s.entry.counter, 1);
    const auto b = ir.counter_add(s.body, a, 2);
    // A second chain whose first sum a frame state reads.
    const auto c = ir.counter_add(s.body, s.entry.counter, 1);
    const auto d = ir.counter_add(s.body, c, 1);
    const auto reader = ir.state(1, c);
    ir.effect(s.body, t3::Op::WriteBack, reader);
    const auto tail = ir.counter_add(s.body, b, 1);
    ir.counter_add(s.body, d, 1);
    end_straight(s);
    ir.expect_valid("before");
    d3::simplify(ir.f, ir.counts);
    ir.expect_valid("after merging");
    check(!ir.present(a), "the first of two sums with one reader is merged away");
    check_equal(ir.f.insts[b].operands[0], s.entry.counter, "the second adds to the first's operand");
    check_equal(ir.f.insts[b].imm, std::uint64_t{3}, "by the sum of both");
    check(ir.present(c), "a sum a frame state reads stays");
    check_equal(ir.f.insts[d].operands[0], c, "and the sum after it is not merged into it");
    (void)tail;
}

NEURA_TEST(tier3, trivial_phis_and_phis_of_one_constant_are_replaced) {
    Ir ir(2);
    const auto head = ir.block(1);
    const auto left = ir.block(2);
    const auto right = ir.block(3);
    const auto join = ir.block(4);
    const auto entry = ir.entry(1, head);
    const auto state = ir.state(1, entry.counter);
    const auto cond = ir.lift(head, Opcode::Less, true, entry.slot[0], entry.slot[1]);
    ir.branch(head, cond, left, right);
    const auto k1 = ir.konst(left, 5);
    ir.jump(left, join);
    const auto k2 = ir.konst(right, 5);
    ir.jump(right, join);
    const auto same = ir.phi(join, 32, 2);
    ir.set_phi(same, 0, entry.slot[0]);
    ir.set_phi(same, 1, entry.slot[0]);
    const auto five = ir.phi(join, 32, 2);
    ir.set_phi(five, 0, k1);
    ir.set_phi(five, 1, k2);
    const auto use = ir.lift(join, Opcode::AddWrapping, false, same, five);
    ir.deopt(join, entry.counter, state);
    ir.finish();
    ir.expect_valid("before");
    d3::simplify(ir.f, ir.counts);
    ir.expect_valid("after");
    check_equal(ir.count_of("copy.phi"), std::uint64_t{1}, "the phi of one value");
    check_equal(ir.count_of("copy.phi_const"), std::uint64_t{1}, "the phi of one constant");
    check_equal(ir.f.insts[use].operands[0], entry.slot[0], "its readers read the value");
    check(ir.f.insts[ir.f.insts[use].operands[1]].op == t3::Op::Const && ir.f.insts[ir.f.insts[use].operands[1]].imm == 5,
          "and a constant 5");
}

// --- Value numbering -----------------------------------------------------------------------------

NEURA_TEST(tier3, value_numbering_merges_equal_ops_and_checks_that_a_dominating_one_covers) {
    Ir ir(2);
    const auto first = ir.block(1);
    const auto second = ir.block(2);
    const auto entry = ir.entry(1, first);
    const auto state = ir.state(1, entry.counter);
    const auto a = ir.lift(first, Opcode::Add, true, entry.slot[0], entry.slot[1], true, state);
    const auto pure_first = ir.lift(first, Opcode::BitXor, false, entry.slot[0], entry.slot[1]);
    ir.jump(first, second);
    const auto b = ir.lift(second, Opcode::Add, true, entry.slot[0], entry.slot[1], true, state);
    const auto pure_second = ir.lift(second, Opcode::BitXor, false, entry.slot[0], entry.slot[1]);
    const auto other = ir.lift(second, Opcode::Add, false, entry.slot[0], entry.slot[1], true, state);
    const auto use = ir.lift(second, Opcode::AddWrapping, false, b, pure_second);
    ir.deopt(second, entry.counter, state);
    ir.finish();
    ir.expect_valid("before");
    d3::number_values(ir.f, ir.counts);
    ir.expect_valid("after");
    check(!ir.present(b) && ir.present(a), "the dominated equal checked add goes, its dominator stays");
    check(!ir.present(pure_second) && ir.present(pure_first), "the dominated equal pure op goes");
    check(ir.present(other), "an op with another signedness stays");
    check_equal(ir.f.insts[use].operands[0], a, "readers of the merged op read the dominator");
    check_equal(ir.f.insts[use].operands[1], pure_first, "also for the pure op");
}

NEURA_TEST(tier3, ops_that_do_not_dominate_each_other_are_not_merged) {
    Ir ir(2);
    const auto head = ir.block(1);
    const auto left = ir.block(2);
    const auto right = ir.block(3);
    const auto entry = ir.entry(1, head);
    const auto state = ir.state(1, entry.counter);
    const auto cond = ir.lift(head, Opcode::Less, true, entry.slot[0], entry.slot[1]);
    ir.branch(head, cond, left, right);
    const auto a = ir.lift(left, Opcode::BitOr, false, entry.slot[0], entry.slot[1]);
    ir.deopt(left, entry.counter, state);
    const auto b = ir.lift(right, Opcode::BitOr, false, entry.slot[0], entry.slot[1]);
    ir.deopt(right, entry.counter, state);
    ir.finish();
    d3::number_values(ir.f, ir.counts);
    ir.expect_valid("after");
    check(ir.present(a) && ir.present(b), "two equal ops on the two arms of a branch both stay");
}

// A StateCall that checks a reference (type 3) of `value`.
std::uint32_t reference_check(Ir& ir, std::uint32_t b, std::uint32_t value, std::uint32_t state, std::uint64_t type = 3) {
    t3::Inst in;
    in.op = t3::Op::StateCall;
    in.operands[0] = value;
    in.imm = type;
    in.flags = t3::Checked | t3::Effect | t3::CheckReference;
    in.state = state;
    return ir.add(b, in);
}

NEURA_TEST(tier3, a_repeated_reference_check_goes_unless_a_helper_a_call_or_a_join_with_a_path_without_it_lies_between) {
    {
        Straight s(2, 32);
        auto& ir = s.ir;
        const auto first = reference_check(ir, s.body, s.entry.slot[0], s.state);
        const auto second = reference_check(ir, s.body, s.entry.slot[0], s.state);
        const auto other_type = reference_check(ir, s.body, s.entry.slot[0], s.state, 4);
        end_straight(s);
        ir.expect_valid("before");
        d3::number_values(ir.f, ir.counts);
        ir.expect_valid("after");
        check(ir.present(first) && !ir.present(second), "the second check of the same value and type goes");
        check(ir.present(other_type), "a check against another type stays");
    }
    for (const auto op : {t3::Op::Helper, t3::Op::Call}) {
        Straight s(2, 32);
        auto& ir = s.ir;
        const auto first = reference_check(ir, s.body, s.entry.slot[0], s.state);
        ir.effect(s.body, op, s.state, s.entry.counter);
        // The effect's block cannot hold a check after it: the second check
        // is in the next image block.
        const auto next = ir.block(2);
        ir.jump(s.body, next);
        const auto second = reference_check(ir, next, s.entry.slot[0], s.state);
        ir.deopt(next, s.entry.counter, s.state);
        ir.finish();
        d3::number_values(ir.f, ir.counts);
        check(ir.present(first) && ir.present(second),
              std::string("a check after a ") + (op == t3::Op::Helper ? "helper" : "call") + " is not dropped");
    }
    {
        Ir ir(2);
        const auto head = ir.block(1);
        const auto left = ir.block(2);
        const auto right = ir.block(3);
        const auto join = ir.block(4);
        const auto entry = ir.entry(1, head);
        const auto state = ir.state(1, entry.counter);
        const auto cond = ir.lift(head, Opcode::Less, true, entry.slot[0], entry.slot[1]);
        ir.branch(head, cond, left, right);
        const auto in_left = reference_check(ir, left, entry.slot[0], state);
        ir.jump(left, join);
        ir.jump(right, join);
        const auto after = reference_check(ir, join, entry.slot[0], state);
        ir.deopt(join, entry.counter, state);
        ir.finish();
        d3::number_values(ir.f, ir.counts);
        ir.expect_valid("after");
        check(ir.present(in_left) && ir.present(after), "a check after a join with a path that made none stays");
    }
    {
        // On both paths: the check after the join goes.
        Ir ir(2);
        const auto head = ir.block(1);
        const auto left = ir.block(2);
        const auto right = ir.block(3);
        const auto join = ir.block(4);
        const auto entry = ir.entry(1, head);
        const auto state = ir.state(1, entry.counter);
        const auto cond = ir.lift(head, Opcode::Less, true, entry.slot[0], entry.slot[1]);
        ir.branch(head, cond, left, right);
        reference_check(ir, left, entry.slot[0], state);
        ir.jump(left, join);
        reference_check(ir, right, entry.slot[0], state);
        ir.jump(right, join);
        const auto after = reference_check(ir, join, entry.slot[0], state);
        ir.deopt(join, entry.counter, state);
        ir.finish();
        d3::number_values(ir.f, ir.counts);
        check(!ir.present(after), "a check that every path to it has made goes");
    }
}

// --- Range analysis and check elimination ----------------------------------------------------------

NEURA_TEST(tier3, a_checked_add_whose_interval_reaches_the_types_maximum_keeps_its_check) {
    struct Case {
        const char* name;
        unsigned width;
        bool is_signed;
        std::uint64_t mask;
        bool removable;
    };
    const std::vector<Case> cases = {
        {"i32 signed, up to max - 1", 32, true, 0x7ffffffeU, true},
        {"i32 signed, up to max", 32, true, 0x7fffffffU, false},
        {"i64 signed, up to max - 1", 64, true, 0x7ffffffffffffffeULL, true},
        {"i64 signed, up to max", 64, true, 0x7fffffffffffffffULL, false},
        {"u32, up to max - 1", 32, false, 0xfffffffeU, true},
        {"u32, up to max", 32, false, 0xffffffffU, false},
        {"u64, up to max - 1", 64, false, 0xfffffffffffffffeULL, true},
        {"u64, up to max", 64, false, 0xffffffffffffffffULL, false},
        {"i8 signed, up to 126", 8, true, 126, true},
        {"i8 signed, up to 127", 8, true, 127, false},
        {"u8, up to 254", 8, false, 254, true},
        {"u8, up to 255", 8, false, 255, false},
    };
    for (const auto& c : cases) {
        Straight s(1, c.width);
        auto& ir = s.ir;
        const auto bounded = masked(ir, s.body, s.entry.slot[0], c.mask);
        const auto one = ir.konst(s.body, 1);
        const auto sum = ir.lift(s.body, Opcode::Add, c.is_signed, bounded, one, true, s.state);
        end_straight(s);
        ir.expect_valid(std::string(c.name) + " before");
        run_elimination(ir);
        check_equal(ir.checked(sum) ? 0 : 1, c.removable ? 1 : 0,
                    std::string(c.name) + (c.removable ? ": the check goes" : ": the check stays"));
        if (c.removable) check(ir.f.insts[sum].lifted.op == Opcode::AddWrapping, std::string(c.name) + ": the wrapping form");
    }
}

NEURA_TEST(tier3, subtract_multiply_and_negate_checks_go_exactly_when_the_intervals_prove_no_overflow) {
    Straight s(2, 64);
    auto& ir = s.ir;
    const auto x = s.entry.slot[0];
    const auto y = s.entry.slot[1];
    // Unsigned 64: a - b where a is at least 2^40 and b is below 2^32.
    const auto big = ir.lift(s.body, Opcode::BitOr, false, masked(ir, s.body, x, 0xffffffffULL),
                             ir.konst(s.body, std::uint64_t{1} << 40));
    const auto small = masked(ir, s.body, y, 0xffffffffULL);
    const auto diff_ok = ir.lift(s.body, Opcode::Subtract, false, big, small, true, s.state);
    const auto diff_bad = ir.lift(s.body, Opcode::Subtract, false, small, big, true, s.state);
    // Signed 64 multiply: below 2^31 each fits; below 2^32 each does not;
    // unsigned 64 below 2^32 each fits; full registers do not.
    const auto a31 = masked(ir, s.body, x, 0x7fffffffULL);
    const auto b31 = masked(ir, s.body, y, 0x7fffffffULL);
    const auto prod_ok = ir.lift(s.body, Opcode::Multiply, true, a31, b31, true, s.state);
    const auto a32 = masked(ir, s.body, x, 0xffffffffULL);
    const auto b32 = masked(ir, s.body, y, 0xffffffffULL);
    const auto prod_signed_bad = ir.lift(s.body, Opcode::Multiply, true, a32, b32, true, s.state);
    const auto prod_unsigned_ok = ir.lift(s.body, Opcode::Multiply, false, a32, b32, true, s.state);
    const auto prod_unsigned_bad = ir.lift(s.body, Opcode::Multiply, false, x, y, true, s.state);
    // Negate: signed of a value below 2^63 (not the minimum); of a full register.
    const auto small63 = masked(ir, s.body, x, 0x7fffffffffffffffULL);
    const auto neg_ok = ir.lift(s.body, Opcode::Negate, true, small63, t3::none, true, s.state);
    const auto neg_bad = ir.lift(s.body, Opcode::Negate, true, x, t3::none, true, s.state);
    const auto neg_zero = ir.lift(s.body, Opcode::Negate, false, masked(ir, s.body, x, 0), t3::none, true, s.state);
    end_straight(s);
    ir.expect_valid("before");
    run_elimination(ir);
    check(!ir.checked(diff_ok), "an unsigned difference of a value known to be at least the other goes");
    check(ir.checked(diff_bad), "and its reverse stays");
    check(!ir.checked(prod_ok), "a signed product below 2^62 goes");
    check(ir.checked(prod_signed_bad), "a signed product that can reach 2^64 stays");
    check(!ir.checked(prod_unsigned_ok), "an unsigned 64-bit product of two values below 2^32 goes");
    check(ir.checked(prod_unsigned_bad), "an unsigned 64-bit product of two full registers stays");
    check(!ir.checked(neg_ok), "negating a value that is not the minimum goes");
    check(ir.checked(neg_bad), "negating a full register stays");
    check(!ir.checked(neg_zero), "negating 0 unsigned goes");
}

NEURA_TEST(tier3, a_division_by_a_variable_keeps_both_checks_and_one_by_a_value_that_excludes_them_loses_them) {
    Straight s(2, 32);
    auto& ir = s.ir;
    const auto x = s.entry.slot[0];
    const auto y = s.entry.slot[1];
    const auto by_variable = ir.lift(s.body, Opcode::Divide, true, x, y, true, s.state);
    const auto rem_by_variable = ir.lift(s.body, Opcode::Remainder, true, x, y, true, s.state);
    // y | 0x80000000 is negative, so it is not 0, but it can be -1: the
    // MIN / -1 check stays unless the dividend excludes the minimum.
    const auto odd = ir.lift(s.body, Opcode::BitOr, false, y, ir.konst(s.body, 0x80000000U));
    const auto odd_div_full = ir.lift(s.body, Opcode::Divide, true, x, odd, true, s.state);
    const auto non_min = masked(ir, s.body, x, 0x7fffffffU);
    const auto odd_div_non_min = ir.lift(s.body, Opcode::Divide, true, non_min, odd, true, s.state);
    const auto odd_rem = ir.lift(s.body, Opcode::Remainder, true, x, odd, true, s.state);
    // A positive divisor: 1 + (y & 0xffff).
    const auto positive = ir.lift(s.body, Opcode::AddWrapping, false, masked(ir, s.body, y, 0xffff), ir.konst(s.body, 1));
    const auto div_positive = ir.lift(s.body, Opcode::Divide, true, x, positive, true, s.state);
    const auto udiv_positive = ir.lift(s.body, Opcode::Divide, false, x, positive, true, s.state);
    const auto udiv_variable = ir.lift(s.body, Opcode::Divide, false, x, y, true, s.state);
    end_straight(s);
    ir.expect_valid("before");
    run_elimination(ir);
    check(ir.checked(by_variable), "a division by a variable keeps its check");
    check(ir.checked(rem_by_variable), "a remainder by a variable keeps its check");
    check(ir.checked(odd_div_full), "a divisor that is never 0 but may be -1 keeps the MIN / -1 check");
    check(!ir.checked(odd_div_non_min), "a dividend that is not the minimum makes -1 harmless");
    check(!ir.checked(odd_rem), "a remainder only fails on 0");
    check(!ir.checked(div_positive), "a positive divisor loses both");
    check(!ir.checked(udiv_positive), "an unsigned division by a nonzero value loses its check");
    check(ir.checked(udiv_variable), "an unsigned division by a variable keeps it");
}

NEURA_TEST(tier3, shift_index_convert_and_switch_checks_go_when_the_operand_interval_proves_them) {
    Straight s(2, 32);
    auto& ir = s.ir;
    const auto x = s.entry.slot[0];
    const auto y = s.entry.slot[1];
    const auto small_count = masked(ir, s.body, y, 15);
    const auto big_count = masked(ir, s.body, y, 63);
    const auto shl_ok = ir.lift(s.body, Opcode::ShiftLeft, false, x, small_count, true, s.state);
    const auto shl_bad = ir.lift(s.body, Opcode::ShiftLeft, false, x, big_count, true, s.state);
    const auto shr_ok = ir.lift(s.body, Opcode::ShiftRight, true, x, small_count, true, s.state);
    const auto shr_bad = ir.lift(s.body, Opcode::ShiftRight, true, x, y, true, s.state);
    const auto non_negative = masked(ir, s.body, x, 0x7fffffffU);
    const auto index_ok = ir.lift(s.body, Opcode::Index, true, non_negative, t3::none, true, s.state);
    const auto index_bad = ir.lift(s.body, Opcode::Index, true, x, t3::none, true, s.state);
    const auto index_unsigned = ir.lift(s.body, Opcode::Index, false, x, t3::none, true, s.state);
    const auto widen_ok = ir.convert(s.body, 32, true, 64, true, x, true, s.state);
    const auto narrow_bad = ir.convert(s.body, 32, true, 16, true, x, true, s.state);
    const auto narrow_ok = ir.convert(s.body, 32, true, 16, true, masked(ir, s.body, x, 0x7fff), true, s.state);
    const auto negative_to_unsigned = ir.convert(s.body, 32, true, 32, false, x, true, s.state);
    const auto unsigned_to_signed = ir.convert(s.body, 32, false, 32, true, masked(ir, s.body, x, 0x7fffffffU), true, s.state);
    t3::Inst sc;
    sc.op = t3::Op::SwitchCheck;
    sc.operands[0] = masked(ir, s.body, y, 3);
    sc.imm = 4;
    sc.flags = t3::Checked;
    sc.state = s.state;
    const auto switch_ok = ir.add(s.body, sc);
    sc.imm = 3;
    const auto switch_bad = ir.add(s.body, sc);
    end_straight(s);
    ir.expect_valid("before");
    run_elimination(ir);
    check(!ir.checked(shl_ok) && ir.checked(shl_bad), "a shift count below the width: the check goes; one that can reach it: it stays");
    check(!ir.checked(shr_ok) && ir.checked(shr_bad), "the same for a right shift");
    check(!ir.checked(index_ok) && ir.checked(index_bad), "an index known to be non-negative loses its check; others keep it");
    check(!ir.checked(index_unsigned), "an unsigned index never fails");
    check(!ir.checked(widen_ok), "a signed 32-bit value always fits 64");
    check(ir.checked(narrow_bad) && !ir.checked(narrow_ok), "a narrowing conversion keeps its check unless the interval fits");
    check(ir.checked(negative_to_unsigned), "a signed value that may be negative converted to unsigned keeps its check");
    check(!ir.checked(unsigned_to_signed), "an unsigned value below 2^31 converted to signed32 loses it");
    check(!ir.present(switch_ok) && ir.present(switch_bad), "a selector check goes when the index is below the width");
    check_equal(ir.count_of("elim.switch"), std::uint64_t{1}, "one selector check removed");
}

NEURA_TEST(tier3, conversion_division_and_shift_checks_hold_at_the_64_bit_boundaries) {
    Straight s(2, 64);
    auto& ir = s.ir;
    const auto x = s.entry.slot[0];
    const auto y = s.entry.slot[1];
    // i64 -> i32: [0, 2^31 - 1] fits, [0, 2^31] does not.
    const auto fits = ir.convert(s.body, 64, true, 32, true, masked(ir, s.body, x, 0x7fffffffULL), true, s.state);
    const auto over = ir.convert(s.body, 64, true, 32, true, masked(ir, s.body, x, 0x80000000ULL), true, s.state);
    // u64 -> i64: up to 2^63 - 1 fits, up to 2^63 does not.
    const auto u_fits = ir.convert(s.body, 64, false, 64, true, masked(ir, s.body, x, 0x7fffffffffffffffULL), true, s.state);
    const auto u_over = ir.convert(s.body, 64, false, 64, true, masked(ir, s.body, x, 0x8000000000000000ULL), true, s.state);
    // i64 -> u64: a value below 2^63 is not negative; a full register may be.
    const auto s_to_u = ir.convert(s.body, 64, true, 64, false, masked(ir, s.body, x, 0x7fffffffffffffffULL), true, s.state);
    const auto s_to_u_bad = ir.convert(s.body, 64, true, 64, false, x, true, s.state);
    // MIN / -1 at 64 bits: a dividend that is not the minimum makes -1 harmless.
    const auto minus_one = ir.konst(s.body, ~std::uint64_t{0});
    const auto non_min = masked(ir, s.body, x, 0x7fffffffffffffffULL);
    const auto div_ok = ir.lift(s.body, Opcode::Divide, true, non_min, minus_one, true, s.state);
    const auto div_bad = ir.lift(s.body, Opcode::Divide, true, x, minus_one, true, s.state);
    const auto rem_by_minus_one = ir.lift(s.body, Opcode::Remainder, true, x, minus_one, true, s.state);
    // Shifts: a count up to 63 is below 64, up to 64 is not.
    const auto shl_ok = ir.lift(s.body, Opcode::ShiftLeft, false, x, masked(ir, s.body, y, 63), true, s.state);
    const auto shl_bad = ir.lift(s.body, Opcode::ShiftLeft, false, x, masked(ir, s.body, y, 64), true, s.state);
    // Index at 64: a signed register below 2^63 is not negative.
    const auto index_ok = ir.lift(s.body, Opcode::Index, true, non_min, t3::none, true, s.state);
    const auto index_bad = ir.lift(s.body, Opcode::Index, true, x, t3::none, true, s.state);
    // The register 2^63 has the sign bit set: an index of up to exactly it can fail.
    const auto index_edge = ir.lift(s.body, Opcode::Index, true, masked(ir, s.body, x, 0x8000000000000000ULL), t3::none, true, s.state);
    end_straight(s);
    ir.expect_valid("before");
    run_elimination(ir);
    check(ir.checked(index_edge), "an index that can be exactly 2^63 keeps its check");
    check(!ir.checked(fits) && ir.checked(over), "i64 -> i32 at the 2^31 boundary");
    check(!ir.checked(u_fits) && ir.checked(u_over), "u64 -> i64 at the 2^63 boundary");
    check(!ir.checked(s_to_u) && ir.checked(s_to_u_bad), "i64 -> u64 for a non-negative and a full register");
    check(!ir.checked(div_ok) && ir.checked(div_bad), "MIN / -1 at 64 bits");
    check(!ir.checked(rem_by_minus_one), "a remainder by -1 only fails on a zero divisor");
    check(!ir.checked(shl_ok) && ir.checked(shl_bad), "a shift count at the 64-bit width");
    check(!ir.checked(index_ok) && ir.checked(index_bad), "an index at the 2^63 boundary");
}

NEURA_TEST(tier3, an_operand_wider_than_the_instruction_is_not_taken_for_a_value_of_its_width) {
    // A 32-bit register used by a signed 8-bit add: its values reach 200, so
    // the bits above 8 and the sign are not those of an 8-bit value.
    Straight s(1, 32);
    auto& ir = s.ir;
    const auto x = s.entry.slot[0];
    const auto up_to_50 = masked(ir, s.body, x, 50);
    const auto up_to_200 = masked(ir, s.body, x, 200);
    const auto fits = ir.lift(s.body, Opcode::Add, true, up_to_50, up_to_50, true, s.state, 8, 8);
    const auto sign_bit = ir.lift(s.body, Opcode::Add, true, up_to_200, ir.konst(s.body, 100), true, s.state, 8, 8);
    const auto wide = ir.lift(s.body, Opcode::Add, true, x, ir.konst(s.body, 0), true, s.state, 8, 8);
    end_straight(s);
    run_elimination(ir);
    check(!ir.checked(fits), "50 + 50 cannot overflow 8 signed bits");
    check(ir.checked(sign_bit), "a register that reaches 200 may have the 8-bit sign bit set");
    check(ir.checked(wide), "a register with bits above the width is not an 8-bit value");
}

NEURA_TEST(tier3, a_mask_that_drops_nothing_gives_the_registers_of_the_narrower_width_their_own_sign) {
    // A 32-bit value up to 200, masked to 8 bits: the registers are 0 to 200,
    // which as 8-bit values include the negative ones (200 is -56).
    Straight s(1, 32);
    auto& ir = s.ir;
    const auto up_to_200 = masked(ir, s.body, s.entry.slot[0], 200);
    const auto narrow = ir.mask(s.body, up_to_200, 8);
    const auto hundred = ir.konst(s.body, 100, 8);
    const auto unsigned_add = ir.lift(s.body, Opcode::Add, false, narrow, hundred, true, s.state, 8, 8);
    const auto signed_subtract = ir.lift(s.body, Opcode::Subtract, true, narrow, hundred, true, s.state, 8, 8);
    end_straight(s);
    ir.expect_valid("before");
    run_elimination(ir);
    check(ir.checked(unsigned_add), "200 + 100 overflows 8 unsigned bits");
    check(ir.checked(signed_subtract), "-56 - 100 overflows 8 signed bits");
}

NEURA_TEST(tier3, branch_facts_hold_on_their_own_edge_only) {
    // if (i < n) { i + 1 } else { i + 1 }: only the true edge excludes i = max.
    Ir ir(2);
    const auto head = ir.block(1);
    const auto yes = ir.block(2);
    const auto no = ir.block(3);
    const auto entry = ir.entry(1, head);
    const auto head_state = ir.state(1, entry.counter);
    const auto cond = ir.lift(head, Opcode::Less, true, entry.slot[0], entry.slot[1]);
    ir.branch(head, cond, yes, no);
    const auto yes_state = ir.state(2, entry.counter);
    const auto no_state = ir.state(3, entry.counter);
    const auto one_yes = ir.konst(yes, 1);
    const auto inc_yes = ir.lift(yes, Opcode::Add, true, entry.slot[0], one_yes, true, yes_state);
    ir.deopt(yes, entry.counter, yes_state);
    const auto one_no = ir.konst(no, 1);
    const auto inc_no = ir.lift(no, Opcode::Add, true, entry.slot[0], one_no, true, no_state);
    ir.deopt(no, entry.counter, no_state);
    (void)head_state;
    ir.finish();
    ir.expect_valid("before");
    run_elimination(ir);
    check(!ir.checked(inc_yes), "after i < n, i + 1 cannot overflow");
    check(ir.checked(inc_no), "after !(i < n), i + 1 can: i may be the maximum");
    // The same facts for >=, <=, > and unsigned comparisons.
    struct Case {
        Opcode op;
        bool is_signed;
        bool on_true;
        bool removable;
    };
    for (const auto& c : std::vector<Case>{{Opcode::Less, true, true, true},
                                           {Opcode::Less, true, false, false},
                                           {Opcode::GreaterEqual, true, false, true},
                                           {Opcode::GreaterEqual, true, true, false},
                                           {Opcode::LessEqual, true, true, false},
                                           {Opcode::LessEqual, true, false, false},
                                           {Opcode::Greater, true, true, false},
                                           {Opcode::Less, false, true, true},
                                           {Opcode::Less, false, false, false}}) {
        Ir other(2);
        const auto h = other.block(1);
        const auto t = other.block(2);
        const auto e = other.block(3);
        const auto in = other.entry(1, h);
        const auto cmp = other.lift(h, c.op, c.is_signed, in.slot[0], in.slot[1]);
        other.branch(h, cmp, t, e);
        const auto target = c.on_true ? t : e;
        const auto st = other.state(c.on_true ? 2 : 3, in.counter);
        const auto add = other.lift(target, Opcode::Add, c.is_signed, in.slot[0], other.konst(target, 1), true, st);
        other.deopt(target, in.counter, st);
        const auto rest = c.on_true ? e : t;
        other.deopt(rest, in.counter, other.state(c.on_true ? 3 : 2, in.counter));
        other.finish();
        run_elimination(other);
        check_equal(other.checked(add) ? 0 : 1, c.removable ? 1 : 0, "a comparison fact on the edge it holds on");
    }
}

NEURA_TEST(tier3, a_loop_variable_nothing_bounds_widens_to_the_type_and_does_not_stop_other_proofs) {
    // i = i + 2 (checked) in a loop nothing bounds: its interval
    // grows until it is widened to the whole type, so the analysis settles; a
    // proof about a value outside the loop still goes through.
    LoopOptions options;
    options.bound = false;
    options.start_at_zero = true;
    options.middle_entry = false;
    auto l = counted_loop(options, [](Loop& loop) {
        const auto two = loop.ir.konst(loop.body, 2);
        loop.next = loop.ir.lift(loop.body, Opcode::Add, true, loop.i, two, true, loop.body_state);
    });
    auto& ir = l->ir;
    // After the loop: a masked load, checked +1.
    ir.f.blocks[l->exit].insts.pop_back();
    const auto bounded = masked(ir, l->exit, l->n, 0x7ffffffe);
    const auto sum = ir.lift(l->exit, Opcode::Add, true, bounded, ir.konst(l->exit, 1), true, l->exit_state);
    ir.deopt(l->exit, l->c, l->exit_state);
    ir.finish();
    ir.expect_valid("before");
    run_elimination(ir);
    check(!ir.checked(sum), "the analysis settled (widening) and proved the add after the loop");
    check(ir.checked(l->next), "the unbounded variable's own add keeps its check");
}

NEURA_TEST(tier3, an_equal_comparison_narrows_and_not_equal_trims_an_interval_edge) {
    Ir ir(2);
    const auto head = ir.block(1);
    const auto yes = ir.block(2);
    const auto no = ir.block(3);
    const auto entry = ir.entry(1, head);
    // x is in [0, 255]: if x != 255 then x + 1 is fine at 8 bits unsigned.
    const auto x = masked(ir, head, entry.slot[0], 255);
    const auto limit = ir.konst(head, 255);
    const auto cmp = ir.lift(head, Opcode::NotEqual, false, x, limit);
    ir.branch(head, cmp, yes, no);
    const auto st_yes = ir.state(2, entry.counter);
    const auto st_no = ir.state(3, entry.counter);
    const auto inc_yes = ir.lift(yes, Opcode::Add, false, x, ir.konst(yes, 1), true, st_yes, 8);
    ir.deopt(yes, entry.counter, st_yes);
    const auto inc_no = ir.lift(no, Opcode::Add, false, x, ir.konst(no, 1), true, st_no, 8);
    ir.deopt(no, entry.counter, st_no);
    ir.finish();
    run_elimination(ir);
    check(!ir.checked(inc_yes), "x != 255 leaves x + 1 inside 8 bits");
    check(ir.checked(inc_no), "x == 255 does not");
}

// --- The interval arithmetic against lift::evaluate --------------------------------------------

// The width of the result the builder gives a lifted instruction on operands
// declared dx and dy bits wide (tier3_build.cpp, result_type).
unsigned result_width_of(const lift::Instruction& l, unsigned dx, unsigned dy) {
    switch (l.op) {
    case Opcode::Divide:
    case Opcode::Remainder: return l.is_signed ? l.width : dx;
    case Opcode::Equal:
    case Opcode::NotEqual:
    case Opcode::Less:
    case Opcode::LessEqual:
    case Opcode::Greater:
    case Opcode::GreaterEqual:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Not: return 1;
    case Opcode::BitAnd: return std::min(dx, dy);
    case Opcode::BitOr:
    case Opcode::BitXor: return std::max(dx, dy);
    case Opcode::Index: return dx;
    case Opcode::ValueHash: return 32;
    case Opcode::ConvertInteger: return l.to_width;
    default: return l.width;
    }
}

bool is_unary(Opcode op) {
    return op == Opcode::Negate || op == Opcode::Not || op == Opcode::BitNot || op == Opcode::Index ||
           op == Opcode::ConvertInteger || op == Opcode::ValueHash;
}

const std::vector<Opcode>& audited_opcodes() {
    static const std::vector<Opcode> ops = {
        Opcode::Add,      Opcode::AddWrapping, Opcode::Subtract, Opcode::SubtractWrapping, Opcode::Multiply, Opcode::Divide,
        Opcode::Remainder, Opcode::Negate,     Opcode::Equal,    Opcode::NotEqual,         Opcode::Less,     Opcode::LessEqual,
        Opcode::Greater,  Opcode::GreaterEqual, Opcode::And,     Opcode::Or,               Opcode::Not,      Opcode::BitAnd,
        Opcode::BitOr,    Opcode::BitXor,      Opcode::BitNot,   Opcode::ShiftLeft,        Opcode::ShiftRight, Opcode::Index,
        Opcode::ConvertInteger, Opcode::ValueHash};
    return ops;
}

// Audits every opcode, signedness, and conversion target at width `w` for
// operands declared `dx` and `dy` bits wide, over the intervals `xs` and `ys`.
void audit_width(unsigned w, unsigned dx, unsigned dy,
                 const std::vector<std::pair<std::uint64_t, std::uint64_t>>& xs,
                 const std::vector<std::pair<std::uint64_t, std::uint64_t>>& ys, std::uint64_t samples,
                 std::uint64_t& audited) {
    for (const auto op : audited_opcodes()) {
        for (const bool is_signed : {false, true}) {
            std::vector<std::pair<unsigned, bool>> targets{{0, false}};
            if (op == Opcode::ConvertInteger) {
                targets.clear();
                for (const auto to : {w > 1 ? w - 1 : 1U, w, w + 1}) {
                    for (const bool to_signed : {false, true}) targets.emplace_back(to, to_signed);
                }
            }
            for (const auto& [to, to_signed] : targets) {
                lift::Instruction l;
                l.op = op;
                l.width = static_cast<std::uint8_t>(w);
                l.is_signed = is_signed;
                l.to_width = static_cast<std::uint8_t>(to);
                l.to_signed = to_signed;
                const auto rw = result_width_of(l, dx, dy);
                if (rw > 64) continue;
                // The value hash of a 33 to 63 bit integer is not reduced to 32 bits (lift/evaluate.hpp);
                // the language has no such integer.
                if (op == Opcode::ValueHash && w > 32 && w < 64) continue;
                // A signed division reads sign_extend(b, w): a register with bits above
                // the width could make it 0 behind the check, which no program builds.
                if ((op == Opcode::Divide || op == Opcode::Remainder) && is_signed && (dx > w || dy > w)) continue;
                for (const auto& [xlo, xhi] : xs) {
                    if (is_unary(op)) {
                        const auto text = d3::audit_lift(l, rw, dx, dy, false, xlo, xhi, 0, 0, samples);
                        check(text.empty(), text);
                        ++audited;
                        continue;
                    }
                    for (const auto& [ylo, yhi] : ys) {
                        const auto text = d3::audit_lift(l, rw, dx, dy, true, xlo, xhi, ylo, yhi, samples);
                        check(text.empty(), text);
                        ++audited;
                    }
                }
            }
        }
    }
}

NEURA_TEST(tier3, the_interval_arithmetic_agrees_with_lift_evaluate_on_every_value_in_the_intervals_at_small_widths) {
    std::uint64_t audited = 0;
    for (const unsigned w : {1U, 2U, 3U, 4U}) {
        for (const unsigned dx : {w, w + 1}) {
            for (const unsigned dy : {w, w + 1}) {
                const auto intervals = [&](unsigned d) {
                    std::vector<std::pair<std::uint64_t, std::uint64_t>> list;
                    const std::uint64_t top = (std::uint64_t{1} << d) - 1;
                    for (std::uint64_t lo = 0; lo <= top; ++lo) {
                        for (std::uint64_t hi = lo; hi <= top; ++hi) list.emplace_back(lo, hi);
                    }
                    return list;
                };
                auto ys = intervals(dy);
                // Every interval for the first operand, every third for the second.
                std::vector<std::pair<std::uint64_t, std::uint64_t>> some;
                for (std::size_t i = 0; i < ys.size(); i += 3) some.push_back(ys[i]);
                audit_width(w, dx, dy, intervals(dx), some, 0, audited);
            }
        }
    }
    check(audited > 100000, "many pairs audited: " + std::to_string(audited));
}

NEURA_TEST(tier3, the_interval_arithmetic_agrees_with_lift_evaluate_at_8_16_32_and_64_bits_around_the_boundaries) {
    std::uint64_t audited = 0;
    for (const unsigned w : {8U, 16U, 32U, 64U}) {
        for (const unsigned dx : {w, 64U}) {
            for (const unsigned dy : {w, 64U}) {
                const auto interesting = [&](unsigned d) {
                    const auto top = d >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << d) - 1;
                    const auto half = std::uint64_t{1} << (w - 1);
                    std::vector<std::uint64_t> values{0, 1, 2, 3, half - 1, half, half + 1, (half << 1) - 1, top - 1, top};
                    std::vector<std::uint64_t> kept;
                    for (const auto v : values) {
                        if (v <= top && std::find(kept.begin(), kept.end(), v) == kept.end()) kept.push_back(v);
                    }
                    std::sort(kept.begin(), kept.end());
                    std::vector<std::pair<std::uint64_t, std::uint64_t>> list;
                    for (std::size_t i = 0; i < kept.size(); ++i) {
                        for (std::size_t j = i; j < kept.size(); ++j) list.emplace_back(kept[i], kept[j]);
                    }
                    return list;
                };
                audit_width(w, dx, dy, interesting(dx), interesting(dy), 6, audited);
            }
        }
    }
    check(audited > 100000, "many pairs audited: " + std::to_string(audited));
}

NEURA_TEST(tier3, the_interval_arithmetic_agrees_with_lift_evaluate_on_random_intervals_at_odd_widths) {
    std::uint64_t audited = 0;
    // NEURA_TEST_AUDIT_SCALE=N audits N times as many intervals (a longer search, not a different test).
    const char* scale_text = std::getenv("NEURA_TEST_AUDIT_SCALE");
    const int scale = scale_text ? std::max(1, std::atoi(scale_text)) : 1;
    std::uint64_t seed = 0x2545f4914f6cdd1dULL;
    const auto next = [&] {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return seed >> 7;
    };
    for (const unsigned w : {5U, 7U, 12U, 24U, 33U, 48U, 63U, 64U}) {
        for (const unsigned dx : {w, std::min(64U, w + 3), 64U}) {
            for (const unsigned dy : {w, 64U}) {
                const auto random_intervals = [&](unsigned d) {
                    const auto top = d >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << d) - 1;
                    std::vector<std::pair<std::uint64_t, std::uint64_t>> list;
                    for (int i = 0; i < 14 * scale; ++i) {
                        // A value with a random number of significant bits, near a power of two half the time.
                        const auto make = [&] {
                            const unsigned bits = static_cast<unsigned>(next() % (d + 1));
                            auto v = bits == 0 ? 0 : next() >> (64 - bits);
                            if (next() % 2 == 0 && bits > 0) v = (std::uint64_t{1} << (bits - 1)) + (next() % 3) - 1;
                            return std::min(v, top);
                        };
                        auto a = make();
                        auto b = make();
                        if (a > b) std::swap(a, b);
                        list.emplace_back(a, b);
                    }
                    return list;
                };
                audit_width(w, dx, dy, random_intervals(dx), random_intervals(dy), 8, audited);
            }
        }
    }
    check(audited > 50000, "many pairs audited: " + std::to_string(audited));
}

NEURA_TEST(tier3, a_comparison_narrows_two_ranges_to_the_pairs_of_values_that_satisfy_it) {
    std::uint64_t audited = 0;
    const std::vector<Opcode> compares = {Opcode::Equal, Opcode::NotEqual, Opcode::Less, Opcode::LessEqual, Opcode::Greater,
                                          Opcode::GreaterEqual};
    for (const unsigned w : {1U, 2U, 3U, 4U}) {
        for (const unsigned dx : {w, w + 1}) {
            for (const unsigned dy : {w, w + 1}) {
                const std::uint64_t tx = (std::uint64_t{1} << dx) - 1;
                const std::uint64_t ty = (std::uint64_t{1} << dy) - 1;
                for (const auto op : compares) {
                    for (const bool sense : {false, true}) {
                        for (const bool is_signed : {false, true}) {
                            for (std::uint64_t xl = 0; xl <= tx; ++xl) {
                                for (std::uint64_t xh = xl; xh <= tx; ++xh) {
                                    for (std::uint64_t yl = 0; yl <= ty; yl += 2) {
                                        for (std::uint64_t yh = yl; yh <= ty; ++yh) {
                                            const auto text = d3::audit_refine(op, sense, is_signed, w, dx, dy, xl, xh, yl, yh, 0);
                                            check(text.empty(), text);
                                            ++audited;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // Wide ones, around the boundaries.
    for (const unsigned w : {8U, 32U, 64U}) {
        const auto half = std::uint64_t{1} << (w - 1);
        const std::vector<std::uint64_t> values = {0, 1, half - 1, half, half + 1, w == 64 ? ~std::uint64_t{0} - 1 : (half << 1) - 2,
                                                   w == 64 ? ~std::uint64_t{0} : (half << 1) - 1};
        for (const unsigned dx : {w, 64U}) {
            for (const unsigned dy : {w, 64U}) {
                for (const auto op : compares) {
                    for (const bool sense : {false, true}) {
                        for (const bool is_signed : {false, true}) {
                            for (std::size_t i = 0; i < values.size(); ++i) {
                                for (std::size_t j = i; j < values.size(); ++j) {
                                    for (std::size_t k = 0; k < values.size(); ++k) {
                                        for (std::size_t m = k; m < values.size(); ++m) {
                                            const auto text = d3::audit_refine(op, sense, is_signed, w, dx, dy, values[i], values[j],
                                                                               values[k], values[m], 4);
                                            check(text.empty(), text);
                                            ++audited;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    check(audited > 100000, "many refinements audited: " + std::to_string(audited));
}

// --- Dead code -------------------------------------------------------------------------------------------

NEURA_TEST(tier3, dead_code_removal_drops_unused_pure_values_and_never_a_check_an_effect_or_a_counter_add) {
    Straight s(2, 32);
    auto& ir = s.ir;
    const auto x = s.entry.slot[0];
    const auto unused_pure = ir.lift(s.body, Opcode::BitXor, false, x, s.entry.slot[1]);
    const auto unused_constant = ir.konst(s.body, 9);
    const auto unused_phi_like_mask = ir.mask(s.body, x, 7);
    const auto unused_check = ir.lift(s.body, Opcode::Add, true, x, s.entry.slot[1], true, s.state);
    const auto unused_counter = ir.counter_add(s.body, s.entry.counter, 4);
    const auto write_back = ir.effect(s.body, t3::Op::WriteBack, s.state);
    const auto used = ir.lift(s.body, Opcode::BitAnd, false, x, s.entry.slot[1]);
    const auto reader = ir.state(1, s.entry.counter, {{0, used}});
    ir.effect(s.body, t3::Op::WriteBack, s.state);
    ir.deopt(s.body, s.entry.counter, reader);
    ir.finish();
    ir.expect_valid("before");
    d3::remove_dead(ir.f, ir.counts);
    ir.expect_valid("after");
    check(!ir.present(unused_pure) && !ir.present(unused_constant) && !ir.present(unused_phi_like_mask),
          "unused pure values go");
    check(ir.present(unused_check), "an unused check stays: the check is the trap");
    check(ir.present(unused_counter), "a counter add stays");
    check(ir.present(write_back), "an effect stays");
    check(ir.present(used), "a value only a frame state reads stays");
    check_equal(ir.count_of("dce.values"), std::uint64_t{3}, "three values removed");
}

// --- Induction variables and their guards ------------------------------------------------------------------------

std::vector<std::uint32_t> guards_in(const Ir& ir) { return ir.all_of(t3::Op::Guard); }

// The preheader block of the loop whose head is `head`, or none.
std::uint32_t preheader_of(const Ir& ir, std::uint32_t head) {
    for (const auto p : ir.f.blocks[head].preds) {
        if (ir.f.blocks[p].preheader) return p;
    }
    return t3::none;
}

NEURA_TEST(tier3, an_index_over_a_variable_that_starts_at_an_entry_value_gets_a_guard_in_the_preheader) {
    auto l = counted_loop(LoopOptions{}, [](Loop& loop) {
        loop.index = loop.ir.lift(loop.body, Opcode::Index, true, loop.i, t3::none, true, loop.body_state);
        increment(loop, 32);
    });
    auto& ir = l->ir;
    ir.expect_valid("before");
    run_elimination(ir);
    const auto guards = guards_in(ir);
    check_equal(guards.size(), std::size_t{1}, "one guard: the variable starts at 0 or more");
    const auto p = preheader_of(ir, l->head);
    check(p != t3::none, "the loop has a preheader");
    check_equal(ir.f.blocks[p].preds.size(), std::size_t{2}, "the code before the loop and the entry at the head both pass through it");
    check_equal(ir.block_of(guards[0]), p, "the guard is in the preheader");
    const auto& guard = ir.f.insts[guards[0]];
    const auto& compare = ir.f.insts[guard.operands[0]];
    check(compare.lifted.op == Opcode::GreaterEqual && compare.lifted.is_signed, "it tests init >= 0, signed");
    check(ir.f.insts[compare.operands[1]].op == t3::Op::Const && ir.f.insts[compare.operands[1]].imm == 0, "against 0");
    // The init is what the preheader gives the variable.
    check(ir.f.insts[compare.operands[0]].op == t3::Op::Phi && ir.block_of(compare.operands[0]) == p,
          "of the phi that merges the two entries' values");
    check(!ir.checked(l->index), "the index check is gone");
    check_equal(ir.f.insts[l->index].guard, guards[0], "and names its guard");
    check(!ir.checked(l->next), "the increment cannot overflow: i < n");
    check_equal(ir.f.insts[l->next].guard, guards[0] == t3::none ? t3::none : ir.f.insts[l->next].guard, "no claim");
    // The guard's state is the head's entry state with the head's phis
    // replaced by what the preheader gives them.
    const auto& state = ir.f.states[guard.state];
    check_equal(state.block, 1U, "its state resumes at the loop head's image block");
    check_equal(state.caller, t3::none, "in the physical frame");
    check(state.slots[0].kind == t3::StateSlot::Kind::Value && state.slots[0].value == compare.operands[0],
          "with the variable's slot holding its initial value");
    const auto& head_phi_c = ir.f.insts[l->c];
    check(state.blocks != l->c && ir.block_of(state.blocks) == p && head_phi_c.op == t3::Op::Phi,
          "and the step count the preheader's");
    check(ir.violations().empty(), "V10 and the other rules hold");
    check_equal(ir.count_of("guard.init_nonnegative"), std::uint64_t{1}, "counted");
}

NEURA_TEST(tier3, a_variable_that_starts_at_zero_and_is_never_entered_in_the_middle_needs_no_guard) {
    LoopOptions options;
    options.start_at_zero = true;
    options.middle_entry = false;
    auto l = counted_loop(options, [](Loop& loop) {
        loop.index = loop.ir.lift(loop.body, Opcode::Index, true, loop.i, t3::none, true, loop.body_state);
        increment(loop, 32);
    });
    auto& ir = l->ir;
    run_elimination(ir);
    check(guards_in(ir).empty(), "no guard: 0 is known");
    check(!ir.checked(l->index), "the index check is gone");
    check_equal(ir.f.insts[l->index].guard, t3::none, "without a guard");
    check_equal(preheader_of(ir, l->head), t3::none, "and no preheader");
}

NEURA_TEST(tier3, a_variable_that_starts_at_zero_but_can_be_entered_in_the_middle_gets_the_guard_for_that_entry) {
    LoopOptions options;
    options.start_at_zero = true;
    options.middle_entry = true;
    auto l = counted_loop(options, [](Loop& loop) {
        loop.index = loop.ir.lift(loop.body, Opcode::Index, true, loop.i, t3::none, true, loop.body_state);
        increment(loop, 32);
    });
    auto& ir = l->ir;
    run_elimination(ir);
    const auto guards = guards_in(ir);
    check_equal(guards.size(), std::size_t{1}, "the entry at the head brings an unknown value: one guard");
    check(!ir.checked(l->index) && ir.f.insts[l->index].guard == guards[0], "the index check goes under the guard");
}

NEURA_TEST(tier3, an_exit_on_not_equal_gets_a_guard_that_the_variable_starts_at_or_below_the_bound) {
    LoopOptions options;
    options.test = Opcode::NotEqual;
    auto l = counted_loop(options, [](Loop& loop) { increment(loop, 32); });
    auto& ir = l->ir;
    ir.expect_valid("before");
    check(ir.checked(l->next), "the increment is checked before");
    run_elimination(ir);
    const auto guards = guards_in(ir);
    check_equal(guards.size(), std::size_t{1}, "one guard");
    const auto& compare = ir.f.insts[ir.f.insts[guards[0]].operands[0]];
    check(compare.lifted.op == Opcode::LessEqual && compare.lifted.is_signed, "init <= bound, signed");
    check(ir.f.insts[compare.operands[1]].op == t3::Op::Phi && ir.block_of(compare.operands[1]) == preheader_of(ir, l->head),
          "the bound is what the preheader gives n");
    check(!ir.checked(l->next), "the increment's check is gone");
    check_equal(ir.f.insts[l->next].guard, guards[0], "it names the guard");
    check_equal(ir.f.insts[l->compare].guard, guards[0], "so does the exit test, which now says i < n on its true edge");
    check_equal(ir.f.insts[l->compare].imm, std::uint64_t{4}, "signed, the variable the first operand, counting up");
    check_equal(ir.count_of("guard.init_bounded"), std::uint64_t{1}, "counted");
    ir.expect_valid("after");
}

NEURA_TEST(tier3, a_constant_bound_of_the_loop_body_is_made_again_in_the_preheader_for_the_guard) {
    LoopOptions options;
    options.test = Opcode::NotEqual;
    options.constant_bound = 10;
    auto l = counted_loop(options, [](Loop& loop) { increment(loop, 32); });
    auto& ir = l->ir;
    ir.expect_valid("before");
    run_elimination(ir);
    const auto guards = guards_in(ir);
    check_equal(guards.size(), std::size_t{1}, "one guard");
    const auto p = preheader_of(ir, l->head);
    const auto& compare = ir.f.insts[ir.f.insts[guards[0]].operands[0]];
    check(ir.f.insts[compare.operands[1]].op == t3::Op::Const && ir.f.insts[compare.operands[1]].imm == 10 &&
              ir.block_of(compare.operands[1]) == p,
          "the bound the guard tests is a constant of the preheader");
    check(!ir.checked(l->next), "the increment's check is gone");
}

NEURA_TEST(tier3, an_increment_in_a_not_equal_loop_keeps_its_check_when_the_exit_is_not_a_plain_test_of_the_variable) {
    // i != n where the increment is by two: i can step over n.
    LoopOptions options;
    options.test = Opcode::NotEqual;
    auto l = counted_loop(options, [](Loop& loop) {
        const auto two = loop.ir.konst(loop.body, 2);
        loop.next = loop.ir.lift(loop.body, Opcode::Add, true, loop.i, two, true, loop.body_state);
    });
    run_elimination(l->ir);
    check(l->ir.checked(l->next), "i + 2 can overflow in a loop that ends only when i == n");
    check(guards_in(l->ir).empty(), "and no guard is made");
}

// --- Loop-invariant code motion ------------------------------------------------------------------------------

NEURA_TEST(tier3, an_invariant_pure_op_moves_to_the_preheader_and_a_loop_variant_one_stays) {
    std::uint32_t invariant = 0, variant = 0;
    auto l = counted_loop(LoopOptions{}, [&](Loop& loop) {
        invariant = loop.ir.lift(loop.body, Opcode::BitXor, false, loop.n, loop.ir.konst(loop.body, 5));
        variant = loop.ir.lift(loop.body, Opcode::BitXor, false, loop.i, invariant);
        increment(loop, 32);
    });
    auto& ir = l->ir;
    ir.expect_valid("before");
    d3::hoist_invariants(ir.f, ir.counts);
    ir.expect_valid("after");
    const auto p = preheader_of(ir, l->head);
    check(p != t3::none, "a preheader was made");
    check_equal(ir.block_of(invariant), p, "the op on n moved to it");
    check_equal(ir.block_of(variant), l->body, "the op on i did not");
    // Its operand is what the preheader gives n, not the head's phi.
    check(ir.block_of(ir.f.insts[invariant].operands[0]) != l->head, "its operand is not a phi of the head");
    check_equal(ir.count_of("licm.pure"), std::uint64_t{1}, "counted");
}

NEURA_TEST(tier3, a_division_whose_check_went_stays_where_its_proof_holds_and_does_not_move_to_the_preheader) {
    std::uint32_t division = 0, remainder = 0, xor_op = 0;
    auto l = counted_loop(LoopOptions{}, [&](Loop& loop) {
        auto& ir = loop.ir;
        // Both have no check: a divisor of 7 is proven. Running them where the
        // loop is not entered would be harmless here, but a divisor that is
        // proven only on a path must not meet a hardware division elsewhere.
        division = ir.lift(loop.body, Opcode::Divide, true, loop.n, ir.konst(loop.body, 7));
        remainder = ir.lift(loop.body, Opcode::Remainder, false, loop.n, ir.konst(loop.body, 7));
        xor_op = ir.lift(loop.body, Opcode::BitXor, false, loop.n, ir.konst(loop.body, 5));
        increment(loop, 32);
    });
    auto& ir = l->ir;
    d3::hoist_invariants(ir.f, ir.counts);
    ir.expect_valid("after");
    const auto p = preheader_of(ir, l->head);
    check(p != t3::none, "the pure op moved, so a preheader was made");
    check_equal(ir.block_of(xor_op), p, "a bit operation on n moves");
    check_equal(ir.block_of(division), l->body, "a division stays");
    check_equal(ir.block_of(remainder), l->body, "a remainder stays");
}

NEURA_TEST(tier3, an_invariant_check_on_every_path_moves_to_the_preheader_with_the_loop_heads_entry_state) {
    std::uint32_t product = 0;
    auto l = counted_loop(LoopOptions{}, [&](Loop& loop) {
        product = loop.ir.lift(loop.body, Opcode::Multiply, true, loop.n, loop.n, true, loop.body_state);
        increment(loop, 32);
    });
    auto& ir = l->ir;
    d3::hoist_invariants(ir.f, ir.counts);
    ir.expect_valid("after");
    const auto p = preheader_of(ir, l->head);
    check_equal(ir.block_of(product), p, "the checked product moved");
    check(ir.checked(product), "and is still checked");
    const auto& state = ir.f.states[ir.f.insts[product].state];
    check_equal(state.block, 1U, "its state is the entry of the loop head's image block");
    check(state.blocks != l->c, "with the preheader's step count, not the head's phi");
    check_equal(ir.count_of("licm.check"), std::uint64_t{1}, "counted");
}

NEURA_TEST(tier3, a_check_on_a_conditional_path_is_not_hoisted) {
    std::uint32_t conditional = 0, always = 0;
    auto l = counted_loop(LoopOptions{}, [&](Loop& loop) {
        auto& ir = loop.ir;
        always = ir.lift(loop.body, Opcode::Multiply, true, loop.n, loop.n, true, loop.body_state);
        const auto test = ir.lift(loop.body, Opcode::Less, true, loop.i, loop.n);
        const auto arm = ir.block(4);
        const auto skip = ir.block(5);
        const auto join = ir.block(6);
        ir.branch(loop.body, test, arm, skip);
        const auto arm_state = ir.state(4, loop.c, {{0, loop.i}, {1, loop.n}});
        conditional = ir.lift(arm, Opcode::Multiply, true, loop.n, loop.n, true, arm_state);
        ir.jump(arm, join);
        ir.jump(skip, join);
        const auto one = ir.konst(join, 1);
        loop.next = ir.lift(join, Opcode::AddWrapping, false, loop.i, one);
        loop.latch = join;
    });
    auto& ir = l->ir;
    ir.expect_valid("before");
    d3::hoist_invariants(ir.f, ir.counts);
    ir.expect_valid("after");
    const auto p = preheader_of(ir, l->head);
    check(p != t3::none, "a preheader was made for the check that can move");
    check_equal(ir.block_of(always), p, "a check on every path moves");
    check(ir.block_of(conditional) != p, "a check on a conditional path stays");
}

NEURA_TEST(tier3, a_check_is_not_hoisted_past_a_state_call_that_writes_a_helper_a_call_or_a_write_back) {
    for (const auto effect : {t3::Op::StateCall, t3::Op::Helper, t3::Op::Call, t3::Op::WriteBack}) {
        std::uint32_t before = 0, after = 0;
        auto l = counted_loop(LoopOptions{}, [&](Loop& loop) {
            auto& ir = loop.ir;
            before = ir.lift(loop.body, Opcode::Multiply, true, loop.n, loop.n, true, loop.body_state);
            ir.effect(loop.body, effect, loop.body_state, effect == t3::Op::StateCall || effect == t3::Op::WriteBack ? t3::none : loop.c);
            // The check after the effect is in the next image block.
            const auto later = ir.block(7);
            ir.jump(loop.body, later);
            const auto later_state = ir.state(7, loop.c, {{0, loop.i}, {1, loop.n}});
            after = ir.lift(later, Opcode::Multiply, true, loop.n, loop.n, true, later_state);
            const auto one = ir.konst(later, 1);
            loop.next = ir.lift(later, Opcode::AddWrapping, false, loop.i, one);
            loop.latch = later;
        });
        auto& ir = l->ir;
        const std::string what = effect == t3::Op::StateCall ? "a state call" : effect == t3::Op::Helper ? "a helper"
                                 : effect == t3::Op::Call ? "a call" : "a write-back";
        ir.expect_valid(what + " before");
        d3::hoist_invariants(ir.f, ir.counts);
        ir.expect_valid(what + " after");
        const auto p = preheader_of(ir, l->head);
        check(p != t3::none, what + ": a preheader was made for the check before the effect");
        check_equal(ir.block_of(before), p, what + ": the check before the effect moves");
        check(ir.block_of(after) != p, what + ": the check after the effect stays");
    }
}

NEURA_TEST(tier3, a_loop_that_can_be_entered_in_its_middle_is_left_alone) {
    std::uint32_t invariant = 0;
    LoopOptions options;
    options.body_entry = true;
    auto l = counted_loop(options, [&](Loop& loop) {
        // Nothing here reads the head's values: the body is also entered from outside.
        invariant = loop.ir.lift(loop.body, Opcode::BitXor, false, loop.ir.konst(loop.body, 7), loop.ir.konst(loop.body, 5));
        loop.next = loop.ir.lift(loop.body, Opcode::AddWrapping, false, loop.ir.konst(loop.body, 1), loop.ir.konst(loop.body, 1));
    });
    auto& ir = l->ir;
    ir.expect_valid("before");
    d3::hoist_invariants(ir.f, ir.counts);
    run_elimination(ir);
    check_equal(preheader_of(ir, l->head), t3::none, "no preheader: the loop has an entry besides its head");
    check_equal(ir.block_of(invariant), l->body, "nothing moved");
    check(guards_in(ir).empty(), "no guards");
}

// --- Checkpoint lengths -----------------------------------------------------------------------------

NEURA_TEST(tier3, the_checkpoint_lengths_are_recomputed_on_the_graph_the_passes_leave) {
    // A loop whose body branches on a constant: a long arm (5 blocks) and a
    // short one (1). Folding the branch to the short arm shortens the run
    // from the head's budget to the next.
    std::uint32_t budget = 0;
    auto l = counted_loop(LoopOptions{}, [&](Loop& loop) {
        auto& ir = loop.ir;
        const auto cond = ir.konst(loop.body, 0, 1);
        const auto long_arm = ir.block(4);
        const auto short_arm = ir.block(5);
        const auto join = ir.block(6);
        ir.branch(loop.body, cond, long_arm, short_arm);
        const auto five = ir.counter_add(long_arm, loop.c, 5);
        ir.jump(long_arm, join);
        const auto one = ir.counter_add(short_arm, loop.c, 1);
        ir.jump(short_arm, join);
        const auto merged = ir.phi(join, 64, 2);
        ir.set_phi(merged, 0, five);
        ir.set_phi(merged, 1, one);
        const auto step = ir.konst(join, 1);
        loop.next = ir.lift(join, Opcode::AddWrapping, false, loop.i, step);
        loop.c = loop.c;
        // The latch adds to the merged count.
        loop.latch = join;
        (void)merged;
    });
    auto& ir = l->ir;
    for (const auto id : ir.f.blocks[l->head].insts) {
        if (ir.f.insts[id].op == t3::Op::Budget) budget = id;
    }
    ir.expect_valid("before");
    check_equal(ir.f.insts[budget].imm, std::uint64_t{6}, "the long arm and the latch's step: 5 + 1");
    d3::simplify(ir.f, ir.counts);
    d3::compute_budgets(ir.f);
    ir.expect_valid("after the branch folded to the short arm");
    check_equal(ir.f.insts[budget].imm, std::uint64_t{2}, "1 + 1 on the graph that is left");
}

// --- The passes at work on programs ------------------------------------------------------------------------

using Perturb = nvm::TierShadowHooks::Perturb;

// Lifts every block, forms every program at the first arrival, and turns a
// program native at its `native_after`th checkpoint arrival.
nvm::TierOptions native_options(std::uint32_t native_after = 1, bool passes = true) {
    nvm::TierOptions options;
    options.hint_after = 1;
    options.search_after = 1;
    options.minimum_proof_work = std::uint64_t{1} << 30;
    options.region_after = 1;
    options.programs = nvm::TierOptions::Programs::On;
    options.tier3 = nvm::TierOptions::Tier3::Interp;
    options.native_after = native_after;
    options.native_passes = passes;
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

void compare_with_tier_0(const nvr::Image& image, nvm::TierCache& cache, const std::string& label,
                         const std::vector<std::string>& arguments, const Adjust& adjust = {}) {
    auto reference_limits = limits_at(0);
    if (adjust) adjust(reference_limits);
    const auto reference = run_captured(image, reference_limits, std::nullopt, arguments);
    auto native_limits = limits_at(2, &cache);
    if (adjust) adjust(native_limits);
    check_same(run_captured(image, native_limits, std::nullopt, arguments), reference, label);
}

// The step limits to try for a run of `steps`: all of them when there are few,
// else the first and last 200 and one in eleven between.
std::vector<std::uint64_t> limit_points(std::uint64_t steps) {
    std::vector<std::uint64_t> points;
    for (std::uint64_t value = 1; value <= steps + 1; ++value) {
        if (steps <= 1500 || value <= 200 || value + 200 >= steps || value % 11 == 0) points.push_back(value);
    }
    return points;
}

fs::path passes_program(const Environment& environment) {
    return backend_dir(environment) / "tier3" / "passes.nra";
}

struct PassesCase {
    std::vector<std::string> arguments;
    bool sweep;
};

NEURA_TEST(tier3, programs_with_checks_the_passes_remove_match_tier_0_and_the_passes_off_at_every_step_limit) {
    if (!programs_run()) return;
    const auto image = build_program({passes_program(environment)});
    const std::vector<PassesCase> cases = {
        {{"window", "130", "-90"}, true}, {{"window", "130", "0"}, true},    {{"window", "130", "-100"}, true},
        {{"window", "130", "-101"}, true}, {{"count", "3", "30"}, true},     {{"digits", "30"}, true},
        {{"scaled", "12", "5"}, true},     {{"scaled", "100000", "100000"}, true}, {{"skipped", "5", "2147483647"}, true},
    };
    for (const auto after : {1U, 7U}) {
        for (const auto& c : cases) {
            nvm::TierCache with(image, native_options(after, true));
            nvm::TierCache without(image, native_options(after, false));
            const auto label = c.arguments[0] + " " + c.arguments[1] + " after " + std::to_string(after);
            compare_with_tier_0(image, with, label + " with the passes", c.arguments);
            compare_with_tier_0(image, without, label + " without the passes", c.arguments);
            if (!c.sweep) continue;
            const auto steps = run_captured(image, limits_at(0), std::nullopt, c.arguments).result.steps;
            for (const auto limit : limit_points(steps)) {
                const Adjust set = [&](nvm::RunLimits& limits) { limits.max_steps = limit; };
                compare_with_tier_0(image, with, label + " with the passes and " + std::to_string(limit) + " steps", c.arguments, set);
                compare_with_tier_0(image, without, label + " without the passes and " + std::to_string(limit) + " steps", c.arguments, set);
            }
        }
    }
}

NEURA_TEST(tier3, a_loop_handed_to_native_code_in_its_middle_runs_the_guard_and_a_failed_guard_deoptimizes_exactly) {
    if (!programs_run() || nvm::tier_eager() || !t3::passes_enabled()) return;
    const auto image = build_program({passes_program(environment)});
    // The loop starts at -90 and is handed to native code a few iterations
    // in, where the variable is still negative: the guard that it starts at 0
    // or more fails, native code deoptimizes at the loop head, and tier 1
    // runs on: the run is the run of tier 0.
    nvm::TierCache failing(image, native_options(7, true));
    compare_with_tier_0(image, failing, "a guard that fails at the entry in the middle", {"window", "300", "-90"});
    check(failing.statistics().native.guards > 0, "the guard ran on that entry and failed");
    // The same without a negative start: the guard passes.
    nvm::TierCache passing(image, native_options(7, true));
    compare_with_tier_0(image, passing, "a guard that holds", {"window", "300", "0"});
    check_equal(passing.statistics().native.guards, std::uint64_t{0}, "no guard failed");
    check(passing.statistics().native.steps > 0, "native code ran the loop");
    // Without the passes there are no guards at all.
    nvm::TierCache off(image, native_options(7, false));
    compare_with_tier_0(image, off, "without the passes", {"window", "300", "-90"});
    check_equal(off.statistics().native.guards, std::uint64_t{0}, "no guards without the passes");
    // The loop that starts below the index range traps at its first
    // iteration: the failed guard hands it to tier 1, which traps there.
    nvm::TierCache traps(image, native_options(1, true));
    compare_with_tier_0(image, traps, "a negative index", {"window", "300", "-101"});
    check(traps.statistics().native.guards > 0, "the guard failed before the trap");
}

NEURA_TEST(tier3, a_loop_that_ends_on_not_equal_and_starts_past_its_bound_deoptimizes_at_its_guard) {
    // NEURA_TIER3_PASSES=0 turns the guards off for every cache.
    if (!programs_run() || !t3::passes_enabled()) return;
    const auto image = build_program({passes_program(environment)});
    nvm::TierCache cache(image, native_options(1, true));
    const auto steps = [](nvm::RunLimits& limits) { limits.max_steps = 3000; };
    compare_with_tier_0(image, cache, "start past the bound", {"count", "50", "3"}, steps);
    check(cache.statistics().native.guards > 0, "the init <= bound guard failed");
    nvm::TierCache fine(image, native_options(1, true));
    compare_with_tier_0(image, fine, "start before the bound", {"count", "3", "50"});
    check_equal(fine.statistics().native.guards, std::uint64_t{0}, "and holds when the variable starts below it");
}

// The function programs of a cache's run, built without the passes.
struct Built {
    std::uint32_t function = 0;
    std::shared_ptr<const nvm::tier2::Region> region;
    t3::BuildResult result;
};

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

NEURA_TEST(tier3, the_passes_leave_every_function_program_of_the_test_programs_well_formed_and_remove_checks_from_them) {
    if (!programs_run()) return;
    d3::Counts total;
    std::size_t functions = 0;
    const auto optimize_all = [&](const std::string& directory, const std::string& name,
                                  const std::vector<std::vector<std::string>>& runs) {
        const auto image = build_program({backend_dir(environment) / directory / (name + ".nra")});
        nvm::TierCache cache(image, native_options());
        for (const auto& arguments : runs) run_captured(image, limits_at(2, &cache), std::nullopt, arguments);
        for (auto& built : build_all(image, cache)) {
            if (!built.result.program) continue;
            auto& ir = built.result.program->ir;
            t3::optimize(image, ir, true);
            const auto violations = t3::verify(image, ir);
            check(violations.empty(), name + " function " + std::to_string(built.function) + " verifies after the passes: " +
                                          (violations.empty() ? "" : violations.front()));
            for (const auto& [kind, count] : ir.pass_counts) total[kind] += count;
            ++functions;
        }
    };
    optimize_all("tier3", "passes",
                 {{"window", "130", "-90"}, {"count", "3", "30"}, {"digits", "30"}, {"scaled", "12", "5"}, {"skipped", "5", "9"}});
    optimize_all("tier3", "shapes", {{"sum", "40"}, {"make", "8"}, {"descend", "6"}, {"grow", "30"}});
    optimize_all("tier", "program-calls", {{"calls", "4"}, {"overflow", "4"}, {"index", "4"}, {"nil", "4"}, {"field", "4"}});
    optimize_all("tier", "program-helpers", {{"alloc", "9"}, {"kernel", "5"}, {"text", "5"}, {"free", "4"}, {"remove", "4"}});
    check(functions > 20, "the passes ran over many programs: " + std::to_string(functions));
    for (const auto* kind : {"elim.index", "elim.add", "elim.convert", "guard.init_nonnegative",
                             "guard.init_bounded", "licm.check", "licm.pure", "gvn.value", "fold.counter", "copy.mask"}) {
        check(total[kind] > 0, std::string("the passes did `") + kind + "` somewhere");
    }
}

} // namespace

} // namespace neura::testing
