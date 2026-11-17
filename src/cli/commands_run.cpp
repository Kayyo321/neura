#include "commands.hpp"

#include "argument_lists.hpp"
#include "gpu_run.hpp"
#include "launcher/container.hpp"
#include "nvm/fleet.hpp"
#include "nvm/host.hpp"
#include "nvm/schedule.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace neura::cli {

using neura::runner::Host;
using neura::runner::image_costs;
using neura::runner::provide_native_library;
using neura::runner::read_file;

bool has_image_magic(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 4 && std::ranges::equal(bytes.first(4), std::string_view("NEUR"));
}

neura::nvr::Image load_image(const fs::path& path, std::size_t* byte_count) {
    try {
        auto bytes = read_file(path);
        if (!has_image_magic(bytes) && neura::launcher::is_container(bytes)) {
            bytes = neura::launcher::read_container(bytes).image;
        }
        auto image = neura::nvr::load(bytes);
        if (byte_count) *byte_count = bytes.size();
        return image;
    } catch (const neura::nvr::LoadError& error) {
        throw std::runtime_error(path.string() + ": " + std::string(neura::nvr::describe(error.kind)) + ": " +
                                 error.what());
    }
}

namespace {

// The exact sum of up to 1,048,576 uint64 counters fits in these two decimal parts.
struct MemberTotal {
    void add(std::uint64_t value) {
        low += value % base;
        high += value / base + low / base;
        low %= base;
    }

