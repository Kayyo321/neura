#include "x86_64.hpp"

#include "assembler.hpp"
#include "jit.hpp"

#include <map>
#include <optional>

namespace neura::jit::x86_64 {

void Emitter::u32(std::uint32_t value) {
    for (int i = 0; i < 4; ++i) byte(static_cast<std::uint8_t>(value >> (8 * i)));
}

void Emitter::rex(bool w, unsigned reg, unsigned base, bool force) {
    const auto prefix = static_cast<std::uint8_t>(0x40 | (w ? 8 : 0) | ((reg >> 3) << 2) | (base >> 3));
    if (prefix != 0x40 || force) byte(prefix);
}

// ModRM (and SIB) for [base + displacement] with `reg` in the reg field:
// no displacement where the base allows it, else 8 bits where it fits,
// else 32.
void Emitter::memory(unsigned reg, Reg base, std::int32_t displacement) {
    const unsigned rm = base & 7U;
    unsigned mod = 2;
    if (displacement == 0 && rm != 5) {
        mod = 0;
    } else if (displacement >= -128 && displacement <= 127) {
        mod = 1;
    }
    byte(static_cast<std::uint8_t>((mod << 6) | ((reg & 7U) << 3) | rm));
    if (rm == 4) byte(0x24);  // SIB: base only
    if (mod == 1) byte(static_cast<std::uint8_t>(displacement));
    if (mod == 2) u32(static_cast<std::uint32_t>(displacement));
}

void Emitter::mov(Reg to, Reg from) {
    rex(true, from, to);
    byte(0x89);
    byte(static_cast<std::uint8_t>(0xC0 | ((from & 7U) << 3) | (to & 7U)));
}

void Emitter::load(Reg to, Reg base, std::int32_t displacement) {
    rex(true, to, base);
    byte(0x8B);
    memory(to, base, displacement);
}

void Emitter::store(Reg base, std::int32_t displacement, Reg from) {
    rex(true, from, base);
    byte(0x89);
    memory(from, base, displacement);
}

void Emitter::load_byte(Reg to, Reg base, std::int32_t displacement) {
    rex(false, to, base);
    byte(0x0F);
    byte(0xB6);
    memory(to, base, displacement);
}

void Emitter::store_byte(Reg base, std::int32_t displacement, std::uint8_t value) {
    rex(false, 0, base);
    byte(0xC6);
    memory(0, base, displacement);
    byte(value);
}

void Emitter::store32(Reg base, std::int32_t displacement, std::uint32_t value) {
    rex(false, 0, base);
    byte(0xC7);
    memory(0, base, displacement);
    u32(value);
}

void Emitter::mov_immediate(Reg to, std::uint64_t value) {
    if (value <= 0xFFFFFFFFULL) {
        rex(false, 0, to);
        byte(static_cast<std::uint8_t>(0xB8 + (to & 7U)));
        u32(static_cast<std::uint32_t>(value));
        return;
    }
    const auto as_signed = static_cast<std::int64_t>(value);
    if (as_signed >= INT32_MIN && as_signed <= INT32_MAX) {
        rex(true, 0, to);
        byte(0xC7);
        byte(static_cast<std::uint8_t>(0xC0 | (to & 7U)));
        u32(static_cast<std::uint32_t>(value));
        return;
    }
    rex(true, 0, to);
    byte(static_cast<std::uint8_t>(0xB8 + (to & 7U)));
    u32(static_cast<std::uint32_t>(value));
    u32(static_cast<std::uint32_t>(value >> 32));
}

void Emitter::alu(AluOp op, Reg to, Reg from) {
    rex(true, from, to);
    byte(static_cast<std::uint8_t>(op));
    byte(static_cast<std::uint8_t>(0xC0 | ((from & 7U) << 3) | (to & 7U)));
}

void Emitter::add_immediate(Reg to, std::int32_t value) {
    rex(true, 0, to);
    if (value >= -128 && value <= 127) {
        byte(0x83);
        byte(static_cast<std::uint8_t>(0xC0 | (to & 7U)));
        byte(static_cast<std::uint8_t>(value));
    } else {
        byte(0x81);
        byte(static_cast<std::uint8_t>(0xC0 | (to & 7U)));
        u32(static_cast<std::uint32_t>(value));
    }
}

void Emitter::imul(Reg to, Reg from) {
    rex(true, to, from);
    byte(0x0F);
    byte(0xAF);
    byte(static_cast<std::uint8_t>(0xC0 | ((to & 7U) << 3) | (from & 7U)));
}

void Emitter::mul(Reg from) {
    rex(true, 0, from);
    byte(0xF7);
    byte(static_cast<std::uint8_t>(0xC0 | (4U << 3) | (from & 7U)));
}

void Emitter::div(Reg from) {
    rex(true, 0, from);
    byte(0xF7);
    byte(static_cast<std::uint8_t>(0xC0 | (6U << 3) | (from & 7U)));
}

void Emitter::idiv(Reg from) {
    rex(true, 0, from);
    byte(0xF7);
    byte(static_cast<std::uint8_t>(0xC0 | (7U << 3) | (from & 7U)));
}

void Emitter::cqo() {
    byte(0x48);
    byte(0x99);
}

void Emitter::shift_cl(ShiftOp op, Reg value) {
    rex(true, 0, value);
    byte(0xD3);
    byte(static_cast<std::uint8_t>(0xC0 | (static_cast<unsigned>(op) << 3) | (value & 7U)));
}

void Emitter::shift_immediate(ShiftOp op, Reg value, std::uint8_t count) {
    rex(true, 0, value);
    byte(count == 1 ? 0xD1 : 0xC1);
    byte(static_cast<std::uint8_t>(0xC0 | (static_cast<unsigned>(op) << 3) | (value & 7U)));
    if (count != 1) byte(count);
}

void Emitter::neg(Reg value) {
    rex(true, 0, value);
    byte(0xF7);
    byte(static_cast<std::uint8_t>(0xC0 | (3U << 3) | (value & 7U)));
}

void Emitter::not_(Reg value) {
    rex(true, 0, value);
    byte(0xF7);
    byte(static_cast<std::uint8_t>(0xC0 | (2U << 3) | (value & 7U)));
}

void Emitter::setcc(CC cc, Reg to) {
    // SPL, BPL, SIL, and DIL need a REX prefix to be byte registers.
    rex(false, 0, to, to >= 4 && to < 8);
    byte(0x0F);
    byte(static_cast<std::uint8_t>(0x90 + cc));
    byte(static_cast<std::uint8_t>(0xC0 | (to & 7U)));
}

void Emitter::movzx_byte(Reg to, Reg from) {
    rex(false, to, from, from >= 4 && from < 8);
    byte(0x0F);
    byte(0xB6);
    byte(static_cast<std::uint8_t>(0xC0 | ((to & 7U) << 3) | (from & 7U)));
}

void Emitter::bt(Reg value, std::uint8_t bit) {
    rex(true, 0, value);
    byte(0x0F);
    byte(0xBA);
    byte(static_cast<std::uint8_t>(0xC0 | (4U << 3) | (value & 7U)));
    byte(bit);
}

void Emitter::test(Reg a, Reg b) {
    rex(true, b, a);
    byte(0x85);
    byte(static_cast<std::uint8_t>(0xC0 | ((b & 7U) << 3) | (a & 7U)));
}

std::size_t Emitter::jcc(CC cc) {
    byte(0x0F);
    byte(static_cast<std::uint8_t>(0x80 + cc));
    const auto at = size();
    u32(0);
    return at;
}

std::size_t Emitter::jmp() {
    byte(0xE9);
    const auto at = size();
    u32(0);
    return at;
}

void Emitter::patch(std::size_t at, std::size_t target) {
    const auto displacement = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(at + 4);
    const auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement));
    for (int i = 0; i < 4; ++i) code[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

void Emitter::call(Reg base, std::int32_t displacement) {
    rex(false, 0, base);
    byte(0xFF);
    memory(2, base, displacement);
}

void Emitter::push(Reg value) {
    rex(false, 0, value);
    byte(static_cast<std::uint8_t>(0x50 + (value & 7U)));
}

void Emitter::pop(Reg value) {
    rex(false, 0, value);
    byte(static_cast<std::uint8_t>(0x58 + (value & 7U)));
}

void Emitter::ret() { byte(0xC3); }

namespace {

// Register use of the generated code (System V AMD64 calling convention):
// r13 the context, r15 the register array, r14 the initialized flags, r12
// the completed blocks; rax, rcx, rdx, r11 the scratch registers T0 to T3;
// rbx and rbp (callee-saved) and rsi, rdi, r8, r9, r10 (caller-saved,
// stored and reloaded around calls) hold pinned region registers.
constexpr Reg context = r13;
constexpr Reg registers = r15;
constexpr Reg flags = r14;
constexpr Reg done = r12;
constexpr Reg pinnable[] = {rbx, rbp, rsi, rdi, r8, r9, r10};

bool caller_saved(Reg reg) { return reg != rbx && reg != rbp; }

Reg scratch(Scratch s) {
    switch (s) {
    case Scratch::T0: return rax;
    case Scratch::T1: return rcx;
    case Scratch::T2: return rdx;
    case Scratch::T3: return r11;
    }
    return rax;
}

CC condition(Cond cond) {
    switch (cond) {
    case Cond::Eq: return e;
    case Cond::Ne: return ne;
    case Cond::ULt: return b;
    case Cond::ULe: return be;
    case Cond::UGt: return a;
    case Cond::UGe: return ae;
    case Cond::SLt: return l;
    case Cond::SLe: return le;
    case Cond::SGt: return g;
    case Cond::SGe: return ge;
    }
    return e;
}

std::int32_t slot_of(std::uint32_t reg) {
    if (reg >= (1U << 27)) throw GenerationError("too many registers");
    return static_cast<std::int32_t>(reg * 8);
}

class Assembler final : public MacroAssembler {
public:
    void bind(Label label) override {
        if (bound_.size() <= label.id) bound_.resize(label.id + 1);
        bound_[label.id] = e_.size();
    }

    void jump(Label label) override { fixups_.emplace_back(e_.jmp(), label.id); }

    std::size_t pin_capacity() const override { return std::size(pinnable); }

    void pin(const std::vector<std::uint32_t>& pinned) override {
        for (std::size_t i = 0; i < pinned.size() && i < std::size(pinnable); ++i) {
            pinned_.emplace(pinned[i], pinnable[i]);
        }
    }

    void prologue() override {
        for (const auto reg : {rbx, rbp, r12, r13, r14, r15}) e_.push(reg);
        e_.add_immediate(rsp, -8);  // 16-byte alignment at calls
        e_.mov(context, rdi);
        e_.load(registers, context, static_cast<std::int32_t>(offsets::registers));
        e_.load(flags, context, static_cast<std::int32_t>(offsets::initialized));
        e_.mov_immediate(done, 0);
        for (const auto& [reg, machine] : pinned_) e_.load(machine, registers, slot_of(reg));
    }

    void epilogue() override {
        flush_pinned();
        e_.store(context, static_cast<std::int32_t>(offsets::done), done);
        e_.add_immediate(rsp, 8);
        for (const auto reg : {r15, r14, r13, r12, rbp, rbx}) e_.pop(reg);
        e_.ret();
    }

    void load(Scratch to, std::uint32_t reg) override {
        if (const auto it = pinned_.find(reg); it != pinned_.end()) {
            e_.mov(scratch(to), it->second);
        } else {
            e_.load(scratch(to), registers, slot_of(reg));
        }
    }

    void store(std::uint32_t reg, Scratch from) override {
        if (const auto it = pinned_.find(reg); it != pinned_.end()) {
            e_.mov(it->second, scratch(from));
        } else {
            e_.store(registers, slot_of(reg), scratch(from));
        }
    }

    void set_initialized(std::uint32_t reg) override {
        if (reg >= (1U << 30)) throw GenerationError("too many registers");
        e_.store_byte(flags, static_cast<std::int32_t>(reg), 1);
    }

    void flush_pinned() override {
        for (const auto& [reg, machine] : pinned_) e_.store(registers, slot_of(reg), machine);
    }

    void move_immediate(Scratch to, std::uint64_t value) override { e_.mov_immediate(scratch(to), value); }
    void move(Scratch to, Scratch from) override {
        if (to != from) e_.mov(scratch(to), scratch(from));
    }

    void alu(Alu op, Scratch to, Scratch from) override {
        switch (op) {
        case Alu::Add: e_.alu(AluOp::add, scratch(to), scratch(from)); break;
        case Alu::Sub: e_.alu(AluOp::sub, scratch(to), scratch(from)); break;
        case Alu::And: e_.alu(AluOp::and_, scratch(to), scratch(from)); break;
        case Alu::Or: e_.alu(AluOp::or_, scratch(to), scratch(from)); break;
        case Alu::Xor: e_.alu(AluOp::xor_, scratch(to), scratch(from)); break;
        case Alu::Mul: e_.imul(scratch(to), scratch(from)); break;
        }
    }

    static ShiftOp shift_op(Shift op) {
        switch (op) {
        case Shift::Left: return ShiftOp::shl;
        case Shift::LogicalRight: return ShiftOp::shr;
        case Shift::ArithmeticRight: return ShiftOp::sar;
        }
        return ShiftOp::shl;
    }

    void shift(Shift op, Scratch to) override {
        if (to == Scratch::T1) throw GenerationError("shift of the count register");
        e_.shift_cl(shift_op(op), scratch(to));
    }

    void shift_immediate(Shift op, Scratch to, unsigned count) override {
        e_.shift_immediate(shift_op(op), scratch(to), static_cast<std::uint8_t>(count));
    }

    void negate(Scratch value) override { e_.neg(scratch(value)); }
    void bit_not(Scratch value) override { e_.not_(scratch(value)); }

    void set_if(Cond cond, Scratch to, Scratch a, Scratch b) override {
        e_.alu(AluOp::cmp, scratch(a), scratch(b));
        e_.setcc(condition(cond), scratch(to));
        e_.movzx_byte(scratch(to), scratch(to));
    }

    void branch_if(Cond cond, Scratch a, Scratch b, Label target) override {
        e_.alu(AluOp::cmp, scratch(a), scratch(b));
        fixups_.emplace_back(e_.jcc(condition(cond)), target.id);
    }

    void branch_if_zero(Scratch value, Label target) override {
        e_.test(scratch(value), scratch(value));
        fixups_.emplace_back(e_.jcc(e), target.id);
    }

    void branch_if_nonzero(Scratch value, Label target) override {
        e_.test(scratch(value), scratch(value));
        fixups_.emplace_back(e_.jcc(ne), target.id);
    }

    void branch_if_bit(Scratch value, unsigned bit, Label target) override {
        e_.bt(scratch(value), static_cast<std::uint8_t>(bit));
        fixups_.emplace_back(e_.jcc(b), target.id);  // CF holds the bit
    }

    void add_or_branch(Overflow kind, Scratch to, Scratch from, Label target) override {
        e_.alu(AluOp::add, scratch(to), scratch(from));
        fixups_.emplace_back(e_.jcc(kind == Overflow::Unsigned ? b : o), target.id);
    }

    void subtract_or_branch(Scratch to, Scratch from, Label target) override {
        e_.alu(AluOp::sub, scratch(to), scratch(from));
        fixups_.emplace_back(e_.jcc(o), target.id);
    }

    void multiply_or_branch(Overflow kind, Scratch to, Scratch from, Label target) override {
        if (kind == Overflow::Unsigned) {
            // mul sets CF and OF when RDX, the high half, is not zero.
            if (to != Scratch::T0 || from != Scratch::T1) throw GenerationError("unsigned multiply operands");
            e_.mul(rcx);
        } else {
            // A two-operand imul sets OF when the signed product is
            // truncated.
            e_.imul(scratch(to), scratch(from));
        }
        fixups_.emplace_back(e_.jcc(o), target.id);
    }

    void divide(bool is_signed, bool remainder) override {
        if (is_signed) {
            e_.cqo();
            e_.idiv(rcx);
        } else {
            e_.mov_immediate(rdx, 0);
            e_.div(rcx);
        }
        if (remainder) e_.mov(rax, rdx);
    }

    void load_context(Scratch to, std::uint32_t offset) override {
        e_.load(scratch(to), context, static_cast<std::int32_t>(offset));
    }

    void load_context_byte(Scratch to, std::uint32_t offset) override {
        e_.load_byte(scratch(to), context, static_cast<std::int32_t>(offset));
    }

    void store_context32(std::uint32_t offset, std::uint32_t value) override {
        e_.store32(context, static_cast<std::int32_t>(offset), value);
    }

    void load_indirect(Scratch to, Scratch base) override { e_.load(scratch(to), scratch(base), 0); }
    void done_to(Scratch to) override { e_.mov(scratch(to), done); }

    void add_done(std::uint32_t count) override {
        if (count != 0) e_.add_immediate(done, static_cast<std::int32_t>(count));
    }

    void call(std::uint32_t helper, std::uint32_t immediate) override {
        for (const auto& [reg, machine] : pinned_) {
            if (caller_saved(machine)) e_.store(registers, slot_of(reg), machine);
        }
        // (context, immediate, a, b) in rdi, rsi, rdx, rcx: b is already
        // in rcx (T1).
        e_.mov(rdx, rax);
        e_.mov(rdi, context);
        e_.mov_immediate(rsi, immediate);
        e_.call(context, static_cast<std::int32_t>(helper));
        for (const auto& [reg, machine] : pinned_) {
            if (caller_saved(machine)) e_.load(machine, registers, slot_of(reg));
        }
    }

    std::vector<std::uint8_t> finish() override {
        for (const auto& [at, id] : fixups_) {
            if (id >= bound_.size() || !bound_[id]) throw GenerationError("unbound label");
            e_.patch(at, *bound_[id]);
        }
        return std::move(e_.code);
    }

private:
    Emitter e_;
    std::map<std::uint32_t, Reg> pinned_;
    std::vector<std::optional<std::size_t>> bound_;
    std::vector<std::pair<std::size_t, std::uint32_t>> fixups_;
};

} // namespace

} // namespace neura::jit::x86_64

namespace neura::jit {

std::unique_ptr<MacroAssembler> make_x86_64() { return std::make_unique<x86_64::Assembler>(); }

} // namespace neura::jit
