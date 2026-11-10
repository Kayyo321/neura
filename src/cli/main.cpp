#include "argument_lists.hpp"
#include "backend/converter.hpp"
#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"
#include "gpu_run.hpp"
#include "ir-optimization/optimizer.hpp"
#include "launcher/container.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/fleet.hpp"
#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"
#include "nvr/format.hpp"
#include "runner/runner.hpp"
#include "source_docs.hpp"
#include "source_loader.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

namespace fs = std::filesystem;

using neura::runner::Host;
using neura::runner::image_costs;
using neura::runner::provide_native_library;
using neura::runner::read_file;
using neura::runner::write_file;

void usage(std::ostream& out) {
    out << "usage: neura check [-I source-root] <file.nra>...\n"
           "       neura docs <file.nra>... [-o <file.json>]\n"
           "       neura docs --doxygen <file.nra>\n"
           "       neura emit-ir [-I source-root] [--no-optimize] <file.nra>... [-o <file.nir>]\n"
           "       neura emit-ir [--no-optimize] <file.nir>\n"
           "       neura build [-I source-root] [--no-optimize] [--no-source-map] <file.nra>... -o <file.nvr>\n"
           "       neura build [--no-optimize] [--no-source-map] <file.nir> -o <file.nvr>\n"
           "       neura build --native [policy] [--static] [--target <triple>] [--launcher <file>]\n"
           "                  <file.nra>... | <file.nir> | <file.nvr> -o <file>\n"
           "       neura inspect <file> [-o <file.nvr>]\n"
           "       neura run [--max-steps N] [--max-frames N] [--max-heap-cells N] [--stats] [--profile] [capabilities] "
           "[--record <file> | --replay <file>] [--each <lists>] [--gpu=auto|always|off] [--no-gpu] "
           "[--require-gpu] [--gpu-software] [--gpu-verify] <file.nvr> [-- args...]\n"
           "       neura test [-I source-root] [--no-optimize] [--max-steps N] [--max-frames N]\n"
           "                  [--max-heap-cells N] [--stats] [capabilities] [--gpu=auto|always|off] [--no-gpu]\n"
           "                  [--require-gpu] [--gpu-software] [--gpu-verify] <file.nra>...\n"
           "       neura disassemble <file.nvr> [-o <file.nvra>]\n"
           "       neura assemble <file.nvra> -o <file.nvr>\n"
           "       neura decompile [--no-provenance] <file.nvr> [-o <file>]\n"
           "       neura c-bindings [--linked] [c-options] <header.h>\n"
           "\n"
           "capabilities: run and test allow only console input and output (and process exit) by\n"
           "default; a denied host service fails with error 14 (CapabilityDenied).\n"
           "  --allow-fs[=prefix,...]  files and directories, optionally only below the prefixes\n"
           "  --allow-net              TCP connections and listeners\n"
           "  --allow-env              environment variables\n"
           "  --allow-clock            wall and monotonic clocks and sleeping\n"
           "  --allow-entropy          operating-system entropy\n"
           "  --allow-native           native libraries, memory, and calls (unsafe)\n"
           "  --record <file>          (run) write every host request and response to a trace\n"
           "  --profile                (run) print the steps each function took\n"
           "  --replay <file>          (run) answer host requests from a trace instead of services; a trace of a\n"
           "                           concurrent run carries its own quantum and limits, which replace the options\n"
           "\n"
           "graph execution: run and test choose between the CPU and a GPU by predicted speed.\n"
           "  --gpu=auto               decide automatically (default)\n"
           "  --gpu=always             evaluate every eligible graph on a GPU when one is usable\n"
           "  --gpu=off, --no-gpu      evaluate graphs on the CPU\n"
           "  --require-gpu            --gpu=always, and fail unless a GPU device is available\n"
           "  --gpu-software           also accept software Vulkan implementations\n"
           "  --gpu-verify             check every GPU evaluation against the CPU\n"
           "  --each <lists>           (run) run the image once per line of <lists>; each line is one argument list\n"
           "  --max-tasks N            bound the number of live tasks in a concurrent image\n"
           "  --task-quantum N         polls per turn for a concurrent image\n"
           "  --max-queued-messages N  bound the values buffered in all channels\n"
           "  --max-pending-requests N bound the overlapping host requests in flight\n"
           "  --task-dump[=text|json]  print every task when one traps; SIGQUIT prints a dump at any time\n"
           "  --trace-events FILE      write one JSON line per scheduler event (run only)\n"
           "  --trace-wall-time        add wall-clock times to --trace-events\n"
           "\n"
           "c-options: check, emit-ir, build, test, and c-bindings accept these for import:c(...)\n"
           "  --c-include <dir>        search a directory for C headers\n"
           "  --c-define <name[=value]> define a macro while reading C headers\n"
           "  --libclang <file>        the libclang library to load (default: NEURA_LIBCLANG, then a search)\n"
           "  --cc <compiler>          the C compiler that links the native library (default: $CC, then cc)\n"
           "  --c-link-arg <arg>       pass an argument to that link\n";
}

enum class Command { Check, Docs, EmitIr, Build, Inspect, Run, Test, Disassemble, Assemble, Decompile, CBindings };

constexpr const char* commands =
    "expected check, docs, emit-ir, build, inspect, run, test, disassemble, assemble, decompile, or c-bindings";

