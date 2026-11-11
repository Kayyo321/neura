// The region code generator (docs/runtime/NVM.md, "Native compilation"):
// tier2::Region -> MacroAssembler. Each op is compiled to do what the region
// interpreter (src/nvm/tier2_steps.inc) does with it, on every 64-bit value
// of its operands, not only on the values a well-typed program produces:
//
// - An operation computes into scratch registers and writes its
//   destination only after every check of it has passed, so a
//   deoptimization finds the destination (a slot register written
//   directly, for one) as it was.
// - Integer operations follow lift::evaluate (src/lift/evaluate.hpp)
//   expression by expression: the same masks, the same sign extension
//   ((v ^ sign) - sign, which is not a shift pair when v has bits above
//   the width), and the same failure conditions, tested with the
//   processor's carry and overflow flags where they are exactly those
//   conditions.
// - Everything else of a lowered instruction (float operations, division,
//   widths outside 1 to 64) calls lift::evaluate itself, so it is the
//   interpreter's code by construction.
// - Budget, Poll, CheckRef, and State do what the interpreter does, the
//   last three through helpers that run the interpreter's own code.
#include "assembler.hpp"
#include "jit.hpp"

#include "lift/evaluate.hpp"
#include "nvm/tier2.hpp"
#include "nvr/image.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

namespace neura::jit {
namespace {

using nvm::tier2::Code;
using nvm::tier2::Op;
using nvm::tier2::Region;
constexpr auto T0 = Scratch::T0;
constexpr auto T1 = Scratch::T1;
constexpr auto T2 = Scratch::T2;
constexpr auto T3 = Scratch::T3;

class Generator {
public:
    Generator(const Region& region, MacroAssembler& masm) : region_(region), m_(masm) {}

    std::uint32_t pinned() const { return pinned_; }

    void run() {
        const auto size = region_.code.size();
        if (size == 0) throw GenerationError("an empty region");
        for (std::size_t pc = 0; pc < size; ++pc) labels_.push_back(m_.label());
        exit_ = m_.label();
        exception_ = m_.label();
        choose_pinned();
        m_.prologue();
        for (std::uint32_t pc = 0; pc < size; ++pc) {
            m_.bind(labels_[pc]);
            emit(pc);
        }
        // Deoptimization stubs: the block and the reason, then the exit.
        for (const auto& [key, label] : stubs_) {
            m_.bind(label);
            m_.store_context32(offsets::next_block, std::get<0>(key));
            m_.store_context32(offsets::outcome, static_cast<std::uint32_t>(std::get<1>(key)));
            m_.jump(exit_);
        }
        m_.bind(exception_);
        m_.store_context32(offsets::outcome, static_cast<std::uint32_t>(Outcome::Exception));
        m_.bind(exit_);
        m_.epilogue();
    }

private:
    // The registers the program uses most, by static count of reads and
    // writes, live in machine registers.
    void choose_pinned() {
        std::map<std::uint32_t, std::uint64_t> uses;
        const auto use = [&](std::uint32_t reg) {
            if (reg < region_.register_count) ++uses[reg];
        };
        for (const auto& op : region_.code) {
            switch (op.code) {
            case Code::Lift:
                use(op.dst);
                use(op.a);
                use(op.b);
                break;
            case Code::Mask:
            case Code::Move:
            case Code::Store:
                use(op.dst);
                use(op.a);
                break;
            case Code::Gather:
                use(op.dst);
                for (unsigned i = 0; i < op.width; ++i) use(region_.gathers[op.index + i].reg);
                break;
            case Code::Branch:
            case Code::Switch:
            case Code::CheckRef:
                use(op.a);
                break;
            default:
                break;
            }
        }
        std::vector<std::pair<std::uint64_t, std::uint32_t>> ranked;
        for (const auto& [reg, count] : uses) {
            if (count >= 2) ranked.emplace_back(count, reg);
        }
        // Most uses first; the lower register on a tie.
        std::sort(ranked.begin(), ranked.end(), [](const auto& x, const auto& y) {
            return x.first != y.first ? x.first > y.first : x.second < y.second;
        });
        std::vector<std::uint32_t> pinned;
        for (const auto& [count, reg] : ranked) {
            if (pinned.size() >= m_.pin_capacity()) break;
            pinned.push_back(reg);
        }
        pinned_ = static_cast<std::uint32_t>(pinned.size());
        m_.pin(pinned);
    }

