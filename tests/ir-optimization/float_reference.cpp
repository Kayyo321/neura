#include "float_reference.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace neura::testing::floats {
namespace {

std::uint64_t sign_bit(Format format) {
    return std::uint64_t{1} << (format.width() - 1);
}

std::uint64_t infinity_bits(Format format) {
    return ((std::uint64_t{1} << format.exponent_bits) - 1) << format.fraction_bits;
}

} // namespace

bool is_nan(std::uint64_t bits, Format format) {
    const auto exponent = (bits >> format.fraction_bits) & ((std::uint64_t{1} << format.exponent_bits) - 1);
    const auto fraction = bits & ((std::uint64_t{1} << format.fraction_bits) - 1);
    return exponent == (std::uint64_t{1} << format.exponent_bits) - 1 && fraction != 0;
}

double decode(std::uint64_t bits, Format format) {
    const bool negative = (bits & sign_bit(format)) != 0;
    const auto exponent =
        static_cast<int>((bits >> format.fraction_bits) & ((std::uint64_t{1} << format.exponent_bits) - 1));
    const auto fraction = bits & ((std::uint64_t{1} << format.fraction_bits) - 1);
    double magnitude;
    if (exponent == (1 << format.exponent_bits) - 1) {
        magnitude = fraction == 0 ? INFINITY : NAN;
    } else if (exponent == 0) {
        magnitude = std::ldexp(static_cast<double>(fraction),
                               1 - format.bias() - static_cast<int>(format.fraction_bits));
    } else {
        magnitude = std::ldexp(static_cast<double>(fraction | (std::uint64_t{1} << format.fraction_bits)),
                               exponent - format.bias() - static_cast<int>(format.fraction_bits));
    }
    return negative ? -magnitude : magnitude;
}

std::uint64_t encode(double value, Format format) {
    if (std::isnan(value)) {
        return format.canonical_nan();
    }
    const auto sign = std::signbit(value) ? sign_bit(format) : 0;
    const double magnitude = std::fabs(value);
    if (std::isinf(magnitude)) {
        return sign | infinity_bits(format);
    }
    if (magnitude == 0) {
        return sign;
    }
    const int f = static_cast<int>(format.fraction_bits);
    int exp = 0;
    std::frexp(magnitude, &exp);
    const int unbiased = exp - 1;
    const int minimum = 1 - format.bias();
    int scale = (unbiased < minimum ? minimum : unbiased) - f;
    // magnitude / 2^scale has at most f + 1 integer bits; nearbyint rounds
    // ties to even in the default rounding mode.
    double significand = std::nearbyint(std::ldexp(magnitude, -scale));
    if (significand == std::ldexp(1.0, f + 1)) {
        significand /= 2;
        ++scale;
    }
    const auto integer = static_cast<std::uint64_t>(significand);
    if (integer < (std::uint64_t{1} << f)) {
        return sign | integer;
    }
    const auto field = scale + f + format.bias();
    if (field >= (1 << format.exponent_bits) - 1) {
        return sign | infinity_bits(format);
    }
    return sign | static_cast<std::uint64_t>(field) << f | (integer - (std::uint64_t{1} << f));
}

std::uint64_t binary(Op op, std::uint64_t a, std::uint64_t b, Format format) {
    if (format.width() == 32) {
        float x = 0;
        float y = 0;
        auto ua = static_cast<std::uint32_t>(a);
        auto ub = static_cast<std::uint32_t>(b);
        std::memcpy(&x, &ua, 4);
        std::memcpy(&y, &ub, 4);
        float r = 0;
        switch (op) {
        case Op::Add: r = x + y; break;
        case Op::Subtract: r = x - y; break;
        case Op::Multiply: r = x * y; break;
        case Op::Divide: r = x / y; break;
        }
        if (std::isnan(r)) {
            return format.canonical_nan();
        }
        std::uint32_t bits = 0;
        std::memcpy(&bits, &r, 4);
        return bits;
    }
    const double x = decode(a, format);
    const double y = decode(b, format);
    double r = 0;
    switch (op) {
    case Op::Add: r = x + y; break;
    case Op::Subtract: r = x - y; break;
    case Op::Multiply: r = x * y; break;
    case Op::Divide: r = x / y; break;
    }
    if (format.width() == 64) {
        if (std::isnan(r)) {
            return format.canonical_nan();
        }
        std::uint64_t bits = 0;
        std::memcpy(&bits, &r, 8);
        return bits;
    }
    return encode(r, format);
}

bool compare(Compare op, std::uint64_t a, std::uint64_t b, Format format) {
    const double x = decode(a, format);
    const double y = decode(b, format);
    switch (op) {
    case Compare::Equal: return x == y;
    case Compare::NotEqual: return x != y;
    case Compare::Less: return x < y;
    case Compare::LessEqual: return x <= y;
    case Compare::Greater: return x > y;
    case Compare::GreaterEqual: return x >= y;
    }
    return false;
}

std::uint64_t convert(std::uint64_t bits, Format from, Format to) {
    return encode(decode(bits, from), to);
}

