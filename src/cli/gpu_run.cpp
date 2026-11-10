#include "gpu_run.hpp"

#ifdef NEURA_HAS_GPU
#include "gpu/calibrate.hpp"
#endif

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace neura::cli {

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
        GpuMode selected;
        if (arg == "--gpu=always") {
            selected = GpuMode::Always;
        } else if (arg == "--gpu=auto") {
            selected = GpuMode::Auto;
        } else if (arg == "--gpu=off" || arg == "--no-gpu") {
            selected = GpuMode::Off;
        } else {
            throw std::runtime_error("expected --gpu=auto, --gpu=always, or --gpu=off");
        }
        if (mode && mode != selected) throw std::runtime_error("conflicting GPU modes");
        mode = selected;
    }
    return true;
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
               [[maybe_unused]] std::size_t max_jobs) {
#ifdef NEURA_HAS_GPU
    required_ = options.required;
    if (options.mode == GpuMode::Auto) {
        nvm::AutoOptions placement;
        placement.max_jobs = max_jobs;
        auto_ = std::make_unique<nvm::AutoDevice>(image, gpu::make_opener(image, {options.software}),
                                                  std::move(placement));
        if (options.verify && auto_->state() != nvm::AutoDevice::State::Rejected) verifier_.emplace(*auto_);
        return;
    }
    if (!required_ && options.mode != GpuMode::Always) return;
    std::string reason;
    context_ = gpu::Context::create({options.software}, reason);
    if (context_) {
        device_ = std::make_unique<gpu::VulkanGraphDevice>(*context_, image);
        if (device_->failed()) {
            reason = device_->reason();
            device_.reset();
        } else if (options.verify) {
            verifier_.emplace(*device_);
        }
    }
    if (required_ && !device_) throw std::runtime_error("--require-gpu: " + reason);
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

int GpuRun::finish(int status) const {
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
