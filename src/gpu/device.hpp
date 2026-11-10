#pragma once

#include "nvm/layered.hpp"

#include <memory>
#include <string>

namespace neura::gpu {

struct ContextOptions {
    bool allow_software = false;
};

class Context {
public:
    static std::unique_ptr<Context> create(const ContextOptions& options, std::string& reason);
    ~Context();

    const std::string& device_name() const;
    std::uint32_t max_storage_buffer_range() const;
    std::uint32_t max_requests_per_dispatch() const;

private:
    friend class VulkanGraphDevice;
    struct State;
    explicit Context(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

// Borrows the context and the image. The context serves one device at a time.
class VulkanGraphDevice final : public nvm::GraphDevice {
public:
    VulkanGraphDevice(Context& context, const nvr::Image& image);
    ~VulkanGraphDevice() override;

    const nvr::Image& image() const override;
    void evaluate(std::span<nvm::GraphJob> jobs) override;
    std::size_t batch_hint() const override;
    nvm::GraphDeviceStatistics statistics() const override;

    bool failed() const;
    const std::string& reason() const;
    const nvm::LayeredTables& tables() const;

private:
    friend class Context;
    static bool self_test(Context& context);
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace neura::gpu
