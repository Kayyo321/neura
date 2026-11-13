#include "aarch64.hpp"

#include "assembler.hpp"
#include "jit.hpp"

#include <map>
#include <optional>

namespace neura::jit::aarch64 {

std::vector<std::uint8_t> Emitter::bytes() const {
    std::vector<std::uint8_t> out;
    out.reserve(code.size() * 4);
    for (const auto word : code) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(word >> (8 * i)));
    }
    return out;
}

void Emitter::three(std::uint32_t base, Reg to, Reg a, Reg b) {
    emit(base | (std::uint32_t{b} << 16) | (std::uint32_t{a} << 5) | to);
}

void Emitter::mov(Reg to, Reg from) { three(0xAA000000, to, zr, from); }

void Emitter::ldr(Reg to, Reg base, std::uint32_t offset) {
    emit(0xF9400000 | ((offset / 8) << 10) | (std::uint32_t{base} << 5) | to);
}

void Emitter::str(Reg from, Reg base, std::uint32_t offset) {
    emit(0xF9000000 | ((offset / 8) << 10) | (std::uint32_t{base} << 5) | from);
}

void Emitter::ldr_indexed(Reg to, Reg base, Reg index) { three(0xF8607800, to, base, index); }
void Emitter::str_indexed(Reg from, Reg base, Reg index) { three(0xF8207800, from, base, index); }

void Emitter::ldrb(Reg to, Reg base, std::uint32_t offset) {
    emit(0x39400000 | (offset << 10) | (std::uint32_t{base} << 5) | to);
}

void Emitter::strb(Reg from, Reg base, std::uint32_t offset) {
    emit(0x39000000 | (offset << 10) | (std::uint32_t{base} << 5) | from);
}

void Emitter::strb_indexed(Reg from, Reg base, Reg index) { three(0x38206800, from, base, index); }

void Emitter::str_w(Reg from, Reg base, std::uint32_t offset) {
    emit(0xB9000000 | ((offset / 4) << 10) | (std::uint32_t{base} << 5) | from);
}

void Emitter::movz(Reg to, std::uint16_t value, unsigned shift) {
    emit(0xD2800000 | ((shift / 16) << 21) | (std::uint32_t{value} << 5) | to);
}

void Emitter::movk(Reg to, std::uint16_t value, unsigned shift) {
    emit(0xF2800000 | ((shift / 16) << 21) | (std::uint32_t{value} << 5) | to);
}

void Emitter::movn(Reg to, std::uint16_t value, unsigned shift) {
    emit(0x92800000 | ((shift / 16) << 21) | (std::uint32_t{value} << 5) | to);
}

void Emitter::mov_immediate(Reg to, std::uint64_t value) {
    unsigned zeros = 0;
    unsigned ones = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto half = (value >> (16 * i)) & 0xFFFFU;
        if (half == 0) ++zeros;
        if (half == 0xFFFFU) ++ones;
    }
    if (ones > zeros) {
        // movn sets every halfword but one to 0xffff.
        bool first = true;
        for (unsigned i = 0; i < 4; ++i) {
            const auto half = static_cast<std::uint16_t>(value >> (16 * i));
            if (half == 0xFFFFU) continue;
            if (first) {
                movn(to, static_cast<std::uint16_t>(~half), 16 * i);
                first = false;
            } else {
                movk(to, half, 16 * i);
            }
        }
        if (first) movn(to, 0, 0);  // all ones
        return;
    }
    bool first = true;
    for (unsigned i = 0; i < 4; ++i) {
        const auto half = static_cast<std::uint16_t>(value >> (16 * i));
        if (half == 0) continue;
        if (first) {
            movz(to, half, 16 * i);
            first = false;
        } else {
            movk(to, half, 16 * i);
        }
    }
    if (first) movz(to, 0, 0);
}

void Emitter::add(Reg to, Reg a, Reg b) { three(0x8B000000, to, a, b); }
void Emitter::adds(Reg to, Reg a, Reg b) { three(0xAB000000, to, a, b); }
void Emitter::sub(Reg to, Reg a, Reg b) { three(0xCB000000, to, a, b); }
void Emitter::subs(Reg to, Reg a, Reg b) { three(0xEB000000, to, a, b); }
void Emitter::and_(Reg to, Reg a, Reg b) { three(0x8A000000, to, a, b); }
void Emitter::orr(Reg to, Reg a, Reg b) { three(0xAA000000, to, a, b); }
void Emitter::eor(Reg to, Reg a, Reg b) { three(0xCA000000, to, a, b); }
void Emitter::mul(Reg to, Reg a, Reg b) { three(0x9B007C00, to, a, b); }
void Emitter::umulh(Reg to, Reg a, Reg b) { three(0x9BC07C00, to, a, b); }
void Emitter::smulh(Reg to, Reg a, Reg b) { three(0x9B407C00, to, a, b); }
void Emitter::udiv(Reg to, Reg a, Reg b) { three(0x9AC00800, to, a, b); }
void Emitter::sdiv(Reg to, Reg a, Reg b) { three(0x9AC00C00, to, a, b); }

