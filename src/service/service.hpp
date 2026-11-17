#pragma once

// The resident runtime service (docs/design/Service030.md, docs/runtime/Service.md):
// the supervisor and worker of `neurad`, the client of `neura run
// --runtime=...`, and the `neura runtime` commands. The protocol is in
// protocol.hpp and the operating-system helpers in system.hpp.

#include "cli/options.hpp"
#include "protocol.hpp"
#include "system.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace neura::service {

// --- The launch: what a direct run would have had ---------------------------

// The option of a run that the service does not support in 0.3.0
// (`--each`, `--record`, `--replay`, a graph device), or nothing.
std::optional<std::string> unsupported_option(const cli::Options& options);

// The envelope of a launch of `bytes` with these options, from this
// process's environment and settings. The digest and size are of `bytes`.
Envelope make_envelope(const cli::Options& options, std::span<const std::uint8_t> bytes, Lifetime lifetime,
                       Clock::time_point client_start);

// The options of a direct run that the envelope describes.
cli::Options options_of(const Envelope& envelope);

// --- The worker -----------------------------------------------------------------

// `neurad --worker --channel FD --build BUILD --supervisor PID`.
int worker_main(const std::vector<std::string>& arguments);

// A worker as its supervisor sees it.
struct WorkerProcess {
    int pid = -1;
    UniqueFd channel;
    Clock::time_point spawned{};
    bool ready = false;
};

// Starts `neurad --worker` with this environment (NAME=VALUE entries, or
// the supervisor's own when empty) and, for a fresh worker, these limits.
// Throws TransportError when it cannot be started.
// `never_ready` is a test fault: the worker never reports ready.
WorkerProcess spawn_worker(const fs::path& neurad, const std::string& build, const std::vector<std::string>* environment,
                           const std::vector<ResourceLimit>* limits, bool never_ready = false);

// Whether a ready worker started with `worker_environment` can run a
// launch with this envelope: the loader and allocator variables equal, and
// every limit settable from inside. `fresh_possible` is false when even a
// fresh worker cannot be given a setting (not-reproducible).
struct Reuse {
    bool warm = false;
    bool fresh_possible = true;
    std::string reason;
};
Reuse reuse(const Envelope& envelope, const std::vector<std::string>& worker_environment);

// --- The supervisor ---------------------------------------------------------------

// `neurad --foreground --root ROOT [--started-by=client|command]`.
int supervisor_main(const std::vector<std::string>& arguments, const fs::path& self);

// --- The client ---------------------------------------------------------------------

// `neura run --runtime=auto|required`, after the options are read.
int run_with_service(const cli::Options& options, Clock::time_point client_start);

// `neura runtime ...`: the arguments after the command word.
int runtime_command(const std::vector<std::string>& arguments, const fs::path& self);

// The JSON object of `neura capabilities` about the service, for the
// installation of `neura` at `bin`.
std::string capabilities_member(const fs::path& bin);

// Writes the --runtime-report object.
struct Report {
    std::string path;  // direct, service-warm, service-fresh, direct-fallback
    std::optional<std::string> reason;
    std::optional<std::string> application;
    std::optional<std::string> state;
    std::optional<int> status;
    std::vector<std::pair<std::string, std::uint64_t>> times;  // nanoseconds since the client started
};
void write_report(const fs::path& file, const Report& report);

// The exit statuses of the service (Service030.md, "Diagnostics").
inline constexpr int status_not_run = 5;
inline constexpr int status_unknown = 6;

} // namespace neura::service
