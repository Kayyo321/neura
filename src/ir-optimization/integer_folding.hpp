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

enum class IntegerOperation { Add, Subtract, Multiply, Divide, Remainder };

// Reads an Integer literal whose type has a specified format and whose value
// is in range. Returns nothing for any other node.
std::optional<IntegerConstant> integer_constant(const IrNode& node);

// Returns the result of `left op right` when the language defines one: the
// divisor is nonzero and the result is in the operands' range. Division
// truncates toward zero and the remainder takes the dividend's sign
// (docs/lang/Numbers.md); MIN % -1 is 0. Returns nothing when the operation
// traps at run time, so the trap stays in the program. Never overflows a C++
// integer.
std::optional<IntegerConstant> fold(IntegerOperation op, const IntegerConstant& left,
                                    const IntegerConstant& right);

// Returns `value` converted to `target` when it is in range, and nothing when
// the checked conversion traps.
std::optional<IntegerConstant> convert(const IntegerConstant& value, IntegerFormat target);

// Returns -value when it is in the operand's range.
std::optional<IntegerConstant> negate(const IntegerConstant& value);

IntegerConstant complement(const IntegerConstant& value);
std::optional<IntegerConstant> fold_bits(std::string_view op, const IntegerConstant& left,
                                         const IntegerConstant& right);

// Three-way comparison of two values of the same format: -1, 0, or 1.
int compare(const IntegerConstant& left, const IntegerConstant& right);

// Canonical decimal spelling, as accepted by parse_integer_literal.
std::string spelling(const IntegerConstant& value);

// Folds one Boolean or integer operation on literal operands when it is total.
// This is also used to recognize source-level static constant expressions.
bool fold_scalar_literals(IrNode& node);

} // namespace neura::optimization
