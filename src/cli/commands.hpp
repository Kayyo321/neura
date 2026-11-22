#pragma once

// The commands of the Neura tools, one function each (Products030.md). The
// dispatcher and each sibling tool call them after reading the options.

#include "options.hpp"
#include "nvm/machine.hpp"
#include "nvm/tier1.hpp"
#include "nvr/format.hpp"
#include "runner/runner.hpp"

#include <chrono>
#include <iostream>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace neura {
struct IrProgram;
}

namespace neura::cli {

// Shared by the commands that read images (commands_run.cpp).
bool has_image_magic(std::span<const std::uint8_t> bytes);
neura::nvr::Image load_image(const fs::path& path, std::size_t* byte_count = nullptr);
std::unique_ptr<neura::nvm::TierCache> tier_cache(const neura::nvr::Image& image, neura::nvm::RunLimits& limits);
std::optional<neura::nvm::TierTimes> tier_times(const neura::nvm::RunLimits& limits,
                                                std::optional<std::chrono::steady_clock::time_point> started,
                                                bool device);
void report_tiers(const Options& options, const std::unique_ptr<neura::nvm::TierCache>& cache,
                  const std::optional<neura::nvm::TierTimes>& times);

// Writes `text` to -o, or to standard output.
inline void write_text(const Options& options, const std::string& text) {
    if (options.output) {
        neura::runner::write_file(*options.output, std::vector<std::uint8_t>(text.begin(), text.end()));
    } else {
        std::cout << text;
    }
}

// The execution core (neura-run, neura-inspect).
int run(const Options& options);

// The bytes of an image file, or of the image inside a native executable.
std::vector<std::uint8_t> image_bytes_of(std::vector<std::uint8_t> bytes);
// Decodes and validates image bytes read from `path`, with the loader's
// message prefixed by the path on failure.
neura::nvr::Image load_image_bytes(std::span<const std::uint8_t> bytes, const fs::path& path);

// One run of an image, as `neura run` makes it: the code a direct run and a
// worker of the runtime service share (docs/design/Service030.md, "The
// worker"). `bytes` are the image's own bytes, `path` names it in messages,
// and `started` is when the command began, for --tier-stats. `ending`
// receives whether the run trapped.
int run_image(const Options& options, std::span<const std::uint8_t> bytes, const fs::path& path,
              std::optional<std::chrono::steady_clock::time_point> started,
              neura::runner::RunOptions::Ending* ending = nullptr);
int inspect(const Options& options);

// The compiler (neurac, neura-doc, neura-cbind, neura-test).
neura::IrProgram load_source_program(const Options& options);
neura::IrProgram load_program(const Options& options);
int check(const Options& options);
int prove(const Options& options);
int emit_ir(const Options& options);
int build(const Options& options);
int docs(const Options& options);
int c_bindings(const Options& options);
int run_tests(const Options& options);

// The artifact tools of the decompiler (neura-asm, neura-disasm, neura-decompile).
int disassemble(const Options& options);
int assemble(const Options& options);
int decompile(const Options& options);

// The reference tools (neura-explain, neura-tags).
int tags(const Options& options);
int explain(const Options& options);

} // namespace neura::cli
