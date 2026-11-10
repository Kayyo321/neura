#pragma once

#include <vulkan/vulkan.h>

// Keep the dispatch table declarations and dynamic lookups in sync.
#define NEURA_VK_INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) \
    X(EnumeratePhysicalDevices) \
    X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceFeatures) \
    X(GetPhysicalDeviceMemoryProperties) \
    X(CreateDevice) \
    X(GetDeviceProcAddr)

#define NEURA_VK_DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) \
    X(GetDeviceQueue) \
    X(DeviceWaitIdle) \
    X(CreateShaderModule) \
    X(DestroyShaderModule) \
    X(CreateDescriptorSetLayout) \
    X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) \
    X(DestroyPipelineLayout) \
    X(CreateComputePipelines) \
    X(DestroyPipeline) \
    X(CreateCommandPool) \
    X(DestroyCommandPool) \
    X(AllocateCommandBuffers) \
    X(FreeCommandBuffers) \
    X(ResetCommandBuffer) \
    X(BeginCommandBuffer) \
    X(EndCommandBuffer) \
    X(CmdBindPipeline) \
    X(CmdBindDescriptorSets) \
    X(CmdDispatch) \
    X(CmdCopyBuffer) \
    X(CmdPipelineBarrier) \
    X(CreateFence) \
    X(DestroyFence) \
    X(GetFenceStatus) \
    X(WaitForFences) \
    X(ResetFences) \
    X(QueueSubmit) \
    X(CreateBuffer) \
    X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) \
    X(AllocateMemory) \
    X(FreeMemory) \
    X(BindBufferMemory) \
    X(MapMemory) \
    X(UnmapMemory) \
    X(FlushMappedMemoryRanges) \
    X(CreateDescriptorPool) \
    X(DestroyDescriptorPool) \
    X(AllocateDescriptorSets) \
    X(UpdateDescriptorSets)

namespace neura::gpu {

#define NEURA_VK_FIELD(name) PFN_vk##name name = nullptr;
struct InstanceFunctions {
    NEURA_VK_INSTANCE_FUNCTIONS(NEURA_VK_FIELD)
};

struct DeviceFunctions {
    NEURA_VK_DEVICE_FUNCTIONS(NEURA_VK_FIELD)
};
#undef NEURA_VK_FIELD

class VulkanLoader {
public:
    VulkanLoader() = default;
    ~VulkanLoader();
    VulkanLoader(const VulkanLoader&) = delete;
    VulkanLoader& operator=(const VulkanLoader&) = delete;

    bool open();
    PFN_vkCreateInstance create_instance() const;
    bool load_instance(VkInstance instance, InstanceFunctions& functions) const;

    // Keep driver code loaded if submitted work could still be running.
    void abandon();

private:
    void* library_ = nullptr;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr_ = nullptr;
    PFN_vkCreateInstance create_instance_ = nullptr;
};

bool load_device_functions(VkDevice device, PFN_vkGetDeviceProcAddr get_proc_addr, DeviceFunctions& functions);

} // namespace neura::gpu