    Label stub(std::uint32_t block, Outcome outcome) {
        const auto key = std::make_tuple(block, outcome);
        if (const auto it = stubs_.find(key); it != stubs_.end()) return it->second;
        const auto label = m_.label();
        stubs_.emplace(key, label);
        return label;
    }

    Label target(std::uint32_t pc) const {
        if (pc >= labels_.size()) throw GenerationError("a branch outside the region program");
        return labels_[pc];
    }

    void check_register(std::uint32_t reg) const {
        if (reg >= region_.register_count) throw GenerationError("a register outside the region");
    }

    // value = sign_extend(value, width) as lift::sign_extend computes it.
    void sign_extend(Scratch value, unsigned width) {
        if (width >= 64) return;
        m_.move_immediate(T3, std::uint64_t{1} << (width - 1));
        m_.alu(Alu::Xor, value, T3);
        m_.alu(Alu::Sub, value, T3);
    }

    void mask(Scratch value, unsigned width) {
        if (width >= 64) return;
        m_.move_immediate(T3, lift::mask_of(width));
        m_.alu(Alu::And, value, T3);
    }

    // Branches to `fail` unless lo <= value <= hi, signed.
    void signed_range(Scratch value, std::int64_t lo, std::int64_t hi, Label fail) {
        m_.move_immediate(T3, static_cast<std::uint64_t>(hi));
        m_.branch_if(Cond::SGt, value, T3, fail);
        m_.move_immediate(T3, static_cast<std::uint64_t>(lo));
        m_.branch_if(Cond::SLt, value, T3, fail);
    }

    void emit(std::uint32_t pc) {
        const auto& op = region_.code[pc];
        switch (op.code) {
        case Code::Budget: return budget(op);
        case Code::Lift: return lift_op(pc, op);
        case Code::Mask:
            check_register(op.a);
            check_register(op.dst);
            m_.load(T0, op.a);
            mask(T0, op.width);
            m_.store(op.dst, T0);
            return;
        case Code::Move:
            check_register(op.a);
            check_register(op.dst);
            m_.load(T0, op.a);
            m_.store(op.dst, T0);
            return;
        case Code::Gather: return gather(op);
        case Code::Store:
            check_register(op.a);
            check_register(op.dst);
            m_.load(T0, op.a);
            m_.store(op.dst, T0);
            m_.set_initialized(op.dst);
            return;
        case Code::Branch:
            check_register(op.a);
            m_.load(T0, op.a);
            m_.branch_if_bit(T0, 0, target(op.target));
            if (op.other != pc + 1) m_.jump(target(op.other));
            return;
        case Code::Switch: {
            check_register(op.a);
            m_.load(T0, op.a);
            for (unsigned i = 0; i < op.width; ++i) {
                if (op.index + i >= region_.operands.size()) throw GenerationError("a switch outside its operands");
                m_.move_immediate(T1, i);
                m_.branch_if(Cond::Eq, T0, T1, target(region_.operands[op.index + i]));
            }
            m_.jump(stub(op.block, Outcome::Selectors));
            return;
        }
        case Code::Jump:
            m_.add_done(op.flags & nvm::tier2::completes);
            if (op.target != pc + 1) m_.jump(target(op.target));
            return;
        case Code::CheckRef: {
            check_register(op.a);
            m_.load(T0, op.a);
            m_.call(offsets::check_reference, op.index);
            helper_status(op.block, Outcome::References);
            return;
        }
        case Code::State: {
            // The helper reads the operation's arguments from the register
            // array.
            m_.flush_pinned();
            m_.call(offsets::state, pc);
            helper_status(op.block, Outcome::StateTraps);
            if (op.dst != nvr::absent) {
                check_register(op.dst);
                m_.store(op.dst, T0);
            }
            return;
        }
        case Code::Poll: {
            const auto skip = m_.label();
            m_.load_context(T0, offsets::polls);
            m_.branch_if_zero(T0, skip);
            m_.call(offsets::poll, 0);
            m_.load_context_byte(T1, offsets::status);
            m_.branch_if_nonzero(T1, exception_);
            m_.bind(skip);
            return;
        }
        case Code::Complete:
            m_.add_done(1);
            return;
        case Code::Leave:
            m_.add_done(op.flags & nvm::tier2::completes);
            m_.store_context32(offsets::next_block, op.target);
            m_.store_context32(offsets::outcome, static_cast<std::uint32_t>(Outcome::Leave));
            m_.jump(exit_);
            return;
        case Code::Deopt:
            m_.jump(stub(op.block, Outcome::Exits));
            return;
        }
        throw GenerationError("an unknown op");
    }

