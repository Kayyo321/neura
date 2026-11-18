#pragma once

#include "nvm/providers.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace neura::server {

// What the host tells a request worker on its command line. The worker's
// channel is descriptor 3.
struct WorkerOptions {
    std::filesystem::path image;
    // The groups the host grants beyond http_server and app (and the
    // console, clock and entropy, which every worker has).
    nvm::Capabilities capabilities;
    // Write each execution's host requests to <directory>/<request id>.trace.
    std::optional<std::filesystem::path> record_directory;
    std::uint64_t response_bytes = 8ull * 1024 * 1024;
    // The NVM step limit of the execution (RunLimits::max_steps), or the
    // runner's default.
    std::optional<std::uint64_t> max_steps;
    // The highest execution tier and the native-code mode of the execution
    // (RunLimits::tier and ::jit), or the runner's defaults.
    std::optional<std::uint8_t> tier;
    std::optional<std::string> jit;
};

inline constexpr int worker_channel_descriptor = 3;

// The worker mode of neura-server-host: loads and verifies the image, greets
// the host, waits for one Assign and its request body, runs the image once,
// and returns the process status. Runs no Neura code before the Assign.
int run_request_worker(const WorkerOptions& options);

} // namespace neura::server
