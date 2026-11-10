#pragma once

#include "nvm/machine.hpp"
#include "nvm/providers.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace neura::launcher {

inline constexpr std::uint32_t container_version = 1;

struct Policy {
    nvm::Capabilities capabilities;
    // The container records max_steps, max_frames, and max_heap_cells.
    nvm::RunLimits limits;
    bool sealed = false;
};

struct Container {
    Policy policy;
    std::vector<std::uint8_t> image;
    std::uint64_t launcher_size;
};

struct Descriptor {
    std::string triple;
    std::uint8_t flavor;
    std::uint16_t nvr_major;
    std::uint16_t nvr_minor;
    std::uint32_t container_version;
    std::string toolchain_version;
};

// Recognizes the footer magic. Call read_container to validate the contents.
bool is_container(std::span<const std::uint8_t> bytes);

// Validate the footer, checksum, and policy. Image validation is the
// caller's responsibility. The file overload reads only the footer and payload.
Container read_container(std::span<const std::uint8_t> bytes);
Container read_container(const std::filesystem::path& path);

// A launcher must contain exactly one well-formed descriptor.
std::optional<Descriptor> read_descriptor(std::span<const std::uint8_t> bytes);

// Write a sibling temporary file and replace the destination after all writes
// succeed. The launcher and image spans are copied without modification.
void write_executable(const std::filesystem::path& path, std::span<const std::uint8_t> launcher,
                      std::span<const std::uint8_t> image, const Policy& policy);

} // namespace neura::launcher
