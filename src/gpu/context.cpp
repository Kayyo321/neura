#include "context_internal.hpp"

#include "nvm/layered.hpp"

#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neura::gpu {
namespace {

constexpr std::uint32_t max_enumeration_count = 1024;
constexpr int max_enumeration_attempts = 8;

struct Candidate {
    VkPhysicalDevice physical;
    VkPhysicalDeviceProperties properties;
    std::uint32_t queue_family;
    int rank;
};

int device_rank(VkPhysicalDeviceType type, bool allow_software) {
    switch (type) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 0;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 1;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
    case VK_PHYSICAL_DEVICE_TYPE_OTHER: return 3;
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return allow_software ? 4 : -1;
    default: return -1;
    }
}

std::vector<VkPhysicalDevice> physical_devices(VkInstance instance, const InstanceFunctions& vk) {
    for (int attempt = 0; attempt < max_enumeration_attempts; ++attempt) {
        std::uint32_t count = 0;
        if (vk.EnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS ||
            count == 0 || count > max_enumeration_count) return {};
        std::vector<VkPhysicalDevice> devices(count);
        const auto result = vk.EnumeratePhysicalDevices(instance, &count, devices.data());
        if (result == VK_SUCCESS) {
            if (count > devices.size()) return {};
            devices.resize(count);
            return devices;
        }
        if (result != VK_INCOMPLETE) return {};
    }
    return {};
}

std::vector<Candidate> candidates(VkInstance instance, const InstanceFunctions& vk, bool allow_software) {
    std::vector<Candidate> choices;
    for (const auto physical : physical_devices(instance, vk)) {
        VkPhysicalDeviceProperties properties{};
        vk.GetPhysicalDeviceProperties(physical, &properties);
        const auto rank = device_rank(properties.deviceType, allow_software);
        const auto& limits = properties.limits;
        if (rank < 0 || limits.maxComputeWorkGroupInvocations < nvm::local_size ||
            limits.maxComputeWorkGroupSize[0] < nvm::local_size ||
            limits.maxPerStageDescriptorStorageBuffers < 3 || limits.maxStorageBufferRange < (1U << 20)) {
            continue;
        }

        std::uint32_t count = 0;
        vk.GetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        if (count == 0 || count > max_enumeration_count) continue;
        std::vector<VkQueueFamilyProperties> families(count);
        vk.GetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        if (count > families.size()) continue;
        for (std::uint32_t i = 0; i < count; ++i) {
            if (families[i].queueCount != 0 && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0) {
                choices.push_back({physical, properties, i, rank});
                break;
            }
        }
    }
    std::ranges::stable_sort(choices, {}, &Candidate::rank);
    return choices;
}

template <class Handle, class Info, class Create>
bool create_object(VkDevice device, Handle& target, const Info& info, Create create) {
    Handle created = VK_NULL_HANDLE;
    if (create(device, &info, nullptr, &created) != VK_SUCCESS) return false;
    target = created;
    return true;
}

void note_allocation_failure(std::string& reason) {
    try {
        reason = allocation_failure;
    } catch (const std::bad_alloc&) {
        reason.clear();
    } catch (const std::length_error&) {
        reason.clear();
    }
}

} // namespace

Context::State::~State() {
    destroy_device();
    if (!instance) return;
    if (abandoned_instance || !instance_vk.DestroyInstance) {
        loader.abandon();
    } else {
        instance_vk.DestroyInstance(instance, nullptr);
    }
}

void Context::State::destroy_device() {
    if (!device) return;
    if (timed_out || !vk.DestroyDevice) {
        // Keep the instance and loader as well as the device alive. The
        // same instance can still enumerate and open other candidates.
        abandoned_instance = true;
    } else {
        if (vk.DeviceWaitIdle) vk.DeviceWaitIdle(device);
        if (fence) vk.DestroyFence(device, fence, nullptr);
        if (command_pool) vk.DestroyCommandPool(device, command_pool, nullptr);
        if (pipeline) vk.DestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout) vk.DestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_layout) vk.DestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
        if (shader) vk.DestroyShaderModule(device, shader, nullptr);
        vk.DestroyDevice(device, nullptr);
    }
    device = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
    shader = VK_NULL_HANDLE;
    descriptor_layout = VK_NULL_HANDLE;
    pipeline_layout = VK_NULL_HANDLE;
    pipeline = VK_NULL_HANDLE;
    command_pool = VK_NULL_HANDLE;
    fence = VK_NULL_HANDLE;
    vk = {};
    timed_out = false;
    device_in_use = false;
}

