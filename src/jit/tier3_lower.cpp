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
#include "nvm/tier3_passes.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <map>
#include <string>
#include <tuple>
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

bool is_float_compare(Opcode op) {
    switch (op) {
    case Opcode::FloatEqual:
    case Opcode::FloatNotEqual:
    case Opcode::FloatLess:
    case Opcode::FloatLessEqual:
    case Opcode::FloatGreater:
    case Opcode::FloatGreaterEqual: return true;
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
            for (const auto& constant : edge_constants_[m]) {
                Step step;
                step.kind = Kind::Const;
                step.imm = constant.bits;
                current_ = m;
                emit(std::move(step)).defs.push_back(reg({constant.vreg, constant.cls}));
            }
        }
        for (std::uint32_t m = 0; m < edge_converts_.size(); ++m) {
            for (const auto& convert : edge_converts_[m]) {
                Step step;
                step.kind = Kind::Convert;
                step.width = convert.width;
                current_ = m;
                auto& inst = emit(std::move(step));
                inst.uses.push_back(reg(convert.from));
                inst.defs.push_back(reg(convert.to));
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
        if (in.op == Op::Phi || in.op == Op::StateCall || in.op == Op::Helper || in.op == Op::Call || in.op == Op::Select ||
            in.op == Op::AllocObject || in.op == Op::Append || in.op == Op::Return) {
            for (std::uint32_t i = 0; i < in.count; ++i) f(f_.extras[in.extra + i]);
        } else if (in.op == Op::Gather) {
            for (std::uint32_t i = 0; i < in.count; i += 2) f(f_.extras[in.extra + i]);
        }
    }

    // The slot and flag values a frame state names, at every level of its
    // chain (a deoptimization materializes the inlined frames too), and K.
    template <typename F>
    void for_each_state_value(std::uint32_t s, F&& f) const {
        for (std::uint32_t depth = 0; s != t3::none; s = f_.states[s].caller) {
            if (s >= f_.states.size() || ++depth > 64) refuse("a frame state chain that does not end");
            const auto& state = f_.states[s];
            if (state.kernel_steps != t3::none) f(state.kernel_steps);
            for (const auto& slot : state.slots) {
                if (slot.kind == t3::StateSlot::Kind::Value) f(static_cast<std::uint32_t>(slot.value));
            }
            for (const auto& flag : state.flags) {
                if (flag.kind == t3::StateFlag::Kind::Value) f(flag.value);
            }
        }
    }

    // The level of a frame state's chain that describes the physical frame.
    std::uint32_t physical(std::uint32_t s) const {
        for (std::uint32_t depth = 0; f_.states[s].caller != t3::none; ++depth) {
            if (depth > 64) refuse("a frame state chain that does not end");
            s = f_.states[s].caller;
        }
        return s;
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

    // The values of C: EntryCounter 0, the CounterAdds of C and the phis of
    // them (a greatest fixed point: every candidate stays unless one of its
    // inputs is not one). K (EntryCounter 1 and its CounterAdds) is an
    // ordinary value.
    void find_counters() {
        const auto n = f_.insts.size();
        counter_.assign(n, 0);
        std::vector<std::uint32_t> candidates;
        for (std::uint32_t id = 0; id < n; ++id) {
            const auto& in = f_.insts[id];
            // An instruction in no block is dead (the passes leave them).
            if (block_of_[id] == t3::none) continue;
            if ((in.op == Op::EntryCounter && in.imm == 0) || in.op == Op::CounterAdd || in.op == Op::Phi) {
                counter_[id] = 1;
                candidates.push_back(id);
            }
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto id : candidates) {
                if (!counter_[id]) continue;
                const auto& in = f_.insts[id];
                bool keep = true;
                if (in.op == Op::CounterAdd) {
                    keep = in.operands[0] < n && counter_[in.operands[0]];
                } else if (in.op == Op::Phi) {
                    for (std::uint32_t i = 0; i < in.count && keep; ++i) {
                        const auto v = f_.extras[in.extra + i];
                        keep = v == id || (v < n && counter_[v]);
                    }
                }
                if (!keep) {
                    counter_[id] = 0;
                    changed = true;
                }
            }
        }
        for (const auto id : candidates) {
            const auto& in = f_.insts[id];
            if (in.op != Op::Phi || counter_[id]) continue;
            for (std::uint32_t i = 0; i < in.count; ++i) {
                const auto v = f_.extras[in.extra + i];
                if (v < n && counter_[v]) refuse("a phi joins the block counter with another value");
            }
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
                case Op::EntryCounter:
                    if (counter_[id]) current = id;
                    break;
                case Op::CounterAdd:
                    if (!counter_[id]) {
                        if (counter_[in.operands[0]]) refuse("the block counter as a value");
                        break;
                    }
                    expect(in.operands[0]);
                    current = id;
                    break;
                case Op::Budget:
                case Op::Helper:
                case Op::Call:
                case Op::Return:
                case Op::Leave:
                case Op::Deopt:
                case Op::AllocObject:
                case Op::Append:
                    expect(in.operands[0]);
                    for (int k = 1; k < 4; ++k) {
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
        if (in.op == Op::StateCall || in.op == Op::Helper || in.op == Op::Call || in.op == Op::Select ||
            in.op == Op::AllocObject || in.op == Op::Append) {
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
        // The initialized flag of a slot that is not a parameter, at entry 0
        // of a function program (the entry at image block 0), is 0: the
        // callee's frame is fresh (push_frame initializes the parameters
        // only), and the dispatcher refuses entry 0 when another slot is
        // initialized (run_native_program). A slot the entry reads is the
        // case above (that entry is refused).
        if (f_.region != nullptr && f_.region->program) {
            for (const auto e : entries_) {
                if (e >= f_.region->entries.size() || f_.region->entries[e].block != 0) continue;
                for (const auto id : f_.blocks[e].insts) {
                    const auto& in = f_.insts[id];
                    if (in.op == Op::EntryFlag && in.imm >= f_.region->parameter_count && !known_[id]) {
                        known_[id] = 1;
                        known_value_[id] = 0;
                    }
                }
            }
        }
        // The flags the image proves at an entry's block (tier2.cpp,
        // analyse_flags): a flag every frame there has as 0 or as 1 is known,
        // and the entry flags of slots whose flags are equal in every frame
        // are one value, which joins the flag phis of the slots a loop
        // assigns together. A fact is a proof about the frames that arrive at
        // the block, whichever way they got there.
        if (f_.region != nullptr && f_.region->program) {
            for (const auto e : entries_) {
                if (e >= f_.region->entries.size()) continue;
                const auto& facts = f_.region->entries[e].flags;
                std::map<std::uint32_t, std::uint32_t> representative;
                for (const auto id : f_.blocks[e].insts) {
                    const auto& in = f_.insts[id];
                    if (in.op != Op::EntryFlag || in.imm >= facts.size() || known_[id]) continue;
                    const auto fact = facts[in.imm];
                    if (fact == nvm::tier2::Region::flag_unknown) continue;
                    if (fact <= 1) {
                        known_[id] = 1;
                        known_value_[id] = fact;
                    } else if (const auto [it, first] = representative.try_emplace(fact, id); !first) {
                        canon_[id] = it->second;
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
        merge_congruent_phis(phis);
    }

    // Phis of one block that take congruent values on every edge of the unit
    // are one value (the initialized flags of the slots a loop assigns, each
    // 0 on entry and 1 after a write, are the usual ones). Congruence is the
    // greatest one: phis of one block and type start in one class, a known
    // value is in the class of its type and bits, and a class is split while
    // its phis' inputs fall in different classes (Alpern, Wegman and Zadeck,
    // partitioning to find equal values).
    void merge_congruent_phis(const std::vector<std::uint32_t>& phis) {
        std::vector<std::uint32_t> candidates;
        for (const auto id : phis) {
            if (!known_[id] && canon(id) == id) candidates.push_back(id);
        }
        if (candidates.size() < 2) return;
        // Classes of the values that are not candidates, made on demand.
        std::map<std::tuple<std::uint8_t, std::uint8_t, std::uint64_t>, std::uint32_t> known_class;
        std::map<std::tuple<std::uint32_t, std::uint8_t, std::uint8_t>, std::uint32_t> phi_class;
        std::map<std::uint32_t, std::uint32_t> other_class;
        std::vector<std::uint32_t> cls(f_.insts.size(), t3::none);
        std::uint32_t next = 0;
        for (const auto id : candidates) {
            const auto& type = f_.insts[id].type;
            const auto key = std::make_tuple(block_of_[id], static_cast<std::uint8_t>(type.type), type.width);
            const auto [it, fresh_class] = phi_class.try_emplace(key, next);
            if (fresh_class) ++next;
            cls[id] = it->second;
        }
        std::vector<std::uint8_t> is_candidate(f_.insts.size(), 0);
        for (const auto id : candidates) is_candidate[id] = 1;
        const auto class_of = [&](std::uint32_t input) {
            const auto x = canon(input);
            if (is_candidate[x]) return cls[x];
            if (known_[x]) {
                const auto& type = f_.insts[x].type;
                const auto key = std::make_tuple(static_cast<std::uint8_t>(type.type), type.width, known_value_[x]);
                const auto [it, fresh_class] = known_class.try_emplace(key, next);
                if (fresh_class) ++next;
                return it->second;
            }
            const auto [it, fresh_class] = other_class.try_emplace(x, next);
            if (fresh_class) ++next;
            return it->second;
        };
        std::size_t classes = phi_class.size();
        for (;;) {
            std::map<std::pair<std::uint32_t, std::vector<std::uint32_t>>, std::uint32_t> refined;
            std::vector<std::uint32_t> assigned(candidates.size());
            for (std::size_t k = 0; k < candidates.size(); ++k) {
                const auto id = candidates[k];
                const auto& in = f_.insts[id];
                std::vector<std::uint32_t> signature;
                for (const auto i : unit_preds_[block_of_[id]]) signature.push_back(class_of(f_.extras[in.extra + i]));
                const auto [it, inserted] = refined.try_emplace({cls[id], std::move(signature)}, next);
                if (inserted) ++next;
                assigned[k] = it->second;
            }
            for (std::size_t k = 0; k < candidates.size(); ++k) cls[candidates[k]] = assigned[k];
            // Classes only split: no split, no change.
            if (refined.size() == classes) break;
            classes = refined.size();
        }
        std::map<std::uint32_t, std::uint32_t> representative;
        for (const auto id : candidates) {
            const auto [it, first] = representative.try_emplace(cls[id], id);
            if (!first) canon_[id] = it->second;
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
        case t3::Type::Ref:
        case t3::Type::Ptr: return 64;
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
            if (compare.op != Op::Lift || (compare.flags & t3::Checked) != 0) continue;
            if (!is_compare(compare.lifted.op) && !is_float_compare(compare.lifted.op)) continue;
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
                for (auto s = in.state; s != t3::none; s = f_.states[s].caller) {
                    for (const auto& flag : f_.states[s].flags) {
                        if (flag.kind == t3::StateFlag::Kind::Value) keep(flag.value);
                    }
                }
            }
        }
        for (const auto b : order_) {
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                // A phi's known inputs are made on its edges.
                if (in.op == Op::Phi) continue;
                if (in.op == Op::Lift) {
                    if (const auto x = t3::detail::abs_operand(f_, in); x != t3::none) {
                        need(x);
                        continue;
                    }
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

    static bool float_type(const t3::ValueType& type) {
        return type.type == t3::Type::F32 || type.type == t3::Type::F64;
    }

    // The register file of value `v`'s own virtual register: XMM for a
    // binary32 or binary64 float, except one a helper or call returns (in
    // rax) and one the heap code reads or chooses (its bits, in a general
    // register).
    mir::RegClass home_class(std::uint32_t v) {
        v = canon(v);
        const auto& in = f_.insts[v];
        if (!float_type(in.type)) return mir::RegClass::Gpr;
        switch (in.op) {
        case Op::Helper:
        case Op::Call:
        case Op::StateCall:
        case Op::LoadField:
        case Op::LoadElement:
        case Op::LoadGlobal:
        case Op::Select:
        case Op::ResultWord:
        case Op::AllocObject: return mir::RegClass::Gpr;
        default: return mir::RegClass::Xmm;
        }
    }

    // The width of the bits a move between register files carries.
    std::uint8_t move_width(std::uint32_t v) const {
        return f_.insts[v].type.type == t3::Type::F32 ? 32 : 64;
    }

    mir::VReg vreg(std::uint32_t v) {
        if (v == t3::none || v >= f_.insts.size()) refuse("a missing value");
        v = canon(v);
        if (counter_[v]) refuse("the block counter as a value");
        if (vreg_.empty()) vreg_.assign(f_.insts.size(), t3::none);
        const auto cls = home_class(v);
        if (vreg_[v] == t3::none) {
            (void)width_of(v);
            vreg_[v] = static_cast<std::uint32_t>(out_.function.classes.size());
            out_.function.classes.push_back(cls);
            out_.function.spill_home.push_back(-1);
            value_of_.push_back(v);
        }
        return {vreg_[v], cls};
    }

    // Value `v` in a register of file `cls`, for an operand: its own virtual
    // register, or a copy made here in the other file (a known value is
    // made here in that file).
    mir::VReg use_as(std::uint32_t v, mir::RegClass cls) {
        if (v == t3::none || v >= f_.insts.size()) refuse("a missing value");
        const auto c = canon(v);
        if (home_class(c) == cls && (!known(c) || needed_[c])) return vreg(c);
        if (known(c)) {
            Step step;
            step.kind = Kind::Const;
            step.inst = c;
            step.imm = known_value(c);
            const auto copy = fresh(c, cls);
            emit(std::move(step)).defs.push_back(reg({copy, cls}));
            return {copy, cls};
        }
        const auto source = vreg(c);
        Step step;
        step.kind = Kind::Convert;
        step.inst = c;
        step.width = move_width(c);
        const auto copy = fresh(c, cls);
        auto& inst = emit(std::move(step));
        inst.uses.push_back(reg(source));
        inst.defs.push_back(reg({copy, cls}));
        return {copy, cls};
    }

    mir::RegClass file(bool xmm) const { return xmm ? mir::RegClass::Xmm : mir::RegClass::Gpr; }
    std::vector<std::uint32_t> value_of_;  // vreg -> IR value
    // By machine block: the constants to make at its end (vreg, bits).
    struct EdgeConstant {
        std::uint32_t vreg;
        std::uint64_t bits;
        mir::RegClass cls;
    };
    std::vector<std::vector<EdgeConstant>> edge_constants_;
    // By machine block: the phi inputs to copy into the phi's register file
    // at its end.
    struct EdgeConvert {
        mir::VReg to;
        mir::VReg from;
        std::uint8_t width;
    };
    std::vector<std::vector<EdgeConvert>> edge_converts_;
    // By machine block: the initialized flags to read again at its end
    // (vreg, slot).
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> edge_flags_;

    // A virtual register of its own for a copy of IR value `v`.
    std::uint32_t fresh(std::uint32_t v, mir::RegClass cls = mir::RegClass::Gpr) {
        const auto id = static_cast<std::uint32_t>(out_.function.classes.size());
        out_.function.classes.push_back(cls);
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
                    const auto from = vreg(input);
                    if (from.cls == phi.def.cls) {
                        phi.inputs.push_back(from);
                        continue;
                    }
                    // An input in the other register file: copied at the end
                    // of the predecessor.
                    const mir::VReg to{fresh(canon(input), phi.def.cls), phi.def.cls};
                    if (edge_converts_.size() < out_.function.blocks.size()) edge_converts_.resize(out_.function.blocks.size());
                    edge_converts_[preds[k]].push_back({to, from, move_width(canon(input))});
                    phi.inputs.push_back(to);
                    continue;
                }
                // A known input: a constant of its own at the end of the
                // predecessor.
                const auto v = fresh(canon(input), phi.def.cls);
                if (edge_constants_.size() < out_.function.blocks.size()) edge_constants_.resize(out_.function.blocks.size());
                edge_constants_[preds[k]].push_back({v, known_value(input), phi.def.cls});
                phi.inputs.push_back({v, phi.def.cls});
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
        case Op::EntryCounter: {
            // r12 holds C: set by the trampoline or the caller at an entry,
            // and by the callee after a call. K (in a function with inline
            // kernels) is in the context there.
            if (counter_[id]) return;
            Step step;
            step.kind = Kind::KernelLoad;
            step.inst = id;
            emit(std::move(step)).defs.push_back(reg(vreg(id)));
            return;
        }
        case Op::Phi: return;
        case Op::Lift: {
            if (fused_[id]) return;
            if (const auto x = t3::detail::abs_operand(f_, in); x != t3::none) {
                const auto operand = use_as(x, mir::RegClass::Xmm);
                Step step;
                step.kind = Kind::Abs;
                step.inst = id;
                step.width = static_cast<std::uint8_t>(in.type.width);
                auto& inst = emit(std::move(step));
                inst.uses.push_back(reg(operand));
                inst.defs.push_back(same(vreg(id)));
                return;
            }
            auto step = lift_step(id);
            const auto shape = lift_shape(in.lifted, step);
            const bool checked = (in.flags & t3::Checked) != 0;
            step.reason = static_cast<std::uint8_t>(t3::Reason::Checks);
            const auto uses = lift_uses(in, shape);
            auto& inst = emit(std::move(step));
            attach_lift(inst, in, shape, uses, true);
            if (checked) add_state(inst, in.state);
            if (!inst.defs.empty() && inst.defs[0].vreg.cls != file(shape.def_xmm)) {
                // A result made in the other register file (lift::evaluate's
                // bits of a float): copied into the value's own.
                const auto own = inst.defs[0].vreg;
                const mir::VReg made{fresh(canon(id), file(shape.def_xmm)), file(shape.def_xmm)};
                inst.defs[0].vreg = made;
                Step move;
                move.kind = Kind::Convert;
                move.inst = id;
                move.width = move_width(canon(id));
                auto& copy = emit(std::move(move));
                copy.uses.push_back(reg(made));
                copy.defs.push_back(reg(own));
            }
            return;
        }
        case Op::Canon:
        case Op::Sqrt: {
            const auto operand = use_as(in.operands[0], mir::RegClass::Xmm);
            Step step;
            step.kind = in.op == Op::Canon ? Kind::Canon : Kind::Sqrt;
            step.inst = id;
            step.width = static_cast<std::uint8_t>(in.type.width);
            // A Sqrt not Deferred canonicalizes its NaN.
            step.checked = in.op == Op::Sqrt && (in.flags & t3::Deferred) == 0;
            auto& inst = emit(std::move(step));
            if (in.op == Op::Canon) {
                inst.uses.push_back(reg(operand));
                inst.defs.push_back(same(vreg(id)));
            } else {
                inst.uses.push_back(any(operand));
                inst.defs.push_back(reg(vreg(id)));
            }
            return;
        }
        case Op::Mask: {
            const auto operand = use_as(in.operands[0], mir::RegClass::Gpr);
            Step step;
            step.kind = Kind::Mask;
            step.inst = id;
            step.width = static_cast<std::uint8_t>(std::min<std::uint64_t>(in.imm, 64));
            step.a_width = width_of(in.operands[0]);
            auto& inst = emit(std::move(step));
            inst.uses.push_back(reg(operand));
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
                uses.push_back(any(use_as(f_.extras[in.extra + i], mir::RegClass::Gpr)));
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
            step.inst = id;
            step.imm = in.imm;
            if (!counter_[id]) {
                // K: an ordinary value.
                step.kind = Kind::KernelAdd;
                const auto operand = vreg(in.operands[0]);
                auto& inst = emit(std::move(step));
                inst.uses.push_back(reg(operand));
                inst.defs.push_back(same(vreg(id)));
                return;
            }
            step.kind = Kind::CounterAdd;
            emit(std::move(step));
            return;
        }
        case Op::Budget: {
            Step step;
            step.kind = Kind::Budget;
            step.inst = id;
            step.imm = in.imm;
            step.reason = static_cast<std::uint8_t>(t3::Reason::Budget);
            auto& inst = emit(std::move(step));
            // K: C + (K - context.K) + P against the room.
            if (in.operands[1] != t3::none) inst.uses.push_back(reg(vreg(in.operands[1])));
            add_state(inst, in.state);
            return;
        }
        case Op::SwitchCheck:
        case Op::Guard: {
            const auto operand = use_as(in.operands[0], mir::RegClass::Gpr);
            Step step;
            step.kind = in.op == Op::SwitchCheck ? Kind::SwitchCheck : Kind::Guard;
            step.inst = id;
            step.imm = in.imm;
            step.reason = static_cast<std::uint8_t>(in.op == Op::SwitchCheck ? t3::Reason::Selectors : t3::Reason::Guards);
            auto& inst = emit(std::move(step));
            inst.uses.push_back(reg(operand));
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
                const auto operand = use_as(in.operands[0], mir::RegClass::Gpr);
                auto& inst = emit(std::move(step));
                inst.uses.push_back(fixed(operand, rdx));
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
            // K, stored into the context before the call.
            if (in.operands[1] != t3::none) inst.uses.push_back(any(vreg(in.operands[1])));
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
            step.b_constant = in.operands[1] != t3::none;  // whether a value is returned
            // index: the words after the first (NVR 1.7), whose uses follow the
            // value's, before K's.
            step.index = in.count;
            const auto value =
                in.operands[1] != t3::none ? use_as(in.operands[1], mir::RegClass::Gpr) : mir::VReg{};
            std::vector<mir::VReg> rest;
            for (std::uint32_t i = 0; i < in.count; ++i) rest.push_back(use_as(f_.extras[in.extra + i], mir::RegClass::Gpr));
            auto& inst = emit(std::move(step));
            if (in.operands[1] != t3::none) inst.uses.push_back(fixed(value, rsi));
            for (const auto word : rest) inst.uses.push_back(any(word));
            if (in.operands[2] != t3::none) inst.uses.push_back(any(vreg(in.operands[2])));
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
            if (in.operands[1] != t3::none) inst.uses.push_back(any(vreg(in.operands[1])));
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
                const auto uses = lift_uses(f_.insts[c], shape);
                auto& inst = emit(std::move(step));
                attach_lift(inst, f_.insts[c], shape, uses, false);
                return;
            }
            const auto operand = use_as(c, mir::RegClass::Gpr);
            Step step;
            step.kind = Kind::Branch;
            step.inst = id;
            emit(std::move(step)).uses.push_back(reg(operand));
            return;
        }
        case Op::Switch: {
            const auto operand = use_as(in.operands[0], mir::RegClass::Gpr);
            Step step;
            step.kind = Kind::Switch;
            step.inst = id;
            step.imm = f_.blocks[block_of_[id]].succs.size();
            emit(std::move(step)).uses.push_back(reg(operand));
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
            const auto operand = use_as(in.operands[0], mir::RegClass::Gpr);
            Step step;
            step.kind = Kind::Dispatch;
            step.inst = id;
            step.imm = in.imm;
            emit(std::move(step)).uses.push_back(reg(operand));
            return;
        }
        default: lower_heap(id, in); return;
        }
        refuse("an operation the code generator does not handle");
    }

    // ---- heap access ------------------------------------------------------

    // The deoptimization reason of a heap check (as the IR interpreter's).
    static std::uint8_t heap_reason(const t3::Inst& in) {
        return static_cast<std::uint8_t>((in.flags & t3::ReferenceCheck) != 0 ? t3::Reason::References : t3::Reason::StateTraps);
    }

    // A heap check, read or write: registers for entries, data, indices and
    // values, a memory operand allowed for a length and a storage width.
    void lower_heap(std::uint32_t id, const t3::Inst& in) {
        // A float value the heap code stores or chooses, as its bits in a
        // general register: moved there before the instruction.
        std::map<std::uint32_t, mir::VReg> bits;
        const auto in_gpr = [&](std::uint32_t v) {
            if (v != t3::none && v < f_.insts.size() && home_class(v) == mir::RegClass::Xmm) {
                bits.emplace(canon(v), use_as(v, mir::RegClass::Gpr));
            }
        };
        for (const auto operand : in.operands) in_gpr(operand);
        if (in.op == Op::Select) {
            for (std::uint32_t i = 0; i < in.count; ++i) in_gpr(f_.extras[in.extra + i]);
        }
        const auto value = [&](std::uint32_t v) {
            const auto found = v == t3::none ? bits.end() : bits.find(canon(v));
            return found != bits.end() ? found->second : vreg(v);
        };
        Step step;
        step.inst = id;
        step.imm = in.imm;
        const auto use = [&](mir::Inst& inst, unsigned k) { inst.uses.push_back(reg(value(in.operands[k]))); };
        const auto def = [&](mir::Inst& inst) { inst.defs.push_back(reg(vreg(id))); };
        switch (in.op) {
        case Op::Lookup: {
            step.kind = Kind::Lookup;
            step.reason = heap_reason(in);
            auto& inst = emit(std::move(step));
            use(inst, 0);
            def(inst);
            inst.temps_gpr = 1;
            add_state(inst, in.state);
            return;
        }
        case Op::CheckNotNil:
        case Op::CheckKind:
        case Op::CheckInit: {
            step.kind = in.op == Op::CheckNotNil ? Kind::CheckNotNil : in.op == Op::CheckKind ? Kind::CheckKind : Kind::CheckInit;
            step.reason = heap_reason(in);
            step.index = static_cast<std::uint32_t>(in.imm);
            if (in.op == Op::CheckKind) {
                if (in.imm >= f_.type_tests.size()) refuse("a type test out of range");
                // An entry from a lookup that does not let nil through is
                // never 0.
                const auto& source = f_.insts[canon(in.operands[0])];
                step.checked = !(source.op == Op::Lookup && (source.imm & 1U) == 0);
            }
            auto& inst = emit(std::move(step));
            use(inst, 0);
            add_state(inst, in.state);
            return;
        }
        case Op::CheckIndex: {
            step.kind = Kind::CheckIndex;
            step.reason = heap_reason(in);
            step.a_width = width_of(in.operands[0]);
            step.b_width = width_of(in.operands[1]);
            const bool constant = known(in.operands[1]) && simm32(known_value(in.operands[1])) &&
                                  static_cast<std::int64_t>(known_value(in.operands[1])) >= 0;
            if (constant) {
                step.b_constant = true;
                step.b_value = known_value(in.operands[1]);
            }
            auto& inst = emit(std::move(step));
            use(inst, 0);
            if (!constant) inst.uses.push_back(any(value(in.operands[1])));
            add_state(inst, in.state);
            return;
        }
        case Op::CheckGlobalInit: {
            step.kind = Kind::CheckGlobalInit;
            step.reason = heap_reason(in);
            add_state(emit(std::move(step)), in.state);
            return;
        }
        case Op::TestKind: {
            if (in.imm >= f_.type_tests.size()) refuse("a type test out of range");
            step.kind = Kind::TestKind;
            step.index = static_cast<std::uint32_t>(in.imm);
            auto& inst = emit(std::move(step));
            use(inst, 0);
            def(inst);
            inst.temps_gpr = 1;
            return;
        }
        case Op::LoadTag:
        case Op::LoadField:
        case Op::LoadLength:
        case Op::LoadData:
        case Op::LoadStorage:
        case Op::LoadVersion: {
            step.kind = in.op == Op::LoadTag      ? Kind::LoadTag
                        : in.op == Op::LoadField  ? Kind::LoadField
                        : in.op == Op::LoadLength ? Kind::LoadLength
                        : in.op == Op::LoadData   ? Kind::LoadData
                        : in.op == Op::LoadStorage ? Kind::LoadStorage
                                                   : Kind::LoadVersion;
            step.index = in.aux;
            auto& inst = emit(std::move(step));
            use(inst, 0);
            def(inst);
            return;
        }
        case Op::LoadElement:
        case Op::StoreElement: {
            if (in.imm != 1 && in.imm != 2 && in.imm != 4 && in.imm != 8) refuse("an element width out of range");
            step.kind = in.op == Op::LoadElement ? Kind::LoadElement : Kind::StoreElement;
            auto& inst = emit(std::move(step));
            use(inst, 0);
            use(inst, 1);
            inst.uses.push_back(any(value(in.operands[2])));
            if (in.op == Op::StoreElement) use(inst, 3);
            else def(inst);
            return;
        }
        case Op::LoadGlobal: {
            step.kind = Kind::LoadGlobal;
            def(emit(std::move(step)));
            return;
        }
        case Op::ResultWord: {
            step.kind = Kind::ResultWord;
            step.imm = in.imm;
            def(emit(std::move(step)));
            return;
        }
        case Op::StoreGlobal: {
            step.kind = Kind::StoreGlobal;
            use(emit(std::move(step)), 0);
            return;
        }
        case Op::StoreField: {
            step.kind = Kind::StoreField;
            step.index = in.aux;
            auto& inst = emit(std::move(step));
            use(inst, 0);
            use(inst, 1);
            return;
        }
        case Op::Select: {
            step.kind = Kind::Select;
            step.imm = in.count;
            auto& inst = emit(std::move(step));
            use(inst, 0);
            for (std::uint32_t i = 0; i < in.count; ++i) inst.uses.push_back(any(value(f_.extras[in.extra + i])));
            def(inst);
            return;
        }
        case Op::Widen: {
            step.kind = Kind::Widen;
            auto& inst = emit(std::move(step));
            inst.uses.push_back(fixed(value(in.operands[0]), rsi));
            inst.uses.push_back(fixed(value(in.operands[1]), rdx));
            inst.uses.push_back(fixed(value(in.operands[2]), rcx));
            inst.clobbers_gpr = caller_saved_gpr;
            inst.clobbers_xmm = allocatable_xmm;
            inst.is_call = true;
            return;
        }
        case Op::AllocObject:
        case Op::Append: {
            if (in.aux >= f_.exits.size()) refuse("an exit out of range");
            step.kind = in.op == Op::AllocObject ? Kind::AllocObject : Kind::Append;
            step.index = in.aux;
            step.state = in.state;
            if (in.op == Op::Append) {
                // The value may not fit the storage unless its type fits the
                // declared width (imm bytes), which the storage never is below.
                const auto& type = f_.insts[in.operands[2]].type;
                const unsigned bits = type.type == t3::Type::Int ? type.width : 64U;
                step.checked = !(in.imm >= 8 || bits <= 8 * in.imm);
            }
            auto& inst = emit(std::move(step));
            auto& made = out_.steps[inst.opcode];
            if (in.op == Op::Append) {
                use(inst, 1);
                use(inst, 2);
            }
            for (std::uint32_t i = 0; i < in.count; ++i) inst.uses.push_back(any(vreg(f_.extras[in.extra + i])));
            made.arguments = in.count;
            out_.argument_words = std::max(out_.argument_words, in.count);
            if (f_.states[in.state].caller != t3::none) {
                // Inside inlined code the slow path materializes every frame
                // of the state from its values, as a deoptimization stub does.
                made.inlined = true;
                if (in.op == Op::AllocObject) def(inst);
                inst.temps_gpr = in.op == Op::AllocObject ? 4 : 3;
                inst.clobbers_xmm = allocatable_xmm;
                add_state(inst, in.state);
                return;
            }
            const auto& state = f_.states[in.state];
            if (state.slots.size() != f_.slot_count || state.flags.size() != f_.slot_count) {
                refuse("a frame state without one entry per slot");
            }
            const auto value = [&](std::uint32_t v) {
                if (known(v)) made.state_constants.emplace_back(v, known_value(v));
                else inst.uses.push_back(any(vreg(v)));
            };
            for (const auto& slot : state.slots) {
                if (slot.kind == t3::StateSlot::Kind::Value) value(static_cast<std::uint32_t>(slot.value));
            }
            for (const auto& flag : state.flags) {
                if (flag.kind == t3::StateFlag::Kind::Value) value(flag.value);
            }
            if (in.op == Op::AllocObject) def(inst);
            inst.temps_gpr = in.op == Op::AllocObject ? 4 : 3;
            inst.clobbers_xmm = allocatable_xmm;
            return;
        }
        default: break;
        }
        refuse("an operation the code generator does not handle");
    }

    // The operands of a Lift (or a fused compare), in the register files its
    // shape names; copies into the other file are made before it.
    std::vector<mir::Operand> lift_uses(const t3::Inst& in, const LiftShape& shape) {
        using U = LiftShape::Use;
        std::vector<mir::Operand> uses;
        const auto operand = [&](U use, std::uint32_t v, bool xmm, bool memory) {
            switch (use) {
            case U::None:
            case U::Immediate: return;
            case U::Register: {
                const auto r = use_as(v, file(xmm));
                uses.push_back(memory ? any(r) : reg(r));
                return;
            }
            case U::Rax: uses.push_back(fixed(use_as(v, mir::RegClass::Gpr), rax)); return;
            case U::Rcx: uses.push_back(fixed(use_as(v, mir::RegClass::Gpr), rcx)); return;
            }
        };
        operand(shape.a, in.operands[0], shape.a_xmm, false);
        operand(shape.b, in.operands[1], shape.b_xmm, shape.b_memory);
        return uses;
    }

    void attach_lift(mir::Inst& inst, const t3::Inst& in, const LiftShape& shape, const std::vector<mir::Operand>& uses,
                     bool with_def) {
        using D = LiftShape::Def;
        inst.uses = uses;
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
        // Only the states of the physical frame name its slots; an inlined
        // level's slots are another frame's.
        for (const auto& state : f_.states) {
            if (state.caller != t3::none || state.function != f_.function) continue;
            for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
                const auto& slot = state.slots[r];
                if (slot.kind != t3::StateSlot::Kind::Value) continue;
                const auto x = canon(static_cast<std::uint32_t>(slot.value));
                if (vreg_.empty() || x >= vreg_.size() || vreg_[x] == t3::none) continue;
                candidates[vreg_[x]].push_back(r);
            }
        }
        // A deferred float value (Function::frame_canon) may hold the
        // hardware's NaN, which no frame slot may hold where reference code
        // reads the frame (a spill can come after the write-back before a
        // call): a move into its home stores the canonical NaN in place of a
        // NaN (Lowered::canon_vreg), which keeps the value a NaN exactly
        // where the reference's is.
        //
        // The frame is read as it is only by reference code that runs after
        // a WriteBack, at a Helper or a Call (an allocation's or append's
        // slow path writes the frame back itself, in the cold code that
        // canonicalizes in place, as every WriteBack does for a value in its
        // home). A spill between the WriteBack and that instruction, which
        // may hold other instructions, stores into the frame after the
        // canonical write, and only a value live at the WriteBack or at the
        // instruction (before or after it) can be spilled there. The other
        // values' homes are canonicalized where the frame is written, so a
        // move into one stores the bits as they are.
        out_.canon_vreg.assign(nv, 0);
        std::vector<std::uint8_t> read_by_reference(nv, 0);
        {
            for (std::uint32_t b = 0; b < nb; ++b) {
                const auto& block = fn.blocks[b];
                auto here = live_out[b];
                for (auto j = block.insts.size(); j > 0; --j) {
                    const auto& inst = block.insts[j - 1];
                    const auto after = here;
                    for (const auto& d : inst.defs) clear(here, d.vreg.id);
                    for (const auto& u : inst.uses) set(here, u.vreg.id);
                    for (const auto& u : inst.state_uses) set(here, u.vreg.id);
                    const auto kind = out_.steps[inst.opcode].kind;
                    if (kind != Kind::WriteBack && kind != Kind::Helper && kind != Kind::Call) continue;
                    for (std::size_t w = 0; w < words; ++w) {
                        for (auto bits = here[w] | after[w]; bits != 0; bits &= bits - 1) {
                            read_by_reference[64 * w + static_cast<unsigned>(std::countr_zero(bits))] = 1;
                        }
                    }
                }
            }
        }
        for (std::uint32_t v = 0; v < nv; ++v) {
            const auto x = value_of_[v];
            if (fn.classes[v] == mir::RegClass::Xmm && x < f_.frame_canon.size() && read_by_reference[v]) {
                out_.canon_vreg[v] = f_.frame_canon[x];
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
                const bool observer = step.kind == Kind::WriteBack || step.kind == Kind::Helper || step.kind == Kind::Call ||
                                      step.kind == Kind::AllocObject || step.kind == Kind::Append;
                const auto state_id = step.kind == Kind::Helper || step.kind == Kind::Call ? f_.insts[step.inst].state
                                                                                          : step.state;
                Bits around = live;
                for (std::size_t w = 0; w < words; ++w) around[w] |= after[w];
                const auto check = [&](std::uint32_t v) {
                    if (candidates[v].empty()) return;
                    if (state_id != t3::none) {
                        const auto& slots = f_.states[physical(state_id)].slots;
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
                        // The slots a multi-word result fills (NVR 1.7).
                        const auto result = f_.exits[step.index]->result_slot;
                        const auto found = f_.call_results.find(static_cast<std::uint32_t>(f_.insts[step.inst].imm));
                        const std::uint32_t words = found == f_.call_results.end() ? 1 : found->second;
                        forbid(v, [&](std::uint32_t r) { return r >= result && r - result < words; });
                    }
                };
                for (std::size_t w = 0; w < words; ++w) {
                    for (auto bits = around[w]; bits != 0; bits &= bits - 1) {
                        check(static_cast<std::uint32_t>(64 * w + static_cast<unsigned>(std::countr_zero(bits))));
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
                    const auto& entries = f_.states[physical(state_id)].slots;
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
        // Binary32 and binary64 in XMM registers (design 7.1); the 16-bit
        // formats through lift::evaluate.
        switch (in.op) {
        case Opcode::FloatAdd:
        case Opcode::FloatSubtract:
        case Opcode::FloatMultiply:
        case Opcode::FloatDivide:
            if (!hardware_float(in.format)) return evaluate();
            shape.a_xmm = shape.b_xmm = shape.def_xmm = true;
            shape.def = D::SameAsA;
            shape.b_memory = true;
            if (c) shape.b = U::Immediate;
            return shape;
        case Opcode::FloatEqual:
        case Opcode::FloatNotEqual:
        case Opcode::FloatGreater:
        case Opcode::FloatGreaterEqual:
            if (!hardware_float(in.format)) return evaluate();
            shape.a_xmm = shape.b_xmm = true;
            shape.b_memory = true;
            if (c) shape.b = U::Immediate;
            return shape;
        case Opcode::FloatLess:
        case Opcode::FloatLessEqual:
            // a < b is b > a: ucomis b, a, with b in a register (a temporary
            // for a constant).
            if (!hardware_float(in.format)) return evaluate();
            shape.a_xmm = shape.b_xmm = true;
            if (c) {
                shape.b = U::Immediate;
                shape.xmm_temps = 1;
            }
            return shape;
        case Opcode::FloatNegate:
            if (in.format == lift::FloatType::None) return evaluate();
            shape.def = D::SameAsA;
            if (hardware_float(in.format)) shape.a_xmm = shape.def_xmm = true;
            return shape;
        case Opcode::FloatConvert:
            if (!hardware_float(in.format) || !hardware_float(in.to_format) || in.format == in.to_format) {
                return evaluate();
            }
            shape.a_xmm = shape.def_xmm = true;
            shape.xmm_temps = 1;
            return shape;
        case Opcode::FloatFromInteger:
            if (!hardware_float(in.to_format) || w == 0 || w > 64) return evaluate();
            shape.def_xmm = true;
            shape.temps = 1;
            return shape;
        case Opcode::FloatToInteger:
            if (!hardware_float(in.format) || in.to_width == 0 || in.to_width > 64) return evaluate();
            shape.a_xmm = true;
            shape.temps = 1;
            return shape;
        default: return evaluate();
        }
    }
    if (in.op == Opcode::ValueHash && in.format != lift::FloatType::None) {
        if (!hardware_float(in.format)) return evaluate();
        shape.a_xmm = true;
        return shape;
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
        shape.def = D::SameAsA;
        return shape;
    default: return evaluate();
    }
}

Lowered lower(const nvm::tier3::Function& function, const std::vector<std::uint32_t>& entries) {
    return Lowering(function, entries).run();
}

std::vector<std::vector<std::uint32_t>> entry_units(const nvm::tier3::Function& function) {
    const auto count = static_cast<std::uint32_t>(function.entries.size());
    if (count == 0) refuse("a function without entries");
    for (std::uint32_t e = 0; e < count; ++e) {
        if (e >= function.blocks.size() || function.blocks[e].succs.size() != 1) {
            refuse("an entry block without one successor");
        }
    }
    // Entry 0 is a unit of its own, and so is every other entry: a function
    // starts at entry 0 with a frame that holds its parameters only, so the
    // initialized flag of every other slot is known there (simplify_phis),
    // which a join with an entry's frame, whose flags are the run's, would
    // lose for every value the code assigns (an entry at a loop head would
    // also add phis that are live through the loop, and an entry inside a
    // loop makes the code irreducible). The other entries' code is made when
    // a run first enters there.
    std::vector<std::vector<std::uint32_t>> units;
    for (std::uint32_t e = 0; e < count; ++e) units.push_back({e});
    return units;
}

} // namespace neura::jit::native
