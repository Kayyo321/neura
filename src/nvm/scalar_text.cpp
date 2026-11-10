#include "scalar_text.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace neura::nvm {
namespace {

struct Decimal {
    std::string digits;
    // The power of ten at the first digit.
    int exponent;
};

Decimal binary_decimal(std::uint64_t significand, int exponent) {
    constexpr std::uint64_t base = 1'000'000'000;
    std::vector<std::uint32_t> groups;
    do {
        groups.push_back(static_cast<std::uint32_t>(significand % base));
        significand /= base;
    } while (significand != 0);

    // m * 2^-n = (m * 5^n) * 10^-n. Multiply by several
    // powers at once while keeping each limb product within uint64.
    const auto multiply = [&](std::uint32_t factor) {
        std::uint64_t carry = 0;
        for (auto& group : groups) {
            const auto value = group * std::uint64_t{factor} + carry;
            group = static_cast<std::uint32_t>(value % base);
            carry = value / base;
        }
        if (carry != 0) groups.push_back(static_cast<std::uint32_t>(carry));
    };
    const auto scale = std::min(exponent, 0);
    if (exponent >= 0) {
        for (; exponent >= 29; exponent -= 29) multiply(1U << 29);
        multiply(1U << exponent);
    } else {
        for (; exponent <= -12; exponent += 12) multiply(244'140'625U); // 5^12
        std::uint32_t remaining = 1;
        for (; exponent < 0; ++exponent) remaining *= 5;
        multiply(remaining);
    }
    auto digits = std::to_string(groups.back());
    for (std::size_t i = groups.size() - 1; i > 0; --i) {
        const auto group = std::to_string(groups[i - 1]);
        digits.append(9 - group.size(), '0');
        digits += group;
    }
    const auto leading = static_cast<int>(digits.size()) - 1 + scale;
    return {std::move(digits), leading};
}

int compare(const Decimal& a, const Decimal& b) {
    if (a.exponent != b.exponent) return a.exponent < b.exponent ? -1 : 1;
    for (std::size_t i = 0; i < std::max(a.digits.size(), b.digits.size()); ++i) {
        const auto left = i < a.digits.size() ? a.digits[i] : '0';
        const auto right = i < b.digits.size() ? b.digits[i] : '0';
        if (left != right) return left < right ? -1 : 1;
    }
    return 0;
}

Decimal shortest(std::uint64_t significand, int binary_exponent, bool close_lower) {
    const auto exact = binary_decimal(significand, binary_exponent);
    const auto lower = binary_decimal(4 * significand - (close_lower ? 1 : 2), binary_exponent - 2);
    const auto upper = binary_decimal(4 * significand + 2, binary_exponent - 2);
    const bool even = (significand & 1U) == 0;
    const auto inside = [&](const Decimal& candidate) {
        const auto low = compare(candidate, lower);
        const auto high = compare(candidate, upper);
        return (low > 0 || (even && low == 0)) && (high < 0 || (even && high == 0));
    };

    std::uint64_t prefix = 0;
    for (std::size_t count = 1; count < exact.digits.size() && count <= 17; ++count) {
        prefix = 10 * prefix + static_cast<unsigned>(exact.digits[count - 1] - '0');
        Decimal down{exact.digits.substr(0, count), exact.exponent};
        const auto rounded = std::to_string(prefix + 1);
        Decimal up{rounded, exact.exponent + (rounded.size() > count ? 1 : 0)};
        const auto next = exact.digits[count];
        const bool round_up = next > '5' || (next == '5' &&
            ((prefix & 1U) != 0 || exact.digits.find_first_not_of('0', count + 1) != std::string::npos));
        if (round_up) {
            if (inside(up)) return up;
            if (inside(down)) return down;
        } else {
            if (inside(down)) return down;
            if (inside(up)) return up;
        }
    }
    return exact;
}

std::string float_text(const nvr::Type& type, std::uint64_t bits) {
    const unsigned fraction = type.kind == nvr::TypeKind::BFloat ? 7 : type.width == 16 ? 10
                                                         : type.width == 32 ? 23 : 52;
    const auto exponent_width = type.width - 1U - fraction;
    const auto exponent_mask = (1U << exponent_width) - 1U;
    const auto field = static_cast<unsigned>((bits >> fraction) & exponent_mask);
    const auto hidden = std::uint64_t{1} << fraction;
    auto significand = bits & (hidden - 1);
    const bool negative = ((bits >> (type.width - 1)) & 1U) != 0;
    const std::string sign = negative ? "-" : "";
    if (field == exponent_mask) return significand == 0 ? sign + "Infinity" : "NaN";
    if (field == 0 && significand == 0) return sign + "0.0";

    const auto bias = static_cast<int>(exponent_mask / 2);
    const int binary_exponent = static_cast<int>(std::max(field, 1U)) - bias - static_cast<int>(fraction);
    const bool close_lower = significand == 0 && field > 1;
    if (field != 0) significand |= hidden;
    auto [digits, exponent] = shortest(significand, binary_exponent, close_lower);
    digits.erase(digits.find_last_not_of('0') + 1);
    if (exponent < -4 || exponent >= 16) {
        return sign + digits.front() + "." + (digits.size() == 1 ? "0" : digits.substr(1)) +
               "e" + std::to_string(exponent);
    }
    if (exponent < 0) {
        return sign + "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
    }
    const auto split = static_cast<std::size_t>(exponent + 1);
    if (split >= digits.size()) {
        return sign + digits + std::string(split - digits.size(), '0') + ".0";
    }
    return sign + digits.substr(0, split) + "." + digits.substr(split);
}

} // namespace

std::string scalar_text(const nvr::Type& type, std::uint64_t bits) {
    switch (type.kind) {
    case nvr::TypeKind::Bool:
        return bits != 0 ? "true" : "false";
    case nvr::TypeKind::Unsigned:
        return std::to_string(bits);
    case nvr::TypeKind::Signed:
        if (((bits >> (type.width - 1)) & 1U) == 0) return std::to_string(bits);
        return "-" + std::to_string(type.width == 64 ? std::uint64_t{0} - bits
                                                     : (std::uint64_t{1} << type.width) - bits);
    case nvr::TypeKind::Float:
    case nvr::TypeKind::BFloat:
        return float_text(type, bits);
    default:
        throw std::logic_error("scalar text requires a bool, integer, or float type");
    }
}

} // namespace neura::nvm
