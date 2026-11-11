#include "tier2.hpp"

#include "tier1.hpp"
#include "lift/evaluate.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace neura::nvm::tier2 {
namespace {

constexpr std::uint32_t none = nvr::absent;

bool internal_exit(const nvr::Exit& exit) {
    return exit.kind == nvr::ExitKind::Continue ||
           (exit.kind == nvr::ExitKind::State && inline_state_operation(exit));
}

// --- Region optimization (tier2.hpp, "Optimization") ---------------------
//
// Every rewrite here keeps the region's effect on every input exactly the
// same; each is justified by a fact that holds on every path through the
// region, and when one cannot be shown the op stays as it is.

bool instruction_can_fail(const lift::Instruction& in) {
    switch (in.op) {
    case lift::Opcode::Add:
    case lift::Opcode::Subtract:
    case lift::Opcode::Negate:
    case lift::Opcode::Multiply:
    case lift::Opcode::Divide:
    case lift::Opcode::Remainder:
    case lift::Opcode::ShiftLeft:
    case lift::Opcode::ShiftRight:
    case lift::Opcode::Index:
    case lift::Opcode::ConvertInteger:
    case lift::Opcode::FloatToInteger:
        return true;
    default:
        return false;
    }
}

// Operands an instruction reads: 0, 1 (a), or 2 (a and b).
unsigned instruction_arity(const lift::Instruction& in) {
    switch (in.op) {
    case lift::Opcode::Constant:
        return 0;
    case lift::Opcode::Negate:
    case lift::Opcode::Not:
    case lift::Opcode::BitNot:
    case lift::Opcode::Index:
    case lift::Opcode::ConvertInteger:
    case lift::Opcode::FloatNegate:
    case lift::Opcode::FloatFromInteger:
    case lift::Opcode::FloatToInteger:
    case lift::Opcode::FloatConvert:
        return 1;
    default:
        return 2;
    }
}

unsigned float_bits(lift::FloatType type) {
    switch (type) {
    case lift::FloatType::Binary16:
    case lift::FloatType::BFloat16: return 16;
    case lift::FloatType::Binary32: return 32;
    default: return 64;
    }
}

unsigned bits_of(std::uint64_t value) {
    unsigned bits = 0;
    while (bits < 64 && (value >> bits) != 0) ++bits;
    return bits;
}

// The most significant bit an instruction's result can have set, as a
// width, given its operands' widths (lift/evaluate.hpp).
unsigned result_bits(const lift::Instruction& in, unsigned a, unsigned b) {
    using lift::Opcode;
    switch (in.op) {
    case Opcode::Constant: return bits_of(in.immediate);
    case Opcode::Add:
    case Opcode::AddWrapping:
    case Opcode::Subtract:
    case Opcode::Negate:
    case Opcode::Multiply:
    case Opcode::BitNot:
    case Opcode::ShiftLeft:
    case Opcode::ShiftRight:
        return std::min(in.width, std::uint8_t{64});
    case Opcode::Divide:
    case Opcode::Remainder:
        // Signed results are masked; an unsigned quotient or remainder is
        // at most the dividend.
        return in.is_signed ? std::min(in.width, std::uint8_t{64}) : a;
    case Opcode::Equal:
    case Opcode::NotEqual:
    case Opcode::Less:
    case Opcode::LessEqual:
    case Opcode::Greater:
    case Opcode::GreaterEqual:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Not:
    case Opcode::FloatEqual:
    case Opcode::FloatNotEqual:
    case Opcode::FloatLess:
    case Opcode::FloatLessEqual:
    case Opcode::FloatGreater:
    case Opcode::FloatGreaterEqual:
        return 1;
    case Opcode::BitAnd: return std::min(a, b);
    case Opcode::BitOr:
    case Opcode::BitXor: return std::max(a, b);
    case Opcode::Index: return a;
    case Opcode::ConvertInteger:
    case Opcode::FloatToInteger: return std::min(in.to_width, std::uint8_t{64});
    case Opcode::FloatAdd:
    case Opcode::FloatSubtract:
    case Opcode::FloatMultiply:
    case Opcode::FloatDivide: return float_bits(in.format);
    case Opcode::FloatNegate: return std::max(a, float_bits(in.format));
    case Opcode::FloatFromInteger:
    case Opcode::FloatConvert: return float_bits(in.to_format);
    }
    return 64;
}

class Optimizer {
public:
    Optimizer(Region& region, std::vector<std::uint8_t> slot_widths)
        : region_(region), slot_widths_(std::move(slot_widths)) {}

    void run() {
        for (const auto& [reg, value] : region_.immediates) immediate_values_[reg] = value;
        fold_completes();
        hoist_constants();
        // The entry check that every slot register fits its type.
        for (std::uint32_t r = 0; r < region_.slots.size(); ++r) {
            if (slot_widths_[r] < 64) region_.entry_canonical.emplace_back(r, slot_widths_[r]);
        }
        analyse_widths();
        drop_masks();
        analyse_widths();
        eliminate_checks();
        drop_dead();
        write_directly();
        compact();
    }

private:
    std::size_t size() const { return region_.code.size(); }
    bool is_slot(std::uint32_t reg) const { return reg < region_.slots.size(); }

    bool can_fail(const Op& op) const {
        switch (op.code) {
        case Code::Budget:
        case Code::CheckRef:
        case Code::State:
        case Code::Switch:
        case Code::Deopt:
            return true;
        case Code::Lift:
            return instruction_can_fail(region_.instructions[op.index]);
        default:
            return false;
        }
    }

    // The registers an op reads.
    template <typename F>
    void each_use(Op& op, F&& use) {
        switch (op.code) {
        case Code::Lift: {
            const auto arity = instruction_arity(region_.instructions[op.index]);
            if (arity >= 1) use(op.a);
            if (arity >= 2) use(op.b);
            break;
        }
        case Code::Mask:
        case Code::Move:
        case Code::Store:
        case Code::Branch:
        case Code::Switch:
        case Code::CheckRef:
            use(op.a);
            break;
        case Code::Gather:
            for (unsigned i = 0; i < op.width; ++i) use(region_.gathers[op.index + i].reg);
            break;
        case Code::State:
            for (unsigned i = 0; i < op.width; ++i) use(region_.operands[op.other + i]);
            break;
        default:
            break;
        }
    }

