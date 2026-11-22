#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace neura::nvm::generation_internal {

// Linux UAPI version 1: include/uapi/linux/vmclock-abi.h. Decode fixed
// little-endian byte offsets instead of depending on the build kernel headers.
inline constexpr std::size_t vmclock_size = 112;
struct VmClockObservation {
    bool valid = false;
    std::optional<std::uint64_t> generation;
};
inline VmClockObservation vmclock_generation(std::span<const unsigned char> bytes) {
    const auto read = [&](std::size_t offset, std::size_t width) {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < width; ++i) value |= std::uint64_t{bytes[offset + i]} << (8 * i);
        return value;
    };
    if (bytes.size() < 32 || read(0, 4) != 0x4b4c4356 || read(8, 2) != 1 || read(4, 4) < 32) return {};
    if ((read(24, 8) & (1u << 8)) == 0) return {true, std::nullopt};
    if (bytes.size() < vmclock_size || read(4, 4) < vmclock_size) return {};
    return {true, read(104, 8)};
}

inline bool vmgenid_event(std::string_view message) {
    // NUL-delimited fields, not a substring match on arbitrary event data.
    while (!message.empty()) {
        const auto end = message.find('\0');
        if (message.substr(0, end) == "NEW_VMGENID=1") return true;
        if (end == std::string_view::npos) break;
        message.remove_prefix(end + 1);
    }
    return false;
}

} // namespace neura::nvm::generation_internal
