#pragma once

#include <cstdint>
#include <vector>

namespace neura::jit::aarch64 {

// A hand-written encoder for the few AArch64 instructions the region code
// generator emits, all on 64-bit X registers unless the name says W.
// Register 31 is XZR or SP as the instruction reads it. tests/jit/ checks
// the encodings against an assembler's.
using Reg = std::uint8_t;
inline constexpr Reg zr = 31;
inline constexpr Reg sp = 31;

enum Cond : std::uint8_t {
    eq = 0, ne = 1, hs = 2, lo = 3, mi = 4, pl = 5, vs = 6, vc = 7,
    hi = 8, ls = 9, ge = 10, lt = 11, gt = 12, le = 13,
};

class Emitter {
public:
    std::vector<std::uint32_t> code;

    std::size_t size() const { return code.size(); }
    std::vector<std::uint8_t> bytes() const;

    // mov to, from (orr to, xzr, from)
    void mov(Reg to, Reg from);
    // ldr/str to, [base, #offset]: offset a multiple of 8 below 32768.
    void ldr(Reg to, Reg base, std::uint32_t offset);
    void str(Reg from, Reg base, std::uint32_t offset);
    // ldr/str to, [base, index, lsl #3]
    void ldr_indexed(Reg to, Reg base, Reg index);
    void str_indexed(Reg from, Reg base, Reg index);
    // ldrb/strb w, [base, #offset]: offset below 4096.
    void ldrb(Reg to, Reg base, std::uint32_t offset);
    void strb(Reg from, Reg base, std::uint32_t offset);
    // strb w, [base, index]
    void strb_indexed(Reg from, Reg base, Reg index);
    // str w, [base, #offset]: offset a multiple of 4 below 16384.
    void str_w(Reg from, Reg base, std::uint32_t offset);
    void movz(Reg to, std::uint16_t value, unsigned shift);
    void movk(Reg to, std::uint16_t value, unsigned shift);
    void movn(Reg to, std::uint16_t value, unsigned shift);
    // The shortest movz/movn + movk sequence for `value`.
    void mov_immediate(Reg to, std::uint64_t value);
    void add(Reg to, Reg a, Reg b);
    void adds(Reg to, Reg a, Reg b);
    void sub(Reg to, Reg a, Reg b);
    void subs(Reg to, Reg a, Reg b);
    void and_(Reg to, Reg a, Reg b);
    void orr(Reg to, Reg a, Reg b);
    void eor(Reg to, Reg a, Reg b);
    void mul(Reg to, Reg a, Reg b);
    void umulh(Reg to, Reg a, Reg b);
    void smulh(Reg to, Reg a, Reg b);
    void udiv(Reg to, Reg a, Reg b);
    void sdiv(Reg to, Reg a, Reg b);
    // to = minuend - a * b
    void msub(Reg to, Reg a, Reg b, Reg minuend);
    void lslv(Reg to, Reg a, Reg b);
    void lsrv(Reg to, Reg a, Reg b);
    void asrv(Reg to, Reg a, Reg b);
    // Shifts by 1 to 63.
    void lsl(Reg to, Reg a, unsigned count);
    void lsr(Reg to, Reg a, unsigned count);
    void asr(Reg to, Reg a, unsigned count);
    void neg(Reg to, Reg a);
    void mvn(Reg to, Reg a);
    void cmp(Reg a, Reg b);
    // cmp a, b, asr #count
    void cmp_asr(Reg a, Reg b, unsigned count);
    // tst a, #(1 << bit)
    void tst_bit(Reg a, unsigned bit);
    void cset(Reg to, Cond cond);
    // add to, from, #value (value below 4096); `from` may be sp.
    void add_immediate(Reg to, Reg from, std::uint32_t value);
    // Branches, with the offset patched later; return their index.
    std::size_t b();
    std::size_t b_cond(Cond cond);
    std::size_t cbz(Reg value);
    std::size_t cbnz(Reg value);
    // Sets the branch at `at` to reach instruction `target`; false when it
    // is out of range.
    bool patch(std::size_t at, std::size_t target);
    void blr(Reg target);
    void ret();
    // stp/ldp a, b, [sp, #offset]! (pre-index), [sp], #offset (post-index),
    // and [sp, #offset]; offset a multiple of 8 in [-512, 504].
    void stp_pre(Reg a, Reg b, Reg base, std::int32_t offset);
    void ldp_post(Reg a, Reg b, Reg base, std::int32_t offset);
    void stp(Reg a, Reg b, Reg base, std::int32_t offset);
    void ldp(Reg a, Reg b, Reg base, std::int32_t offset);

private:
    void emit(std::uint32_t word) { code.push_back(word); }
    void three(std::uint32_t base, Reg to, Reg a, Reg b);
};

} // namespace neura::jit::aarch64