    std::string text() const {
        const auto tail = std::to_string(low);
        if (high == 0) return tail;
        return std::to_string(high) + std::string(18 - tail.size(), '0') + tail;
    }

private:
    static constexpr std::uint64_t base = 1'000'000'000'000'000'000;
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

} // namespace

// The lifted blocks shared by every run of one image (docs/runtime/NVM.md,
// "Execution tiers"), unless the runs use a graph device or tier 0.
std::unique_ptr<neura::nvm::TierCache> tier_cache(const neura::nvr::Image& image, neura::nvm::RunLimits& limits,
                                                  const neura::nvm::GraphDevice* device) {
    std::unique_ptr<neura::nvm::TierCache> cache;
    if (!device && std::min(limits.tier, neura::nvm::tier_ceiling()) >= 1) {
        cache = std::make_unique<neura::nvm::TierCache>(image);
        limits.tier_cache = cache.get();
    }
    return cache;
}

// --tier-stats: the timeline of the runs of one command, from `started`, or
// none when they cannot be timed (neura::runner::times_runs).
std::optional<neura::nvm::TierTimes> tier_times(const neura::nvm::RunLimits& limits,
                                                std::optional<std::chrono::steady_clock::time_point> started,
                                                bool device) {
    std::optional<neura::nvm::TierTimes> times;
    if (started && !device && neura::runner::times_runs(limits)) times.emplace(*started);
    return times;
}

void report_tiers(const Options& options, const std::unique_ptr<neura::nvm::TierCache>& cache,
                  const std::optional<neura::nvm::TierTimes>& times) {
    if (!options.tier_stats) return;
    std::cerr << (cache ? neura::nvm::describe(cache->statistics()) : std::string("tier: tier 0\n"));
    if (times) std::cerr << times->describe(std::chrono::steady_clock::now());
}

namespace {

int run_each(const Options& options, const neura::nvr::Image& image, Host& host,
             std::span<neura::cli::ArgumentList> lists, std::size_t image_bytes, neura::nvm::GraphDevice* device,
             std::optional<std::chrono::steady_clock::time_point> started) {
    const auto& path = options.files.front();
    const auto bindings = neura::nvm::bind(image, host.registry);
    const auto report_prefix = path.string() + ": " + options.each->string() + ':';
    MemberTotal steps;
    MemberTotal requests;
    std::uint64_t peak_heap_cells = 0;
    std::uint64_t peak_stack_cells = 0;
    int first_status = 0;
    const auto finished = [&](std::size_t member, const neura::nvm::RunResult& result) {
        host.services.end_run();
        if (options.stats) {
            steps.add(result.steps);
            requests.add(result.host_requests);
            peak_heap_cells = std::max(peak_heap_cells, result.peak_heap_cells);
            peak_stack_cells = std::max(peak_stack_cells, result.peak_stack_cells);
        }
        const auto status = result.completed ? result.status : neura::nvm::exit_status(result.trap);
        if (!result.completed) {
            std::cerr << report_prefix << lists[member].line << ": trap: "
                      << neura::nvm::describe(result.trap, image) << '\n';
        } else if (status != 0) {
            std::cerr << report_prefix << lists[member].line << ": exit status " << status << '\n';
        }
        if (first_status == 0) first_status = status;
    };

    const auto append_list = [](std::vector<std::string>& arguments, std::vector<std::string>& listed) {
        arguments.insert(arguments.end(), std::make_move_iterator(listed.begin()), std::make_move_iterator(listed.end()));
    };
    neura::runner::TaskDumps dumps(options.task_dump);
    auto limits = options.limits;
    limits.task_dump = dumps.request();
    const auto tiers = tier_cache(image, limits, device);
    // Without a device the members run one after another on one timeline.
    auto times = tier_times(limits, started, device != nullptr);
    if (times) limits.tier_times = &*times;
    if (device) {
        neura::nvm::Fleet fleet(image, {limits, device});
        for (auto& list : lists) {
            if (options.arguments.empty()) {
                fleet.add(image.entry, std::move(list.arguments));
            } else {
                auto arguments = options.arguments;
                append_list(arguments, list.arguments);
                fleet.add(image.entry, std::move(arguments));
            }
        }
        neura::nvm::run_fleet(fleet, bindings, finished);
    } else {
        auto arguments = options.arguments;
        for (std::size_t member = 0; member < lists.size(); ++member) {
            arguments.resize(options.arguments.size());
            append_list(arguments, lists[member].arguments);
            finished(member, neura::nvm::run(image, bindings, arguments, limits));
        }
    }
    if (options.stats) {
        std::cerr << "stats: members " << lists.size() << ", steps " << steps.text()
                  << ", peak heap cells " << peak_heap_cells
                  // Only an image that uses regions reports their cells (NVR 1.7).
                  << (neura::nvr::uses_regions(image) ? ", peak stack cells " + std::to_string(peak_stack_cells)
                                                      : std::string())
                  << ", host requests " << requests.text()
                  << ", " << image_costs(image) << ", image bytes " << image_bytes << '\n';
    }
    report_tiers(options, tiers, times);
    return first_status;
}

} // namespace

int run(const Options& options) {
    // --tier-stats times the command from before the image is read.
    const auto started = options.tier_stats ? std::optional(std::chrono::steady_clock::now()) : std::nullopt;
    const auto& path = options.files.front();
    std::size_t image_bytes = 0;
    const auto image = load_image(path, &image_bytes);
    auto lists = options.each ? neura::cli::read_argument_lists(*options.each) : std::vector<neura::cli::ArgumentList>{};
    const auto max_jobs = options.each ? std::min<std::size_t>(lists.size(), neura::nvm::FleetOptions{}.window) : 1;
    neura::cli::GpuRun gpu(options.gpu, image, max_jobs);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Host host(options.capabilities);
    provide_native_library(image, host.services);
    if (options.each) {
        return gpu.finish(run_each(options, image, host, lists, image_bytes, gpu.device(), started), options.stats);
    }
    neura::runner::RunOptions run_options{options.limits, options.stats, options.record, options.replay,
                                          options.task_dump, options.trace_events, options.trace_wall_time};
    run_options.limits.graph_device = gpu.device();
    run_options.virtual_time = options.virtual_time;
    run_options.tier_stats = options.tier_stats;
    run_options.started = started;
    const auto status = neura::runner::run(image, host, options.arguments, run_options, path.string(), image_bytes);
    return gpu.finish(status, options.stats);
}

} // namespace neura::cli
