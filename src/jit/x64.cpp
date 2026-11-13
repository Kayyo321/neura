#include "x64.hpp"

#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace neura::jit::x64 {

namespace {

[[noreturn]] void fail(const char* message) {
    throw std::logic_error(message);
}

void require(bool condition, const char* message) {
    if (!condition) fail(message);
}

bool fits8(std::int64_t value) {
    return value >= -128 && value <= 127;
}

unsigned scale_bits(unsigned scale) {
    switch (scale) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    default: fail("a scale is 1, 2, 4, or 8");
    }
}

// The multi-byte nops of 1 to 9 bytes that Intel's manuals recommend.
void append_nop(std::vector<std::uint8_t>& out, std::size_t count) {
    static const std::vector<std::uint8_t> patterns[10] = {
        {},
        {0x90},
        {0x66, 0x90},
        {0x0F, 0x1F, 0x00},
        {0x0F, 0x1F, 0x40, 0x00},
        {0x0F, 0x1F, 0x44, 0x00, 0x00},
        {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00},
        {0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00},
        {0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    while (count > 0) {
        const auto chunk = std::min<std::size_t>(count, 9);
        out.insert(out.end(), patterns[chunk].begin(), patterns[chunk].end());
        count -= chunk;
    }
}

void append_u32(std::vector<std::uint8_t>& out, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(bits >> (8 * i)));
}

constexpr std::uint8_t kJmp = 0xFF;  // the `cc` of an unconditional jump

} // namespace

Mem Mem::at(Gpr base, std::int32_t disp) {
    Mem m;
    m.base = static_cast<std::int8_t>(base.n);
    m.disp = disp;
    return m;
}

Mem Mem::at(Gpr base, Gpr index, unsigned scale, std::int32_t disp) {
    require(index.n != rsp.n, "rsp cannot be an index");
    scale_bits(scale);
    Mem m = at(base, disp);
    m.index = static_cast<std::int8_t>(index.n);
    m.scale = static_cast<std::uint8_t>(scale);
    return m;
}

Mem Mem::absolute(std::int32_t disp) {
    Mem m;
    m.disp = disp;
    return m;
}

Mem Mem::absolute(Gpr index, unsigned scale, std::int32_t disp) {
    require(index.n != rsp.n, "rsp cannot be an index");
    scale_bits(scale);
    Mem m;
    m.index = static_cast<std::int8_t>(index.n);
    m.scale = static_cast<std::uint8_t>(scale);
    m.disp = disp;
    return m;
}

Mem Mem::rip_relative(Label label, std::int32_t addend) {
    Mem m;
    m.rip = true;
    m.label = label;
    m.disp = addend;
    return m;
}

// How an instruction is encoded around its ModRM byte.
struct Assembler::Encoding {
    std::uint8_t prefix = 0;  // a mandatory prefix (66, F2, F3) or 0
    bool size16 = false;      // the operand-size prefix
    bool w = false;           // REX.W
    bool reg_byte = false;    // the reg field names a byte register
    bool rm_byte = false;     // the r/m field names a byte register
    std::uint8_t opcode[3] = {};
    unsigned length = 0;
    unsigned tail = 0;        // bytes after the ModRM operand (immediates)

    Encoding(std::initializer_list<std::uint8_t> bytes) {
        for (const auto b : bytes) opcode[length++] = b;
    }
};

Label Assembler::label() {
    labels_.emplace_back();
    return Label{static_cast<std::uint32_t>(labels_.size() - 1)};
}

void Assembler::bind(Label label) {
    require(label.id < labels_.size(), "a label of another assembler");
    auto& info = labels_[label.id];
    require(!info.bound, "a label is bound once");
    info.bound = true;
    info.position = raw_.size();
    info.marks = marks_.size();
}

std::size_t Assembler::offset(Label label) const {
    require(finished_, "offsets exist after finish");
    require(label.id < labels_.size() && labels_[label.id].bound, "an unbound label");
    return labels_[label.id].final_offset;
}

void Assembler::immediate(std::int64_t value, unsigned bytes) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (unsigned i = 0; i < bytes; ++i) byte(static_cast<std::uint8_t>(bits >> (8 * i)));
}

