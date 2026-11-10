#include "circuit_tests.hpp"
#include "fixtures.hpp"
#include "gpu/device.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
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
    check_integer_graph_device([&](const nvr::Image& image) {
        auto device = std::make_unique<gpu::VulkanGraphDevice>(*context, image);
        check(!device->failed(), device->reason());
        return device;
    });

    // Interleave two graphs in the same call. Each graph has 16 input, 16
    // output and 32 wire lanes. One MiB holds only 8,192 such wire regions
    // and fewer I/O records. A 65,536-job call also exceeds the dispatch
    // group-count ceiling even when both buffers can hold all its data.
    nvr::Image image;
    constexpr std::uint32_t lanes = 16;
    for (const auto weight : {1, -1}) {
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
        jobs.push_back({static_cast<std::uint32_t>(request % 2), input,
                        std::span(outputs).subspan(request * lanes, lanes)});
    }
    device.evaluate(jobs);
    check(!device.failed(), device.reason());
    const auto stats = device.statistics();
    check_equal(stats.evaluated, static_cast<std::uint64_t>(count), "all mixed graph requests evaluated");
    const auto capacity = std::bit_floor(std::min(256U << 20, context->max_storage_buffer_range()));
    const auto dispatch_size = std::min({65'535U, context->max_requests_per_dispatch(),
                                        capacity / (4U * (2U * lanes + 5U)), capacity / (4U * 2U * lanes)});
    check(dispatch_size > 0, "the device can hold one mixed graph request");
    const auto expected_dispatches = (count + dispatch_size - 1U) / dispatch_size;
    check_equal(stats.dispatches, expected_dispatches, "both buffers grew as needed for the longest dispatch prefixes");
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

} // namespace neura::testing
