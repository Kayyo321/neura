#include "gpu_run.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>

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
    if (mode == GpuMode::Off) {
        if (required) throw std::runtime_error("--require-gpu needs --gpu=always");
        if (software) throw std::runtime_error("--gpu-software cannot be combined with --gpu=off");
        if (verify) throw std::runtime_error("--gpu-verify cannot be combined with --gpu=off");
    }
}

GpuRun::GpuRun(const GpuOptions& options, [[maybe_unused]] const nvr::Image& image) {
#ifdef NEURA_HAS_GPU
    required_ = options.required;
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
