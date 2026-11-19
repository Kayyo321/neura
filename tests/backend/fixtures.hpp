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
// tests/backend/tier/multi-result.nvra, assembled: an NVR 1.7 image whose
// functions return two and three words (the compiler does not emit them yet).
nvr::Image multi_result_image(const Environment& environment);
// tests/backend/tier/aggregates.nvra, assembled and given the lift hints of its
// graphs: an NVR 1.7 image with arrays and lists of aggregate elements. The
// entry's argument count selects a trap case (1 to 4).
nvr::Image aggregate_image(const Environment& environment);
// tests/backend/tier/region-storage.nvra, assembled and given the lift hints of its
// graphs: an NVR 1.7 image with storage regions (the compiler does not emit them yet).
// The entry's argument count selects a trap case (1 to 9).
nvr::Image region_image(const Environment& environment);
// tests/backend/tier/region-tiers.nvra, assembled and given the lift hints of its graphs:
// storage regions in hot loops, recursion and calls that tiers 2 and 3 compile. The entry's
// argument count selects a trap case (1 or 2).
nvr::Image region_tiers_image(const Environment& environment);
// tests/backend/tier/region-tasks.nvra and region-tasks-token.nvra, assembled: three tasks with
// regions of their own, and the variant in which a child uses the root's token.
nvr::Image region_tasks_image(const Environment& environment, bool token_variant = false);
// tests/backend/tier/aggregate-cells.nvra, assembled: the cell accounting of
// aggregate collections (peak 18 cells, 15 at the end).
nvr::Image aggregate_cells_image(const Environment& environment);

// One image of the corpus that the decompiler tests use.
struct CorpusImage {
    std::string label;
    std::vector<std::uint8_t> bytes;
    // Whether the compiler built it, rather than a hand-assembled fixture.
    bool compiled = false;
    bool optimized = false;
    bool source_map = false;
};

// Every test and example program that has an entry point, built optimized
// and unoptimized, with and without a source map, plus the hand-assembled
// fixtures. Built once per process.
const std::vector<CorpusImage>& image_corpus(const Environment& environment);

struct Captured {
    nvm::RunResult result;
    std::string output;
    std::vector<nvm::HostRequest> requests;
};

// The corpus needs only console output. Keep a copy of its requests as well
// as the bytes written, so differential tests can compare both boundaries.
class CapturingConsole final : public nvm::HostService {
public:
    explicit CapturingConsole(std::optional<std::size_t> capacity = std::nullopt);
    const nvm::HostSignature& signature() const override;
    nvm::HostCompletion invoke(const nvm::HostRequest& request) override;
    const std::string& output() const { return sink_.contents(); }

    std::vector<nvm::HostRequest> requests;

private:
    nvm::StringSink sink_;
    nvm::ConsoleWriteLine console_;
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