void Assembler::reference(Label target, std::size_t tail, std::int32_t addend) {
    require(target.id < labels_.size(), "a label of another assembler");
    patches_.push_back({raw_.size(), marks_.size(), tail, target.id, addend});
    immediate(0, 4);
}

void Assembler::encode(const Encoding& e, unsigned reg, const Rm& rm) {
    if (e.size16) byte(0x66);
    if (e.prefix != 0) byte(e.prefix);
    unsigned rex = (e.w ? 8U : 0U) | ((reg >> 3) << 2);
    if (rm.memory) {
        const Mem& m = rm.mem;
        require(m.index != static_cast<std::int8_t>(rsp.n), "rsp cannot be an index");
        require(!m.rip || (m.base < 0 && m.index < 0), "a RIP-relative operand has no registers");
        if (m.index >= 0) rex |= (static_cast<unsigned>(m.index) >> 3) << 1;
        if (m.base >= 0) rex |= static_cast<unsigned>(m.base) >> 3;
    } else {
        rex |= rm.reg >> 3U;
    }
    // spl, bpl, sil and dil need a REX prefix; without one the same
    // encodings name ah, ch, dh and bh.
    const bool byte_register = (e.reg_byte && reg >= 4 && reg < 8) ||
                               (e.rm_byte && !rm.memory && rm.reg >= 4 && rm.reg < 8);
    if (rex != 0 || byte_register) byte(static_cast<std::uint8_t>(0x40 | rex));
    for (unsigned i = 0; i < e.length; ++i) byte(e.opcode[i]);

    const unsigned field = (reg & 7U) << 3;
    if (!rm.memory) {
        byte(static_cast<std::uint8_t>(0xC0 | field | (rm.reg & 7U)));
        return;
    }
    const Mem& m = rm.mem;
    if (m.rip) {
        byte(static_cast<std::uint8_t>(0x05 | field));
        reference(m.label, e.tail, m.disp);
        return;
    }
    const unsigned scale = scale_bits(m.scale) << 6;
    if (m.base < 0) {
        // [disp32] or [index * scale + disp32]: a SIB byte whose base is
        // "none" (101 with mod 00).
        const unsigned index = m.index >= 0 ? static_cast<unsigned>(m.index) & 7U : 4U;
        byte(static_cast<std::uint8_t>(0x04 | field));
        byte(static_cast<std::uint8_t>(scale | (index << 3) | 5U));
        immediate(m.disp, 4);
        return;
    }
    const unsigned base = static_cast<unsigned>(m.base) & 7U;
    // rbp and r13 as a base need a displacement even when it is 0.
    const unsigned mod = (m.disp == 0 && base != 5) ? 0U : fits8(m.disp) ? 1U : 2U;
    // rsp and r12 as a base need a SIB byte.
    const bool sib = m.index >= 0 || base == 4;
    byte(static_cast<std::uint8_t>((mod << 6) | field | (sib ? 4U : base)));
    if (sib) {
        const unsigned index = m.index >= 0 ? static_cast<unsigned>(m.index) & 7U : 4U;
        byte(static_cast<std::uint8_t>((m.index >= 0 ? scale : 0U) | (index << 3) | base));
    }
    if (mod == 1) immediate(m.disp, 1);
    if (mod == 2) immediate(m.disp, 4);
}

// ---- raw bytes ----

void Assembler::data8(std::uint8_t value) { byte(value); }
void Assembler::data16(std::uint16_t value) { immediate(value, 2); }
void Assembler::data32(std::uint32_t value) { immediate(value, 4); }
void Assembler::data64(std::uint64_t value) { immediate(static_cast<std::int64_t>(value), 8); }

void Assembler::data(const void* bytes, std::size_t count) {
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    raw_.insert(raw_.end(), p, p + count);
}

void Assembler::nop(std::size_t count) {
    append_nop(raw_, count);
}

void Assembler::align(std::size_t boundary) {
    require(boundary != 0 && (boundary & (boundary - 1)) == 0, "an alignment is a power of two");
    marks_.push_back({MarkKind::Align, 0, 0, raw_.size(), boundary, false});
}

void Assembler::ud2() {
    byte(0x0F);
    byte(0x0B);
}

