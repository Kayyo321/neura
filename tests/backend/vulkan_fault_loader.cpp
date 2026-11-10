// Test-only Vulkan loader shim. All startup calls run against the real
// driver. One ordinary graph dispatch after startup receives the fault.
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <map>

namespace {

enum class Fault { Submit, Mismatch };

Fault fault() {
    const auto* mode = std::getenv("NEURA_TEST_VULKAN_FAULT");
    if (mode && std::strcmp(mode, "submit") == 0) return Fault::Submit;
    if (mode && std::strcmp(mode, "mismatch") == 0) return Fault::Mismatch;
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
PFN_vkBeginCommandBuffer real_begin = nullptr;
PFN_vkCmdDispatch real_dispatch = nullptr;
PFN_vkMapMemory real_map = nullptr;
PFN_vkUnmapMemory real_unmap = nullptr;
PFN_vkQueueSubmit real_submit = nullptr;
PFN_vkResetFences real_reset_fences = nullptr;

std::map<VkCommandBuffer, std::uint32_t> group_counts;
std::map<VkDeviceMemory, std::uint32_t*> mappings;
bool self_test_submitted = false;
bool injected = false;
bool corrupt_output = false;

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
            if (groups->second >= 128) {
                const auto result = real_submit(queue, count, infos, fence);
                self_test_submitted = result == VK_SUCCESS;
                return result;
            }
            if (groups->second == 1 && self_test_submitted && !injected) {
                injected = true;
                if (fault() == Fault::Submit) return VK_ERROR_DEVICE_LOST;
                corrupt_output = true;
            }
        }
    }
    return real_submit(queue, count, infos, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL reset_fences(VkDevice device, std::uint32_t count, const VkFence* fences) {
    if (corrupt_output) {
        // The fixture has zero inputs and one output. Its I/O allocation is
        // the only live mapping after the staging copy has been released.
        // ResetFences is reached only after the actual driver has signaled
        // completion and published the host-visible output.
        if (mappings.size() != 1) std::abort();
        auto* words = mappings.begin()->second;
        const auto output_base = words[3];
        if (output_base != 4 || words[output_base] != 0 || words[output_base + 1] != 2) std::abort();
        words[output_base] = 1;
        corrupt_output = false;
    }
    return real_reset_fences(device, count, fences);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char* name) {
    const auto function = real_device_proc(device, name);
    if (!function) return nullptr;
#define INTERCEPT(vulkan_name, original, replacement)                           \
    if (std::strcmp(name, #vulkan_name) == 0) {                                 \
        original = reinterpret_cast<PFN_##vulkan_name>(function);              \
        return reinterpret_cast<PFN_vkVoidFunction>(replacement);              \
    }
    INTERCEPT(vkBeginCommandBuffer, real_begin, begin)
    INTERCEPT(vkCmdDispatch, real_dispatch, dispatch)
    INTERCEPT(vkMapMemory, real_map, map_memory)
    INTERCEPT(vkUnmapMemory, real_unmap, unmap_memory)
    INTERCEPT(vkQueueSubmit, real_submit, submit)
    INTERCEPT(vkResetFences, real_reset_fences, reset_fences)
#undef INTERCEPT
    return function;
}

} // namespace

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    const auto get = real_instance_proc();
    if (!get) return nullptr;
    const auto function = get(instance, name);
    if (function && std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        real_device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(function);
        return reinterpret_cast<PFN_vkVoidFunction>(device_proc);
    }
    return function;
}
