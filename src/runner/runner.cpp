#include "runner.hpp"

#include "nvm/schedule.hpp"
#include "nvm/tier1.hpp"

#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"
#include "nvr/names.hpp"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

#ifndef _WIN32
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace neura::runner {
namespace fs = std::filesystem;

namespace {

// The request SIGQUIT sets. A handler may only store to a lock-free atomic.
std::atomic<nvm::TaskDumpRequest*> signalled_request{nullptr};

#ifdef SIGQUIT
extern "C" void request_task_dump(int) {
    if (auto* request = signalled_request.load(std::memory_order_relaxed)) {
        request->requested.store(true, std::memory_order_relaxed);
    }
}
#endif

} // namespace

TaskDumps::TaskDumps(std::optional<nvm::TaskDumpFormat> on_trap) {
    request_.format = on_trap.value_or(nvm::TaskDumpFormat::Text);
    request_.on_trap = on_trap.has_value();
    request_.write = [](const std::string& text) {
        if (text.empty()) {
            std::cerr << "task dump: this image has no tasks\n";
        } else {
            std::cerr << text;
        }
        std::cerr.flush();
    };
    signalled_request.store(&request_, std::memory_order_relaxed);
#ifdef SIGQUIT
    std::signal(SIGQUIT, request_task_dump);
#endif
}

struct TraceEvents::Output {
    std::FILE* file = nullptr;
    ~Output() {
        if (file) std::fclose(file);
    }
};

// The events are written as the run produces them, so the file is opened in
// place. A symbolic link at the path is refused instead of followed: through
// NEURA_RUN_OPTIONS the path comes from whoever sets the environment, and a
// planted link must not make the process overwrite another file.
TraceEvents::TraceEvents(const fs::path& path, bool wall_time)
    : output_(std::make_unique<Output>()), wall_time_(wall_time) {
#ifdef _WIN32
    output_->file = _wfopen(path.c_str(), L"wb");
#else
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (descriptor < 0 && errno == ELOOP) {
        throw std::runtime_error("cannot write " + path.string() + ": it is a symbolic link");
    }
    if (descriptor >= 0) {
        output_->file = ::fdopen(descriptor, "wb");
        if (!output_->file) ::close(descriptor);
    }
#endif
    if (!output_->file) throw std::runtime_error("cannot write " + path.string());
}

TraceEvents::~TraceEvents() = default;

void TraceEvents::event(const nvm::TaskEvent& event) {
    using Kind = nvm::TaskEvent::Kind;
    const auto quoted = [](const std::string& text) {
        std::string out = "\"";
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                constexpr std::string_view hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[(c >> 4) & 15];
                out += hex[c & 15];
            } else {
                out += c;
            }
        }
        return out + '"';
    };
    // The name of the event and of the field that holds `other`.
    std::string_view name;
    std::string_view other;
    switch (event.kind) {
    case Kind::Spawn: name = "spawn"; other = "child"; break;
    case Kind::Start: name = "start"; break;
    case Kind::Park: name = "park"; break;
    case Kind::Wake: name = "wake"; break;
    case Kind::Finish: name = "finish"; break;
    case Kind::Send: name = "send"; other = "channel"; break;
    case Kind::Receive: name = "receive"; other = "channel"; break;
    case Kind::Close: name = "close"; other = "channel"; break;
    case Kind::Cancel: name = "cancel"; other = "scope"; break;
    case Kind::DeadlineExpired: name = "deadline"; other = event.detail == "timer" ? "timer" : "scope"; break;
    case Kind::HostRequest: name = "host-request"; other = "request"; break;
    case Kind::HostCompletion: name = "host-completion"; other = "request"; break;
    }
    std::ostringstream out;
    out << "{\"event\":\"" << name << "\",\"tick\":" << event.tick << ",\"task\":" << event.task;
    if (!other.empty()) out << ",\"" << other << "\":" << event.other;
    if (event.kind == Kind::Park) out << ",\"reason\":" << quoted(event.detail);
    if (event.kind == Kind::Cancel) out << ",\"cause\":" << quoted(event.detail);
    if (event.kind == Kind::HostRequest || event.kind == Kind::HostCompletion) {
        out << ",\"service\":" << quoted(event.detail);
    }
    if (!event.where.empty()) out << ",\"at\":" << quoted(event.where);
    if (wall_time_) {
        out << ",\"wall_ns\":"
            << std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    }
    out << "}\n";
    const auto line = out.str();
    std::fwrite(line.data(), 1, line.size(), output_->file);
}

TaskDumps::~TaskDumps() {
#ifdef SIGQUIT
    std::signal(SIGQUIT, SIG_DFL);
#endif
    signalled_request.store(nullptr, std::memory_order_relaxed);
}