    std::optional<std::uint32_t> definition(const Op& op) const {
        switch (op.code) {
        case Code::Lift:
        case Code::Mask:
        case Code::Move:
        case Code::Gather:
        case Code::Store:
            return op.dst;
        case Code::State:
            if (op.dst != none) return op.dst;
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    // The ops that can run after `pc` inside the region.
    std::vector<std::uint32_t> successors(std::uint32_t pc) const {
        const auto& op = region_.code[pc];
        switch (op.code) {
        case Code::Branch: return {op.target, op.other};
        case Code::Switch: {
            std::vector<std::uint32_t> next;
            for (unsigned i = 0; i < op.width; ++i) next.push_back(region_.operands[op.index + i]);
            return next;
        }
        case Code::Jump: return {op.target};
        case Code::Leave:
        case Code::Deopt: return {};
        default: return {pc + 1};
        }
    }

    // Each Complete precedes the Jump or Leave of its exit: fold it in.
    void fold_completes() {
        for (std::uint32_t pc = 0; pc + 1 < size(); ++pc) {
            auto& op = region_.code[pc];
            auto& next = region_.code[pc + 1];
            if (op.code == Code::Complete && (next.code == Code::Jump || next.code == Code::Leave)) {
                next.flags |= completes;
                deleted_.insert(pc);
            }
        }
    }

    // A constant instruction's value is known: load it at entry.
    void hoist_constants() {
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            const auto& op = region_.code[pc];
            if (op.code != Code::Lift || region_.instructions[op.index].op != lift::Opcode::Constant) continue;
            region_.immediates.emplace_back(op.dst, region_.instructions[op.index].immediate);
            immediate_values_[op.dst] = region_.instructions[op.index].immediate;
            deleted_.insert(pc);
        }
    }

    // For every op, the most significant bit each register can have set
    // when the op runs (a width; registers not yet written count as 64).
    void analyse_widths() {
        const auto count = region_.register_count;
        std::vector<std::uint8_t> start(count, 64);
        for (std::uint32_t r = 0; r < region_.slots.size(); ++r) start[r] = slot_widths_[r];
        for (const auto& constant : region_.constants) start[constant.reg] = constant.width;
        for (const auto& [reg, value] : region_.immediates) start[reg] = static_cast<std::uint8_t>(bits_of(value));
        // `reached_` marks the ops some path reaches: a region with no
        // registers has empty states that are still states.
        widths_.assign(size(), {});
        reached_.assign(size(), 0);
        widths_[0] = start;
        reached_[0] = 1;
        std::vector<std::uint32_t> work{0};
        std::vector<std::uint8_t> queued(size(), 0);
        queued[0] = 1;
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            queued[pc] = 0;
            auto out = widths_[pc];
            const auto& op = region_.code[pc];
            if (!deleted_.contains(pc)) {
                if (const auto dst = definition(op)) out[*dst] = width_of(op, widths_[pc]);
            }
            for (const auto next : successors(pc)) {
                if (next >= size()) continue;
                auto& in = widths_[next];
                bool changed = false;
                if (!reached_[next]) {
                    in = out;
                    reached_[next] = 1;
                    changed = true;
                } else {
                    for (std::uint32_t r = 0; r < count; ++r) {
                        if (out[r] > in[r]) {
                            in[r] = out[r];
                            changed = true;
                        }
                    }
                }
                if (changed && !queued[next]) {
                    queued[next] = 1;
                    work.push_back(next);
                }
            }
        }
    }

    std::uint8_t width_of(const Op& op, const std::vector<std::uint8_t>& in) const {
        switch (op.code) {
        case Code::Lift: {
            const auto& instruction = region_.instructions[op.index];
            return static_cast<std::uint8_t>(result_bits(instruction, in[op.a], in[op.b]));
        }
        case Code::Mask: return std::min(op.width, in[op.a]);
        case Code::Move:
        case Code::Store: return in[op.a];
        case Code::Gather: return op.width;
        default: return 64;
        }
    }

    // A Mask whose operand already fits its width changes nothing: its
    // readers read the operand instead.
    void drop_masks() {
        std::map<std::uint32_t, std::uint32_t> renamed;
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            const auto& op = region_.code[pc];
            if (op.code != Code::Mask || deleted_.contains(pc) || !reached_[pc]) continue;
            if (widths_[pc][op.a] <= op.width) {
                renamed[op.dst] = op.a;
                deleted_.insert(pc);
            }
        }
        if (renamed.empty()) return;
        for (auto& op : region_.code) {
            each_use(op, [&](std::uint32_t& reg) {
                if (const auto it = renamed.find(reg); it != renamed.end()) reg = it->second;
            });
        }
    }

    // Overflow checks a comparison on every path shows cannot fail. A fact
    // says that a slot register, as a `width`-bit integer of the stated
    // signedness, is not the type's largest value (it was found less than
    // another) or not its smallest (found greater). Facts hold from the
    // comparison's branch until the register is written; at a join, only
    // facts every incoming path has survive.
    struct Fact {
        std::uint8_t width = 0;
        bool is_signed = false;
        bool below_max = false;
        bool above_min = false;
        bool operator==(const Fact&) const = default;
    };
    using Facts = std::map<std::uint32_t, Fact>;