    // status 1 deoptimizes for `outcome`, 2 ends with the helper's exception.
    void helper_status(std::uint32_t block, Outcome outcome) {
        const auto ok = m_.label();
        m_.load_context_byte(T1, offsets::status);
        m_.branch_if_zero(T1, ok);
        m_.move_immediate(T2, 2);
        m_.branch_if(Cond::Eq, T1, T2, exception_);
        m_.jump(stub(block, outcome));
        m_.bind(ok);
    }

    // As the interpreter's Budget: with used = steps + done,
    //   used <= max_steps && max_steps - used >= steps of the checkpoint,
    //   allowance - done >= steps of the checkpoint, and
    //   no scheduler, or polls + polls of the checkpoint < quantum
    // (polls only when the checkpoint has any), all in wrapping 64-bit
    // arithmetic.
    void budget(const Op& op) {
        if (op.a >= region_.checkpoints.size()) throw GenerationError("a checkpoint outside the region");
        const auto& checkpoint = region_.checkpoints[op.a];
        const auto fail = stub(op.block, Outcome::Budget);
        m_.load_context(T0, offsets::steps);
        m_.load_indirect(T0, T0);
        m_.done_to(T1);
        m_.alu(Alu::Add, T0, T1);
        m_.load_context(T2, offsets::max_steps);
        m_.branch_if(Cond::UGt, T0, T2, fail);
        m_.alu(Alu::Sub, T2, T0);
        m_.move_immediate(T3, checkpoint.steps);
        m_.branch_if(Cond::ULt, T2, T3, fail);
        m_.load_context(T0, offsets::allowance);
        m_.alu(Alu::Sub, T0, T1);
        m_.branch_if(Cond::ULt, T0, T3, fail);
        if (checkpoint.polls != 0) {
            const auto fits = m_.label();
            m_.load_context(T0, offsets::polls);
            m_.branch_if_zero(T0, fits);
            m_.load_indirect(T0, T0);
            m_.move_immediate(T3, checkpoint.polls);
            m_.alu(Alu::Add, T0, T3);
            m_.load_context(T2, offsets::quantum);
            m_.branch_if(Cond::UGe, T0, T2, fail);
            m_.bind(fits);
        }
    }

