#pragma once

#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"

#include <filesystem>
#include <optional>

namespace neura::runner {

struct Host {
    nvm::StreamSink output{stdout};
    nvm::StreamSink error{stderr};
    nvm::DescriptorSource input{0};
    nvm::ConsoleWriteLine console{output};
    nvm::StandardServices services;
    nvm::HostRegistry registry;

    explicit Host(const nvm::Capabilities& capabilities);
};

struct RunOptions {
    nvm::RunLimits limits;
    bool stats = false;
    std::optional<std::filesystem::path> record;
    std::optional<std::filesystem::path> replay;
};

std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);
std::string image_costs(const nvr::Image& image);
void provide_native_library(const nvr::Image& image, nvm::StandardServices& services);

int run(const nvr::Image& image, Host& host, std::span<const std::string> arguments,
        const RunOptions& options, std::string_view name, std::size_t image_bytes);

} // namespace neura::runner
