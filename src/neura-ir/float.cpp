#include "float.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace neura {
namespace {

// A nonnegative integer of any size, little-endian in 32-bit limbs, with no
// leading zero limbs. Only what exact literal rounding needs.
class BigInt {
public:
    BigInt() = default;
    explicit BigInt(std::uint64_t value) {
        while (value != 0) {
            limbs_.push_back(static_cast<std::uint32_t>(value));
            value >>= 32;
        }
    }

    bool zero() const { return limbs_.empty(); }

    std::size_t bit_length() const {
        if (limbs_.empty()) {
            return 0;
        }
        std::size_t bits = 32 * (limbs_.size() - 1);
        for (auto top = limbs_.back(); top != 0; top >>= 1) {
            ++bits;
        }
        return bits;
    }

    void multiply_add(std::uint32_t factor, std::uint32_t addend) {
        std::uint64_t carry = addend;
        for (auto& limb : limbs_) {
            const auto product = std::uint64_t{limb} * factor + carry;
            limb = static_cast<std::uint32_t>(product);
            carry = product >> 32;
        }
        if (carry != 0) {
            limbs_.push_back(static_cast<std::uint32_t>(carry));
        }
        trim();
    }

    BigInt shifted_left(std::size_t bits) const {
        if (zero()) {
            return {};
        }
        BigInt result;
        result.limbs_.assign(bits / 32, 0);
        const auto offset = bits % 32;
        std::uint32_t carry = 0;
        for (const auto limb : limbs_) {
            result.limbs_.push_back(offset == 0 ? limb : (limb << offset) | carry);
            carry = offset == 0 ? 0 : limb >> (32 - offset);
        }
        if (carry != 0) {
            result.limbs_.push_back(carry);
        }
        return result;
    }

    // Requires *this >= other.
    void subtract(const BigInt& other) {
        std::int64_t borrow = 0;
        for (std::size_t i = 0; i < limbs_.size(); ++i) {
            std::int64_t value = std::int64_t{limbs_[i]} - borrow -
                                 (i < other.limbs_.size() ? std::int64_t{other.limbs_[i]} : 0);
            borrow = value < 0 ? 1 : 0;
            if (value < 0) {
                value += std::int64_t{1} << 32;
            }
            limbs_[i] = static_cast<std::uint32_t>(value);
        }
        trim();
    }

    friend int compare(const BigInt& a, const BigInt& b) {
        if (a.limbs_.size() != b.limbs_.size()) {
            return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
        }
        for (std::size_t i = a.limbs_.size(); i-- > 0;) {
            if (a.limbs_[i] != b.limbs_[i]) {
                return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
            }
        }
        return 0;
    }

private:
    void trim() {
        while (!limbs_.empty() && limbs_.back() == 0) {
            limbs_.pop_back();
        }
    }

    std::vector<std::uint32_t> limbs_;
};

BigInt power_of_ten(std::size_t exponent) {
    BigInt result(1);
    for (std::size_t i = 0; i < exponent; ++i) {
        result.multiply_add(10, 0);
    }
    return result;
}

} // namespace

std::optional<IrType> float_bits_type(const IrType& type) {
    const auto format = float_format(type);
    if (!format) return std::nullopt;
    return IrType{TypeKind::Integer, "uint" + std::to_string(format->width()), no_symbol, {}};
}

std::optional<FloatFormat> float_format(const IrType& type) {
    if (type.kind != TypeKind::Float) {
        return {};
    }
    if (type.name == "float16") return binary16;
    if (type.name == "bfloat16") return bfloat16;
    if (type.name == "float32") return binary32;
    if (type.name == "float64") return binary64;
    return {};
}