void Emitter::msub(Reg to, Reg a, Reg b, Reg minuend) {
    emit(0x9B008000 | (std::uint32_t{b} << 16) | (std::uint32_t{minuend} << 10) | (std::uint32_t{a} << 5) | to);
}

void Emitter::lslv(Reg to, Reg a, Reg b) { three(0x9AC02000, to, a, b); }
void Emitter::lsrv(Reg to, Reg a, Reg b) { three(0x9AC02400, to, a, b); }
void Emitter::asrv(Reg to, Reg a, Reg b) { three(0x9AC02800, to, a, b); }

void Emitter::lsl(Reg to, Reg a, unsigned count) {
    // ubfm to, a, #(-count mod 64), #(63 - count)
    emit(0xD3400000 | (((64 - count) % 64) << 16) | ((63 - count) << 10) | (std::uint32_t{a} << 5) | to);
}

void Emitter::lsr(Reg to, Reg a, unsigned count) {
    emit(0xD3400000 | (count << 16) | (63U << 10) | (std::uint32_t{a} << 5) | to);
}

void Emitter::asr(Reg to, Reg a, unsigned count) {
    emit(0x93400000 | (count << 16) | (63U << 10) | (std::uint32_t{a} << 5) | to);
}

void Emitter::neg(Reg to, Reg a) { three(0xCB000000, to, zr, a); }
void Emitter::mvn(Reg to, Reg a) { three(0xAA200000, to, zr, a); }
void Emitter::cmp(Reg a, Reg b) { three(0xEB000000, zr, a, b); }

void Emitter::cmp_asr(Reg a, Reg b, unsigned count) {
    emit(0xEB800000 | (std::uint32_t{b} << 16) | (count << 10) | (std::uint32_t{a} << 5) | zr);
}

void Emitter::tst_bit(Reg a, unsigned bit) {
    // ands xzr, a, #(1 << bit): a 64-bit element with one bit set, rotated
    // right by (64 - bit) mod 64.
    emit(0xF2400000 | (((64 - bit) % 64) << 16) | (std::uint32_t{a} << 5) | zr);
}

void Emitter::cset(Reg to, Cond cond) {
    // csinc to, xzr, xzr, invert(cond)
    emit(0x9A9F07E0 | ((static_cast<std::uint32_t>(cond) ^ 1U) << 12) | to);
}

void Emitter::add_immediate(Reg to, Reg from, std::uint32_t value) {
    emit(0x91000000 | (value << 10) | (std::uint32_t{from} << 5) | to);
}

std::size_t Emitter::b() {
    emit(0x14000000);
    return code.size() - 1;
}

std::size_t Emitter::b_cond(Cond cond) {
    emit(0x54000000 | cond);
    return code.size() - 1;
}

std::size_t Emitter::cbz(Reg value) {
    emit(0xB4000000 | value);
    return code.size() - 1;
}

std::size_t Emitter::cbnz(Reg value) {
    emit(0xB5000000 | value);
    return code.size() - 1;
}

bool Emitter::patch(std::size_t at, std::size_t target) {
    const auto offset = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(at);
    auto& word = code[at];
    if ((word & 0xFC000000U) == 0x14000000U) {
        if (offset < -(std::int64_t{1} << 25) || offset >= (std::int64_t{1} << 25)) return false;
        word = 0x14000000U | (static_cast<std::uint32_t>(offset) & 0x03FFFFFFU);
        return true;
    }
    // b.cond, cbz, cbnz: a 19-bit offset at bit 5.
    if (offset < -(std::int64_t{1} << 18) || offset >= (std::int64_t{1} << 18)) return false;
    word = (word & 0xFF00001FU) | ((static_cast<std::uint32_t>(offset) & 0x7FFFFU) << 5);
    return true;
}

void Emitter::blr(Reg target) { emit(0xD63F0000 | (std::uint32_t{target} << 5)); }
void Emitter::ret() { emit(0xD65F03C0); }