struct Options {
    Command command = Command::Check;
    bool optimize = true;
    bool source_map = true;
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
    std::optional<fs::path> trace_events;
    bool trace_wall_time = false;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
    fs::path library;
    fs::path executable;
    std::optional<fs::path> output;
    std::vector<std::string> arguments;
    neura::nvm::RunLimits limits;
    neura::nvm::Capabilities capabilities;
    // A plain --allow-fs grants every path, whatever prefixes are also given.
    bool all_paths = false;
    std::optional<fs::path> record;
    std::optional<fs::path> replay;
    std::optional<fs::path> each;
    neura::cli::GpuOptions gpu;
    // C imports: header search and definitions, the libclang library, and
    // the C compiler and extra arguments that link the native library.
    neura::CImportOptions c_import;
    std::string cc;
    std::vector<std::string> link_arguments;
    // c-bindings: generate as if the import named libraries.
    bool linked = false;
};

fs::path executable_path(const fs::path& launch) {
    std::error_code error;
#ifdef __linux__
    if (const auto actual = fs::canonical("/proc/self/exe", error); !actual.empty()) return actual;
#endif
    if (launch.has_parent_path()) return fs::canonical(launch, error);
    const auto* path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    constexpr char separator = ';';
    auto file = launch;
    if (!file.has_extension()) file += ".exe";
#else
    constexpr char separator = ':';
    const auto& file = launch;
#endif
    std::string_view remaining(path);
    for (;;) {
        const auto end = remaining.find(separator);
        const auto candidate = fs::path(remaining.substr(0, end)) / file;
        if (fs::is_regular_file(candidate, error)) return fs::canonical(candidate, error);
        if (end == std::string_view::npos) return {};
        remaining.remove_prefix(end + 1);
    }
}

fs::path standard_library(const fs::path& executable) {
    if (executable.empty()) return {};
    const auto prefix = executable.parent_path().parent_path();
    std::error_code error;
    for (const auto& candidate : {prefix / "share/neura/stdlib", prefix / "stdlib"}) {
        if (fs::is_directory(candidate / "std", error)) return fs::canonical(candidate, error);
    }
    return {};
}

std::uint64_t parse_limit(std::string_view option, const char* text) {
    std::uint64_t value = 0;
    const std::string_view digits(text);
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc{} || end != digits.data() + digits.size() || value == 0) {
        throw std::runtime_error("expected a positive integer after " + std::string(option));
    }
    return value;
}

