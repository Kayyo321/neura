#pragma once

#include "layered.hpp"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace neura::nvm {

struct DeviceModel {
    double a = 0;
    double b0 = 0;
    double b1 = 0;
    double K = 1;
    double d = 0;
};

struct CpuModel {
    double c0 = 0;
    double c1 = 0;
    double c2 = 0;
};

DeviceModel fit_device(const std::array<double, 6>& measurements);
CpuModel fit_cpu(const std::array<double, 3>& measurements);
nvr::Image probe_image();

struct Placement {
    std::vector<std::size_t> jobs;
    double gain = 0;
    double predicted = 0;
};

// The original job indices chosen by the sorted prefix walk. The other
// fields report its gain and its uncorrected predicted dispatch time.
Placement place_jobs(std::span<const GraphShape> shapes, std::span<const GraphJob> jobs,
                     const DeviceModel& device, const CpuModel& cpu, double correction = 1);

struct Opened {
    std::unique_ptr<GraphDevice> device;
    DeviceModel device_model;
    CpuModel cpu_model;
    std::string name;
};

using Opener = std::function<std::optional<Opened>(std::string& reason)>;

struct AutoOptions {
    std::size_t max_jobs = 1;
    std::function<double()> now;
};

class AutoDevice final : public GraphDevice {
public:
    AutoDevice(const nvr::Image& image, Opener opener, AutoOptions options = {});
    ~AutoDevice() override;

    enum class State { Gated, Active, Rejected };
    State state() const;
    const std::string& reason() const;
    const std::string& device_name() const;
    double correction() const;
    std::span<const GraphShape> shapes() const;

    const nvr::Image& image() const override;
    void evaluate(std::span<GraphJob> jobs) override;
    std::size_t batch_hint() const override;
    GraphDeviceStatistics statistics() const override;
    std::span<const GraphShape> graph_shapes() const override;
    std::string_view failure_reason() const override;

private:
    bool evaluate_prepared(std::span<const std::uint32_t> graphs,
                           const detail::MaterializeGraphJobs& materialize) override;
    void evaluate_batch(std::span<GraphJob> jobs, std::span<const std::uint32_t> graphs,
                        const detail::MaterializeGraphJobs* materialize);
    void open();
    void reject(std::string reason);

    const nvr::Image& image_;
    Opener opener_;
    AutoOptions options_;
    State state_ = State::Gated;
    std::string reason_;
    std::string device_name_;
    std::vector<GraphShape> shapes_;
    std::unique_ptr<GraphDevice> inner_;
    DeviceModel device_model_;
    CpuModel cpu_model_;
    GraphDeviceStatistics last_inner_statistics_;
    std::uint64_t declined_placement_ = 0;
    double saved_ = 0;
    double work_ = 0;
    double correction_ = 1;
    bool failed_ = false;

    struct Scratch;
    std::unique_ptr<Scratch> scratch_;
};

} // namespace neura::nvm