Host::Host(const nvm::Capabilities& capabilities)
    : services(output, error, input, capabilities) {
    registry.add(console);
    services.register_all(registry);
}

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        throw std::runtime_error("error reading " + path.string());
    }
    return bytes;
}

#ifdef _WIN32
// Writes through a temporary file and renames it into place, so a failed
// build never leaves a partial output.
void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot write " + path.string());
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            fs::remove(temporary);
            throw std::runtime_error("error writing " + path.string());
        }
    }
    fs::rename(temporary, path);
}
#else
namespace {

// Owns the temporary file until it has been renamed into place.
class TemporaryFile {
public:
    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    ~TemporaryFile() {
        if (descriptor_ >= 0) ::close(descriptor_);
        if (!path_.empty()) ::unlink(path_.c_str());
    }

    // Creates a new file next to the destination under a name nobody else
    // has used. O_EXCL and O_NOFOLLOW make a planted file or symbolic link
    // at a guessed name an error instead of a write target.
    void create(const fs::path& destination) {
        static std::atomic<unsigned long long> sequence{0};
        const auto directory = destination.parent_path();
        const auto stem = destination.filename().string();
        const auto seed = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            char suffix[64];
            std::snprintf(suffix, sizeof suffix, ".%d.%llx.tmp", static_cast<int>(::getpid()),
                          seed + sequence.fetch_add(1, std::memory_order_relaxed) * 0x9e3779b97f4a7c15ULL);
            const auto candidate = directory / ("." + stem + suffix);
            const int descriptor = ::open(candidate.c_str(),
                                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
            if (descriptor >= 0) {
                descriptor_ = descriptor;
                path_ = candidate;
                return;
            }
            if (errno != EEXIST) break;
        }
        throw std::runtime_error("cannot write " + destination.string() + ": " + std::strerror(errno));
    }

    void write_all(const fs::path& destination, const std::vector<std::uint8_t>& bytes) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto written = ::write(descriptor_, bytes.data() + offset, bytes.size() - offset);
            if (written < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("error writing " + destination.string() + ": " + std::strerror(errno));
            }
            offset += static_cast<std::size_t>(written);
        }
        while (::fsync(descriptor_) != 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("error writing " + destination.string() + ": " + std::strerror(errno));
        }
        const int closed = ::close(descriptor_);
        descriptor_ = -1;
        if (closed != 0) {
            throw std::runtime_error("error writing " + destination.string() + ": " + std::strerror(errno));
        }
    }

    void publish(const fs::path& destination) {
        if (::rename(path_.c_str(), destination.c_str()) != 0) {
            throw std::runtime_error("cannot write " + destination.string() + ": " + std::strerror(errno));
        }
        path_.clear();
    }

private:
    int descriptor_ = -1;
    fs::path path_;
};

} // namespace

// Writes through a uniquely named temporary file in the destination
// directory, flushes it, and renames it into place, so a failed write never
// leaves a partial output and an existing symbolic link at the destination is
// replaced rather than followed.
void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    TemporaryFile temporary;
    temporary.create(path);
    temporary.write_all(path, bytes);
    temporary.publish(path);
}
#endif

// Gives the runner the image's embedded native library when it was linked
// for this target; otherwise neura.ffi.library.embedded fails when used.
void provide_native_library(const nvr::Image& image, nvm::StandardServices& services) {
    if (image.native_library && image.native_library->triple == nvm::native_target_triple()) {
        const auto& object = image.native_library->object;
        services.set_embedded_object(std::string(object.begin(), object.end()));
    }
}

// The size of an image's executable circuits: graph, neuron, and edge counts.
std::string image_costs(const nvr::Image& image) {
    std::uint64_t neurons = 0;
    std::uint64_t edges = 0;
    for (const auto& graph : image.graphs) {
        neurons += graph.nodes.size();
        for (const auto& node : graph.nodes) edges += node.edges.size();
    }
    return "graphs " + std::to_string(image.graphs.size()) + ", neurons " + std::to_string(neurons) + ", edges " +
           std::to_string(edges);
}

