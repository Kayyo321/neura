#pragma once

#include "neura-ir/integer.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace neura::optimization {

// A value of an integer type with a specified width and signedness. Signed
// values are held in `signed_value` and unsigned values in `unsigned_value`.
struct IntegerConstant {
    IntegerFormat format;
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
};

enum class IntegerOperation { Add, Subtract, Multiply, Divide };

// Reads an Integer literal whose type has a specified format and whose value
// is in range. Returns nothing for any other node.
std::optional<IntegerConstant> integer_constant(const IrNode& node);

// Returns the exact mathematical result of `left op right` when it is defined
// by every candidate overflow, division-by-zero, and rounding rule: the
// divisor is nonzero, a division has no remainder, and the result is in the
// operands' range. Returns nothing otherwise. Never overflows a C++ integer.
std::optional<IntegerConstant> fold(IntegerOperation op, const IntegerConstant& left,
                                    const IntegerConstant& right);

// Returns -value when it is in the operand's range.
std::optional<IntegerConstant> negate(const IntegerConstant& value);

// Three-way comparison of two values of the same format: -1, 0, or 1.
int compare(const IntegerConstant& left, const IntegerConstant& right);

// Canonical decimal spelling, as accepted by parse_integer_literal.
std::string spelling(const IntegerConstant& value);

} // namespace neura::optimization
