#include "tier3_passes.hpp"
#include "environment.hpp"

#include "lift/evaluate.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>

namespace neura::nvm::tier3 {
namespace detail {

// --- The graph --------------------------------------------------------------------------------

Cfg::Cfg(const Function& f) {
    const auto n = static_cast<std::uint32_t>(f.blocks.size());
    root = n;
    std::vector<std::uint8_t> seen(n + 1, 0);
    std::vector<std::uint32_t> order;
    std::vector<std::pair<std::uint32_t, std::size_t>> stack{{root, 0}};
    seen[root] = 1;
    const auto successors = [&](std::uint32_t b) -> std::vector<std::uint32_t> {
        if (b == root) {
            std::vector<std::uint32_t> roots;
            for (const auto& entry : f.entries) roots.push_back(entry.ir_block);
            return roots;
        }
        return f.blocks[b].succs;
    };
    while (!stack.empty()) {
        auto& [b, next] = stack.back();
        const auto succs = successors(b);
        if (next == succs.size()) {
            order.push_back(b);
            stack.pop_back();
            continue;
        }
        const auto s = succs[next++];
        if (!seen[s]) {
            seen[s] = 1;
            stack.emplace_back(s, 0);
        }
    }
    std::reverse(order.begin(), order.end());
    number.assign(n + 1, none);
    for (std::uint32_t i = 0; i < order.size(); ++i) number[order[i]] = i;
    for (const auto b : order) {
        if (b != root) rpo.push_back(b);
    }
    idom.assign(n + 1, none);
    idom[root] = root;
    const auto predecessors = [&](std::uint32_t b) {
        std::vector<std::uint32_t> list = f.blocks[b].preds;
        for (const auto& entry : f.entries) {
            if (entry.ir_block == b) list.push_back(root);
        }
        return list;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto b : order) {
            if (b == root) continue;
            std::uint32_t chosen = none;
            for (const auto p : predecessors(b)) {
                if (number[p] == none || idom[p] == none) continue;
                if (chosen == none) {
                    chosen = p;
                    continue;
                }
                auto x = p;
                auto y = chosen;
                while (x != y) {
                    while (number[x] > number[y]) x = idom[x];
                    while (number[y] > number[x]) y = idom[y];
                }
                chosen = x;
            }
            if (chosen != idom[b]) {
                idom[b] = chosen;
                changed = true;
            }
        }
    }
    children.assign(n + 1, {});
    for (const auto b : rpo) children[idom[b]].push_back(b);
}

bool Cfg::dominates(std::uint32_t a, std::uint32_t b) const {
    if (!reachable(a) || !reachable(b)) return false;
    while (b != a && b != root) b = idom[b];
    return b == a;
}

Places::Places(const Function& f) : block(f.insts.size(), none), position(f.insts.size(), 0) {
    for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
        for (std::uint32_t i = 0; i < f.blocks[b].insts.size(); ++i) {
            block[f.blocks[b].insts[i]] = b;
            position[f.blocks[b].insts[i]] = i;
        }
    }
}

bool is_heap_check(Op op) {
    switch (op) {
    case Op::CheckNotNil:
    case Op::Lookup:
    case Op::CheckKind:
    case Op::CheckInit:
    case Op::CheckIndex:
    case Op::CheckGlobalInit: return true;
    default: return false;
    }
}

bool is_heap_read(Op op) {
    switch (op) {
    case Op::TestKind:
    case Op::LoadTag:
    case Op::LoadField:
    case Op::LoadLength:
    case Op::LoadData:
    case Op::LoadStorage:
    case Op::LoadElement:
    case Op::LoadVersion:
    case Op::LoadGlobal: return true;
    default: return false;
    }
}

bool is_effect(const Inst& in) {
    switch (in.op) {
    case Op::StoreField:
    case Op::StoreElement:
    case Op::StoreGlobal:
    case Op::Widen:
    case Op::AllocObject:
    case Op::Append: return true;
    case Op::StateCall: return (in.flags & CheckReference) == 0;
    case Op::WriteBack:
    case Op::Helper:
    case Op::Call:
    case Op::Return:
    case Op::Leave:
    case Op::Deopt: return true;
    default: return false;
    }
}

// --- Replacement ---------------------------------------------------------------------------------

Replacements::Replacements(std::size_t size) : to_(size) {
    for (std::size_t i = 0; i < size; ++i) to_[i] = static_cast<std::uint32_t>(i);
}

void Replacements::grow(std::size_t size) {
    for (auto i = to_.size(); i < size; ++i) to_.push_back(static_cast<std::uint32_t>(i));
}

std::uint32_t Replacements::find(std::uint32_t v) {
    while (to_[v] != v) {
        to_[v] = to_[to_[v]];
        v = to_[v];
    }
    return v;
}

