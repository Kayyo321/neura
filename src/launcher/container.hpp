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

// Version 2 added the task settings to the policy. A reader accepts both;
// a writer writes the current one.
inline constexpr std::uint32_t container_version = 2;
inline constexpr std::uint32_t max_parallel_workers = 1'024;

struct Policy {
    nvm::Capabilities capabilities;
    // The container records max_steps, max_frames, and max_heap_cells, and
    // since version 2 the task settings: max_tasks, task_quantum,
    // max_queued_messages, max_pending_requests, and parallel_workers. A
    // version 1 container leaves those at the defaults of RunLimits.
    nvm::RunLimits limits;
    bool sealed = false;
};

struct Container {
    // The version the file was written with.
    std::uint32_t version = container_version;
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
