#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neura::jit::x64 {

// A general x86-64 encoder: an operand model (general registers with an
// explicit operand size, XMM registers, memory operands with every
// addressing mode, immediates, labels) and the instructions the tier 3
// back end emits. It is independent of the region code generator's emitter
// (x86_64.hpp). tests/jit/x64_tests.cpp checks every instruction form
// against an assembler's bytes (tests/jit/x64-encodings.inc, from
// scripts/x64-encodings.py).
//
// The bytes depend on nothing but the calls made: no address is read, and
// the output is position-independent apart from the RIP-relative
// displacements it contains, which are relative to the code itself.
//
// A misuse (an operand size the instruction lacks, an index of rsp, an
// unbound label) throws std::logic_error.

// An operand size in bits. The instructions that name a size take it as the
// first argument; a byte register operand of size 8 is al, cl, dl, bl, spl,
// bpl, sil, dil, r8b..r15b: ah..bh are never encoded.
enum class Size : std::uint8_t { b8 = 8, b16 = 16, b32 = 32, b64 = 64 };
inline constexpr Size S8 = Size::b8;
inline constexpr Size S16 = Size::b16;
inline constexpr Size S32 = Size::b32;
inline constexpr Size S64 = Size::b64;

struct Gpr {
    std::uint8_t n;
    constexpr bool operator==(const Gpr& other) const { return n == other.n; }
};
inline constexpr Gpr rax{0}, rcx{1}, rdx{2}, rbx{3}, rsp{4}, rbp{5}, rsi{6}, rdi{7};
inline constexpr Gpr r8{8}, r9{9}, r10{10}, r11{11}, r12{12}, r13{13}, r14{14}, r15{15};

struct Xmm {
    std::uint8_t n;
    constexpr bool operator==(const Xmm& other) const { return n == other.n; }
};
inline constexpr Xmm xmm0{0}, xmm1{1}, xmm2{2}, xmm3{3}, xmm4{4}, xmm5{5}, xmm6{6}, xmm7{7};
inline constexpr Xmm xmm8{8}, xmm9{9}, xmm10{10}, xmm11{11}, xmm12{12}, xmm13{13}, xmm14{14}, xmm15{15};

// Immediates. Instructions that take an Imm32 pick the shortest encoding
// that holds it: a sign-extended byte where one exists.
struct Imm8 {
    std::uint8_t value;
};
struct Imm32 {
    std::int32_t value;
};
struct Imm64 {
    std::int64_t value;
};

// A position in the code, made by Assembler::label and fixed by bind.
struct Label {
    std::uint32_t id = 0xFFFFFFFFU;
};

// A memory operand: [base + index * scale + disp], [index * scale + disp],
// [disp] (absolute, through a SIB byte), or [rip + label + addend].
struct Mem {
    std::int8_t base = -1;   // -1: none
    std::int8_t index = -1;  // -1: none; never rsp
    std::uint8_t scale = 1;  // 1, 2, 4, 8
    std::int32_t disp = 0;   // the addend of a RIP-relative operand
    bool rip = false;
    Label label;

    static Mem at(Gpr base, std::int32_t disp = 0);
    static Mem at(Gpr base, Gpr index, unsigned scale, std::int32_t disp = 0);
    static Mem absolute(std::int32_t disp);
    static Mem absolute(Gpr index, unsigned scale, std::int32_t disp = 0);
    // The address of `label` plus `addend`, relative to the end of the
    // instruction that holds the operand.
    static Mem rip_relative(Label label, std::int32_t addend = 0);
};

// A register or memory operand of an instruction's r/m field.
struct Rm {
    bool memory;
    std::uint8_t reg;
    Mem mem;
    Rm(Gpr r) : memory(false), reg(r.n) {}
    Rm(Xmm r) : memory(false), reg(r.n) {}
    Rm(const Mem& m) : memory(true), reg(0), mem(m) {}
};

// The low nibble of Jcc, SETcc and CMOVcc.
enum class Cond : std::uint8_t {
    o = 0x0, no = 0x1, b = 0x2, ae = 0x3, e = 0x4, ne = 0x5, be = 0x6, a = 0x7,
    s = 0x8, ns = 0x9, p = 0xA, np = 0xB, l = 0xC, ge = 0xD, le = 0xE, g = 0xF,
};
// The opposite condition.
constexpr Cond invert(Cond cc) { return static_cast<Cond>(static_cast<std::uint8_t>(cc) ^ 1U); }

