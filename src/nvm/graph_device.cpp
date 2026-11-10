#include "graph_device.hpp"

#include "machine.hpp"

#include <algorithm>
#include <string>

namespace neura::nvm {

void validate_graph_jobs(const nvr::Image& image, std::span<const GraphJob> jobs) {
    for (const auto& job : jobs) {
        if (job.graph >= image.graphs.size()) {
            throw std::invalid_argument("graph job index out of range");
        }
        const auto& graph = image.graphs[job.graph];
        if (job.inputs.size() != graph.input_lanes || job.outputs.size() != graph.outputs.size()) {
            throw std::invalid_argument("graph job lane count does not match the graph");
        }
        if (std::any_of(job.inputs.begin(), job.inputs.end(), [](auto lane) { return lane != 0 && lane != 1; })) {
            throw std::invalid_argument("graph job input lane is not zero or one");
        }
    }
}

VerifyingDevice::VerifyingDevice(GraphDevice& inner) : inner_(inner) {}

const nvr::Image& VerifyingDevice::image() const {
    return inner_.image();
}

void VerifyingDevice::evaluate(std::span<GraphJob> jobs) {
    validate_graph_jobs(image(), jobs);
    inner_.evaluate(jobs);
    for (const auto& job : jobs) {
        if (!job.evaluated) {
            continue;
        }
        const auto& graph = image().graphs[job.graph];
        reference_.resize(graph.outputs.size());
        if (auto node = evaluate_outputs(graph, job.inputs, scratch_, reference_)) {
            throw GraphDeviceMismatch("graph device mismatch: graph " + std::to_string(job.graph) +
                                      ": reference evaluation overflows at node " + std::to_string(*node));
        }
        for (std::size_t i = 0; i < reference_.size(); ++i) {
            if (job.outputs[i] != reference_[i]) {
                throw GraphDeviceMismatch("graph device mismatch: graph " + std::to_string(job.graph) +
                                          " output " + std::to_string(i) + ": device " +
                                          std::to_string(job.outputs[i]) + ", reference " +
                                          std::to_string(reference_[i]));
            }
        }
    }
}

std::size_t VerifyingDevice::batch_hint() const {
    return inner_.batch_hint();
}

GraphDeviceStatistics VerifyingDevice::statistics() const {
    return inner_.statistics();
}

std::span<const GraphShape> VerifyingDevice::graph_shapes() const { return inner_.graph_shapes(); }

std::string_view VerifyingDevice::failure_reason() const { return inner_.failure_reason(); }

} // namespace neura::nvm
