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
    check(stats.dispatches > 1, "the group-count limit split the mixed graph call");
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

} // namespace neura::testing