// The group-1 operations: the value is the /digit of the immediate form and
// the opcode row (op * 8) of the register forms.
enum class Alu : std::uint8_t { add = 0, or_ = 1, adc = 2, and_ = 4, sub = 5, xor_ = 6, cmp = 7 };
// The group-2 operations (the /digit of the shift forms).
enum class Shift : std::uint8_t { rol = 0, ror = 1, shl = 4, shr = 5, sar = 7 };
// Bit test and set or reset.
enum class Bit : std::uint8_t { bt, bts, btr };
// Scalar floating-point arithmetic (the second opcode byte).
enum class Fp : std::uint8_t { sqrt = 0x51, add = 0x58, mul = 0x59, sub = 0x5C, div = 0x5E };
// Single or double precision.
enum class Prec : std::uint8_t { single, dbl };

class Assembler {
public:
    // A new label, not yet bound.
    Label label();
    // Binds `label` to the current position. A label is bound once.
    void bind(Label label);

    // The code, with every jump at its final size and every label
    // reference resolved. Call once, after the last instruction. Jumps start
    // short and are widened until none is out of range, so the result is the
    // same for the same calls.
    std::vector<std::uint8_t> finish();
    // The offset of a bound label in the code finish returned.
    std::size_t offset(Label label) const;

    // Raw bytes: constants placed in the code (reached through
    // Mem::rip_relative) and padding.
    void data8(std::uint8_t value);
    void data16(std::uint16_t value);
    void data32(std::uint32_t value);
    void data64(std::uint64_t value);
    void data(const void* bytes, std::size_t count);
    // `count` bytes of multi-byte nops (instruction forms of 1 to 9 bytes).
    void nop(std::size_t count = 1);
    // Multi-byte nops up to the next multiple of `boundary`, a power of two.
    void align(std::size_t boundary);
    void ud2();

    // Moves. Sizes 8, 16, 32, 64.
    void mov(Size size, Gpr to, Gpr from);
    void mov(Size size, Gpr to, const Mem& from);
    void mov(Size size, const Mem& to, Gpr from);
    // A register or memory takes the immediate: for size 64 the immediate is
    // sign-extended from 32 bits. For size 8 the value is -128 to 255, for 16
    // it is -32768 to 65535.
    void mov(Size size, Gpr to, Imm32 value);
    void mov(Size size, const Mem& to, Imm32 value);
    // movabs: a 64-bit immediate.
    void mov(Gpr to, Imm64 value);
    // The shortest way to load `value` into the 64-bit register: mov r32,
    // imm32 (zero-extending), mov r64, simm32, or movabs.
    void mov_constant(Gpr to, std::uint64_t value);
    void lea(Size size, Gpr to, const Mem& from);  // 16, 32, 64

    // add, or, adc, and, sub, xor, cmp.
    void alu(Alu op, Size size, Gpr to, Gpr from);
    void alu(Alu op, Size size, Gpr to, const Mem& from);
    void alu(Alu op, Size size, const Mem& to, Gpr from);
    void alu(Alu op, Size size, Gpr to, Imm32 value);
    void alu(Alu op, Size size, const Mem& to, Imm32 value);

    void test(Size size, Gpr a, Gpr b);
    void test(Size size, const Mem& a, Gpr b);
    void test(Size size, Gpr a, Imm32 value);
    void test(Size size, const Mem& a, Imm32 value);

    // imul r, r/m (sizes 16, 32, 64), and imul r, r/m, imm.
    void imul(Size size, Gpr to, const Rm& from);
    void imul(Size size, Gpr to, const Rm& from, Imm32 value);
    // mul, div, idiv: rdx:rax (edx:eax, dx:ax, ax) by r/m, any size.
    void mul(Size size, const Rm& value);
    void div(Size size, const Rm& value);
    void idiv(Size size, const Rm& value);
    void neg(Size size, const Rm& value);
    void not_(Size size, const Rm& value);
    // edx:eax = eax sign-extended; rdx:rax = rax sign-extended.
    void cdq();
    void cqo();

