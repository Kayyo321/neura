#pragma once

#include "device.hpp"
#include "vulkan_api.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace neura::gpu {

inline constexpr char allocation_failure[] = "cannot allocate device memory";

std::span<const std::uint32_t> graph_eval_words();

struct Context::State {
    ~State();

    std::string create_device(VkPhysicalDevice physical, std::uint32_t queue_family,
                              const VkPhysicalDeviceProperties& candidate_properties);
    void destroy_device();

    VulkanLoader loader;
    InstanceFunctions instance_vk;
    DeviceFunctions vk;
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    VkShaderModule shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::string name;
    bool device_in_use = false;
    bool timed_out = false;
    bool abandoned_instance = false;
};

} // namespace neura::gpu
