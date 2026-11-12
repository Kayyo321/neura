#pragma once

#include <cstdint>
#include <vector>

namespace neura::jit::x86_64 {

// A hand-written encoder for the few x86-64 instructions the region code
// generator emits. Every operation is 64-bit unless its name says
// otherwise. tests/jit/ checks the encodings against an assembler's.
enum Reg : std::uint8_t {
    rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15,
};

// Condition codes (the low nibble of Jcc and SETcc).
enum CC : std::uint8_t {
    o = 0x0, no = 0x1, b = 0x2, ae = 0x3, e = 0x4, ne = 0x5, be = 0x6, a = 0x7,
    s = 0x8, ns = 0x9, p = 0xA, np = 0xB, l = 0xC, ge = 0xD, le = 0xE, g = 0xF,
};

// The SSE registers the float operations use.
enum Xmm : std::uint8_t { xmm0, xmm1 };

enum class AluOp : std::uint8_t { add = 0x01, or_ = 0x09, and_ = 0x21, sub = 0x29, xor_ = 0x31, cmp = 0x39 };
enum class ShiftOp : std::uint8_t { shl = 4, shr = 5, sar = 7 };
// Scalar SSE arithmetic (the second opcode byte of addsd/addss and so on).
enum class SseOp : std::uint8_t { add = 0x58, mul = 0x59, sub = 0x5C, div = 0x5E };

class Emitter {
public:
    std::vector<std::uint8_t> code;

    std::size_t size() const { return code.size(); }

    void mov(Reg to, Reg from);
    // to = [base + displacement]
    void load(Reg to, Reg base, std::int32_t displacement);
    // [base + displacement] = from
    void store(Reg base, std::int32_t displacement, Reg from);
    // movzx to, byte [base + displacement]
    void load_byte(Reg to, Reg base, std::int32_t displacement);
    // byte [base + displacement] = value
    void store_byte(Reg base, std::int32_t displacement, std::uint8_t value);
    // dword [base + displacement] = value
    void store32(Reg base, std::int32_t displacement, std::uint32_t value);
    // The shortest of mov r32, imm32 (zero-extending), mov r64, simm32, and
    // mov r64, imm64.
    void mov_immediate(Reg to, std::uint64_t value);
    void alu(AluOp op, Reg to, Reg from);
    void add_immediate(Reg to, std::int32_t value);
    void imul(Reg to, Reg from);
    // rdx:rax = rax * from, unsigned
    void mul(Reg from);
    // rax = rdx:rax / from, rdx = the remainder, unsigned and signed
    void div(Reg from);
    void idiv(Reg from);
    // rdx:rax = rax sign-extended
    void cqo();
    void shift_cl(ShiftOp op, Reg value);
    void shift_immediate(ShiftOp op, Reg value, std::uint8_t count);
    void neg(Reg value);
    void not_(Reg value);
    void setcc(CC cc, Reg to);
    // movzx to32, from8
    void movzx_byte(Reg to, Reg from);
    void bt(Reg value, std::uint8_t bit);
    void test(Reg a, Reg b);
    // Jumps with a 32-bit displacement; return the offset of it.
    std::size_t jcc(CC cc);
    std::size_t jmp();
    // Sets the displacement at `at` to reach `target`.
    void patch(std::size_t at, std::size_t target);
    // call [base + displacement]
    void call(Reg base, std::int32_t displacement);
    void push(Reg value);
    void pop(Reg value);
    void ret();

    // SSE2 scalar instructions, on binary64 (sd) or, with `single`,
    // binary32 (ss).
    // movq xmm, r64 / movd xmm, r32
    void movq_to_xmm(Xmm to, Reg from);
    void movd_to_xmm(Xmm to, Reg from);
    // movq r64, xmm / movd r32, xmm (zero-extending)
    void movq_from_xmm(Reg to, Xmm from);
    void movd_from_xmm(Reg to, Xmm from);
    void scalar(SseOp op, bool single, Xmm to, Xmm from);
    // cmpsd/cmpss to, from, predicate: to = all ones when it holds.
    void compare_scalar(bool single, Xmm to, Xmm from, std::uint8_t predicate);
    void ucomis(bool single, Xmm a, Xmm b);
    // cvtsi2sd/cvtsi2ss xmm, r64
    void convert_from_integer(bool single, Xmm to, Reg from);
    // cvttsd2si/cvttss2si r64, xmm
    void truncate_to_integer(bool single, Reg to, Xmm from);
    // cvtss2sd (to_single false) or cvtsd2ss (to_single true) xmm, xmm
    void convert_scalar(bool to_single, Xmm to, Xmm from);

private:
    void byte(std::uint8_t value) { code.push_back(value); }
    void u32(std::uint32_t value);
    void rex(bool w, unsigned reg, unsigned base, bool force = false);
    void memory(unsigned reg, Reg base, std::int32_t displacement);
    // [prefix] [REX] 0F opcode, ModRM with register operands.
    void sse(std::uint8_t prefix, bool w, std::uint8_t opcode, unsigned reg, unsigned rm);
};

} // namespace neura::jit::x86_64