template <class Next>
bool parse_policy_option(Options& options, std::string_view arg, Next next) {
    if (arg == "--max-steps") {
        options.limits.max_steps = parse_limit(arg, next());
    } else if (arg == "--max-frames") {
        const auto value = parse_limit(arg, next());
        if (value > 0xffffffffU) {
            throw std::runtime_error("--max-frames is too large");
        }
        options.limits.max_frames = static_cast<std::uint32_t>(value);
    } else if (arg == "--max-heap-cells") {
        options.limits.max_heap_cells = parse_limit(arg, next());
    } else if (arg == "--allow-fs") {
        options.capabilities.fs = true;
        options.all_paths = true;
    } else if (arg.starts_with("--allow-fs=")) {
        options.capabilities.fs = true;
        auto list = arg.substr(std::string_view("--allow-fs=").size());
        for (;;) {
            const auto comma = list.find(',');
            const auto prefix = list.substr(0, comma);
            if (prefix.empty()) {
                throw std::runtime_error("--allow-fs= expects a comma-separated list of path prefixes");
            }
            options.capabilities.fs_prefixes.emplace_back(prefix);
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
    } else if (arg == "--allow-net") {
        options.capabilities.net = true;
    } else if (arg == "--allow-env") {
        options.capabilities.env = true;
    } else if (arg == "--allow-clock") {
        options.capabilities.clock = true;
    } else if (arg == "--allow-entropy") {
        options.capabilities.entropy = true;
    } else if (arg == "--allow-native") {
        options.capabilities.native = true;
    } else {
        return false;
    }
    return true;
}

Options read_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error(commands);
    }
    Options options;
    options.executable = executable_path(argv[0]);
    options.library = standard_library(options.executable);
    const std::string_view command = argv[1];
    if (command == "check") {
        options.command = Command::Check;
    } else if (command == "docs") {
        options.command = Command::Docs;
    } else if (command == "emit-ir") {
        options.command = Command::EmitIr;
    } else if (command == "build") {
        options.command = Command::Build;
    } else if (command == "inspect") {
        options.command = Command::Inspect;
    } else if (command == "run") {
        options.command = Command::Run;
    } else if (command == "test") {
        options.command = Command::Test;
    } else if (command == "disassemble") {
        options.command = Command::Disassemble;
    } else if (command == "assemble") {
        options.command = Command::Assemble;
    } else if (command == "decompile") {
        options.command = Command::Decompile;
    } else if (command == "c-bindings") {
        options.command = Command::CBindings;
    } else {
        throw std::runtime_error(commands);
    }
    const bool runs = options.command == Command::Run || options.command == Command::Test;
    const bool compiles = options.command == Command::Check || options.command == Command::EmitIr ||
                          options.command == Command::Build || options.command == Command::Test ||
                          options.command == Command::CBindings;
    const bool single_input = options.command == Command::Run || options.command == Command::Disassemble ||
                              options.command == Command::Assemble || options.command == Command::Decompile ||
                              options.command == Command::CBindings || options.command == Command::Inspect;
    const bool builds = options.command == Command::Build;
    std::optional<std::string_view> first_policy_flag;
    bool positional_only = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (!positional_only && options.gpu.parse(arg, runs)) continue;
        auto next = [&]() -> const char* {
            if (++i == argc) {
                throw std::runtime_error("expected a value after " + std::string(arg));
            }
            return argv[i];
        };
        if (!positional_only && (runs || builds) && parse_policy_option(options, arg, next)) {
            if (builds && !first_policy_flag) first_policy_flag = arg;
            continue;
        }
        if (positional_only) {
            if (options.command == Command::Run) {
                options.arguments.emplace_back(arg);
            } else {
                options.files.push_back(fs::canonical(argv[i]));
            }
        } else if (arg == "--") {
            positional_only = true;
        } else if (compiles && arg == "-I") {
            const auto root = fs::canonical(next());
            if (!fs::is_directory(root)) {
                throw std::runtime_error("source root is not a directory: " + root.string());
            }
            options.roots.push_back(root);
        } else if (arg == "--no-optimize") {
            if (options.command != Command::EmitIr && options.command != Command::Build &&
                options.command != Command::Test) {
                throw std::runtime_error("--no-optimize applies only to emit-ir, build, and test");
            }
            options.optimize = false;
        } else if (arg == "--no-provenance") {
            if (options.command != Command::Decompile) {
                throw std::runtime_error("--no-provenance applies only to decompile");
            }
            options.provenance = false;
        } else if (arg == "--doxygen") {
            if (options.command != Command::Docs)
                throw std::runtime_error("--doxygen applies only to docs");
            options.doxygen = true;
        } else if (arg == "--native") {
            if (options.command != Command::Build) throw std::runtime_error("--native applies only to build");
            options.native_executable = true;
        } else if (arg == "--sealed") {
            if (!builds) throw std::runtime_error("--sealed applies only with --native");
            options.sealed = true;
            if (!first_policy_flag) first_policy_flag = arg;
        } else if (arg == "--static" || arg == "--target" || arg == "--launcher") {
            if (!builds) throw std::runtime_error(std::string(arg) + " applies only with --native");
            if (!first_policy_flag) first_policy_flag = arg;
            if (arg == "--static") {
                options.static_launcher = true;
            } else if (arg == "--target") {
                const std::string triple = next();
                if (options.native_target && *options.native_target != triple) {
                    throw std::runtime_error("conflicting --target values");
                }
                if (triple.empty() || triple == "." || triple == ".." ||
                    triple.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos) {
                    throw std::runtime_error("--target expects an ELF target triple");
                }
                options.native_target = triple;
            } else {
                const fs::path file = next();
                if (options.launcher && *options.launcher != file) throw std::runtime_error("conflicting --launcher values");
                options.launcher = file;
            }
        } else if (arg == "--no-source-map") {
            if (options.command != Command::Build) {
                throw std::runtime_error("--no-source-map applies only to build");
            }
            options.source_map = false;
        } else if (arg == "-o") {
            if (options.command != Command::Docs && options.command != Command::EmitIr &&
                options.command != Command::Build && options.command != Command::Inspect &&
                options.command != Command::Disassemble && options.command != Command::Assemble &&
                options.command != Command::Decompile) {
                throw std::runtime_error("-o applies only to docs, emit-ir, build, inspect, disassemble, assemble, and decompile");
            }
            options.output = fs::path(next());
        } else if (options.command == Command::Run && arg == "--profile") {
            options.limits.profile = true;
        } else if (arg == "--max-tasks" || arg == "--task-quantum" || arg == "--max-queued-messages" ||
                   arg == "--max-pending-requests") {
            if (!runs) throw std::runtime_error(std::string(arg) + " applies only to run and test");
            const auto value = parse_limit(arg, next());
            if (arg == "--max-tasks") options.limits.max_tasks = value;
            else if (arg == "--task-quantum") options.limits.task_quantum = value;
            else if (arg == "--max-queued-messages") options.limits.max_queued_messages = value;
            else options.limits.max_pending_requests = value;
        } else if (arg == "--task-dump" || arg.starts_with("--task-dump=")) {
            if (!runs) throw std::runtime_error("--task-dump applies only to run and test");
            const auto form = arg == "--task-dump" ? std::string_view("text") : arg.substr(12);
            if (form == "text") options.task_dump = neura::nvm::TaskDumpFormat::Text;
            else if (form == "json") options.task_dump = neura::nvm::TaskDumpFormat::Json;
            else throw std::runtime_error("--task-dump expects text or json");
        } else if (arg == "--trace-events") {
            if (options.command != Command::Run) throw std::runtime_error("--trace-events applies only to run");
            options.trace_events = fs::path(next());
        } else if (arg == "--trace-wall-time") {
            if (options.command != Command::Run) throw std::runtime_error("--trace-wall-time applies only to run");
            options.trace_wall_time = true;
        } else if (runs && arg == "--stats") {
            options.stats = true;
        } else if (compiles && arg == "--c-include") {
            options.c_import.include_dirs.push_back(fs::canonical(next()).string());
        } else if (compiles && arg == "--c-define") {
            options.c_import.defines.emplace_back(next());
        } else if (compiles && arg == "--libclang") {
            options.c_import.libclang = next();
        } else if (compiles && arg == "--cc") {
            options.cc = next();
        } else if (compiles && arg == "--c-link-arg") {
            options.link_arguments.emplace_back(next());
        } else if (options.command == Command::CBindings && arg == "--linked") {
            options.linked = true;
        } else if (options.command == Command::Run && arg == "--record") {
            options.record = fs::path(next());
        } else if (options.command == Command::Run && arg == "--replay") {
            options.replay = fs::path(next());
        } else if (arg == "--each") {
            if (options.command != Command::Run) throw std::runtime_error("--each applies only to run");
            if (options.each) throw std::runtime_error("--each given more than once");
            options.each = fs::path(next());
        } else if (arg.starts_with('-')) {
            throw std::runtime_error("unknown option: " + std::string(arg));
        } else if (options.command == Command::Run && !options.files.empty()) {
            throw std::runtime_error("program arguments follow --");
        } else if (single_input && !options.files.empty()) {
            throw std::runtime_error("expected one input file");
        } else {
            options.files.push_back(fs::canonical(argv[i]));
        }
    }
    if (first_policy_flag && !options.native_executable) {
        throw std::runtime_error(std::string(*first_policy_flag) + " applies only with --native");
    }
    if (options.files.empty()) {
        throw std::runtime_error(options.command == Command::Run ? "expected an image file"
                                                                : "expected at least one input file");
    }
    if (options.command == Command::Build && !options.output) {
        throw std::runtime_error(options.native_executable ? "build --native requires -o <file>" : "build requires -o <file.nvr>");
    }
    if (options.doxygen && options.files.size() != 1) {
        throw std::runtime_error("--doxygen requires one source file");
    }
    if (options.command == Command::Assemble && !options.output) {
        throw std::runtime_error("assemble requires -o <file.nvr>");
    }
    if (options.each) {
        if (options.record) throw std::runtime_error("--each cannot be combined with --record");
        if (options.replay) throw std::runtime_error("--each cannot be combined with --replay");
        if (options.limits.profile) throw std::runtime_error("--each cannot be combined with --profile");
    }
    if (options.record && options.replay) {
        throw std::runtime_error("--record and --replay cannot be combined");
    }
    if (runs) options.gpu.read_environment();
    options.gpu.validate();
    if (options.all_paths) {
        options.capabilities.fs_prefixes.clear();
    }
    return options;
}