// ---- moves ----

namespace {

// The encoding of an integer instruction on `size`: the 8-bit form of the
// opcode for size 8, else the other with the size prefix or REX.W. The r/m
// operand of the 8-bit form is a byte register; a caller whose reg field
// holds a register (not an opcode digit) calls `with_register`.
template <typename Encoding>
Encoding sized(Size size, std::initializer_list<std::uint8_t> byte_form, std::initializer_list<std::uint8_t> form) {
    const bool b8 = size == Size::b8;
    Encoding e(b8 ? byte_form : form);
    e.size16 = size == Size::b16;
    e.w = size == Size::b64;
    e.rm_byte = b8;
    return e;
}

// The reg field names a register of the instruction's size.
template <typename Encoding>
Encoding with_register(Encoding e, Size size) {
    e.reg_byte = size == Size::b8;
    return e;
}

// For instructions with no 8-bit form.
void require_wide(Size size) {
    require(size != Size::b8, "this instruction has no 8-bit form");
}

} // namespace

void Assembler::mov(Size size, Gpr to, Gpr from) {
    encode(with_register(sized<Encoding>(size, {0x88}, {0x89}), size), from.n, to);
}

void Assembler::mov(Size size, Gpr to, const Mem& from) {
    encode(with_register(sized<Encoding>(size, {0x8A}, {0x8B}), size), to.n, from);
}

void Assembler::mov(Size size, const Mem& to, Gpr from) {
    encode(with_register(sized<Encoding>(size, {0x88}, {0x89}), size), from.n, to);
}

namespace {

// The immediate of a 8, 16, or 32-bit operation, as bytes: an 8-bit value
// is -128 to 255, a 16-bit value -32768 to 65535.
unsigned immediate_bytes(Size size, std::int32_t value) {
    switch (size) {
    case Size::b8:
        require(value >= -128 && value <= 255, "an 8-bit immediate is -128 to 255");
        return 1;
    case Size::b16:
        require(value >= -32768 && value <= 65535, "a 16-bit immediate is -32768 to 65535");
        return 2;
    default:
        return 4;
    }
}

} // namespace

void Assembler::mov(Size size, Gpr to, Imm32 value) {
    const unsigned bytes = immediate_bytes(size, value.value);
    if (size == Size::b64) {
        auto e = sized<Encoding>(size, {0xC6}, {0xC7});
        e.tail = 4;
        encode(e, 0, to);
    } else {
        // B0+r ib, B8+r iw/id.
        if (size == Size::b16) byte(0x66);
        const unsigned rex = to.n >> 3;
        const bool force = size == Size::b8 && to.n >= 4 && to.n < 8;
        if (rex != 0 || force) byte(static_cast<std::uint8_t>(0x40 | rex));
        byte(static_cast<std::uint8_t>((size == Size::b8 ? 0xB0 : 0xB8) + (to.n & 7U)));
    }
    immediate(value.value, bytes);
}

void Assembler::mov(Size size, const Mem& to, Imm32 value) {
    const unsigned bytes = immediate_bytes(size, value.value);
    auto e = sized<Encoding>(size, {0xC6}, {0xC7});
    e.tail = bytes;
    encode(e, 0, to);
    immediate(value.value, bytes);
}

void Assembler::mov(Gpr to, Imm64 value) {
    byte(static_cast<std::uint8_t>(0x48 | (to.n >> 3)));
    byte(static_cast<std::uint8_t>(0xB8 + (to.n & 7U)));
    immediate(value.value, 8);
}

void Assembler::mov_constant(Gpr to, std::uint64_t value) {
    if (value <= 0xFFFFFFFFULL) {
        mov(Size::b32, to, Imm32{static_cast<std::int32_t>(static_cast<std::uint32_t>(value))});
    } else if (static_cast<std::int64_t>(value) >= INT32_MIN && static_cast<std::int64_t>(value) < 0) {
        mov(Size::b64, to, Imm32{static_cast<std::int32_t>(value)});
    } else {
        mov(to, Imm64{static_cast<std::int64_t>(value)});
    }
}

void Assembler::lea(Size size, Gpr to, const Mem& from) {
    require_wide(size);
    encode(sized<Encoding>(size, {0x8D}, {0x8D}), to.n, from);
}

