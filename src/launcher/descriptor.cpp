#include "container.hpp"

#include "nvr/format.hpp"

#include <algorithm>
#include <string_view>

namespace neura::launcher {
namespace {

constexpr std::string_view descriptor_magic = "NEURA-LAUNCHER-1";

template <class Integer>
bool take_integer(std::span<const std::uint8_t>& bytes, Integer& value) {
    if (bytes.size() < sizeof(Integer)) return false;
    value = 0;
    for (unsigned i = 0; i < sizeof(Integer); ++i) {
        value |= static_cast<Integer>(static_cast<std::uint32_t>(bytes[i]) << (i * 8));
    }
    bytes = bytes.subspan(sizeof(Integer));
    return true;
}

bool take_text(std::span<const std::uint8_t>& bytes, std::string_view& value) {
    std::uint32_t size;
    if (!take_integer(bytes, size) || size == 0 || size > bytes.size()) return false;
    value = {reinterpret_cast<const char*>(bytes.data()), size};
    bytes = bytes.subspan(size);
    return nvr::valid_utf8(value);
}

} // namespace

std::optional<Descriptor> read_descriptor(std::span<const std::uint8_t> bytes) {
    const auto first = std::search(bytes.begin(), bytes.end(), descriptor_magic.begin(), descriptor_magic.end());
    if (first == bytes.end() ||
        std::search(first + 1, bytes.end(), descriptor_magic.begin(), descriptor_magic.end()) != bytes.end()) {
        return std::nullopt;
    }

    auto fields = bytes.subspan(static_cast<std::size_t>(first - bytes.begin()) + descriptor_magic.size());
    std::string_view triple;
    std::string_view toolchain_version;
    Descriptor descriptor;
    if (!take_text(fields, triple) ||
        !take_integer(fields, descriptor.flavor) || descriptor.flavor > 1 ||
        !take_integer(fields, descriptor.nvr_major) || descriptor.nvr_major == 0 ||
        !take_integer(fields, descriptor.nvr_minor) ||
        !take_integer(fields, descriptor.container_version) || descriptor.container_version != container_version ||
        !take_text(fields, toolchain_version)) {
        return std::nullopt;
    }
    descriptor.triple = triple;
    descriptor.toolchain_version = toolchain_version;
    return descriptor;
}

} // namespace neura::launcher
