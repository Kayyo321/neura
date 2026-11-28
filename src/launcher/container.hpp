#pragma once

#include "nvm/machine.hpp"
#include "nvm/providers.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace neura::launcher {

// Version 2 added the task settings to the policy, version 3 the stack cell
// limit (NVR 1.7 storage regions), and version 4 a scoped HTTP client grant.
// An executable without an origin list still uses version 3.
inline constexpr std::uint32_t unscoped_container_version = 3;
inline constexpr std::uint32_t container_version = 4;
inline constexpr std::uint32_t max_parallel_workers = 1'024;

struct Policy {
    nvm::Capabilities capabilities;
    // The container records max_steps, max_frames, and max_heap_cells, since
    // version 2 the task settings: max_tasks, task_quantum,
    // max_queued_messages, max_pending_requests, and parallel_workers, and
    // since version 3 max_stack_cells. An older container leaves what it does
    // not record at the defaults of RunLimits.
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

class CorruptContainerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Recognizes the footer magic at EOF or before a PE certificate table.
// Malformed PE bounds throw CorruptContainerError.
bool is_container(std::span<const std::uint8_t> bytes);

// Validate the footer, checksum, and policy. Image validation is the caller's
// responsibility. The file overload reads only the metadata and payload.
Container read_container(std::span<const std::uint8_t> bytes);
Container read_container(const std::filesystem::path& path);

// A launcher must contain exactly one well-formed descriptor.
std::optional<Descriptor> read_descriptor(std::span<const std::uint8_t> bytes);

// Write a sibling temporary file and replace the destination after all writes
// succeed. A signed PE launcher is copied up to its certificate table, with
// the Security directory entry and PE checksum cleared.
void write_executable(const std::filesystem::path& path, std::span<const std::uint8_t> launcher,
                      std::span<const std::uint8_t> image, const Policy& policy);

} // namespace neura::launcher
