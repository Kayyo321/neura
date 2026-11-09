#pragma once

#include "ir-optimization/support.hpp"
#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvr/format.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace neura::testing {

// Paths of backend test inputs.
fs::path backend_dir(const Environment& environment);
fs::path program_path(const Environment& environment, std::string_view name);

// The model a hand-assembled fixture must decode to.
nvr::Image hello_model();
// A second fixture with graphs, a call, a selector, state operations, a
// global, a layout, and a source map. Its entry function returns 7.
nvr::Image logic_model();
// An NVR 1.1 image: allocates a list, reads its structural version, and
// traps with reason 5. version_bytes assembles it with the given header minor
// version.
nvr::Image version_model();
std::vector<std::uint8_t> version_bytes(std::uint16_t minor);
std::vector<std::uint8_t> logic_bytes();

struct Captured {
    nvm::RunResult result;
    std::string output;
};

// Runs an image with neura.console.write_line bound to an in-memory sink.
Captured run_captured(const nvr::Image& image, nvm::RunLimits limits = {},
                      std::optional<std::size_t> capacity = std::nullopt,
                      const std::vector<std::string>& arguments = {});

// Lowers and builds a program the way `neura build` does.
nvr::Image build_program(const std::vector<fs::path>& files, const std::vector<fs::path>& roots = {},
                         bool optimize = true);

std::string describe_trap(const nvm::RunResult& result, const nvr::Image& image);

} // namespace neura::testing