bool is_nir_input(const fs::path& path) {
    return path.extension() == ".nir";
}

bool has_image_magic(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 4 && std::ranges::equal(bytes.first(4), std::string_view("NEUR"));
}

// Runs `arguments` and returns its exit status, with standard output and
// standard error in `output`.
int run_command(const std::vector<std::string>& arguments, std::string& output, const fs::path& log) {
    std::string command;
    for (const auto& argument : arguments) {
        command += command.empty() ? "" : " ";
        command += "'";
        for (const char c : argument) {
            if (c == '\'') command += "'\\''";
            else command += c;
        }
        command += "'";
    }
    command += " > '" + log.string() + "' 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream input(log);
    output.assign(std::istreambuf_iterator<char>{input}, {});
    return status;
}

// Links the static libraries of the program's C imports, and wrappers for
// their inline functions, into one shared object for the image's NATV
// section (ForeignInterop.md, C imports).
void link_native(neura::IrProgram& program, const Options& options) {
    if (program.native_units.empty()) return;
#ifdef _WIN32
    throw std::runtime_error("linking C imports is not supported on Windows in this release");
#else
    std::error_code ignored;
    const auto directory = fs::temp_directory_path() / ("neura-link-" + std::to_string(::getpid()));
    fs::remove_all(directory, ignored);
    fs::create_directories(directory);
    std::string shim = "/* Wrappers generated by neura for C imports. */\ntypedef int neura_native_unit;\n";
    std::vector<std::string> libraries;
    std::set<std::string> include_dirs;
    std::set<std::string> symbols;
    for (const auto& unit : program.native_units) {
        shim += unit.shim;
        include_dirs.insert(fs::path(unit.header).parent_path().string());
        for (const auto& library : unit.libraries)
            if (std::find(libraries.begin(), libraries.end(), library) == libraries.end()) libraries.push_back(library);
        symbols.insert(unit.symbols.begin(), unit.symbols.end());
    }
    {
        std::ofstream file(directory / "neura-native.c");
        file << shim;
    }
    std::string compiler = options.cc;
    if (compiler.empty()) {
        const char* environment = std::getenv("CC");
        compiler = environment != nullptr && *environment != '\0' ? environment : "cc";
    }
    std::vector<std::string> command{compiler, "-shared", "-fPIC", "-O2", "-o", (directory / "neura-native.so").string(),
                                     (directory / "neura-native.c").string(),
                                     "-ffile-prefix-map=" + directory.string() + "=.", "-Wl,--build-id=none"};
    for (const auto& dir : include_dirs) command.push_back("-I" + dir);
    for (const auto& dir : options.c_import.include_dirs) command.push_back("-I" + dir);
    for (const auto& define : options.c_import.defines) command.push_back("-D" + define);
    if (!libraries.empty()) {
        command.push_back("-Wl,--whole-archive");
        command.insert(command.end(), libraries.begin(), libraries.end());
        command.push_back("-Wl,--no-whole-archive");
    }
    command.insert(command.end(), options.link_arguments.begin(), options.link_arguments.end());
    std::string output;
    const auto status = run_command(command, output, directory / "link.log");
    if (status != 0) {
        fs::remove_all(directory, ignored);
        std::string message = "linking the native library of the C imports failed:\n" + output;
        if (output.find("-fPIC") != std::string::npos || output.find("relocation") != std::string::npos)
            message += "hint: a static library linked into an image must be compiled with -fPIC\n";
        throw std::runtime_error(message);
    }
    const auto object = read_file(directory / "neura-native.so");
    fs::remove_all(directory, ignored);
    neura::IrNativeLibrary library;
    library.triple = neura::nvm::native_target_triple();
    library.object = object;
    library.symbols.assign(symbols.begin(), symbols.end());
    program.native_library = std::move(library);
    program.native_units.clear();
#endif
}