    void eliminate_checks() {
        // The comparison that defines each temporary, if any.
        std::map<std::uint32_t, std::uint32_t> compare_of;
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            const auto& op = region_.code[pc];
            if (op.code == Code::Lift && !deleted_.contains(pc)) compare_of[op.dst] = pc;
        }
        std::vector<std::optional<Facts>> facts(size());
        facts[0] = Facts{};
        std::vector<std::uint32_t> work{0};
        const auto merge = [&](std::uint32_t next, const Facts& out) {
            if (next >= size()) return;
            auto& in = facts[next];
            if (!in) {
                in = out;
                work.push_back(next);
                return;
            }
            Facts kept;
            for (const auto& [reg, fact] : *in) {
                const auto it = out.find(reg);
                if (it == out.end() || it->second.width != fact.width || it->second.is_signed != fact.is_signed) {
                    continue;
                }
                Fact both = fact;
                both.below_max = fact.below_max && it->second.below_max;
                both.above_min = fact.above_min && it->second.above_min;
                if (both.below_max || both.above_min) kept.emplace(reg, both);
            }
            if (kept != *in) {
                in = kept;
                work.push_back(next);
            }
        };
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            auto out = *facts[pc];
            const auto& op = region_.code[pc];
            if (!deleted_.contains(pc)) {
                if (const auto dst = definition(op)) out.erase(*dst);
            }
            if (op.code == Code::Branch && !deleted_.contains(pc)) {
                auto when_true = out;
                auto when_false = out;
                if (const auto it = compare_of.find(op.a); it != compare_of.end()) {
                    learn(it->second, pc, when_true, when_false);
                }
                merge(op.target, when_true);
                merge(op.other, when_false);
                continue;
            }
            for (const auto next : successors(pc)) merge(next, out);
        }
        // Rewrite the checked additions and subtractions of 1 the facts
        // cover.
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            auto& op = region_.code[pc];
            if (op.code != Code::Lift || deleted_.contains(pc) || !facts[pc]) continue;
            auto instruction = region_.instructions[op.index];
            if (instruction.op != lift::Opcode::Add && instruction.op != lift::Opcode::Subtract) continue;
            const auto one = [&](std::uint32_t reg) {
                const auto it = immediate_values_.find(reg);
                return it != immediate_values_.end() && it->second == 1;
            };
            std::optional<std::uint32_t> counter;
            if (instruction.op == lift::Opcode::Add && one(op.b)) counter = op.a;
            if (instruction.op == lift::Opcode::Add && !counter && one(op.a)) counter = op.b;
            if (instruction.op == lift::Opcode::Subtract && one(op.b)) counter = op.a;
            if (!counter || !is_slot(*counter)) continue;
            const auto it = facts[pc]->find(*counter);
            if (it == facts[pc]->end() || it->second.width != instruction.width ||
                it->second.is_signed != instruction.is_signed) {
                continue;
            }
            // An unsigned check compares the whole register: it must fit.
            if (!instruction.is_signed && widths_[pc][*counter] > instruction.width) continue;
            if (instruction.width < 2) continue;
            const bool safe = instruction.op == lift::Opcode::Add ? it->second.below_max : it->second.above_min;
            if (!safe) continue;
            if (instruction.op == lift::Opcode::Subtract) {
                // a - 1 is a + (2^width - 1), wrapped.
                const auto all_ones = lift::mask_of(instruction.width);
                const auto reg = region_.register_count++;
                region_.immediates.emplace_back(reg, all_ones);
                immediate_values_[reg] = all_ones;
                op.a = *counter;
                op.b = reg;
            }
            instruction.op = lift::Opcode::AddWrapping;
            instruction.reason = 0;
            instruction.trap_exit = lift::no_exit;
            op.index = static_cast<std::uint32_t>(region_.instructions.size());
            region_.instructions.push_back(instruction);
            ++region_.eliminated_checks;
        }
    }

    // What the branch at `branch` on the comparison at `compare` tells about
    // its operands on each side.
    void learn(std::uint32_t compare, std::uint32_t branch, Facts& when_true, Facts& when_false) {
        const auto& op = region_.code[compare];
        const auto& in = region_.instructions[op.index];
        // The operands must be the slot registers' values at the branch:
        // nothing between the comparison and the branch writes them.
        for (auto pc = compare + 1; pc < branch; ++pc) {
            if (deleted_.contains(pc)) continue;
            const auto dst = definition(region_.code[pc]);
            if (dst && (*dst == op.a || *dst == op.b)) return;
        }
        // An unsigned comparison compares whole registers: both must fit
        // its width for the facts to be about `width`-bit values.
        if (!in.is_signed && (widths_[compare][op.a] > in.width || widths_[compare][op.b] > in.width)) return;
        const auto note = [&](Facts& facts, std::uint32_t reg, bool below_max) {
            if (!is_slot(reg)) return;
            auto& fact = facts[reg];
            if (fact.width != in.width || fact.is_signed != in.is_signed) fact = Fact{in.width, in.is_signed};
            (below_max ? fact.below_max : fact.above_min) = true;
        };
        // x < y: x is not the largest value, y not the smallest.
        const auto less = [&](Facts& facts, std::uint32_t x, std::uint32_t y) {
            note(facts, x, true);
            note(facts, y, false);
        };
        switch (in.op) {
        case lift::Opcode::Less: less(when_true, op.a, op.b); break;
        case lift::Opcode::GreaterEqual: less(when_false, op.a, op.b); break;
        case lift::Opcode::Greater: less(when_true, op.b, op.a); break;
        case lift::Opcode::LessEqual: less(when_false, op.b, op.a); break;
        default: break;
        }
    }

    // Pure ops whose results nothing reads.
    void drop_dead() {
        for (bool changed = true; changed;) {
            changed = false;
            std::vector<std::uint32_t> uses(region_.register_count, 0);
            for (std::uint32_t pc = 0; pc < size(); ++pc) {
                if (deleted_.contains(pc)) continue;
                each_use(region_.code[pc], [&](std::uint32_t& reg) { ++uses[reg]; });
            }
            for (std::uint32_t pc = 0; pc < size(); ++pc) {
                const auto& op = region_.code[pc];
                if (deleted_.contains(pc) || can_fail(op)) continue;
                const bool pure = op.code == Code::Lift || op.code == Code::Mask || op.code == Code::Move ||
                                  op.code == Code::Gather;
                if (pure && !is_slot(op.dst) && uses[op.dst] == 0) {
                    deleted_.insert(pc);
                    changed = true;
                }
            }
        }
    }

    // `t = lift ...; ...; store slot = t` becomes `slot = lift ...` when t
    // has no other reader and nothing in between can deoptimize (a
    // deoptimization needs the slot's old value), reads or writes the
    // slot, or is a branch target.
    void write_directly() {
        std::set<std::uint32_t> targets;
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            for (const auto next : successors(pc)) {
                if (next != pc + 1) targets.insert(next);
            }
        }
        std::vector<std::uint32_t> uses(region_.register_count, 0);
        std::map<std::uint32_t, std::uint32_t> defined_at;
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            if (deleted_.contains(pc)) continue;
            each_use(region_.code[pc], [&](std::uint32_t& reg) { ++uses[reg]; });
            if (const auto dst = definition(region_.code[pc]); dst && !is_slot(*dst)) defined_at[*dst] = pc;
        }
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            auto& store = region_.code[pc];
            if (store.code != Code::Store || deleted_.contains(pc) || is_slot(store.a) || uses[store.a] != 1) continue;
            const auto def = defined_at.find(store.a);
            if (def == defined_at.end() || region_.code[def->second].code != Code::Lift) continue;
            const auto slot = store.dst;
            bool clear = true;
            for (auto between = def->second + 1; between <= pc && clear; ++between) {
                if (targets.contains(between)) clear = false;
                if (between == pc || deleted_.contains(between)) continue;
                auto& op = region_.code[between];
                if (can_fail(op) || op.code == Code::Branch || op.code == Code::Jump || op.code == Code::Leave) {
                    clear = false;
                }
                each_use(op, [&](std::uint32_t& reg) { clear = clear && reg != slot; });
                if (const auto dst = definition(op); dst && *dst == slot) clear = false;
            }
            if (!clear) continue;
            auto& lift = region_.code[def->second];
            lift.dst = slot;
            lift.flags |= defines;
            deleted_.insert(pc);
        }
    }

    // Removes deleted ops and renumbers the targets.
    void compact() {
        std::vector<std::uint32_t> moved(size() + 1, 0);
        std::vector<Op> kept;
        for (std::uint32_t pc = 0; pc < size(); ++pc) {
            moved[pc] = static_cast<std::uint32_t>(kept.size());
            if (!deleted_.contains(pc)) kept.push_back(region_.code[pc]);
        }
        moved[size()] = static_cast<std::uint32_t>(kept.size());
        for (auto& op : kept) {
            if (op.code == Code::Jump) op.target = moved[op.target];
            if (op.code == Code::Branch) {
                op.target = moved[op.target];
                op.other = moved[op.other];
            }
            if (op.code == Code::Switch) {
                for (unsigned i = 0; i < op.width; ++i) {
                    auto& target = region_.operands[op.index + i];
                    target = moved[target];
                }
            }
        }
        region_.code = std::move(kept);
    }

    Region& region_;
    std::vector<std::uint8_t> slot_widths_;
    std::set<std::uint32_t> deleted_;
    std::map<std::uint32_t, std::uint64_t> immediate_values_;
    std::vector<std::vector<std::uint8_t>> widths_;
    std::vector<std::uint8_t> reached_;
};