void Replacements::apply(Function& f) {
    for (std::uint32_t id = 0; id < to_.size(); ++id) {
        if (to_[id] != id) any_ = true;
    }
    if (!any_) return;
    for (auto& in : f.insts) {
        for (auto& operand : in.operands) {
            if (operand != none) operand = find(operand);
        }
        for (std::uint32_t i = 0; i < in.count; ++i) {
            if (in.op == Op::Gather && (i % 2) != 0) continue;
            if (in.op != Op::Gather && in.op != Op::Phi && in.op != Op::StateCall && in.op != Op::Helper &&
                in.op != Op::Call && in.op != Op::Select && in.op != Op::AllocObject && in.op != Op::Append &&
                in.op != Op::Return) {
                continue;
            }
            auto& extra = f.extras[in.extra + i];
            if (extra != none) extra = find(extra);
        }
        if (in.guard != none) in.guard = find(in.guard);
    }
    for (auto& state : f.states) {
        if (state.blocks != none) state.blocks = find(state.blocks);
        if (state.kernel_steps != none) state.kernel_steps = find(state.kernel_steps);
        for (auto& slot : state.slots) {
            if (slot.kind != StateSlot::Kind::Value) continue;
            const auto v = find(static_cast<std::uint32_t>(slot.value));
            const auto& in = f.insts[v];
            if (in.op == Op::Const) slot = {StateSlot::Kind::Const, in.imm};
            else slot.value = v;
        }
        for (auto& flag : state.flags) {
            if (flag.kind != StateFlag::Kind::Value) continue;
            const auto v = find(flag.value);
            const auto& in = f.insts[v];
            if (in.op == Op::Const && (in.imm & 1U) != 0) flag = {StateFlag::Kind::One, 0};
            else flag.value = v;
        }
    }
}

// --- Editing the graph -----------------------------------------------------------------------------

namespace {

// Drops operand `index` of the phi `id`.
void drop_phi_operand(Function& f, std::uint32_t id, std::size_t index) {
    auto& phi = f.insts[id];
    const auto old = phi.extra;
    phi.extra = static_cast<std::uint32_t>(f.extras.size());
    for (std::uint32_t i = 0; i < phi.count; ++i) {
        if (i != index) f.extras.push_back(f.extras[old + i]);
    }
    --phi.count;
}

} // namespace

void remove_edge_target(Function& f, std::uint32_t from, std::uint32_t to) {
    auto& target = f.blocks[to];
    const auto found = std::find(target.preds.begin(), target.preds.end(), from);
    if (found == target.preds.end()) throw std::logic_error("tier 3: an edge to remove is missing");
    const auto index = static_cast<std::size_t>(found - target.preds.begin());
    target.preds.erase(found);
    for (const auto id : target.insts) {
        if (f.insts[id].op != Op::Phi) break;
        drop_phi_operand(f, id, index);
    }
}

bool remove_unreachable(Function& f) {
    const auto n = f.blocks.size();
    std::vector<std::uint8_t> reached(n, 0);
    std::vector<std::uint32_t> work;
    for (const auto& entry : f.entries) {
        if (!reached[entry.ir_block]) {
            reached[entry.ir_block] = 1;
            work.push_back(entry.ir_block);
        }
    }
    while (!work.empty()) {
        const auto b = work.back();
        work.pop_back();
        for (const auto s : f.blocks[b].succs) {
            if (!reached[s]) {
                reached[s] = 1;
                work.push_back(s);
            }
        }
    }
    if (std::all_of(reached.begin(), reached.end(), [](std::uint8_t r) { return r != 0; })) return false;
    for (std::uint32_t d = 0; d < n; ++d) {
        if (reached[d]) continue;
        for (const auto s : f.blocks[d].succs) {
            if (reached[s]) remove_edge_target(f, d, s);
        }
    }
    std::vector<std::uint32_t> renumber(n, none);
    std::vector<Block> blocks;
    for (std::uint32_t b = 0; b < n; ++b) {
        if (!reached[b]) continue;
        renumber[b] = static_cast<std::uint32_t>(blocks.size());
        blocks.push_back(std::move(f.blocks[b]));
    }
    for (auto& block : blocks) {
        for (auto& p : block.preds) p = renumber[p];
        for (auto& s : block.succs) s = renumber[s];
    }
    for (auto& entry : f.entries) entry.ir_block = renumber[entry.ir_block];
    f.blocks = std::move(blocks);
    return true;
}