namespace {

std::uint32_t pair(std::uint32_t base, Reg a, Reg b, Reg n, std::int32_t offset) {
    const auto imm7 = static_cast<std::uint32_t>(offset / 8) & 0x7FU;
    return base | (imm7 << 15) | (std::uint32_t{b} << 10) | (std::uint32_t{n} << 5) | a;
}

} // namespace

void Emitter::stp_pre(Reg a, Reg b, Reg base, std::int32_t offset) { emit(pair(0xA9800000, a, b, base, offset)); }
void Emitter::ldp_post(Reg a, Reg b, Reg base, std::int32_t offset) { emit(pair(0xA8C00000, a, b, base, offset)); }
void Emitter::stp(Reg a, Reg b, Reg base, std::int32_t offset) { emit(pair(0xA9000000, a, b, base, offset)); }
void Emitter::ldp(Reg a, Reg b, Reg base, std::int32_t offset) { emit(pair(0xA9400000, a, b, base, offset)); }

void Emitter::csel(Reg to, Reg a, Reg b, Cond cond) {
    emit(0x9A800000 | (std::uint32_t{b} << 16) | (std::uint32_t{cond} << 12) | (std::uint32_t{a} << 5) | to);
}

namespace {

// The type field (bits 22-23) of a scalar floating-point instruction: 00
// single, 01 double.
std::uint32_t fp_type(bool single) { return single ? 0 : 0x00400000U; }

} // namespace

void Emitter::fmov_to_fp(bool single, Reg to, Reg from) {
    emit((single ? 0x1E270000U : 0x9E670000U) | (std::uint32_t{from} << 5) | to);
}

void Emitter::fmov_from_fp(bool single, Reg to, Reg from) {
    emit((single ? 0x1E260000U : 0x9E660000U) | (std::uint32_t{from} << 5) | to);
}

void Emitter::fadd(bool single, Reg to, Reg a, Reg b) { three(0x1E202800 | fp_type(single), to, a, b); }
void Emitter::fsub(bool single, Reg to, Reg a, Reg b) { three(0x1E203800 | fp_type(single), to, a, b); }
void Emitter::fmul(bool single, Reg to, Reg a, Reg b) { three(0x1E200800 | fp_type(single), to, a, b); }
void Emitter::fdiv(bool single, Reg to, Reg a, Reg b) { three(0x1E201800 | fp_type(single), to, a, b); }
void Emitter::fcmp(bool single, Reg a, Reg b) { three(0x1E202000 | fp_type(single), 0, a, b); }

void Emitter::fcvt(bool to_single, Reg to, Reg from) {
    emit((to_single ? 0x1E624000U : 0x1E22C000U) | (std::uint32_t{from} << 5) | to);
}

void Emitter::scvtf(bool single, Reg to, Reg from) {
    emit((single ? 0x9E220000U : 0x9E620000U) | (std::uint32_t{from} << 5) | to);
}

void Emitter::fcvtzs(bool single, Reg to, Reg from) {
    emit((single ? 0x9E380000U : 0x9E780000U) | (std::uint32_t{from} << 5) | to);
}

namespace {

// Register use of the generated code (AAPCS64; x18, the platform register,
// is never touched): x21 the context, x19 the register array, x20 the
// initialized flags, x22 the completed blocks; x9 to x12 the scratch
// registers T0 to T3; x16 and x17 temporaries inside one operation;
// x23 to x28 (callee-saved) and x13 to x15 and x4 to x7 (caller-saved,
// stored and reloaded around calls) hold pinned region registers.
constexpr Reg context = 21;
constexpr Reg registers = 19;
constexpr Reg flags = 20;
constexpr Reg done = 22;
constexpr Reg ip0 = 16;
constexpr Reg ip1 = 17;
constexpr Reg pinnable[] = {23, 24, 25, 26, 27, 28, 13, 14, 15, 4, 5, 6, 7};

bool caller_saved(Reg reg) { return reg < 19; }

Reg scratch(Scratch s) { return static_cast<Reg>(9 + static_cast<unsigned>(s)); }

Cond condition(neura::jit::Cond cond) {
    using C = neura::jit::Cond;
    switch (cond) {
    case C::Eq: return eq;
    case C::Ne: return ne;
    case C::ULt: return lo;
    case C::ULe: return ls;
    case C::UGt: return hi;
    case C::UGe: return hs;
    case C::SLt: return lt;
    case C::SLe: return le;
    case C::SGt: return gt;
    case C::SGe: return ge;
    }
    return eq;
}

class Assembler final : public MacroAssembler {
public:
    void bind(Label label) override {
        if (bound_.size() <= label.id) bound_.resize(label.id + 1);
        bound_[label.id] = e_.size();
    }

