#include "calibrate.hpp"

#include "context_internal.hpp"
#include "nvm/machine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <new>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>

namespace neura::gpu {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t samples = 5;
constexpr std::size_t warmups = 3;
constexpr std::size_t cpu_evaluations = 256;

double elapsed_microseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

std::optional<double> median(std::array<double, samples> times) {
    if (!std::ranges::all_of(times, [](double time) { return std::isfinite(time); })) return std::nullopt;
    std::ranges::sort(times);
    return times[times.size() / 2];
}

std::optional<double> measure_device(VulkanGraphDevice& device, std::uint32_t graph_index,
                                     std::size_t request_count) {
    const auto& graph = device.image().graphs[graph_index];
    const std::vector<std::int64_t> inputs(graph.input_lanes, 1);
    std::vector<std::int64_t> scratch;
    std::vector<std::int64_t> expected(graph.outputs.size());
    if (nvm::evaluate_outputs(graph, inputs, scratch, expected)) return std::nullopt;

    std::vector<std::int64_t> outputs(request_count * expected.size());
    std::vector<nvm::GraphJob> jobs;
    jobs.reserve(request_count);
    for (std::size_t request = 0; request < request_count; ++request) {
        jobs.push_back({graph_index, inputs, std::span(outputs).subspan(request * expected.size(), expected.size())});
    }
    const auto matches = [&](const nvm::GraphJob& job) {
        return job.evaluated && std::ranges::equal(job.outputs, expected);
    };
    for (std::size_t warmup = 0; warmup < warmups; ++warmup) {
        device.evaluate(jobs);
        if (!std::ranges::all_of(jobs, matches)) return std::nullopt;
    }

    std::array<double, samples> times{};
    for (auto& time : times) {
        const auto start = Clock::now();
        device.evaluate(jobs);
        time = elapsed_microseconds(start);
        if (!std::ranges::all_of(jobs, matches)) return std::nullopt;
    }
    return median(times);
}

std::optional<double> measure_cpu(const nvr::Graph& graph) {
    const std::vector<std::int64_t> inputs(graph.input_lanes, 1);
    std::vector<std::int64_t> scratch(inputs.size() + graph.nodes.size());
    std::vector<std::int64_t> outputs(graph.outputs.size());
    std::array<double, samples> times{};
    for (auto& time : times) {
        const auto start = Clock::now();
        for (std::size_t job = 0; job < cpu_evaluations; ++job) {
            if (nvm::evaluate_outputs(graph, inputs, scratch, outputs)) return std::nullopt;
        }
        time = elapsed_microseconds(start) / cpu_evaluations;
    }
    return median(times);
}

} // namespace

std::optional<Calibration> calibrate(Context& context, std::string* reason) try {
    const auto image = nvm::probe_image();
    VulkanGraphDevice device(context, image);
    // A device that ran out of host memory is reported as such, not as a
    // failed measurement.
    const auto fail = [&]() -> std::optional<Calibration> {
        if (reason && device.failed() && device.reason() == allocation_failure) *reason = allocation_failure;
        return std::nullopt;
    };
    if (device.failed()) return fail();

    struct Trial {
        std::uint32_t graph;
        std::size_t request_count;
    };
    constexpr std::array<Trial, 6> trials{{{0, 1}, {1, 1}, {2, 1}, {3, 1}, {1, 1024}, {3, 1024}}};
    std::array<double, trials.size()> device_times{};
    for (std::size_t i = 0; i < trials.size(); ++i) {
        const auto& trial = trials[i];
        const auto time = measure_device(device, trial.graph, trial.request_count);
        if (!time) return fail();
        device_times[i] = *time;
    }
    const auto device_model = nvm::fit_device(device_times);
    if (device_model.a > 10'000) return std::nullopt;

    std::array<double, 3> cpu_times{};
    for (std::size_t i = 0; i < cpu_times.size(); ++i) {
        const auto time = measure_cpu(image.graphs[i]);
        if (!time) return std::nullopt;
        cpu_times[i] = *time;
    }
    return Calibration{device_model, nvm::fit_cpu(cpu_times)};
} catch (const std::bad_alloc&) {
    if (reason) *reason = allocation_failure;
    return std::nullopt;
} catch (const std::length_error&) {
    if (reason) *reason = allocation_failure;
    return std::nullopt;
}

nvm::Opener make_opener(const nvr::Image& image, ContextOptions options) {
    return [&image, options](std::string& reason) -> std::optional<nvm::Opened> {
        // Running out of host memory is a device that cannot be opened, not an
        // error of the run.
        try {
            auto context = Context::create(options, reason);
            if (!context) return std::nullopt;
            std::string calibration_reason;
            const auto models = calibrate(*context, &calibration_reason);
            if (!models) {
                reason = calibration_reason.empty() ? "calibration failed" : calibration_reason;
                return std::nullopt;
            }
            auto name = context->device_name();
            auto device = std::make_unique<VulkanGraphDevice>(std::move(context), image);
            if (device->failed()) {
                reason = device->reason();
                return std::nullopt;
            }
            return nvm::Opened{std::move(device), models->device_model, models->cpu_model, std::move(name)};
        } catch (const std::bad_alloc&) {
            reason = allocation_failure;
        } catch (const std::length_error&) {
            reason = allocation_failure;
        }
        return std::nullopt;
    };
}

} // namespace neura::gpu