class Compiler {
public:
    Compiler(const nvr::Image& image, std::uint32_t function, const std::vector<std::uint32_t>& blocks,
             const std::vector<BlockForm>& forms)
        : image_(image), function_(image.functions[function]), blocks_(blocks), forms_(forms) {
        region_.function = function;
        region_.entry = blocks.front();
        region_.blocks = blocks;
        for (std::uint32_t i = 0; i < blocks.size(); ++i) local_[blocks[i]] = i;
    }

    std::shared_ptr<const Region> compile() {
        collect_slots();
        analyse_assignments();
        find_checkpoints();
        for (std::uint32_t i = 0; i < blocks_.size(); ++i) emit_block(i);
        // Resolve the block labels of jumps (branches and switches name
        // exit code, whose addresses are known when it is emitted).
        for (auto& op : region_.code) {
            if (op.code == Code::Jump) op.target = labels_.at(op.target - label_base);
        }
        region_.register_count = next_register_;
        std::vector<std::uint8_t> widths;
        for (const auto slot : region_.slots) {
            widths.push_back(static_cast<std::uint8_t>(
                std::min(nvr::lane_count(image_.types[function_.frame_types[slot]]), 64U)));
        }
        // NEURA_TIER2_NO_OPTIMIZE=1 keeps the program as compiled, to
        // compare against the optimized one when debugging.
        if (!std::getenv("NEURA_TIER2_NO_OPTIMIZE")) Optimizer(region_, std::move(widths)).run();
        return std::make_shared<const Region>(std::move(region_));
    }

private:
    // Branch and Jump targets name a block's label as label_base + local
    // index until the labels are known.
    static constexpr std::uint32_t label_base = 0x40000000U;

    const nvr::Block& block(std::uint32_t local) const { return function_.blocks[blocks_[local]]; }
    bool in_region(std::uint32_t image_block) const { return local_.contains(image_block); }

    std::uint32_t slot_register(std::uint32_t slot) {
        const auto [it, inserted] = slot_registers_.emplace(slot, static_cast<std::uint32_t>(region_.slots.size()));
        if (inserted) region_.slots.push_back(slot);
        return it->second;
    }

    std::uint32_t temporary() { return next_register_++; }

    // The exits of a block the region can take: those its form can select.
    void collect_slots() {
        for (std::uint32_t local = 0; local < blocks_.size(); ++local) {
            const auto& b = block(local);
            for (const auto& input : b.inputs) {
                if (input.kind == nvr::ValueKind::Slot) slot_register(input.index);
            }
            for (const auto& exit : b.exits) {
                if (!internal_exit(exit)) continue;
                for (const auto& assignment : exit.assignments) {
                    slot_register(assignment.slot);
                    if (assignment.value.kind == nvr::ValueKind::Slot) slot_register(assignment.value.index);
                }
                for (const auto& argument : exit.arguments) {
                    if (argument.kind == nvr::ValueKind::Slot) slot_register(argument.index);
                }
                if (exit.kind == nvr::ExitKind::State && exit.result_slot != none) slot_register(exit.result_slot);
            }
        }
        next_register_ = static_cast<std::uint32_t>(region_.slots.size());
    }

    // Which slots are assigned on every path from the entry to each block
    // (a must-analysis over the region's edges; the entry is also entered
    // from outside, with nothing assigned), the slots the region writes,
    // and the reads that may precede every write: those slots must be
    // initialized at entry, and no other read can find a slot uninitialized.
    void analyse_assignments() {
        const auto count = region_.slots.size();
        const auto n = blocks_.size();
        std::vector<std::vector<std::uint8_t>> in(n, std::vector<std::uint8_t>(count, 1));
        in[0].assign(count, 0);
        std::vector<std::uint8_t> written(count, 0);
        const auto assigned = [&](const nvr::Exit& exit, std::vector<std::uint8_t>& set) {
            for (const auto& assignment : exit.assignments) set[slot_registers_.at(assignment.slot)] = 1;
            if (exit.kind == nvr::ExitKind::State && exit.result_slot != none) {
                set[slot_registers_.at(exit.result_slot)] = 1;
            }
        };
        for (bool changed = true; changed;) {
            changed = false;
            std::vector<std::vector<std::uint8_t>> next(n, std::vector<std::uint8_t>(count, 1));
            next[0].assign(count, 0);
            std::vector<std::uint8_t> reached(n, 0);
            reached[0] = 1;
            for (std::uint32_t local = 0; local < n; ++local) {
                for (const auto& exit : block(local).exits) {
                    if (!internal_exit(exit) || !in_region(exit.resume)) continue;
                    auto out = in[local];
                    assigned(exit, out);
                    const auto target = local_.at(exit.resume);
                    reached[target] = 1;
                    if (target == 0) continue;  // the entry is also entered with nothing assigned
                    for (std::size_t s = 0; s < count; ++s) next[target][s] &= out[s];
                }
            }
            for (std::uint32_t local = 0; local < n; ++local) {
                if (!reached[local]) next[local].assign(count, 0);
                if (next[local] != in[local]) {
                    in[local] = next[local];
                    changed = true;
                }
            }
        }
        assigned_in_ = in;
        std::vector<std::uint8_t> early(count, 0);
        for (std::uint32_t local = 0; local < n; ++local) {
            const auto& b = block(local);
            const auto read = [&](const nvr::Value& value) {
                if (value.kind != nvr::ValueKind::Slot) return;
                const auto reg = slot_registers_.at(value.index);
                if (!in[local][reg]) early[reg] = 1;
            };
            for (const auto& input : b.inputs) read(input);
            for (const auto& exit : b.exits) {
                if (!internal_exit(exit)) continue;
                for (const auto& assignment : exit.assignments) read(assignment.value);
                for (const auto& argument : exit.arguments) read(argument);
                assigned(exit, written);
            }
        }
        for (std::uint32_t r = 0; r < count; ++r) {
            if (written[r]) region_.written.push_back(r);
            if (early[r]) region_.entry_reads.push_back(r);
        }
    }