namespace {

nvm::RunResult result_of(const nvm::Trace& trace) {
    nvm::RunResult result;
    result.completed = trace.outcome.completed;
    result.status = trace.outcome.status;
    result.trap = trace.trap;
    result.steps = trace.steps;
    result.host_requests = trace.entries.size();
    result.peak_heap_cells = trace.peak_heap_cells;
    return result;
}

nvm::Trace read_trace_file(const fs::path& path) {
    const auto bytes = read_file(path);
    try {
        return nvm::read_trace(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    } catch (const nvm::TraceFormatError& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

// The functions that took the most steps, with names from the image's NAME
// section when it has one.
void print_profile(const nvr::Image& image, const nvm::RunResult& result) {
    const auto names = nvr::decode_names(image);
    std::vector<std::pair<std::uint64_t, std::size_t>> ranked;
    for (std::size_t f = 0; f < result.function_steps.size(); ++f) {
        if (result.function_steps[f] != 0) ranked.emplace_back(result.function_steps[f], f);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::cerr << "profile: steps by function\n";
    for (std::size_t i = 0; i < ranked.size() && i < 25; ++i) {
        const auto [steps, f] = ranked[i];
        std::cerr << "  " << steps << "  f" << f;
        if (names && f < names->functions.size()) std::cerr << "  " << names->functions[f];
        std::cerr << '\n';
    }
    // Per spawn site: whether its tasks were computing, waiting for a turn,
    // waiting on a channel or timer, or waiting on host I/O.
    if (!result.task_profile.empty()) {
        std::cerr << "profile: tasks by spawn site (steps executed; ticks runnable, on channels, on host I/O)\n";
        for (std::size_t i = 0; i < result.task_profile.size(); ++i) {
            const auto& site = result.task_profile[i];
            std::cerr << "  " << result.task_profile_sites[i] << "  tasks " << site.tasks << "  steps " << site.steps
                      << "  runnable " << site.runnable_ticks << "  channel " << site.channel_ticks << "  host "
                      << site.host_ticks << '\n';
        }
    }
}

} // namespace

int run(const nvr::Image& image, Host& host, std::span<const std::string> arguments,
        const RunOptions& options, std::string_view name, std::size_t image_bytes) {
    TaskDumps dumps(options.task_dump);
    auto limits = options.limits;
    limits.task_dump = dumps.request();
    // One cache of lifted blocks for the run, so that its statistics can be
    // reported; the run would otherwise keep its own.
    std::optional<nvm::TierCache> tiers;
    if (!limits.graph_device && !limits.tier_cache && std::min(limits.tier, nvm::tier_ceiling()) >= 1) {
        tiers.emplace(image);
        limits.tier_cache = &*tiers;
    }
    std::optional<TraceEvents> events;
    if (options.trace_events) {
        events.emplace(*options.trace_events, options.trace_wall_time);
        limits.observer = &*events;
    }
    // A replay takes its clock readings from the trace.
    nvm::VirtualTaskClock virtual_clock;
    nvm::TaskClock* clock = options.virtual_time && !options.replay ? &virtual_clock : nullptr;
    host.services.set_clock(clock);
    nvm::RunResult result;
    if (options.replay) {
        // No service runs: every response comes from the trace. Console
        // output is reproduced from the recorded requests so that the
        // replayed run prints what the recorded one printed.
        const auto expected = read_trace_file(*options.replay);
        const auto replayed = nvm::replay(
            image, expected, arguments, limits, [&](const nvm::TraceEntry& entry) {
                nvm::replay_console_output(entry.import, entry.version, entry.arguments, entry.completion,
                                                  host.output, host.error);
            });
        if (!replayed.matches) {
            throw std::runtime_error("replay of " + options.replay->string() + " diverged: " + replayed.difference);
        }
        result = result_of(replayed.trace);
    } else if (options.record) {
        const auto bindings = nvm::bind(image, host.registry);
        const auto trace = nvm::record(image, bindings, arguments, limits, clock);
        host.services.end_run();
        const auto text = nvm::write_trace(trace);
        write_file(*options.record, std::vector<std::uint8_t>(text.begin(), text.end()));
        result = result_of(trace);
    } else {
        const auto bindings = nvm::bind(image, host.registry);
        result = nvm::run(image, bindings, arguments, limits, clock);
        host.services.end_run();
    }
    if (options.stats) {
        std::cerr << "stats: steps " << result.steps << ", peak heap cells " << result.peak_heap_cells
                  << ", host requests " << result.host_requests << ", " << image_costs(image)
                  << ", image bytes " << image_bytes;
        // Only an image with tasks reports them, so other output is unchanged.
        if (result.peak_tasks != 0) {
            std::cerr << ", peak tasks " << result.peak_tasks << ", peak queued messages "
                      << result.peak_queued_messages << ", peak pending requests " << result.peak_pending_requests;
        }
        std::cerr << '\n';
    }
    if (options.tier_stats) {
        std::cerr << (tiers ? nvm::describe(tiers->statistics()) : std::string("tier: tier 0\n"));
    }
    if (options.limits.profile) {
        print_profile(image, result);
    }
    if (!result.completed) {
        std::cerr << name << ": trap: " << nvm::describe(result.trap, image) << '\n';
        return nvm::exit_status(result.trap);
    }
    return result.status;
}

} // namespace neura::runner
