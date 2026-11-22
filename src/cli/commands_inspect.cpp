#include "commands.hpp"

#include "launcher/container.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace neura::cli {

using neura::runner::read_file;
using neura::runner::write_file;

int inspect(const Options& options) {
    const auto& path = options.files.front();
    const auto bytes = read_file(path);
    if (!neura::launcher::is_container(bytes) && !neura::launcher::read_descriptor(bytes)) {
        throw std::runtime_error(path.string() + ": not a native executable (no NEURAEXE footer)");
    }
    const auto executable = neura::launcher::read_container(bytes);
    const auto descriptor = neura::launcher::read_descriptor(std::span(bytes).first(executable.launcher_size));
    if (!descriptor) throw std::runtime_error(path.string() + ": not a Neura launcher");
    const auto image = neura::nvr::load(executable.image);
    if (options.output) write_file(*options.output, executable.image);

    const auto& policy = executable.policy;
    const auto& caps = policy.capabilities;
    std::cout << "native executable: " << path.filename().string() << '\n'
              << "launcher: neura " << descriptor->toolchain_version << ", " << descriptor->triple << ", "
              << (descriptor->flavor == 0 ? "default" : "static") << ", runs NVR up to "
              << descriptor->nvr_major << '.' << descriptor->nvr_minor << '\n'
              << "capabilities: console, process exit";
    if (caps.fs) {
        std::cout << ", files";
        if (!caps.fs_prefixes.empty()) {
            std::cout << " (" << caps.fs_prefixes.front();
            for (const auto& prefix : std::span(caps.fs_prefixes).subspan(1)) std::cout << ", " << prefix;
            std::cout << ')';
        }
    }
    if (caps.net) std::cout << ", network";
    if (caps.raw) std::cout << ", raw";
    if (caps.http_client) {
        std::cout << ", http_client";
        if (const auto& origins = caps.http_client_origins) {
            std::cout << " (";
            if (origins->empty()) std::cout << "no origins";
            for (std::size_t i = 0; i < origins->size(); ++i) {
                if (i != 0) std::cout << ", ";
                std::cout << (*origins)[i];
            }
            std::cout << ')';
        }
    }
    if (caps.env) std::cout << ", environment";
    if (caps.clock) std::cout << ", clock";
    if (caps.entropy) std::cout << ", entropy";
    if (caps.native) std::cout << ", native";
    std::cout << "\nlimits: steps " << policy.limits.max_steps << ", frames " << policy.limits.max_frames
              << ", heap cells " << policy.limits.max_heap_cells << ", stack cells " << policy.limits.max_stack_cells
              << (executable.version < 3
                      ? " (default: container version " + std::to_string(executable.version) + " records none)"
                      : "")
              << "\ntasks: at most " << policy.limits.max_tasks << ", quantum " << policy.limits.task_quantum
              << ", queued messages " << policy.limits.max_queued_messages << ", pending requests "
              << policy.limits.max_pending_requests << ", parallel workers " << policy.limits.parallel_workers
              << (executable.version < 2 ? " (defaults: container version 1 records none)" : "")
              << "\nrun options: " << (policy.sealed ? "sealed" : "accepted")
              << "\nimage: " << executable.image.size() << " bytes, NVR 1." << image.minor_version
              << ", " << image.graphs.size() << " graphs, source map " << (image.source_map ? "present" : "absent")
              << ", native library " << (image.native_library ? "present" : "absent") << '\n';
    return 0;
}

} // namespace neura::cli
