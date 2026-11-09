#include "integer_folding.hpp"

#include <charconv>
#include <limits>

namespace neura::optimization {
namespace {

using Signed = std::int64_t;
using Unsigned = std::uint64_t;
constexpr Signed signed_min = std::numeric_limits<Signed>::min();
constexpr Signed signed_max = std::numeric_limits<Signed>::max();
constexpr Unsigned unsigned_max = std::numeric_limits<Unsigned>::max();

Signed signed_upper(IntegerFormat format) {
    return format.bits == 64 ? signed_max : (Signed{1} << (format.bits - 1)) - 1;
}

Signed signed_lower(IntegerFormat format) {
    return -signed_upper(format) - 1;
}

Unsigned unsigned_upper(IntegerFormat format) {
    return format.bits == 64 ? unsigned_max : (Unsigned{1} << format.bits) - 1;
}

Unsigned bits_of(const IntegerConstant& value) {
    const auto bits = value.format.is_signed ? static_cast<Unsigned>(value.signed_value) : value.unsigned_value;
    return bits & unsigned_upper(value.format);
}

IntegerConstant from_bits(Unsigned bits, IntegerFormat format) {
    const auto mask = unsigned_upper(format);
    bits &= mask;
    if (!format.is_signed) {
        return {format, 0, bits};
    }
    if ((bits >> (format.bits - 1)) != 0) {
        bits |= ~mask;
    }
    return {format, static_cast<Signed>(bits), 0};
}

std::optional<IntegerConstant> make_signed(IntegerFormat format, Signed value) {
    if (value < signed_lower(format) || value > signed_upper(format)) {
        return {};
    }
    return IntegerConstant{format, value, 0};
}

std::optional<IntegerConstant> make_unsigned(IntegerFormat format, Unsigned value) {
    if (value > unsigned_upper(format)) {
        return {};
    }
    return IntegerConstant{format, 0, value};
}

// Each helper declines before any C++ operation that could overflow.
std::optional<Signed> signed_result(IntegerOperation op, Signed a, Signed b) {
    switch (op) {
    case IntegerOperation::Add:
        if ((b > 0 && a > signed_max - b) || (b < 0 && a < signed_min - b)) return {};
        return a + b;
    case IntegerOperation::Subtract:
        if ((b < 0 && a > signed_max + b) || (b > 0 && a < signed_min + b)) return {};
        return a - b;
    case IntegerOperation::Multiply:
        if (a == 0 || b == 0) return Signed{0};
        if (a > 0) {
            if (b > 0 ? a > signed_max / b : b < signed_min / a) return {};
        } else if (b > 0 ? a < signed_min / b : a < signed_max / b) {
            return {};
        }
        return a * b;
    case IntegerOperation::Divide:
        // Division by zero traps, and signed_min / -1 is not representable.
        // C++ division truncates toward zero, as Neura's does.
        if (b == 0 || (a == signed_min && b == -1)) return {};
        return a / b;
    case IntegerOperation::Remainder:
        // signed_min % -1 is 0 in Neura but undefined in C++.
        if (b == 0) return {};
        if (b == -1) return Signed{0};
        return a % b;
    }
    return {};
}

std::optional<Unsigned> unsigned_result(IntegerOperation op, Unsigned a, Unsigned b) {
    switch (op) {
    case IntegerOperation::Add:
        if (a > unsigned_max - b) return {};
        return a + b;
    case IntegerOperation::Subtract:
        if (a < b) return {};
        return a - b;
    case IntegerOperation::Multiply:
        if (a != 0 && b > unsigned_max / a) return {};
        return a * b;
    case IntegerOperation::Divide:
        if (b == 0) return {};
        return a / b;
    case IntegerOperation::Remainder:
        if (b == 0) return {};
        return a % b;
    }
    return {};
}

} // namespace

std::optional<IntegerConstant> integer_constant(const IrNode& node) {
    if (node.op != IrOp::Integer) {
        return {};
    }
    const auto format = specified_integer_format(node.type);
    const auto literal = parse_integer_literal(node.text);
    if (!format || !literal || !representable(*literal, *format)) {
        return {};
    }
    if (!format->is_signed) {
        return IntegerConstant{*format, 0, literal->magnitude};
    }
    // Negating the magnitude in unsigned arithmetic is defined, and the range
    // check above guarantees the result fits in int64.
    const auto value = literal->negative ? static_cast<Signed>(Unsigned{0} - literal->magnitude)
                                         : static_cast<Signed>(literal->magnitude);
    return IntegerConstant{*format, value, 0};
}

std::optional<IntegerConstant> fold(IntegerOperation op, const IntegerConstant& left,
                                    const IntegerConstant& right) {
    if (left.format.bits != right.format.bits || left.format.is_signed != right.format.is_signed) {
        return {};
    }
    if (left.format.is_signed) {
        const auto result = signed_result(op, left.signed_value, right.signed_value);
        return result ? make_signed(left.format, *result) : std::nullopt;
    }
    const auto result = unsigned_result(op, left.unsigned_value, right.unsigned_value);
    return result ? make_unsigned(left.format, *result) : std::nullopt;
}

std::optional<IntegerConstant> convert(const IntegerConstant& value, IntegerFormat target) {
    if (value.format.is_signed && value.signed_value < 0) {
        return target.is_signed ? make_signed(target, value.signed_value) : std::nullopt;
    }
    const auto magnitude = value.format.is_signed ? static_cast<Unsigned>(value.signed_value) : value.unsigned_value;
    if (target.is_signed) {
        return magnitude > static_cast<Unsigned>(signed_upper(target))
                   ? std::nullopt
                   : make_signed(target, static_cast<Signed>(magnitude));
    }
    return make_unsigned(target, magnitude);
}

std::optional<IntegerConstant> negate(const IntegerConstant& value) {
    if (value.format.is_signed) {
        return value.signed_value == signed_min ? std::nullopt
                                                : make_signed(value.format, -value.signed_value);
    }
    return value.unsigned_value == 0 ? std::optional{value} : std::nullopt;
}

IntegerConstant complement(const IntegerConstant& value) {
    return from_bits(~bits_of(value), value.format);
}

std::optional<IntegerConstant> fold_bits(std::string_view op, const IntegerConstant& left,
                                         const IntegerConstant& right) {
    const auto a = bits_of(left);
    const auto b = bits_of(right);
    if (op == "&") return from_bits(a & b, left.format);
    if (op == "|") return from_bits(a | b, left.format);
    if (op == "^") return from_bits(a ^ b, left.format);
    if (b >= left.format.bits || (right.format.is_signed && right.signed_value < 0)) {
        return std::nullopt;
    }
    if (op == "<<") return from_bits(a << b, left.format);
    if (op == ">>") {
        const auto extended = left.format.is_signed && left.signed_value < 0
            ? a | ~unsigned_upper(left.format) : a;
        // Use sign fill explicitly so no C++ shift ever counts to 64.
        const auto fill = left.format.is_signed && left.signed_value < 0 && b != 0
            ? unsigned_max << (64 - b) : 0;
        return from_bits((extended >> b) | fill, left.format);
    }
    return std::nullopt;
}

int compare(const IntegerConstant& left, const IntegerConstant& right) {
    if (left.format.is_signed) {
        return left.signed_value < right.signed_value ? -1 : (left.signed_value > right.signed_value ? 1 : 0);
    }
    return left.unsigned_value < right.unsigned_value ? -1 : (left.unsigned_value > right.unsigned_value ? 1 : 0);
}

std::string spelling(const IntegerConstant& value) {
    char buffer[24];
    const auto [end, error] = value.format.is_signed
        ? std::to_chars(buffer, buffer + sizeof buffer, value.signed_value)
        : std::to_chars(buffer, buffer + sizeof buffer, value.unsigned_value);
    (void)error; // 24 bytes hold any 64-bit decimal value and its sign.
    return std::string(buffer, end);
}

} // namespace neura::optimization
