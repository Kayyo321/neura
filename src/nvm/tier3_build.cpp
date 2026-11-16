#include "tier3.hpp"

#include "heap.hpp"
#include "nvr/format.hpp"
#include "tier1.hpp"

#include "jit/tier3_native.hpp"
#include "kernels/kernel_binding.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <utility>

namespace neura::nvm::tier3 {
namespace {

using tier2::Code;

// The failure and arity of a lifted instruction, as tier2.cpp defines them
// for the region compiler (instruction_can_fail, instruction_arity).
bool can_fail(const lift::Instruction& in) {
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

unsigned arity(const lift::Instruction& in) {
    switch (in.op) {
    case lift::Opcode::Constant:
        return 0;
    case lift::Opcode::Negate:
    case lift::Opcode::Not:
    case lift::Opcode::BitNot:
    case lift::Opcode::Index:
    case lift::Opcode::ConvertInteger:
    case lift::Opcode::ValueHash:
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

ValueType integer(unsigned width) {
    return {Type::Int, static_cast<std::uint8_t>(std::clamp(width, 1U, 64U))};
}

ValueType float_type(lift::FloatType format) {
    if (format == lift::FloatType::Binary32) return {Type::F32, 32};
    if (format == lift::FloatType::Binary64) return {Type::F64, 64};
    return integer(float_bits(format));
}

// The type of a lifted instruction's result given its operands' widths
// (tier2.cpp, result_bits): floats from float arithmetic, an integer wide
// enough for the result otherwise.
ValueType result_type(const lift::Instruction& in, unsigned a, unsigned b) {
    using lift::Opcode;
    switch (in.op) {
    case Opcode::Constant: return integer(bits_of(in.immediate));
    case Opcode::Add:
    case Opcode::AddWrapping:
    case Opcode::Subtract:
    case Opcode::SubtractWrapping:
    case Opcode::Negate:
    case Opcode::Multiply:
    case Opcode::BitNot:
    case Opcode::ShiftLeft:
    case Opcode::ShiftRight: return integer(in.width);
    case Opcode::Divide:
    case Opcode::Remainder: return integer(in.is_signed ? in.width : a);
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
    case Opcode::FloatGreaterEqual: return integer(1);
    case Opcode::BitAnd: return integer(std::min(a, b));
    case Opcode::BitOr:
    case Opcode::BitXor: return integer(std::max(a, b));
    case Opcode::Index: return integer(a);
    case Opcode::ValueHash: return integer(32);
    case Opcode::ConvertInteger:
    case Opcode::FloatToInteger: return integer(in.to_width);
    case Opcode::FloatAdd:
    case Opcode::FloatSubtract:
    case Opcode::FloatMultiply:
    case Opcode::FloatDivide: return float_type(in.format);
    case Opcode::FloatNegate: return in.format == lift::FloatType::Binary32 || in.format == lift::FloatType::Binary64
                                         ? float_type(in.format)
                                         : integer(std::max(a, float_bits(in.format)));
    case Opcode::FloatFromInteger:
    case Opcode::FloatConvert: return float_type(in.to_format);
    }
    return integer(64);
}

bool control(Code code) {
    switch (code) {
    case Code::Branch:
    case Code::Switch:
    case Code::Dispatch:
    case Code::Jump:
    case Code::Leave:
    case Code::Return:
    case Code::Deopt:
    case Code::Call:
    case Code::Helper: return true;
    default: return false;
    }
}

// Whether `exit` is the kernel exit that native code computes itself (design
// 7.3): the KERN entry neura.math.sqrt at ABI version 1, whose kernel takes
// one binary64 argument, charges the constant formula 1 and returns a
// binary64, called with one binary64 argument. Its kernel is the correctly
// rounded square root with every NaN canonical (neura_math.c), which is
// Canon(Sqrt(x)) with the charge K + 1.
bool inline_sqrt(const nvr::Image& image, const nvr::Exit& exit) {
    if (exit.kind != nvr::ExitKind::State || exit.operation != nvr::StateOp::Kernel) return false;
    if (exit.immediate >= image.kernels.size()) return false;
    const auto& entry = image.kernels[exit.immediate];
    if (entry.name != "neura.math.sqrt" || entry.abi_version != 1) return false;
    const auto* info = kernels::find_kernel(entry.name, entry.abi_version);
    if (info == nullptr || info->param_count != 1 || info->params[0] != kernels::Kind::F64 ||
        info->result != kernels::Kind::F64 || info->buffer_count != 0 || info->cost.step_count != 1 ||
        info->cost.steps[0].op != kernels::Op::Const || info->cost.steps[0].value != 1) {
        return false;
    }
    const auto binary64 = [&](std::uint32_t type) {
        return type < image.types.size() && image.types[type].kind == nvr::TypeKind::Float &&
               image.types[type].width == 64;
    };
    return exit.arguments.size() == 1 && binary64(exit.arguments[0].type) && entry.parameter_types.size() == 1 &&
           binary64(entry.parameter_types[0]) && binary64(entry.result_type);
}

// Most (assigned slot, flag) entries all frame states of a function may hold.
constexpr std::uint64_t max_state_entries = std::uint64_t{1} << 23;
constexpr std::size_t max_instructions = std::size_t{1} << 21;

class Builder {
public:
    Builder(const nvr::Image& image, const tier2::Region& region, Variant variant)
        : image_(image), region_(region), variant_(variant) {}

    BuildResult run(bool check) {
        BuildResult result;
        auto program = std::make_unique<Program>();
        f_ = &program->ir;
        if (!region_.program) return refuse("not a function program");
        prepare();
        if (!error_.empty()) return refuse(error_);
        analyse();
        if (!error_.empty()) return refuse(error_);
        for (std::uint32_t b = 0; b < f_->blocks.size() && error_.empty(); ++b) {
            if (f_->blocks[b].entry == none) fill(b);
        }
        if (!error_.empty()) return refuse(error_);
        finish();
        if (!error_.empty()) return refuse(error_);
        if (check) {
            const auto violations = verify(image_, *f_);
            if (!violations.empty()) {
                // A violation is a bug in the builder: it must not pass as a
                // refusal that only the statistics show.
                std::fprintf(stderr, "neura: NEURA_TIER3_VERIFY: the IR of function %u (program %u) is not well formed:\n",
                             region_.function, region_.program_id);
                for (const auto& violation : violations) std::fprintf(stderr, "  %s\n", violation.c_str());
                std::fprintf(stderr, "%s", describe(*f_).c_str());
                std::abort();
            }
        }
        result.program = std::move(program);
        return result;
    }

private:
    BuildResult refuse(std::string reason) {
        BuildResult result;
        result.reason = std::move(reason);
        return result;
    }

    // --- Variables -------------------------------------------------------------

    std::uint32_t slot_var(std::uint32_t r) const { return r; }
    std::uint32_t flag_var(std::uint32_t r) const { return slots_ + r; }
    std::uint32_t c_var() const { return 2 * slots_; }
    std::uint32_t k_var() const { return 2 * slots_ + 1; }
    std::uint32_t temp_var(std::uint32_t reg) const { return 2 * slots_ + 2 + (reg - slots_); }

    ValueType var_type(std::uint32_t var) const {
        if (var < slots_) return f_->slot_types[var];
        if (var < 2 * slots_) return integer(1);
        return integer(64);
    }

    void prepare() {
        const auto& fn = image_.functions.at(region_.function);
        slots_ = static_cast<std::uint32_t>(fn.frame_types.size());
        f_->region = &region_;
        f_->function = region_.function;
        f_->slot_count = slots_;
        f_->parameter_count = region_.parameter_count;
        f_->uses_float = region_.uses_float;
        // A function with an inline kernel keeps K as an SSA value, and its
        // native code depends on the floating-point environment.
        for (const auto& op : region_.code) {
            if (op.code == Code::Helper && op.index < region_.exits.size() &&
                inline_sqrt(image_, *region_.exits[op.index])) {
                f_->inline_kernels = true;
                f_->uses_float = true;
            }
        }
        f_->programs.push_back(&region_);
        f_->exits = region_.exits;
        f_->dispatch_tables = region_.dispatch_tables;
        for (std::uint32_t table = 0; table < region_.dispatch_tables.size(); ++table) {
            f_->dispatch_origins.emplace_back(0, table);
        }
        for (const auto type : fn.frame_types) {
            const auto& t = image_.types[type];
            ValueType v;
            if (nvr::is_reference(t.kind)) v = {Type::Ref, 64};
            else if (t.kind == nvr::TypeKind::Float && t.width == 32) v = {Type::F32, 32};
            else if (t.kind == nvr::TypeKind::Float && t.width == 64) v = {Type::F64, 64};
            else v = integer(std::min(nvr::lane_count(t), 64U));
            f_->slot_types.push_back(v);
        }
        // Value 0 is never used: a read that fails returns it.
        Inst placeholder;
        placeholder.op = Op::Const;
        placeholder.type = integer(1);
        make(std::move(placeholder));
        for (const auto& [reg, value] : region_.immediates) immediates_[reg] = value;
        for (const auto& constant : region_.constants) constants_[constant.reg] = constant;
        assigned_.assign(slots_, 0);
    }

    // --- The control-flow graph of the program's code --------------------------

    std::vector<std::uint32_t> successors(std::uint32_t pc) const {
        const auto& op = region_.code[pc];
        switch (op.code) {
        case Code::Branch: return {op.target, op.other};
        case Code::Switch: {
            std::vector<std::uint32_t> next;
            for (unsigned i = 0; i < op.width; ++i) next.push_back(region_.operands[op.index + i]);
            return next;
        }
        case Code::Dispatch: {
            std::vector<std::uint32_t> next;
            for (unsigned i = 0; i < op.width; ++i) next.push_back(region_.operands[op.other + i]);
            return next;
        }
        case Code::Jump:
        case Code::Call:
        case Code::Helper: return {op.target};
        case Code::Leave:
        case Code::Return:
        case Code::Deopt: return {};
        default: return {pc + 1};
        }
    }

    void analyse() {
        const auto& code = region_.code;
        const auto n = static_cast<std::uint32_t>(code.size());
        if (region_.entries.empty()) {
            error_ = "the program has no entry";
            return;
        }
        // Reachable ops.
        std::vector<std::uint8_t> reached(n, 0);
        std::vector<std::uint32_t> work;
        for (const auto& entry : region_.entries) {
            if (entry.label >= n) {
                error_ = "an entry label is outside the code";
                return;
            }
            if (!reached[entry.label]) {
                reached[entry.label] = 1;
                work.push_back(entry.label);
            }
        }
        std::vector<std::uint8_t> leader(n, 0);
        for (const auto& entry : region_.entries) leader[entry.label] = 1;
        while (!work.empty()) {
            const auto pc = work.back();
            work.pop_back();
            for (const auto next : successors(pc)) {
                if (next >= n) {
                    error_ = "a jump leaves the code";
                    return;
                }
                if (control(code[pc].code)) leader[next] = 1;
                if (!reached[next]) {
                    reached[next] = 1;
                    work.push_back(next);
                }
            }
        }
        // The slots the code assigns.
        for (std::uint32_t pc = 0; pc < n; ++pc) {
            if (!reached[pc]) continue;
            const auto& op = code[pc];
            std::uint32_t dst = none;
            switch (op.code) {
            case Code::Store: dst = op.dst; break;
            case Code::Lift:
                if (op.flags & tier2::defines) dst = op.dst;
                break;
            case Code::Call:
            case Code::Helper: dst = op.dst; break;
            default: break;
            }
            if (dst != none && dst < slots_) assigned_[dst] = 1;
        }
        for (std::uint32_t pc = 1; pc < n; ++pc) {
            if (reached[pc] && control(code[pc - 1].code)) leader[pc] = 1;
        }
        // Blocks: the entries' blocks first, then one per leader in order.
        const auto entry_count = static_cast<std::uint32_t>(region_.entries.size());
        f_->blocks.resize(entry_count);
        for (std::uint32_t e = 0; e < entry_count; ++e) {
            f_->blocks[e].entry = e;
            f_->entries.push_back({region_.entries[e].block, e});
        }
        pc_block_.assign(n + 1, none);
        for (std::uint32_t pc = 0; pc < n; ++pc) {
            if (!reached[pc] || !leader[pc]) continue;
            pc_block_[pc] = static_cast<std::uint32_t>(f_->blocks.size());
            first_pc_.push_back(pc);
            f_->blocks.emplace_back();
        }
        // The end of each block's ops.
        block_end_.resize(f_->blocks.size(), 0);
        for (std::uint32_t b = entry_count; b < f_->blocks.size(); ++b) {
            auto pc = first_pc_[b - entry_count];
            for (;;) {
                if (control(code[pc].code)) {
                    block_end_[b] = pc + 1;
                    break;
                }
                if (pc + 1 >= n || pc_block_[pc + 1] != none) {
                    block_end_[b] = pc + 1;
                    break;
                }
                ++pc;
            }
        }
        // Edges.
        for (std::uint32_t e = 0; e < entry_count; ++e) add_edge(e, pc_block_[region_.entries[e].label]);
        for (std::uint32_t b = entry_count; b < f_->blocks.size(); ++b) {
            const auto last = block_end_[b] - 1;
            const auto& op = code[last];
            if (control(op.code)) {
                for (const auto next : successors(last)) add_edge(b, pc_block_[next]);
            } else if (last + 1 < n) {
                add_edge(b, pc_block_[last + 1]);
            }
        }
        // The image block of each IR block: a label's block, else the block
        // of the code that selects among its exits.
        std::map<std::uint32_t, std::uint32_t> label_block;
        for (std::uint32_t image_block = 0; image_block < region_.block_labels.size(); ++image_block) {
            label_block.emplace(region_.block_labels[image_block], image_block);
        }
        for (std::uint32_t b = entry_count; b < f_->blocks.size(); ++b) {
            auto& block = f_->blocks[b];
            block.origin_function = region_.function;
            const auto found = label_block.find(first_pc_[b - entry_count]);
            if (found != label_block.end()) {
                block.origin_block = found->second;
                block.image_start = true;
                label_of_[found->second] = b;
            }
        }
        for (std::uint32_t b = entry_count; b < f_->blocks.size(); ++b) {
            auto& block = f_->blocks[b];
            if (block.origin_block != none) continue;
            for (const auto pred : block.preds) {
                if (pred >= entry_count && f_->blocks[pred].origin_block != none) {
                    block.origin_block = f_->blocks[pred].origin_block;
                    break;
                }
            }
            if (block.origin_block == none) error_ = "a block of code belongs to no image block";
        }
        std::uint64_t assigned = 0;
        for (const auto a : assigned_) assigned += a;
        if (assigned * 2 * region_.block_labels.size() > max_state_entries) {
            error_ = "the function is too large";
            return;
        }
        def_.resize(f_->blocks.size());
        start_.resize(f_->blocks.size());
        incomplete_.resize(f_->blocks.size());
        phis_.resize(f_->blocks.size());
        entry_insts_.resize(f_->blocks.size());
        sealed_.assign(f_->blocks.size(), 0);
        filled_edges_.assign(f_->blocks.size(), 0);
        // Entry blocks have no predecessors: sealed, and filled now.
        for (std::uint32_t e = 0; e < entry_count; ++e) {
            sealed_[e] = 1;
            filled(e);
        }
    }

    void add_edge(std::uint32_t from, std::uint32_t to) {
        f_->blocks[from].succs.push_back(to);
        f_->blocks[to].preds.push_back(from);
    }

    // `block` is complete: its successors may now be sealed.
    void filled(std::uint32_t block) {
        for (const auto next : f_->blocks[block].succs) {
            if (++filled_edges_[next] == f_->blocks[next].preds.size()) seal(next);
        }
    }

    void seal(std::uint32_t block) {
        sealed_[block] = 1;
        auto pending = std::move(incomplete_[block]);
        incomplete_[block].clear();
        for (const auto& [var, phi] : pending) add_phi_operands(var, phi, block);
    }

    // --- SSA construction (Braun et al.) ---------------------------------------

    std::uint32_t make(Inst in) {
        const auto id = static_cast<std::uint32_t>(f_->insts.size());
        f_->insts.push_back(std::move(in));
        repl_.push_back(id);
        return id;
    }

    std::uint32_t find(std::uint32_t v) {
        while (repl_[v] != v) {
            repl_[v] = repl_[repl_[v]];
            v = repl_[v];
        }
        return v;
    }

    std::uint32_t read(std::uint32_t var, std::uint32_t block) {
        if (!error_.empty()) return 0;
        if (const auto it = def_[block].find(var); it != def_[block].end()) return it->second;
        return read_start(var, block);
    }

    // The value of `var` at the start of `block`.
    std::uint32_t read_start(std::uint32_t var, std::uint32_t block) {
        if (!error_.empty()) return 0;
        if (const auto it = start_[block].find(var); it != start_[block].end()) return it->second;
        std::uint32_t value = 0;
        const auto& b = f_->blocks[block];
        if (b.entry != none) {
            value = entry_value(var, block);
        } else if (!sealed_[block]) {
            value = new_phi(var, block);
            incomplete_[block].emplace_back(var, value);
        } else if (b.preds.size() == 1) {
            value = read(var, b.preds[0]);
        } else {
            value = new_phi(var, block);
            start_[block][var] = value;
            add_phi_operands(var, value, block);
        }
        start_[block][var] = value;
        return value;
    }

    std::uint32_t new_phi(std::uint32_t var, std::uint32_t block) {
        Inst in;
        in.op = Op::Phi;
        in.type = var_type(var);
        in.extra = static_cast<std::uint32_t>(f_->extras.size());
        in.count = static_cast<std::uint32_t>(f_->blocks[block].preds.size());
        in.origin_block = f_->blocks[block].origin_block;
        in.origin_function = region_.function;
        f_->extras.resize(f_->extras.size() + in.count, none);
        const auto id = make(std::move(in));
        phis_[block].push_back(id);
        phi_vars_.push_back(id);
        return id;
    }

    void add_phi_operands(std::uint32_t var, std::uint32_t phi, std::uint32_t block) {
        const auto preds = f_->blocks[block].preds;
        for (std::size_t i = 0; i < preds.size(); ++i) {
            const auto value = read(var, preds[i]);
            f_->extras[f_->insts[phi].extra + i] = value;
        }
    }

    // What `var` holds when the activation enters at `block`.
    std::uint32_t entry_value(std::uint32_t var, std::uint32_t block) {
        Inst in;
        in.origin_function = region_.function;
        if (var < slots_) {
            in.op = Op::EntryLoad;
            in.imm = var;
        } else if (var < 2 * slots_) {
            in.op = Op::EntryFlag;
            in.imm = var - slots_;
        } else if (var == c_var() || var == k_var()) {
            in.op = Op::EntryCounter;
            in.imm = var == c_var() ? 0 : 1;
        } else {
            error_ = "a temporary is live across a block";
            return 0;
        }
        in.type = var_type(var);
        const auto id = make(std::move(in));
        entry_insts_[block].push_back(id);
        return id;
    }

    // --- Emission --------------------------------------------------------------

    std::uint32_t emit(Inst in) {
        in.origin_block = image_block_;
        in.origin_function = region_.function;
        in.origin_seq = seq_++;
        const auto id = make(std::move(in));
        f_->blocks[cur_].insts.push_back(id);
        return id;
    }

    std::uint32_t constant(std::uint64_t bits, ValueType type) {
        Inst in;
        in.op = Op::Const;
        in.type = type;
        in.imm = bits;
        return emit(std::move(in));
    }

    // Flag value One, once per block.
    std::uint32_t one() {
        const auto found = ones_.find(cur_);
        if (found != ones_.end()) return found->second;
        const auto id = constant(1, integer(1));
        ones_[cur_] = id;
        return id;
    }

    std::uint32_t value(std::uint32_t reg) {
        if (reg < slots_) return read(slot_var(reg), cur_);
        if (const auto it = immediates_.find(reg); it != immediates_.end()) {
            return constant(it->second, integer(bits_of(it->second)));
        }
        if (const auto it = constants_.find(reg); it != constants_.end()) {
            Inst in;
            in.op = Op::RunConst;
            in.imm = it->second.index;
            in.type = it->second.width >= 64 ? ValueType{Type::Ref, 64} : integer(it->second.width);
            return emit(std::move(in));
        }
        return read(temp_var(reg), cur_);
    }

    unsigned width_of(std::uint32_t v) { return f_->insts[v].type.width; }

    void write(std::uint32_t reg, std::uint32_t v, bool initializes) {
        if (reg < slots_) {
            def_[cur_][slot_var(reg)] = v;
            if (initializes) def_[cur_][flag_var(reg)] = one();
        } else {
            def_[cur_][temp_var(reg)] = v;
        }
    }

    std::uint32_t counter_add(std::uint32_t c, std::uint64_t amount) {
        Inst in;
        in.op = Op::CounterAdd;
        in.type = integer(64);
        in.operands[0] = c;
        in.imm = amount;
        return emit(std::move(in));
    }

    void complete_block() {
        const auto c = counter_add(read(c_var(), cur_), 1);
        def_[cur_][c_var()] = c;
    }

    // K here, in a function with inline kernels; none otherwise.
    std::uint32_t kernel_steps() { return f_->inline_kernels ? read(k_var(), cur_) : none; }

    // The state at the entry of image block `b`: tier 1 re-runs the block.
    std::uint32_t entry_state(std::uint32_t b) {
        if (const auto it = entry_states_.find(b); it != entry_states_.end()) return it->second;
        const auto found = label_of_.find(b);
        if (found == label_of_.end()) {
            error_ = "an op belongs to a block that has no code";
            return 0;
        }
        const auto label = found->second;
        FrameState state;
        state.function = region_.function;
        state.block = b;
        state.slots.resize(slots_);
        state.flags.resize(slots_);
        for (std::uint32_t r = 0; r < slots_ && error_.empty(); ++r) {
            if (!assigned_[r]) continue;
            state.slots[r] = {StateSlot::Kind::Value, read_start(slot_var(r), label)};
            state.flags[r] = {StateFlag::Kind::Value, read_start(flag_var(r), label)};
        }
        state.blocks = read_start(c_var(), label);
        if (f_->inline_kernels) state.kernel_steps = read_start(k_var(), label);
        const auto id = static_cast<std::uint32_t>(f_->states.size());
        f_->states.push_back(std::move(state));
        entry_states_[b] = id;
        return id;
    }

    // The state after the commit of image block `b`, with `blocks` completed.
    std::uint32_t commit_state(std::uint32_t b, std::uint32_t blocks) {
        FrameState state;
        state.function = region_.function;
        state.block = b;
        state.slots.resize(slots_);
        state.flags.resize(slots_);
        for (std::uint32_t r = 0; r < slots_ && error_.empty(); ++r) {
            if (!assigned_[r]) continue;
            state.slots[r] = {StateSlot::Kind::Value, read(slot_var(r), cur_)};
            state.flags[r] = {StateFlag::Kind::Value, read(flag_var(r), cur_)};
        }
        state.blocks = blocks;
        state.kernel_steps = kernel_steps();
        const auto id = static_cast<std::uint32_t>(f_->states.size());
        f_->states.push_back(std::move(state));
        return id;
    }

    ValueType slot_type_or(std::uint32_t slot, ValueType otherwise) const {
        return slot != none && slot < slots_ ? f_->slot_types[slot] : otherwise;
    }

    std::vector<std::uint32_t> values(std::uint32_t first, unsigned count) {
        std::vector<std::uint32_t> result;
        for (unsigned i = 0; i < count; ++i) result.push_back(value(region_.operands[first + i]));
        return result;
    }

    void set_extras(Inst& in, const std::vector<std::uint32_t>& list) {
        in.extra = static_cast<std::uint32_t>(f_->extras.size());
        in.count = static_cast<std::uint32_t>(list.size());
        f_->extras.insert(f_->extras.end(), list.begin(), list.end());
    }

    void terminate(Op op, std::uint32_t operand = none, std::uint64_t imm = 0) {
        Inst in;
        in.op = op;
        in.operands[0] = operand;
        in.imm = imm;
        emit(std::move(in));
    }

    // Fills IR block `b` from its ops.
    void fill(std::uint32_t b) {
        cur_ = b;
        image_block_ = f_->blocks[b].origin_block;
        const auto first = first_pc_[b - region_.entries.size()];
        bool terminated = false;
        for (std::uint32_t pc = first; pc < block_end_[b] && error_.empty(); ++pc) {
            terminated = translate(pc);
        }
        if (error_.empty() && !terminated) terminate(Op::Jump);
        filled(b);
    }

    // --- Heap access (the inline state operations) ------------------------------

    // The test of an entry of static type `type_index`: what typed_entry's
    // `compatible` accepts (machine.cpp), as a set of layouts or type indices.
    std::uint32_t type_test(std::uint32_t type_index) {
        const auto key = std::make_pair(std::uint32_t{0}, type_index);
        if (const auto found = tests_.find(key); found != tests_.end()) return found->second;
        TypeTest test;
        const auto& t = image_.types[type_index];
        switch (t.kind) {
        case nvr::TypeKind::Object: return class_test(t.reference, key);
        case nvr::TypeKind::Array:
        case nvr::TypeKind::List:
            test.kind = static_cast<std::uint8_t>(t.kind == nvr::TypeKind::Array ? EntryKind::Array : EntryKind::List);
            for (std::uint32_t other = 0; other < image_.types.size(); ++other) {
                if (types_.same(other, type_index)) test.types.push_back(other);
            }
            break;
        case nvr::TypeKind::Text:
        case nvr::TypeKind::Bytes:
            test.kind = static_cast<std::uint8_t>(t.kind == nvr::TypeKind::Text ? EntryKind::Text : EntryKind::Bytes);
            test.any_type = true;
            break;
        default:
            // No entry is compatible with a type that is not a reference.
            test.kind = 0xff;
            break;
        }
        return add_test(std::move(test), key);
    }

    // The objects whose layout derives from `layout` (TypeSystem::derives),
    // and the initialized-word count they share, if they all have one.
    std::uint32_t class_test(std::uint32_t layout, std::pair<std::uint32_t, std::uint32_t> key) {
        TypeTest test;
        test.kind = static_cast<std::uint8_t>(EntryKind::Object);
        std::uint32_t words = none;
        bool same = true;
        for (std::uint32_t l = 0; l < image_.layouts.size(); ++l) {
            if (!types_.derives(l, layout)) continue;
            test.types.push_back(l);
            const auto w = static_cast<std::uint32_t>((image_.layouts[l].fields.size() + 63) / 64);
            if (words == none) words = w;
            else if (w != words) same = false;
        }
        test.words = same ? words : none;
        return add_test(std::move(test), key);
    }

    std::uint32_t layout_test(std::uint32_t layout) {
        const auto key = std::make_pair(std::uint32_t{1}, layout);
        if (const auto found = tests_.find(key); found != tests_.end()) return found->second;
        return class_test(layout, key);
    }

    std::uint32_t add_test(TypeTest test, std::pair<std::uint32_t, std::uint32_t> key) {
        const auto id = static_cast<std::uint32_t>(f_->type_tests.size());
        f_->type_tests.push_back(std::move(test));
        tests_[key] = id;
        return id;
    }

    // A heap check (or, for Lookup, the entry it finds) on `a` and `b`.
    std::uint32_t heap_check(Op op, std::uint32_t a, std::uint32_t b, std::uint64_t imm, std::uint32_t state,
                             bool reference = false) {
        Inst in;
        in.op = op;
        in.operands[0] = a;
        in.operands[1] = b;
        in.imm = imm;
        in.flags = static_cast<std::uint16_t>(Checked | (reference ? ReferenceCheck : 0));
        in.state = state;
        if (op == Op::Lookup) in.type = {Type::Ptr, 0};
        return emit(std::move(in));
    }

    std::uint32_t heap_op(Op op, ValueType type, std::uint32_t a, std::uint32_t b = none, std::uint32_t c = none,
                          std::uint32_t d = none, std::uint64_t imm = 0, std::uint32_t aux = none) {
        Inst in;
        in.op = op;
        in.type = type;
        in.operands[0] = a;
        in.operands[1] = b;
        in.operands[2] = c;
        in.operands[3] = d;
        in.imm = imm;
        in.aux = aux;
        if (op == Op::StoreField || op == Op::StoreElement || op == Op::StoreGlobal || op == Op::Widen) in.flags = Effect;
        return emit(std::move(in));
    }

    // typed_entry (machine.cpp): nil is a trap of its own for an object
    // type, then the handle must name a live entry of the type.
    std::uint32_t typed_entry(std::uint32_t handle, std::uint32_t type_index, std::uint32_t state) {
        if (image_.types[type_index].kind == nvr::TypeKind::Object && image_.minor_version >= 2) {
            heap_check(Op::CheckNotNil, handle, none, 0, state);
        }
        const auto entry = heap_check(Op::Lookup, handle, none, 0, state);
        heap_check(Op::CheckKind, entry, none, type_test(type_index), state);
        return entry;
    }

    // The declared element width, in bytes, of a collection of type
    // `type_index` (one for text and bytes).
    unsigned declared_bytes(std::uint32_t type_index) const {
        const auto& t = image_.types[type_index];
        if (t.kind != nvr::TypeKind::Array && t.kind != nvr::TypeKind::List) return 1;
        return static_cast<unsigned>(element_width(image_.types[t.reference]));
    }

    // Whether every value of `v`'s type fits `bytes` bytes.
    bool fits(std::uint32_t v, unsigned bytes) const {
        const auto& type = f_->insts[v].type;
        const unsigned bits = type.type == Type::Int ? type.width : 64U;
        return bytes >= 8 || bits <= 8 * bytes;
    }

    // The inline form of a state operation (docs/runtime/NVM.md,
    // "Execution tiers"): state()'s reads and writes on the published layout,
    // its traps as checks that deoptimize to the block's entry. False for an
    // operation that stays a StateCall.
    bool inline_heap_operation(const tier2::Op& op, const nvr::Exit& exit) {
        using nvr::StateOp;
        // The profile and checkable variants write through the reference.
        const bool checkable = variant_ != Variant::Standard;
        switch (exit.operation) {
        case StateOp::ReadTag:
        case StateOp::ReadField:
        case StateOp::ReadGlobal:
        case StateOp::ReadLength:
        case StateOp::ReadElement:
        case StateOp::ReadVersion:
        case StateOp::ClassIs:
        case StateOp::ClassCast: break;
        case StateOp::WriteField:
        case StateOp::WriteGlobal:
        case StateOp::WriteElement:
            if (checkable) return false;
            break;
        case StateOp::ScalarText:
            if (exit.arguments.size() < 2) return false;
            break;
        default: return false;
        }
        const auto args = values(op.other, op.width);
        const auto& types = exit.arguments;
        const auto state = entry_state(op.block);
        const auto result_type = slot_type_or(exit.result_slot, integer(64));
        std::uint32_t result = none;
        switch (exit.operation) {
        case StateOp::ReadTag: result = heap_op(Op::LoadTag, result_type, typed_entry(args[0], types[0].type, state)); break;
        case StateOp::ReadField: {
            const auto entry = typed_entry(args[0], types[0].type, state);
            heap_check(Op::CheckInit, entry, none, exit.immediate, state);
            result = heap_op(Op::LoadField, result_type, entry, none, none, none, exit.immediate,
                             f_->type_tests[type_test(types[0].type)].words);
            break;
        }
        case StateOp::WriteField: {
            const auto entry = typed_entry(args[0], types[0].type, state);
            heap_op(Op::StoreField, {}, entry, args[1], none, none, exit.immediate, f_->type_tests[type_test(types[0].type)].words);
            break;
        }
        case StateOp::ReadGlobal:
            heap_check(Op::CheckGlobalInit, none, none, exit.immediate, state);
            result = heap_op(Op::LoadGlobal, result_type, none, none, none, none, exit.immediate);
            break;
        case StateOp::WriteGlobal: heap_op(Op::StoreGlobal, {}, args[0], none, none, none, exit.immediate); break;
        case StateOp::ReadLength:
            result = heap_op(Op::LoadLength, result_type, typed_entry(args[0], types[0].type, state));
            break;
        case StateOp::ReadElement:
        case StateOp::WriteElement: {
            const auto entry = typed_entry(args[0], types[0].type, state);
            const auto length = heap_op(Op::LoadLength, integer(64), entry);
            heap_check(Op::CheckIndex, args[1], length, 0, state);
            const auto declared = declared_bytes(types[0].type);
            if (exit.operation == StateOp::WriteElement && !fits(args[2], declared)) {
                // A value that may not fit the declared width: the reference
                // store, which widens the storage when it does not.
                heap_op(Op::Widen, {}, args[0], args[1], args[2]);
                break;
            }
            const auto data = heap_op(Op::LoadData, {Type::Ptr, 0}, entry);
            const auto storage = heap_op(Op::LoadStorage, integer(8), entry);
            if (exit.operation == StateOp::ReadElement) {
                result = heap_op(Op::LoadElement, result_type, data, args[1], storage, none, declared);
            } else {
                heap_op(Op::StoreElement, {}, data, args[1], storage, args[2], declared);
            }
            break;
        }
        case StateOp::ReadVersion: {
            const auto entry = typed_entry(args[0], types[0].type, state);
            result = image_.types[types[0].type].kind == nvr::TypeKind::List ? heap_op(Op::LoadVersion, result_type, entry)
                                                                             : constant(0, result_type);
            break;
        }
        case StateOp::ClassIs:
        case StateOp::ClassCast: {
            // Nil gives false, or itself, before any check.
            const auto entry = heap_check(Op::Lookup, args[0], none, 1, state);
            heap_check(Op::CheckKind, entry, none, type_test(types[0].type), state);
            const auto test = layout_test(static_cast<std::uint32_t>(exit.immediate));
            if (exit.operation == StateOp::ClassIs) {
                result = heap_op(Op::TestKind, result_type, entry, none, none, none, test);
            } else {
                heap_check(Op::CheckKind, entry, none, test, state);
                result = args[0];
            }
            break;
        }
        case StateOp::ScalarText: {
            const auto names = static_cast<std::uint32_t>(args.size() - 1);
            heap_check(Op::CheckIndex, args[0], constant(names, integer(bits_of(names))), 0, state);
            Inst in;
            in.op = Op::Select;
            in.type = result_type;
            in.operands[0] = args[0];
            set_extras(in, std::vector<std::uint32_t>(args.begin() + 1, args.end()));
            result = emit(std::move(in));
            break;
        }
        default: return false;
        }
        // An operation without a result gives 0, as state() returns it.
        if (op.dst != none) write(op.dst, result != none ? result : constant(0, result_type), false);
        return true;
    }

    // An allocation of an object, or an append, as an operation with an
    // inline fast path whose slow path is the helper (design 6.7, 6.8): the
    // block's step, then the op, which writes the frame back only on its slow
    // path. An append checks its list first, as the reference's typed_entry
    // does: a failure deoptimizes to the block's entry, and tier 1 traps
    // there. False for any other helper.
    bool fast_path(const tier2::Op& op, const nvr::Exit& exit, const std::vector<std::uint32_t>& arguments) {
        if (variant_ != Variant::Standard) return false;
        const bool allocates = exit.operation == nvr::StateOp::AllocateObject && arguments.empty() &&
                               exit.immediate < image_.layouts.size();
        const bool appends = exit.operation == nvr::StateOp::AppendElement && arguments.size() == 2 &&
                             exit.arguments.size() == 2 && image_.types[exit.arguments[0].type].kind == nvr::TypeKind::List;
        if (!allocates && !appends) return false;
        std::uint32_t entry = none;
        if (appends) entry = typed_entry(arguments[0], exit.arguments[0].type, entry_state(op.block));
        const auto charged = counter_add(read(c_var(), cur_), 1);
        def_[cur_][c_var()] = charged;
        Inst in;
        in.op = allocates ? Op::AllocObject : Op::Append;
        in.operands[0] = charged;
        if (appends) {
            in.operands[1] = entry;
            in.operands[2] = arguments[1];
        }
        in.aux = op.index;
        in.imm = allocates ? image_.layouts[exit.immediate].fields.size() : declared_bytes(exit.arguments[0].type);
        in.flags = Effect;
        in.state = commit_state(op.block, charged);
        in.type = allocates ? slot_type_or(exit.result_slot, ValueType{Type::Ref, 64}) : ValueType{};
        set_extras(in, arguments);
        const auto id = emit(std::move(in));
        if (op.dst != none) write(op.dst, allocates ? id : constant(0, slot_type_or(exit.result_slot, integer(64))), true);
        terminate(Op::Jump);
        return true;
    }

    // Translates the op at `pc`; true when it ended the block.
    bool translate(std::uint32_t pc) {
        const auto& op = region_.code[pc];
        switch (op.code) {
        case Code::Budget: {
            Inst in;
            in.op = Op::Budget;
            in.operands[0] = read(c_var(), cur_);
            in.operands[1] = kernel_steps();
            in.flags = Checked;
            in.state = entry_state(op.block);
            emit(std::move(in));
            return false;
        }
        case Code::Lift: {
            const auto& lifted = region_.instructions[op.index];
            Inst in;
            in.op = Op::Lift;
            in.lifted = lifted;
            const auto n = arity(lifted);
            if (n >= 1) in.operands[0] = value(op.a);
            if (n >= 2) in.operands[1] = value(op.b);
            in.type = result_type(lifted, n >= 1 ? width_of(in.operands[0]) : 0, n >= 2 ? width_of(in.operands[1]) : 0);
            if (can_fail(lifted)) {
                in.flags = Checked;
                in.state = entry_state(op.block);
            }
            const auto id = emit(std::move(in));
            write(op.dst, id, (op.flags & tier2::defines) != 0);
            return false;
        }
        case Code::Mask: {
            Inst in;
            in.op = Op::Mask;
            in.operands[0] = value(op.a);
            in.imm = op.width;
            in.type = integer(op.width);
            write(op.dst, emit(std::move(in)), false);
            return false;
        }
        case Code::Move: write(op.dst, value(op.a), false); return false;
        case Code::Gather: {
            std::vector<std::uint32_t> list;
            const auto* bits = region_.gathers.data() + op.index;
            for (unsigned i = 0; i < op.width; ++i) {
                list.push_back(value(bits[i].reg));
                list.push_back(bits[i].bit);
            }
            Inst in;
            in.op = Op::Gather;
            in.imm = op.width;
            in.type = integer(op.width);
            set_extras(in, list);
            write(op.dst, emit(std::move(in)), false);
            return false;
        }
        case Code::Store: write(op.dst, value(op.a), true); return false;
        case Code::Branch: {
            auto condition = value(op.a);
            if (!(f_->insts[condition].type.type == Type::Int && f_->insts[condition].type.width == 1)) {
                Inst in;
                in.op = Op::Mask;
                in.operands[0] = condition;
                in.imm = 1;
                in.type = integer(1);
                condition = emit(std::move(in));
            }
            terminate(Op::Branch, condition);
            return true;
        }
        case Code::Switch: {
            const auto index = value(op.a);
            Inst check;
            check.op = Op::SwitchCheck;
            check.operands[0] = index;
            check.imm = op.width;
            check.flags = Checked;
            check.state = entry_state(op.block);
            emit(std::move(check));
            terminate(Op::Switch, index, op.width);
            return true;
        }
        case Code::Dispatch: terminate(Op::Dispatch, value(op.a), op.index); return true;
        case Code::Jump:
            if (op.flags & tier2::completes) complete_block();
            terminate(Op::Jump);
            return true;
        case Code::CheckRef: {
            // check_reference: nil passes for an object type (images 0.2 and
            // later), anything else must be a live entry of the type.
            const auto& type = image_.types[op.index];
            const bool nil = type.kind == nvr::TypeKind::Object && image_.minor_version >= 2;
            const auto state = entry_state(op.block);
            const auto entry = heap_check(Op::Lookup, value(op.a), none, nil ? 1 : 0, state, true);
            heap_check(Op::CheckKind, entry, none, type_test(op.index), state, true);
            return false;
        }
        case Code::State: {
            const auto& exit = *region_.exits[op.index];
            if (inline_heap_operation(op, exit)) return false;
            Inst in;
            in.op = Op::StateCall;
            in.aux = op.index;
            set_extras(in, values(op.other, op.width));
            in.flags = Checked | Effect;
            in.state = entry_state(op.block);
            in.type = op.dst == none ? ValueType{} : slot_type_or(exit.result_slot, integer(64));
            const auto id = emit(std::move(in));
            if (op.dst != none) write(op.dst, id, false);
            return false;
        }
        case Code::Poll: return false;
        case Code::Complete: complete_block(); return false;
        case Code::Leave:
            if (op.flags & tier2::completes) complete_block();
            {
                // The frame is written before the activation ends outside
                // native code: the state after the block's commit, at the
                // block the run goes on at.
                const auto count = read(c_var(), cur_);
                Inst back;
                back.op = Op::WriteBack;
                back.flags = Effect;
                back.state = commit_state(op.target, count);
                emit(std::move(back));
                Inst leave;
                leave.op = Op::Leave;
                leave.operands[0] = count;
                leave.operands[1] = kernel_steps();
                leave.imm = op.target;
                emit(std::move(leave));
            }
            return true;
        case Code::Deopt: {
            Inst in;
            in.op = Op::Deopt;
            in.operands[0] = read(c_var(), cur_);
            in.flags = Effect;
            in.state = entry_state(op.block);
            emit(std::move(in));
            return true;
        }
        case Code::Call:
        case Code::Helper: {
            const bool call = op.code == Code::Call;
            const auto& exit = *region_.exits[op.index];
            const auto arguments = values(op.other, op.width);
            if (!call && inline_sqrt(image_, exit) && arguments.size() == 1) {
                // The block's step, the kernel's charge of one step, and the
                // square root: nothing can fail (the Budget before covers the
                // charge), so there is no write-back and no frame state.
                def_[cur_][c_var()] = counter_add(read(c_var(), cur_), 1);
                def_[cur_][k_var()] = counter_add(read(k_var(), cur_), 1);
                Inst root;
                root.op = Op::Sqrt;
                root.type = {Type::F64, 64};
                root.operands[0] = arguments[0];
                const auto id = emit(std::move(root));
                if (op.dst != none) write(op.dst, id, true);
                terminate(Op::Jump);
                return true;
            }
            if (!call && fast_path(op, exit, arguments)) return true;
            // The block's step, the commit, and the frame the reference sees.
            const auto charged = counter_add(read(c_var(), cur_), 1);
            def_[cur_][c_var()] = charged;
            const auto state = commit_state(op.block, charged);
            Inst back;
            back.op = Op::WriteBack;
            back.flags = Effect;
            back.state = state;
            emit(std::move(back));
            Inst in;
            in.op = call ? Op::Call : Op::Helper;
            in.operands[0] = charged;
            in.operands[1] = kernel_steps();
            in.aux = op.index;
            in.imm = call ? op.a : 0;
            in.flags = Effect;
            in.state = state;
            in.type = slot_type_or(exit.result_slot, integer(64));
            set_extras(in, arguments);
            if (call) {
                const auto& callee = image_.functions.at(op.a);
                f_->call_frames[op.a] = static_cast<std::uint32_t>(callee.frame_types.size());
                std::vector<std::uint8_t> widths;
                for (std::uint32_t slot = 0; slot < callee.parameter_count && slot < callee.frame_types.size(); ++slot) {
                    widths.push_back(static_cast<std::uint8_t>(std::min(nvr::lane_count(image_.types[callee.frame_types[slot]]), 64U)));
                }
                f_->call_widths[op.a] = std::move(widths);
            }
            const auto id = emit(std::move(in));
            if (op.dst != none) write(op.dst, id, true);
            if (call) {
                // The callee's counts continue ours: the context holds them.
                Inst reload;
                reload.op = Op::EntryCounter;
                reload.type = integer(64);
                reload.imm = 0;
                def_[cur_][c_var()] = emit(std::move(reload));
                if (f_->inline_kernels) {
                    // And K: the callee's inline kernels may have charged.
                    Inst kernel;
                    kernel.op = Op::EntryCounter;
                    kernel.type = integer(64);
                    kernel.imm = 1;
                    def_[cur_][k_var()] = emit(std::move(kernel));
                }
            }
            terminate(Op::Jump);
            return true;
        }
        case Code::Return: {
            Inst in;
            in.op = Op::Return;
            in.operands[0] = read(c_var(), cur_);
            if (op.a != none) in.operands[1] = value(op.a);
            in.operands[2] = kernel_steps();
            in.flags = Checked | Effect;
            in.state = entry_state(op.block);
            emit(std::move(in));
            return true;
        }
        }
        return false;
    }

    // --- Finishing ---------------------------------------------------------------

    void finish() {
        const auto entry_count = static_cast<std::uint32_t>(region_.entries.size());
        for (std::uint32_t e = 0; e < entry_count; ++e) {
            auto& block = f_->blocks[e];
            block.insts = entry_insts_[e];
            Inst in;
            in.op = Op::Jump;
            in.origin_function = region_.function;
            in.origin_block = region_.entries[e].block;
            block.origin_block = region_.entries[e].block;
            block.origin_function = region_.function;
            const auto id = make(std::move(in));
            block.insts.push_back(id);
        }
        // Phis come first in their blocks.
        for (std::uint32_t b = entry_count; b < f_->blocks.size(); ++b) {
            auto& insts = f_->blocks[b].insts;
            insts.insert(insts.begin(), phis_[b].begin(), phis_[b].end());
        }
        remove_trivial_phis();
        normalize_states();
        remove_dead();
        compact();
        compute_budgets();
    }

    void remove_trivial_phis() {
        // Users of each phi among the phis, for the worklist.
        std::vector<std::vector<std::uint32_t>> users(f_->insts.size());
        for (const auto phi : phi_vars_) {
            const auto& in = f_->insts[phi];
            for (std::uint32_t i = 0; i < in.count; ++i) {
                const auto operand = f_->extras[in.extra + i];
                if (operand != none) users[operand].push_back(phi);
            }
        }
        std::vector<std::uint32_t> work(phi_vars_.rbegin(), phi_vars_.rend());
        while (!work.empty()) {
            const auto phi = work.back();
            work.pop_back();
            if (find(phi) != phi) continue;
            const auto& in = f_->insts[phi];
            std::uint32_t same = none;
            bool trivial = true;
            for (std::uint32_t i = 0; i < in.count && trivial; ++i) {
                const auto operand = find(f_->extras[in.extra + i]);
                if (operand == phi || operand == same) continue;
                if (same != none) trivial = false;
                else same = operand;
            }
            if (!trivial) continue;
            if (same == none) {
                // A phi of itself only: unreachable code reads it.
                error_ = "a variable has no definition";
                return;
            }
            repl_[phi] = same;
            for (const auto user : users[phi]) {
                if (find(user) == user) work.push_back(user);
            }
            if (same < users.size()) {
                for (const auto user : users[phi]) users[same].push_back(user);
            }
        }
    }

    // For each value, the slot whose entry value it is when it is one: a load
    // of the slot at some entry, or a phi of only such loads of the same slot.
    // `load` is EntryLoad (slot values) or EntryFlag (flags). A phi that joins
    // loads of two entries is the slot's entry value too (the frame holds it
    // only while nothing has written the slot: normalize_states).
    std::vector<std::uint32_t> entry_slots(Op load) {
        constexpr std::uint32_t unset = none - 1;
        std::vector<std::uint32_t> slot_of(f_->insts.size(), none);
        std::vector<std::uint32_t> phis;
        for (std::uint32_t id = 0; id < f_->insts.size(); ++id) {
            if (find(id) != id) continue;
            const auto& in = f_->insts[id];
            if (in.op == load) slot_of[id] = static_cast<std::uint32_t>(in.imm);
            else if (in.op == Op::Phi) {
                slot_of[id] = unset;
                phis.push_back(id);
            }
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto phi : phis) {
                const auto& in = f_->insts[phi];
                auto meet = unset;
                for (std::uint32_t i = 0; i < in.count && meet != none; ++i) {
                    const auto operand = find(f_->extras[in.extra + i]);
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

    // Resolves replaced values everywhere, then turns each state entry into
    // its kind: the frame's own value (Unchanged), a constant, or a value.
    void normalize_states() {
        const auto frame_values = entry_slots(Op::EntryLoad);
        const auto frame_flags = entry_slots(Op::EntryFlag);
        for (auto& in : f_->insts) {
            for (auto& operand : in.operands) {
                if (operand != none) operand = find(operand);
            }
            for (std::uint32_t i = 0; i < in.count; ++i) {
                // Gather's odd entries are bit numbers, not values.
                if (in.op == Op::Gather && (i % 2) != 0) continue;
                auto& extra = f_->extras[in.extra + i];
                if (extra != none) extra = find(extra);
            }
        }
        // Each assigned entry as a value, or Unchanged when the value is the
        // slot's value at the entry, which the frame holds only while nothing
        // wrote the slot since: a write-back or a call's result can have
        // changed it, and then the slot is assigned its entry value again.
        const auto count = static_cast<std::uint32_t>(slots_);
        std::vector<std::vector<std::uint32_t>> slot_values(f_->states.size());
        std::vector<std::vector<std::uint32_t>> flag_values(f_->states.size());
        for (std::uint32_t s = 0; s < f_->states.size(); ++s) {
            auto& state = f_->states[s];
            if (state.blocks != none) state.blocks = find(state.blocks);
            slot_values[s].assign(count, none);
            flag_values[s].assign(count, none);
            for (std::uint32_t r = 0; r < count; ++r) {
                if (state.slots[r].kind != StateSlot::Kind::Value) continue;
                slot_values[s][r] = find(static_cast<std::uint32_t>(state.slots[r].value));
                flag_values[s][r] = find(state.flags[r].value);
            }
        }
        const auto settle = [&](std::uint32_t s, std::uint32_t r, bool slot_unchanged, bool flag_unchanged) {
            auto& state = f_->states[s];
            const auto v = slot_values[s][r];
            const auto& in = f_->insts[v];
            if (slot_unchanged) state.slots[r] = {};
            else if (in.op == Op::Const) state.slots[r] = {StateSlot::Kind::Const, in.imm};
            else state.slots[r] = {StateSlot::Kind::Value, v};
            const auto fv = flag_values[s][r];
            const auto& fin = f_->insts[fv];
            if (flag_unchanged) state.flags[r] = {};
            else if (fin.op == Op::Const && (fin.imm & 1) != 0) state.flags[r] = {StateFlag::Kind::One, 0};
            else state.flags[r] = {StateFlag::Kind::Value, fv};
        };
        for (std::uint32_t s = 0; s < f_->states.size(); ++s) {
            for (std::uint32_t r = 0; r < count; ++r) {
                if (slot_values[s][r] == none) continue;
                settle(s, r, frame_values[slot_values[s][r]] == r, frame_flags[flag_values[s][r]] == r);
            }
        }
        // Demote the Unchanged entries of states reached after a write of the
        // slot (or its flag); a demotion is itself a write at the next
        // write-back, so go on until nothing changes.
        const auto width = 2 * static_cast<std::size_t>(count);
        for (bool changed = true; changed;) {
            changed = false;
            const auto before = may_written(width);
            std::vector<std::vector<std::uint8_t>> point(f_->states.size(), std::vector<std::uint8_t>(width, 0));
            for (std::uint32_t b = 0; b < f_->blocks.size(); ++b) {
                auto current = before[b];
                for (const auto id : f_->blocks[b].insts) {
                    const auto& in = f_->insts[id];
                    if (find(id) != id) continue;
                    if (in.state != none) {
                        for (std::size_t i = 0; i < width; ++i) point[in.state][i] |= current[i];
                    }
                    apply_writes(in, current);
                }
            }
            for (std::uint32_t s = 0; s < f_->states.size(); ++s) {
                auto& state = f_->states[s];
                for (std::uint32_t r = 0; r < count; ++r) {
                    if (slot_values[s][r] == none) continue;
                    const bool slot_unchanged = state.slots[r].kind == StateSlot::Kind::Unchanged && !point[s][r];
                    const bool flag_unchanged = state.flags[r].kind == StateFlag::Kind::Unchanged && !point[s][count + r];
                    if (slot_unchanged != (state.slots[r].kind == StateSlot::Kind::Unchanged) ||
                        flag_unchanged != (state.flags[r].kind == StateFlag::Kind::Unchanged)) {
                        settle(s, r, slot_unchanged, flag_unchanged);
                        changed = true;
                    }
                }
            }
        }
    }

    // What an op writes into the frame: the slots and flags (the second half
    // of `written`) a write-back stores, and a call's result slot.
    void apply_writes(const Inst& in, std::vector<std::uint8_t>& written) const {
        const auto count = static_cast<std::size_t>(slots_);
        if (in.op == Op::WriteBack || in.op == Op::AllocObject || in.op == Op::Append) {
            const auto& state = f_->states[in.state];
            for (std::size_t r = 0; r < count; ++r) {
                if (state.slots[r].kind != StateSlot::Kind::Unchanged) written[r] = 1;
                if (state.flags[r].kind != StateFlag::Kind::Unchanged) written[count + r] = 1;
            }
        } else if (in.op == Op::Call) {
            const auto slot = f_->exits[in.aux]->result_slot;
            if (slot != none && slot < count) written[slot] = written[count + slot] = 1;
        }
    }

    // The slots and flags that may have been written into the frame at the
    // start of each block: a forward union over the control-flow graph, the
    // entries' blocks starting with nothing.
    std::vector<std::vector<std::uint8_t>> may_written(std::size_t width) const {
        const auto n = f_->blocks.size();
        std::vector<std::vector<std::uint8_t>> in(n, std::vector<std::uint8_t>(width, 0));
        for (bool changed = true; changed;) {
            changed = false;
            for (std::uint32_t b = 0; b < n; ++b) {
                auto current = in[b];
                for (const auto id : f_->blocks[b].insts) apply_writes(f_->insts[id], current);
                for (const auto next : f_->blocks[b].succs) {
                    for (std::size_t i = 0; i < width; ++i) {
                        if (current[i] && !in[next][i]) {
                            in[next][i] = 1;
                            changed = true;
                        }
                    }
                }
            }
        }
        return in;
    }

    // Pure values nothing uses.
    void remove_dead() {
        const auto n = f_->insts.size();
        live_.assign(n, 0);
        std::vector<std::uint32_t> work;
        const auto mark = [&](std::uint32_t v) {
            if (v != none && !live_[v]) {
                live_[v] = 1;
                work.push_back(v);
            }
        };
        const auto pure = [&](const Inst& in) {
            switch (in.op) {
            case Op::Const:
            case Op::RunConst:
            case Op::EntryLoad:
            case Op::EntryFlag:
            case Op::EntryCounter:
            case Op::Phi:
            case Op::Mask:
            case Op::Gather:
            case Op::CounterAdd:
            case Op::Sqrt: return true;
            case Op::Lift: return (in.flags & Checked) == 0;
            default: return false;
            }
        };
        std::vector<std::uint8_t> state_used(f_->states.size(), 0);
        for (std::uint32_t b = 0; b < f_->blocks.size(); ++b) {
            for (const auto id : f_->blocks[b].insts) {
                if (!pure(f_->insts[id])) mark(id);
            }
        }
        const auto mark_state = [&](std::uint32_t s) {
            while (s != none && !state_used[s]) {
                state_used[s] = 1;
                const auto& state = f_->states[s];
                mark(state.blocks);
                mark(state.kernel_steps);
                for (const auto& slot : state.slots) {
                    if (slot.kind == StateSlot::Kind::Value) mark(static_cast<std::uint32_t>(slot.value));
                }
                for (const auto& flag : state.flags) {
                    if (flag.kind == StateFlag::Kind::Value) mark(flag.value);
                }
                s = state.caller;
            }
        };
        for (std::uint32_t id = 0; id < n; ++id) {
            if (live_[id] && f_->insts[id].state != none) mark_state(f_->insts[id].state);
        }
        while (!work.empty()) {
            const auto v = work.back();
            work.pop_back();
            const auto& in = f_->insts[v];
            for (const auto operand : in.operands) mark(operand);
            if (in.op == Op::Phi || in.op == Op::StateCall || in.op == Op::Helper || in.op == Op::Call ||
                in.op == Op::Select || in.op == Op::AllocObject || in.op == Op::Append) {
                for (std::uint32_t i = 0; i < in.count; ++i) mark(f_->extras[in.extra + i]);
            } else if (in.op == Op::Gather) {
                for (std::uint32_t i = 0; i < in.count; i += 2) mark(f_->extras[in.extra + i]);
            }
            if (in.state != none) mark_state(in.state);
        }
        used_states_ = std::move(state_used);
    }

    // Renumbers the surviving instructions and states densely.
    void compact() {
        const auto n = f_->insts.size();
        std::vector<std::uint32_t> renumber(n, none);
        std::vector<Inst> insts;
        for (std::uint32_t id = 0; id < n; ++id) {
            if (!live_[id] || find(id) != id) continue;
            renumber[id] = static_cast<std::uint32_t>(insts.size());
            insts.push_back(std::move(f_->insts[id]));
        }
        std::vector<std::uint32_t> state_number(f_->states.size(), none);
        std::vector<FrameState> states;
        for (std::uint32_t s = 0; s < f_->states.size(); ++s) {
            if (!used_states_[s]) continue;
            state_number[s] = static_cast<std::uint32_t>(states.size());
            states.push_back(std::move(f_->states[s]));
        }
        const auto map = [&](std::uint32_t v) { return v == none ? none : renumber[v]; };
        std::vector<std::uint32_t> extras;
        for (auto& in : insts) {
            for (auto& operand : in.operands) operand = map(operand);
            if (in.state != none) in.state = state_number[in.state];
            if (in.count != 0) {
                const auto old = in.extra;
                in.extra = static_cast<std::uint32_t>(extras.size());
                for (std::uint32_t i = 0; i < in.count; ++i) {
                    auto v = f_->extras[old + i];
                    // Gather's odd entries are bit numbers.
                    if (in.op != Op::Gather || (i % 2) == 0) v = map(v);
                    extras.push_back(v);
                }
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
        for (auto& block : f_->blocks) {
            std::vector<std::uint32_t> kept;
            for (const auto id : block.insts) {
                if (renumber[id] != none) kept.push_back(renumber[id]);
            }
            block.insts = std::move(kept);
        }
        f_->insts = std::move(insts);
        f_->states = std::move(states);
        f_->extras = std::move(extras);
        if (f_->insts.size() > max_instructions) error_ = "the function is too large";
    }

    // P of each Budget: the longest run of completed blocks from it to the
    // next Budget, or to the end of the activation. Back edges end at a
    // Budget, so every cycle without one is an error.
    void compute_budgets() {
        const auto n = f_->blocks.size();
        std::vector<std::uint64_t> weight(n, 0);
        std::vector<std::uint8_t> starts(n, 0);
        for (std::uint32_t b = 0; b < n; ++b) {
            for (const auto id : f_->blocks[b].insts) {
                const auto& in = f_->insts[id];
                if (in.op == Op::CounterAdd) weight[b] += in.imm;
                if (in.op == Op::Return) weight[b] += 1;
            }
            for (const auto id : f_->blocks[b].insts) {
                if (f_->insts[id].op == Op::Phi) continue;
                starts[b] = f_->insts[id].op == Op::Budget;
                break;
            }
        }
        std::vector<std::uint64_t> longest(n, 0);
        std::vector<std::uint8_t> state(n, 0);
        // Iterative depth-first search.
        for (std::uint32_t root = 0; root < n && error_.empty(); ++root) {
            if (state[root] != 0) continue;
            std::vector<std::pair<std::uint32_t, std::size_t>> stack{{root, 0}};
            state[root] = 1;
            while (!stack.empty() && error_.empty()) {
                auto& [b, next] = stack.back();
                const auto& succs = f_->blocks[b].succs;
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
                if (state[s] == 1) {
                    error_ = "a loop without a checkpoint";
                } else if (state[s] == 0) {
                    state[s] = 1;
                    stack.emplace_back(s, 0);
                }
            }
        }
        if (!error_.empty()) return;
        for (std::uint32_t b = 0; b < n; ++b) {
            for (const auto id : f_->blocks[b].insts) {
                if (f_->insts[id].op == Op::Budget) f_->insts[id].imm = longest[b];
            }
        }
    }

    const nvr::Image& image_;
    const tier2::Region& region_;
    const Variant variant_;
    nvr::TypeSystem types_{image_};
    Function* f_ = nullptr;
    std::string error_;
    std::uint32_t slots_ = 0;
    std::uint32_t cur_ = 0;
    std::uint32_t image_block_ = 0;
    std::uint32_t seq_ = 0;
    std::map<std::uint32_t, std::uint64_t> immediates_;
    std::map<std::uint32_t, tier2::Region::Constant> constants_;
    std::vector<std::uint8_t> assigned_;
    std::vector<std::uint32_t> pc_block_;
    std::vector<std::uint32_t> first_pc_;
    std::vector<std::uint32_t> block_end_;
    std::map<std::uint32_t, std::uint32_t> label_of_;
    std::map<std::uint32_t, std::uint32_t> entry_states_;
    std::map<std::uint32_t, std::uint32_t> ones_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> tests_;
    std::vector<std::map<std::uint32_t, std::uint32_t>> def_;
    std::vector<std::map<std::uint32_t, std::uint32_t>> start_;
    std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> incomplete_;
    std::vector<std::vector<std::uint32_t>> phis_;
    std::vector<std::vector<std::uint32_t>> entry_insts_;
    std::vector<std::uint8_t> sealed_;
    std::vector<std::uint32_t> filled_edges_;
    std::vector<std::uint32_t> phi_vars_;
    std::vector<std::uint32_t> repl_;
    std::vector<std::uint8_t> live_;
    std::vector<std::uint8_t> used_states_;
};

} // namespace

BuildResult build(const nvr::Image& image, const tier2::Region& program, bool check, Variant variant) {
    return Builder(image, program, variant).run(check);
}

namespace {

// The address of a lazy entry whose code could not be made.
const std::uint8_t* const lazy_failed = reinterpret_cast<const std::uint8_t*>(std::uintptr_t{1});

} // namespace

const std::uint8_t* Program::entry(std::uint32_t index, std::uint64_t& bytes, std::string& failure) const {
    bytes = 0;
    if (index >= entry_code.size() || !machine) return nullptr;
    if (entry_code[index] != jit::native::lazy) return machine->address() + entry_code[index];
    auto& slot = lazy_entries[index];
    if (const auto* found = slot.load(std::memory_order_acquire)) return found == lazy_failed ? nullptr : found;
    const std::lock_guard lock(lazy_mutex);
    if (const auto* found = slot.load(std::memory_order_acquire)) return found == lazy_failed ? nullptr : found;
    auto generated = jit::native::generate_entry(ir, index, verify);
    std::unique_ptr<jit::Program> code;
    if (generated.failure.empty()) {
        code = jit::Program::load(generated.code.bytes, generated.failure);
        if (!code) generated.failure = "no executable memory: " + generated.failure;
    }
    if (!code) {
        failure = generated.failure;
        slot.store(lazy_failed, std::memory_order_release);
        return nullptr;
    }
    const auto* address = code->address() + generated.code.entries.at(index);
    bytes = generated.code.bytes.size();
    lazy_code.push_back(std::move(code));
    slot.store(address, std::memory_order_release);
    return address;
}

void Program::enter(Context& context, const std::uint8_t* address) const {
    using Trampoline = void (*)(Context*, const void*);
    const auto* base = machine->address();
    const auto trampoline_code = reinterpret_cast<Trampoline>(const_cast<std::uint8_t*>(base + trampoline));
    trampoline_code(&context, address);
}

bool verify_enabled() {
    static const bool enabled = [] {
#ifndef NDEBUG
        return true;
#else
        const char* text = std::getenv("NEURA_TIER3_VERIFY");
        return text && std::strcmp(text, "1") == 0;
#endif
    }();
    return enabled;
}

} // namespace neura::nvm::tier3

namespace neura::nvm {
namespace {

// The states of a program's slot in TierCache::native_programs_ besides the
// published program: being compiled by some thread, and failed.
const tier3::Program* const native_forming = reinterpret_cast<const tier3::Program*>(std::uintptr_t{1});
const tier3::Program* const native_failed = reinterpret_cast<const tier3::Program*>(std::uintptr_t{2});

} // namespace

std::uint64_t TierCache::count_native(std::uint32_t program_id) {
    if (program_id >= native_capacity_) return 0;
    return native_arrivals_[program_id].fetch_add(1, std::memory_order_relaxed) + 1;
}

bool TierCache::native_ready(const tier2::Region& program, std::uint64_t arrivals) {
    const std::uint64_t after = options_.native_after;
    const std::uint64_t period = std::max<std::uint64_t>(1, after / 8);
    if (arrivals < after || (arrivals - after) % period != 0) return false;
    if (program.program_id >= native_capacity_) return true;
    const std::lock_guard lock(mutex_);
    std::uint64_t activity = 0;
    std::uint32_t last = std::numeric_limits<std::uint32_t>::max();
    bool pending = false;
    for (const auto& op : program.code) {
        if (op.code != tier2::Code::Call || op.a >= programs_.size() || op.a == last) continue;
        last = op.a;
        if (programs_[op.a].state == RegionEntry::State::Settled) continue;
        pending = true;
        const auto first = first_entry_[op.a];
        const auto blocks = image_.functions[op.a].blocks.size();
        for (std::size_t b = 0; b < blocks; ++b) {
            activity += counts_[first + b].load(std::memory_order_relaxed) +
                        region_counts_[first + b].load(std::memory_order_relaxed);
        }
    }
    if (!pending) return true;
    if (native_waiting_.size() != native_capacity_) native_waiting_.assign(native_capacity_, {});
    auto& waiting = native_waiting_[program.program_id];
    if (activity != waiting.activity) waiting = {activity, 0};
    else ++waiting.idle;
    return arrivals >= after * 16 || waiting.idle >= 2;
}

const tier3::Program* TierCache::native_code(const tier2::Region& program, tier3::Variant variant) const {
    if (program.program_id >= native_capacity_) return nullptr;
    const auto* found =
        native_programs_[program.program_id * tier3::variant_count + static_cast<unsigned>(variant)].load(
            std::memory_order_acquire);
    return found == nullptr || found == native_forming || found == native_failed ? nullptr : found;
}

const tier3::Program* TierCache::compile_native(const tier2::Region& program, tier3::Variant variant) {
    if (program.program_id >= native_capacity_) return nullptr;
    auto& slot = native_programs_[program.program_id * tier3::variant_count + static_cast<unsigned>(variant)];
    const tier3::Program* expected = nullptr;
    // The first thread to ask builds it; the others go on interpreting.
    if (!slot.compare_exchange_strong(expected, native_forming, std::memory_order_acq_rel)) {
        return native_code(program, variant);
    }
    tier3::BuildResult built;
    try {
        built = tier3::build(image_, program, tier3::verify_enabled(), variant);
        if (built.program) built.program->ir.variant = variant;
        // Only the standard variant inlines: the profile and checkable
        // variants keep every call and return the reference's.
        if (built.program && variant == tier3::Variant::Standard && options_.native_inlining) {
            tier3::InlineSource source;
            source.program = [this](std::uint32_t function) -> const tier2::Region* {
                const std::lock_guard lock(mutex_);
                const auto& found = programs_[function];
                return found.state == RegionEntry::State::Settled ? found.region.get() : nullptr;
            };
            source.record = [this](const tier2::Region& region, std::uint32_t table) -> const tier3::DispatchRecord* {
                return dispatch_record(region, table);
            };
            tier3::inline_calls(image_, built.program->ir, source, tier3::verify_enabled());
        }
        if (built.program && options_.native_passes && tier3::passes_enabled()) tier3::optimize(image_, built.program->ir, tier3::verify_enabled());
        if (built.program) built.program->ir.check_heap = tier3::verify_enabled();
        if (built.program) tier3::place_canons(image_, built.program->ir, tier3::verify_enabled());
    } catch (const std::exception& error) {
        built.reason = std::string("internal error: ") + error.what();
    }
    // Machine code; a program that cannot have it stays on the program
    // interpreter.
    std::uint64_t bytes = 0;
    if (built.program && machine_code_) {
        auto generated = jit::native::generate(built.program->ir, tier3::verify_enabled());
        if (generated.failure.empty()) {
            built.program->machine = jit::Program::load(generated.code.bytes, generated.failure);
            if (!built.program->machine) generated.failure = "no executable memory: " + generated.failure;
        }
        if (!generated.failure.empty()) {
            built.reason = generated.failure;
            built.program.reset();
        } else {
            built.program->entry_code = std::move(generated.code.entries);
            built.program->trampoline = generated.code.trampoline;
            built.program->verify = tier3::verify_enabled();
            built.program->lazy_entries =
                std::make_unique<std::atomic<const std::uint8_t*>[]>(built.program->entry_code.size());
            bytes = generated.code.bytes.size();
        }
    }
    const tier3::Program* published = nullptr;
    if (std::getenv("NEURA_TIER_DUMP")) {
        std::string text = "no native program: " + built.reason + "\n";
        if (built.program) {
            text = tier3::describe(built.program->ir);
            for (const auto& [name, count] : built.program->ir.pass_counts) {
                text += "pass " + name + " " + std::to_string(count) + "\n";
            }
        }
        std::fputs(("tier 3 program " + std::to_string(program.program_id) + " of function " +
                    std::to_string(program.function) + ":\n" + text).c_str(), stderr);
    }
    {
        const std::lock_guard lock(mutex_);
        if (built.program) {
            published = built.program.get();
            for (const auto& [name, count] : built.program->ir.pass_counts) {
                for (const char* kind : {"nil", "lookup", "kind", "init", "bounds", "global"}) {
                    if (name == std::string("gvn.") + kind || name == std::string("elim.") + kind) {
                        statistics_.native_heap_checks[kind] += count;
                    } else if (name == std::string("licm.") + kind) {
                        statistics_.native_heap_hoisted += count;
                    }
                }
            }
            statistics_.native_inlined += built.program->ir.inlined_calls;
            statistics_.native_class_tests += built.program->ir.class_tested_dispatches;
            native_owned_.push_back(std::move(built.program));
            ++statistics_.native_programs;
            statistics_.native_bytes += bytes;
        } else {
            ++statistics_.native_failed;
            ++statistics_.native_rejections[built.reason];
        }
    }
    slot.store(published ? published : native_failed, std::memory_order_release);
    return published;
}

tier3::DispatchRecord* TierCache::dispatch_record(const tier2::Region& program, std::uint32_t table) const {
    if (program.program_id >= native_capacity_ || table >= program.dispatch_tables.size()) return nullptr;
    auto* records = dispatch_records_[program.program_id].load(std::memory_order_acquire);
    return records == nullptr ? nullptr : records + table;
}

// Under the cache's mutex, when `program` is published.
void TierCache::make_dispatch_records(const tier2::Region& program) {
    if (!native_enabled() || program.dispatch_tables.empty() || program.program_id >= native_capacity_) return;
    dispatch_owned_.push_back(std::make_unique<tier3::DispatchRecord[]>(program.dispatch_tables.size()));
    dispatch_records_[program.program_id].store(dispatch_owned_.back().get(), std::memory_order_release);
}

void TierCache::note_native(bool machine, const std::string& off) {
    const std::lock_guard lock(mutex_);
    if (machine) statistics_.native_mode = "native";
    else if (statistics_.native_mode.empty()) statistics_.native_mode = "interp";
    if (!off.empty()) statistics_.native_off = off;
}

void TierCache::note_native_entry(std::uint64_t bytes, const std::string& failure) {
    const std::lock_guard lock(mutex_);
    statistics_.native_bytes += bytes;
    if (!failure.empty()) ++statistics_.native_rejections["an entry: " + failure];
}

void TierCache::add_native_events(const NativeCounts& counts,
                                  const std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t>& deopts) {
    const std::lock_guard lock(mutex_);
    auto& total = statistics_.native;
    total.entered += counts.entered;
    total.left += counts.left;
    total.refused += counts.refused;
    total.budget += counts.budget;
    total.checks += counts.checks;
    total.guards += counts.guards;
    total.selectors += counts.selectors;
    total.references += counts.references;
    total.state_traps += counts.state_traps;
    total.exits += counts.exits;
    total.traps += counts.traps;
    total.steps += counts.steps;
    total.kernel_helpers += counts.kernel_helpers;
    total.fast_allocations += counts.fast_allocations;
    total.fast_appends += counts.fast_appends;
    total.slow_allocations += counts.slow_allocations;
    total.slow_appends += counts.slow_appends;
    total.helper_entries += counts.helper_entries;
    total.cell_entries += counts.cell_entries;
    for (const auto& [key, value] : deopts) statistics_.native_deopts[key] += value;
}

} // namespace neura::nvm
