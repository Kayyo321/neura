#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <new>

namespace neura::nvm::detail {

// An arena is one C++ object. Both its size and distances within it must
// be representable, independently of the guest's cell and handle limits.
inline constexpr auto addressable_bytes = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());

constexpr std::size_t add_elements(std::size_t used, std::size_t added, std::size_t limit) {
    if (used > limit || added > limit - used) throw std::bad_array_new_length();
    return used + added;
}

constexpr std::size_t allocation_bytes(std::size_t count, std::size_t width, std::size_t prefix = 0) {
    if (prefix > addressable_bytes || (width && count > (addressable_bytes - prefix) / width)) {
        throw std::bad_array_new_length();
    }
    return prefix + count * width;
}

constexpr std::size_t grow_capacity(std::size_t current, std::size_t requested, std::size_t initial, std::size_t limit) {
    if (requested > limit) throw std::bad_array_new_length();
    const auto doubled = current > limit / 2 ? limit : current * 2;
    return std::max({requested, doubled, std::min(initial, limit)});
}

} // namespace neura::nvm::detail
