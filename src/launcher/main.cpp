#include "container.hpp"

#include "nvr/format.hpp"
#include "runner/runner.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view triple = NEURA_LAUNCHER_TRIPLE;
constexpr std::string_view toolchain_version = "development";

[[gnu::used]] constinit const auto descriptor = []() consteval {
    constexpr std::string_view magic = "NEURA-LAUNCHER-1";
    std::array<std::uint8_t, magic.size() + 4 + triple.size() + 1 + 2 + 2 + 4 + 4 + toolchain_version.size()> bytes{};
    std::size_t next = 0;
    const auto raw = [&](std::string_view text) {
        for (const auto value : text) bytes[next++] = static_cast<std::uint8_t>(value);
    };
    const auto number = [&](std::uint32_t value, unsigned width) {
        for (unsigned byte = 0; byte < width; ++byte) bytes[next++] = static_cast<std::uint8_t>(value >> (8 * byte));
    };
    raw(magic);
    number(triple.size(), 4);
    raw(triple);
    number(0, 1);
    number(neura::nvr::major_version, 2);
    number(neura::nvr::minor_version, 2);
    number(neura::launcher::container_version, 4);
    number(toolchain_version.size(), 4);
    raw(toolchain_version);
    return bytes;
}();

neura::runner::RunOptions run_options(const neura::launcher::Policy& policy) {
    neura::runner::RunOptions options;
    options.limits = policy.limits;
    if (policy.sealed) return options;
    const auto* text = std::getenv("NEURA_RUN_OPTIONS");
    if (!text) return options;
    std::string_view input(text);
    const auto next = [&]() {
        const auto first = input.find_first_not_of(" \t");
        if (first == std::string_view::npos) return std::string_view{};
        input.remove_prefix(first);
        const auto end = input.find_first_of(" \t");
        const auto token = input.substr(0, end);
        input.remove_prefix(token.size());
        return token;
    };
    const auto limit = [&](std::string_view flag) {
        const auto token = next();
        std::uint64_t value = 0;
        if (!token.empty()) {
            const auto [end, error] = std::from_chars(token.begin(), token.end(), value);
            if (error == std::errc{} && end == token.end() && value != 0) return value;
        }
        throw std::runtime_error("NEURA_RUN_OPTIONS: expected a positive integer after " + std::string(flag));
    };
    for (auto flag = next(); !flag.empty(); flag = next()) {
        if (flag == "--max-steps") {
            options.limits.max_steps = std::min(options.limits.max_steps, limit(flag));
        } else if (flag == "--max-frames") {
            options.limits.max_frames = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(options.limits.max_frames, limit(flag)));
        } else if (flag == "--max-heap-cells") {
            options.limits.max_heap_cells = std::min(options.limits.max_heap_cells, limit(flag));
        } else if (flag == "--stats") {
            options.stats = true;
        } else if (flag == "--profile") {
            options.limits.profile = true;
        } else if (flag == "--record" || flag == "--replay") {
            const auto path = next();
            if (path.empty()) {
                throw std::runtime_error("NEURA_RUN_OPTIONS: expected a value after " + std::string(flag));
            }
            (flag == "--record" ? options.record : options.replay) = std::filesystem::path(path);
        } else {
            throw std::runtime_error("NEURA_RUN_OPTIONS: " + std::string(flag) +
                                     " is not accepted by a native executable");
        }
    }
    if (options.record && options.replay) {
        throw std::runtime_error("NEURA_RUN_OPTIONS: --record and --replay cannot be combined");
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view name = argc > 0 && *argv[0] ? argv[0] : "neura-launcher";
    try {
        const auto executable = neura::launcher::read_container("/proc/self/exe");
        const auto options = run_options(executable.policy);
        const auto image = neura::nvr::load(executable.image);
        std::vector<std::string> arguments;
        for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
        neura::runner::Host host(executable.policy.capabilities);
        neura::runner::provide_native_library(image, host.services);
        return neura::runner::run(image, host, arguments, options, name, executable.image.size());
    } catch (const std::exception& error) {
        std::cerr << name << ": error: " << error.what() << '\n';
        return 1;
    }
}