    void jump(Label label) override { fixups_.emplace_back(e_.b(), label.id); }

    std::size_t pin_capacity() const override { return std::size(pinnable); }

    void pin(const std::vector<std::uint32_t>& pinned) override {
        for (std::size_t i = 0; i < pinned.size() && i < std::size(pinnable); ++i) {
            pinned_.emplace(pinned[i], pinnable[i]);
        }
    }

    void prologue() override {
        e_.stp_pre(29, 30, sp, -96);
        e_.add_immediate(29, sp, 0);
        e_.stp(19, 20, sp, 16);
        e_.stp(21, 22, sp, 32);
        e_.stp(23, 24, sp, 48);
        e_.stp(25, 26, sp, 64);
        e_.stp(27, 28, sp, 80);
        e_.mov(context, 0);
        e_.ldr(registers, context, offsets::registers);
        e_.ldr(flags, context, offsets::initialized);
        e_.movz(done, 0, 0);
        for (const auto& [reg, machine] : pinned_) array_load(machine, reg);
    }

    void epilogue() override {
        flush_pinned();
        e_.str(done, context, offsets::done);
        e_.ldp(27, 28, sp, 80);
        e_.ldp(25, 26, sp, 64);
        e_.ldp(23, 24, sp, 48);
        e_.ldp(21, 22, sp, 32);
        e_.ldp(19, 20, sp, 16);
        e_.ldp_post(29, 30, sp, 96);
        e_.ret();
    }

    void array_load(Reg to, std::uint32_t reg) {
        if (reg < 4096) {
            e_.ldr(to, registers, reg * 8);
        } else {
            e_.mov_immediate(ip1, reg);
            e_.ldr_indexed(to, registers, ip1);
        }
    }

    void array_store(std::uint32_t reg, Reg from) {
        if (reg < 4096) {
            e_.str(from, registers, reg * 8);
        } else {
            e_.mov_immediate(ip1, reg);
            e_.str_indexed(from, registers, ip1);
        }
    }

    void load(Scratch to, std::uint32_t reg) override {
        if (const auto it = pinned_.find(reg); it != pinned_.end()) {
            e_.mov(scratch(to), it->second);
        } else {
            array_load(scratch(to), reg);
        }
    }

    void store(std::uint32_t reg, Scratch from) override {
        if (const auto it = pinned_.find(reg); it != pinned_.end()) {
            e_.mov(it->second, scratch(from));
        } else {
            array_store(reg, scratch(from));
        }
    }

    void set_initialized(std::uint32_t reg) override {
        e_.movz(ip0, 1, 0);
        if (reg < 4096) {
            e_.strb(ip0, flags, reg);
        } else {
            e_.mov_immediate(ip1, reg);
            e_.strb_indexed(ip0, flags, ip1);
        }
    }

    void flush_pinned() override {
        for (const auto& [reg, machine] : pinned_) array_store(reg, machine);
    }

    void move_immediate(Scratch to, std::uint64_t value) override { e_.mov_immediate(scratch(to), value); }
    void move(Scratch to, Scratch from) override {
        if (to != from) e_.mov(scratch(to), scratch(from));
    }

    void alu(Alu op, Scratch to, Scratch from) override {
        const auto d = scratch(to);
        const auto s = scratch(from);
        switch (op) {
        case Alu::Add: e_.add(d, d, s); break;
        case Alu::Sub: e_.sub(d, d, s); break;
        case Alu::And: e_.and_(d, d, s); break;
        case Alu::Or: e_.orr(d, d, s); break;
        case Alu::Xor: e_.eor(d, d, s); break;
        case Alu::Mul: e_.mul(d, d, s); break;
        }
    }

    void shift(Shift op, Scratch to) override {
        if (to == Scratch::T1) throw GenerationError("shift of the count register");
        const auto d = scratch(to);
        const auto count = scratch(Scratch::T1);
        switch (op) {
        case Shift::Left: e_.lslv(d, d, count); break;
        case Shift::LogicalRight: e_.lsrv(d, d, count); break;
        case Shift::ArithmeticRight: e_.asrv(d, d, count); break;
        }
    }