neura::IrProgram load_source_program(const Options& options) {
    auto roots = options.roots;
    for (const auto& file : options.files) {
        roots.push_back(file.parent_path());
    }
    neura::set_c_import_options(options.c_import);
    auto program = neura::lower_sources(options.files, roots, options.library);
    if (options.command != Command::Check) link_native(program, options);
    return program;
}

int c_bindings(const Options& options) {
    neura::set_c_import_options(options.c_import);
    std::cout << neura::c_import_source(options.files.front(), options.roots, options.linked);
    return 0;
}

// Loads the program from source files or from one .nir file.
neura::IrProgram load_program(const Options& options) {
    const bool nir = std::any_of(options.files.begin(), options.files.end(), is_nir_input);
    if (!nir) return load_source_program(options);
    if (options.files.size() != 1) {
        throw std::runtime_error("a .nir input must be the only input file");
    }
    if (!options.roots.empty()) {
        throw std::runtime_error("-I applies only to source inputs");
    }
    const auto bytes = read_file(options.files.front());
    try {
        return neura::read_nir(bytes, options.files.front().string());
    } catch (const neura::NirError& error) {
        throw std::runtime_error(error.what());
    }
}

int emit_ir(const Options& options) {
    auto program = load_program(options);
    if (options.output) {
        // .nir holds NeuraIR as lowered, before optimization.
        neura::require_valid_ir(program, "before writing .nir");
        write_file(options.output.value(), neura::write_nir(program));
        return 0;
    }
    // Unoptimized emission shows lowering's output and skips verification, so
    // that IR produced by a faulty lowering can still be inspected. The
    // default listing is what conversion receives: specialized and optimized.
    if (options.optimize) {
        neura::require_valid_ir(program, "before specialization");
        neura::materialize_reference_storage(program);
        neura::specialize(program);
        neura::optimize(program);
    }
    neura::print_ir(std::cout, program);
    return 0;
}

std::vector<std::uint8_t> build_image_bytes(const Options& options) {
    const bool nvr_input = std::ranges::any_of(options.files, [](const fs::path& file) { return file.extension() == ".nvr"; });
    if (nvr_input) {
        if (!options.native_executable) {
            throw std::runtime_error("build takes sources or one .nir file; packaging a .nvr needs --native");
        }
        if (options.files.size() != 1) throw std::runtime_error("a .nvr input must be the only input file");
        if (!options.roots.empty()) throw std::runtime_error("-I applies only to source inputs");
        if (!options.optimize) throw std::runtime_error("--no-optimize cannot be applied to an image that is already built");
        if (!options.source_map) throw std::runtime_error("--no-source-map cannot be applied to an image that is already built");
        if (fs::file_size(options.files.front()) >= (std::uint64_t{1} << 32)) {
            throw std::runtime_error("native executable: image must be smaller than 4 GiB");
        }
        return read_file(options.files.front());
    }
    neura::backend::BuildOptions build_options;
    build_options.optimize = options.optimize;
    build_options.source_map = options.source_map;
    const auto conversion = neura::backend::build(load_program(options), build_options);
    return neura::nvr::encode(conversion.image);
}

void note_denied_imports(const neura::nvr::Image& image, const neura::nvm::Capabilities& capabilities) {
    struct Group {
        std::string_view flag;
        std::string_view prefix;
        bool granted;
    };
    const Group groups[] = {
        {"fs", "neura.fs.", capabilities.fs}, {"net", "neura.net.", capabilities.net},
        {"env", "neura.env.", capabilities.env}, {"clock", "neura.clock.", capabilities.clock},
        {"entropy", "neura.entropy.", capabilities.entropy}, {"native", "neura.ffi.", capabilities.native},
    };
    for (const auto& group : groups) {
        if (!group.granted && std::ranges::any_of(image.imports, [&](const auto& import) {
            return import.name.starts_with(group.prefix);
        })) {
            std::cerr << "neura: note: the image uses " << group.flag << " services; without --allow-"
                      << group.flag << " they fail with CapabilityDenied\n";
        }
    }
}

