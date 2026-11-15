// The lowering of tier 3 (tier3_lower.hpp): IR to machine IR.
//
// - Blocks are laid out with the entry blocks first, then in reverse
//   postorder with every loop's blocks together. An edge from a block with
//   several successors to a block with several predecessors, and every edge
//   that repeats a successor, gets a block of its own, so that the moves on
//   the edge have a place.
// - Every IR value is one virtual register, except the block counter C,
//   which lives in r12: its values (EntryCounter 0, CounterAdd, and phis of
//   them) must form one chain, each the successor of the last, so that the
//   counter any instruction reads is the one r12 holds. A function where that
//   is not so is refused.
// - Constants are encoded as immediates where an instruction can take one,
//   and materialized only for the operands that need a register.
// - A check's frame state becomes the instruction's state uses: the values
//   its stub stores. A write-back's frame state becomes its uses.
// - A spill home (mir::Function::spill_home) is a slot of the physical frame
//   whose current value the virtual register is wherever it is live (see
//   assign_homes).
#include "jit/tier3_lower.hpp"

#include "lift/evaluate.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace neura::jit::native {
namespace {

namespace t3 = nvm::tier3;
using t3::Op;
using lift::Opcode;

constexpr std::uint64_t bit(unsigned r) { return std::uint64_t{1} << r; }
constexpr std::uint8_t rax = 0, rcx = 1, rdx = 2, rbx = 3, rbp = 5, rsi = 6, rdi = 7, r8 = 8, r9 = 9, r10 = 10,
                       r14 = 14;
constexpr std::uint64_t allocatable_gpr = bit(rax) | bit(rcx) | bit(rdx) | bit(rbx) | bit(rbp) | bit(rsi) | bit(rdi) |
                                          bit(r8) | bit(r9) | bit(r10) | bit(r14);
constexpr std::uint64_t allocatable_xmm = 0x7FFF;
// What a call into C++ destroys of the allocatable registers (System V).
constexpr std::uint64_t caller_saved_gpr =
    bit(rax) | bit(rcx) | bit(rdx) | bit(rsi) | bit(rdi) | bit(r8) | bit(r9) | bit(r10);

[[noreturn]] void refuse(const std::string& why) { throw LoweringError(why); }

bool is_float_op(Opcode op) { return lift::float_opcode(op); }

bool hardware_float(lift::FloatType type) {
    return type == lift::FloatType::Binary32 || type == lift::FloatType::Binary64;
}

bool is_compare(Opcode op) {
    switch (op) {
    case Opcode::Equal:
    case Opcode::NotEqual:
    case Opcode::Less:
    case Opcode::LessEqual:
    case Opcode::Greater:
    case Opcode::GreaterEqual: return true;
    default: return false;
    }
}

// The blocks reachable from `roots`, in reverse postorder, and the natural
// loops among them, by header: the targets of retreating edges.
struct Cfg {
    std::vector<std::uint32_t> rpo;
    std::vector<std::uint32_t> number;  // position in rpo; none: unreachable
    std::map<std::uint32_t, std::vector<std::uint8_t>> loops;
};

Cfg analyze(const t3::Function& f, const std::vector<std::uint32_t>& roots) {
    const auto nb = static_cast<std::uint32_t>(f.blocks.size());
    Cfg cfg;
    std::vector<std::uint8_t> state(nb, 0);
    std::vector<std::uint32_t> post;
    for (const auto root : roots) {
        if (root >= nb || state[root] != 0) continue;
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{root, 0}};
        state[root] = 1;
        while (!stack.empty()) {
            auto& [b, next] = stack.back();
            const auto& succs = f.blocks[b].succs;
            if (next < succs.size()) {
                const auto s = succs[next++];
                if (s >= nb) refuse("a successor out of range");
                if (state[s] == 0) {
                    state[s] = 1;
                    stack.emplace_back(s, 0);
                }
                continue;
            }
            post.push_back(b);
            stack.pop_back();
        }
    }
    cfg.rpo.assign(post.rbegin(), post.rend());
    cfg.number.assign(nb, t3::none);
    for (std::uint32_t i = 0; i < cfg.rpo.size(); ++i) cfg.number[cfg.rpo[i]] = i;
    for (const auto u : cfg.rpo) {
        for (const auto h : f.blocks[u].succs) {
            if (cfg.number[h] > cfg.number[u]) continue;
            auto& body = cfg.loops[h];
            if (body.empty()) body.assign(nb, 0);
            body[h] = 1;
            std::vector<std::uint32_t> work;
            if (!body[u]) {
                body[u] = 1;
                work.push_back(u);
            }
            while (!work.empty()) {
                const auto x = work.back();
                work.pop_back();
                for (const auto p : f.blocks[x].preds) {
                    if (cfg.number[p] == t3::none || body[p]) continue;
                    body[p] = 1;
                    work.push_back(p);
                }
            }
        }
    }
    return cfg;
}

class Lowering {
public:
    Lowering(const t3::Function& f, const std::vector<std::uint32_t>& entries) : f_(f), entries_(entries) {}

    Lowered run() {
        if (f_.blocks.empty() || f_.entries.empty()) refuse("a function without entries");
        for (const auto& state : f_.states) {
            if (state.caller != t3::none) refuse("an inlined frame");
            if (state.kernel_steps != t3::none) refuse("inline kernel steps");
        }
        count_uses();
        find_counters();
        layout();
        simplify_phis();
        decide_constants();
        build_blocks();
        for (const auto b : order_) lower_block(b);
        // The constants phis take on edges, made at the end of the edge's
        // predecessor so that they live only across the edge.
        for (std::uint32_t m = 0; m < edge_constants_.size(); ++m) {
            for (const auto& [v, bits] : edge_constants_[m]) {
                Step step;
                step.kind = Kind::Const;
                step.imm = bits;
                current_ = m;
                emit(std::move(step)).defs.push_back(reg({v, mir::RegClass::Gpr}));
            }
        }
        for (std::uint32_t m = 0; m < edge_flags_.size(); ++m) {
            for (const auto& [v, slot] : edge_flags_[m]) {
                Step step;
                step.kind = Kind::EntryFlag;
                step.index = slot;
                current_ = m;
                emit(std::move(step)).defs.push_back(reg({v, mir::RegClass::Gpr}));
            }
        }
        check_counter();
        out_.function.allocatable_gpr = allocatable_gpr;
        out_.function.allocatable_xmm = allocatable_xmm;
        out_.function.vreg_count = static_cast<std::uint32_t>(out_.function.classes.size());
        assign_homes();
        return std::move(out_);
    }

private:
    const t3::Function& f_;
    // The IR entries this unit's code has.
    const std::vector<std::uint32_t>& entries_;
    Lowered out_;
    // The IR blocks of the unit, and the predecessors of each among them (by
    // index in Block::preds).
    std::vector<std::uint8_t> in_unit_;
    std::vector<std::vector<std::uint32_t>> unit_preds_;

    // Uses of each IR value, by operands, extras and frame states.
    std::vector<std::uint32_t> uses_;
    // The value's single user (when uses_ is 1).
    std::vector<std::uint32_t> user_;
    std::vector<std::uint8_t> counter_;
    // The IR block of each instruction.
    std::vector<std::uint32_t> block_of_;

    std::vector<std::uint32_t> order_;   // IR blocks in layout order
    std::vector<std::uint32_t> mir_of_;  // IR block -> machine block
    // By IR block and successor index: the machine block the edge enters.
    std::vector<std::vector<std::uint32_t>> edge_target_;
    std::vector<std::uint8_t> loop_head_;

    // IR value -> the value it is in this unit: a phi whose inputs from the
    // unit's predecessors are one value (or itself) is that value.
    std::vector<std::uint32_t> canon_;
    // Values whose bits are known in this unit, and the bits.
    std::vector<std::uint8_t> known_;
    std::vector<std::uint64_t> known_value_;
    std::vector<std::uint8_t> fused_;      // compares a branch encodes
    std::vector<std::uint8_t> flag_kept_;  // entry flags made at their entry
    std::vector<std::uint8_t> needed_;     // constants some operand needs in a register
    std::vector<std::uint32_t> vreg_;      // IR value -> vreg (none: not yet)

    // ---- uses -------------------------------------------------------------