    void shift_immediate(Shift op, Scratch to, unsigned count) override {
        const auto d = scratch(to);
        switch (op) {
        case Shift::Left: e_.lsl(d, d, count); break;
        case Shift::LogicalRight: e_.lsr(d, d, count); break;
        case Shift::ArithmeticRight: e_.asr(d, d, count); break;
        }
    }

    void negate(Scratch value) override { e_.neg(scratch(value), scratch(value)); }
    void bit_not(Scratch value) override { e_.mvn(scratch(value), scratch(value)); }

    void set_if(neura::jit::Cond cond, Scratch to, Scratch a, Scratch b) override {
        e_.cmp(scratch(a), scratch(b));
        e_.cset(scratch(to), condition(cond));
    }

    void branch_if(neura::jit::Cond cond, Scratch a, Scratch b, Label target) override {
        e_.cmp(scratch(a), scratch(b));
        fixups_.emplace_back(e_.b_cond(condition(cond)), target.id);
    }

    void branch_if_zero(Scratch value, Label target) override {
        fixups_.emplace_back(e_.cbz(scratch(value)), target.id);
    }

    void branch_if_nonzero(Scratch value, Label target) override {
        fixups_.emplace_back(e_.cbnz(scratch(value)), target.id);
    }

    void branch_if_bit(Scratch value, unsigned bit, Label target) override {
        e_.tst_bit(scratch(value), bit);
        fixups_.emplace_back(e_.b_cond(ne), target.id);
    }

    void add_or_branch(Overflow kind, Scratch to, Scratch from, Label target) override {
        e_.adds(scratch(to), scratch(to), scratch(from));
        fixups_.emplace_back(e_.b_cond(kind == Overflow::Unsigned ? hs : vs), target.id);
    }

    void subtract_or_branch(Scratch to, Scratch from, Label target) override {
        e_.subs(scratch(to), scratch(to), scratch(from));
        fixups_.emplace_back(e_.b_cond(vs), target.id);
    }

    void multiply_or_branch(Overflow kind, Scratch to, Scratch from, Label target) override {
        const auto d = scratch(to);
        const auto s = scratch(from);
        if (kind == Overflow::Unsigned) {
            // The high half of the unsigned product must be zero.
            e_.umulh(ip0, d, s);
            e_.mul(d, d, s);
            fixups_.emplace_back(e_.cbnz(ip0), target.id);
        } else {
            // The high half of the signed product must be the low half's
            // sign.
            e_.smulh(ip0, d, s);
            e_.mul(d, d, s);
            e_.cmp_asr(ip0, d, 63);
            fixups_.emplace_back(e_.b_cond(ne), target.id);
        }
    }

    void divide(bool is_signed, bool remainder) override {
        const auto a = scratch(Scratch::T0);
        const auto b = scratch(Scratch::T1);
        const auto quotient = remainder ? scratch(Scratch::T2) : a;
        if (is_signed) {
            e_.sdiv(quotient, a, b);
        } else {
            e_.udiv(quotient, a, b);
        }
        if (remainder) e_.msub(a, quotient, b, a);
    }

    // The float operations, on v0 and v1 (scalar instructions only, no
    // fused multiply-add).
    static constexpr Reg v0 = 0;
    static constexpr Reg v1 = 1;

    // to = the canonical quiet NaN when v0 holds a NaN, which the processor
    // leaves with its payload (FPCR.DN is clear in the default
    // environment). fcmp of a NaN is unordered, which sets V.
    void canonicalize(FloatFormat format, Scratch to) {
        const bool single = format == FloatFormat::Binary32;
        e_.fcmp(single, v0, v0);
        e_.mov_immediate(ip0, single ? 0x7FC00000ULL : 0x7FF8000000000000ULL);
        e_.csel(scratch(to), ip0, scratch(to), vs);
    }

    void float_alu(FloatAlu op, FloatFormat format, Scratch to, Scratch from) override {
        const bool single = format == FloatFormat::Binary32;
        e_.fmov_to_fp(single, v0, scratch(to));
        e_.fmov_to_fp(single, v1, scratch(from));
        switch (op) {
        case FloatAlu::Add: e_.fadd(single, v0, v0, v1); break;
        case FloatAlu::Sub: e_.fsub(single, v0, v0, v1); break;
        case FloatAlu::Mul: e_.fmul(single, v0, v0, v1); break;
        case FloatAlu::Div: e_.fdiv(single, v0, v0, v1); break;
        }
        e_.fmov_from_fp(single, scratch(to), v0);
        canonicalize(format, to);
    }