    void gather(const Op& op) {
        check_register(op.dst);
        if (op.width > 64) throw GenerationError("a gather wider than a register");
        if (op.index + op.width > region_.gathers.size()) throw GenerationError("a gather outside its bits");
        m_.move_immediate(T0, 0);
        for (unsigned i = 0; i < op.width; ++i) {
            const auto& bit = region_.gathers[op.index + i];
            check_register(bit.reg);
            if (bit.bit >= 64) throw GenerationError("a gathered bit outside a register");
            // value |= ((reg >> bit) & 1) << i
            m_.load(T1, bit.reg);
            if (bit.bit != 0) m_.shift_immediate(Shift::LogicalRight, T1, bit.bit);
            m_.move_immediate(T3, 1);
            m_.alu(Alu::And, T1, T3);
            if (i != 0) m_.shift_immediate(Shift::Left, T1, i);
            m_.alu(Alu::Or, T0, T1);
        }
        m_.store(op.dst, T0);
    }

    // Whether the instruction is compiled inline: integer operations of 1
    // to 64 bits other than division. The rest calls lift::evaluate.
    static bool inline_instruction(const lift::Instruction& in) {
        if (in.width == 0 || in.width > 64) return false;
        switch (in.op) {
        case lift::Opcode::Constant:
        case lift::Opcode::Add:
        case lift::Opcode::AddWrapping:
        case lift::Opcode::Subtract:
        case lift::Opcode::Negate:
        case lift::Opcode::Multiply:
        case lift::Opcode::Equal:
        case lift::Opcode::NotEqual:
        case lift::Opcode::Less:
        case lift::Opcode::LessEqual:
        case lift::Opcode::Greater:
        case lift::Opcode::GreaterEqual:
        case lift::Opcode::And:
        case lift::Opcode::Or:
        case lift::Opcode::Not:
        case lift::Opcode::BitAnd:
        case lift::Opcode::BitOr:
        case lift::Opcode::BitXor:
        case lift::Opcode::BitNot:
        case lift::Opcode::ShiftLeft:
        case lift::Opcode::ShiftRight:
        case lift::Opcode::Index:
            return true;
        case lift::Opcode::ConvertInteger:
            return in.to_width >= 1 && in.to_width <= 64;
        default:
            return false;
        }
    }

    void lift_op(std::uint32_t, const Op& op) {
        if (op.index >= region_.instructions.size()) throw GenerationError("an instruction outside the region");
        const auto& in = region_.instructions[op.index];
        check_register(op.dst);
        check_register(op.a);
        check_register(op.b);
        const auto fail = stub(op.block, Outcome::Checks);
        if (!inline_instruction(in)) {
            m_.load(T0, op.a);
            m_.load(T1, op.b);
            m_.call(offsets::lift, op.index);
            m_.load_context_byte(T1, offsets::status);
            m_.branch_if_nonzero(T1, fail);
            m_.store(op.dst, T0);
        } else {
            const auto result = integer(in, op, fail);
            m_.store(op.dst, result);
        }
        if (op.flags & nvm::tier2::defines) m_.set_initialized(op.dst);
    }

