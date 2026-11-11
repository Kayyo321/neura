#pragma once

#include "circuit.hpp"
#include "float_format.hpp"

namespace neura::backend {

// IEEE 754 binary floating point on bit lanes, for any exponent and fraction
// width (docs/lang/Numbers.md). Every operation rounds to nearest, ties to
// even, supports subnormals, signed zeros, and infinities, sets no flags, and
// returns the canonical quiet NaN for every NaN result. Words hold the
// encoding least significant lane first: fraction, then exponent, then sign.
//
// Like the integer gadgets, these circuits fold constant lanes and use only
// ReLU and identity neurons, so they evaluate under graph profile 1.

Word float_add(Circuit& circuit, const Word& a, const Word& b, FloatFormat format);
Word float_subtract(Circuit& circuit, const Word& a, const Word& b, FloatFormat format);
Word float_multiply(Circuit& circuit, const Word& a, const Word& b, FloatFormat format);
Word float_divide(Circuit& circuit, const Word& a, const Word& b, FloatFormat format);

// Flips the sign lane, including a NaN's. Needs no neuron.
Word float_negate(const Word& a);

enum class FloatComparison { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

// IEEE comparison: a NaN operand is unordered, so only != holds, and -0
// equals +0.
Signal float_compare(Circuit& circuit, const Word& a, const Word& b, FloatFormat format, FloatComparison op);

// An integer of a.size() lanes, rounded to the nearest value of `format`.
Word float_from_integer(Circuit& circuit, const Word& a, bool is_signed, FloatFormat format);

// Truncation toward zero to an integer of `width` lanes. `failure` is set for
// a NaN, an infinity, or a value whose truncation is outside the range.
Checked float_to_integer(Circuit& circuit, const Word& a, FloatFormat format, unsigned width, bool is_signed);

// Conversion between formats: exact when `to` can represent the value,
// otherwise rounded; a NaN becomes `to`'s canonical NaN.
Word float_convert(Circuit& circuit, const Word& a, FloatFormat from, FloatFormat to);

} // namespace neura::backend
