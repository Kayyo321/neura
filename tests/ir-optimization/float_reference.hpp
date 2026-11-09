#pragma once

// Reference floating-point semantics for testing the float circuits. Shares no
// code with the compiler.
//
// Two independent references:
// - Brute force for formats of at most 8 lanes: every finite value of the
//   format is enumerated, and an exact rational result (numerator and
//   denominator in 128-bit integers) is rounded by searching for the nearest
//   value, ties to an even fraction.
// - Hardware double precision for formats up to 64 lanes: operands decode
//   exactly to double, the operation runs in double (or float for binary32),
//   and the result is rounded to the format. Rounding twice gives the
//   correctly rounded result because double has at least 2p + 2 significand
//   bits for every narrower format's precision p (binary32: 53 >= 50).

#include <cstdint>

namespace neura::testing::floats {

struct Format {
    unsigned exponent_bits;
    unsigned fraction_bits;

    unsigned width() const { return 1 + exponent_bits + fraction_bits; }
    int bias() const { return (1 << (exponent_bits - 1)) - 1; }
    std::uint64_t canonical_nan() const {
        return ((std::uint64_t{1} << exponent_bits) - 1) << fraction_bits | std::uint64_t{1} << (fraction_bits - 1);
    }
};

enum class Op { Add, Subtract, Multiply, Divide };
enum class Compare { Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual };

bool is_nan(std::uint64_t bits, Format format);

// Exact decoding; every format here is a subset of double.
double decode(std::uint64_t bits, Format format);
// Round to nearest, ties to even; NaN becomes the canonical NaN.
std::uint64_t encode(double value, Format format);

std::uint64_t binary(Op op, std::uint64_t a, std::uint64_t b, Format format);
bool compare(Compare op, std::uint64_t a, std::uint64_t b, Format format);
std::uint64_t convert(std::uint64_t bits, Format from, Format to);

// Brute force over the values of a format of at most 8 lanes.
std::uint64_t exact_binary(Op op, std::uint64_t a, std::uint64_t b, Format format);

} // namespace neura::testing::floats
