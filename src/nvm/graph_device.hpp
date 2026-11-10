#pragma once

#include "nvr/image.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace neura::nvm {

enum class IneligibleReason { none, no_nodes, weight_budget, table_limit, escaped };

struct GraphShape {
    bool eligible = false;
    IneligibleReason reason = IneligibleReason::none;
    std::uint64_t layers = 0;
    std::uint64_t edges = 0;
    std::uint64_t nodes = 0;
    std::uint64_t critical = 0;
    std::uint64_t io_words = 0;
};

struct GraphJob {
    std::uint32_t graph = 0;
    std::span<const std::int64_t> inputs;
    std::span<std::int64_t> outputs;
    bool evaluated = false;
};

namespace detail {
using MaterializeGraphJobs = std::function<std::span<GraphJob>(std::span<const std::size_t>)>;
} // namespace detail

struct GraphDeviceStatistics {
    std::uint64_t evaluated = 0;
    std::uint64_t declined_ineligible = 0;
    std::uint64_t declined_placement = 0;
    std::uint64_t escaped = 0;
    std::uint64_t declined_failed = 0;
    std::uint64_t dispatches = 0;
};

// A device belongs to one image object. The image and its graph table must
// stay alive and unchanged while the device is in use.
class GraphDevice {
public:
    virtual ~GraphDevice() = default;
    virtual const nvr::Image& image() const = 0;
    // For evaluated jobs, return the reference output lanes exactly and only
    // when the reference computation does not overflow. On device failure or
    // when declining a job, clear evaluated and leave its outputs unspecified.
    // An invalid graph index, lane count, or input bit is a caller error.
    virtual void evaluate(std::span<GraphJob> jobs) = 0;
    // The largest useful batch. A hint of one gives no benefit from batching.
    virtual std::size_t batch_hint() const = 0;
    virtual GraphDeviceStatistics statistics() const = 0;
    // Optional observations for a placement wrapper. An empty shape span
    // leaves eligibility to the wrapper's static analysis of the image.
    virtual std::span<const GraphShape> graph_shapes() const { return {}; }
    virtual std::string_view failure_reason() const { return {}; }

private:
    friend class Fleet;
    // Fleet has resolved binary inputs for every graph in this order. An
    // opting-in device may synchronously materialize a single nonempty
    // subset of strictly increasing job indices. Returning false without
    // materializing leaves all work to the caller's ordinary eager batch.
    virtual bool evaluate_prepared(std::span<const std::uint32_t>, const detail::MaterializeGraphJobs&) { return false; }
};

// Checks the caller's graph indices, lane counts, and input bits before a
// device starts evaluating the batch. Throws std::invalid_argument on error.
void validate_graph_jobs(const nvr::Image& image, std::span<const GraphJob> jobs);

class GraphDeviceMismatch : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class VerifyingDevice final : public GraphDevice {
public:
    explicit VerifyingDevice(GraphDevice& inner);

    const nvr::Image& image() const override;
    void evaluate(std::span<GraphJob> jobs) override;
    std::size_t batch_hint() const override;
    GraphDeviceStatistics statistics() const override;
    std::span<const GraphShape> graph_shapes() const override;
    std::string_view failure_reason() const override;

private:
    GraphDevice& inner_;
    std::vector<std::int64_t> scratch_;
    std::vector<std::int64_t> reference_;
};

} // namespace neura::nvm
