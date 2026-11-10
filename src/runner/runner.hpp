#pragma once

#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"

#include <filesystem>
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

int run(const nvr::Image& image, Host& host, std::span<const std::string> arguments,
        const RunOptions& options, std::string_view name, std::size_t image_bytes);

} // namespace neura::runner