    template <typename F>
    void for_each_value_use(const t3::Inst& in, F&& f) const {
        for (const auto operand : in.operands) {
            if (operand != t3::none) f(operand);
        }
        if (in.op == Op::Phi || in.op == Op::StateCall || in.op == Op::Helper || in.op == Op::Call) {
            for (std::uint32_t i = 0; i < in.count; ++i) f(f_.extras[in.extra + i]);
        } else if (in.op == Op::Gather) {
            for (std::uint32_t i = 0; i < in.count; i += 2) f(f_.extras[in.extra + i]);
        }
    }

    template <typename F>
    void for_each_state_value(std::uint32_t s, F&& f) const {
        const auto& state = f_.states[s];
        for (const auto& slot : state.slots) {
            if (slot.kind == t3::StateSlot::Kind::Value) f(static_cast<std::uint32_t>(slot.value));
        }
        for (const auto& flag : state.flags) {
            if (flag.kind == t3::StateFlag::Kind::Value) f(flag.value);
        }
    }

    void count_uses() {
        const auto n = f_.insts.size();
        uses_.assign(n, 0);
        user_.assign(n, t3::none);
        block_of_.assign(n, t3::none);
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            for (const auto id : f_.blocks[b].insts) {
                if (id >= n) refuse("an instruction out of range");
                block_of_[id] = b;
            }
        }
        const auto use = [&](std::uint32_t v, std::uint32_t by) {
            if (v >= n) refuse("an operand out of range");
            ++uses_[v];
            user_[v] = by;
        };
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                for_each_value_use(in, [&](std::uint32_t v) { use(v, id); });
                if (in.state != t3::none) {
                    for_each_state_value(in.state, [&](std::uint32_t v) { use(v, id); });
                    use(f_.states[in.state].blocks, id);
                }
            }
        }
    }

    // ---- the counter C ----------------------------------------------------

    void find_counters() {
        const auto n = f_.insts.size();
        counter_.assign(n, 0);
        for (std::uint32_t id = 0; id < n; ++id) {
            const auto& in = f_.insts[id];
            // An instruction in no block is dead (the passes leave them).
            if (block_of_[id] == t3::none) continue;
            if ((in.op == Op::EntryCounter && in.imm == 0) || in.op == Op::CounterAdd) counter_[id] = 1;
            if (in.op == Op::EntryCounter && in.imm != 0) refuse("inline kernel steps");
        }
        // A phi is a counter when its inputs are: an optimistic fixed point.
        std::vector<std::uint32_t> phis;
        for (std::uint32_t id = 0; id < n; ++id) {
            const auto& in = f_.insts[id];
            if (in.op != Op::Phi || block_of_[id] == t3::none) continue;
            bool any = false;
            for (std::uint32_t i = 0; i < in.count; ++i) {
                const auto v = f_.extras[in.extra + i];
                any = any || (v < n && counter_[v]);
            }
            if (any) {
                counter_[id] = 1;
                phis.push_back(id);
            }
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto id : phis) {
                if (!counter_[id]) continue;
                const auto& in = f_.insts[id];
                for (std::uint32_t i = 0; i < in.count; ++i) {
                    const auto v = f_.extras[in.extra + i];
                    if (v != id && !counter_[v]) {
                        counter_[id] = 0;
                        changed = true;
                        break;
                    }
                }
            }
        }
        for (const auto id : phis) {
            if (!counter_[id]) refuse("a phi joins the block counter with another value");
        }
    }

    // Each instruction reads the counter r12 holds: the chain of counter
    // values is linear. Checked over every edge.
    void check_counter() {
        const auto nb = f_.blocks.size();
        std::vector<std::uint32_t> start(nb, t3::none), end(nb, t3::none);
        std::vector<std::uint8_t> done(nb, 0);
        for (const auto b : order_) {
            const auto& block = f_.blocks[b];
            std::uint32_t current = t3::none;
            std::uint32_t phi = t3::none;
            for (const auto id : block.insts) {
                if (f_.insts[id].op != Op::Phi) break;
                if (counter_[id]) {
                    if (phi != t3::none) refuse("two counter phis in a block");
                    phi = id;
                }
            }
            if (phi != t3::none) {
                current = phi;
            } else {
                for (const auto p : block.preds) {
                    if (done[p]) {
                        current = end[p];
                        break;
                    }
                }
            }
            start[b] = current;
            const auto expect = [&](std::uint32_t v) {
                if (v != current || v == t3::none) refuse("the block counter is not the one in r12");
            };
            for (const auto id : block.insts) {
                const auto& in = f_.insts[id];
                if (in.op == Op::Phi) {
                    if (!counter_[id]) {
                        for (std::uint32_t i = 0; i < in.count; ++i) {
                            if (counter_[f_.extras[in.extra + i]]) refuse("the block counter is a phi input");
                        }
                    }
                    continue;
                }
                if (in.state != t3::none) expect(f_.states[in.state].blocks);
                switch (in.op) {
                case Op::EntryCounter: current = id; break;
                case Op::CounterAdd:
                    expect(in.operands[0]);
                    current = id;
                    break;
                case Op::Budget:
                case Op::Helper:
                case Op::Call:
                case Op::Return:
                case Op::Leave:
                case Op::Deopt:
                    expect(in.operands[0]);
                    for (int k = 1; k < 3; ++k) {
                        if (in.operands[k] != t3::none && counter_[in.operands[k]]) refuse("the block counter as a value");
                    }
                    for_each_extra(in, [&](std::uint32_t v) {
                        if (counter_[v]) refuse("the block counter as an argument");
                    });
                    break;
                default:
                    for_each_value_use(in, [&](std::uint32_t v) {
                        if (counter_[v]) refuse("the block counter as a value");
                    });
                    break;
                }
            }
            end[b] = current;
            done[b] = 1;
        }
        for (std::uint32_t s = 0; s < nb; ++s) {
            const auto& block = f_.blocks[s];
            if (!done[s]) continue;
            const auto phi = start[s] != t3::none && f_.insts[start[s]].op == Op::Phi && counter_[start[s]] &&
                                     block_of_[start[s]] == s
                                 ? start[s]
                                 : t3::none;
            for (std::size_t i = 0; i < block.preds.size(); ++i) {
                const auto p = block.preds[i];
                if (!done[p]) continue;
                const auto arriving = end[p];
                const auto wanted = phi != t3::none ? f_.extras[f_.insts[phi].extra + i] : start[s];
                if (arriving != wanted) refuse("the block counter differs along an edge");
            }
        }
    }

    template <typename F>
    void for_each_extra(const t3::Inst& in, F&& f) const {
        if (in.op == Op::StateCall || in.op == Op::Helper || in.op == Op::Call) {
            for (std::uint32_t i = 0; i < in.count; ++i) f(f_.extras[in.extra + i]);
        }
    }

    // ---- layout -----------------------------------------------------------

    void layout() {
        const auto nb = static_cast<std::uint32_t>(f_.blocks.size());
        const auto entries = static_cast<std::uint32_t>(f_.entries.size());
        for (std::uint32_t e = 0; e < entries; ++e) {
            if (f_.entries[e].ir_block != e || !f_.blocks[e].preds.empty()) refuse("entry blocks are not first");
        }
        for (std::uint32_t b = entries; b < nb; ++b) {
            if (f_.blocks[b].entry != t3::none) refuse("an entry block after the first blocks");
        }
        std::vector<std::uint32_t> roots;
        for (const auto e : entries_) {
            if (e >= entries) refuse("an entry out of range");
            roots.push_back(e);
        }
        const auto cfg = analyze(f_, roots);
        in_unit_.assign(nb, 0);
        for (const auto b : cfg.rpo) in_unit_[b] = 1;
        unit_preds_.assign(nb, {});
        for (const auto b : cfg.rpo) {
            const auto& preds = f_.blocks[b].preds;
            for (std::uint32_t i = 0; i < preds.size(); ++i) {
                if (in_unit_[preds[i]]) unit_preds_[b].push_back(i);
            }
        }
        loop_head_.assign(nb, 0);
        for (const auto& [header, body] : cfg.loops) loop_head_[header] = 1;
        std::vector<std::uint8_t> placed(nb, 0);
        for (const auto e : entries_) {
            order_.push_back(e);
            placed[e] = 1;
        }
        // Places the blocks of `members` in reverse postorder, each inner
        // loop's blocks together when its header comes.
        std::function<void(const std::vector<std::uint8_t>*, std::uint32_t)> place =
            [&](const std::vector<std::uint8_t>* members, std::uint32_t header) {
                for (const auto v : cfg.rpo) {
                    if (placed[v] || (members && !(*members)[v])) continue;
                    const auto found = cfg.loops.find(v);
                    if (v != header && found != cfg.loops.end()) {
                        place(&found->second, v);
                        continue;
                    }
                    placed[v] = 1;
                    order_.push_back(v);
                }
            };
        place(nullptr, t3::none);
    }

    // ---- phis of one value ---------------------------------------------------

    std::uint32_t canon(std::uint32_t v) {
        if (v == t3::none || v >= canon_.size()) return v;
        while (canon_[v] != v) {
            canon_[v] = canon_[canon_[v]];
            v = canon_[v];
        }
        return v;
    }

    // A loop head that is an entry joins each value with the entry's load;
    // in a unit without that entry, or in the entry's own unit without the
    // code before the loop, the phi has one value (as the builder's removal
    // of trivial phis, Braun et al., over this unit's edges).
    void simplify_phis() {
        canon_.resize(f_.insts.size());
        for (std::uint32_t v = 0; v < canon_.size(); ++v) canon_[v] = v;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto b : order_) {
                for (const auto id : f_.blocks[b].insts) {
                    const auto& in = f_.insts[id];
                    if (in.op != Op::Phi) break;
                    if (counter_[id] || canon(id) != id) continue;
                    std::uint32_t same = t3::none;
                    bool trivial = true;
                    for (const auto i : unit_preds_[b]) {
                        const auto x = canon(f_.extras[in.extra + i]);
                        if (x == id || x == same) continue;
                        if (same != t3::none) trivial = false;
                        same = x;
                    }
                    if (trivial && same != t3::none) {
                        canon_[id] = same;
                        changed = true;
                    }
                }
            }
        }
        // Known constants: each Const, and each phi whose inputs over the
        // unit's edges are constants of one value (the initialized flag of a
        // slot assigned on every path, a phi of a Const 1 per block, is the
        // usual one).
        known_.assign(f_.insts.size(), 0);
        known_value_.assign(f_.insts.size(), 0);
        for (std::uint32_t v = 0; v < f_.insts.size(); ++v) {
            if (f_.insts[v].op == Op::Const) {
                known_[v] = 1;
                known_value_[v] = f_.insts[v].imm;
            }
        }
        // The initialized flag of a slot the entry's code reads before writing
        // is 1 there: the run enters only when it is (run_native_program, and
        // tier3_callee_code for entry 0 of a callee, whose reads are
        // parameters).
        if (f_.region != nullptr) {
            for (const auto e : entries_) {
                if (e >= f_.region->entries.size()) continue;
                const auto& reads = f_.region->entries[e].reads;
                for (const auto id : f_.blocks[e].insts) {
                    const auto& in = f_.insts[id];
                    if (in.op == Op::EntryFlag && std::find(reads.begin(), reads.end(), in.imm) != reads.end()) {
                        known_[id] = 1;
                        known_value_[id] = 1;
                    }
                }
            }
        }
        std::vector<std::uint32_t> phis;
        for (const auto b : order_) {
            for (const auto id : f_.blocks[b].insts) {
                if (f_.insts[id].op != Op::Phi) break;
                if (!counter_[id] && canon(id) == id) phis.push_back(id);
            }
        }
        // Optimistic: every phi is a constant until an input says otherwise.
        std::vector<std::uint8_t> maybe(f_.insts.size(), 0);
        for (const auto id : phis) maybe[id] = 1;
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto id : phis) {
                if (!maybe[id]) continue;
                const auto& in = f_.insts[id];
                bool have = false;
                std::uint64_t value = 0;
                bool constant = true;
                for (const auto i : unit_preds_[block_of_[id]]) {
                    const auto x = canon(f_.extras[in.extra + i]);
                    if (x == id || (maybe[x] && !known_[x])) continue;
                    if (!known_[x] || (have && known_value_[x] != value)) {
                        constant = false;
                        break;
                    }
                    have = true;
                    value = known_value_[x];
                }
                if (!constant || !have) {
                    // No longer a constant: no phi may take it for one.
                    maybe[id] = 0;
                    known_[id] = 0;
                    known_value_[id] = 0;
                    changed = true;
                } else if (!known_[id] || known_value_[id] != value) {
                    known_[id] = 1;
                    known_value_[id] = value;
                    changed = true;
                }
            }
        }
        for (const auto id : phis) {
            if (!maybe[id]) {
                known_[id] = 0;
                known_value_[id] = 0;
            }
        }
    }

    bool known(std::uint32_t v) { return v != t3::none && known_[canon(v)] != 0; }
    std::uint64_t known_value(std::uint32_t v) { return known_value_[canon(v)]; }

    // ---- constants and fused compares ---------------------------------------

    std::uint8_t width_of(std::uint32_t v) const {
        const auto& type = f_.insts[v].type;
        switch (type.type) {
        case t3::Type::Int:
        case t3::Type::F32:
        case t3::Type::F64: return type.width;
        case t3::Type::Ref: return 64;
        default: refuse("a value of a type the code generator does not handle");
        }
    }

    bool is_const(std::uint32_t v) { return known(v); }

    Step lift_step(std::uint32_t id) {
        const auto& in = f_.insts[id];
        Step step;
        step.kind = Kind::Lift;
        step.inst = id;
        step.checked = (in.flags & t3::Checked) != 0;
        const auto n = lift_arity(in.lifted);
        if (n >= 1) step.a_width = width_of(in.operands[0]);
        if (n >= 2) {
            step.b_width = width_of(in.operands[1]);
            if (is_const(in.operands[1])) {
                step.b_constant = true;
                step.b_value = known_value(in.operands[1]);
            }
        }
        return step;
    }

    // Compares whose only use is the branch that ends their block, and the
    // constants that some operand needs in a register.
    void decide_constants() {
        const auto n = f_.insts.size();
        fused_.assign(n, 0);
        needed_.assign(n, 0);
        for (const auto b : order_) {
            const auto& insts = f_.blocks[b].insts;
            if (insts.empty()) continue;
            const auto last = insts.back();
            const auto& branch = f_.insts[last];
            if (branch.op != Op::Branch) continue;
            const auto c = branch.operands[0];
            if (c == t3::none || uses_[c] != 1 || block_of_[c] != b) continue;
            const auto& compare = f_.insts[c];
            if (compare.op != Op::Lift || (compare.flags & t3::Checked) != 0 || !is_compare(compare.lifted.op)) continue;
            if (lift_shape(compare.lifted, lift_step(c)).evaluate) continue;
            fused_[c] = 1;
        }
        // A known value is materialized only for an operand that needs it
        // in a register (frame states and write-backs store its bits), at
        // its definition (a phi's: the start of its block).
        const auto need = [&](std::uint32_t v) {
            if (is_const(v)) needed_[canon(v)] = 1;
        };
        // Entry flags used other than as phi inputs (which read them again
        // on the edge): states that name them, and operands.
        flag_kept_.assign(n, 0);
        const auto keep = [&](std::uint32_t v) {
            if (v != t3::none) flag_kept_[canon(v)] = 1;
        };
        for (const auto b : order_) {
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                if (in.op == Op::Phi) continue;
                for_each_value_use(in, keep);
                if (in.state == t3::none) continue;
                for (const auto& flag : f_.states[in.state].flags) {
                    if (flag.kind == t3::StateFlag::Kind::Value) keep(flag.value);
                }
            }
        }
        for (const auto b : order_) {
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                // A phi's known inputs are made on its edges.
                if (in.op == Op::Phi) continue;
                if (in.op == Op::Lift) {
                    const auto step = lift_step(id);
                    const auto shape = lift_shape(in.lifted, step);
                    need(in.operands[0]);
                    if (shape.b != LiftShape::Use::Immediate && shape.b != LiftShape::Use::None) need(in.operands[1]);
                    continue;
                }
                if (in.op == Op::Branch && fused_[in.operands[0]]) continue;
                if (in.op == Op::WriteBack) continue;
                for_each_value_use(in, need);
            }
        }
    }

    // ---- blocks -----------------------------------------------------------

    void build_blocks() {
        const auto nb = f_.blocks.size();
        auto& blocks = out_.function.blocks;
        mir_of_.assign(nb, t3::none);
        edge_target_.assign(nb, {});
        // Machine blocks in layout order: each IR block, then the blocks of
        // its edges that need one.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> split_of;  // per machine block: (IR block, succ) or none
        for (const auto b : order_) {
            mir_of_[b] = static_cast<std::uint32_t>(blocks.size());
            blocks.emplace_back();
            out_.loop_head.push_back(loop_head_[b] != 0);
            split_of.emplace_back(t3::none, 0);
            const auto& succs = f_.blocks[b].succs;
            edge_target_[b].assign(succs.size(), t3::none);
            for (std::uint32_t k = 0; k < succs.size(); ++k) {
                const auto s = succs[k];
                const bool repeated = std::count(succs.begin(), succs.end(), s) > 1;
                const bool critical = succs.size() > 1 && unit_preds_[s].size() > 1;
                if (!repeated && !critical) continue;
                edge_target_[b][k] = static_cast<std::uint32_t>(blocks.size());
                blocks.emplace_back();
                out_.loop_head.push_back(false);
                split_of.emplace_back(b, k);
            }
        }
        for (const auto b : order_) {
            const auto& succs = f_.blocks[b].succs;
            for (std::uint32_t k = 0; k < succs.size(); ++k) {
                const auto target = mir_of_[succs[k]];
                if (edge_target_[b][k] == t3::none) {
                    edge_target_[b][k] = target;
                    blocks[mir_of_[b]].succs.push_back(target);
                } else {
                    const auto split = edge_target_[b][k];
                    blocks[mir_of_[b]].succs.push_back(split);
                    blocks[split].preds.push_back(mir_of_[b]);
                    blocks[split].succs.push_back(target);
                }
            }
        }
        // Predecessors of each IR block's machine block, in IR pred order:
        // the i-th occurrence of p among s's preds is p's i-th edge to s.
        for (const auto s : order_) {
            const auto& preds = f_.blocks[s].preds;
            for (const auto i : unit_preds_[s]) {
                const auto p = preds[i];
                const auto occurrence = std::count(preds.begin(), preds.begin() + static_cast<std::ptrdiff_t>(i), p);
                std::ptrdiff_t seen = 0;
                std::uint32_t from = t3::none;
                const auto& succs = f_.blocks[p].succs;
                for (std::uint32_t k = 0; k < succs.size(); ++k) {
                    if (succs[k] != s) continue;
                    if (seen++ == occurrence) {
                        from = edge_target_[p][k] == mir_of_[s] ? mir_of_[p] : edge_target_[p][k];
                        break;
                    }
                }
                if (from == t3::none) refuse("a predecessor without its edge");
                blocks[mir_of_[s]].preds.push_back(from);
            }
        }
        for (const auto e : entries_) out_.entry_blocks.push_back(mir_of_[e]);
    }

    // ---- values -----------------------------------------------------------

    mir::VReg vreg(std::uint32_t v) {
        if (v == t3::none || v >= f_.insts.size()) refuse("a missing value");
        v = canon(v);
        if (counter_[v]) refuse("the block counter as a value");
        if (vreg_.empty()) vreg_.assign(f_.insts.size(), t3::none);
        if (vreg_[v] == t3::none) {
            (void)width_of(v);
            vreg_[v] = static_cast<std::uint32_t>(out_.function.classes.size());
            out_.function.classes.push_back(mir::RegClass::Gpr);
            out_.function.spill_home.push_back(-1);
            value_of_.push_back(v);
        }
        return {vreg_[v], mir::RegClass::Gpr};
    }
    std::vector<std::uint32_t> value_of_;  // vreg -> IR value
    // By machine block: the constants to make at its end (vreg, bits).
    std::vector<std::vector<std::pair<std::uint32_t, std::uint64_t>>> edge_constants_;
    // By machine block: the initialized flags to read again at its end
    // (vreg, slot).
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> edge_flags_;

    // A virtual register of its own for a copy of IR value `v`.
    std::uint32_t fresh(std::uint32_t v) {
        const auto id = static_cast<std::uint32_t>(out_.function.classes.size());
        out_.function.classes.push_back(mir::RegClass::Gpr);
        out_.function.spill_home.push_back(-1);
        value_of_.push_back(v);
        return id;
    }

    static mir::Operand reg(mir::VReg v) { return {v, mir::Constraint::Register, 0, 0}; }
    static mir::Operand any(mir::VReg v) { return {v, mir::Constraint::Any, 0, 0}; }
    static mir::Operand fixed(mir::VReg v, std::uint8_t r) { return {v, mir::Constraint::Fixed, r, 0}; }
    static mir::Operand same(mir::VReg v) { return {v, mir::Constraint::SameAsUse, 0, 0}; }

    std::uint32_t current_ = 0;  // machine block being filled

    mir::Inst& emit(Step step) {
        mir::Inst inst;
        inst.opcode = static_cast<std::uint32_t>(out_.steps.size());
        out_.steps.push_back(std::move(step));
        auto& insts = out_.function.blocks[current_].insts;
        insts.push_back(std::move(inst));
        return insts.back();
    }

    // The state uses of a check: every value its frame state names, one use
    // per virtual register.
    void add_state(mir::Inst& inst, std::uint32_t s) {
        auto& step = out_.steps[inst.opcode];
        step.state = s;
        std::vector<std::uint32_t> named;
        std::vector<std::uint32_t> registers;
        for_each_state_value(s, [&](std::uint32_t v) {
            if (std::find(named.begin(), named.end(), v) != named.end()) return;
            named.push_back(v);
            if (known(v)) {
                step.state_constants.emplace_back(v, known_value(v));
                return;
            }
            const auto r = vreg(v).id;
            auto use = static_cast<std::uint32_t>(std::find(registers.begin(), registers.end(), r) - registers.begin());
            if (use == registers.size()) {
                registers.push_back(r);
                inst.state_uses.push_back(any(vreg(v)));
            }
            step.state_values.emplace_back(use, v);
        });
    }

    void add_arguments(mir::Inst& inst, const t3::Inst& in) {
        for (std::uint32_t i = 0; i < in.count; ++i) inst.uses.push_back(any(vreg(f_.extras[in.extra + i])));
        out_.argument_words = std::max(out_.argument_words, in.count);
    }

    void lower_block(std::uint32_t b) {
        const auto& block = f_.blocks[b];
        current_ = mir_of_[b];
        // Phis (the counter's live in r12).
        for (const auto id : block.insts) {
            const auto& in = f_.insts[id];
            if (in.op != Op::Phi) break;
            if (counter_[id] || canon(id) != id) continue;
            if (known(id)) {
                if (!needed_[id]) continue;
                Step step;
                step.kind = Kind::Const;
                step.inst = id;
                step.imm = known_value(id);
                emit(std::move(step)).defs.push_back(reg(vreg(id)));
                continue;
            }
            mir::Phi phi;
            phi.def = vreg(id);
            if (in.count != block.preds.size()) refuse("a phi without one operand per predecessor");
            const auto& preds = out_.function.blocks[current_].preds;
            for (std::size_t k = 0; k < unit_preds_[b].size(); ++k) {
                const auto input = f_.extras[in.extra + unit_preds_[b][k]];
                if (f_.insts[canon(input)].op == Op::EntryFlag && !known(input)) {
                    // A slot's initialized flag at an entry, where the slot
                    // is not assigned on the way to this edge: the frame
                    // still holds it (it is written only after the slot is
                    // assigned), so it is read again at the end of the
                    // predecessor rather than kept from the entry.
                    const auto v = fresh(canon(input));
                    if (edge_flags_.size() < out_.function.blocks.size()) edge_flags_.resize(out_.function.blocks.size());
                    edge_flags_[preds[k]].emplace_back(v, static_cast<std::uint32_t>(f_.insts[canon(input)].imm));
                    phi.inputs.push_back({v, mir::RegClass::Gpr});
                    continue;
                }
                if (!known(input)) {
                    phi.inputs.push_back(vreg(input));
                    continue;
                }
                // A known input: a constant of its own at the end of the
                // predecessor.
                const auto v = fresh(canon(input));
                if (edge_constants_.size() < out_.function.blocks.size()) edge_constants_.resize(out_.function.blocks.size());
                edge_constants_[preds[k]].emplace_back(v, known_value(input));
                phi.inputs.push_back({v, mir::RegClass::Gpr});
            }
            out_.function.blocks[current_].phis.push_back(std::move(phi));
        }
        for (const auto id : block.insts) {
            const auto& in = f_.insts[id];
            if (in.op != Op::Phi) lower(id, in);
        }
    }

    void lower(std::uint32_t id, const t3::Inst& in) {
        switch (in.op) {
        case Op::Const: {
            if (!needed_[id]) return;
            Step step;
            step.kind = Kind::Const;
            step.inst = id;
            step.imm = in.imm;
            emit(std::move(step)).defs.push_back(reg(vreg(id)));
            return;
        }
        case Op::RunConst: {
            Step step;
            step.kind = Kind::RunConst;
            step.inst = id;
            step.index = static_cast<std::uint32_t>(in.imm);
            step.width = width_of(id);
            emit(std::move(step)).defs.push_back(reg(vreg(id)));
            return;
        }
        case Op::EntryLoad:
        case Op::EntryFlag: {
            if (in.imm >= f_.slot_count) refuse("a slot out of range");
            if (in.op == Op::EntryFlag && !known(id) && !flag_kept_[id]) return;
            if (known(id)) {
                // Its bits are known: a constant where an operand needs it.
                if (!needed_[id]) return;
                Step step;
                step.kind = Kind::Const;
                step.inst = id;
                step.imm = known_value(id);
                emit(std::move(step)).defs.push_back(reg(vreg(id)));
                return;
            }
            Step step;
            step.kind = in.op == Op::EntryLoad ? Kind::EntryLoad : Kind::EntryFlag;
            step.inst = id;
            step.index = static_cast<std::uint32_t>(in.imm);
            auto& inst = emit(std::move(step));
            inst.defs.push_back(in.op == Op::EntryLoad ? any(vreg(id)) : reg(vreg(id)));
            return;
        }
        case Op::EntryCounter:
            // r12 holds it: set by the trampoline or the caller at an entry,
            // and by the callee after a call.
            return;
        case Op::Phi: return;
        case Op::Lift: {
            if (fused_[id]) return;
            auto step = lift_step(id);
            const auto shape = lift_shape(in.lifted, step);
            const bool checked = (in.flags & t3::Checked) != 0;
            step.reason = static_cast<std::uint8_t>(t3::Reason::Checks);
            auto& inst = emit(std::move(step));
            add_lift_operands(inst, in, shape, true);
            if (checked) add_state(inst, in.state);
            return;
        }
        case Op::Mask: {
            Step step;
            step.kind = Kind::Mask;
            step.inst = id;
            step.width = static_cast<std::uint8_t>(std::min<std::uint64_t>(in.imm, 64));
            step.a_width = width_of(in.operands[0]);
            auto& inst = emit(std::move(step));
            inst.uses.push_back(reg(vreg(in.operands[0])));
            inst.defs.push_back(same(vreg(id)));
            return;
        }
        case Op::Gather: {
            Step step;
            step.kind = Kind::Gather;
            step.inst = id;
            std::vector<mir::Operand> uses;
            for (std::uint32_t i = 0; i + 1 < in.count; i += 2) {
                const auto bit_number = f_.extras[in.extra + i + 1];
                if (bit_number >= 64) refuse("a gathered bit outside a register");
                uses.push_back(any(vreg(f_.extras[in.extra + i])));
                step.bits.push_back(bit_number);
            }
            if (step.bits.size() > 64) refuse("a gather wider than a register");
            auto& inst = emit(std::move(step));
            inst.uses = std::move(uses);
            inst.defs.push_back(reg(vreg(id)));
            inst.temps_gpr = 1;
            return;
        }
        case Op::CounterAdd: {
            Step step;
            step.kind = Kind::CounterAdd;
            step.inst = id;
            step.imm = in.imm;
            emit(std::move(step));
            return;
        }
        case Op::Budget: {
            Step step;
            step.kind = Kind::Budget;
            step.inst = id;
            step.imm = in.imm;
            step.reason = static_cast<std::uint8_t>(t3::Reason::Budget);
            add_state(emit(std::move(step)), in.state);
            return;
        }
        case Op::SwitchCheck:
        case Op::Guard: {
            Step step;
            step.kind = in.op == Op::SwitchCheck ? Kind::SwitchCheck : Kind::Guard;
            step.inst = id;
            step.imm = in.imm;
            step.reason = static_cast<std::uint8_t>(in.op == Op::SwitchCheck ? t3::Reason::Selectors : t3::Reason::Guards);
            auto& inst = emit(std::move(step));
            inst.uses.push_back(reg(vreg(in.operands[0])));
            add_state(inst, in.state);
            return;
        }
        case Op::StateCall: {
            Step step;
            step.inst = id;
            if ((in.flags & t3::CheckReference) != 0) {
                step.kind = Kind::CheckReference;
                step.index = static_cast<std::uint32_t>(in.imm);
                step.reason = static_cast<std::uint8_t>(t3::Reason::References);
                auto& inst = emit(std::move(step));
                inst.uses.push_back(fixed(vreg(in.operands[0]), rdx));
                inst.clobbers_gpr = caller_saved_gpr;
                inst.clobbers_xmm = allocatable_xmm;
                inst.is_call = true;
                add_state(inst, in.state);
                return;
            }
            step.kind = Kind::StateCall;
            step.index = in.aux;
            step.reason = static_cast<std::uint8_t>(t3::Reason::StateTraps);
            if (in.aux >= f_.exits.size()) refuse("an exit out of range");
            auto& inst = emit(std::move(step));
            add_arguments(inst, in);
            if (in.type.type != t3::Type::None) inst.defs.push_back(fixed(vreg(id), rax));
            inst.clobbers_gpr = caller_saved_gpr;
            inst.clobbers_xmm = allocatable_xmm;
            inst.is_call = true;
            add_state(inst, in.state);
            return;
        }
        case Op::WriteBack: {
            Step step;
            step.kind = Kind::WriteBack;
            step.inst = id;
            step.state = in.state;
            auto& inst = emit(std::move(step));
            // Uses in the order of the state: slot values, then flag values.
            const auto& state = f_.states[in.state];
            if (state.slots.size() != f_.slot_count || state.flags.size() != f_.slot_count) {
                refuse("a frame state without one entry per slot");
            }
            auto& written = out_.steps[inst.opcode];
            const auto value = [&](std::uint32_t v) {
                if (known(v)) {
                    written.state_constants.emplace_back(v, known_value(v));
                } else {
                    inst.uses.push_back(any(vreg(v)));
                }
            };
            for (const auto& slot : state.slots) {
                if (slot.kind == t3::StateSlot::Kind::Value) value(static_cast<std::uint32_t>(slot.value));
            }
            for (const auto& flag : state.flags) {
                if (flag.kind == t3::StateFlag::Kind::Value) value(flag.value);
            }
            inst.temps_gpr = 1;
            return;
        }
        case Op::Helper:
        case Op::Call: {
            Step step;
            step.kind = in.op == Op::Helper ? Kind::Helper : Kind::Call;
            step.inst = id;
            step.index = in.aux;
            step.imm = in.imm;
            if (in.aux >= f_.exits.size()) refuse("an exit out of range");
            auto& inst = emit(std::move(step));
            add_arguments(inst, in);
            inst.defs.push_back(fixed(vreg(id), rax));
            inst.clobbers_gpr = in.op == Op::Helper ? caller_saved_gpr : allocatable_gpr;
            inst.clobbers_xmm = allocatable_xmm;
            inst.is_call = true;
            return;
        }
        case Op::Return: {
            Step step;
            step.kind = Kind::Return;
            step.inst = id;
            step.reason = static_cast<std::uint8_t>(t3::Reason::Exits);
            auto& inst = emit(std::move(step));
            if (in.operands[1] != t3::none) inst.uses.push_back(fixed(vreg(in.operands[1]), rsi));
            inst.clobbers_gpr = caller_saved_gpr;
            inst.clobbers_xmm = allocatable_xmm;
            inst.is_call = true;
            add_state(inst, in.state);
            return;
        }
        case Op::Leave: {
            Step step;
            step.kind = Kind::Leave;
            step.inst = id;
            step.imm = in.imm;
            auto& inst = emit(std::move(step));
            inst.clobbers_gpr = caller_saved_gpr;
            inst.clobbers_xmm = allocatable_xmm;
            inst.is_call = true;
            return;
        }
        case Op::Deopt: {
            Step step;
            step.kind = Kind::Deopt;
            step.inst = id;
            step.reason = static_cast<std::uint8_t>(t3::Reason::Exits);
            add_state(emit(std::move(step)), in.state);
            return;
        }
        case Op::Jump:
            if (f_.blocks[block_of_[id]].succs.size() != 1) refuse("a jump without one successor");
            return;
        case Op::Branch: {
            if (f_.blocks[block_of_[id]].succs.size() != 2) refuse("a branch without two successors");
            const auto c = in.operands[0];
            if (fused_[c]) {
                auto step = lift_step(c);
                step.kind = Kind::CompareBranch;
                const auto shape = lift_shape(f_.insts[c].lifted, step);
                auto& inst = emit(std::move(step));
                add_lift_operands(inst, f_.insts[c], shape, false);
                return;
            }
            Step step;
            step.kind = Kind::Branch;
            step.inst = id;
            emit(std::move(step)).uses.push_back(reg(vreg(c)));
            return;
        }
        case Op::Switch: {
            Step step;
            step.kind = Kind::Switch;
            step.inst = id;
            step.imm = f_.blocks[block_of_[id]].succs.size();
            emit(std::move(step)).uses.push_back(reg(vreg(in.operands[0])));
            return;
        }
        case Op::Dispatch: {
            if (in.imm >= f_.dispatch_tables.size()) refuse("a dispatch table out of range");
            const auto& table = f_.dispatch_tables[in.imm];
            const auto succs = f_.blocks[block_of_[id]].succs.size();
            if (table.fallback >= succs) refuse("a dispatch exit out of range");
            for (const auto& [tag, exit] : table.exits) {
                if (exit >= succs) refuse("a dispatch exit out of range");
            }
            Step step;
            step.kind = Kind::Dispatch;
            step.inst = id;
            step.imm = in.imm;
            emit(std::move(step)).uses.push_back(reg(vreg(in.operands[0])));
            return;
        }
        }
        refuse("an operation the code generator does not handle");
    }

    void add_lift_operands(mir::Inst& inst, const t3::Inst& in, const LiftShape& shape, bool with_def) {
        using U = LiftShape::Use;
        using D = LiftShape::Def;
        const auto operand = [&](U use, std::uint32_t v) {
            switch (use) {
            case U::None:
            case U::Immediate: return;
            case U::Register: inst.uses.push_back(reg(vreg(v))); return;
            case U::Rax: inst.uses.push_back(fixed(vreg(v), rax)); return;
            case U::Rcx: inst.uses.push_back(fixed(vreg(v), rcx)); return;
            }
        };
        operand(shape.a, in.operands[0]);
        operand(shape.b, in.operands[1]);
        const auto self = static_cast<std::uint32_t>(&in - f_.insts.data());
        switch (with_def ? shape.def : D::None) {
        case D::None: break;
        case D::Register: inst.defs.push_back(reg(vreg(self))); break;
        case D::SameAsA: inst.defs.push_back(same(vreg(self))); break;
        case D::Rax: inst.defs.push_back(fixed(vreg(self), rax)); break;
        case D::Rdx: inst.defs.push_back(fixed(vreg(self), rdx)); break;
        }
        inst.temps_gpr = shape.temps;
        inst.temps_xmm = shape.xmm_temps;
        inst.clobbers_gpr = shape.clobbers;
    }

    // ---- spill homes ----------------------------------------------------------

    // A virtual register gets the frame slot r of the physical frame as its
    // stack location (its spill home, design 8.3) only when every spill of
    // it there is a valid write-back:
    //
    // 1. wherever it is live at an instruction that shows the frame to
    //    reference code (WriteBack, Helper, Call), that instruction's state
    //    says slot r holds this value, or that r is unchanged and the value
    //    is r's entry value;
    // 2. wherever it is live at a check, the check's state does not say r is
    //    unchanged unless the value is r's entry value (the stub would
    //    leave the spilled value in the frame);
    // 3. it is not live after a call whose result slot is r (the callee's
    //    return writes r);
    // 4. no state reachable from its definition says r is unchanged, unless
    //    it is r's entry value: a spill writes slot r, and the write stays
    //    after the value dies, where the builder's rule for unchanged entries
    //    (no WriteBack or call result may have written the slot) does not
    //    see it. This is a forward dataflow over the frame slots from every
    //    definition of a value with home r other than r's entry load, with
    //    nothing that ends it; a slot where it reaches a state that says the
    //    slot is unchanged (or a Leave) keeps no home but its entry values.
    //
    // Rule 1 is "r's current SSA value is this value wherever it is live",
    // read from the frame states; rules 2 to 4 keep every state's unchanged
    // entries true of the frame.
    void assign_homes() {
        auto& fn = out_.function;
        const auto nv = fn.vreg_count;
        if (nv == 0) return;
        const auto nb = fn.blocks.size();
        const std::size_t words = (nv + 63) / 64;
        using Bits = std::vector<std::uint64_t>;
        const auto set = [](Bits& b, std::uint32_t v) { b[v / 64] |= std::uint64_t{1} << (v % 64); };
        const auto clear = [](Bits& b, std::uint32_t v) { b[v / 64] &= ~(std::uint64_t{1} << (v % 64)); };
        const auto has = [](const Bits& b, std::uint32_t v) { return ((b[v / 64] >> (v % 64)) & 1U) != 0; };
        // Liveness: live out of each block (phi inputs live out of their
        // predecessor), to a fixed point.
        std::vector<Bits> live_in(nb, Bits(words, 0)), live_out(nb, Bits(words, 0));
        const auto transfer = [&](std::uint32_t b, Bits live) {
            const auto& block = fn.blocks[b];
            for (auto j = block.insts.size(); j > 0; --j) {
                const auto& inst = block.insts[j - 1];
                for (const auto& d : inst.defs) clear(live, d.vreg.id);
                for (const auto& u : inst.uses) set(live, u.vreg.id);
                for (const auto& u : inst.state_uses) set(live, u.vreg.id);
            }
            for (const auto& phi : block.phis) clear(live, phi.def.id);
            return live;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (auto b = nb; b > 0; --b) {
                const auto block_id = static_cast<std::uint32_t>(b - 1);
                const auto& block = fn.blocks[block_id];
                Bits out(words, 0);
                for (const auto s : block.succs) {
                    for (std::size_t w = 0; w < words; ++w) out[w] |= live_in[s][w];
                    const auto& preds = fn.blocks[s].preds;
                    const auto k = static_cast<std::size_t>(std::find(preds.begin(), preds.end(), block_id) - preds.begin());
                    for (const auto& phi : fn.blocks[s].phis) set(out, phi.inputs.at(k).id);
                }
                auto in = transfer(block_id, out);
                if (out != live_out[block_id] || in != live_in[block_id]) {
                    live_out[block_id] = std::move(out);
                    live_in[block_id] = std::move(in);
                    changed = true;
                }
            }
        }
        // Candidates: the slot whose entry load the value is, and the slots
        // states name it for.
        const auto entry_slot = [&](std::uint32_t v) -> std::uint32_t {
            const auto& in = f_.insts[value_of_[v]];
            return in.op == Op::EntryLoad ? static_cast<std::uint32_t>(in.imm) : t3::none;
        };
        std::vector<std::vector<std::uint32_t>> candidates(nv);
        for (std::uint32_t v = 0; v < nv; ++v) {
            if (entry_slot(v) != t3::none) candidates[v].push_back(entry_slot(v));
        }
        for (const auto& state : f_.states) {
            for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
                const auto& slot = state.slots[r];
                if (slot.kind != t3::StateSlot::Kind::Value) continue;
                const auto x = canon(static_cast<std::uint32_t>(slot.value));
                if (vreg_.empty() || x >= vreg_.size() || vreg_[x] == t3::none) continue;
                candidates[vreg_[x]].push_back(r);
            }
        }
        std::vector<std::vector<std::uint8_t>> allowed(nv);
        for (std::uint32_t v = 0; v < nv; ++v) {
            auto& c = candidates[v];
            std::sort(c.begin(), c.end());
            c.erase(std::unique(c.begin(), c.end()), c.end());
            allowed[v].assign(c.size(), 1);
        }
        const auto forbid = [&](std::uint32_t v, const std::function<bool(std::uint32_t)>& bad) {
            for (std::size_t k = 0; k < candidates[v].size(); ++k) {
                if (allowed[v][k] && bad(candidates[v][k])) allowed[v][k] = 0;
            }
        };
        for (std::uint32_t b = 0; b < nb; ++b) {
            const auto& block = fn.blocks[b];
            auto live = live_out[b];
            for (auto j = block.insts.size(); j > 0; --j) {
                const auto& inst = block.insts[j - 1];
                const auto after = live;
                for (const auto& d : inst.defs) clear(live, d.vreg.id);
                for (const auto& u : inst.uses) set(live, u.vreg.id);
                for (const auto& u : inst.state_uses) set(live, u.vreg.id);
                const auto& step = out_.steps[inst.opcode];
                const bool observer = step.kind == Kind::WriteBack || step.kind == Kind::Helper || step.kind == Kind::Call;
                const auto state_id = step.kind == Kind::Helper || step.kind == Kind::Call ? f_.insts[step.inst].state
                                                                                          : step.state;
                Bits around = live;
                for (std::size_t w = 0; w < words; ++w) around[w] |= after[w];
                const auto check = [&](std::uint32_t v) {
                    if (candidates[v].empty()) return;
                    if (state_id != t3::none) {
                        const auto& slots = f_.states[state_id].slots;
                        const auto x = value_of_[v];
                        forbid(v, [&](std::uint32_t r) {
                            const auto& entry = slots[r];
                            if (entry.kind == t3::StateSlot::Kind::Unchanged) return entry_slot(v) != r;
                            if (!observer) return false;
                            return !(entry.kind == t3::StateSlot::Kind::Value &&
                                     canon(static_cast<std::uint32_t>(entry.value)) == x);
                        });
                    }
                    if (step.kind == Kind::Call && has(after, v)) {
                        const auto result = f_.exits[step.index]->result_slot;
                        forbid(v, [&](std::uint32_t r) { return r == result; });
                    }
                };
                for (std::size_t w = 0; w < words; ++w) {
                    for (auto bits = around[w]; bits != 0; bits &= bits - 1) {
                        check(static_cast<std::uint32_t>(64 * w + static_cast<unsigned>(__builtin_ctzll(bits))));
                    }
                }
            }
        }
        for (std::uint32_t v = 0; v < nv; ++v) {
            for (std::size_t k = 0; k < candidates[v].size(); ++k) {
                if (allowed[v][k]) {
                    fn.spill_home[v] = static_cast<std::int32_t>(candidates[v][k]);
                    break;
                }
            }
        }
        // Rule 4: a slot that may hold a spilled value other than its entry
        // value where a state says it is unchanged (or at a Leave) loses
        // those homes, whether or not the value is live there.
        const auto slots = f_.slot_count;
        const std::size_t slot_words = (slots + 63) / 64;
        std::vector<Bits> dirty_in(nb, Bits(slot_words, 0));
        const auto dirt = [&](std::uint32_t v) -> std::uint32_t {
            const auto home = fn.spill_home[v];
            if (home < 0 || entry_slot(v) == static_cast<std::uint32_t>(home)) return t3::none;
            return static_cast<std::uint32_t>(home);
        };
        Bits violated(slot_words, 0);
        const auto run = [&](std::uint32_t b, Bits dirty, bool record) {
            const auto& block = fn.blocks[b];
            for (const auto& phi : block.phis) {
                if (const auto r = dirt(phi.def.id); r != t3::none) set(dirty, r);
            }
            for (const auto& inst : block.insts) {
                const auto& step = out_.steps[inst.opcode];
                const auto state_id = step.kind == Kind::Helper || step.kind == Kind::Call ? f_.insts[step.inst].state
                                                                                          : step.state;
                if (record && state_id != t3::none) {
                    const auto& entries = f_.states[state_id].slots;
                    for (std::uint32_t r = 0; r < slots; ++r) {
                        if (has(dirty, r) && entries[r].kind == t3::StateSlot::Kind::Unchanged) set(violated, r);
                    }
                }
                if (record && step.kind == Kind::Leave) {
                    for (std::size_t w = 0; w < slot_words; ++w) violated[w] |= dirty[w];
                }
                for (const auto& d : inst.defs) {
                    if (const auto r = dirt(d.vreg.id); r != t3::none) set(dirty, r);
                }
            }
            return dirty;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (std::uint32_t b = 0; b < nb; ++b) {
                const auto out = run(b, dirty_in[b], false);
                for (const auto s : fn.blocks[b].succs) {
                    for (std::size_t w = 0; w < slot_words; ++w) {
                        const auto merged = dirty_in[s][w] | out[w];
                        if (merged != dirty_in[s][w]) {
                            dirty_in[s][w] = merged;
                            changed = true;
                        }
                    }
                }
            }
        }
        for (std::uint32_t b = 0; b < nb; ++b) run(b, dirty_in[b], true);
        for (std::uint32_t v = 0; v < nv; ++v) {
            const auto r = dirt(v);
            if (r != t3::none && has(violated, r)) fn.spill_home[v] = -1;
        }
    }
};

} // namespace