void compact(Function& f) {
    const auto n = f.insts.size();
    std::vector<std::uint32_t> renumber(n, none);
    std::vector<Inst> insts;
    // Block order, so that a listing reads in the order the blocks do.
    std::vector<std::uint32_t> order;
    for (const auto& block : f.blocks) {
        for (const auto id : block.insts) order.push_back(id);
    }
    // Values in id order keep their relative order (deterministic and close
    // to the listing the builder gave).
    std::vector<std::uint8_t> kept(n, 0);
    for (const auto id : order) kept[id] = 1;
    for (std::uint32_t id = 0; id < n; ++id) {
        if (!kept[id]) continue;
        renumber[id] = static_cast<std::uint32_t>(insts.size());
        insts.push_back(std::move(f.insts[id]));
    }
    std::vector<std::uint8_t> state_used(f.states.size(), 0);
    for (const auto& in : insts) {
        for (auto s = in.state; s != none && !state_used[s]; s = f.states[s].caller) state_used[s] = 1;
    }
    std::vector<std::uint32_t> state_number(f.states.size(), none);
    std::vector<FrameState> states;
    for (std::uint32_t s = 0; s < f.states.size(); ++s) {
        if (!state_used[s]) continue;
        state_number[s] = static_cast<std::uint32_t>(states.size());
        states.push_back(std::move(f.states[s]));
    }
    const auto map = [&](std::uint32_t v) { return v == none ? none : renumber[v]; };
    std::vector<std::uint32_t> extras;
    for (auto& in : insts) {
        for (auto& operand : in.operands) operand = map(operand);
        if (in.state != none) in.state = state_number[in.state];
        in.guard = map(in.guard);
        if (in.count != 0) {
            const auto old = in.extra;
            in.extra = static_cast<std::uint32_t>(extras.size());
            for (std::uint32_t i = 0; i < in.count; ++i) {
                auto v = f.extras[old + i];
                if (in.op != Op::Gather || (i % 2) == 0) v = map(v);
                extras.push_back(v);
            }
        } else {
            in.extra = 0;
        }
    }
    for (auto& state : states) {
        state.blocks = map(state.blocks);
        state.kernel_steps = map(state.kernel_steps);
        for (auto& slot : state.slots) {
            if (slot.kind == StateSlot::Kind::Value) slot.value = map(static_cast<std::uint32_t>(slot.value));
        }
        for (auto& flag : state.flags) {
            if (flag.kind == StateFlag::Kind::Value) flag.value = map(flag.value);
        }
        if (state.caller != none) state.caller = state_number[state.caller];
    }
    for (auto& block : f.blocks) {
        for (auto& id : block.insts) id = renumber[id];
    }
    if (!f.frame_canon.empty()) {
        std::vector<std::uint8_t> canon(insts.size(), 0);
        for (std::uint32_t id = 0; id < n && id < f.frame_canon.size(); ++id) {
            if (renumber[id] != none) canon[renumber[id]] = f.frame_canon[id];
        }
        f.frame_canon = std::move(canon);
    }
    f.insts = std::move(insts);
    f.states = std::move(states);
    f.extras = std::move(extras);
}

void compute_budgets(Function& f) {
    const auto n = f.blocks.size();
    std::vector<std::uint64_t> weight(n, 0);
    std::vector<std::uint8_t> starts(n, 0);
    for (std::uint32_t b = 0; b < n; ++b) {
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            if (in.op == Op::CounterAdd) weight[b] += in.imm;
            if (in.op == Op::Return) weight[b] += 1;
        }
        for (const auto id : f.blocks[b].insts) {
            if (f.insts[id].op == Op::Phi) continue;
            starts[b] = f.insts[id].op == Op::Budget;
            break;
        }
    }
    std::vector<std::uint64_t> longest(n, 0);
    std::vector<std::uint8_t> state(n, 0);
    for (std::uint32_t root = 0; root < n; ++root) {
        if (state[root] != 0) continue;
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{root, 0}};
        state[root] = 1;
        while (!stack.empty()) {
            auto& [b, next] = stack.back();
            const auto& succs = f.blocks[b].succs;
            if (next == succs.size()) {
                std::uint64_t best = 0;
                for (const auto s : succs) {
                    if (!starts[s]) best = std::max(best, longest[s]);
                }
                longest[b] = weight[b] + best;
                state[b] = 2;
                stack.pop_back();
                continue;
            }
            const auto s = succs[next++];
            if (starts[s]) continue;
            if (state[s] == 1) throw std::logic_error("tier 3: a loop without a checkpoint");
            if (state[s] == 0) {
                state[s] = 1;
                stack.emplace_back(s, 0);
            }
        }
    }
    for (std::uint32_t b = 0; b < n; ++b) {
        for (const auto id : f.blocks[b].insts) {
            if (f.insts[id].op == Op::Budget) f.insts[id].imm = longest[b];
        }
    }
}

// --- Constant folding and copy propagation ---------------------------------------------------------