// ---- arithmetic ----

void Assembler::alu_form(Alu op, Size size, unsigned reg, const Rm& rm, bool store) {
    const auto row = static_cast<std::uint8_t>(static_cast<unsigned>(op) * 8U + (store ? 0U : 2U));
    encode(with_register(sized<Encoding>(size, {row}, {static_cast<std::uint8_t>(row + 1)}), size), reg, rm);
}

void Assembler::alu(Alu op, Size size, Gpr to, Gpr from) {
    alu_form(op, size, from.n, to, true);
}

void Assembler::alu(Alu op, Size size, Gpr to, const Mem& from) {
    alu_form(op, size, to.n, from, false);
}

void Assembler::alu(Alu op, Size size, const Mem& to, Gpr from) {
    alu_form(op, size, from.n, to, true);
}

void Assembler::alu_immediate(Alu op, Size size, const Rm& to, Imm32 value) {
    const auto digit = static_cast<unsigned>(op);
    const bool accumulator = !to.memory && to.reg == 0;
    const unsigned bytes = immediate_bytes(size, value.value);
    // A value that fits a signed byte uses the sign-extended form (83).
    std::int32_t v = value.value;
    if (size == Size::b16 && v > 32767) v -= 65536;
    if (size == Size::b8) {
        if (accumulator) {
            byte(static_cast<std::uint8_t>(digit * 8U + 4U));
            immediate(v, 1);
            return;
        }
        auto e = sized<Encoding>(size, {0x80}, {0x81});
        e.tail = 1;
        encode(e, digit, to);
        immediate(v, 1);
        return;
    }
    if (fits8(v)) {
        auto e = sized<Encoding>(size, {0x83}, {0x83});
        e.tail = 1;
        encode(e, digit, to);
        immediate(v, 1);
        return;
    }
    if (accumulator) {
        // op al/ax/eax/rax, imm: no ModRM byte, one byte shorter than the
        // general form.
        if (size == Size::b16) byte(0x66);
        if (size == Size::b64) byte(0x48);
        byte(static_cast<std::uint8_t>(digit * 8U + 5U));
        immediate(v, bytes);
        return;
    }
    auto e = sized<Encoding>(size, {0x81}, {0x81});
    e.tail = bytes;
    encode(e, digit, to);
    immediate(v, bytes);
}

void Assembler::alu(Alu op, Size size, Gpr to, Imm32 value) {
    alu_immediate(op, size, to, value);
}

void Assembler::alu(Alu op, Size size, const Mem& to, Imm32 value) {
    alu_immediate(op, size, to, value);
}

void Assembler::test(Size size, Gpr a, Gpr b) {
    encode(with_register(sized<Encoding>(size, {0x84}, {0x85}), size), b.n, a);
}

void Assembler::test(Size size, const Mem& a, Gpr b) {
    encode(with_register(sized<Encoding>(size, {0x84}, {0x85}), size), b.n, a);
}

void Assembler::test(Size size, Gpr a, Imm32 value) {
    const unsigned bytes = immediate_bytes(size, value.value);
    if (a.n == 0) {
        // test al/ax/eax/rax, imm: no ModRM byte.
        if (size == Size::b16) byte(0x66);
        if (size == Size::b64) byte(0x48);
        byte(size == Size::b8 ? 0xA8 : 0xA9);
        immediate(value.value, bytes);
        return;
    }
    auto e = sized<Encoding>(size, {0xF6}, {0xF7});
    e.tail = bytes;
    encode(e, 0, a);
    immediate(value.value, bytes);
}

void Assembler::test(Size size, const Mem& a, Imm32 value) {
    const unsigned bytes = immediate_bytes(size, value.value);
    auto e = sized<Encoding>(size, {0xF6}, {0xF7});
    e.tail = bytes;
    encode(e, 0, a);
    immediate(value.value, bytes);
}

void Assembler::imul(Size size, Gpr to, const Rm& from) {
    require_wide(size);
    encode(sized<Encoding>(size, {0}, {0x0F, 0xAF}), to.n, from);
}