    // Checkpoints: the entry and the target of every back edge of a depth-
    // first search from it, so every cycle passes one. Each records the
    // most blocks and polls a path from it takes before the next
    // checkpoint or the region's end.
    void find_checkpoints() {
        const auto n = blocks_.size();
        checkpoint_of_.assign(n, none);
        std::vector<std::uint8_t> state(n, 0);  // 0 new, 1 on the stack, 2 done
        std::vector<std::uint8_t> header(n, 0);
        header[0] = 1;
        // Iterative depth-first search.
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{0, 0}};
        state[0] = 1;
        while (!stack.empty()) {
            auto& [local, next] = stack.back();
            const auto& exits = block(local).exits;
            if (next == exits.size()) {
                state[local] = 2;
                stack.pop_back();
                continue;
            }
            const auto& exit = exits[next++];
            if (!internal_exit(exit) || !in_region(exit.resume)) continue;
            const auto target = local_.at(exit.resume);
            if (state[target] == 1) {
                header[target] = 1;
            } else if (state[target] == 0) {
                state[target] = 1;
                stack.emplace_back(target, 0);
            }
        }
        std::vector<std::optional<std::pair<std::uint64_t, std::uint64_t>>> memo(n);
        // The longest path from `local`, in blocks and in polls, along edges
        // that do not enter a checkpoint: those edges form an acyclic graph,
        // since every back edge enters one.
        std::function<std::pair<std::uint64_t, std::uint64_t>(std::uint32_t)> longest = [&](std::uint32_t local) {
            if (memo[local]) return *memo[local];
            std::uint64_t steps = 0;
            std::uint64_t polls = 0;
            for (const auto& exit : block(local).exits) {
                if (!internal_exit(exit)) continue;
                std::uint64_t s = 0;
                std::uint64_t p = 0;
                if (in_region(exit.resume)) {
                    const auto target = local_.at(exit.resume);
                    if (!header[target]) std::tie(s, p) = longest(target);
                }
                if (exit.kind == nvr::ExitKind::Continue && exit.poll) ++p;
                steps = std::max(steps, s);
                polls = std::max(polls, p);
            }
            memo[local] = std::make_pair(steps + 1, polls);
            return *memo[local];
        };
        for (std::uint32_t local = 0; local < n; ++local) {
            if (!header[local]) continue;
            const auto [steps, polls] = longest(local);
            checkpoint_of_[local] = static_cast<std::uint32_t>(region_.checkpoints.size());
            region_.checkpoints.push_back({blocks_[local], steps, polls});
        }
    }

    // The register of constant `index` masked to `width` bits. A scalar
    // constant's value is its payload, little-endian, as the executor
    // reads it (Execution::State::start): it becomes an immediate. A text
    // or bytes constant is a handle the run assigns: it is loaded from the
    // run's constants at entry.
    std::uint32_t constant_register(std::uint32_t index, unsigned width) {
        const auto key = std::make_pair(index, std::min(width, 64U));
        if (const auto it = constant_registers_.find(key); it != constant_registers_.end()) return it->second;
        const auto reg = temporary();
        const auto& constant = image_.constants[index];
        const auto kind = image_.types[constant.type].kind;
        if (kind != nvr::TypeKind::Text && kind != nvr::TypeKind::Bytes && constant.payload.size() <= 8) {
            std::uint64_t bits = 0;
            for (std::size_t b = 0; b < constant.payload.size(); ++b) {
                bits |= std::uint64_t{constant.payload[b]} << (8 * b);
            }
            region_.immediates.emplace_back(reg, bits & lift::mask_of(key.second));
        } else {
            region_.constants.push_back({reg, index, static_cast<std::uint8_t>(key.second)});
        }
        constant_registers_.emplace(key, reg);
        return reg;
    }

    void emit(Op op) { region_.code.push_back(op); }

    // The register holding block input `i`, masked to `width` lanes for a
    // lowered block (as lift::execute masks its inputs) or whole for a
    // wired one.
    std::uint32_t input_register(const nvr::Value& input, unsigned width, bool masked) {
        if (input.kind == nvr::ValueKind::Constant) return constant_register(input.index, masked ? width : 64);
        const auto reg = slot_registers_.at(input.index);
        if (!masked || width >= 64) return reg;
        const auto temp = temporary();
        Op op;
        op.code = Code::Mask;
        op.dst = temp;
        op.a = reg;
        op.width = static_cast<std::uint8_t>(width);
        emit(op);
        return temp;
    }

    void emit_block(std::uint32_t local) {
        const auto image_block = blocks_[local];
        const auto& b = block(local);
        const auto& form = forms_[local];
        labels_.resize(std::max<std::size_t>(labels_.size(), local + 1));
        labels_[local] = static_cast<std::uint32_t>(region_.code.size());
        if (checkpoint_of_[local] != none) {
            Op op;
            op.code = Code::Budget;
            op.a = checkpoint_of_[local];
            op.block = image_block;
            emit(op);
        }
        // Lane values: the register of the value at each first lane.
        std::map<std::uint32_t, std::uint32_t> lanes;
        std::optional<std::uint32_t> fixed_exit;
        std::optional<std::uint32_t> condition;
        std::uint32_t if_true = 0;
        std::uint32_t if_false = 0;
        std::optional<std::uint32_t> selector;
        std::vector<std::uint32_t> input_regs;
        if (form.kind == BlockForm::Kind::Lifted) {
            const auto& lowered = *form.lifted;
            region_.uses_float = region_.uses_float || lowered.uses_float;
            std::vector<std::uint32_t> regs;
            for (std::size_t i = 0; i < b.inputs.size(); ++i) {
                regs.push_back(input_register(b.inputs[i], lowered.input_widths[i], true));
            }
            for (const auto& in : lowered.code) {
                const auto dst = temporary();
                Op op;
                op.code = Code::Lift;
                op.dst = dst;
                op.a = in.a < regs.size() ? regs[in.a] : 0;
                op.b = in.b < regs.size() ? regs[in.b] : 0;
                op.block = image_block;
                op.index = static_cast<std::uint32_t>(region_.instructions.size());
                region_.instructions.push_back(in);
                emit(op);
                regs.push_back(dst);
            }
            for (const auto& output : lowered.outputs) lanes[output.first_lane] = regs[output.reg];
            switch (lowered.selection) {
            case lift::LoweredBlock::Selection::Fixed: fixed_exit = lowered.fixed_exit; break;
            case lift::LoweredBlock::Selection::Condition:
                condition = regs[lowered.condition];
                if_true = lowered.exit_if_true;
                if_false = lowered.exit_if_false;
                break;
            case lift::LoweredBlock::Selection::Dispatch: fixed_exit = none; break;
            }
        } else if (form.kind == BlockForm::Kind::Wired) {
            const auto& graph = image_.graphs[b.graph];
            std::vector<GatherBit> sources;
            for (const auto& input : b.inputs) {
                const auto count = nvr::lane_count(image_.types[input.type]);
                const auto reg = input_register(input, count, false);
                for (std::uint32_t bit = 0; bit < count; ++bit) sources.push_back({reg, bit});
            }
            const auto gather = [&](std::uint32_t first, std::uint32_t count) {
                // Lanes that are one register's low bits in order are that
                // register, masked when they are fewer than 64.
                bool in_order = true;
                for (std::uint32_t i = 0; i < count && in_order; ++i) {
                    const auto source = sources.at(graph.outputs.at(first + i));
                    in_order = source.reg == sources.at(graph.outputs.at(first)).reg && source.bit == i;
                }
                if (in_order && count > 0) {
                    const auto reg = sources.at(graph.outputs.at(first)).reg;
                    if (count >= 64) return reg;
                    const auto dst = temporary();
                    Op op;
                    op.code = Code::Mask;
                    op.dst = dst;
                    op.a = reg;
                    op.width = static_cast<std::uint8_t>(count);
                    emit(op);
                    return dst;
                }
                const auto dst = temporary();
                Op op;
                op.code = Code::Gather;
                op.dst = dst;
                op.index = static_cast<std::uint32_t>(region_.gathers.size());
                op.width = static_cast<std::uint8_t>(count);
                for (std::uint32_t i = 0; i < count; ++i) {
                    region_.gathers.push_back(sources.at(graph.outputs.at(first + i)));
                }
                emit(op);
                return dst;
            };
            for (const auto& exit : b.exits) {
                if (!internal_exit(exit)) continue;
                const auto lane_value = [&](const nvr::Value& value) {
                    if (value.kind == nvr::ValueKind::Lanes && !lanes.contains(value.index)) {
                        lanes[value.index] = gather(value.index, nvr::lane_count(image_.types[value.type]));
                    }
                };
                for (const auto& assignment : exit.assignments) lane_value(assignment.value);
                for (const auto& argument : exit.arguments) lane_value(argument);
            }
            if (b.exits.size() > 1) {
                selector = gather(b.selector_start, nvr::selector_lanes(static_cast<std::uint32_t>(b.exits.size())));
            } else {
                fixed_exit = 0;
            }
        } else {
            fixed_exit = 0;
        }

        // Exit code. Branch and Switch targets name exit labels, resolved
        // as each exit is emitted.
        std::vector<std::uint32_t> exit_labels(b.exits.size(), none);
        std::vector<std::size_t> branch_ops;
        std::size_t switch_first = 0;
        if (condition) {
            Op op;
            op.code = Code::Branch;
            op.a = *condition;
            op.block = image_block;
            branch_ops.push_back(region_.code.size());
            emit(op);
        } else if (selector) {
            Op op;
            op.code = Code::Switch;
            op.a = *selector;
            op.block = image_block;
            op.width = static_cast<std::uint8_t>(std::min<std::size_t>(b.exits.size(), 255));
            op.index = static_cast<std::uint32_t>(region_.operands.size());
            switch_first = region_.operands.size();
            region_.operands.resize(region_.operands.size() + b.exits.size(), none);
            emit(op);
        }
        std::vector<std::uint32_t> wanted;
        if (fixed_exit) {
            if (*fixed_exit != none) wanted.push_back(*fixed_exit);
        } else if (condition) {
            wanted = {if_true, if_false};
        } else {
            for (std::uint32_t e = 0; e < b.exits.size(); ++e) wanted.push_back(e);
        }
        if (fixed_exit && *fixed_exit == none) {
            // A dispatch: every exit is a call.
            Op op;
            op.code = Code::Deopt;
            op.block = image_block;
            emit(op);
        }
        for (const auto e : wanted) {
            if (exit_labels[e] != none) continue;
            exit_labels[e] = static_cast<std::uint32_t>(region_.code.size());
            emit_exit(local, e, lanes);
        }
        if (condition) {
            auto& op = region_.code[branch_ops.front()];
            op.target = exit_labels[if_true];
            op.other = exit_labels[if_false];
        } else if (selector) {
            for (std::uint32_t e = 0; e < b.exits.size(); ++e) region_.operands[switch_first + e] = exit_labels[e];
        }
    }

    // The register a value of an exit reads, with the reference check the
    // commit makes for a reference read from lanes.
    std::uint32_t value_register(const nvr::Value& value, const std::map<std::uint32_t, std::uint32_t>& lanes,
                                 std::uint32_t image_block) {
        switch (value.kind) {
        case nvr::ValueKind::Slot: return slot_registers_.at(value.index);
        case nvr::ValueKind::Constant: return constant_register(value.index, 64);
        case nvr::ValueKind::Lanes: {
            const auto reg = lanes.at(value.index);
            if (nvr::is_reference(image_.types[value.type].kind)) {
                Op op;
                op.code = Code::CheckRef;
                op.a = reg;
                op.index = value.type;
                op.block = image_block;
                emit(op);
            }
            return reg;
        }
        }
        return 0;
    }

    void emit_exit(std::uint32_t local, std::uint32_t e, const std::map<std::uint32_t, std::uint32_t>& lanes) {
        const auto image_block = blocks_[local];
        const auto& exit = block(local).exits[e];
        if (!internal_exit(exit)) {
            Op op;
            op.code = Code::Deopt;
            op.block = image_block;
            emit(op);
            return;
        }
        // Resolve every value against the state before the transition.
        std::vector<std::uint32_t> sources;
        for (const auto& assignment : exit.assignments) {
            sources.push_back(value_register(assignment.value, lanes, image_block));
        }
        std::vector<std::uint32_t> arguments;
        for (const auto& argument : exit.arguments) arguments.push_back(value_register(argument, lanes, image_block));
        std::uint32_t result = none;
        if (exit.kind == nvr::ExitKind::State) {
            // The operation reads only its arguments and heap or global
            // storage, never a slot, so it can run before the assignments
            // commit, while a trap can still deoptimize.
            Op op;
            op.code = Code::State;
            op.index = static_cast<std::uint32_t>(region_.exits.size());
            region_.exits.push_back(&exit);
            op.other = static_cast<std::uint32_t>(region_.operands.size());
            op.width = static_cast<std::uint8_t>(arguments.size());
            region_.operands.insert(region_.operands.end(), arguments.begin(), arguments.end());
            op.block = image_block;
            if (exit.result_slot != none) {
                result = temporary();
                op.dst = result;
            } else {
                op.dst = none;
            }
            emit(op);
        }
        // Parallel assignment: a source that is a slot this exit assigns is
        // copied first.
        std::set<std::uint32_t> targets;
        for (const auto& assignment : exit.assignments) targets.insert(slot_registers_.at(assignment.slot));
        for (auto& source : sources) {
            if (source < region_.slots.size() && targets.contains(source)) {
                const auto temp = temporary();
                Op op;
                op.code = Code::Move;
                op.dst = temp;
                op.a = source;
                emit(op);
                source = temp;
            }
        }
        for (std::size_t i = 0; i < exit.assignments.size(); ++i) {
            Op op;
            op.code = Code::Store;
            op.dst = slot_registers_.at(exit.assignments[i].slot);
            op.a = sources[i];
            emit(op);
        }
        if (result != none) {
            Op op;
            op.code = Code::Store;
            op.dst = slot_registers_.at(exit.result_slot);
            op.a = result;
            emit(op);
        }
        if (exit.kind == nvr::ExitKind::Continue && exit.poll) {
            Op op;
            op.code = Code::Poll;
            op.block = image_block;
            emit(op);
        }
        Op complete;
        complete.code = Code::Complete;
        complete.block = image_block;
        emit(complete);
        Op next;
        if (in_region(exit.resume)) {
            next.code = Code::Jump;
            next.target = label_base + local_.at(exit.resume);
        } else {
            next.code = Code::Leave;
            next.target = exit.resume;
        }
        next.block = image_block;
        emit(next);
    }

    const nvr::Image& image_;
    const nvr::Function& function_;
    const std::vector<std::uint32_t>& blocks_;
    const std::vector<BlockForm>& forms_;
    std::map<std::uint32_t, std::uint32_t> local_;
    Region region_;
    std::map<std::uint32_t, std::uint32_t> slot_registers_;
    std::map<std::pair<std::uint32_t, unsigned>, std::uint32_t> constant_registers_;
    std::uint32_t next_register_ = 0;
    std::vector<std::vector<std::uint8_t>> assigned_in_;
    std::vector<std::uint32_t> checkpoint_of_;
    std::vector<std::uint32_t> labels_;
};

} // namespace