FloatLiteral parse_float_literal(std::string_view text, FloatFormat format) {
    FloatLiteral result;
    const auto malformed = [&] {
        result.error = FloatLiteralError::Malformed;
        return result;
    };
    bool negative = false;
    if (text.starts_with('-')) {
        negative = true;
        text.remove_prefix(1);
    }
    std::string digits;
    long long exponent = 0;
    std::size_t i = 0;
    bool any_digit = false;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        digits.push_back(text[i]);
        any_digit = true;
    }
    if (i < text.size() && text[i] == '.') {
        for (++i; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
            digits.push_back(text[i]);
            --exponent;
            any_digit = true;
        }
    }
    if (!any_digit) {
        return malformed();
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool exponent_negative = false;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            exponent_negative = text[i] == '-';
            ++i;
        }
        if (i == text.size()) {
            return malformed();
        }
        long long written = 0;
        for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
            // Larger exponents are clamped; they overflow or underflow anyway.
            written = std::min(written * 10 + (text[i] - '0'), 1'000'000LL);
        }
        exponent += exponent_negative ? -written : written;
    }
    if (i != text.size()) {
        return malformed();
    }

    const auto sign = negative ? std::uint64_t{1} << (format.width() - 1) : 0;
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos) {
        result.bits = sign;
        return result;
    }
    digits.erase(0, first);
    // The decimal exponent of the leading digit bounds the magnitude, so huge
    // exponents are rejected before any arithmetic on them.
    const auto leading = exponent + static_cast<long long>(digits.size()) - 1;
    if (leading > 400) {
        result.error = FloatLiteralError::Overflow;
        return result;
    }
    if (leading < -400) {
        result.error = FloatLiteralError::Underflow;
        return result;
    }

    BigInt numerator;
    for (const char digit : digits) {
        numerator.multiply_add(10, static_cast<std::uint32_t>(digit - '0'));
    }
    BigInt denominator(1);
    if (exponent >= 0) {
        for (long long k = 0; k < exponent; ++k) {
            numerator.multiply_add(10, 0);
        }
    } else {
        denominator = power_of_ten(static_cast<std::size_t>(-exponent));
    }

    // The binary exponent e with 2^e <= numerator / denominator < 2^(e+1).
    long long e = static_cast<long long>(numerator.bit_length()) - static_cast<long long>(denominator.bit_length());
    const auto below = [&](long long candidate) {
        return candidate >= 0
                   ? compare(numerator, denominator.shifted_left(static_cast<std::size_t>(candidate))) < 0
                   : compare(numerator.shifted_left(static_cast<std::size_t>(-candidate)), denominator) < 0;
    };
    if (below(e)) {
        --e;
    }

    const auto fraction = static_cast<long long>(format.fraction_bits);
    const auto bias = static_cast<long long>(format.bias());
    const long long minimum = 1 - bias;
    const auto scale = std::max(e, minimum);
    // q = floor(value * 2^(fraction - scale)), the significand before
    // rounding; the remainder decides the rounding.
    BigInt dividend = numerator;
    BigInt divisor = denominator;
    const auto shift = fraction - scale;
    if (shift >= 0) {
        dividend = dividend.shifted_left(static_cast<std::size_t>(shift));
    } else {
        divisor = divisor.shifted_left(static_cast<std::size_t>(-shift));
    }
    std::uint64_t q = 0;
    for (long long bit = fraction + 2; bit >= 0; --bit) {
        const auto step = divisor.shifted_left(static_cast<std::size_t>(bit));
        if (compare(step, dividend) <= 0) {
            dividend.subtract(step);
            q |= std::uint64_t{1} << bit;
        }
    }
    const auto twice = dividend.shifted_left(1);
    const int order = compare(twice, divisor);
    if (order > 0 || (order == 0 && (q & 1U) != 0)) {
        ++q;
    }
    auto biased = scale + bias;
    if (q == std::uint64_t{1} << (fraction + 1)) {
        q >>= 1;
        ++biased;
    }
    if (q == 0) {
        result.error = FloatLiteralError::Underflow;
        return result;
    }
    const auto field = (q >> fraction) != 0 ? biased : 0;
    if (field >= (1LL << format.exponent_bits) - 1) {
        result.error = FloatLiteralError::Overflow;
        return result;
    }
    const auto fraction_mask = (std::uint64_t{1} << fraction) - 1;
    result.bits = sign | (static_cast<std::uint64_t>(field) << fraction) | (q & fraction_mask);
    return result;
}

} // namespace neura
