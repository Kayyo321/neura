#include "placement.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>
#include <utility>

namespace neura::nvm {
namespace {

constexpr double margin = 0.75;
constexpr double open_budget = 150'000;
constexpr double give_up_work = 3'000'000;
constexpr DeviceModel optimistic_device{5, 0.17, 0.07, 4'000, 0.00013};
constexpr CpuModel nominal_cpu{0.08, 0.0008, 0.002};

double cpu_time(const GraphShape& shape, const CpuModel& model) {
    return std::fma(model.c2, static_cast<double>(shape.nodes),
                    std::fma(model.c1, static_cast<double>(shape.edges), model.c0));
}

double critical_time(const GraphShape& shape, const DeviceModel& model) {
    return std::fma(model.b1, static_cast<double>(shape.critical),
                    model.b0 * static_cast<double>(shape.layers));
}

double per_job_time(const GraphShape& shape, const DeviceModel& model, double critical) {
    return std::fma(model.d, static_cast<double>(shape.io_words), critical / model.K);
}

bool can_gain(std::span<const GraphShape> shapes, std::size_t max_jobs, const DeviceModel& device,
              const CpuModel& cpu) {
    for (const auto& shape : shapes) {
        if (!shape.eligible) continue;
        const auto critical = critical_time(shape, device);
        const auto gain = std::fma(margin, cpu_time(shape, cpu), -per_job_time(shape, device, critical));
        if (static_cast<double>(max_jobs) * gain > device.a + critical) return true;
    }
    return false;
}

struct Candidate {
    std::uint32_t graph;
    double critical;
    double per_job;
    double gain_per_job;
};

struct Prefix {
    std::size_t graphs = 0;
    std::size_t jobs = 0;
    double gain = 0;
    double predicted = 0;
};

// Scratch reused between batches. The counts and candidate list depend
// only on the current call, so there is no scan of untouched graphs.
class PlacementWork {
public:
    explicit PlacementWork(std::size_t graphs) : counts_(graphs), chosen_(graphs) {}

    void collect(std::span<const GraphJob> jobs) {
        reset_counts();
        for (const auto& job : jobs) count(job.graph);
    }

    void collect(std::span<GraphJob> jobs) {
        reset_counts();
        for (auto& job : jobs) {
            job.evaluated = false;
            count(job.graph);
        }
    }

    void collect(std::span<const std::uint32_t> graphs) {
        reset_counts();
        for (const auto graph : graphs) count(graph);
    }

    double cpu_work(std::span<const GraphShape> shapes, const CpuModel& cpu) const {
        double work = 0;
        for (const auto graph : touched_) {
            work = std::fma(static_cast<double>(counts_[graph]), cpu_time(shapes[graph], cpu), work);
        }
        return work;
    }

    Prefix select_prefix(std::span<const GraphShape> shapes, const DeviceModel& device, const CpuModel& cpu,
                         double correction) {
        candidates_.clear();
        for (const auto graph : touched_) {
            const auto& shape = shapes[graph];
            if (!shape.eligible) continue;
            const auto critical = critical_time(shape, device);
            const auto per_job = per_job_time(shape, device, critical);
            const auto gain = std::fma(margin, cpu_time(shape, cpu), -(correction * per_job));
            if (gain > 0) candidates_.push_back({graph, critical, per_job, gain});
        }
        std::ranges::sort(candidates_, [](const Candidate& left, const Candidate& right) {
            return std::tie(left.critical, left.graph) < std::tie(right.critical, right.graph);
        });

        Prefix best;
        std::size_t total_jobs = 0;
        double total_gain = 0;
        double total_incremental = 0;
        for (std::size_t i = 0; i < candidates_.size(); ++i) {
            const auto& candidate = candidates_[i];
            const auto count = static_cast<double>(counts_[candidate.graph]);
            total_jobs += counts_[candidate.graph];
            total_gain = std::fma(count, candidate.gain_per_job, total_gain);
            total_incremental = std::fma(count, candidate.per_job, total_incremental);
            const auto base = device.a + candidate.critical;
            const auto gain = std::fma(-correction, base, total_gain);
            if (gain > best.gain) best = {i + 1, total_jobs, gain, base + total_incremental};
        }
        return best;
    }

    template <class GraphAt, class Select>
    void select_jobs(std::size_t size, Prefix prefix, GraphAt graph_at, Select select) {
        if (prefix.graphs == 0) return;
        const auto chosen_graphs = std::span(candidates_).first(prefix.graphs);
        for (const auto& candidate : chosen_graphs) chosen_[candidate.graph] = true;
        for (std::size_t i = 0; i < size; ++i) {
            if (chosen_[graph_at(i)]) select(i);
        }
        for (const auto& candidate : chosen_graphs) chosen_[candidate.graph] = false;
    }

private:
    void reset_counts() {
        for (const auto graph : touched_) counts_[graph] = 0;
        touched_.clear();
    }

    void count(std::uint32_t graph) {
        if (counts_[graph]++ == 0) touched_.push_back(graph);
    }

    std::vector<std::size_t> counts_;
    std::vector<std::uint32_t> touched_;
    std::vector<Candidate> candidates_;
    std::vector<std::uint8_t> chosen_;
};

double steady_time() {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

DeviceModel fit_device(const std::array<double, 6>& measurements) {
    const auto [a, chain_one, fat_one, io_one, chain_many, io_many] = measurements;
    const auto b1 = std::max(0.0, (fat_one - chain_one) / 32'256);
    const auto b0 = std::max(0.0, (chain_one - a) / 512 - b1);
    const auto K = std::clamp(1'024 * 512 * (b0 + b1) / std::max(chain_many - chain_one, 0.001), 1.0, 65'536.0);
    const auto d = std::max(0.0, ((io_many - io_one) / 1'024 - (b0 + 4 * b1) / K) / 517);
    return {a, b0, b1, K, d};
}

CpuModel fit_cpu(const std::array<double, 3>& measurements) {
    const auto [unit, chain, fat] = measurements;
    const auto c1 = std::max(0.0, (fat - chain) / 32'256);
    const auto c2 = std::max(0.0, (chain - unit) / 511 - c1);
    const auto c0 = std::max(0.0, unit - c1 - c2);
    return {c0, c1, c2};
}

nvr::Image probe_image() {
    using enum nvr::Activation;
    nvr::Graph unit{1, {{Identity, 0, {{0, 1}}}}, {1}};
    nvr::Graph chain{1, {}, {512}};
    nvr::Graph fat{64, {}, {575}};
    nvr::Graph io{256, {}, {}};
    for (std::uint32_t i = 0; i < 512; ++i) {
        chain.nodes.push_back({Identity, 0, {{i, 1}}});
        nvr::Node node{Relu, 0, {}};
        node.edges.reserve(64);
        if (i == 0) node.edges.push_back({0, 1});
        for (std::uint32_t lane = 1; lane < 64; ++lane) node.edges.push_back({lane, 0});
        if (i != 0) node.edges.push_back({63 + i, 1});
        fat.nodes.push_back(std::move(node));
    }
    for (std::uint32_t i = 0; i < 256; ++i) {
        io.nodes.push_back({Identity, 0, {{i, 1}}});
        io.outputs.push_back(i + 256);
    }
    nvr::Image image;
    image.graphs.reserve(4);
    image.graphs.push_back(std::move(unit));
    image.graphs.push_back(std::move(chain));
    image.graphs.push_back(std::move(fat));
    image.graphs.push_back(std::move(io));
    return image;
}

Placement place_jobs(std::span<const GraphShape> shapes, std::span<const GraphJob> jobs,
                     const DeviceModel& device, const CpuModel& cpu, double correction) {
    if (std::ranges::any_of(jobs, [&](const GraphJob& job) { return job.graph >= shapes.size(); })) {
        throw std::invalid_argument("graph job index out of range");
    }
    PlacementWork work(shapes.size());
    work.collect(jobs);
    const auto prefix = work.select_prefix(shapes, device, cpu, correction);
    Placement placement{{}, prefix.gain, prefix.predicted};
    placement.jobs.reserve(prefix.jobs);
    work.select_jobs(jobs.size(), prefix, [&](std::size_t i) { return jobs[i].graph; },
                     [&](std::size_t i) { placement.jobs.push_back(i); });
    return placement;
}

struct AutoDevice::Scratch {
    explicit Scratch(std::size_t graphs) : placement(graphs) {}

    PlacementWork placement;
    std::vector<std::size_t> selected;
    std::vector<GraphJob> forwarded;
};

AutoDevice::AutoDevice(const nvr::Image& image, Opener opener, AutoOptions options)
    : image_(image), opener_(std::move(opener)), options_(std::move(options)), shapes_(layered_shapes(image)) {
    if (!can_gain(shapes_, options_.max_jobs, optimistic_device, nominal_cpu)) {
        reject("no graph can gain at this batch size");
        return;
    }
    if (!options_.now) options_.now = steady_time;
    scratch_ = std::make_unique<Scratch>(shapes_.size());
}

AutoDevice::~AutoDevice() = default;

AutoDevice::State AutoDevice::state() const { return state_; }
const std::string& AutoDevice::reason() const { return reason_; }
const std::string& AutoDevice::device_name() const { return device_name_; }
double AutoDevice::correction() const { return correction_; }
const nvr::Image& AutoDevice::image() const { return image_; }
std::size_t AutoDevice::batch_hint() const { return state_ == State::Rejected ? 1 : options_.max_jobs; }

std::span<const GraphShape> AutoDevice::shapes() const {
    if (inner_) {
        const auto actual = inner_->graph_shapes();
        if (actual.size() == shapes_.size()) return actual;
    }
    return shapes_;
}

std::span<const GraphShape> AutoDevice::graph_shapes() const { return shapes(); }
std::string_view AutoDevice::failure_reason() const { return failed_ ? std::string_view(reason_) : std::string_view{}; }

GraphDeviceStatistics AutoDevice::statistics() const {
    auto totals = inner_ ? inner_->statistics() : last_inner_statistics_;
    totals.declined_placement += declined_placement_;
    return totals;
}

void AutoDevice::reject(std::string reason) {
    if (inner_) {
        last_inner_statistics_ = inner_->statistics();
        const auto actual = inner_->graph_shapes();
        if (actual.size() == shapes_.size() && actual.data() != shapes_.data()) {
            shapes_.assign(actual.begin(), actual.end());
        }
        inner_.reset();
    }
    scratch_.reset();
    opener_ = {};
    reason_ = std::move(reason);
    state_ = State::Rejected;
}

void AutoDevice::open() {
    std::string reason;
    auto opened = opener_(reason);
    opener_ = {};
    if (!opened) {
        reject("unavailable: " + reason);
        return;
    }
    if (!opened->device) throw std::invalid_argument("auto opener returned no graph device");
    if (&opened->device->image() != &image_) {
        throw std::invalid_argument("graph device was created for another image");
    }
    if (const auto failure = opened->device->failure_reason(); !failure.empty()) {
        reject("unavailable: " + std::string(failure));
        return;
    }

    inner_ = std::move(opened->device);
    device_model_ = opened->device_model;
    cpu_model_ = opened->cpu_model;
    device_name_ = std::move(opened->name);
    if (can_gain(shapes(), options_.max_jobs, device_model_, cpu_model_)) {
        state_ = State::Active;
    } else {
        reject("the device cannot beat the CPU");
    }
}

void AutoDevice::evaluate(std::span<GraphJob> jobs) {
    validate_graph_jobs(image_, jobs);
    evaluate_batch(jobs, {}, nullptr);
}

bool AutoDevice::evaluate_prepared(std::span<const std::uint32_t> graphs,
                                   const detail::MaterializeGraphJobs& materialize) {
    evaluate_batch({}, graphs, &materialize);
    return true;
}

void AutoDevice::evaluate_batch(std::span<GraphJob> jobs, std::span<const std::uint32_t> graphs,
                                const detail::MaterializeGraphJobs* materialize) {
    const auto size = materialize ? graphs.size() : jobs.size();
    if (state_ == State::Rejected || size == 0) {
        for (auto& job : jobs) job.evaluated = false;
        return;
    }

    auto& scratch = *scratch_;
    if (materialize) scratch.placement.collect(graphs);
    else scratch.placement.collect(jobs);
    if (state_ == State::Gated) {
        const auto prefix = scratch.placement.select_prefix(shapes_, optimistic_device, nominal_cpu, 1);
        saved_ += prefix.gain;
        work_ += scratch.placement.cpu_work(shapes_, nominal_cpu);
        declined_placement_ += size;
        if (saved_ >= open_budget) open();
        else if (work_ >= give_up_work) reject("the workload cannot gain");
        return;
    }

    const auto prefix = scratch.placement.select_prefix(shapes(), device_model_, cpu_model_, correction_);
    declined_placement_ += size - prefix.jobs;
    if (prefix.jobs == 0) return;

    scratch.selected.clear();
    scratch.selected.reserve(prefix.jobs);
    std::span<GraphJob> forwarded;
    if (materialize) {
        scratch.placement.select_jobs(size, prefix, [&](std::size_t i) { return graphs[i]; },
                                      [&](std::size_t i) { scratch.selected.push_back(i); });
        forwarded = (*materialize)(scratch.selected);
        if (forwarded.size() != scratch.selected.size()) {
            throw std::logic_error("prepared graph batch returned the wrong number of jobs");
        }
        for (std::size_t i = 0; i < forwarded.size(); ++i) {
            if (forwarded[i].graph != graphs[scratch.selected[i]]) {
                throw std::logic_error("prepared graph batch returned the wrong graph");
            }
        }
    } else {
        scratch.forwarded.clear();
        scratch.forwarded.reserve(prefix.jobs);
        scratch.placement.select_jobs(size, prefix, [&](std::size_t i) { return jobs[i].graph; },
                                      [&](std::size_t i) {
                                          scratch.selected.push_back(i);
                                          scratch.forwarded.push_back(jobs[i]);
                                      });
        forwarded = scratch.forwarded;
    }
    const auto start = options_.now();
    inner_->evaluate(forwarded);
    const auto elapsed = options_.now() - start;
    if (!materialize) {
        for (std::size_t i = 0; i < scratch.selected.size(); ++i) {
            jobs[scratch.selected[i]].evaluated = forwarded[i].evaluated;
        }
    }

    if (prefix.predicted != 0 || elapsed != 0) {
        const auto sample = std::clamp(elapsed / prefix.predicted, correction_ / 2, 2 * correction_);
        if (std::isfinite(sample)) {
            correction_ = std::clamp(correction_ + (sample - correction_) / 8, 1.0 / 16, 64.0);
        }
    }
    if (const auto failure = inner_->failure_reason(); !failure.empty()) {
        failed_ = true;
        reject("device failed: " + std::string(failure));
    }
}

} // namespace neura::nvm