bool inline_state_operation(const nvr::Exit& exit) {
    switch (exit.operation) {
    case nvr::StateOp::ReadTag:
    case nvr::StateOp::ReadField:
    case nvr::StateOp::WriteField:
    case nvr::StateOp::ReadGlobal:
    case nvr::StateOp::WriteGlobal:
    case nvr::StateOp::ReadLength:
    case nvr::StateOp::ReadElement:
    case nvr::StateOp::WriteElement:
    case nvr::StateOp::ReadVersion:
    case nvr::StateOp::TextEqual:
    case nvr::StateOp::ClassIs:
    case nvr::StateOp::ClassCast:
        return true;
    case nvr::StateOp::ScalarText:
        // With an enum's names as arguments it selects one; with one
        // argument it builds text.
        return exit.arguments.size() > 1;
    default:
        return false;
    }
}

Formation form_region(const nvr::Image& image, std::uint32_t function, std::uint32_t entry,
                      const std::function<BlockForm(std::uint32_t block)>& form, std::size_t max_blocks) {
    Formation result;
    const auto& blocks = image.functions.at(function).blocks;
    std::map<std::uint32_t, BlockForm> forms;
    const auto admissible = [&](std::uint32_t b) {
        if (const auto it = forms.find(b); it != forms.end()) return it->second.kind != BlockForm::Kind::None;
        auto f = form(b);
        if (f.kind == BlockForm::Kind::None && f.pending) result.pending = true;
        // A dispatch block's exits are all calls; a block with no exit that
        // can stay in a region is only overhead in one.
        if (f.kind == BlockForm::Kind::Lifted && f.lifted->selection == lift::LoweredBlock::Selection::Dispatch) {
            f.kind = BlockForm::Kind::None;
        }
        if (f.kind != BlockForm::Kind::None &&
            std::none_of(blocks[b].exits.begin(), blocks[b].exits.end(), internal_exit)) {
            f.kind = BlockForm::Kind::None;
        }
        forms.emplace(b, f);
        return f.kind != BlockForm::Kind::None;
    };
    if (!admissible(entry)) {
        result.reason = "the entry is not lifted or cannot continue in a region";
        return result;
    }
    std::vector<std::uint32_t> members{entry};
    std::set<std::uint32_t> seen{entry};
    for (std::size_t next = 0; next < members.size(); ++next) {
        for (const auto& exit : blocks[members[next]].exits) {
            if (!internal_exit(exit) || seen.contains(exit.resume) || members.size() >= max_blocks) continue;
            if (!admissible(exit.resume)) continue;
            seen.insert(exit.resume);
            members.push_back(exit.resume);
        }
    }
    const bool internal_edge = std::any_of(members.begin(), members.end(), [&](std::uint32_t b) {
        return std::any_of(blocks[b].exits.begin(), blocks[b].exits.end(),
                           [&](const nvr::Exit& exit) { return internal_exit(exit) && seen.contains(exit.resume); });
    });
    if (!internal_edge) {
        result.reason = "no exit stays in the region";
        return result;
    }
    std::vector<BlockForm> member_forms;
    for (const auto b : members) member_forms.push_back(forms.at(b));
    Compiler compiler(image, function, members, member_forms);
    result.region = compiler.compile();
    return result;
}

