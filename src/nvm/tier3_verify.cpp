#include "tier3.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace neura::nvm::tier3 {
namespace {

bool has_value(Op op) {
    switch (op) {
    case Op::Const:
    case Op::RunConst:
    case Op::EntryLoad:
    case Op::EntryFlag:
    case Op::EntryCounter:
    case Op::Phi:
    case Op::Lift:
    case Op::Mask:
    case Op::Gather:
    case Op::CounterAdd:
    case Op::Helper:
    case Op::Call: return true;
    default: return false;
    }
}

bool terminator(Op op) {
    switch (op) {
    case Op::Jump:
    case Op::Branch:
    case Op::Switch:
    case Op::Dispatch:
    case Op::Leave:
    case Op::Deopt:
    case Op::Return: return true;
    default: return false;
    }
}

bool needs_state(const Inst& in) {
    switch (in.op) {
    case Op::Budget:
    case Op::SwitchCheck:
    case Op::Guard:
    case Op::StateCall:
    case Op::WriteBack:
    case Op::Helper:
    case Op::Call:
    case Op::Return:
    case Op::Deopt: return true;
    case Op::Lift: return (in.flags & Checked) != 0;
    default: return false;
    }
}

// An op that changes state outside the SSA values.
bool effect(const Inst& in) {
    switch (in.op) {
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

// An op after which no check of the same image block may run (V5): the ones
// that change what tier 1 would re-run.
bool commits(const Inst& in) {
    switch (in.op) {
    case Op::StateCall: return (in.flags & CheckReference) == 0;
    case Op::WriteBack:
    case Op::Helper:
    case Op::Call: return true;
    default: return false;
    }
}

bool safepoint(const Inst& in) {
    return in.op == Op::Helper || in.op == Op::Call || in.op == Op::StateCall;
}

const char* name_of(Op op) {
    static const char* names[] = {"const",     "runconst", "entryload", "entryflag", "entrycounter", "phi",
                                  "lift",      "mask",     "gather",    "counteradd", "budget",     "switchcheck",
                                  "guard",     "statecall", "writeback", "helper",    "call",         "return",
                                  "jump",      "branch",   "switch",    "dispatch",  "leave",        "deopt"};
    return names[static_cast<int>(op)];
}

std::string type_name(ValueType type) {
    switch (type.type) {
    case Type::Int: return "i" + std::to_string(type.width);
    case Type::F32: return "f32";
    case Type::F64: return "f64";
    case Type::Ref: return "ref";
    case Type::Ptr: return "ptr";
    case Type::None: return "none";
    }
    return "?";
}

class Verifier {
public:
    Verifier(const nvr::Image& image, const Function& function) : image_(image), f_(function) {}

    std::vector<std::string> run() {
        if (!structure()) return violations_;
        dominators();
        frame_writes();
        ssa();
        types();
        states();
        order();
        commit_rule();
        pointers();
        roots();
        budgets();
        guards();
        return violations_;
    }

private:
    void fail(const std::string& rule, const std::string& text) { violations_.push_back(rule + ": " + text); }

    std::string at(std::uint32_t id) const {
        std::ostringstream out;
        out << "%" << id << " (" << name_of(f_.insts[id].op) << ") in block " << block_of_[id];
        return out.str();
    }

    // The values an op reads.
    std::vector<std::uint32_t> uses(const Inst& in) const {
        std::vector<std::uint32_t> list;
        for (const auto operand : in.operands) {
            if (operand != none) list.push_back(operand);
        }
        if (in.op == Op::Gather) {
            for (std::uint32_t i = 0; i < in.count; i += 2) list.push_back(f_.extras[in.extra + i]);
        } else if (in.op == Op::Phi || in.op == Op::StateCall || in.op == Op::Helper || in.op == Op::Call) {
            for (std::uint32_t i = 0; i < in.count; ++i) list.push_back(f_.extras[in.extra + i]);
        }
        return list;
    }

    // Blocks and instructions are well formed enough to analyse.
    bool structure() {
        const auto n = f_.insts.size();
        block_of_.assign(n, none);
        position_.assign(n, 0);
        bool good = true;
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            const auto& block = f_.blocks[b];
            for (std::uint32_t i = 0; i < block.insts.size(); ++i) {
                const auto id = block.insts[i];
                if (id >= n) {
                    fail("V1", "block " + std::to_string(b) + " names an instruction that does not exist");
                    return false;
                }
                if (block_of_[id] != none) {
                    fail("V1", "instruction " + std::to_string(id) + " is in two places");
                    good = false;
                }
                block_of_[id] = b;
                position_[id] = i;
            }
            if (block.insts.empty() || !terminator(f_.insts[block.insts.back()].op)) {
                fail("V1", "block " + std::to_string(b) + " does not end in a terminator");
                good = false;
                continue;
            }
            for (std::uint32_t i = 0; i + 1 < block.insts.size(); ++i) {
                if (terminator(f_.insts[block.insts[i]].op)) {
                    fail("V1", "block " + std::to_string(b) + " has a terminator before its end");
                    good = false;
                }
            }
            bool phis_done = false;
            for (const auto id : block.insts) {
                if (f_.insts[id].op != Op::Phi) phis_done = true;
                else if (phis_done) {
                    fail("V1", "block " + std::to_string(b) + " has a phi after another instruction");
                    good = false;
                }
            }
            const auto& last = f_.insts[block.insts.back()];
            std::size_t expected = block.succs.size();
            switch (last.op) {
            case Op::Jump: expected = 1; break;
            case Op::Branch: expected = 2; break;
            case Op::Switch: expected = last.imm; break;
            case Op::Dispatch:
                expected = last.imm < f_.dispatch_tables.size() ? block.succs.size() : 0;
                if (last.imm >= f_.dispatch_tables.size()) {
                    fail("V1", "block " + std::to_string(b) + " dispatches on a table that does not exist");
                    good = false;
                }
                break;
            default: expected = 0; break;
            }
            if (block.succs.size() != expected) {
                fail("V1", "block " + std::to_string(b) + " has " + std::to_string(block.succs.size()) +
                               " successors, its terminator takes " + std::to_string(expected));
                good = false;
            }
            for (const auto s : block.succs) {
                if (s >= f_.blocks.size()) {
                    fail("V1", "block " + std::to_string(b) + " has a successor that does not exist");
                    return false;
                }
            }
        }
        // preds mirror succs.
        std::vector<std::uint32_t> counted(f_.blocks.size(), 0);
        for (const auto& block : f_.blocks) {
            for (const auto s : block.succs) ++counted[s];
        }
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (counted[b] != f_.blocks[b].preds.size()) {
                fail("V1", "block " + std::to_string(b) + " has predecessors that do not match the successor lists");
                good = false;
            }
        }
        for (std::uint32_t e = 0; e < f_.entries.size(); ++e) {
            if (f_.entries[e].ir_block >= f_.blocks.size()) {
                fail("V1", "entry " + std::to_string(e) + " names a block that does not exist");
                good = false;
            }
        }
        for (std::uint32_t id = 0; id < n; ++id) {
            if (f_.insts[id].count != 0 &&
                std::uint64_t{f_.insts[id].extra} + f_.insts[id].count > f_.extras.size()) {
                fail("V1", "instruction " + std::to_string(id) + " has operands outside the operand table");
                return false;
            }
        }
        for (std::uint32_t id = 0; id < n; ++id) {
            for (const auto v : uses(f_.insts[id])) {
                if (v >= n || block_of_[v] == none) {
                    fail("V1", "instruction " + std::to_string(id) + " reads a value that is not defined");
                    return false;
                }
            }
        }
        return good;
    }

    // --- Dominators, with every entry block a root ------------------------------

    void dominators() {
        const auto n = static_cast<std::uint32_t>(f_.blocks.size());
        root_ = n;
        // Reverse postorder from the virtual root.
        std::vector<std::uint8_t> seen(n + 1, 0);
        std::vector<std::uint32_t> order;
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{root_, 0}};
        seen[root_] = 1;
        const auto successors = [&](std::uint32_t b) -> std::vector<std::uint32_t> {
            if (b == root_) {
                std::vector<std::uint32_t> roots;
                for (const auto& entry : f_.entries) roots.push_back(entry.ir_block);
                return roots;
            }
            return f_.blocks[b].succs;
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
        number_.assign(n + 1, none);
        for (std::uint32_t i = 0; i < order.size(); ++i) number_[order[i]] = i;
        idom_.assign(n + 1, none);
        idom_[root_] = root_;
        const auto predecessors = [&](std::uint32_t b) {
            std::vector<std::uint32_t> list = f_.blocks[b].preds;
            for (const auto& entry : f_.entries) {
                if (entry.ir_block == b) list.push_back(root_);
            }
            return list;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto b : order) {
                if (b == root_) continue;
                std::uint32_t chosen = none;
                for (const auto p : predecessors(b)) {
                    if (number_[p] == none || idom_[p] == none) continue;
                    if (chosen == none) {
                        chosen = p;
                        continue;
                    }
                    auto x = p;
                    auto y = chosen;
                    while (x != y) {
                        while (number_[x] > number_[y]) x = idom_[x];
                        while (number_[y] > number_[x]) y = idom_[y];
                    }
                    chosen = x;
                }
                if (chosen != idom_[b]) {
                    idom_[b] = chosen;
                    changed = true;
                }
            }
        }
    }