void Assembler::imul(Size size, Gpr to, const Rm& from, Imm32 value) {
    require_wide(size);
    std::int32_t v = value.value;
    if (size == Size::b16) {
        immediate_bytes(size, v);
        if (v > 32767) v -= 65536;
    }
    const bool small = fits8(v);
    const unsigned bytes = small ? 1U : (size == Size::b16 ? 2U : 4U);
    auto e = sized<Encoding>(size, {0}, {small ? std::uint8_t{0x6B} : std::uint8_t{0x69}});
    e.tail = bytes;
    encode(e, to.n, from);
    immediate(v, bytes);
}

void Assembler::group_f7(Size size, unsigned digit, const Rm& value) {
    encode(sized<Encoding>(size, {0xF6}, {0xF7}), digit, value);
}

void Assembler::mul(Size size, const Rm& value) { group_f7(size, 4, value); }
void Assembler::div(Size size, const Rm& value) { group_f7(size, 6, value); }
void Assembler::idiv(Size size, const Rm& value) { group_f7(size, 7, value); }
void Assembler::neg(Size size, const Rm& value) { group_f7(size, 3, value); }
void Assembler::not_(Size size, const Rm& value) { group_f7(size, 2, value); }

void Assembler::cdq() {
    byte(0x99);
}

void Assembler::cqo() {
    byte(0x48);
    byte(0x99);
}

void Assembler::shift(Shift op, Size size, const Rm& value, Imm8 count) {
    const auto digit = static_cast<unsigned>(op);
    if (count.value == 1) {
        encode(sized<Encoding>(size, {0xD0}, {0xD1}), digit, value);
        return;
    }
    auto e = sized<Encoding>(size, {0xC0}, {0xC1});
    e.tail = 1;
    encode(e, digit, value);
    byte(count.value);
}

void Assembler::shift_cl(Shift op, Size size, const Rm& value) {
    encode(sized<Encoding>(size, {0xD2}, {0xD3}), static_cast<unsigned>(op), value);
}

void Assembler::bit(Bit op, Size size, const Rm& value, Gpr index) {
    require_wide(size);
    static const std::uint8_t opcodes[] = {0xA3, 0xAB, 0xB3};
    encode(sized<Encoding>(size, {0}, {0x0F, opcodes[static_cast<unsigned>(op)]}), index.n, value);
}

void Assembler::bit(Bit op, Size size, const Rm& value, Imm8 index) {
    require_wide(size);
    auto e = sized<Encoding>(size, {0}, {0x0F, 0xBA});
    e.tail = 1;
    encode(e, 4U + static_cast<unsigned>(op), value);
    byte(index.value);
}

void Assembler::setcc(Cond cc, const Rm& to) {
    Encoding e{0x0F, static_cast<std::uint8_t>(0x90 | static_cast<unsigned>(cc))};
    e.rm_byte = true;
    encode(e, 0, to);
}

void Assembler::cmov(Cond cc, Size size, Gpr to, const Rm& from) {
    require_wide(size);
    encode(sized<Encoding>(size, {0}, {0x0F, static_cast<std::uint8_t>(0x40 | static_cast<unsigned>(cc))}), to.n, from);
}

void Assembler::movzx(Size to_size, Gpr to, Size from_size, const Rm& from) {
    require(from_size == Size::b8 || from_size == Size::b16, "movzx extends 8 or 16 bits");
    require(to_size > from_size, "movzx extends to a wider size");
    auto e = sized<Encoding>(to_size, {0}, {0x0F, from_size == Size::b8 ? std::uint8_t{0xB6} : std::uint8_t{0xB7}});
    e.rm_byte = from_size == Size::b8;
    encode(e, to.n, from);
}

void Assembler::movsx(Size to_size, Gpr to, Size from_size, const Rm& from) {
    require(from_size == Size::b8 || from_size == Size::b16, "movsx extends 8 or 16 bits");
    require(to_size > from_size, "movsx extends to a wider size");
    auto e = sized<Encoding>(to_size, {0}, {0x0F, from_size == Size::b8 ? std::uint8_t{0xBE} : std::uint8_t{0xBF}});
    e.rm_byte = from_size == Size::b8;
    encode(e, to.n, from);
}