    // The integer instruction `in` on registers op.a and op.b, as
    // lift::evaluate computes it; branches to `fail` where it sets
    // `failed`. Returns the scratch register holding the result.
    Scratch integer(const lift::Instruction& in, const Op& op, Label fail) {
        using lift::Opcode;
        const unsigned w = in.width;
        const auto m = lift::mask_of(w);
        const auto operands = [&] {
            m_.load(T0, op.a);
            m_.load(T1, op.b);
        };
        switch (in.op) {
        case Opcode::Constant:
            m_.move_immediate(T0, in.immediate);
            return T0;
        case Opcode::Add:
            operands();
            if (!in.is_signed) {
                if (w >= 64) {
                    // r = a + b; failed = r < a: the carry.
                    m_.add_or_branch(Overflow::Unsigned, T0, T1, fail);
                } else {
                    // failed = a + b > m (wrapping); r = (a + b) & m, which
                    // is a + b when it did not fail.
                    m_.alu(Alu::Add, T0, T1);
                    m_.move_immediate(T3, m);
                    m_.branch_if(Cond::UGt, T0, T3, fail);
                }
                return T0;
            }
            if (w >= 64) {
                // checked_add of the whole values: signed overflow.
                m_.add_or_branch(Overflow::Signed, T0, T1, fail);
                return T0;
            }
            // r = (a + b) & m; failed = checked_add(x, y) overflows or the
            // sum is outside the width's range.
            m_.move(T2, T0);
            m_.alu(Alu::Add, T2, T1);
            sign_extend(T0, w);
            sign_extend(T1, w);
            m_.add_or_branch(Overflow::Signed, T0, T1, fail);
            signed_range(T0, lift::signed_min(w), lift::signed_max(w), fail);
            mask(T2, w);
            return T2;
        case Opcode::AddWrapping:
            operands();
            m_.alu(Alu::Add, T0, T1);
            mask(T0, w);
            return T0;
        case Opcode::Subtract:
            operands();
            if (!in.is_signed) {
                // failed = a < b; r = (a - b) & m.
                m_.branch_if(Cond::ULt, T0, T1, fail);
                m_.alu(Alu::Sub, T0, T1);
                mask(T0, w);
                return T0;
            }
            if (w >= 64) {
                m_.subtract_or_branch(T0, T1, fail);
                return T0;
            }
            // r = (a - b) & m; failed = x - y outside the width's range
            // (x - y of sign-extended values, in wrapping arithmetic).
            m_.move(T2, T0);
            m_.alu(Alu::Sub, T2, T1);
            sign_extend(T0, w);
            sign_extend(T1, w);
            m_.alu(Alu::Sub, T0, T1);
            signed_range(T0, lift::signed_min(w), lift::signed_max(w), fail);
            mask(T2, w);
            return T2;
        case Opcode::Negate:
            m_.load(T0, op.a);
            // failed = signed ? a == 1 << (w - 1) : a != 0; r = (0 - a) & m.
            m_.move_immediate(T3, in.is_signed ? std::uint64_t{1} << (w - 1) : 0);
            m_.branch_if(in.is_signed ? Cond::Eq : Cond::Ne, T0, T3, fail);
            m_.negate(T0);
            mask(T0, w);
            return T0;
        case Opcode::Multiply:
            operands();
            if (!in.is_signed) {
                if (w >= 64) {
                    // failed = a != 0 && b > max / a: the product needs more
                    // than 64 bits.
                    m_.multiply_or_branch(Overflow::Unsigned, T0, T1, fail);
                } else {
                    // failed = a * b > m (wrapping); r = (a * b) & m.
                    m_.alu(Alu::Mul, T0, T1);
                    m_.move_immediate(T3, m);
                    m_.branch_if(Cond::UGt, T0, T3, fail);
                }
                return T0;
            }
            // r = (a * b) & m; failed = checked_multiply(x, y) overflows or
            // the product is outside the width's range.
            m_.move(T2, T0);
            m_.alu(Alu::Mul, T2, T1);
            sign_extend(T0, w);
            sign_extend(T1, w);
            m_.multiply_or_branch(Overflow::Signed, T0, T1, fail);
            if (w < 64) signed_range(T0, lift::signed_min(w), lift::signed_max(w), fail);
            mask(T2, w);
            return T2;
        case Opcode::Equal:
        case Opcode::NotEqual:
            operands();
            m_.set_if(in.op == Opcode::Equal ? Cond::Eq : Cond::Ne, T0, T0, T1);
            return T0;
        case Opcode::Less:
        case Opcode::LessEqual:
        case Opcode::Greater:
        case Opcode::GreaterEqual: {
            operands();
            if (in.is_signed) {
                sign_extend(T0, w);
                sign_extend(T1, w);
            }
            Cond cond = Cond::Eq;
            switch (in.op) {
            case Opcode::Less: cond = in.is_signed ? Cond::SLt : Cond::ULt; break;
            case Opcode::LessEqual: cond = in.is_signed ? Cond::SLe : Cond::ULe; break;
            case Opcode::Greater: cond = in.is_signed ? Cond::SGt : Cond::UGt; break;
            default: cond = in.is_signed ? Cond::SGe : Cond::UGe; break;
            }
            m_.set_if(cond, T0, T0, T1);
            return T0;
        }
        case Opcode::And:
            // a & b & 1
            operands();
            m_.alu(Alu::And, T0, T1);
            mask(T0, 1);
            return T0;
        case Opcode::Or:
            operands();
            m_.alu(Alu::Or, T0, T1);
            mask(T0, 1);
            return T0;
        case Opcode::Not:
            // (a ^ 1) & 1
            m_.load(T0, op.a);
            m_.move_immediate(T3, 1);
            m_.alu(Alu::Xor, T0, T3);
            m_.alu(Alu::And, T0, T3);
            return T0;
        case Opcode::BitAnd:
            operands();
            m_.alu(Alu::And, T0, T1);
            return T0;
        case Opcode::BitOr:
            operands();
            m_.alu(Alu::Or, T0, T1);
            return T0;
        case Opcode::BitXor:
            operands();
            m_.alu(Alu::Xor, T0, T1);
            return T0;
        case Opcode::BitNot:
            m_.load(T0, op.a);
            m_.bit_not(T0);
            mask(T0, w);
            return T0;
        case Opcode::ShiftLeft:
        case Opcode::ShiftRight:
            operands();
            // The count is read as unsigned: failed = b >= w.
            m_.move_immediate(T3, w);
            m_.branch_if(Cond::UGe, T1, T3, fail);
            if (in.op == Opcode::ShiftLeft) {
                m_.shift(Shift::Left, T0);
            } else if (in.is_signed) {
                sign_extend(T0, w);
                m_.shift(Shift::ArithmeticRight, T0);
            } else {
                m_.shift(Shift::LogicalRight, T0);
            }
            mask(T0, w);
            return T0;
        case Opcode::Index:
            m_.load(T0, op.a);
            // failed = signed && bit w - 1 of a is set; r = a.
            if (in.is_signed) m_.branch_if_bit(T0, w - 1, fail);
            return T0;
        case Opcode::ConvertInteger: {
            const unsigned to = in.to_width;
            m_.load(T0, op.a);
            if (in.is_signed) {
                sign_extend(T0, w);
                if (in.to_signed) {
                    signed_range(T0, lift::signed_min(to), lift::signed_max(to), fail);
                } else {
                    // x < 0 || (uint64) x > mask(to)
                    m_.move_immediate(T3, 0);
                    m_.branch_if(Cond::SLt, T0, T3, fail);
                    m_.move_immediate(T3, lift::mask_of(to));
                    m_.branch_if(Cond::UGt, T0, T3, fail);
                }
            } else {
                m_.move_immediate(T3, in.to_signed ? static_cast<std::uint64_t>(lift::signed_max(to))
                                                   : lift::mask_of(to));
                m_.branch_if(Cond::UGt, T0, T3, fail);
            }
            mask(T0, to);
            return T0;
        }
        default:
            break;
        }
        throw GenerationError("an instruction the generator cannot compile inline");
    }

    const Region& region_;
    MacroAssembler& m_;
    std::vector<Label> labels_;
    Label exit_;
    Label exception_;
    std::map<std::tuple<std::uint32_t, Outcome>, Label> stubs_;
    std::uint32_t pinned_ = 0;
};

} // namespace

Generated generate(const Region& region, Arch arch) {
    Generated result;
    try {
        auto masm = arch == Arch::X86_64 ? make_x86_64() : make_aarch64();
        Generator generator(region, *masm);
        generator.run();
        result.code = masm->finish();
        result.pinned = generator.pinned();
    } catch (const GenerationError& error) {
        result.code.clear();
        result.failure = error.what();
    }
    return result;
}

} // namespace neura::jit
