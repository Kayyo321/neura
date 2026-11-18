#pragma once

#include "nvm/providers.hpp"

#include <filesystem>
#include <optional>

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
};

inline constexpr int worker_channel_descriptor = 3;

// The worker mode of neura-server-host: loads and verifies the image, greets
// the host, waits for one Assign and its request body, runs the image once,
// and returns the process status. Runs no Neura code before the Assign.
int run_request_worker(const WorkerOptions& options);

} // namespace neura::server