void package_native(const Options& options, std::span<const std::uint8_t> bytes) {
    constexpr const char* static_interop_error =
        "--static: the image uses native interop, which the static launcher does not provide";
    if (options.static_launcher && options.capabilities.native) throw std::runtime_error(static_interop_error);
    const auto triple = options.native_target.value_or(neura::nvm::native_target_triple());
    auto prefix = options.executable.parent_path();
    if (prefix.filename() == "bin") prefix = prefix.parent_path();
    const auto directory = prefix / "lib/neura/launchers" / triple;
    const auto file = options.launcher.value_or(directory / (options.static_launcher ? "neura-launcher-static" : "neura-launcher"));
    if (!fs::is_regular_file(file)) {
        if (options.launcher) throw std::runtime_error("--native: " + file.string() + " is not a Neura launcher");
        throw std::runtime_error("--native: no launcher for " + triple + " in " + directory.string() +
                                 "; pass --launcher or install one");
    }
    const auto launcher = read_file(file);
    const auto descriptor = neura::launcher::read_descriptor(launcher);
    if (!descriptor || descriptor->nvr_major != neura::nvr::major_version ||
        (!options.launcher && (descriptor->triple != triple || descriptor->flavor != unsigned{options.static_launcher}))) {
        throw std::runtime_error("--native: " + file.string() + " is not a Neura launcher");
    }
    if (options.native_target && descriptor->triple != triple) {
        throw std::runtime_error("--target: launcher is for " + descriptor->triple + ", expected " + triple);
    }
    if (options.static_launcher && descriptor->flavor == 0) {
        throw std::runtime_error("--static: " + file.string() + " is a default launcher");
    }
    if (bytes.size() >= 8 && has_image_magic(bytes)) {
        const unsigned major = bytes[4] | (unsigned{bytes[5]} << 8);
        const unsigned minor = bytes[6] | (unsigned{bytes[7]} << 8);
        if (major == 1 && minor > descriptor->nvr_minor) {
            throw std::runtime_error("--native: the image is NVR 1." + std::to_string(minor) +
                                     "; this launcher runs up to 1." + std::to_string(descriptor->nvr_minor));
        }
    }
    const auto image = neura::nvr::load(bytes);
    if (image.native_library && image.native_library->triple != descriptor->triple) {
        throw std::runtime_error("--native: the image's native library is for " + image.native_library->triple +
                                 "; the launcher is for " + descriptor->triple);
    }
    if (descriptor->flavor == 1 && (options.capabilities.native || image.native_library ||
        std::ranges::any_of(image.imports, [](const auto& import) { return import.name.starts_with("neura.ffi."); }))) {
        throw std::runtime_error(static_interop_error);
    }
    const neura::launcher::Policy policy{options.capabilities, options.limits, options.sealed};
    neura::launcher::write_executable(*options.output, launcher, bytes, policy);
    note_denied_imports(image, policy.capabilities);
}

int build(const Options& options) {
    const auto bytes = build_image_bytes(options);
    if (options.native_executable) {
        package_native(options, bytes);
        return 0;
    }
    // Load the encoded image before publishing it, so that a writer or
    // converter fault cannot produce an executable the runtime rejects.
    neura::nvr::load(bytes);
    write_file(options.output.value(), bytes);
    return 0;
}

neura::nvr::Image load_image(const fs::path& path, std::size_t* byte_count = nullptr) {
    try {
        auto bytes = read_file(path);
        if (!has_image_magic(bytes) && neura::launcher::is_container(bytes)) {
            bytes = neura::launcher::read_container(bytes).image;
        }
        auto image = neura::nvr::load(bytes);
        if (byte_count) *byte_count = bytes.size();
        return image;
    } catch (const neura::nvr::LoadError& error) {
        throw std::runtime_error(path.string() + ": " + std::string(neura::nvr::describe(error.kind)) + ": " +
                                 error.what());
    }
}

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
    if (caps.env) std::cout << ", environment";
    if (caps.clock) std::cout << ", clock";
    if (caps.entropy) std::cout << ", entropy";
    if (caps.native) std::cout << ", native";
    std::cout << "\nlimits: steps " << policy.limits.max_steps << ", frames " << policy.limits.max_frames
              << ", heap cells " << policy.limits.max_heap_cells
              << "\nrun options: " << (policy.sealed ? "sealed" : "accepted")
              << "\nimage: " << executable.image.size() << " bytes, NVR 1." << image.minor_version
              << ", " << image.graphs.size() << " graphs, source map " << (image.source_map ? "present" : "absent")
              << ", native library " << (image.native_library ? "present" : "absent") << '\n';
    return 0;
}

void write_text(const Options& options, const std::string& text) {
    if (options.output) {
        write_file(*options.output, std::vector<std::uint8_t>(text.begin(), text.end()));
    } else {
        std::cout << text;
    }
}

int docs(const Options& options) {
    std::ostringstream index;
    if (options.doxygen) neura::write_doxygen_source(index, options.files.front());
    else neura::write_source_docs(index, options.files);
    write_text(options, std::move(index).str());
    return 0;
}

int disassemble(const Options& options) {
    write_text(options, neura::decompiler::disassemble(load_image(options.files.front())));
    return 0;
}

int decompile(const Options& options) {
    neura::decompiler::DecompileOptions decompile_options;
    decompile_options.provenance = options.provenance;
    write_text(options, neura::decompiler::decompile(load_image(options.files.front()), decompile_options).text);
    return 0;
}