namespace {

bool is_const(const Function& f, std::uint32_t v) { return v != none && f.insts[v].op == Op::Const; }

std::uint64_t mask_bits(unsigned width) { return lift::mask_of(width); }

// Rewrites `in` into a constant of its own type, keeping its place and id.
void make_constant(Inst& in, std::uint64_t bits) {
    in.op = Op::Const;
    in.operands[0] = in.operands[1] = in.operands[2] = none;
    in.count = 0;
    in.extra = 0;
    in.imm = bits;
    in.lifted = {};
    in.state = none;
    in.guard = none;
    in.flags = 0;
    in.aux = none;
}

// Folds the instructions whose operands are constants, in place. A Lift whose
// evaluation fails stays.
bool fold_values(Function& f, const Cfg& cfg, Counts& counts) {
    bool changed = false;
    const bool float_default = lift::float_environment_is_default();
    for (const auto b : cfg.rpo) {
        for (const auto id : f.blocks[b].insts) {
            auto& in = f.insts[id];
            switch (in.op) {
            case Op::Lift: {
                const auto arity_a = in.operands[0];
                const auto arity_b = in.operands[1];
                if ((arity_a != none && !is_const(f, arity_a)) || (arity_b != none && !is_const(f, arity_b))) break;
                if (lift::float_opcode(in.lifted.op) && !float_default) break;
                bool failed = false;
                const auto a = arity_a == none ? 0 : f.insts[arity_a].imm;
                const auto c = arity_b == none ? 0 : f.insts[arity_b].imm;
                const auto result = lift::evaluate(in.lifted, a, c, failed);
                if (failed) break;
                make_constant(in, result);
                ++counts["fold.lift"];
                changed = true;
                break;
            }
            case Op::Mask:
                if (!is_const(f, in.operands[0])) break;
                make_constant(in, f.insts[in.operands[0]].imm & mask_bits(static_cast<unsigned>(in.imm)));
                ++counts["fold.mask"];
                changed = true;
                break;
            case Op::Gather: {
                bool all = true;
                for (std::uint32_t i = 0; i < in.count; i += 2) all = all && is_const(f, f.extras[in.extra + i]);
                if (!all) break;
                std::uint64_t result = 0;
                for (std::uint32_t i = 0; i < in.imm; ++i) {
                    const auto source = f.insts[f.extras[in.extra + 2 * i]].imm;
                    result |= ((source >> f.extras[in.extra + 2 * i + 1]) & 1U) << i;
                }
                make_constant(in, result);
                ++counts["fold.gather"];
                changed = true;
                break;
            }
            default: break;
            }
        }
    }
    return changed;
}

// C + k1 + k2 becomes C + (k1 + k2) when the first sum has no other reader.
bool merge_counter_adds(Function& f, Counts& counts) {
    std::vector<std::uint32_t> uses(f.insts.size(), 0);
    for (const auto& block : f.blocks) {
        for (const auto id : block.insts) {
            const auto& in = f.insts[id];
            for_each_operand(f, in, [&](std::uint32_t v) { ++uses[v]; });
            if (in.state != none) for_each_state_value(f, in.state, [&](std::uint32_t v) { ++uses[v]; });
        }
    }
    // A state is shared by several ops; a value it names is read once per
    // reader, which only ever over-counts and so only keeps more sums.
    bool changed = false;
    std::vector<std::uint8_t> removed(f.insts.size(), 0);
    for (const auto& block : f.blocks) {
        for (const auto id : block.insts) {
            auto& in = f.insts[id];
            if (removed[id] || in.op != Op::CounterAdd) continue;
            const auto inner = in.operands[0];
            if (inner == none || removed[inner] || f.insts[inner].op != Op::CounterAdd || uses[inner] != 1) continue;
            const auto& first = f.insts[inner];
            in.operands[0] = first.operands[0];
            in.imm += first.imm;
            removed[inner] = 1;
            uses[inner] = 0;
            ++counts["fold.counter"];
            changed = true;
        }
    }
    if (!changed) return false;
    for (auto& block : f.blocks) {
        std::vector<std::uint32_t> kept;
        for (const auto id : block.insts) {
            if (!removed[id]) kept.push_back(id);
        }
        block.insts = std::move(kept);
    }
    return true;
}

// Branch, Switch and Dispatch on a constant become a Jump; the edges not
// taken go, with their phi operands.
bool fold_control(Function& f, Counts& counts) {
    bool changed = false;
    for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
        auto& block = f.blocks[b];
        if (block.insts.empty()) continue;
        auto& last = f.insts[block.insts.back()];
        std::uint32_t taken = none;
        const char* kind = nullptr;
        switch (last.op) {
        case Op::Branch:
            if (!is_const(f, last.operands[0])) continue;
            taken = (f.insts[last.operands[0]].imm & 1U) != 0 ? 0 : 1;
            kind = "fold.branch";
            break;
        case Op::Switch:
            if (!is_const(f, last.operands[0])) continue;
            // An index past the exits stays: its SwitchCheck deoptimizes.
            if (f.insts[last.operands[0]].imm >= block.succs.size()) continue;
            taken = static_cast<std::uint32_t>(f.insts[last.operands[0]].imm);
            kind = "fold.switch";
            break;
        case Op::Dispatch: {
            if (!is_const(f, last.operands[0])) continue;
            const auto& table = f.dispatch_tables[last.imm];
            const auto tag = f.insts[last.operands[0]].imm;
            const auto found = std::lower_bound(table.exits.begin(), table.exits.end(), tag,
                                                [](const auto& entry, std::uint64_t key) { return entry.first < key; });
            taken = found != table.exits.end() && found->first == tag ? found->second : table.fallback;
            if (taken >= block.succs.size()) continue;
            kind = "fold.dispatch";
            break;
        }
        default: continue;
        }
        const auto target = block.succs[taken];
        for (std::uint32_t i = 0; i < block.succs.size(); ++i) {
            if (i != taken) remove_edge_target(f, b, block.succs[i]);
        }
        block.succs.assign(1, target);
        last.op = Op::Jump;
        last.operands[0] = none;
        last.imm = 0;
        ++counts[kind];
        changed = true;
    }
    return changed;
}

