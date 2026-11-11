// Test-only Vulkan loader shim. Except where a mode injects a fault,
// Vulkan calls run against the real driver.
#include <vulkan/vulkan.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>
#include <new>
#include <stdexcept>
#include <thread>

namespace {

enum class Fault {
    None, Submit, Mismatch, Timeout, Status, BadAlloc, TooManyDevices, IncompleteDevices,
    Pipeline, ContextBadAlloc, DeviceLengthError, StorageRange, MemoryTypeCount, BadAllocSecondDispatch,
};

constexpr std::uint32_t storage_range = 3U << 19;

Fault fault() {
    const auto* mode = std::getenv("NEURA_TEST_VULKAN_FAULT");
    if (!mode) return Fault::None;
    if (std::strcmp(mode, "submit") == 0) return Fault::Submit;
    if (std::strcmp(mode, "mismatch") == 0) return Fault::Mismatch;
    if (std::strcmp(mode, "timeout") == 0) return Fault::Timeout;
    if (std::strcmp(mode, "status") == 0) return Fault::Status;
    if (std::strcmp(mode, "bad_alloc") == 0) return Fault::BadAlloc;
    if (std::strcmp(mode, "too_many_devices") == 0) return Fault::TooManyDevices;
    if (std::strcmp(mode, "incomplete_devices") == 0) return Fault::IncompleteDevices;
    if (std::strcmp(mode, "pipeline") == 0) return Fault::Pipeline;
    if (std::strcmp(mode, "context_bad_alloc") == 0) return Fault::ContextBadAlloc;
    if (std::strcmp(mode, "device_length_error") == 0) return Fault::DeviceLengthError;
    if (std::strcmp(mode, "storage_range") == 0) return Fault::StorageRange;
    if (std::strcmp(mode, "memory_type_count") == 0) return Fault::MemoryTypeCount;
    if (std::strcmp(mode, "bad_alloc_second_dispatch") == 0) return Fault::BadAllocSecondDispatch;
    std::abort();
}

PFN_vkGetInstanceProcAddr real_instance_proc() {
    static const auto function = [] {
        const auto* path = std::getenv("NEURA_TEST_REAL_VULKAN");
        void* library = path ? dlopen(path, RTLD_LOCAL | RTLD_NOW) : nullptr;
        return library ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"))
                       : nullptr;
    }();
    return function;
}

PFN_vkGetDeviceProcAddr real_device_proc = nullptr;
PFN_vkEnumeratePhysicalDevices real_enumerate = nullptr;
PFN_vkGetPhysicalDeviceProperties real_properties = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties real_memory_properties = nullptr;
PFN_vkDestroyInstance real_destroy_instance = nullptr;
PFN_vkBeginCommandBuffer real_begin = nullptr;
PFN_vkCmdDispatch real_dispatch = nullptr;
PFN_vkMapMemory real_map = nullptr;
PFN_vkUnmapMemory real_unmap = nullptr;
PFN_vkQueueSubmit real_submit = nullptr;
PFN_vkResetFences real_reset_fences = nullptr;
PFN_vkGetFenceStatus real_fence_status = nullptr;
PFN_vkWaitForFences real_wait_fences = nullptr;
PFN_vkDeviceWaitIdle real_wait_idle = nullptr;
PFN_vkDestroyDevice real_destroy_device = nullptr;
PFN_vkCreateComputePipelines real_create_pipelines = nullptr;
PFN_vkDestroyPipeline real_destroy_pipeline = nullptr;
PFN_vkCreateBuffer real_create_buffer = nullptr;
PFN_vkGetBufferMemoryRequirements real_memory_requirements = nullptr;
PFN_vkUpdateDescriptorSets real_update_descriptors = nullptr;

std::map<VkCommandBuffer, std::uint32_t> group_counts;
std::map<VkDeviceMemory, std::uint32_t*> mappings;
bool self_test_submitted = false;
bool injected = false;
Fault dispatch_fault = Fault::None;
bool timed_out = false;
unsigned graph_dispatches = 0;

VKAPI_ATTR VkResult VKAPI_CALL enumerate(VkInstance instance, std::uint32_t* count, VkPhysicalDevice* devices) {
    switch (fault()) {
    case Fault::TooManyDevices:
        if (devices) std::abort();
        *count = 65'536;
        return VK_SUCCESS;
    case Fault::IncompleteDevices: {
        static unsigned attempts = 0;
        if (devices && ++attempts > 16) std::abort();
        *count = 1;
        return devices ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case Fault::ContextBadAlloc:
        throw std::bad_alloc{};
    default:
        return real_enumerate(instance, count, devices);
    }
}

VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice device, VkPhysicalDeviceProperties* result) {
    real_properties(device, result);
    if (fault() == Fault::StorageRange) result->limits.maxStorageBufferRange = storage_range;
}

VKAPI_ATTR void VKAPI_CALL memory_properties(VkPhysicalDevice device, VkPhysicalDeviceMemoryProperties* result) {
    real_memory_properties(device, result);
    if (fault() == Fault::MemoryTypeCount) result->memoryTypeCount = UINT32_MAX;
}

VKAPI_ATTR void VKAPI_CALL memory_requirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* result) {
    real_memory_requirements(device, buffer, result);
    if (fault() == Fault::MemoryTypeCount) result->memoryTypeBits = 0;
}