std::string Context::State::create_device(VkPhysicalDevice physical, std::uint32_t queue_family,
                                          const VkPhysicalDeviceProperties& candidate_properties) {
    properties = candidate_properties;
    name = properties.deviceName;
    instance_vk.GetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    VkPhysicalDeviceFeatures available{};
    instance_vk.GetPhysicalDeviceFeatures(physical, &available);
    VkPhysicalDeviceFeatures enabled{};
    enabled.robustBufferAccess = available.robustBufferAccess;

    constexpr float priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.pEnabledFeatures = &enabled;
    VkDevice created = VK_NULL_HANDLE;
    if (instance_vk.CreateDevice(physical, &device_info, nullptr, &created) != VK_SUCCESS) {
        return "cannot create a Vulkan device";
    }
    device = created;
    if (!load_device_functions(device, instance_vk.GetDeviceProcAddr, vk)) return "Vulkan loader not found";
    vk.GetDeviceQueue(device, queue_family, 0, &queue);

    const auto code = graph_eval_words();
    VkShaderModuleCreateInfo shader_info{};
    shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader_info.codeSize = code.size_bytes();
    shader_info.pCode = code.data();
    if (!create_object(device, shader, shader_info, vk.CreateShaderModule)) return "cannot create the graph shader";

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (std::uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo descriptor_info{};
    descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptor_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
    descriptor_info.pBindings = bindings.data();
    if (!create_object(device, descriptor_layout, descriptor_info, vk.CreateDescriptorSetLayout)) {
        return "cannot create the graph descriptor layout";
    }

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_layout;
    if (!create_object(device, pipeline_layout, layout_info, vk.CreatePipelineLayout)) {
        return "cannot create the graph pipeline layout";
    }

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline_info.stage.module = shader;
    pipeline_info.stage.pName = "main";
    pipeline_info.layout = pipeline_layout;
    pipeline_info.basePipelineIndex = -1;
    VkPipeline created_pipeline = VK_NULL_HANDLE;
    if (vk.CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &created_pipeline) != VK_SUCCESS) {
        return "cannot create the graph compute pipeline";
    }
    pipeline = created_pipeline;

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;
    if (!create_object(device, command_pool, pool_info, vk.CreateCommandPool)) return "cannot create a Vulkan command pool";

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (!create_object(device, fence, fence_info, vk.CreateFence)) return "cannot create a Vulkan fence";
    return {};
}

Context::Context(std::unique_ptr<State> state) : state_(std::move(state)) {}
Context::~Context() = default;

std::unique_ptr<Context> Context::create(const ContextOptions& options, std::string& reason) try {
    auto state = std::make_unique<State>();
    if (!state->loader.open()) {
        reason = "Vulkan loader not found";
        return nullptr;
    }
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "neura";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance_info{};
    instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instance_info.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (state->loader.create_instance()(&instance_info, nullptr, &instance) != VK_SUCCESS) {
        reason = "cannot create a Vulkan instance";
        return nullptr;
    }
    state->instance = instance;
    if (!state->loader.load_instance(instance, state->instance_vk)) {
        reason = "Vulkan loader not found";
        return nullptr;
    }

    const auto choices = candidates(instance, state->instance_vk, options.allow_software);
    if (choices.empty()) {
        reason = "no usable Vulkan device";
        return nullptr;
    }

    auto context = std::unique_ptr<Context>(new Context(std::move(state)));
    reason.clear();
    for (const auto& candidate : choices) {
        auto& current = *context->state_;
        auto failure = current.create_device(candidate.physical, candidate.queue_family, candidate.properties);
        if (failure.empty()) {
            if (VulkanGraphDevice::self_test(*context)) {
                reason.clear();
                return context;
            }
            failure = "self-test failed";
        }
        if (reason.empty()) reason = current.name + ": " + failure;
        current.destroy_device();
    }
    return nullptr;
} catch (const std::bad_alloc&) {
    note_allocation_failure(reason);
    return nullptr;
} catch (const std::length_error&) {
    note_allocation_failure(reason);
    return nullptr;
}

const std::string& Context::device_name() const { return state_->name; }

} // namespace neura::gpu