// A phi that joins one value (and itself) is that value; one that joins one
// constant everywhere is a constant of the block. Returns whether any phi
// went.
bool copy_phis(Function& f, Counts& counts) {
    Replacements repl(f.insts.size());
    bool any = false;
    // Phis whose replacement was set this round: their operands read through
    // the replacement.
    for (bool again = true; again;) {
        again = false;
        for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
            for (const auto id : f.blocks[b].insts) {
                const auto& phi = f.insts[id];
                if (phi.op != Op::Phi) break;
                if (repl.find(id) != id) continue;
                std::uint32_t same = none;
                bool trivial = true;
                for (std::uint32_t i = 0; i < phi.count && trivial; ++i) {
                    const auto operand = repl.find(f.extras[phi.extra + i]);
                    if (operand == id || operand == same) continue;
                    if (same != none) trivial = false;
                    else same = operand;
                }
                if (trivial && same != none) {
                    repl.set(id, same);
                    ++counts["copy.phi"];
                    any = again = true;
                }
            }
        }
    }
    // A phi whose operands are all the same constant (different instances).
    for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
        auto& block = f.blocks[b];
        std::size_t phis = 0;
        while (phis < block.insts.size() && f.insts[block.insts[phis]].op == Op::Phi) ++phis;
        // A Budget stays the first op after the phis: it is where a
        // checkpoint starts (compute_budgets, V8).
        if (phis < block.insts.size() && f.insts[block.insts[phis]].op == Op::Budget) continue;
        std::vector<std::uint32_t> made;
        for (std::size_t k = 0; k < phis; ++k) {
            const auto id = block.insts[k];
            const auto phi = f.insts[id];
            if (repl.find(id) != id) continue;
            std::uint64_t value = 0;
            bool first = true;
            bool constant = true;
            for (std::uint32_t i = 0; i < phi.count && constant; ++i) {
                const auto operand = repl.find(f.extras[phi.extra + i]);
                if (operand == id) continue;
                if (!is_const(f, operand)) {
                    constant = false;
                    break;
                }
                const auto imm = f.insts[operand].imm;
                if (first) value = imm;
                else if (imm != value) constant = false;
                first = false;
            }
            if (!constant || first) continue;
            Inst c;
            c.op = Op::Const;
            c.type = phi.type;
            c.imm = value;
            c.origin_block = phi.origin_block;
            c.origin_function = phi.origin_function;
            const auto new_id = static_cast<std::uint32_t>(f.insts.size());
            f.insts.push_back(c);
            repl.grow(f.insts.size());
            made.push_back(new_id);
            repl.set(id, new_id);
            ++counts["copy.phi_const"];
            any = true;
        }
        if (!made.empty()) block.insts.insert(block.insts.begin() + static_cast<std::ptrdiff_t>(phis), made.begin(), made.end());
    }
    if (!any) return false;
    repl.apply(f);
    // The replaced phis are gone from their blocks.
    for (auto& block : f.blocks) {
        std::vector<std::uint32_t> kept;
        for (const auto id : block.insts) {
            if (f.insts[id].op == Op::Phi && repl.find(id) != id) continue;
            kept.push_back(id);
        }
        block.insts = std::move(kept);
    }
    return true;
}

} // namespace

bool simplify(Function& f, Counts& counts) {
    bool any = false;
    for (unsigned round = 0; round < 32; ++round) {
        bool changed = false;
        {
            Cfg cfg(f);
            changed |= fold_values(f, cfg, counts);
        }
        changed |= fold_control(f, counts);
        changed |= remove_unreachable(f);
        changed |= copy_phis(f, counts);
        changed |= merge_counter_adds(f, counts);
        if (!changed) break;
        any = true;
    }
    return any;
}

// --- Value numbering ---------------------------------------------------------------------------------

namespace {

using Key = std::vector<std::uint64_t>;

// The key of a value that may be numbered: the pure ops and the lifted ops
// (a checked one stands for its own check: after it nothing the same can
// fail). The operands are read through `repl`.
bool value_key(const Function& f, const Inst& in, Replacements& repl, bool checked, Key& key) {
    switch (in.op) {
    case Op::Const:
    case Op::RunConst:
    case Op::Mask:
    case Op::Gather:
    case Op::Lift: break;
    default: return false;
    }
    key.clear();
    key.push_back(static_cast<std::uint64_t>(in.op));
    key.push_back((static_cast<std::uint64_t>(in.type.type) << 8) | in.type.width);
    key.push_back(in.imm);
    key.push_back(checked ? 1 : 0);
    if (in.op == Op::Lift) {
        const auto& l = in.lifted;
        key.push_back(static_cast<std::uint64_t>(l.op));
        key.push_back(l.width);
        key.push_back(l.is_signed);
        key.push_back(l.to_width);
        key.push_back(l.to_signed);
        key.push_back(static_cast<std::uint64_t>(l.format));
        key.push_back(static_cast<std::uint64_t>(l.to_format));
        key.push_back(l.immediate);
    }
    for (const auto operand : in.operands) key.push_back(operand == none ? ~std::uint64_t{0} : repl.find(operand));
    if (in.op == Op::Gather) {
        for (std::uint32_t i = 0; i < in.count; ++i) {
            const auto v = f.extras[in.extra + i];
            key.push_back((i % 2) == 0 ? repl.find(v) : v);
        }
    }
    return true;
}

// What a heap read or check depends on, as versions that change where a
// write may change it (docs/runtime/NVM.md, "Execution tiers"): the alias
// classes of the heap (a field index of any object, the elements of a
// declared width, the element storage of arrays and lists, a global) and,
// under every class, the entries that are live and the words that only a
// helper or a call changes. A merge inherits the immediate dominator's
// version of an alias class only when no incoming path writes that class.
class Memory {
public:
    // Alias classes.
    static std::uint64_t field(std::uint64_t index) { return (std::uint64_t{1} << 56) | index; }
    static std::uint64_t elements(std::uint64_t width) { return (std::uint64_t{2} << 56) | width; }
    static constexpr std::uint64_t storage = std::uint64_t{3} << 56;
    static std::uint64_t global(std::uint64_t index) { return (std::uint64_t{4} << 56) | index; }
    // Entries live, and words only helpers and calls write.
    static constexpr std::uint64_t entries = std::uint64_t{5} << 56;
    // The words of one component of the elements of collections of
    // aggregates (NVR 1.7): a component access names it in `aux`.
    static std::uint64_t component(std::uint64_t index) { return (std::uint64_t{6} << 56) | index; }
    static std::uint64_t element_class(const Inst& in) {
        return in.aux != none ? component(in.aux) : elements(in.imm);
    }