std::string describe(const Region& region) {
    std::ostringstream out;
    out << "region function " << region.function << " entry " << region.entry << ": " << region.blocks.size()
        << " blocks, " << region.slots.size() << " slot registers, " << region.register_count << " registers\n";
    out << "  slots:";
    for (std::size_t r = 0; r < region.slots.size(); ++r) out << " r" << r << "=s" << region.slots[r];
    out << "\n  entry reads:";
    for (const auto r : region.entry_reads) out << " r" << r;
    out << "\n  written:";
    for (const auto r : region.written) out << " r" << r;
    out << "\n  constants:";
    for (const auto& constant : region.constants) out << " r" << constant.reg << "=c" << constant.index;
    out << "\n  immediates:";
    for (const auto& [reg, value] : region.immediates) out << " r" << reg << "=" << value;
    out << "\n  checkpoints:";
    for (const auto& c : region.checkpoints) {
        out << " b" << c.block << "(" << c.steps << " steps, " << c.polls << " polls)";
    }
    out << "\n";
    const auto completed = [](const Op& op) {
        return (op.flags & completes) ? " (completes block " + std::to_string(op.block) + ")" : std::string();
    };
    static const char* names[] = {"budget", "lift", "mask", "move", "gather", "store", "branch", "switch",
                                  "jump", "checkref", "state", "poll", "complete", "leave", "deopt"};
    for (std::size_t pc = 0; pc < region.code.size(); ++pc) {
        const auto& op = region.code[pc];
        out << "  " << pc << ": " << names[static_cast<int>(op.code)];
        switch (op.code) {
        case Code::Budget: out << " checkpoint " << op.a << " block " << op.block; break;
        case Code::Lift:
            out << " r" << op.dst << " = op" << static_cast<int>(region.instructions[op.index].op) << "(r" << op.a
                << ", r" << op.b << ") block " << op.block << ((op.flags & defines) ? " (defines its slot)" : "");
            break;
        case Code::Mask: out << " r" << op.dst << " = r" << op.a << " & mask " << int(op.width); break;
        case Code::Move:
        case Code::Store: out << " r" << op.dst << " = r" << op.a; break;
        case Code::Gather: out << " r" << op.dst << " = " << int(op.width) << " lanes"; break;
        case Code::Branch: out << " r" << op.a << " ? " << op.target << " : " << op.other; break;
        case Code::Switch: out << " r" << op.a << " of " << int(op.width) << " block " << op.block; break;
        case Code::Jump: out << " " << op.target << completed(op); break;
        case Code::CheckRef: out << " r" << op.a << " type " << op.index; break;
        case Code::State:
            out << " op" << static_cast<int>(region.exits[op.index]->operation) << " -> "
                << (op.dst == none ? std::string("none") : "r" + std::to_string(op.dst)) << " block " << op.block;
            break;
        case Code::Poll:
        case Code::Complete: out << " block " << op.block; break;
        case Code::Leave: out << " to block " << op.target << completed(op); break;
        case Code::Deopt: out << " block " << op.block; break;
        }
        out << "\n";
    }
    return out.str();
}

} // namespace neura::nvm::tier2

