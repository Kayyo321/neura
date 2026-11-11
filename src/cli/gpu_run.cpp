#include "gpu_run.hpp"

#ifdef NEURA_HAS_GPU
#include "gpu/calibrate.hpp"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace neura::cli {
namespace {

std::optional<GpuMode> parse_mode(std::string_view mode) {
    if (mode == "auto") return GpuMode::Auto;
    if (mode == "always") return GpuMode::Always;
    if (mode == "off") return GpuMode::Off;
    return std::nullopt;
}

} // namespace

bool GpuOptions::parse(std::string_view arg, bool run_or_test) {
    if (!arg.starts_with("--gpu=") && arg != "--no-gpu" && arg != "--require-gpu" &&
        arg != "--gpu-software" && arg != "--gpu-verify") {
        return false;
    }
    if (!run_or_test) {
        throw std::runtime_error(std::string(arg) + " applies only to run and test");
    }
    if (arg == "--require-gpu") {
        required = true;
    } else if (arg == "--gpu-software") {
        software = true;
    } else if (arg == "--gpu-verify") {
        verify = true;
    } else {
        const auto selected = arg == "--no-gpu" ? std::optional(GpuMode::Off) : parse_mode(arg.substr(6));
        if (!selected) {
            throw std::runtime_error("expected --gpu=auto, --gpu=always, or --gpu=off");
        }
        if (mode && mode != selected) throw std::runtime_error("conflicting GPU modes");
        mode = selected;
    }
    return true;
}

void GpuOptions::read_environment() {
    if (mode || required) return;
    const auto* value = std::getenv("NEURA_GPU");
    if (!value || *value == '\0') return;
    mode = parse_mode(value);
    if (!mode) throw std::runtime_error("NEURA_GPU must be auto, always, or off");
}

void GpuOptions::validate() const {
    if (required && mode && mode != GpuMode::Always) {
        throw std::runtime_error("--require-gpu needs --gpu=always");
    }
    if (mode == GpuMode::Off) {
        if (software) throw std::runtime_error("--gpu-software cannot be combined with --gpu=off");
        if (verify) throw std::runtime_error("--gpu-verify cannot be combined with --gpu=off");
    }
}

GpuRun::GpuRun(const GpuOptions& options, [[maybe_unused]] const nvr::Image& image,
               [[maybe_unused]] std::size_t max_jobs)
    : mode_(options.required ? GpuMode::Always : options.mode.value_or(GpuMode::Auto)) {
#ifdef NEURA_HAS_GPU
    required_ = options.required;
    if (mode_ == GpuMode::Off) return;
    if (mode_ == GpuMode::Auto) {
        nvm::AutoOptions placement;
        placement.max_jobs = max_jobs;
        auto_ = std::make_unique<nvm::AutoDevice>(image, gpu::make_opener(image, {options.software}),
                                                  std::move(placement));
        if (options.verify && auto_->state() != nvm::AutoDevice::State::Rejected) verifier_.emplace(*auto_);
        return;
    }
    context_ = gpu::Context::create({options.software}, unavailable_);
    if (context_) {
        device_ = std::make_unique<gpu::VulkanGraphDevice>(*context_, image);
        if (device_->failed()) {
            unavailable_ = device_->reason();
            device_.reset();
        } else if (options.verify) {
            verifier_.emplace(*device_);
        }
    }
    if (required_ && !device_) throw std::runtime_error("--require-gpu: " + unavailable_);
#else
    if (options.required) throw std::runtime_error("--require-gpu: this build has no GPU support");
#endif
}

nvm::GraphDevice* GpuRun::device() {
#ifdef NEURA_HAS_GPU
    if (verifier_) return &*verifier_;
    if (auto_ && auto_->state() != nvm::AutoDevice::State::Rejected) return auto_.get();
    return device_.get();
#else
    return nullptr;
#endif
}

void GpuRun::print_stats() const {
    if (mode_ == GpuMode::Off) {
        std::cerr << "gpu: off\n";
        return;
    }
#ifdef NEURA_HAS_GPU
    if (mode_ == GpuMode::Always && !device_) {
        std::cerr << "gpu: always, unavailable (" << unavailable_ << ")\n";
        return;
    }
    if (auto_ && auto_->device_name().empty()) {
        auto reason = std::string_view(auto_->reason());
        if (auto_->state() == nvm::AutoDevice::State::Gated) {
            reason = "the workload stayed under the opening budget";
        }
        std::cerr << "gpu: auto, not used (" << reason << ")\n";
        return;
    }
    const nvm::GraphDevice& graph_device = auto_ ? static_cast<const nvm::GraphDevice&>(*auto_) : *device_;
    const auto& name = auto_ ? auto_->device_name() : context_->device_name();
    const auto shapes = graph_device.graph_shapes();
    const auto eligible = std::ranges::count_if(shapes, [](const auto& shape) { return shape.eligible; });
    const auto stats = graph_device.statistics();
    std::cerr << "gpu: " << (auto_ ? "auto" : "always") << ", device \"" << name
              << "\", eligible graphs " << eligible << " of " << shapes.size() << ", evaluated " << stats.evaluated;
    if (auto_) std::cerr << ", kept on cpu " << stats.declined_placement;
    std::cerr << ", ineligible " << stats.declined_ineligible << ", escaped " << stats.escaped
              << ", failed " << stats.declined_failed << ", dispatches " << stats.dispatches;
    if (auto_) {
        std::ostringstream correction;
        correction << std::fixed << std::setprecision(2) << auto_->correction();
        std::cerr << ", correction " << correction.str();
        // A retired device was kept and evaluated jobs; the counters above are
        // those it accumulated before it was retired. It has not failed.
        if (auto_->retired()) {
            std::cerr << ", retired (" << auto_->reason() << ')';
        } else if (auto_->state() == nvm::AutoDevice::State::Rejected) {
            std::cerr << ", rejected (" << auto_->reason() << ')';
        }
    }
    std::cerr << '\n';
#else
    std::cerr << "gpu: not built\n";
#endif
}

int GpuRun::finish(int status, bool stats) const {
    if (stats) print_stats();
#ifdef NEURA_HAS_GPU
    if (required_ && device_ && device_->failed()) {
        std::cout.flush();
        std::fflush(stdout);
        throw std::runtime_error("--require-gpu: the device failed during the run: " + device_->reason());
    }
#endif
    return status;
}

} // namespace neura::cli
