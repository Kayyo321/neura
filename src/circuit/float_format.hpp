#pragma once

#include <cstdint>

namespace neura {

// An IEEE 754 binary interchange format with round-to-nearest-even
// (docs/lang/Numbers.md): float16 is binary16, float32 binary32, float64
// binary64, and bfloat16 has binary32's exponent with a 7-bit fraction.
//
// The circuit library owns this description so that it builds without the
// compiler; neura-ir/float.hpp adds the IR-level helpers.
struct FloatFormat {
    unsigned exponent_bits;
    unsigned fraction_bits;

    unsigned width() const { return 1 + exponent_bits + fraction_bits; }
    std::uint64_t bias() const { return (std::uint64_t{1} << (exponent_bits - 1)) - 1; }
    // The canonical quiet NaN: sign 0, all exponent bits, and only the
    // quiet bit of the fraction.
    std::uint64_t canonical_nan() const {
        const auto exponent = ((std::uint64_t{1} << exponent_bits) - 1) << fraction_bits;
        return exponent | (std::uint64_t{1} << (fraction_bits - 1));
    }

    bool operator==(const FloatFormat&) const = default;
};

inline constexpr FloatFormat binary16{5, 10};
inline constexpr FloatFormat bfloat16{8, 7};
inline constexpr FloatFormat binary32{8, 23};
inline constexpr FloatFormat binary64{11, 52};

} // namespace neura
