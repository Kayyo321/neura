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
    s = 0x8, ns = 0x9, l = 0xC, ge = 0xD, le = 0xE, g = 0xF,
};

enum class AluOp : std::uint8_t { add = 0x01, or_ = 0x09, and_ = 0x21, sub = 0x29, xor_ = 0x31, cmp = 0x39 };
enum class ShiftOp : std::uint8_t { shl = 4, shr = 5, sar = 7 };

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

private:
    void byte(std::uint8_t value) { code.push_back(value); }
    void u32(std::uint32_t value);
    void rex(bool w, unsigned reg, unsigned base, bool force = false);
    void memory(unsigned reg, Reg base, std::int32_t displacement);
};

} // namespace neura::jit::x86_64