VKAPI_ATTR void VKAPI_CALL destroy_instance(VkInstance instance, const VkAllocationCallbacks* allocator) {
    if (timed_out) std::abort();
    real_destroy_instance(instance, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL create_pipelines(VkDevice device, VkPipelineCache cache, std::uint32_t count,
                                              const VkComputePipelineCreateInfo* infos,
                                              const VkAllocationCallbacks* allocator, VkPipeline* pipelines) {
    if (fault() == Fault::Pipeline) {
        if (count != 1) std::abort();
#if VK_USE_64_BIT_PTR_DEFINES
        pipelines[0] = reinterpret_cast<VkPipeline>(std::uintptr_t{1});
#else
        pipelines[0] = 1;
#endif
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return real_create_pipelines(device, cache, count, infos, allocator, pipelines);
}

VKAPI_ATTR void VKAPI_CALL destroy_pipeline(VkDevice device, VkPipeline pipeline,
                                           const VkAllocationCallbacks* allocator) {
    if (fault() == Fault::Pipeline) std::abort();
    real_destroy_pipeline(device, pipeline, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice device, const VkBufferCreateInfo* info,
                                           const VkAllocationCallbacks* allocator, VkBuffer* buffer) {
    if (self_test_submitted && fault() == Fault::DeviceLengthError) {
        throw std::length_error("injected host allocation failure");
    }
    return real_create_buffer(device, info, allocator, buffer);
}

VKAPI_ATTR void VKAPI_CALL update_descriptors(VkDevice device, std::uint32_t count, const VkWriteDescriptorSet* writes,
                                            std::uint32_t copy_count, const VkCopyDescriptorSet* copies) {
    if (fault() == Fault::StorageRange) {
        for (std::uint32_t i = 0; i < count; ++i) {
            if (writes[i].pBufferInfo->range > storage_range) std::abort();
        }
    }
    real_update_descriptors(device, count, writes, copy_count, copies);
}

VKAPI_ATTR VkResult VKAPI_CALL begin(VkCommandBuffer command, const VkCommandBufferBeginInfo* info) {
    group_counts.erase(command);
    return real_begin(command, info);
}

VKAPI_ATTR void VKAPI_CALL dispatch(VkCommandBuffer command, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    group_counts[command] = x;
    real_dispatch(command, x, y, z);
}

VKAPI_ATTR VkResult VKAPI_CALL map_memory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
                                         VkMemoryMapFlags flags, void** data) {
    const auto result = real_map(device, memory, offset, size, flags, data);
    if (result == VK_SUCCESS) mappings[memory] = static_cast<std::uint32_t*>(*data);
    return result;
}

VKAPI_ATTR void VKAPI_CALL unmap_memory(VkDevice device, VkDeviceMemory memory) {
    mappings.erase(memory);
    real_unmap(device, memory);
}

VKAPI_ATTR VkResult VKAPI_CALL submit(VkQueue queue, std::uint32_t count, const VkSubmitInfo* infos, VkFence fence) {
    if (count == 1 && infos[0].commandBufferCount == 1) {
        const auto groups = group_counts.find(infos[0].pCommandBuffers[0]);
        if (groups != group_counts.end()) {
            // The startup self-test submits at least 128 jobs in one
            // dispatch. This fixture only submits single graph requests.
            // In the second-dispatch mode every dispatch after the startup
            // self-test is a graph dispatch, of any size. Its second one
            // throws std::bad_alloc after the driver has finished it.
            const bool graph_dispatch = fault() == Fault::BadAllocSecondDispatch && self_test_submitted;
            if (graph_dispatch) {
                if (++graph_dispatches == 2) dispatch_fault = Fault::BadAlloc;
            } else if (groups->second >= 128) {
                const auto result = real_submit(queue, count, infos, fence);
                self_test_submitted = result == VK_SUCCESS;
                return result;
            } else if (groups->second == 1 && self_test_submitted && !injected) {
                injected = true;
                dispatch_fault = fault();
                if (dispatch_fault == Fault::Submit) return VK_ERROR_DEVICE_LOST;
                if (dispatch_fault == Fault::Timeout) timed_out = true;
            }
        }
    }
    return real_submit(queue, count, infos, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL reset_fences(VkDevice device, std::uint32_t count, const VkFence* fences) {
    if (timed_out) std::abort();
    const auto active_fault = dispatch_fault;
    dispatch_fault = Fault::None;
    if (active_fault == Fault::Mismatch || active_fault == Fault::Status) {
        // The fixture has zero inputs and one output. Its I/O allocation is
        // the only live mapping after the staging copy has been released.
        // ResetFences is reached only after the actual driver has signaled
        // completion and published the host-visible output.
        if (mappings.size() != 1) std::abort();
        auto* words = mappings.begin()->second;
        const auto output_base = words[3];
        if (output_base != 4 || words[output_base] != 0 || words[output_base + 1] != 2) std::abort();
        if (active_fault == Fault::Mismatch) {
            words[output_base] = 1;
        } else {
            words[output_base + 1] = 0;
        }
    }
    const auto result = real_reset_fences(device, count, fences);
    if (active_fault == Fault::BadAlloc) throw std::bad_alloc{};
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL fence_status(VkDevice device, VkFence fence) {
    return timed_out ? VK_NOT_READY : real_fence_status(device, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL wait_fences(VkDevice device, std::uint32_t count, const VkFence* fences,
                                         VkBool32 wait_all, std::uint64_t timeout) {
    if (!timed_out) return real_wait_fences(device, count, fences, wait_all, timeout);
    if (count != 1 || !wait_all || timeout > 100'000'000) std::abort();
    std::this_thread::sleep_for(std::chrono::nanoseconds(timeout));
    return VK_TIMEOUT;
}

VKAPI_ATTR VkResult VKAPI_CALL wait_idle(VkDevice device) {
    if (timed_out) std::abort();
    return real_wait_idle(device);
}

VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice device, const VkAllocationCallbacks* allocator) {
    if (timed_out) std::abort();
    real_destroy_device(device, allocator);
}

#define INTERCEPT(vulkan_name, original, replacement)                          \
    if (std::strcmp(name, #vulkan_name) == 0) {                                 \
        original = reinterpret_cast<PFN_##vulkan_name>(function);              \
        return reinterpret_cast<PFN_vkVoidFunction>(replacement);              \
    }

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char* name) {
    const auto function = real_device_proc(device, name);
    if (!function) return nullptr;
    INTERCEPT(vkBeginCommandBuffer, real_begin, begin)
    INTERCEPT(vkCmdDispatch, real_dispatch, dispatch)
    INTERCEPT(vkMapMemory, real_map, map_memory)
    INTERCEPT(vkUnmapMemory, real_unmap, unmap_memory)
    INTERCEPT(vkQueueSubmit, real_submit, submit)
    INTERCEPT(vkResetFences, real_reset_fences, reset_fences)
    INTERCEPT(vkGetFenceStatus, real_fence_status, fence_status)
    INTERCEPT(vkWaitForFences, real_wait_fences, wait_fences)
    INTERCEPT(vkDeviceWaitIdle, real_wait_idle, wait_idle)
    INTERCEPT(vkDestroyDevice, real_destroy_device, destroy_device)
    INTERCEPT(vkCreateComputePipelines, real_create_pipelines, create_pipelines)
    INTERCEPT(vkDestroyPipeline, real_destroy_pipeline, destroy_pipeline)
    INTERCEPT(vkCreateBuffer, real_create_buffer, create_buffer)
    INTERCEPT(vkGetBufferMemoryRequirements, real_memory_requirements, memory_requirements)
    INTERCEPT(vkUpdateDescriptorSets, real_update_descriptors, update_descriptors)
    return function;
}

} // namespace

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    const auto get = real_instance_proc();
    if (!get) return nullptr;
    const auto function = get(instance, name);
    if (!function) return nullptr;
    INTERCEPT(vkGetDeviceProcAddr, real_device_proc, device_proc)
    INTERCEPT(vkEnumeratePhysicalDevices, real_enumerate, enumerate)
    INTERCEPT(vkGetPhysicalDeviceProperties, real_properties, properties)
    INTERCEPT(vkGetPhysicalDeviceMemoryProperties, real_memory_properties, memory_properties)
    INTERCEPT(vkDestroyInstance, real_destroy_instance, destroy_instance)
    return function;
}
#undef INTERCEPT