    explicit Memory(std::uint64_t& counter) : counter_(&counter), base_(++counter) {}

    std::uint64_t version(std::uint64_t cls) const {
        const auto found = classes_.find(cls);
        return found == classes_.end() ? base_ : found->second;
    }
    void kill_all() {
        base_ = ++*counter_;
        classes_.clear();
    }
    void kill(std::uint64_t cls) { classes_[cls] = ++*counter_; }

    // A wildcard invalidates all classes. Other effects invalidate only
    // the same classes they already invalidated on a straight-line path.
    static constexpr std::uint64_t all = 0;
    template <typename Kill>
    static void writes(const Inst& in, Kill kill) {
        switch (in.op) {
        case Op::StoreField: kill(field(in.imm)); break;
        case Op::StoreElement: kill(element_class(in)); break;
        case Op::StoreGlobal: kill(global(in.imm)); break;
        case Op::Widen:
            kill(storage);
            for (const std::uint64_t w : {1, 2, 4, 8}) kill(elements(w));
            break;
        case Op::Helper:
        case Op::Call:
        case Op::AllocObject:
        case Op::Append: kill(all); break;
        case Op::StateCall:
            if ((in.flags & CheckReference) == 0) kill(all);
            break;
        default: break;
        }
    }

    void apply(const Inst& in) {
        writes(in, [&](std::uint64_t cls) { accept_write(cls); });
    }

