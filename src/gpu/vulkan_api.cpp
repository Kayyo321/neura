#include "vulkan_api.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace neura::gpu {

VulkanLoader::~VulkanLoader() {
    if (!library_) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library_));
#else
    dlclose(library_);
#endif
}

bool VulkanLoader::open() {
#ifdef _WIN32
    const auto library = LoadLibraryExA("vulkan-1.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) return false;
    library_ = library;
    get_instance_proc_addr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr"));
#else
    library_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library_) return false;
    get_instance_proc_addr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library_, "vkGetInstanceProcAddr"));
#endif
    if (!get_instance_proc_addr_) return false;
    create_instance_ = reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc_addr_(VK_NULL_HANDLE, "vkCreateInstance"));
    return create_instance_ != nullptr;
}

PFN_vkCreateInstance VulkanLoader::create_instance() const { return create_instance_; }

bool VulkanLoader::load_instance(VkInstance instance, InstanceFunctions& functions) const {
    bool complete = true;
#define NEURA_VK_LOAD(name) \
    functions.name = reinterpret_cast<PFN_vk##name>(get_instance_proc_addr_(instance, "vk" #name)); \
    complete &= functions.name != nullptr;
    NEURA_VK_INSTANCE_FUNCTIONS(NEURA_VK_LOAD)
#undef NEURA_VK_LOAD
    return complete;
}

bool load_device_functions(VkDevice device, PFN_vkGetDeviceProcAddr get_proc_addr, DeviceFunctions& functions) {
    bool complete = true;
#define NEURA_VK_LOAD(name) \
    functions.name = reinterpret_cast<PFN_vk##name>(get_proc_addr(device, "vk" #name)); \
    complete &= functions.name != nullptr;
    NEURA_VK_DEVICE_FUNCTIONS(NEURA_VK_LOAD)
#undef NEURA_VK_LOAD
    return complete;
}

void VulkanLoader::abandon() { library_ = nullptr; }

} // namespace neura::gpu
