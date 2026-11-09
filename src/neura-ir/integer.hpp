#pragma once

#include "ir.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace neura {

// Width and signedness of an integer type. Types.md specifies int8..int64
// and uint8..uint64; `int` and `byte` are aliases of int32 and uint8.
struct IntegerFormat {
    unsigned bits;
    bool is_signed;
};

std::optional<IntegerFormat> specified_integer_format(const IrType& type);

constexpr bool is_shift(std::string_view op) {
    return op == "<<" || op == ">>";
}

constexpr bool is_bitwise_binary(std::string_view op) {
    return op == "&" || op == "|" || op == "^" || is_shift(op);
}

// The mathematical value of a decimal integer literal as NeuraIR spells it:
// optional '-', then one or more ASCII digits.
struct IntegerLiteral {
    bool negative = false;
    std::uint64_t magnitude = 0;
};

std::optional<IntegerLiteral> parse_integer_literal(std::string_view text);

// True when `value` is in the range of `format`.
bool representable(const IntegerLiteral& value, IntegerFormat format);

} // namespace neura