    void accept_write(std::uint64_t cls) {
        if (cls == all) kill_all();
        else kill(cls);
    }

private:
    std::uint64_t* counter_;
    std::uint64_t base_;
    std::map<std::uint64_t, std::uint64_t> classes_;
};

// The class a heap read or check depends on; none for one that depends on
// its operands alone.
std::uint64_t depends_on(const Inst& in) {
    switch (in.op) {
    case Op::LoadField: return Memory::field(in.imm);
    case Op::LoadElement: return Memory::element_class(in);
    case Op::LoadData:
    case Op::LoadStorage: return Memory::storage;
    case Op::LoadGlobal: return Memory::global(in.imm);
    case Op::Lookup:
    case Op::CheckKind:
    case Op::CheckInit:
    case Op::CheckGlobalInit:
    case Op::TestKind:
    case Op::LoadTag:
    case Op::LoadLength:
    case Op::LoadVersion: return Memory::entries;
    default: return none;
    }
}

// The key of a heap read or check under `memory`.
bool heap_key(const Inst& in, Replacements& repl, const Memory& memory, Key& key) {
    if (!is_heap_check(in.op) && !is_heap_read(in.op)) return false;
    key.clear();
    key.push_back(0x100 + static_cast<std::uint64_t>(in.op));
    key.push_back((static_cast<std::uint64_t>(in.type.type) << 8) | in.type.width);
    key.push_back(in.imm);
    key.push_back(in.aux);
    for (const auto operand : in.operands) key.push_back(operand == none ? ~std::uint64_t{0} : repl.find(operand));
    const auto cls = depends_on(in);
    key.push_back(cls == none ? 0 : memory.version(cls));
    return true;
}

// The key of the check a write makes true: StoreField sets its field's
// initialized bit, StoreGlobal its global's.
bool implied_check(const Inst& in, Replacements& repl, const Memory& memory, Key& key) {
    Inst check;
    if (in.op == Op::StoreField) {
        check.op = Op::CheckInit;
        check.operands[0] = in.operands[0];
    } else if (in.op == Op::StoreGlobal) {
        check.op = Op::CheckGlobalInit;
    } else {
        return false;
    }
    check.imm = in.imm;
    return heap_key(check, repl, memory, key);
}

const char* heap_check_kind(Op op) {
    switch (op) {
    case Op::CheckNotNil: return "nil";
    case Op::Lookup: return "lookup";
    case Op::CheckKind: return "kind";
    case Op::CheckInit: return "init";
    case Op::CheckIndex: return "bounds";
    case Op::CheckGlobalInit: return "global";
    default: return "read";
    }
}

// The values of the dominating blocks, scoped by the dominator tree; heap
// reads and checks under the versions of what they read (Memory).
bool number_pure(Function& f, Counts& counts) {
    const Cfg cfg(f);
    Replacements repl(f.insts.size());
    std::map<Key, std::uint32_t> table;
    std::vector<std::uint32_t> removed;
    struct Frame {
        std::uint32_t block;
        std::size_t child;
        std::vector<Key> added;
    };
    std::vector<Frame> stack;
    std::uint64_t counter = 0;
    std::vector<Memory> ends(f.blocks.size(), Memory(counter));
    std::vector<std::vector<std::uint64_t>> writes(f.blocks.size());
    for (const auto b : cfg.rpo) {
        auto& classes = writes[b];
        for (const auto id : f.blocks[b].insts) {
            Memory::writes(f.insts[id], [&](std::uint64_t cls) { classes.push_back(cls); });
        }
        std::sort(classes.begin(), classes.end());
        classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
    }
    std::vector<std::uint32_t> visited(f.blocks.size(), none);
    const auto entry_memory = [&](std::uint32_t b) {
        const auto dominator = cfg.idom[b];
        if (dominator == cfg.root || f.blocks[b].entry != none) return Memory(counter);
        Memory memory = ends[dominator];
        if (f.blocks[b].preds.size() == 1 && f.blocks[b].preds[0] == dominator) return memory;
        std::vector<std::uint32_t> work;
        // Walk back to the dominator. Include the merge itself if a back
        // edge can revisit it, since its previous iteration may have written.
        // Bound compile work at big joins by forgetting memory facts there.
        constexpr std::size_t max_work = 1024;
        std::size_t remaining = max_work;
        const auto queue_predecessors = [&](std::uint32_t block) {
            const auto& predecessors = f.blocks[block].preds;
            if (predecessors.size() > remaining) return false;
            remaining -= predecessors.size();
            work.insert(work.end(), predecessors.begin(), predecessors.end());
            return true;
        };
        if (!queue_predecessors(b)) return Memory(counter);
        while (!work.empty()) {
            const auto p = work.back();
            work.pop_back();
            if (p == dominator || !cfg.reachable(p) || visited[p] == b) continue;
            visited[p] = b;
            if (writes[p].size() > remaining) return Memory(counter);
            remaining -= writes[p].size();
            for (const auto cls : writes[p]) {
                if (cls == Memory::all) return Memory(counter);
                memory.accept_write(cls);
            }
            if (!queue_predecessors(p)) return Memory(counter);
        }
        return memory;
    };
    const auto enter = [&](std::uint32_t b) {
        Frame frame{b, 0, {}};
        Memory memory = entry_memory(b);
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            Key own;
            if (heap_key(in, repl, memory, own)) {
                const auto found = table.find(own);
                if (found != table.end()) {
                    repl.set(id, found->second);
                    removed.push_back(id);
                    ++counts[std::string("gvn.") + heap_check_kind(in.op)];
                    continue;
                }
                table[own] = id;
                frame.added.push_back(std::move(own));
                continue;
            }
            if (implied_check(in, repl, memory, own)) {
                memory.apply(in);
                // The write's own key: the check after it, under the
                // versions after the write.
                implied_check(in, repl, memory, own);
                if (!table.contains(own)) {
                    table[own] = id;
                    frame.added.push_back(std::move(own));
                }
                continue;
            }
            memory.apply(in);
            const bool checked = in.op == Op::Lift && (in.flags & Checked) != 0;
            if (!value_key(f, in, repl, checked, own)) continue;
            // An unchecked op may merge into a checked one: after the check
            // passed the same operation cannot fail.
            auto found = table.find(own);
            if (found == table.end() && in.op == Op::Lift && !checked) {
                Key other;
                value_key(f, in, repl, true, other);
                found = table.find(other);
            }
            if (found != table.end() && f.insts[found->second].type == in.type) {
                repl.set(id, found->second);
                removed.push_back(id);
                ++counts[checked ? "gvn.check" : "gvn.value"];
                continue;
            }
            table[own] = id;
            frame.added.push_back(std::move(own));
        }
        ends[b] = memory;
        stack.push_back(std::move(frame));
    };
    for (const auto top : cfg.children[cfg.root]) {
        enter(top);
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& kids = cfg.children[frame.block];
            if (frame.child < kids.size()) {
                const auto next = kids[frame.child++];
                enter(next);
                continue;
            }
            for (const auto& key : frame.added) table.erase(key);
            stack.pop_back();
        }
    }
    if (removed.empty()) return false;
    repl.apply(f);
    std::vector<std::uint8_t> gone(f.insts.size(), 0);
    for (const auto id : removed) gone[id] = 1;
    for (auto& block : f.blocks) {
        std::vector<std::uint32_t> kept;
        for (const auto id : block.insts) {
            if (!gone[id]) kept.push_back(id);
        }
        block.insts = std::move(kept);
    }
    return true;
}