int assemble(const Options& options) {
    const auto bytes = read_file(options.files.front());
    neura::nvr::Image image;
    try {
        image = neura::decompiler::assemble(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    } catch (const neura::decompiler::AssemblyError& error) {
        throw std::runtime_error(options.files.front().string() + ": " + error.what());
    }
    std::vector<std::uint8_t> encoded;
    try {
        encoded = neura::nvr::encode(image);
        // The assembled image passes the same validation as any other.
        neura::nvr::load(encoded);
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(options.files.front().string() + ": cannot encode the image: " + error.what());
    } catch (const neura::nvr::LoadError& error) {
        throw std::runtime_error(options.files.front().string() + ": the assembled image is " +
                                 std::string(neura::nvr::describe(error.kind)) + ": " + error.what());
    }
    write_file(*options.output, encoded);
    return 0;
}

// The exact sum of up to 1,048,576 uint64 counters fits in these two decimal parts.
struct MemberTotal {
    void add(std::uint64_t value) {
        low += value % base;
        high += value / base + low / base;
        low %= base;
    }

    std::string text() const {
        const auto tail = std::to_string(low);
        if (high == 0) return tail;
        return std::to_string(high) + std::string(18 - tail.size(), '0') + tail;
    }

private:
    static constexpr std::uint64_t base = 1'000'000'000'000'000'000;
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

int run_each(const Options& options, const neura::nvr::Image& image, Host& host,
             std::span<neura::cli::ArgumentList> lists, std::size_t image_bytes, neura::nvm::GraphDevice* device) {
    const auto& path = options.files.front();
    const auto bindings = neura::nvm::bind(image, host.registry);
    const auto report_prefix = path.string() + ": " + options.each->string() + ':';
    MemberTotal steps;
    MemberTotal requests;
    std::uint64_t peak_heap_cells = 0;
    int first_status = 0;
    const auto finished = [&](std::size_t member, const neura::nvm::RunResult& result) {
        host.services.end_run();
        if (options.stats) {
            steps.add(result.steps);
            requests.add(result.host_requests);
            peak_heap_cells = std::max(peak_heap_cells, result.peak_heap_cells);
        }
        const auto status = result.completed ? result.status : neura::nvm::exit_status(result.trap);
        if (!result.completed) {
            std::cerr << report_prefix << lists[member].line << ": trap: "
                      << neura::nvm::describe(result.trap, image) << '\n';
        } else if (status != 0) {
            std::cerr << report_prefix << lists[member].line << ": exit status " << status << '\n';
        }
        if (first_status == 0) first_status = status;
    };

    const auto append_list = [](std::vector<std::string>& arguments, std::vector<std::string>& listed) {
        arguments.insert(arguments.end(), std::make_move_iterator(listed.begin()), std::make_move_iterator(listed.end()));
    };
    neura::runner::TaskDumps dumps(options.task_dump);
    auto limits = options.limits;
    limits.task_dump = dumps.request();
    if (device) {
        neura::nvm::Fleet fleet(image, {limits, device});
        for (auto& list : lists) {
            if (options.arguments.empty()) {
                fleet.add(image.entry, std::move(list.arguments));
            } else {
                auto arguments = options.arguments;
                append_list(arguments, list.arguments);
                fleet.add(image.entry, std::move(arguments));
            }
        }
        neura::nvm::run_fleet(fleet, bindings, finished);
    } else {
        auto arguments = options.arguments;
        for (std::size_t member = 0; member < lists.size(); ++member) {
            arguments.resize(options.arguments.size());
            append_list(arguments, lists[member].arguments);
            finished(member, neura::nvm::run(image, bindings, arguments, limits));
        }
    }
    if (options.stats) {
        std::cerr << "stats: members " << lists.size() << ", steps " << steps.text()
                  << ", peak heap cells " << peak_heap_cells << ", host requests " << requests.text()
                  << ", " << image_costs(image) << ", image bytes " << image_bytes << '\n';
    }
    return first_status;
}

int run(const Options& options) {
    const auto& path = options.files.front();
    std::size_t image_bytes = 0;
    const auto image = load_image(path, &image_bytes);
    auto lists = options.each ? neura::cli::read_argument_lists(*options.each) : std::vector<neura::cli::ArgumentList>{};
    const auto max_jobs = options.each ? std::min<std::size_t>(lists.size(), neura::nvm::FleetOptions{}.window) : 1;
    neura::cli::GpuRun gpu(options.gpu, image, max_jobs);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Host host(options.capabilities);
    provide_native_library(image, host.services);
    if (options.each) return gpu.finish(run_each(options, image, host, lists, image_bytes, gpu.device()), options.stats);
    neura::runner::RunOptions run_options{options.limits, options.stats, options.record, options.replay,
                                          options.task_dump, options.trace_events, options.trace_wall_time};
    run_options.limits.graph_device = gpu.device();
    const auto status = neura::runner::run(image, host, options.arguments, run_options, path.string(), image_bytes);
    return gpu.finish(status, options.stats);
}

// The source location of a block, when the image has a source map entry.
std::optional<neura::nvr::SourceLocation> block_location(const neura::nvr::Image& image, std::uint32_t function,
                                                         std::uint32_t block) {
    if (!image.source_map) return std::nullopt;
    for (const auto& entry : *image.source_map) {
        if (entry.function == function && entry.block == block) return entry;
    }
    return std::nullopt;
}

// A message trap (reason 3) reports its message, such as a failed Std.Test
// assertion's text, at the innermost frame that belongs to one of the listed
// test files: the assertion's call site rather than the library line that
// trapped. Other traps keep their runtime diagnostic.
std::optional<std::string> test_failure_message(const neura::nvm::Trap& trap, const neura::nvr::Image& image,
                                                const std::set<std::string>& test_labels) {
    if (trap.kind != neura::nvm::TrapKind::Program ||
        trap.reason != static_cast<std::uint32_t>(neura::nvr::TrapReason::ProgramFailure) ||
        trap.function == neura::nvr::absent) {
        return std::nullopt;
    }
    auto text = trap.detail;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> frames{{trap.function, trap.block}};
    frames.insert(frames.end(), trap.callers.begin(), trap.callers.end());
    for (const auto& [function, block] : frames) {
        const auto location = block_location(image, function, block);
        if (location && test_labels.contains(location->path)) {
            return text + " at " + location->path + ":" + std::to_string(location->line) + ":" +
                   std::to_string(location->column);
        }
    }
    if (const auto location = block_location(image, trap.function, trap.block)) {
        text += " at " + location->path + ":" + std::to_string(location->line) + ":" +
                std::to_string(location->column);
    }
    return text;
}

// Testing.md: every exported, zero-argument, void function whose name starts
// with test_ in a module of the listed files is a test. Each test runs in a
// fresh execution of one image, so no state carries over between tests.
int run_tests(const Options& options) {
    auto program = load_program(options);
    neura::require_valid_ir(program, "before specialization");
    std::vector<neura::SymbolId> tests;
    std::vector<std::string> names;
    for (neura::SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != neura::SymbolKind::Function || !item.exported || !item.has_body ||
            !item.name.starts_with("test_") || !item.parameters.empty() || !item.generics.empty() ||
            item.type.kind != neura::TypeKind::Void || item.owner == neura::no_symbol ||
            program.symbols[item.owner].kind != neura::SymbolKind::Module ||
            neura::is_static_scope(program.symbols[item.owner]) ||
            !program.symbols[item.owner].generics.empty()) {
            continue;
        }
        const auto file = fs::path(item.span.file);
        if (std::none_of(options.files.begin(), options.files.end(),
                         [&](const fs::path& listed) { return fs::equivalent(listed, file); })) {
            continue;
        }
        tests.push_back(id);
        names.push_back(program.symbols[item.owner].name + "." + item.name);
    }
    if (tests.empty()) {
        throw std::runtime_error("no tests found: a test is an exported, zero-argument void function named "
                                 "test_* in a module of the listed files");
    }
    neura::materialize_reference_storage(program);
    neura::specialize(program);
    if (options.optimize) {
        neura::optimize(program);
    } else {
        neura::require_valid_ir(program, "before conversion");
    }
    neura::backend::ConversionOptions conversion_options;
    conversion_options.entries = tests;
    auto conversion = neura::backend::convert(program, conversion_options);
    auto image = neura::nvr::load(neura::nvr::encode(conversion.image));
    const auto max_jobs = std::min<std::size_t>(tests.size(), neura::nvm::FleetOptions{}.window);
    neura::cli::GpuRun gpu(options.gpu, image, max_jobs);

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::set<std::string> test_labels;
    for (const auto& file : options.files) {
        test_labels.insert(neura::backend::source_label(program, file.generic_string()));
    }
    Host host(options.capabilities);
    provide_native_library(image, host.services);
    const auto bindings = neura::nvm::bind(image, host.registry);
    std::size_t failed = 0;
    const auto finished = [&](std::size_t i, const neura::nvm::RunResult& result) {
        // Each test is its own run: files and sockets it left open close now.
        host.services.end_run();
        // A test that ends the process with a nonzero status through
        // Std.Program.Process.exit fails.
        if (result.completed && result.status != 0) {
            ++failed;
            std::cout << "FAIL " << names[i] << ": the test exited with status " << result.status << '\n';
            return;
        }
        if (result.completed) {
            std::cout << "PASS " << names[i];
            if (options.stats) {
                std::cout << " (steps " << result.steps << ", peak heap cells " << result.peak_heap_cells << ")";
            }
            std::cout << '\n';
            return;
        }
        ++failed;
        std::cout << "FAIL " << names[i] << ": ";
        if (const auto message = test_failure_message(result.trap, image, test_labels)) {
            std::cout << *message << '\n';
        } else {
            // Function and block numbers are internal to the test image; the
            // source location identifies the fault.
            std::cout << neura::nvm::describe(result.trap, image, false) << '\n';
        }
    };
    neura::runner::TaskDumps dumps(options.task_dump);
    auto limits = options.limits;
    limits.task_dump = dumps.request();
    if (auto* device = gpu.device()) {
        neura::nvm::Fleet fleet(image, {limits, device});
        for (std::size_t i = 0; i < tests.size(); ++i) fleet.add(static_cast<std::uint32_t>(i), {});
        neura::nvm::run_fleet(fleet, bindings, finished);
    } else {
        for (std::size_t i = 0; i < tests.size(); ++i) {
            // The start functions share the entry signature, so any of them can
            // be the entry of a validated image.
            image.entry = static_cast<std::uint32_t>(i);
            finished(i, neura::nvm::run(image, bindings, {}, limits));
        }
    }
    std::cout << tests.size() << " test(s), " << tests.size() - failed << " passed, " << failed << " failed\n";
    std::cout.flush();
    return gpu.finish(failed == 0 ? 0 : 1, options.stats);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }

    try {
        const auto options = read_options(argc, argv);
        switch (options.command) {
        case Command::Check: {
            auto program = load_source_program(options);
            if (neura::uses_tasks(program)) {
                // The isolation rules of a spawn need concrete types, which
                // exist after specialization (Concurrency.md section 5).
                neura::require_valid_ir(program, "before specialization");
                neura::materialize_reference_storage(program);
                neura::specialize(program);
            }
            return 0;
        }
        case Command::Docs:
            return docs(options);
        case Command::EmitIr:
            return emit_ir(options);
        case Command::Build:
            return build(options);
        case Command::Inspect:
            return inspect(options);
        case Command::Run:
            return run(options);
        case Command::Test:
            return run_tests(options);
        case Command::Disassemble:
            return disassemble(options);
        case Command::Assemble:
            return assemble(options);
        case Command::Decompile:
            return decompile(options);
        case Command::CBindings:
            return c_bindings(options);
        }
    } catch (const neura::Diagnostic& error) {
        const auto& span = error.span;
        std::cerr << span.file << ':' << span.line << ':' << span.column << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}
