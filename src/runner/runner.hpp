#pragma once

#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

namespace neura::runner {

struct Host {
    nvm::StreamSink output{stdout};
    nvm::StreamSink error{stderr};
    nvm::DescriptorSource input{0};
    nvm::ConsoleWriteLine console{output};
    nvm::StandardServices services;
    nvm::HostRegistry registry;

    explicit Host(const nvm::Capabilities& capabilities);
};

struct RunOptions {
    nvm::RunLimits limits;
    bool stats = false;
    std::optional<std::filesystem::path> record;
    std::optional<std::filesystem::path> replay;
    // --task-dump: the full task dump of a trap, in this form.
    std::optional<nvm::TaskDumpFormat> task_dump;
    // --trace-events: one JSON object per scheduler event, one per line.
    std::optional<std::filesystem::path> trace_events;
    // --trace-wall-time: add wall-clock times, which differ between runs.
    bool trace_wall_time = false;
    // --virtual-time: timers, deadlines, and the clock services follow a
    // clock that moves only when every task is parked.
    bool virtual_time = false;
    // --tier-stats: report blocks lifted and steps per execution tier, and
    // where the wall time went from `started` (read before the image was;
    // when unset, when run() begins) to the report.
    bool tier_stats = false;
    std::optional<std::chrono::steady_clock::time_point> started{};
};

// Writes scheduler events as JSON lines (docs/lang/Concurrency.md section
// 14.2). Every line carries the event, its tick, and its task; other fields
// depend on the event. Wall-clock times are added only on request.
class TraceEvents final : public nvm::TaskObserver {
public:
    TraceEvents(const std::filesystem::path& path, bool wall_time);
    ~TraceEvents() override;
    void event(const nvm::TaskEvent& event) override;

private:
    struct Output;
    std::unique_ptr<Output> output_;
    bool wall_time_;
};

// Task dumps for the runs made while it lives (docs/lang/Concurrency.md
// section 14.1). SIGQUIT asks the running image for a dump on standard
// error and the run continues; `on_trap` adds the full dump when a task
// traps. Pass request() in RunLimits::task_dump. One at a time per process.
class TaskDumps {
public:
    explicit TaskDumps(std::optional<nvm::TaskDumpFormat> on_trap);
    ~TaskDumps();
    TaskDumps(const TaskDumps&) = delete;
    TaskDumps& operator=(const TaskDumps&) = delete;
    nvm::TaskDumpRequest* request() { return &request_; }

private:
    nvm::TaskDumpRequest request_;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);
std::string image_costs(const nvr::Image& image);
void provide_native_library(const nvr::Image& image, nvm::StandardServices& services);
// Whether --tier-stats can time runs with these limits: not with a graph
// device, nor with parallel workers, whose time overlaps the run's.
bool times_runs(const nvm::RunLimits& limits);

int run(const nvr::Image& image, Host& host, std::span<const std::string> arguments,
        const RunOptions& options, std::string_view name, std::size_t image_bytes);

} // namespace neura::runner