// A reference check that every path to it has already made, with nothing
// that can free an object (a helper or a call) between: the object is still
// live and of the type.
bool remove_repeated_reference_checks(Function& f, Counts& counts) {
    const Cfg cfg(f);
    std::map<std::pair<std::uint32_t, std::uint64_t>, std::uint32_t> index;
    for (const auto b : cfg.rpo) {
        for (const auto id : f.blocks[b].insts) {
            const auto& in = f.insts[id];
            if (in.op == Op::StateCall && (in.flags & CheckReference) != 0) {
                index.emplace(std::make_pair(in.operands[0], in.imm), static_cast<std::uint32_t>(index.size()));
            }
        }
    }
    if (index.empty()) return false;
    const auto k = index.size();
    const auto n = f.blocks.size();
    std::vector<std::vector<std::uint8_t>> out(n, std::vector<std::uint8_t>(k, 1));
    std::vector<std::uint8_t> is_entry(n, 0);
    for (const auto& entry : f.entries) is_entry[entry.ir_block] = 1;
    const auto enter_set = [&](std::uint32_t b) {
        std::vector<std::uint8_t> in(k, 0);
        if (is_entry[b]) return in;
        in.assign(k, 1);
        for (const auto p : f.blocks[b].preds) {
            if (!cfg.reachable(p)) continue;
            for (std::size_t i = 0; i < k; ++i) in[i] &= out[p][i];
        }
        return in;
    };
    const auto step = [&](const Inst& in, std::vector<std::uint8_t>& set, bool& redundant) {
        redundant = false;
        if (in.op == Op::StateCall && (in.flags & CheckReference) != 0) {
            auto& bit = set[index.at(std::make_pair(in.operands[0], in.imm))];
            redundant = bit != 0;
            bit = 1;
        } else if (in.op == Op::Helper || in.op == Op::Call || in.op == Op::AllocObject || in.op == Op::Append) {
            std::fill(set.begin(), set.end(), 0);
        }
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto b : cfg.rpo) {
            auto set = enter_set(b);
            for (const auto id : f.blocks[b].insts) {
                bool redundant = false;
                step(f.insts[id], set, redundant);
            }
            if (set != out[b]) {
                out[b] = std::move(set);
                changed = true;
            }
        }
    }
    bool any = false;
    for (const auto b : cfg.rpo) {
        auto set = enter_set(b);
        std::vector<std::uint32_t> kept;
        for (const auto id : f.blocks[b].insts) {
            bool redundant = false;
            step(f.insts[id], set, redundant);
            if (redundant) {
                ++counts["gvn.reference_check"];
                any = true;
            } else {
                kept.push_back(id);
            }
        }
        f.blocks[b].insts = std::move(kept);
    }
    return any;
}

} // namespace

bool number_values(Function& f, Counts& counts) {
    bool any = number_pure(f, counts);
    any |= remove_repeated_reference_checks(f, counts);
    return any;
}

// --- Dead code -------------------------------------------------------------------------------------------

bool remove_dead(Function& f, Counts& counts) {
    const auto n = f.insts.size();
    std::vector<std::uint8_t> live(n, 0);
    std::vector<std::uint32_t> work;
    const auto mark = [&](std::uint32_t v) {
        if (v != none && !live[v]) {
            live[v] = 1;
            work.push_back(v);
        }
    };
    const auto removable = [&](const Inst& in) {
        switch (in.op) {
        case Op::Const:
        case Op::RunConst:
        case Op::EntryLoad:
        case Op::EntryFlag:
        case Op::EntryCounter:
        case Op::Phi:
        case Op::Mask:
        case Op::Gather:
        case Op::Select: return true;
        case Op::Lift: return (in.flags & Checked) == 0;
        default: return is_heap_read(in.op);
        }
    };
    for (const auto& block : f.blocks) {
        for (const auto id : block.insts) {
            if (!removable(f.insts[id])) mark(id);
        }
    }
    while (!work.empty()) {
        const auto v = work.back();
        work.pop_back();
        const auto& in = f.insts[v];
        for_each_operand(f, in, [&](std::uint32_t operand) { mark(operand); });
        if (in.state != none) for_each_state_value(f, in.state, [&](std::uint32_t operand) { mark(operand); });
        mark(in.guard);
    }
    bool any = false;
    for (auto& block : f.blocks) {
        std::vector<std::uint32_t> kept;
        for (const auto id : block.insts) {
            if (live[id]) {
                kept.push_back(id);
            } else {
                ++counts["dce.values"];
                any = true;
            }
        }
        block.insts = std::move(kept);
    }
    return any;
}

} // namespace detail

// --- The pipeline ----------------------------------------------------------------------------------

bool passes_enabled() {
    static const bool enabled = [] {
        require_bound_environment("NEURA_TIER3_PASSES");
        const char* text = std::getenv("NEURA_TIER3_PASSES");
        return !(text && std::strcmp(text, "0") == 0);
    }();
    return enabled;
}

void optimize(const nvr::Image& image, Function& f, bool verify_each) {
    using namespace detail;
    Counts counts = std::move(f.pass_counts);
    const auto check = [&](const char* pass) {
        if (!verify_each) return;
        const auto violations = verify(image, f);
        if (violations.empty()) return;
        std::fprintf(stderr, "neura: NEURA_TIER3_VERIFY: the IR of function %u is not well formed after the %s pass:\n",
                     f.function, pass);
        for (const auto& violation : violations) std::fprintf(stderr, "  %s\n", violation.c_str());
        std::fprintf(stderr, "%s", describe(f).c_str());
        std::abort();
    };
    const auto run = [&](const char* name, auto&& pass) {
        pass();
        compact(f);
        check(name);
    };
    run("fold and propagate", [&] { simplify(f, counts); });
    run("value numbering", [&] { number_values(f, counts); });
    run("check elimination", [&] { eliminate_checks(image, f, counts); });
    run("fold and propagate", [&] { simplify(f, counts); });
    run("loop-invariant code motion", [&] { hoist_invariants(f, counts); });
    run("value numbering", [&] { number_values(f, counts); });
    run("dead code removal", [&] { remove_dead(f, counts); });
    run("checkpoint lengths", [&] { compute_budgets(f); });
    f.pass_counts = std::move(counts);
}

} // namespace neura::nvm::tier3
