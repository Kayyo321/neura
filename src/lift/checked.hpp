#pragma once

#include <cstdint>
#include <limits>

namespace neura::lift {

// Checked signed 64-bit arithmetic, as graph profile 1 defines it: false
// when the exact result is outside the range, and `result` is then unchanged.
inline bool checked_add(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b)) {
        return false;
    }
    result = a + b;
    return true;
}

inline bool checked_multiply(std::int64_t a, std::int64_t b, std::int64_t& result) {
    if (a == 0 || b == 0) {
        result = 0;
        return true;
    }
    const auto magnitude = [](std::int64_t v) {
        return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    };
    const auto ma = magnitude(a);
    const auto mb = magnitude(b);
    if (ma > std::numeric_limits<std::uint64_t>::max() / mb) {
        return false;
    }
    const auto product = ma * mb;
    constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if ((a < 0) != (b < 0)) {
        if (product > max + 1) {
            return false;
        }
        result = product == max + 1 ? std::numeric_limits<std::int64_t>::min() : -static_cast<std::int64_t>(product);
        return true;
    }
    if (product > max) {
        return false;
    }
    result = static_cast<std::int64_t>(product);
    return true;
}

} // namespace neura::lift