unsigned lift_arity(const lift::Instruction& in) {
    switch (in.op) {
    case Opcode::Constant: return 0;
    case Opcode::Negate:
    case Opcode::Not:
    case Opcode::BitNot:
    case Opcode::Index:
    case Opcode::ConvertInteger:
    case Opcode::ValueHash:
    case Opcode::FloatNegate:
    case Opcode::FloatFromInteger:
    case Opcode::FloatToInteger:
    case Opcode::FloatConvert: return 1;
    default: return 2;
    }
}

bool constant_divisor(const lift::Instruction& in, std::uint64_t value) {
    if (value == 0) return true;
    if (!in.is_signed) return (value & (value - 1)) == 0;
    const auto y = lift::sign_extend(value, in.width);
    if (y == 1 || y == -1) return true;
    return y > 0 && (static_cast<std::uint64_t>(y) & (static_cast<std::uint64_t>(y) - 1)) == 0;
}

LiftShape lift_shape(const lift::Instruction& in, const Step& step) {
    using U = LiftShape::Use;
    using D = LiftShape::Def;
    LiftShape shape;
    const unsigned n = lift_arity(in);
    shape.a = n >= 1 ? U::Register : U::None;
    shape.b = n >= 2 ? U::Register : U::None;
    const unsigned w = in.width;
    const bool fits = step.a_width <= w && (n < 2 || step.b_width <= w);
    const bool c = step.b_constant;
    const auto v = step.b_value;
    const auto evaluate = [&] {
        shape.evaluate = true;
        shape.def = D::Register;
        return shape;
    };
    if (is_float_op(in.op)) {
        switch (in.op) {
        case Opcode::FloatAdd:
        case Opcode::FloatSubtract:
        case Opcode::FloatMultiply:
        case Opcode::FloatDivide:
        case Opcode::FloatEqual:
        case Opcode::FloatNotEqual:
        case Opcode::FloatLess:
        case Opcode::FloatLessEqual:
        case Opcode::FloatGreater:
        case Opcode::FloatGreaterEqual:
            if (!hardware_float(in.format)) return evaluate();
            shape.xmm_temps = 2;
            return shape;
        case Opcode::FloatNegate:
            if (in.format == lift::FloatType::None) return evaluate();
            shape.def = D::SameAsA;
            return shape;
        case Opcode::FloatConvert:
            if (!hardware_float(in.format) || !hardware_float(in.to_format) || in.format == in.to_format) {
                return evaluate();
            }
            shape.xmm_temps = 1;
            return shape;
        case Opcode::FloatFromInteger:
            if (!hardware_float(in.to_format) || w == 0 || w > 64) return evaluate();
            shape.xmm_temps = 1;
            shape.temps = 1;
            return shape;
        case Opcode::FloatToInteger:
            if (!hardware_float(in.format) || in.to_width == 0 || in.to_width > 64) return evaluate();
            shape.xmm_temps = 1;
            shape.temps = 1;
            return shape;
        default: return evaluate();
        }
    }
    if (w == 0 || w > 64) return evaluate();
    switch (in.op) {
    case Opcode::Constant: return shape;
    case Opcode::Add:
    case Opcode::Subtract: {
        const bool fast = w == 64 || (w == 32 && fits);
        if (fast) {
            shape.def = D::SameAsA;
            if (c && (w == 32 || simm32(v))) shape.b = U::Immediate;
            return shape;
        }
        if (!in.is_signed) {
            shape.def = D::SameAsA;
            if (c && simm32(v)) shape.b = U::Immediate;
            return shape;
        }
        if (fits) {
            shape.def = D::SameAsA;
            if (c) shape.b = U::Immediate;
            return shape;
        }
        shape.temps = 2;
        return shape;
    }
    case Opcode::AddWrapping:
    case Opcode::SubtractWrapping:
        shape.def = D::SameAsA;
        if (c && (w <= 32 || simm32(v))) shape.b = U::Immediate;
        return shape;
    case Opcode::Negate:
        shape.def = in.is_signed || !step.checked ? D::SameAsA : D::Register;
        return shape;
    case Opcode::Multiply:
        if (!step.checked) {
            // The wrapping 64-bit product, masked.
            shape.def = D::SameAsA;
            if (c && simm32(v)) shape.b = U::Immediate;
            return shape;
        }
        if (!in.is_signed && w == 64) {
            shape.a = U::Rax;
            shape.def = D::Rax;
            shape.clobbers = bit(rdx);
            return shape;
        }
        if (!in.is_signed || w == 64 || (w == 32 && fits)) {
            shape.def = D::SameAsA;
            if (c && (w == 32 && in.is_signed ? true : simm32(v))) shape.b = U::Immediate;
            return shape;
        }
        if (fits) {
            shape.def = D::SameAsA;
            if (c) shape.b = U::Immediate;
            return shape;
        }
        shape.temps = 2;
        return shape;
    case Opcode::Divide:
    case Opcode::Remainder:
        if (c && constant_divisor(in, v)) {
            shape.b = U::Immediate;
            shape.def = D::SameAsA;
            return shape;
        }
        shape.a = U::Rax;
        shape.b = U::Rcx;
        shape.def = in.op == Opcode::Divide ? D::Rax : D::Rdx;
        shape.clobbers = (in.op == Opcode::Divide ? bit(rdx) : bit(rax)) | bit(rcx);
        return shape;
    case Opcode::Equal:
    case Opcode::NotEqual:
        if (c && simm32(v)) shape.b = U::Immediate;
        return shape;
    case Opcode::Less:
    case Opcode::LessEqual:
    case Opcode::Greater:
    case Opcode::GreaterEqual:
        if (!in.is_signed || w == 64) {
            if (c && simm32(v)) shape.b = U::Immediate;
            return shape;
        }
        if ((w == 32 || w == 16 || w == 8) && fits) {
            if (c) shape.b = U::Immediate;
            return shape;
        }
        if (fits) {
            if (c) shape.b = U::Immediate;
            shape.temps = 1;
            return shape;
        }
        shape.temps = 2;
        return shape;
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Not:
    case Opcode::BitNot:
    case Opcode::Index: shape.def = D::SameAsA; return shape;
    case Opcode::BitAnd:
    case Opcode::BitOr:
    case Opcode::BitXor:
        shape.def = D::SameAsA;
        if (c && (simm32(v) || (in.op == Opcode::BitAnd && v <= 0xFFFFFFFFULL))) shape.b = U::Immediate;
        return shape;
    case Opcode::ShiftLeft:
    case Opcode::ShiftRight:
        if (c) {
            shape.b = U::Immediate;
            shape.def = D::SameAsA;
            return shape;
        }
        shape.b = U::Rcx;
        shape.temps = 1;
        return shape;
    case Opcode::ConvertInteger:
        if (in.to_width == 0 || in.to_width > 64) return evaluate();
        shape.def = D::SameAsA;
        return shape;
    case Opcode::ValueHash:
        if (in.format != lift::FloatType::None) return evaluate();
        shape.def = D::SameAsA;
        return shape;
    default: return evaluate();
    }
}