void Assembler::movsxd(Gpr to, const Rm& from) {
    Encoding e{0x63};
    e.w = true;
    encode(e, to.n, from);
}

// ---- control flow ----

void Assembler::jump(std::uint8_t cc, Label target) {
    require(target.id < labels_.size(), "a label of another assembler");
    marks_.push_back({MarkKind::Jump, cc, target.id, raw_.size(), 0, false});
}

void Assembler::jmp(Label target) { jump(kJmp, target); }
void Assembler::jcc(Cond cc, Label target) { jump(static_cast<std::uint8_t>(cc), target); }

void Assembler::jmp(const Rm& target) {
    encode(Encoding{0xFF}, 4, target);
}

void Assembler::call(Label target) {
    byte(0xE8);
    reference(target, 0, 0);
}

void Assembler::call(const Rm& target) {
    encode(Encoding{0xFF}, 2, target);
}

void Assembler::ret() { byte(0xC3); }

void Assembler::push(Gpr value) {
    if (value.n >= 8) byte(0x41);
    byte(static_cast<std::uint8_t>(0x50 + (value.n & 7U)));
}

void Assembler::pop(Gpr value) {
    if (value.n >= 8) byte(0x41);
    byte(static_cast<std::uint8_t>(0x58 + (value.n & 7U)));
}

// ---- floats ----

void Assembler::sse(std::uint8_t prefix, std::uint8_t opcode, unsigned reg, const Rm& rm, bool wide) {
    Encoding e{0x0F, opcode};
    e.prefix = prefix;
    e.w = wide;
    encode(e, reg, rm);
}

void Assembler::movq(Xmm to, Gpr from) { sse(0x66, 0x6E, to.n, from, true); }
void Assembler::movq(Gpr to, Xmm from) { sse(0x66, 0x7E, from.n, to, true); }
void Assembler::movd(Xmm to, Gpr from) { sse(0x66, 0x6E, to.n, from); }
void Assembler::movd(Gpr to, Xmm from) { sse(0x66, 0x7E, from.n, to); }

namespace {
std::uint8_t scalar_prefix(Prec prec) {
    return prec == Prec::dbl ? 0xF2 : 0xF3;
}
} // namespace

void Assembler::movs(Prec prec, Xmm to, Xmm from) { sse(scalar_prefix(prec), 0x10, to.n, from); }
void Assembler::movs(Prec prec, Xmm to, const Mem& from) { sse(scalar_prefix(prec), 0x10, to.n, from); }
void Assembler::movs(Prec prec, const Mem& to, Xmm from) { sse(scalar_prefix(prec), 0x11, from.n, to); }

void Assembler::arith(Fp op, Prec prec, Xmm to, const Rm& from) {
    sse(scalar_prefix(prec), static_cast<std::uint8_t>(op), to.n, from);
}

void Assembler::ucomis(Prec prec, Xmm a, const Rm& b) {
    sse(prec == Prec::dbl ? 0x66 : 0x00, 0x2E, a.n, b);
}

void Assembler::andpd(Xmm to, const Rm& from) { sse(0x66, 0x54, to.n, from); }
void Assembler::andnpd(Xmm to, const Rm& from) { sse(0x66, 0x55, to.n, from); }
void Assembler::xorpd(Xmm to, const Rm& from) { sse(0x66, 0x57, to.n, from); }

void Assembler::cvtsi2s(Prec prec, Xmm to, Size from_size, const Rm& from) {
    require(from_size == Size::b32 || from_size == Size::b64, "the integer source is 32 or 64 bits");
    sse(scalar_prefix(prec), 0x2A, to.n, from, from_size == Size::b64);
}

void Assembler::cvtts2si(Prec prec, Size to_size, Gpr to, const Rm& from) {
    require(to_size == Size::b32 || to_size == Size::b64, "the integer result is 32 or 64 bits");
    sse(scalar_prefix(prec), 0x2C, to.n, from, to_size == Size::b64);
}

void Assembler::cvtss2sd(Xmm to, const Rm& from) { sse(0xF3, 0x5A, to.n, from); }
void Assembler::cvtsd2ss(Xmm to, const Rm& from) { sse(0xF2, 0x5A, to.n, from); }

// ---- layout ----

