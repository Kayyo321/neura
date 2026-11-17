// The launch envelope from the options of a run, and back
// (docs/design/Service030.md, "Launch envelope").

#include "service.hpp"

#include "util/sha256.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <sys/resource.h>
#include <unistd.h>

extern char** environ;

namespace neura::service {

namespace {

// The environment variables of the mode summary.
constexpr std::string_view mode_variables[] = {
    "NEURA_TIER",      "NEURA_TIER3",     "NEURA_TIER3_AFTER",     "NEURA_TIER_PROGRAMS",
    "NEURA_TIER_EAGER", "NEURA_TIER_CHECK", "NEURA_TIER_CHECK_HEAP", "NEURA_JIT",
};

std::string_view jit_name(jit::Mode mode) {
    switch (mode) {
    case jit::Mode::Off: return "off";
    case jit::Mode::On: return "on";
    case jit::Mode::Auto: return "auto";
    }
    return "auto";
}

// The variables that decide how a process is loaded or allocates: a ready
// worker was loaded with the supervisor's, so it serves only a caller with
// the same.
bool loader_variable(std::string_view entry) {
    const auto name = entry.substr(0, entry.find('='));
    return name.starts_with("LD_") || name.starts_with("DYLD_") || name == "GLIBC_TUNABLES" ||
           name.starts_with("MALLOC_");
}

std::vector<std::string> loader_variables(const std::vector<std::string>& environment) {
    std::vector<std::string> out;
    for (const auto& entry : environment) {
        if (loader_variable(entry)) out.push_back(entry);
    }
    std::ranges::sort(out);
    return out;
}

} // namespace

std::optional<std::string> unsupported_option(const cli::Options& options) {
    if (options.each) return "--each";
    if (options.record) return "--record";
    if (options.replay) return "--replay";
#ifdef NEURA_HAS_GPU
    // A graph device: any mode but off may open one.
    if (options.gpu.required || !options.gpu.mode || *options.gpu.mode != cli::GpuMode::Off) return "a graph device";
#else
    if (options.gpu.required || (options.gpu.mode && *options.gpu.mode != cli::GpuMode::Off)) return "a graph device";
#endif
    return std::nullopt;
}

Envelope make_envelope(const cli::Options& options, std::span<const std::uint8_t> bytes, Lifetime lifetime,
                       Clock::time_point client_start) {
    Envelope envelope;
    util::Sha256 digest;
    digest.update(bytes);
    envelope.digest = digest.finish();
    envelope.image_size = bytes.size();
    envelope.image_path = options.files.front().string();
    envelope.arguments = options.arguments;
    for (char** entry = environ; entry && *entry; ++entry) envelope.environment.emplace_back(*entry);
    for (const auto name : mode_variables) {
        for (const auto& entry : envelope.environment) {
            if (entry.size() > name.size() && entry.compare(0, name.size(), name) == 0 && entry[name.size()] == '=') {
                envelope.mode.push_back(entry);
            }
        }
    }
    envelope.mode.push_back("--tier=" + std::to_string(options.limits.tier));
    envelope.mode.push_back("--jit=" + std::string(jit_name(options.limits.jit)));
    const auto& policy = options.capabilities;
    envelope.fs = policy.fs;
    envelope.net = policy.net;
    envelope.env = policy.env;
    envelope.clock = policy.clock;
    envelope.entropy = policy.entropy;
    envelope.native = policy.native;
    envelope.fs_prefixes = policy.fs_prefixes;
    const auto& limits = options.limits;
    envelope.limits = {
        {"max-steps", limits.max_steps},
        {"max-frames", limits.max_frames},
        {"max-heap-cells", limits.max_heap_cells},
        {"max-stack-cells", limits.max_stack_cells},
        {"max-tasks", limits.max_tasks},
        {"task-quantum", limits.task_quantum},
        {"max-queued-messages", limits.max_queued_messages},
        {"max-pending-requests", limits.max_pending_requests},
        {"parallel-workers", limits.parallel_workers},
        {"tier", limits.tier},
        {"jit", static_cast<std::uint64_t>(limits.jit == jit::Mode::Off ? 0 : limits.jit == jit::Mode::On ? 1 : 2)},
    };
    if (options.stats) envelope.flags.emplace_back("stats");
    if (options.tier_stats) envelope.flags.emplace_back("tier-stats");
    if (limits.profile) envelope.flags.emplace_back("profile");
    if (options.trace_wall_time) envelope.flags.emplace_back("trace-wall-time");
    if (options.virtual_time) envelope.flags.emplace_back("virtual-time");
    envelope.task_dump = !options.task_dump ? 0 : *options.task_dump == nvm::TaskDumpFormat::Text ? 1 : 2;
    if (options.trace_events) envelope.trace_events = options.trace_events->string();
    capture_process_settings(envelope);
    envelope.lifetime = lifetime;
    envelope.client_clock = clock_ns(client_start);
    return envelope;
}

cli::Options options_of(const Envelope& envelope) {
    cli::Options options;
    options.command = cli::Command::Run;
    options.files = {fs::path(envelope.image_path)};
    options.arguments = envelope.arguments;
    auto& policy = options.capabilities;
    policy.fs = envelope.fs;
    policy.net = envelope.net;
    policy.env = envelope.env;
    policy.clock = envelope.clock;
    policy.entropy = envelope.entropy;
    policy.native = envelope.native;
    policy.fs_prefixes = envelope.fs_prefixes;
    auto& limits = options.limits;
    const auto limit = [&](const char* name, auto& member) {
        if (const auto found = envelope.limits.find(name); found != envelope.limits.end()) {
            member = static_cast<std::remove_reference_t<decltype(member)>>(found->second);
        }
    };
    limit("max-steps", limits.max_steps);
    limit("max-frames", limits.max_frames);
    limit("max-heap-cells", limits.max_heap_cells);
    limit("max-stack-cells", limits.max_stack_cells);
    limit("max-tasks", limits.max_tasks);
    limit("task-quantum", limits.task_quantum);
    limit("max-queued-messages", limits.max_queued_messages);
    limit("max-pending-requests", limits.max_pending_requests);
    limit("parallel-workers", limits.parallel_workers);
    limit("tier", limits.tier);
    if (const auto jit = envelope.limits.find("jit"); jit != envelope.limits.end()) {
        limits.jit = jit->second == 0 ? jit::Mode::Off : jit->second == 1 ? jit::Mode::On : jit::Mode::Auto;
    }
    options.stats = envelope.flag("stats");
    options.tier_stats = envelope.flag("tier-stats");
    limits.profile = envelope.flag("profile");
    options.trace_wall_time = envelope.flag("trace-wall-time");
    options.virtual_time = envelope.flag("virtual-time");
    if (envelope.task_dump == 1) options.task_dump = nvm::TaskDumpFormat::Text;
    if (envelope.task_dump == 2) options.task_dump = nvm::TaskDumpFormat::Json;
    if (envelope.trace_events) options.trace_events = fs::path(*envelope.trace_events);
    // The service runs no graph device (unsupported_option).
    options.gpu = {};
    options.gpu.mode = cli::GpuMode::Off;
    return options;
}

Reuse reuse(const Envelope& envelope, const std::vector<std::string>& worker_environment) {
    Reuse result;
    result.warm = loader_variables(envelope.environment) == loader_variables(worker_environment);
    if (!result.warm) result.reason = "the loader or allocator variables differ";
    for (const auto& limit : envelope.resource_limits) {
        rlimit own{};
        if (::getrlimit(resource_of(limit.resource), &own) != 0) continue;
        const auto hard = own.rlim_max == RLIM_INFINITY ? unlimited : static_cast<std::uint64_t>(own.rlim_max);
        const auto soft = own.rlim_cur == RLIM_INFINITY ? unlimited : static_cast<std::uint64_t>(own.rlim_cur);
        if (hard != unlimited && (limit.soft == unlimited || limit.soft > hard)) {
            result.fresh_possible = false;
            result.reason = "a soft resource limit of the caller is above the service's hard limit";
            return result;
        }
        // The stack of the main thread and the default stack of new threads
        // are fixed when a process starts, so a different stack limit needs
        // a worker started with it.
        if (static_cast<ResourceKind>(limit.resource) == ResourceKind::Stack && limit.soft != soft) {
            if (result.warm) result.reason = "the stack limit differs";
            result.warm = false;
        }
    }
    errno = 0;
    const int nice_value = ::getpriority(PRIO_PROCESS, 0);
    if (errno == 0 && envelope.nice < nice_value) {
        result.fresh_possible = false;
        result.reason = "the caller's nice value is below the service's";
    }
    return result;
}

} // namespace neura::service