namespace neura::nvm {

std::uint64_t TierCache::region_threshold(std::uint32_t function, std::uint32_t block) const {
    const std::lock_guard lock(mutex_);
    return std::max<std::uint64_t>(options_.region_after, regions_[first_entry_[function] + block].next_attempt);
}

TierCache::RegionResolution TierCache::region(std::uint32_t function, std::uint32_t block, std::uint64_t arrivals) {
    const auto index = first_entry_[function] + block;
    {
        const std::lock_guard lock(mutex_);
        auto& found = regions_[index];
        if (found.state == RegionEntry::State::Settled &&
            (found.refresh_at == 0 || arrivals < found.refresh_at || found.refreshing)) {
            return {found.region.get(), true, found.refresh_at};
        }
        if (found.state == RegionEntry::State::Forming) {
            return {nullptr, false, std::max<std::uint64_t>(arrivals * 2, 2)};
        }
        if (found.state == RegionEntry::State::Settled) {
            // Formed while blocks it would reach were still being lifted:
            // form it again now; the old region stays valid for runs that
            // hold it.
            found.refreshing = true;
        } else {
            if (arrivals < found.next_attempt) return {nullptr, false, found.next_attempt};
            found.state = RegionEntry::State::Forming;
        }
    }
    // Formation reads the settled lowered blocks; it runs outside the lock,
    // taking it for each block it looks at.
    const auto& blocks = image_.functions[function].blocks;
    const auto form = [&](std::uint32_t b) {
        tier2::BlockForm result;
        const auto& decoded = blocks[b];
        const auto& graph = image_.graphs[decoded.graph];
        if (graph.nodes.empty()) {
            // As plan_tier decides for a block without neurons.
            if (!graph.outputs.empty()) {
                result.kind = tier2::BlockForm::Kind::Wired;
            } else if (decoded.exits.size() == 1) {
                result.kind = tier2::BlockForm::Kind::Empty;
            }
            return result;
        }
        const std::lock_guard lock(mutex_);
        const auto& lifted = entry(function, b);
        if (lifted.state != Entry::State::Settled) {
            result.pending = true;
        } else if (lifted.lowered) {
            result.kind = tier2::BlockForm::Kind::Lifted;
            result.lifted = lifted.lowered.get();
        }
        return result;
    };
    auto formation = tier2::form_region(image_, function, block, form);

    const std::lock_guard lock(mutex_);
    auto& found = regions_[index];
    const auto patience = std::uint64_t{options_.region_after} * options_.region_patience;
    if (found.state == RegionEntry::State::Settled) {
        // A refresh: keep the old region alive, publish the new one, and
        // refresh again later while blocks are still pending.
        found.refreshing = false;
        ++found.refreshes;
        found.refresh_at = formation.pending && found.refreshes < 4 ? arrivals * 4 : 0;
        if (!formation.region) return {found.region.get(), true, found.refresh_at};
        retired_regions_.push_back(std::move(found.region));
        found.region = std::move(formation.region);
        ++statistics_.refreshed;
        if (std::getenv("NEURA_TIER_DUMP")) std::fputs(tier2::describe(*found.region).c_str(), stderr);
        return {found.region.get(), true, found.refresh_at};
    }
    if (formation.pending && arrivals < patience) {
        // Blocks the region would reach are still being lifted: form it once
        // they are, or once the block has waited long enough.
        found.state = RegionEntry::State::Unknown;
        found.next_attempt = std::max<std::uint64_t>(arrivals * 2, 2);
        return {nullptr, false, found.next_attempt};
    }
    found.state = RegionEntry::State::Settled;
    found.refresh_at = formation.pending ? arrivals * 4 : 0;
    found.region = std::move(formation.region);
    if (found.region && std::getenv("NEURA_TIER_DUMP")) std::fputs(tier2::describe(*found.region).c_str(), stderr);
    if (found.region) {
        ++statistics_.regions;
        statistics_.region_blocks += found.region->blocks.size();
        statistics_.checks_eliminated += found.region->eliminated_checks;
    } else {
        ++statistics_.region_rejections[formation.reason];
    }
    return {found.region.get(), true, found.refresh_at};
}

void TierCache::add_region_events(const RegionCounts& counts,
                                  const std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t>& steps,
                                  const std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t>& deopts,
                                  const std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t>& leaves) {
    const std::lock_guard lock(mutex_);
    auto& total = statistics_.region_counts;
    total.entries += counts.entries;
    total.leaves += counts.leaves;
    total.refused += counts.refused;
    total.budget += counts.budget;
    total.checks += counts.checks;
    total.selectors += counts.selectors;
    total.references += counts.references;
    total.state_traps += counts.state_traps;
    total.exits += counts.exits;
    for (const auto& [key, value] : steps) statistics_.region_steps[key] += value;
    for (const auto& [key, value] : deopts) statistics_.region_deopts[key] += value;
    for (const auto& [key, value] : leaves) statistics_.region_leaves[key] += value;
}

} // namespace neura::nvm