// Computes where every label falls given the marks' current sizes.
// `bases[k]` is the offset of the code after mark k - 1 minus that mark's
// position in `raw_`, so a raw byte at `p` after k marks lands at
// `bases[k] + p`. Returns the size of the code.
std::size_t Assembler::layout(std::vector<std::size_t>& bases) {
    std::size_t position = 0;
    std::size_t cursor = 0;
    bases.assign(marks_.size() + 1, 0);
    for (std::size_t k = 0; k < marks_.size(); ++k) {
        auto& mark = marks_[k];
        position += mark.position - cursor;
        cursor = mark.position;
        std::size_t size = 0;
        if (mark.kind == MarkKind::Jump) {
            size = mark.wide ? (mark.cc == kJmp ? 5 : 6) : 2;
        } else {
            size = (mark.boundary - (position & (mark.boundary - 1))) & (mark.boundary - 1);
        }
        position += size;
        bases[k + 1] = position - mark.position;
    }
    for (auto& info : labels_) {
        if (info.bound) info.final_offset = bases[info.marks] + info.position;
    }
    return position + (raw_.size() - cursor);
}

std::vector<std::uint8_t> Assembler::finish() {
    require(!finished_, "finish is called once");
    for (const auto& patch : patches_) require(labels_[patch.label].bound, "a reference to an unbound label");
    for (const auto& mark : marks_) {
        if (mark.kind == MarkKind::Jump) require(labels_[mark.label].bound, "a jump to an unbound label");
    }

    // Every jump starts short and is widened while one is out of range.
    // Widening only moves later code later (an alignment can only absorb
    // some of it), so the loop ends: each pass widens at least one jump or
    // stops.
    std::vector<std::size_t> bases;
    for (;;) {
        layout(bases);
        bool changed = false;
        for (std::size_t k = 0; k < marks_.size(); ++k) {
            auto& mark = marks_[k];
            if (mark.kind != MarkKind::Jump || mark.wide) continue;
            const auto end = static_cast<std::int64_t>(bases[k + 1] + mark.position);
            const auto target = static_cast<std::int64_t>(labels_[mark.label].final_offset);
            if (!fits8(target - end)) {
                mark.wide = true;
                changed = true;
            }
        }
        if (!changed) break;
    }
    finished_ = true;

    std::vector<std::uint8_t> out;
    out.reserve(raw_.size() + 4 * marks_.size());
    std::size_t cursor = 0;
    for (std::size_t k = 0; k < marks_.size(); ++k) {
        const auto& mark = marks_[k];
        out.insert(out.end(), raw_.begin() + static_cast<std::ptrdiff_t>(cursor),
                   raw_.begin() + static_cast<std::ptrdiff_t>(mark.position));
        cursor = mark.position;
        if (mark.kind == MarkKind::Align) {
            append_nop(out, bases[k + 1] + mark.position - out.size());
            continue;
        }
        const auto target = static_cast<std::int64_t>(labels_[mark.label].final_offset);
        if (mark.wide) {
            if (mark.cc == kJmp) {
                out.push_back(0xE9);
            } else {
                out.push_back(0x0F);
                out.push_back(static_cast<std::uint8_t>(0x80 | mark.cc));
            }
            append_u32(out, static_cast<std::int32_t>(target - static_cast<std::int64_t>(out.size() + 4)));
        } else {
            out.push_back(mark.cc == kJmp ? std::uint8_t{0xEB} : static_cast<std::uint8_t>(0x70 | mark.cc));
            out.push_back(static_cast<std::uint8_t>(target - static_cast<std::int64_t>(out.size() + 1)));
        }
    }
    out.insert(out.end(), raw_.begin() + static_cast<std::ptrdiff_t>(cursor), raw_.end());

    for (const auto& patch : patches_) {
        const auto at = bases[patch.marks] + patch.position;
        const auto target = static_cast<std::int64_t>(labels_[patch.label].final_offset) + patch.addend;
        const auto value = static_cast<std::uint32_t>(target - static_cast<std::int64_t>(at + 4 + patch.tail));
        for (int i = 0; i < 4; ++i) out[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (8 * i));
    }
    return out;
}

} // namespace neura::jit::x64