    bool reachable(std::uint32_t b) const { return number_[b] != none; }

    // Block `a` dominates block `b` (every path from an entry to b passes a).
    bool dominates(std::uint32_t a, std::uint32_t b) const {
        if (!reachable(a) || !reachable(b)) return false;
        while (b != a && b != root_) b = idom_[b];
        return b == a;
    }

    // The value `v` is defined before the instruction at (block, position).
    bool available(std::uint32_t v, std::uint32_t block, std::size_t position) const {
        const auto def_block = block_of_[v];
        if (def_block == block) return position_[v] < position;
        return dominates(def_block, block);
    }

    void ssa() {
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (!reachable(b)) continue;
            const auto& block = f_.blocks[b];
            for (std::uint32_t i = 0; i < block.insts.size(); ++i) {
                const auto id = block.insts[i];
                const auto& in = f_.insts[id];
                if (in.op == Op::Phi) {
                    if (in.count != block.preds.size()) {
                        fail("V1", at(id) + " has " + std::to_string(in.count) + " operands for " +
                                       std::to_string(block.preds.size()) + " predecessors");
                        continue;
                    }
                    for (std::uint32_t j = 0; j < in.count; ++j) {
                        const auto v = f_.extras[in.extra + j];
                        const auto pred = block.preds[j];
                        if (!reachable(pred)) continue;
                        const bool ok = block_of_[v] == pred || dominates(block_of_[v], pred);
                        if (!ok) fail("V1", at(id) + " takes " + std::to_string(v) + ", which does not dominate predecessor " + std::to_string(pred));
                    }
                    continue;
                }
                for (const auto v : uses(in)) {
                    if (!available(v, b, i)) fail("V1", at(id) + " reads " + std::to_string(v) + " before it is defined");
                }
            }
        }
    }

    // --- Types ------------------------------------------------------------------

    static bool bits(ValueType type) {
        return type.type == Type::Int || type.type == Type::Ref || type.type == Type::F32 || type.type == Type::F64;
    }

    static bool well_formed(ValueType type) {
        switch (type.type) {
        case Type::Int: return type.width >= 1 && type.width <= 64;
        case Type::Ref: return type.width == 64;
        case Type::F32: return type.width == 32;
        case Type::F64: return type.width == 64;
        case Type::Ptr:
        case Type::None: return true;
        }
        return false;
    }

    void types() {
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            const auto& in = f_.insts[id];
            if (block_of_[id] == none || !reachable(block_of_[id])) continue;
            if (!well_formed(in.type)) fail("V2", at(id) + " has a malformed type " + type_name(in.type));
            if (has_value(in.op) && in.type.type == Type::None) fail("V2", at(id) + " has no result type");
            const auto operand_bits = [&](std::uint32_t v, const char* role) {
                if (v == none || v >= f_.insts.size()) return;
                if (!bits(f_.insts[v].type)) {
                    fail("V2", at(id) + " takes " + type_name(f_.insts[v].type) + " as " + role);
                }
            };
            switch (in.op) {
            case Op::Lift: {
                operand_bits(in.operands[0], "its first operand");
                operand_bits(in.operands[1], "its second operand");
                break;
            }
            case Op::Mask:
                operand_bits(in.operands[0], "its operand");
                if (in.imm == 0 || in.imm > 64) fail("V2", at(id) + " masks to " + std::to_string(in.imm) + " bits");
                break;
            case Op::Gather:
                for (std::uint32_t i = 0; i < in.count; i += 2) operand_bits(f_.extras[in.extra + i], "a lane source");
                break;
            case Op::Branch:
            case Op::Switch:
            case Op::Dispatch:
            case Op::SwitchCheck:
            case Op::Guard:
                if (in.operands[0] == none || f_.insts[in.operands[0]].type.type != Type::Int) {
                    fail("V2", at(id) + " selects on a value that is not an integer");
                }
                break;
            case Op::CounterAdd:
            case Op::Budget:
            case Op::Leave:
            case Op::Deopt:
                if (in.operands[0] == none || f_.insts[in.operands[0]].type.type != Type::Int ||
                    f_.insts[in.operands[0]].type.width != 64) {
                    fail("V2", at(id) + " takes a counter that is not a 64-bit integer");
                }
                break;
            case Op::Phi:
                for (std::uint32_t j = 0; j < in.count; ++j) {
                    const auto v = f_.extras[in.extra + j];
                    if (v >= f_.insts.size()) continue;
                    const auto operand = f_.insts[v].type;
                    const bool both_bits = bits(in.type) && bits(operand);
                    const bool both_pointers = in.type.type == Type::Ptr && operand.type == Type::Ptr;
                    if (!both_bits && !both_pointers) {
                        fail("V2", at(id) + " joins " + type_name(operand) + " into " + type_name(in.type));
                    }
                }
                break;
            case Op::StateCall:
            case Op::Helper:
            case Op::Call:
                if ((in.flags & CheckReference) != 0) {
                    operand_bits(in.operands[0], "the reference");
                } else {
                    for (std::uint32_t j = 0; j < in.count; ++j) operand_bits(f_.extras[in.extra + j], "an argument");
                }
                break;
            default: break;
            }
        }
    }

    // --- What may have been written into the frame, by op -------------------------

    // The slots (first half) and flags (second half) a write-back stores, and
    // a call's result slot, which the callee's return writes.
    void writes_of(const Inst& in, std::vector<std::uint8_t>& written) const {
        const auto count = written.size() / 2;
        if (in.op == Op::WriteBack && in.state < f_.states.size()) {
            const auto& state = f_.states[in.state];
            for (std::size_t r = 0; r < count && r < state.slots.size(); ++r) {
                if (state.slots[r].kind != StateSlot::Kind::Unchanged) written[r] = 1;
                if (state.flags[r].kind != StateFlag::Kind::Unchanged) written[count + r] = 1;
            }
        } else if (in.op == Op::Call && in.aux < f_.exits.size()) {
            const auto slot = f_.exits[in.aux]->result_slot;
            if (slot != none && slot < count) written[slot] = written[count + slot] = 1;
        }
    }

    // For each op with a frame state, the slots and flags that may have been
    // written into the frame before it runs: a forward union over the graph
    // from the entries, in `written_before_`.
    void frame_writes() {
        const auto n = f_.blocks.size();
        const auto width = 2 * static_cast<std::size_t>(f_.slot_count);
        std::vector<std::vector<std::uint8_t>> in(n, std::vector<std::uint8_t>(width, 0));
        for (bool changed = true; changed;) {
            changed = false;
            for (std::uint32_t b = 0; b < n; ++b) {
                if (!reachable(b)) continue;
                auto current = in[b];
                for (const auto id : f_.blocks[b].insts) writes_of(f_.insts[id], current);
                for (const auto next : f_.blocks[b].succs) {
                    for (std::size_t i = 0; i < width; ++i) {
                        if (current[i] && !in[next][i]) {
                            in[next][i] = 1;
                            changed = true;
                        }
                    }
                }
            }
        }
        for (std::uint32_t b = 0; b < n; ++b) {
            if (!reachable(b)) continue;
            auto current = in[b];
            for (const auto id : f_.blocks[b].insts) {
                if (f_.insts[id].state != none) written_before_[id] = current;
                writes_of(f_.insts[id], current);
            }
        }
    }

    // --- Frame states -----------------------------------------------------------

    void states() {
        std::vector<std::uint8_t> checked_chain(f_.states.size(), 0);
        // A leave ends the activation outside native code: the frame it
        // leaves is written first, by a write-back of the commit state of
        // the physical frame at the block the leave goes to.
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (!reachable(b)) continue;
            const auto& insts = f_.blocks[b].insts;
            for (std::size_t k = 0; k < insts.size(); ++k) {
                const auto& leave = f_.insts[insts[k]];
                if (leave.op != Op::Leave) continue;
                const bool preceded = k > 0 && f_.insts[insts[k - 1]].op == Op::WriteBack &&
                                      f_.insts[insts[k - 1]].state < f_.states.size();
                if (!preceded) {
                    fail("V3", at(insts[k]) + " is not directly preceded by a write-back");
                    continue;
                }
                const auto& state = f_.states[f_.insts[insts[k - 1]].state];
                if (state.caller != none || state.function != f_.function || state.block != leave.imm) {
                    fail("V3", at(insts[k]) + " is preceded by a write-back of another frame or block");
                }
            }
        }
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            const auto& in = f_.insts[id];
            if (block_of_[id] == none || !reachable(block_of_[id])) continue;
            if (!needs_state(in)) continue;
            if (in.state == none || in.state >= f_.states.size()) {
                fail("V3", at(id) + " has no frame state");
                continue;
            }
            // The chain, innermost first.
            std::uint32_t depth = 0;
            std::uint32_t s = in.state;
            bool good = true;
            while (s != none) {
                if (s >= f_.states.size() || ++depth > 64) {
                    fail("V3", at(id) + " has a frame state chain that does not end");
                    good = false;
                    break;
                }
                const auto& state = f_.states[s];
                if (state.function >= image_.functions.size()) {
                    fail("V3", at(id) + " has a frame state of a function that does not exist");
                    good = false;
                    break;
                }
                const auto slots = image_.functions[state.function].frame_types.size();
                if (state.slots.size() != slots || state.flags.size() != slots) {
                    fail("V3", at(id) + " has a frame state with " + std::to_string(state.slots.size()) +
                                   " slots and " + std::to_string(state.flags.size()) + " flags for a frame of " +
                                   std::to_string(slots));
                    good = false;
                }
                if (state.program >= f_.programs.size()) {
                    fail("V3", at(id) + " has a frame state of a program that does not exist");
                    good = false;
                }
                for (const auto& slot : state.slots) {
                    if (slot.kind != StateSlot::Kind::Value) continue;
                    const auto v = static_cast<std::uint32_t>(slot.value);
                    if (v >= f_.insts.size() || block_of_[v] == none ||
                        !available(v, block_of_[id], position_[id])) {
                        fail("V3", at(id) + " names a slot value that does not dominate it");
                        good = false;
                    }
                }
                for (const auto& flag : state.flags) {
                    if (flag.kind != StateFlag::Kind::Value) continue;
                    if (flag.value >= f_.insts.size() || block_of_[flag.value] == none ||
                        !available(flag.value, block_of_[id], position_[id])) {
                        fail("V3", at(id) + " names a flag value that does not dominate it");
                        good = false;
                    }
                }
                if (state.caller == none && state.function != f_.function) {
                    fail("V3", at(id) + " has a frame state chain that ends in another function's frame");
                    good = false;
                }
                s = state.caller;
            }
            if (good && depth > f_.max_inline_depth + 1) {
                fail("V3", at(id) + " has " + std::to_string(depth - 1) + " inlined frames, more than the " +
                               std::to_string(f_.max_inline_depth) + " the program declares");
            }
            const auto& leaf = f_.states[in.state];
            const auto counter = [&](std::uint32_t v, const char* what) {
                if (v == none) return false;
                if (v >= f_.insts.size() || block_of_[v] == none || !available(v, block_of_[id], position_[id]) ||
                    f_.insts[v].type.type != Type::Int || f_.insts[v].type.width != 64) {
                    fail("V3", at(id) + " has a frame state whose " + what + " is not a counter that dominates it");
                }
                return true;
            };
            if (!counter(leaf.blocks, "block count")) fail("V3", at(id) + " has a frame state without a block count");
            counter(leaf.kernel_steps, "kernel step count");
            // An entry the frame is said to hold already must not have been
            // written since the activation started.
            const auto* root = &leaf;
            for (std::uint32_t depth = 0; root->caller != none && root->caller < f_.states.size() && depth < 64; ++depth) {
                root = &f_.states[root->caller];
            }
            const auto before = written_before_.find(id);
            if (before != written_before_.end() && root->function == f_.function) {
                const auto count = before->second.size() / 2;
                for (std::size_t r = 0; r < count && r < root->slots.size(); ++r) {
                    if (root->slots[r].kind == StateSlot::Kind::Unchanged && before->second[r]) {
                        fail("V3", at(id) + " has a frame state that leaves slot " + std::to_string(r) +
                                       " unchanged after the frame's slot was written");
                    }
                    if (root->flags[r].kind == StateFlag::Kind::Unchanged && before->second[count + r]) {
                        fail("V3", at(id) + " has a frame state that leaves the flag of slot " + std::to_string(r) +
                                       " unchanged after it was written");
                    }
                }
            }
        }
    }

    // --- V4: effects keep their program order -----------------------------------

    void order() {
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            const auto& block = f_.blocks[b];
            if (!reachable(b)) continue;
            std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> last;
            for (const auto id : block.insts) {
                const auto& in = f_.insts[id];
                if (!effect(in)) continue;
                if (in.origin_block != block.origin_block || in.origin_function != block.origin_function) {
                    fail("V4", at(id) + " is in a block of another image block's code");
                }
                const auto key = std::make_pair(in.origin_function, in.origin_block);
                const auto found = last.find(key);
                if (found != last.end() && found->second >= in.origin_seq) {
                    fail("V4", at(id) + " runs before an effect that came before it in the program");
                }
                last[key] = in.origin_seq;
            }
        }
    }

    // --- V5: no check after an effect of its image block ------------------------

    void commit_rule() {
        const auto n = f_.blocks.size();
        // committed_in[b]: some path from the start of the image block's
        // execution to the start of b has run a committing op of the block.
        std::vector<std::uint8_t> in_state(n, 0);
        std::vector<std::uint8_t> out_state(n, 0);
        for (bool changed = true; changed;) {
            changed = false;
            for (std::uint32_t b = 0; b < n; ++b) {
                if (!reachable(b)) continue;
                const auto& block = f_.blocks[b];
                std::uint8_t in = 0;
                if (!block.image_start) {
                    for (const auto p : block.preds) {
                        if (f_.blocks[p].origin_block == block.origin_block &&
                            f_.blocks[p].origin_function == block.origin_function) {
                            in |= out_state[p];
                        }
                    }
                }
                std::uint8_t out = in;
                for (const auto id : block.insts) {
                    if (commits(f_.insts[id])) out = 1;
                }
                if (in != in_state[b] || out != out_state[b]) {
                    in_state[b] = in;
                    out_state[b] = out;
                    changed = true;
                }
            }
        }
        for (std::uint32_t b = 0; b < n; ++b) {
            if (!reachable(b)) continue;
            bool committed = in_state[b] != 0;
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                // The check of a state operation is its own commit: a block
                // has one such op, which a check cannot follow.
                if ((in.flags & Checked) != 0 && committed && in.op != Op::Return) {
                    fail("V5", at(id) + " is a check that runs after an effect of its block");
                }
                if (commits(in)) committed = true;
            }
        }
    }

    // --- V6: pointers ---------------------------------------------------------------

    // Values live after each instruction, by backward dataflow over the values
    // `wanted` selects.
    struct Liveness {
        // live-out of each block.
        std::vector<std::vector<std::uint8_t>> out;
        std::vector<std::uint32_t> index;
        std::vector<std::uint32_t> values;
    };

    template <typename Wanted>
    Liveness liveness(Wanted&& wanted) {
        Liveness result;
        result.index.assign(f_.insts.size(), none);
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            if (block_of_[id] != none && wanted(f_.insts[id])) {
                result.index[id] = static_cast<std::uint32_t>(result.values.size());
                result.values.push_back(id);
            }
        }
        const auto n = f_.blocks.size();
        const auto m = result.values.size();
        std::vector<std::vector<std::uint8_t>> in(n, std::vector<std::uint8_t>(m, 0));
        result.out.assign(n, std::vector<std::uint8_t>(m, 0));
        for (bool changed = true; changed && m != 0;) {
            changed = false;
            for (std::uint32_t b = static_cast<std::uint32_t>(n); b-- > 0;) {
                if (!reachable(b)) continue;
                std::vector<std::uint8_t> live(m, 0);
                for (const auto s : f_.blocks[b].succs) {
                    for (std::size_t i = 0; i < m; ++i) live[i] |= in[s][i];
                    // A phi of the successor reads the value this edge carries.
                    const auto& successor = f_.blocks[s];
                    for (std::size_t k = 0; k < successor.preds.size(); ++k) {
                        if (successor.preds[k] != b) continue;
                        for (const auto id : successor.insts) {
                            const auto& phi = f_.insts[id];
                            if (phi.op != Op::Phi) break;
                            const auto v = f_.extras[phi.extra + k];
                            if (result.index[v] != none) live[result.index[v]] = 1;
                        }
                    }
                }
                result.out[b] = live;
                const auto& insts = f_.blocks[b].insts;
                for (std::size_t k = insts.size(); k-- > 0;) {
                    const auto& inst = f_.insts[insts[k]];
                    if (result.index[insts[k]] != none) live[result.index[insts[k]]] = 0;
                    if (inst.op == Op::Phi) continue;
                    for (const auto v : uses(inst)) {
                        if (result.index[v] != none) live[result.index[v]] = 1;
                    }
                    if (inst.state != none) {
                        for (const auto v : state_values(inst.state)) {
                            if (result.index[v] != none) live[result.index[v]] = 1;
                        }
                    }
                }
                if (live != in[b]) {
                    in[b] = live;
                    changed = true;
                }
            }
        }
        return result;
    }

    std::vector<std::uint32_t> state_values(std::uint32_t s) const {
        std::vector<std::uint32_t> list;
        for (std::uint32_t depth = 0; s != none && s < f_.states.size() && depth < 64; ++depth) {
            const auto& state = f_.states[s];
            if (state.blocks != none) list.push_back(state.blocks);
            if (state.kernel_steps != none) list.push_back(state.kernel_steps);
            for (const auto& slot : state.slots) {
                if (slot.kind == StateSlot::Kind::Value) list.push_back(static_cast<std::uint32_t>(slot.value));
            }
            for (const auto& flag : state.flags) {
                if (flag.kind == StateFlag::Kind::Value) list.push_back(flag.value);
            }
            s = state.caller;
        }
        return list;
    }

    // The values live just after instruction `k` of block `b` (excluding the
    // value it defines), for the wanted values.
    std::vector<std::uint32_t> live_after(const Liveness& live, std::uint32_t b, std::size_t k) const {
        auto current = live.out[b];
        const auto& insts = f_.blocks[b].insts;
        for (std::size_t i = insts.size(); i-- > k + 1;) {
            const auto& inst = f_.insts[insts[i]];
            if (live.index[insts[i]] != none) current[live.index[insts[i]]] = 0;
            if (inst.op == Op::Phi) continue;
            for (const auto v : uses(inst)) {
                if (live.index[v] != none) current[live.index[v]] = 1;
            }
            if (inst.state != none) {
                for (const auto v : state_values(inst.state)) {
                    if (live.index[v] != none) current[live.index[v]] = 1;
                }
            }
        }
        std::vector<std::uint32_t> list;
        for (std::size_t i = 0; i < current.size(); ++i) {
            if (current[i] && live.values[i] != insts[k]) list.push_back(live.values[i]);
        }
        return list;
    }

    void pointers() {
        const auto is_ptr = [](const Inst& in) { return in.type.type == Type::Ptr; };
        bool any = false;
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            if (block_of_[id] != none && is_ptr(f_.insts[id])) any = true;
        }
        // A pointer in a frame state or as an argument of a helper.
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            const auto& in = f_.insts[id];
            if (block_of_[id] == none || !reachable(block_of_[id])) continue;
            if (in.op == Op::Helper || in.op == Op::Call || (in.op == Op::StateCall && (in.flags & CheckReference) == 0)) {
                for (std::uint32_t j = 0; j < in.count; ++j) {
                    const auto v = f_.extras[in.extra + j];
                    if (v < f_.insts.size() && is_ptr(f_.insts[v])) fail("V6", at(id) + " takes a pointer as an argument");
                }
            }
            if (in.state != none) {
                for (const auto v : state_values(in.state)) {
                    if (v < f_.insts.size() && is_ptr(f_.insts[v])) {
                        fail("V6", at(id) + " has a frame state that names a pointer");
                        break;
                    }
                }
            }
        }
        if (!any) return;
        const auto live = liveness(is_ptr);
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (!reachable(b)) continue;
            const auto& insts = f_.blocks[b].insts;
            for (std::size_t k = 0; k < insts.size(); ++k) {
                if (!safepoint(f_.insts[insts[k]])) continue;
                for (const auto v : live_after(live, b, k)) {
                    fail("V6", "pointer %" + std::to_string(v) + " is live across " + at(insts[k]));
                }
            }
        }
        // A pointer phi on a loop's back edge, in a loop with a safepoint.
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (!reachable(b)) continue;
            for (const auto id : f_.blocks[b].insts) {
                const auto& phi = f_.insts[id];
                if (phi.op != Op::Phi) break;
                if (phi.type.type != Type::Ptr) continue;
                for (std::size_t k = 0; k < f_.blocks[b].preds.size(); ++k) {
                    const auto pred = f_.blocks[b].preds[k];
                    if (!dominates(b, pred)) continue;
                    // pred is on a back edge to b: any safepoint in the loop?
                    if (loop_has_safepoint(b, pred)) fail("V6", at(id) + " carries a pointer around a loop with a safepoint");
                }
            }
        }
    }

    // The blocks of the natural loop of the back edge pred -> head hold a safepoint.
    bool loop_has_safepoint(std::uint32_t head, std::uint32_t tail) const {
        std::vector<std::uint8_t> in_loop(f_.blocks.size(), 0);
        std::vector<std::uint32_t> work{tail};
        in_loop[head] = 1;
        in_loop[tail] = 1;
        while (!work.empty()) {
            const auto b = work.back();
            work.pop_back();
            if (b == head) continue;
            for (const auto p : f_.blocks[b].preds) {
                if (!in_loop[p]) {
                    in_loop[p] = 1;
                    work.push_back(p);
                }
            }
        }
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (!in_loop[b]) continue;
            for (const auto id : f_.blocks[b].insts) {
                if (safepoint(f_.insts[id])) return true;
            }
        }
        return false;
    }

    // --- V7: roots --------------------------------------------------------------------

    // For each value, the slot whose value at an entry it is (a load of the
    // slot at some entry, or a phi of only such loads of that slot): the
    // frame holds it while the slot is unchanged.
    std::vector<std::uint32_t> frame_slots() const {
        constexpr std::uint32_t unset = none - 1;
        std::vector<std::uint32_t> slot_of(f_.insts.size(), none);
        std::vector<std::uint32_t> phis;
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            if (block_of_[id] == none) continue;
            if (f_.insts[id].op == Op::EntryLoad) slot_of[id] = static_cast<std::uint32_t>(f_.insts[id].imm);
            else if (f_.insts[id].op == Op::Phi) {
                slot_of[id] = unset;
                phis.push_back(id);
            }
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto phi : phis) {
                const auto& in = f_.insts[phi];
                auto meet = unset;
                for (std::uint32_t i = 0; i < in.count && meet != none; ++i) {
                    const auto operand = f_.extras[in.extra + i];
                    if (operand == phi) continue;
                    const auto s = slot_of[operand];
                    if (s == unset) continue;
                    if (s == none) meet = none;
                    else if (meet == unset) meet = s;
                    else if (meet != s) meet = none;
                }
                if (meet != slot_of[phi]) {
                    slot_of[phi] = meet;
                    changed = true;
                }
            }
        }
        for (auto& s : slot_of) {
            if (s == unset) s = none;
        }
        return slot_of;
    }

    void roots() {
        const auto is_ref = [](const Inst& in) { return in.type.type == Type::Ref && in.op != Op::RunConst; };
        bool any = false;
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            if (block_of_[id] != none && is_ref(f_.insts[id])) any = true;
        }
        if (!any) return;
        const auto live = liveness(is_ref);
        const auto frame_slot = frame_slots();
        for (std::uint32_t b = 0; b < f_.blocks.size(); ++b) {
            if (!reachable(b)) continue;
            const auto& insts = f_.blocks[b].insts;
            for (std::size_t k = 0; k < insts.size(); ++k) {
                const auto& in = f_.insts[insts[k]];
                if (in.op != Op::Helper && in.op != Op::Call) continue;
                if (in.state == none || in.state >= f_.states.size()) continue;
                // The values the state writes into the frames.
                std::vector<std::uint32_t> named;
                for (std::uint32_t s = in.state, depth = 0; s != none && depth < 64; s = f_.states[s].caller, ++depth) {
                    const auto& state = f_.states[s];
                    for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
                        if (state.slots[r].kind == StateSlot::Kind::Value) {
                            named.push_back(static_cast<std::uint32_t>(state.slots[r].value));
                        }
                    }
                }
                const auto& physical = f_.states[in.state];
                for (const auto v : live_after(live, b, k)) {
                    if (std::find(named.begin(), named.end(), v) != named.end()) continue;
                    // The frame already holds an unchanged slot's value.
                    const auto written = written_before_.find(insts[k]);
                    if (frame_slot[v] != none && physical.caller == none && frame_slot[v] < physical.slots.size() &&
                        physical.slots[frame_slot[v]].kind == StateSlot::Kind::Unchanged &&
                        written != written_before_.end() && !written->second[frame_slot[v]]) {
                        continue;
                    }
                    fail("V7", "reference %" + std::to_string(v) + " is live across " + at(insts[k]) +
                                   " but no frame holds it");
                }
            }
        }
    }

    // --- V8: budgets --------------------------------------------------------------------

    void budgets() {
        const auto n = f_.blocks.size();
        std::vector<std::uint64_t> weight(n, 0);
        std::vector<std::uint8_t> starts(n, 0);
        for (std::uint32_t b = 0; b < n; ++b) {
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                if (in.op == Op::CounterAdd) weight[b] += in.imm;
                if (in.op == Op::Return) weight[b] += 1;
            }
            for (const auto id : f_.blocks[b].insts) {
                if (f_.insts[id].op == Op::Phi) continue;
                starts[b] = f_.insts[id].op == Op::Budget;
                break;
            }
        }
        // A cycle through no Budget block: removing the Budget blocks must leave no cycle.
        std::vector<std::uint8_t> state(n, 0);
        std::vector<std::uint64_t> longest(n, 0);
        bool cycle = false;
        for (std::uint32_t root = 0; root < n; ++root) {
            if (state[root] != 0 || !reachable(root)) continue;
            std::vector<std::pair<std::uint32_t, std::size_t>> stack{{root, 0}};
            state[root] = 1;
            while (!stack.empty()) {
                auto& [b, next] = stack.back();
                const auto& succs = f_.blocks[b].succs;
                if (next == succs.size()) {
                    std::uint64_t best = 0;
                    for (const auto s : succs) {
                        if (!starts[s] && state[s] == 2) best = std::max(best, longest[s]);
                    }
                    longest[b] = weight[b] + best;
                    state[b] = 2;
                    stack.pop_back();
                    continue;
                }
                const auto s = succs[next++];
                if (starts[s]) continue;
                if (state[s] == 1) cycle = true;
                else if (state[s] == 0) {
                    state[s] = 1;
                    stack.emplace_back(s, 0);
                }
            }
        }
        if (cycle) fail("V8", "a cycle of the control-flow graph has no Budget");
        for (std::uint32_t b = 0; b < n; ++b) {
            if (!reachable(b)) continue;
            for (const auto id : f_.blocks[b].insts) {
                const auto& in = f_.insts[id];
                if (in.op == Op::Budget && in.imm < longest[b]) {
                    fail("V8", at(id) + " covers " + std::to_string(in.imm) + " blocks, the longest run to the next Budget is " +
                                   std::to_string(longest[b]));
                }
            }
        }
    }

    // --- V10: removed checks have their guard -----------------------------------------------

    void guards() {
        for (std::uint32_t id = 0; id < f_.insts.size(); ++id) {
            const auto& in = f_.insts[id];
            if (in.guard == none || block_of_[id] == none || !reachable(block_of_[id])) continue;
            if (in.guard >= f_.insts.size() || f_.insts[in.guard].op != Op::Guard || block_of_[in.guard] == none) {
                fail("V10", at(id) + " names a guard that is not a Guard");
                continue;
            }
            if (!available(in.guard, block_of_[id], position_[id])) {
                fail("V10", at(id) + " is not dominated by the guard that covers it");
            }
        }
    }

    const nvr::Image& image_;
    const Function& f_;
    std::vector<std::string> violations_;
    std::vector<std::uint32_t> block_of_;
    std::vector<std::uint32_t> position_;
    std::vector<std::uint32_t> idom_;
    std::vector<std::uint32_t> number_;
    std::map<std::uint32_t, std::vector<std::uint8_t>> written_before_;
    std::uint32_t root_ = 0;
};

} // namespace

