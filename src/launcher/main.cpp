#include "container.hpp"

#include "nvr/format.hpp"
#include "runner/runner.hpp"

#include <array>
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

} // namespace

int main(int argc, char** argv) {
    const std::string_view name = argc > 0 && *argv[0] ? argv[0] : "neura-launcher";
    try {
        const auto executable = neura::launcher::read_container("/proc/self/exe");
        const auto image = neura::nvr::load(executable.image);
        std::vector<std::string> arguments;
        for (int i = 1; i < argc; ++i) arguments.emplace_back(argv[i]);
        neura::runner::Host host(executable.policy.capabilities);
        neura::runner::provide_native_library(image, host.services);
        neura::runner::RunOptions options;
        options.limits = executable.policy.limits;
        return neura::runner::run(image, host, arguments, options, name, executable.image.size());
    } catch (const std::exception& error) {
        std::cerr << name << ": error: " << error.what() << '\n';
        return 1;
    }
}
