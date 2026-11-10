#pragma once

#include "ir.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace neura {

// An IEEE 754 binary interchange format with round-to-nearest-even
// (docs/lang/Numbers.md): float16 is binary16, float32 binary32, float64
// binary64, and bfloat16 has binary32's exponent with a 7-bit fraction.
struct FloatFormat {
    unsigned exponent_bits;
    unsigned fraction_bits;

    unsigned width() const { return 1 + exponent_bits + fraction_bits; }
    std::uint64_t bias() const { return (std::uint64_t{1} << (exponent_bits - 1)) - 1; }
    // The canonical quiet NaN: sign 0, all exponent bits, and only the
    // quiet bit of the fraction.
    std::uint64_t canonical_nan() const;

    bool operator==(const FloatFormat&) const = default;
};

inline constexpr FloatFormat binary16{5, 10};
inline constexpr FloatFormat bfloat16{8, 7};
inline constexpr FloatFormat binary32{8, 23};
inline constexpr FloatFormat binary64{11, 52};

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