std::vector<std::string> verify(const nvr::Image& image, const Function& function) {
    return Verifier(image, function).run();
}

std::string describe(const Function& f) {
    std::ostringstream out;
    out << "function " << f.function << ": " << f.insts.size() << " values, " << f.blocks.size() << " blocks, "
        << f.states.size() << " states\n";
    for (std::uint32_t b = 0; b < f.blocks.size(); ++b) {
        const auto& block = f.blocks[b];
        out << "b" << b;
        if (block.entry != none) out << " (entry " << block.entry << ")";
        if (block.origin_block != none) out << " [image block " << block.origin_block << (block.image_start ? ", start" : "") << "]";
        out << " preds";
        for (const auto p : block.preds) out << " b" << p;
        out << "\n";
        for (const auto id : block.insts) {
            const auto& in = f.insts[id];
            out << "  %" << id << " = " << name_of(in.op) << " " << type_name(in.type);
            for (const auto operand : in.operands) {
                if (operand != none) out << " %" << operand;
            }
            if (in.count != 0) {
                out << " [";
                for (std::uint32_t i = 0; i < in.count; ++i) out << (i ? " " : "") << f.extras[in.extra + i];
                out << "]";
            }
            if (in.op == Op::Const || in.op == Op::Mask || in.op == Op::CounterAdd || in.op == Op::Budget ||
                in.op == Op::EntryLoad || in.op == Op::EntryFlag || in.op == Op::Leave || in.op == Op::Call ||
                in.op == Op::SwitchCheck) {
                out << " #" << in.imm;
            }
            if (in.op == Op::Lift) out << " op" << static_cast<int>(in.lifted.op);
            if (in.state != none) out << " state " << in.state;
            if (in.op == Op::Jump || in.op == Op::Branch || in.op == Op::Switch || in.op == Op::Dispatch) {
                out << " ->";
                for (const auto s : block.succs) out << " b" << s;
            }
            out << "\n";
        }
    }
    for (std::uint32_t s = 0; s < f.states.size(); ++s) {
        const auto& state = f.states[s];
        out << "state " << s << ": function " << state.function << " block " << state.block << " C %" << state.blocks;
        if (state.caller != none) out << " caller " << state.caller;
        out << "\n ";
        for (std::uint32_t r = 0; r < state.slots.size(); ++r) {
            const auto& slot = state.slots[r];
            if (slot.kind == StateSlot::Kind::Unchanged && state.flags[r].kind == StateFlag::Kind::Unchanged) continue;
            out << " s" << r << "=";
            out << (slot.kind == StateSlot::Kind::Value ? "%" + std::to_string(slot.value)
                    : slot.kind == StateSlot::Kind::Const ? "#" + std::to_string(slot.value)
                                                          : std::string("."));
            out << (state.flags[r].kind == StateFlag::Kind::One ? "!" : state.flags[r].kind == StateFlag::Kind::Value
                                                                         ? "?%" + std::to_string(state.flags[r].value)
                                                                         : std::string());
        }
        out << "\n";
    }
    return out.str();
}

} // namespace neura::nvm::tier3
