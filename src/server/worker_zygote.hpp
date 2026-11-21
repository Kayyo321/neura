#pragma once

#include "server/request_worker.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#ifndef _WIN32
#include <sys/types.h>
#endif
#include <vector>

namespace neura::server {

// The worker zygote (docs/runtime/ServerHost.md, "Worker start"): one
// single-threaded process that reads, hashes, loads and verifies the image
// once, and then forks one request worker per request of the host. Each
// worker is a fresh process that has run no Neura code, as a worker started
// with exec is; it skips only the exec, the image read, the digest and the
// verification. Linux only: the zygote forks with CLONE_PARENT, so every
// worker is a child of the host, which waits for it and kills it as it
// does a worker it started itself. Elsewhere every function refuses (macOS
// and Windows start workers with exec).

// The process identifier of a worker: pid_t on POSIX, a Windows process
// identifier (platform::process_id) on Windows, which has no zygote.
#ifdef _WIN32
using worker_pid = platform::process_id;
#else
using worker_pid = pid_t;
#endif

// True when this build can start workers from a zygote.
bool zygote_supported();

// The zygote mode of neura-server-host (--zygote): the control channel is
// descriptor 3. Each control message is one flags byte with one descriptor,
// the worker's end of its channel; the reply is a 4-byte little-endian
// process identifier, or a negative errno. Returns when the host closes the
// control channel.
int run_worker_zygote(const WorkerOptions& options);

// The host's side of the zygote.
class ZygoteClient {
public:
    // Starts `executable --zygote arguments...` and waits until it has
    // loaded the image. Nothing when it cannot (the reason in `problem`).
    static std::unique_ptr<ZygoteClient> start(const std::filesystem::path& executable,
                                               const std::vector<std::string>& arguments, std::string& problem);
    ~ZygoteClient();
    ZygoteClient(const ZygoteClient&) = delete;
    ZygoteClient& operator=(const ZygoteClient&) = delete;

    static constexpr unsigned char refuse_assign_flag = 1;

    // A worker forked with `channel` (the worker's end, which the caller
    // keeps and closes) as its descriptor 3, or -1. After -1, failed() tells
    // whether the zygote has ended (it is then of no further use) or only
    // that fork failed.
    worker_pid fork_worker(int channel, unsigned char flags);
    bool failed() const { return failed_; }

    worker_pid pid() const { return pid_; }

    // Closes the control channel, kills the zygote and waits for it.
    void stop();

private:
    ZygoteClient(worker_pid pid, int control) : pid_(pid), control_(control) {}

    worker_pid pid_;
    int control_;
    bool failed_ = false;
    std::mutex mutex_;
};

} // namespace neura::server
