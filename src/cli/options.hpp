#pragma once

// The command-line options of every Neura tool (Tooling.md): the commands,
// the option reader, and the usage text. The reader is shared by the
// sibling tools and the neura dispatcher's forms of each command, so a
// command reads its arguments the same way in both (Products030.md). It
// links no compiler code.

#include "c-import/c_import.hpp"
#include "tool.hpp"
#include "diagnostic_render.hpp"
#include "gpu_run.hpp"
#include "lifecycle.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace neura::cli {

namespace fs = std::filesystem;

using neura::tool::usage;

enum class Command {
    Check, Docs, EmitIr, Build, Inspect, Run, Test, Disassemble, Assemble, Decompile, CBindings,
    Version, Help, Doctor, Update, Uninstall, Explain, Tags
};

inline constexpr const char* commands = neura::tool::command_list;

// How `run` reaches the runtime (docs/design/Service030.md): directly, the
// default, or through the resident service.
enum class RuntimeMode { Off, Auto, Required };

// An error of the service options or of a service launch: printed after
// `neura: runtime: `, with its own exit status (1 for a usage error, 5 when
// the service ran nothing, 6 when the outcome is unknown).
class RuntimeError : public std::runtime_error {
public:
    RuntimeError(const std::string& message, int status) : std::runtime_error(message), status_(status) {}
    int status() const { return status_; }

private:
    int status_;
};

neura::nvm::Capabilities granted_capabilities();

struct Options {
    Command command = Command::Check;
    bool optimize = true;
    bool source_map = true;
    // build: emit the optional LIFT section (--no-lift-hints omits it).
    bool lift_hints = true;
    // run and test: report blocks lifted and steps per execution tier.
    bool tier_stats = false;
    bool provenance = true;
    bool doxygen = false;
    bool native_executable = false;
    bool sealed = false;
    bool static_launcher = false;
    std::optional<std::string> native_target;
    std::optional<fs::path> launcher;
    // Report execution costs: steps, peak heap cells, and image size.
    bool stats = false;
    std::optional<neura::nvm::TaskDumpFormat> task_dump;
    // check --task-safety: report every function instead of only checking.
    bool task_safety = false;
    // check --fix: apply machine-applicable suggestions in place.
    bool fix = false;
    // Editor test discovery and execution. Human terminal output is the default.
    bool list_tests = false;
    bool test_format_json = false;
    bool test_format_set = false;
    bool test_reporter_json = false;
    std::vector<std::string> test_filters;
    std::optional<fs::path> test_workspace;
    std::optional<fs::path> test_coverage;
    neura::cli::DiagnosticConfig diagnostics;
    std::optional<fs::path> trace_events;
    bool trace_wall_time = false;
    // Testing concurrent code (Concurrency.md section 14.4).
    bool virtual_time = false;
    std::uint64_t schedules = 1;
    std::uint64_t schedule_seed = 0;
    bool exploration_options = false;
    std::optional<fs::path> save_schedule;
    std::optional<fs::path> schedule;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
    fs::path library;
    fs::path executable;
    std::optional<fs::path> output;
    std::vector<std::string> arguments;
    neura::nvm::RunLimits limits;
    // The command-line tools start with every group granted; the C++
    // default of nvm::Capabilities denies every group.
    neura::nvm::Capabilities capabilities = granted_capabilities();
    // An explicit --allow-native that is still in effect; the static
    // launcher cannot honor it.
    bool allow_native_requested = false;
    std::optional<fs::path> record;
    std::optional<fs::path> replay;
    std::optional<fs::path> each;
    // run --runtime=MODE; unset is the default, a direct run.
    std::optional<RuntimeMode> runtime;
    // run --detach --stdout=FILE --stderr=FILE [--stdin=FILE].
    bool detach = false;
    std::optional<fs::path> detach_stdout;
    std::optional<fs::path> detach_stderr;
    std::optional<fs::path> detach_stdin;
    // run --runtime-report=FILE: one JSON object about the launch.
    std::optional<fs::path> runtime_report;
    neura::cli::GpuOptions gpu;
    // C imports: header search and definitions, the libclang library, and
    // the C compiler and extra arguments that link the native library.
    neura::CImportOptions c_import;
    std::string cc;
    std::vector<std::string> link_arguments;
    // c-bindings: generate as if the import named libraries.
    bool linked = false;
    // explain: a code, or --list.
    std::string explain_code;
    bool explain_list = false;
    // update and uninstall.
    neura::lifecycle::UpdateOptions update;
    neura::lifecycle::UninstallOptions uninstall;
};

using neura::tool::executable_path;

// The standard library next to an executable in <prefix>/bin.
fs::path standard_library(const fs::path& executable);

// Reads `argv[1]` as the command and the rest as its options.
Options read_options(int argc, char** argv);

} // namespace neura::cli
