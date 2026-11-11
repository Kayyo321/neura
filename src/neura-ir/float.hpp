#pragma once

#include "circuit/float_format.hpp"
#include "ir.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace neura {

std::optional<FloatFormat> float_format(const IrType& type);
std::optional<IrType> float_bits_type(const IrType& type);

enum class FloatLiteralError { Malformed, Overflow, Underflow };

struct FloatLiteral {
    std::uint64_t bits = 0;
    std::optional<FloatLiteralError> error;
};

// Converts a decimal literal as NeuraIR spells it (optional '-', digits,
// optional fraction, optional exponent) to the nearest value of `format`,
// ties to even, by exact rational arithmetic. A finite literal whose rounded
// value is infinite is an Overflow, and a nonzero literal that rounds to zero
// is an Underflow; both are compile-time errors. `-0.0` is negative zero.
FloatLiteral parse_float_literal(std::string_view text, FloatFormat format);

} // namespace neura