namespace {

// 64 bits hold every product and cross-multiplication for formats of at most
// 8 lanes: e4m3 values scale to below 2^18 and the largest product below 2^54.
using Wide = std::int64_t;

// A finite value as numerator / 2^shift with a common shift for the format.
struct Exact {
    bool negative;
    Wide numerator;
};

int common_shift(Format format) {
    return format.bias() - 1 + static_cast<int>(format.fraction_bits);
}

Exact exact(std::uint64_t bits, Format format) {
    const bool negative = (bits & sign_bit(format)) != 0;
    const auto exponent = static_cast<int>((bits >> format.fraction_bits) & ((1U << format.exponent_bits) - 1));
    const auto fraction = static_cast<Wide>(bits & ((std::uint64_t{1} << format.fraction_bits) - 1));
    // value = significand * 2^(exponent - bias - f); scaled by 2^shift.
    const Wide significand = exponent == 0 ? fraction : fraction | (Wide{1} << format.fraction_bits);
    const int power = (exponent == 0 ? 1 : exponent) - 1;
    return {negative, significand << power};
}

} // namespace

std::uint64_t exact_binary(Op op, std::uint64_t a, std::uint64_t b, Format format) {
    if (format.width() > 8) {
        throw std::invalid_argument("exact_binary handles formats of at most 8 lanes");
    }
    const auto inf_bits = infinity_bits(format);
    const auto magnitude_mask = sign_bit(format) - 1;
    const bool a_nan = is_nan(a, format);
    const bool b_nan = is_nan(b, format);
    const bool a_inf = (a & magnitude_mask) == inf_bits;
    const bool b_inf = (b & magnitude_mask) == inf_bits;
    const bool a_zero = (a & magnitude_mask) == 0;
    const bool b_zero = (b & magnitude_mask) == 0;
    bool a_neg = (a & sign_bit(format)) != 0;
    bool b_neg = (b & sign_bit(format)) != 0;
    if (op == Op::Subtract) {
        b_neg = !b_neg;
        op = Op::Add;
    }
    const auto with_sign = [&](bool negative, std::uint64_t magnitude) {
        return (negative ? sign_bit(format) : 0) | magnitude;
    };
    if (a_nan || b_nan) {
        return format.canonical_nan();
    }
    switch (op) {
    case Op::Add:
        if (a_inf && b_inf) {
            return a_neg == b_neg ? with_sign(a_neg, inf_bits) : format.canonical_nan();
        }
        if (a_inf || b_inf) {
            return with_sign(a_inf ? a_neg : b_neg, inf_bits);
        }
        break;
    case Op::Multiply:
        if ((a_inf && b_zero) || (a_zero && b_inf)) {
            return format.canonical_nan();
        }
        if (a_inf || b_inf) {
            return with_sign(a_neg != b_neg, inf_bits);
        }
        break;
    case Op::Divide:
        if ((a_zero && b_zero) || (a_inf && b_inf)) {
            return format.canonical_nan();
        }
        if (a_inf || b_zero) {
            return with_sign(a_neg != b_neg, inf_bits);
        }
        if (b_inf || a_zero) {
            return with_sign(a_neg != b_neg, 0);
        }
        break;
    default:
        break;
    }

    // The exact result as a signed rational numerator / denominator, in units
    // of 2^-(2 * shift) so that products stay integral.
    const auto x = exact(a, format);
    const auto y = exact(b, format);
    const int shift = common_shift(format);
    Wide numerator = 0;
    Wide denominator = Wide{1} << (2 * shift);
    bool negative = false;
    switch (op) {
    case Op::Add: {
        const Wide sx = (a_neg ? -x.numerator : x.numerator) << shift;
        const Wide sy = (b_neg ? -y.numerator : y.numerator) << shift;
        const Wide sum = sx + sy;
        negative = sum < 0;
        numerator = negative ? -sum : sum;
        if (sum == 0) {
            return with_sign(a_neg && b_neg, 0);
        }
        break;
    }
    case Op::Multiply:
        numerator = x.numerator * y.numerator;
        negative = a_neg != b_neg;
        break;
    case Op::Divide:
        // (x / 2^shift) / (y / 2^shift) = x / y = x * 2^(2 shift) / (y * 2^(2 shift))
        numerator = x.numerator << (2 * shift);
        denominator = y.numerator << (2 * shift);
        negative = a_neg != b_neg;
        break;
    default:
        break;
    }
    if (numerator == 0) {
        return with_sign(negative, 0);
    }

    // Nearest finite value by search; beyond the largest finite value by at
    // least half an ulp of it the result is infinite.
    std::uint64_t best = 0;
    Wide best_numerator = 0;
    Wide best_denominator = 1;
    bool found = false;
    std::uint64_t largest = 0;
    for (std::uint64_t candidate = 0; candidate < inf_bits; ++candidate) {
        const auto value = exact(candidate, format);
        largest = candidate;
        // |result - value| as a fraction with denominator `denominator * 2^shift`.
        const Wide scaled = value.numerator * denominator;
        const Wide target = numerator << shift;
        const Wide distance = scaled > target ? scaled - target : target - scaled;
        if (!found || distance * best_denominator < best_numerator) {
            best = candidate;
            best_numerator = distance;
            best_denominator = 1;
            found = true;
        } else if (distance * best_denominator == best_numerator && (candidate & 1U) == 0) {
            best = candidate;
        }
    }
    // Overflow: the result reaches max + ulp(max) / 2 (the next power of two
    // beyond max would be the next value).
    const auto max = exact(largest, format);
    const auto previous = exact(largest - 1, format);
    const Wide half_ulp_twice = max.numerator - previous.numerator;  // ulp, in units of 2^-shift
    const Wide threshold = 2 * max.numerator + half_ulp_twice;       // 2 * (max + ulp/2)
    if ((numerator << shift) * 2 >= threshold * denominator) {
        return with_sign(negative, inf_bits);
    }
    return with_sign(negative, best);
}

} // namespace neura::testing::floats