Lowered lower(const nvm::tier3::Function& function, const std::vector<std::uint32_t>& entries) {
    return Lowering(function, entries).run();
}

namespace {

// Whether every loop of the code the IR entries `roots` reach is entered
// only at its header: every retreating edge of a depth-first search from the
// roots goes to a block that dominates its source, with the roots children
// of one virtual root. It is the allocator's rule (regalloc.cpp,
// analyze_cfg), which edge splitting does not change.
bool reducible(const t3::Function& f, const std::vector<std::uint32_t>& roots) {
    const auto nb = static_cast<std::uint32_t>(f.blocks.size());
    const auto root = nb;
    std::vector<std::uint8_t> state(nb, 0);
    std::vector<std::uint32_t> post;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> back;
    for (const auto r : roots) {
        if (state[r] != 0) continue;
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{r, 0}};
        state[r] = 1;
        while (!stack.empty()) {
            auto& [b, next] = stack.back();
            const auto& succs = f.blocks[b].succs;
            if (next < succs.size()) {
                const auto s = succs[next++];
                if (state[s] == 0) {
                    state[s] = 1;
                    stack.emplace_back(s, 0);
                } else if (state[s] == 1) {
                    back.emplace_back(b, s);
                }
                continue;
            }
            state[b] = 2;
            post.push_back(b);
            stack.pop_back();
        }
    }
    // Dominators (Cooper, Harvey and Kennedy) under the virtual root.
    std::vector<std::uint32_t> number(nb + 1, 0);
    for (std::size_t i = 0; i < post.size(); ++i) number[post[i]] = static_cast<std::uint32_t>(post.size() - i);
    std::vector<std::uint32_t> idom(nb + 1, t3::none);
    idom[root] = root;
    std::vector<std::uint8_t> is_root(nb, 0);
    for (const auto r : roots) {
        idom[r] = root;
        is_root[r] = 1;
    }
    const auto intersect = [&](std::uint32_t a, std::uint32_t b) {
        while (a != b) {
            while (number[a] > number[b]) a = idom[a];
            while (number[b] > number[a]) b = idom[b];
        }
        return a;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = post.rbegin(); it != post.rend(); ++it) {
            const auto b = *it;
            if (is_root[b]) continue;
            std::uint32_t next = t3::none;
            for (const auto p : f.blocks[b].preds) {
                if (state[p] != 2 || idom[p] == t3::none) continue;
                next = next == t3::none ? p : intersect(p, next);
            }
            if (next != idom[b]) {
                idom[b] = next;
                changed = true;
            }
        }
    }
    for (const auto& [source, target] : back) {
        auto b = source;
        while (b != target && b != root) b = idom[b];
        if (b != target) return false;
    }
    return true;
}

} // namespace

