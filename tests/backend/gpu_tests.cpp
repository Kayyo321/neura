#include "allocation_fault.hpp"
#include "circuit_tests.hpp"
#include "fixtures.hpp"
#include "gpu/calibrate.hpp"
#include "gpu/device.hpp"
#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

namespace neura::testing {
namespace {

std::unique_ptr<gpu::Context> open_gpu() {
    std::string reason;
    auto context = gpu::Context::create({.allow_software = true}, reason);
    if (!context) {
        std::cout << "gpu: skipped (" << reason << ")\n";
        std::exit(77);
    }
    std::cout << "gpu: device \"" << context->device_name() << "\"\n";
    return context;
}

} // namespace

NEURA_TEST(gpu_device, integer_circuits_and_mixed_dispatch) {
    auto context = open_gpu();
    // Calibration checks every measured and warm-up job against the CPU.
    const auto calibration = gpu::calibrate(*context);
    check(calibration.has_value(), "all calibration probes evaluated with the reference output lanes");
    const auto& model = calibration->device_model;
    const auto& cpu = calibration->cpu_model;
    const std::array coefficients{model.a, model.b0, model.b1, model.K, model.d, cpu.c0, cpu.c1, cpu.c2};
    check(std::ranges::all_of(coefficients, [](double value) { return std::isfinite(value) && value >= 0; }),
          "calibration coefficients are finite and non-negative");
    check(model.K >= 1 && model.K <= 65'536, "calibration parallelism estimate is within its bounds");
    std::cout << std::setprecision(10)
              << "calibration: a=" << model.a << " b0=" << model.b0 << " b1=" << model.b1
              << " K=" << model.K << " d=" << model.d
              << " c0=" << cpu.c0 << " c1=" << cpu.c1 << " c2=" << cpu.c2 << '\n';

    check_integer_graph_device([&](const nvr::Image& image) {
        auto device = std::make_unique<gpu::VulkanGraphDevice>(*context, image);
        check(!device->failed(), device->reason());
        return device;
    });

    // Interleave five graphs in the same call. Each graph has 16 input, 16
    // output and 32 wire lanes. One MiB holds only 8,192 such wire regions
    // and fewer I/O records. A 65,536-job call also exceeds the dispatch
    // group-count ceiling even when both buffers can hold all its data.
    nvr::Image image;
    constexpr std::uint32_t lanes = 16;
    for (const auto weight : {1, -1, 2, -2, 3}) {
        nvr::Graph graph;
        graph.input_lanes = lanes;
        for (std::uint32_t lane = 0; lane < lanes; ++lane) {
            graph.nodes.push_back({nvr::Activation::Identity, 0, {{lane, weight}}});
            graph.outputs.push_back(lane + lanes);
        }
        image.graphs.push_back(std::move(graph));
    }
    gpu::VulkanGraphDevice device(*context, image);
    check(!device.failed(), device.reason());
    const auto initial_io = device.io_capacity();
    const auto initial_wires = device.wires_capacity();
    check_equal(initial_io, std::uint64_t{1} << 20, "initial IO capacity");
    check_equal(initial_wires, std::uint64_t{1} << 20, "initial WIRES capacity");
    constexpr std::size_t count = 65'536;
    std::vector<std::int64_t> inputs(count * lanes);
    std::vector<std::int64_t> outputs(count * lanes);
    std::vector<nvm::GraphJob> jobs;
    jobs.reserve(count);
    for (std::size_t request = 0; request < count; ++request) {
        auto input = std::span(inputs).subspan(request * lanes, lanes);
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            input[lane] = static_cast<std::int64_t>((request >> lane) & 1U);
        }
        jobs.push_back({static_cast<std::uint32_t>(request % image.graphs.size()), input,
                        std::span(outputs).subspan(request * lanes, lanes)});
    }
    device.evaluate(jobs);
    check(!device.failed(), device.reason());
    const auto stats = device.statistics();
    check_equal(stats.evaluated, static_cast<std::uint64_t>(count), "all mixed graph requests evaluated");
    check(device.io_capacity() > initial_io, "the mixed graph dispatch grew IO");
    check(device.wires_capacity() > initial_wires, "the mixed graph dispatch grew WIRES");
    // Each request needs 16 + 16 + 5 = 37 IO words (148 bytes) and 16 + 16 = 32
    // wire words (128 bytes). Section 7.4 gives each dispatch the longest
    // prefix of the remaining requests within the smaller of 65,535 and
    // maxComputeWorkGroupCount[0], which is 65,535 on every conforming device
    // (the Vulkan minimum), and within the IO and WIRES ceilings. 65,535
    // requests need 9.7 MB of IO and 8.4 MB of WIRES, far inside any device's
    // maxStorageBufferRange (at least 128 MiB) and the 256 MiB ceiling. So
    // the first dispatch carries 65,535 requests and the second the last one.
    check_equal(context->max_requests_per_dispatch(), std::uint32_t{65'535}, "the dispatch group-count ceiling");
    check_equal(stats.dispatches, std::uint64_t{2}, "the group-count limit split the mixed graph call in two");
    std::vector<std::int64_t> wires;
    for (std::size_t request = 0; request < count; ++request) {
        const auto& job = jobs[request];
        const auto& graph = image.graphs[job.graph];
        wires.assign(job.inputs.begin(), job.inputs.end());
        if (!job.evaluated || nvm::evaluate_graph(graph, wires)) {
            fail("mixed graph request " + std::to_string(request) + " did not evaluate");
        }
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            if (job.outputs[lane] != wires[graph.outputs[lane]]) {
                fail("mixed graph request " + std::to_string(request) + " differs at lane " + std::to_string(lane));
            }
        }
    }
    std::cout << "mixed graph call: " << count << " requests in " << stats.dispatches << " dispatches\n";
}

NEURA_TEST(gpu_storage_limits, oversized_graphs_leave_other_graphs_eligible) {
    auto context = open_gpu();
    constexpr std::uint32_t range = 3U << 19;
    check_equal(context->max_storage_buffer_range(), range, "the shim reports a non-power-of-two range");
    constexpr std::uint32_t words = range / sizeof(std::uint32_t);
    constexpr std::uint32_t near_lanes = (1U << 18) + 20;
    const nvr::Node constant{nvr::Activation::Identity, 7, {}};
    nvr::Image image;
    image.graphs = {
        {words - 5, {constant}, {words - 5}},                  // IO too large, WIRES fits
        {words - 6, std::vector<nvr::Node>(7, constant), {}}, // WIRES too large, IO fits
        {near_lanes, {constant}, {near_lanes}},               // both fit above 1 MiB
        {0, {constant}, {0}},
    };
    gpu::VulkanGraphDevice device(*context, image);
    check(!device.failed(), device.reason());
    check(device.io_capacity() > (1U << 20) && device.io_capacity() <= range &&
          device.wires_capacity() > (1U << 20) && device.wires_capacity() <= range,
          "both initial buffers fit the eligible graph and the device range");
    const auto shapes = device.graph_shapes();
    for (std::size_t graph = 0; graph < 2; ++graph) {
        check(!shapes[graph].eligible && shapes[graph].reason == nvm::IneligibleReason::table_limit &&
              device.tables().slots[graph] == nvr::absent, "an oversized graph has no table slot");
    }
    check(shapes[2].eligible && shapes[3].eligible, "later graphs remain eligible");

    const std::vector<std::int64_t> inputs(words, 0);
    std::array<std::int64_t, 4> outputs{};
    auto input = std::span(inputs);
    auto output = std::span(outputs);
    std::array<nvm::GraphJob, 5> jobs{{
        {0, input.first(words - 5), output.subspan(0, 1)},
        {1, input.first(words - 6), {}},
        {2, input.first(near_lanes), output.subspan(1, 1)},
        {3, {}, output.subspan(2, 1)},
        {2, input.first(near_lanes), output.subspan(3, 1)},
    }};
    device.evaluate(jobs);
    check(!device.failed(), device.reason());
    const auto stats = device.statistics();
    check(stats.declined_ineligible == 2 && stats.evaluated == 3 && stats.dispatches == 3,
          "the oversized jobs decline and the eligible jobs split across dispatches");
    check(!jobs[0].evaluated && !jobs[1].evaluated &&
          jobs[2].evaluated && jobs[3].evaluated && jobs[4].evaluated &&
          outputs[1] == 7 && outputs[2] == 7 && outputs[3] == 7,
          "the device returns the eligible graphs' output lanes");
}

NEURA_TEST(gpu_device, calibration_and_opener_contain_allocation_failure) {
    auto context = open_gpu();
    // Count the allocations calibration makes, then fail a spread of them:
    // the first ones (the probe image and the device's tables), the middle
    // (the measurement vectors), and the last.
    std::size_t total = 0;
    {
        AllocationFault count;
        check(gpu::calibrate(*context).has_value(), "calibration succeeds when allocation does");
        total = count.allocations();
    }
    check(total > 16, "calibration allocates host memory");
    const std::vector<std::size_t> points{1, 2, 3, 4, total / 8, total / 4, total / 2, 3 * total / 4,
                                          total - 8, total - 1, total};
    std::size_t contained = 0;
    for (const auto nth : points) {
        std::string reason;
        std::optional<gpu::Calibration> result;
        bool fired = false;
        {
            AllocationFault fault(nth);
            result = gpu::calibrate(*context, &reason);
            fired = fault.fired();
        }
        if (!fired) {
            check(result.has_value(), "a calibration that met no failure succeeds");
            continue;
        }
        ++contained;
        check(!result && reason == "cannot allocate device memory",
              "allocation " + std::to_string(nth) + " of " + std::to_string(total) + " fails calibration with its reason");
    }
    check(contained >= 8, "most injected failures landed in calibration");

    // The opener (context, calibration, and device) gives AutoDevice the
    // same reason, and AutoDevice rejects without failing.
    std::size_t opener_total = 0;
    nvr::Image image;
    image.graphs = {{1, {{nvr::Activation::Identity, 0, {{0, 1}}}}, {1}}};
    {
        std::string reason;
        AllocationFault count;
        auto opened = gpu::make_opener(image, {.allow_software = true})(reason);
        opener_total = count.allocations();
        check(opened.has_value(), "the opener succeeds when allocation does: " + reason);
    }
    std::size_t opener_contained = 0;
    for (const auto nth : {std::size_t{1}, opener_total / 3, 2 * opener_total / 3, opener_total}) {
        std::string reason;
        std::optional<nvm::Opened> opened;
        bool fired = false;
        {
            AllocationFault fault(nth);
            opened = gpu::make_opener(image, {.allow_software = true})(reason);
            fired = fault.fired();
        }
        if (fired) {
            ++opener_contained;
            check(!opened && reason == "cannot allocate device memory",
                  "opener allocation " + std::to_string(nth) + " fails with its reason, got '" + reason + "'");
        } else {
            check(opened.has_value(), "an opener that met no failure succeeds");
        }
    }
    check(opener_contained >= 3, "the opener met injected failures");
    std::cout << "allocations: calibration " << total << ", opener " << opener_total << '\n';
}

NEURA_TEST(gpu_device, callback_trace_is_identical_across_evaluators) {
    auto context = open_gpu();
    auto image = hello_model();
    image.graphs.push_back({0, {{nvr::Activation::Identity, 0, {}}}, {}});
    image.functions[0].blocks[0].graph = 1;
    image.constants.push_back({3, {'n', 'e', 's', 't', 'e', 'd'}});
    image.constants.push_back({2, {37, 0, 0, 0}});
    auto callback = image.functions[0];
    callback.parameter_count = 0;
    callback.blocks[0].exits[0].arguments[0].index = 2;
    callback.blocks[1].exits[0].arguments[0].index = 3;
    image.functions.push_back(std::move(callback));
    image.callbacks = {{"neura.test.callback", 1, 1}};
    image = nvr::load(nvr::encode(image));

    class CallbackConsole final : public nvm::HostService {
    public:
        const nvm::HostSignature& signature() const override { return console.signature(); }
        nvm::HostCompletion invoke(const nvm::HostRequest& request) override {
            if (request.arguments[0].bytes == "hi") {
                check(request.callbacks != nullptr, "outer request has a callback context");
                const auto value = request.callbacks->call("neura.test.callback", 1, {});
                check(value && value->bits == 37, "callback returned the value after its nested request");
            } else {
                check_equal(request.arguments[0].bytes, std::string("nested"), "nested request arguments");
            }
            return console.invoke(request);
        }
        CapturingConsole console;
    };

    gpu::VulkanGraphDevice device(*context, image);
    check(!device.failed(), device.reason());
    nvm::RunLimits gpu_limits{.graph_device = &device};
    const auto record = [&](nvm::RunLimits limits) {
        CallbackConsole service;
        nvm::HostRegistry registry;
        registry.add(service);
        return nvm::record(image, nvm::bind(image, registry), {}, limits);
    };
    const auto cpu = record({});
    const auto gpu = record(gpu_limits);
    check(cpu.outcome.completed && cpu.outcome.status == 0, "recording completed");
    check(cpu.entries.size() == 1 && cpu.entries[0].callbacks.size() == 1 &&
          cpu.entries[0].callbacks[0].entries.size() == 1, "trace contains an outer request and nested callback request");
    const auto cpu_bytes = nvm::write_trace(cpu);
    const auto gpu_bytes = nvm::write_trace(gpu);
    check_equal(gpu_bytes, cpu_bytes, "deterministic callback recordings have identical bytes");
    const auto cpu_replay = nvm::replay(image, nvm::read_trace(gpu_bytes), {});
    const auto gpu_replay = nvm::replay(image, nvm::read_trace(cpu_bytes), {}, gpu_limits);
    check(cpu_replay.matches && gpu_replay.matches, "callback trace replays in both directions");
    check_equal(nvm::write_trace(cpu_replay.trace), cpu_bytes, "CPU replay trace bytes");
    check_equal(nvm::write_trace(gpu_replay.trace), gpu_bytes, "GPU replay trace bytes");
    check_equal(device.statistics().evaluated, std::uint64_t{4}, "GPU evaluates both the outer and callback blocks on each run");
}

NEURA_TEST(gpu_failure_counts, ineligible_jobs_remain_ineligible_after_failure) {
    auto context = open_gpu();
    nvr::Image image;
    image.graphs = {{}, {0, {{nvr::Activation::Identity, 7, {}}}, {0}}};
    gpu::VulkanGraphDevice device(*context, image);
    check(!device.failed(), device.reason());
    std::array<std::int64_t, 1> output{};
    std::array<nvm::GraphJob, 3> jobs{{{0, {}, {}}, {1, {}, output}, {0, {}, {}}}};
    for (std::uint64_t calls = 1; calls <= 2; ++calls) {
        device.evaluate(jobs);
        const auto stats = device.statistics();
        check(device.failed() && device.failure_reason() == "cannot allocate device memory",
              "an allocation exception fails the device");
        check(std::ranges::none_of(jobs, &nvm::GraphJob::evaluated), "all jobs declined");
        check(stats.evaluated == 0 && stats.dispatches == 1 && stats.declined_failed == calls &&
              stats.declined_ineligible == 2 * calls, "failed and ineligible jobs are counted separately");
    }
}

} // namespace neura::testing

namespace neura::testing {

// Runs only under the fault loader, which makes the second graph dispatch
// throw std::bad_alloc after the driver finished it.
NEURA_TEST(gpu_partial_failure, completed_dispatches_stay_evaluated_and_the_rest_is_declined) {
    auto context = open_gpu();
    check_equal(context->max_requests_per_dispatch(), std::uint32_t{65'535}, "the dispatch group-count ceiling");
    nvr::Image image;
    image.graphs = {{1, {{nvr::Activation::Identity, 0, {{0, 1}}}}, {1}}};
    gpu::VulkanGraphDevice device(*context, image);
    check(!device.failed(), device.reason());

    // 65,535 requests fit in a dispatch, so 2 * 65,535 + 1 requests are
    // three dispatches: jobs 0 to 65,534, then 65,535 to 131,069, then 131,070.
    // The second throws after it has run, so its 65,535 jobs are declined
    // as failed, and the third is declined without a submission.
    constexpr std::size_t dispatch_size = 65'535;
    constexpr std::size_t count = 2 * dispatch_size + 1;
    std::vector<std::int64_t> inputs(count);
    std::vector<std::int64_t> outputs(count, -7);
    std::vector<nvm::GraphJob> jobs;
    jobs.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        inputs[i] = static_cast<std::int64_t>(i & 1U);
        jobs.push_back({0, std::span(inputs).subspan(i, 1), std::span(outputs).subspan(i, 1)});
    }
    device.evaluate(jobs);
    check(device.failed() && device.failure_reason() == "cannot allocate device memory",
          "the allocation exception fails the device");
    for (std::size_t i = 0; i < count; ++i) {
        if (i < dispatch_size) {
            check(jobs[i].evaluated && outputs[i] == inputs[i], "a job of the completed dispatch keeps its result");
        } else {
            check(!jobs[i].evaluated, "a job of the failed or unsent dispatch is declined");
        }
    }
    const auto stats = device.statistics();
    check(stats.evaluated == dispatch_size && stats.dispatches == 2 && stats.declined_failed == dispatch_size + 1 &&
          stats.declined_ineligible == 0 && stats.escaped == 0,
          "one dispatch evaluated, two submitted, 65,536 jobs declined as failed");

    // The reference evaluator computes every declined job.
    std::vector<std::int64_t> wires;
    for (std::size_t i = dispatch_size; i < count; ++i) {
        wires.assign(1, inputs[i]);
        check(!nvm::evaluate_graph(image.graphs[0], wires) && wires[image.graphs[0].outputs[0]] == inputs[i],
              "a declined job has a reference result");
    }
    // A later call is declined whole.
    jobs[0].evaluated = true;
    device.evaluate(std::span(jobs).first(3));
    check(!jobs[0].evaluated && !jobs[1].evaluated && !jobs[2].evaluated &&
          device.statistics().declined_failed == stats.declined_failed + 3, "a failed device declines later jobs");
}

} // namespace neura::testing
