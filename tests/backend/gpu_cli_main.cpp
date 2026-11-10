#include "gpu/device.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <process.h>
#include <utility>
#include <vector>
#else
#include <unistd.h>
#endif

#ifdef NEURA_TEST_VULKAN_FAULTS
#include <dlfcn.h>
#include <filesystem>

namespace {

bool locate_real_vulkan() {
    void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    Dl_info info{};
    const auto* symbol = dlsym(library, "vkGetInstanceProcAddr");
    std::error_code error;
    std::filesystem::path path;
    if (symbol && dladdr(symbol, &info) && info.dli_fname) {
        path = std::filesystem::canonical(info.dli_fname, error);
    }
    dlclose(library);
    return !error && !path.empty() && setenv("NEURA_TEST_REAL_VULKAN", path.c_str(), 1) == 0;
}

} // namespace
#endif

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: neura_gpu_cli_test <cmake> [args...]\n";
        return 2;
    }
    std::string reason;
    auto context = neura::gpu::Context::create({.allow_software = true}, reason);
    if (!context) {
        std::cout << "gpu: skipped (" << reason << ")\n";
        return 77;
    }
    std::cout << "gpu: device \"" << context->device_name() << "\"\n" << std::flush;
    context.reset();

#ifdef NEURA_TEST_VULKAN_FAULTS
    if (!locate_real_vulkan()) {
        std::cerr << "could not identify the Vulkan loader for fault injection\n";
        return EXIT_FAILURE;
    }
#endif

    // Older CMake script mode cannot return an arbitrary exit code. Probe
    // here and otherwise run the script without a shell.
#if defined(_WIN32)
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];
        if (argument.find_first_of(" \t") != std::string::npos) argument = '"' + argument + '"';
        arguments.push_back(std::move(argument));
    }
    std::vector<const char*> child_argv;
    for (const auto& argument : arguments) child_argv.push_back(argument.c_str());
    child_argv.push_back(nullptr);
    const auto status = _spawnvp(_P_WAIT, argv[1], child_argv.data());
    if (status >= 0) return static_cast<int>(status);
#else
    execvp(argv[1], argv + 1);
#endif
    std::cerr << "could not start " << argv[1] << '\n';
    return EXIT_FAILURE;
}