std::vector<std::vector<std::uint32_t>> entry_units(const nvm::tier3::Function& function) {
    const auto count = static_cast<std::uint32_t>(function.entries.size());
    if (count == 0) refuse("a function without entries");
    for (std::uint32_t e = 0; e < count; ++e) {
        if (e >= function.blocks.size() || function.blocks[e].succs.size() != 1) {
            refuse("an entry block without one successor");
        }
    }
    // Entry 0's unit takes every entry whose code it can share: not an entry
    // at a loop head (in the unit of the code before the loop, the loop
    // head's phis would join every value with the entry's load and be live
    // through the loop), and not one that makes the code irreducible (an
    // entry inside a loop). Each other entry is a unit of its own.
    std::vector<std::uint32_t> roots(count);
    for (std::uint32_t e = 0; e < count; ++e) roots[e] = e;
    const auto cfg = analyze(function, roots);
    std::vector<std::vector<std::uint32_t>> units{{0}};
    for (std::uint32_t e = 1; e < count; ++e) {
        if (cfg.loops.count(function.blocks[e].succs[0]) == 0) {
            units[0].push_back(e);
            if (reducible(function, units[0])) continue;
            units[0].pop_back();
        }
        units.push_back({e});
    }
    return units;
}

} // namespace neura::jit::native
