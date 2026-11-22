#pragma once

#include "nvm/providers.hpp"
#include "nvr/format.hpp"
#include "platform/socket.hpp"
#include "server/worker_wire.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace neura::server {

// The worker's channel on POSIX, where the host makes it descriptor 3.
inline constexpr int worker_channel_descriptor = 3;

// What the host tells a request worker on its command line.
struct WorkerOptions {
    // The worker's end of its channel: descriptor 3 on POSIX; on Windows the
    // inherited socket that --channel=<value> names (W27).
#ifdef _WIN32
    platform::native_socket channel = platform::invalid_socket;
#else
    platform::native_socket channel = worker_channel_descriptor;
#endif
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
    // A test hook (NEURA_SERVER_TEST_REFUSE_ASSIGN on the host): after the
    // greeting the worker refuses its Assign by shutting down the reading
    // side of its channel, and waits until it is killed. It runs nothing.
    bool refuse_assign = false;
};

// An image read, hashed, loaded and verified: everything a worker does
// before its greeting. No Neura code has run.
struct PreparedImage {
    std::vector<std::uint8_t> bytes;
    worker::ImageDigest digest{};
    nvr::Image image;
};

PreparedImage prepare_image(const WorkerOptions& options);

// Greets the host on options.channel, waits for one Assign and its request
// body, runs the image once, and returns the process status. Runs no Neura
// code before the Assign.
int serve_assignment(const WorkerOptions& options, const PreparedImage& prepared);

// The worker mode of neura-server-host (--worker): prepare_image, then
// serve_assignment.
int run_request_worker(const WorkerOptions& options);

} // namespace neura::server
