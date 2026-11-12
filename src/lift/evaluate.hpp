#pragma once

#include "checked.hpp"
#include "lowered_block.hpp"

#include <cstdint>
#include <limits>

namespace neura::lift {

// The semantics of one lowered instruction (lowered_block.hpp), shared by
// lift::execute and the executor's region programs (src/nvm/tier2.hpp), so
// that both compute every operation with the same code. Integer and boolean
// operations are inline here. Float operations are out of line in
// lowered_block.cpp, which is compiled without floating-point contraction,
// so no caller's flags can fuse them.

inline std::uint64_t mask_of(unsigned width) {
    return width >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << width) - 1;
}

inline std::int64_t sign_extend(std::uint64_t value, unsigned width) {
    if (width >= 64) {
        return static_cast<std::int64_t>(value);
    }
    const auto sign = std::uint64_t{1} << (width - 1);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

inline std::int64_t signed_min(unsigned width) {
    return width >= 64 ? std::numeric_limits<std::int64_t>::min() : -(std::int64_t{1} << (width - 1));
}

inline std::int64_t signed_max(unsigned width) {
    return width >= 64 ? std::numeric_limits<std::int64_t>::max() : (std::int64_t{1} << (width - 1)) - 1;
}

// The value hash of a scalar: a Boolean (width 1) hashes to 1231 when true
// and 1237 when false; a float first has any NaN replaced by its format's
// canonical quiet NaN; then a 64-bit value is folded to the exclusive or of
// its halves, and a narrower one is extended to 32 bits, with its sign when
// it is a signed integer.
inline std::uint64_t value_hash(std::uint64_t a, unsigned width, bool is_signed, FloatType format) {
    if (width == 1) return (a & 1U) != 0 ? 1231 : 1237;
    if (format != FloatType::None) {
        unsigned exponent = 11;
        unsigned fraction = 52;
        if (format == FloatType::Binary16) {
            exponent = 5;
            fraction = 10;
        } else if (format == FloatType::BFloat16) {
            exponent = 8;
            fraction = 7;
        } else if (format == FloatType::Binary32) {
            exponent = 8;
            fraction = 23;
        }
        const auto exponent_mask = ((std::uint64_t{1} << exponent) - 1) << fraction;
        const auto fraction_mask = (std::uint64_t{1} << fraction) - 1;
        if ((a & exponent_mask) == exponent_mask && (a & fraction_mask) != 0) {
            a = exponent_mask | (std::uint64_t{1} << (fraction - 1));
        }
        is_signed = false;
    }
    if (width >= 64) return (a ^ (a >> 32)) & 0xffffffffU;
    if (is_signed) return static_cast<std::uint64_t>(sign_extend(a, width)) & 0xffffffffU;
    return a & mask_of(width);
}

// Whether `op` is a float operation (evaluated by evaluate_float).
inline bool float_opcode(Opcode op) {
    switch (op) {
    case Opcode::FloatAdd:
    case Opcode::FloatSubtract:
    case Opcode::FloatMultiply:
    case Opcode::FloatDivide:
    case Opcode::FloatNegate:
    case Opcode::FloatEqual:
    case Opcode::FloatNotEqual:
    case Opcode::FloatLess:
    case Opcode::FloatLessEqual:
    case Opcode::FloatGreater:
    case Opcode::FloatGreaterEqual:
    case Opcode::FloatFromInteger:
    case Opcode::FloatToInteger:
    case Opcode::FloatConvert:
        return true;
    default:
        return false;
    }
}

// A float instruction on operand values `a` and `b`. Sets `failed` when the
// operation's check fails (a float-to-integer conversion out of range).
std::uint64_t evaluate_float(const Instruction& in, std::uint64_t a, std::uint64_t b, bool& failed);

// Instruction `in` on operand values `a` and `b` (register values in their
// slot encoding). Returns the result and sets `failed` when the operation's
// check fails, as the circuit's check would: the result is then unspecified.
inline std::uint64_t evaluate(const Instruction& in, std::uint64_t a, std::uint64_t b, bool& failed) {
    const unsigned w = in.width;
    const auto m = mask_of(w);
    std::uint64_t r = 0;
    switch (in.op) {
    case Opcode::Constant:
        r = in.immediate;
        break;
    case Opcode::Add:
        r = (a + b) & m;
        if (in.is_signed) {
            std::int64_t sum = 0;
            failed = !checked_add(sign_extend(a, w), sign_extend(b, w), sum) || sum < signed_min(w) ||
                     sum > signed_max(w);
        } else {
            failed = w >= 64 ? r < a : a + b > m;
        }
        break;
    case Opcode::AddWrapping:
        r = (a + b) & m;
        break;
    case Opcode::SubtractWrapping:
        r = (a - b) & m;
        break;
    case Opcode::Subtract:
        r = (a - b) & m;
        if (in.is_signed) {
            const auto x = sign_extend(a, w);
            const auto y = sign_extend(b, w);
            if (w >= 64) {
                failed = (y < 0 && x > std::numeric_limits<std::int64_t>::max() + y) ||
                         (y > 0 && x < std::numeric_limits<std::int64_t>::min() + y);
            } else {
                const auto difference = x - y;
                failed = difference < signed_min(w) || difference > signed_max(w);
            }
        } else {
            failed = a < b;
        }
        break;
    case Opcode::Negate:
        r = (0 - a) & m;
        failed = in.is_signed ? a == (std::uint64_t{1} << (w - 1)) : a != 0;
        break;
    case Opcode::Multiply:
        r = (a * b) & m;
        if (in.is_signed) {
            std::int64_t product = 0;
            failed = !checked_multiply(sign_extend(a, w), sign_extend(b, w), product) || product < signed_min(w) ||
                     product > signed_max(w);
        } else if (w >= 64) {
            failed = a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a;
        } else {
            failed = a * b > m;
        }
        break;
    case Opcode::Divide:
        if (b == 0) {
            failed = true;
        } else if (in.is_signed) {
            const auto x = sign_extend(a, w);
            const auto y = sign_extend(b, w);
            if (x == signed_min(w) && y == -1) {
                failed = true;
            } else {
                r = static_cast<std::uint64_t>(x / y) & m;
            }
        } else {
            r = a / b;
        }
        break;
    case Opcode::Remainder:
        if (b == 0) {
            failed = true;
        } else if (in.is_signed) {
            const auto y = sign_extend(b, w);
            r = y == -1 ? 0 : static_cast<std::uint64_t>(sign_extend(a, w) % y) & m;
        } else {
            r = a % b;
        }
        break;
    case Opcode::Equal:
        r = a == b ? 1 : 0;
        break;
    case Opcode::NotEqual:
        r = a != b ? 1 : 0;
        break;
    case Opcode::Less:
        r = (in.is_signed ? sign_extend(a, w) < sign_extend(b, w) : a < b) ? 1 : 0;
        break;
    case Opcode::LessEqual:
        r = (in.is_signed ? sign_extend(a, w) <= sign_extend(b, w) : a <= b) ? 1 : 0;
        break;
    case Opcode::Greater:
        r = (in.is_signed ? sign_extend(a, w) > sign_extend(b, w) : a > b) ? 1 : 0;
        break;
    case Opcode::GreaterEqual:
        r = (in.is_signed ? sign_extend(a, w) >= sign_extend(b, w) : a >= b) ? 1 : 0;
        break;
    case Opcode::And:
        r = a & b & 1U;
        break;
    case Opcode::Or:
        r = (a | b) & 1U;
        break;
    case Opcode::Not:
        r = (a ^ 1U) & 1U;
        break;
    case Opcode::BitAnd:
        r = a & b;
        break;
    case Opcode::BitOr:
        r = a | b;
        break;
    case Opcode::BitXor:
        r = a ^ b;
        break;
    case Opcode::BitNot:
        r = ~a & m;
        break;
    case Opcode::ShiftLeft:
    case Opcode::ShiftRight:
        // The count is read as unsigned, so a negative count fails too.
        if (b >= w) {
            failed = true;
        } else if (in.op == Opcode::ShiftLeft) {
            r = (a << b) & m;
        } else {
            r = (in.is_signed ? static_cast<std::uint64_t>(sign_extend(a, w) >> b) : a >> b) & m;
        }
        break;
    case Opcode::ValueHash:
        r = value_hash(a, w, in.is_signed, in.format);
        break;
    case Opcode::Index:
        r = a;
        failed = in.is_signed && ((a >> (w - 1)) & 1U) != 0;
        break;
    case Opcode::ConvertInteger: {
        const unsigned to = in.to_width;
        if (in.is_signed) {
            const auto x = sign_extend(a, w);
            failed = in.to_signed ? x < signed_min(to) || x > signed_max(to)
                                  : x < 0 || static_cast<std::uint64_t>(x) > mask_of(to);
            r = static_cast<std::uint64_t>(x) & mask_of(to);
        } else {
            failed = a > (in.to_signed ? static_cast<std::uint64_t>(signed_max(to)) : mask_of(to));
            r = a & mask_of(to);
        }
        break;
    }
    default:
        r = evaluate_float(in, a, b, failed);
        break;
    }
    return r;
}

} // namespace neura::lift