    // Shifts and rotates by an immediate or by cl.
    void shift(Shift op, Size size, const Rm& value, Imm8 count);
    void shift_cl(Shift op, Size size, const Rm& value);
    // bt, bts, btr on a register or memory operand, sizes 16, 32, 64.
    void bit(Bit op, Size size, const Rm& value, Gpr index);
    void bit(Bit op, Size size, const Rm& value, Imm8 index);

    void setcc(Cond cc, const Rm& to);  // a byte register or memory
    void cmov(Cond cc, Size size, Gpr to, const Rm& from);  // 16, 32, 64

    // The extending moves: `from` is size 8 or 16 (movzx, movsx) and `to`
    // is wider; movsxd sign-extends a 32-bit operand into a 64-bit register.
    void movzx(Size to_size, Gpr to, Size from_size, const Rm& from);
    void movsx(Size to_size, Gpr to, Size from_size, const Rm& from);
    void movsxd(Gpr to, const Rm& from);

    // Control flow. Jumps to a label use rel8 where the final displacement
    // fits and rel32 otherwise; a call is always rel32.
    void jmp(Label target);
    void jcc(Cond cc, Label target);
    void jmp(const Rm& target);   // jmp r/m64
    void call(Label target);
    void call(const Rm& target);  // call r/m64
    void ret();
    void push(Gpr value);
    void pop(Gpr value);

    // Floats. movq and movd move between a general and an XMM register.
    void movq(Xmm to, Gpr from);
    void movq(Gpr to, Xmm from);
    void movd(Xmm to, Gpr from);
    void movd(Gpr to, Xmm from);
    // Loads, stores and register moves of one scalar.
    void movs(Prec prec, Xmm to, Xmm from);
    void movs(Prec prec, Xmm to, const Mem& from);
    void movs(Prec prec, const Mem& to, Xmm from);
    // addsd, subsd, mulsd, divsd, sqrtsd and the ss forms.
    void arith(Fp op, Prec prec, Xmm to, const Rm& from);
    void ucomis(Prec prec, Xmm a, const Rm& b);
    void andpd(Xmm to, const Rm& from);
    void andnpd(Xmm to, const Rm& from);
    void xorpd(Xmm to, const Rm& from);
    // cvtsi2sd and cvtsi2ss from a 32 or 64-bit integer.
    void cvtsi2s(Prec prec, Xmm to, Size from_size, const Rm& from);
    // cvttsd2si and cvttss2si to a 32 or 64-bit integer.
    void cvtts2si(Prec prec, Size to_size, Gpr to, const Rm& from);
    void cvtss2sd(Xmm to, const Rm& from);
    void cvtsd2ss(Xmm to, const Rm& from);

private:
    struct Encoding;
    enum class MarkKind : std::uint8_t { Jump, Align };
    // A variable-size part of the code, at `position` in `raw_`: a jump to a
    // label (`cc` is 0xFF for jmp), or alignment padding.
    struct Mark {
        MarkKind kind;
        std::uint8_t cc;
        std::uint32_t label;
        std::size_t position;
        std::size_t boundary;
        bool wide;
    };
    // A 32-bit displacement in `raw_` to patch: relative to the end of the
    // instruction, `tail` bytes after the displacement.
    struct Patch {
        std::size_t position;
        std::size_t marks;  // marks before it
        std::size_t tail;
        std::uint32_t label;
        std::int32_t addend;
    };
    struct LabelInfo {
        bool bound = false;
        std::size_t position = 0;
        std::size_t marks = 0;
        std::size_t final_offset = 0;
    };

    void encode(const Encoding& encoding, unsigned reg, const Rm& rm);
    void alu_form(Alu op, Size size, unsigned reg, const Rm& rm, bool store);
    void alu_immediate(Alu op, Size size, const Rm& to, Imm32 value);
    void group_f7(Size size, unsigned digit, const Rm& value);
    void sse(std::uint8_t prefix, std::uint8_t opcode, unsigned reg, const Rm& rm, bool wide = false);
    void byte(std::uint8_t value) { raw_.push_back(value); }
    void immediate(std::int64_t value, unsigned bytes);
    void reference(Label target, std::size_t tail, std::int32_t addend);
    void jump(std::uint8_t cc, Label target);
    std::size_t layout(std::vector<std::size_t>& bases);

    std::vector<std::uint8_t> raw_;
    std::vector<Mark> marks_;
    std::vector<Patch> patches_;
    std::vector<LabelInfo> labels_;
    bool finished_ = false;
};

} // namespace neura::jit::x64
