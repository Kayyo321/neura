#pragma once

#include "nvm/graph_device.hpp"

#include <optional>
#include <string>
#include <string_view>

#ifdef NEURA_HAS_GPU
#include "gpu/device.hpp"
#include "nvm/placement.hpp"
#include <memory>
#endif

namespace neura::cli {

enum class GpuMode { Off, Always, Auto };

struct GpuOptions {
    std::optional<GpuMode> mode;
    bool required = false;
    bool software = false;
    bool verify = false;

    bool parse(std::string_view arg, bool run_or_test);
    void read_environment();
    void validate() const;
};

class GpuRun {
public:
    GpuRun(const GpuOptions& options, const nvr::Image& image, std::size_t max_jobs);
    nvm::GraphDevice* device();
    int finish(int status, bool stats) const;

private:
    void print_stats() const;
    GpuMode mode_;
#ifdef NEURA_HAS_GPU
    // The context outlives its graph device and the verifying wrapper.
    std::unique_ptr<gpu::Context> context_;
    std::unique_ptr<gpu::VulkanGraphDevice> device_;
    std::unique_ptr<nvm::AutoDevice> auto_;
    std::optional<nvm::VerifyingDevice> verifier_;
    std::string unavailable_;
    bool required_ = false;
#endif
};

} // namespace neura::cli