    void float_compare(FloatCond cond, FloatFormat format, Scratch to, Scratch a, Scratch b) override {
        const bool single = format == FloatFormat::Binary32;
        e_.fmov_to_fp(single, v0, scratch(a));
        e_.fmov_to_fp(single, v1, scratch(b));
        e_.fcmp(single, v0, v1);
        // Unordered sets C and V and clears N and Z, so each of these
        // conditions is false then, except ne.
        Cond c = eq;
        switch (cond) {
        case FloatCond::Eq: c = eq; break;
        case FloatCond::Ne: c = ne; break;
        case FloatCond::Lt: c = mi; break;
        case FloatCond::Le: c = ls; break;
        case FloatCond::Gt: c = gt; break;
        case FloatCond::Ge: c = ge; break;
        }
        e_.cset(scratch(to), c);
    }

    void float_convert(FloatFormat to, Scratch value) override {
        const bool to_single = to == FloatFormat::Binary32;
        e_.fmov_to_fp(!to_single, v0, scratch(value));
        e_.fcvt(to_single, v0, v0);
        e_.fmov_from_fp(to_single, scratch(value), v0);
        canonicalize(to, value);
    }

    void float_from_integer(FloatFormat format, Scratch value) override {
        const bool single = format == FloatFormat::Binary32;
        e_.scvtf(single, v0, scratch(value));
        e_.fmov_from_fp(single, scratch(value), v0);
    }

    // fcvtzs saturates (to INT64_MIN below the range, INT64_MAX above it)
    // and gives 0 for NaN; NaN and INT64_MAX, which no truncation is
    // exactly, become INT64_MIN.
    void float_to_integer(FloatFormat format, Scratch value) override {
        const bool single = format == FloatFormat::Binary32;
        const auto d = scratch(value);
        e_.fmov_to_fp(single, v0, d);
        e_.fcvtzs(single, d, v0);
        e_.fcmp(single, v0, v0);
        e_.mov_immediate(ip0, 0x8000000000000000ULL);
        e_.csel(d, ip0, d, vs);
        e_.mvn(ip1, ip0);
        e_.cmp(d, ip1);
        e_.csel(d, ip0, d, eq);
    }

    void load_context(Scratch to, std::uint32_t offset) override { e_.ldr(scratch(to), context, offset); }
    void load_context_byte(Scratch to, std::uint32_t offset) override { e_.ldrb(scratch(to), context, offset); }

    void store_context32(std::uint32_t offset, std::uint32_t value) override {
        e_.mov_immediate(ip0, value);
        e_.str_w(ip0, context, offset);
    }

    void load_indirect(Scratch to, Scratch base) override { e_.ldr(scratch(to), scratch(base), 0); }
    void done_to(Scratch to) override { e_.mov(scratch(to), done); }

    void add_done(std::uint32_t count) override {
        if (count == 0) return;
        if (count < 4096) {
            e_.add_immediate(done, done, count);
        } else {
            e_.mov_immediate(ip0, count);
            e_.add(done, done, ip0);
        }
    }

    void call(std::uint32_t helper, std::uint32_t immediate) override {
        for (const auto& [reg, machine] : pinned_) {
            if (caller_saved(machine)) array_store(reg, machine);
        }
        e_.mov(0, context);
        e_.mov_immediate(1, immediate);
        e_.mov(2, scratch(Scratch::T0));
        e_.mov(3, scratch(Scratch::T1));
        e_.ldr(ip0, context, helper);
        e_.blr(ip0);
        e_.mov(scratch(Scratch::T0), 0);
        for (const auto& [reg, machine] : pinned_) {
            if (caller_saved(machine)) array_load(machine, reg);
        }
    }

    std::vector<std::uint8_t> finish() override {
        for (const auto& [at, id] : fixups_) {
            if (id >= bound_.size() || !bound_[id]) throw GenerationError("unbound label");
            if (!e_.patch(at, *bound_[id])) throw GenerationError("a branch out of range");
        }
        return e_.bytes();
    }

private:
    Emitter e_;
    std::map<std::uint32_t, Reg> pinned_;
    std::vector<std::optional<std::size_t>> bound_;
    std::vector<std::pair<std::size_t, std::uint32_t>> fixups_;
};

} // namespace

} // namespace neura::jit::aarch64

namespace neura::jit {

std::unique_ptr<MacroAssembler> make_aarch64() { return std::make_unique<aarch64::Assembler>(); }

} // namespace neura::jit
