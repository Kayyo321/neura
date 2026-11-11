#include "argument_lists.hpp"
#include "backend/converter.hpp"
#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"
#include "gpu_run.hpp"
#include "neura/version.hpp"
#include "ir-optimization/optimizer.hpp"
#include "launcher/container.hpp"
#include "lifecycle.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/task_analysis.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/fleet.hpp"
#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"
#include "nvm/schedule.hpp"
#include "nvr/format.hpp"
#include "runner/runner.hpp"
#include "source_docs.hpp"
#include "source_loader.hpp"

#include <algorithm>
#include <charconv>
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

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
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
    out << "usage: neura check [-I source-root] [--task-safety] <file.nra>...\n"
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
           "       neura --version | version\n"
           "       neura --help | help\n"
           "       neura doctor\n"
           "       neura update [--check] [--source <https-url-or-directory>] [--require-attestation]\n"
           "       neura uninstall [--dry-run] [--yes]\n"
           "\n"
           "doctor, update, and uninstall manage an installed neura and are described in docs/Install.md.\n"
           "update and uninstall exit with status 3 for an install that neura did not make, and update --check\n"
           "exits with status 4 when a newer version is available.\n"
           "\n"
           "capabilities: run and test, and the policy that build --native records, grant every host\n"
           "service by default: files, network, environment, clock, entropy, and native code. A denied\n"
           "service fails with error 14 (CapabilityDenied). Options apply in command-line order.\n"
           "  --deny-fs                files and directories\n"
           "  --deny-net               TCP connections and listeners\n"
           "  --deny-env               environment variables\n"
           "  --deny-clock             wall and monotonic clocks and sleeping\n"
           "  --deny-entropy           operating-system entropy\n"
           "  --deny-native            native libraries, memory, and calls\n"
           "  --deny-all               every group above\n"
           "  --allow-fs[=prefix,...]  grant files again: every path, or only below the prefixes\n"
           "  --allow-net, --allow-env, --allow-clock, --allow-entropy, --allow-native\n"
           "                           grant that group again after a --deny option (native code is unsafe)\n"
           "  Run an image you do not trust with --deny-all, plus the --allow options it needs.\n"
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
           "  --parallel-workers N     threads that evaluate the items of a parallel map (default 1)\n"
           "  --task-dump[=text|json]  print every task when one traps; SIGQUIT prints a dump at any time\n"
           "  --virtual-time           timers, deadlines, and the clock follow a clock that moves only when\n"
           "                           every task is parked, so a timeout takes no real time\n"
           "  --schedules N            (test) run each test under up to N legal task schedules\n"
           "  --schedule-seed S        (test) the seed of the explored schedules (default 0)\n"
           "  --save-schedule FILE     (test) write the first failing schedule to FILE\n"
           "  --schedule FILE          (test) run the test of a saved schedule under exactly that schedule\n"
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

enum class Command {
    Check, Docs, EmitIr, Build, Inspect, Run, Test, Disassemble, Assemble, Decompile, CBindings,
    Version, Help, Doctor, Update, Uninstall
};

constexpr const char* commands =
    "expected check, docs, emit-ir, build, inspect, run, test, disassemble, assemble, decompile, c-bindings, "
    "doctor, update, uninstall, version, or help";

neura::nvm::Capabilities granted_capabilities() {
    neura::nvm::Capabilities capabilities;
    capabilities.fs = capabilities.net = capabilities.env = capabilities.clock = capabilities.entropy = true;
    capabilities.native = true;
    return capabilities;
}

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
    // check --task-safety: report every function instead of only checking.
    bool task_safety = false;
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
    neura::cli::GpuOptions gpu;
    // C imports: header search and definitions, the libclang library, and
    // the C compiler and extra arguments that link the native library.
    neura::CImportOptions c_import;
    std::string cc;
    std::vector<std::string> link_arguments;
    // c-bindings: generate as if the import named libraries.
    bool linked = false;
    // update and uninstall.
    neura::lifecycle::UpdateOptions update;
    neura::lifecycle::UninstallOptions uninstall;
};

fs::path executable_path(const fs::path& launch) {
    std::error_code error;
#ifdef __linux__
    if (const auto actual = fs::canonical("/proc/self/exe", error); !actual.empty()) return actual;
#endif
#ifdef __APPLE__
    {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            buffer.resize(std::strlen(buffer.c_str()));
            if (const auto actual = fs::canonical(buffer, error); !actual.empty()) return actual;
        }
    }
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
        // Every path, replacing any earlier prefix list.
        options.capabilities.fs = true;
        options.capabilities.fs_prefixes.clear();
    } else if (arg.starts_with("--allow-fs=")) {
        auto list = arg.substr(std::string_view("--allow-fs=").size());
        std::vector<std::string> prefixes;
        for (;;) {
            const auto comma = list.find(',');
            const auto prefix = list.substr(0, comma);
            if (prefix.empty()) {
                throw std::runtime_error("--allow-fs= expects a comma-separated list of path prefixes");
            }
            prefixes.emplace_back(prefix);
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
        // A list narrows "every path" or "denied" to the listed prefixes and
        // adds to a list that is already in effect.
        auto& capabilities = options.capabilities;
        if (!capabilities.fs) capabilities.fs_prefixes.clear();
        capabilities.fs = true;
        capabilities.fs_prefixes.insert(capabilities.fs_prefixes.end(), prefixes.begin(), prefixes.end());
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
        options.allow_native_requested = true;
    } else if (arg == "--deny-fs") {
        options.capabilities.fs = false;
        options.capabilities.fs_prefixes.clear();
    } else if (arg == "--deny-net") {
        options.capabilities.net = false;
    } else if (arg == "--deny-env") {
        options.capabilities.env = false;
    } else if (arg == "--deny-clock") {
        options.capabilities.clock = false;
    } else if (arg == "--deny-entropy") {
        options.capabilities.entropy = false;
    } else if (arg == "--deny-native") {
        options.capabilities.native = false;
        options.allow_native_requested = false;
    } else if (arg == "--deny-all") {
        options.capabilities = {};
        options.allow_native_requested = false;
    } else {
        return false;
    }
    return true;
}

// The commands that manage the installed tool take no files.
void read_lifecycle_options(Options& options, int argc, char** argv) {
    const std::string name = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const auto value = [&]() -> std::string {
            if (++i == argc) throw std::runtime_error("expected a value after " + std::string(arg));
            return argv[i];
        };
        if (options.command == Command::Update && arg == "--check") {
            options.update.check = true;
        } else if (options.command == Command::Update && arg == "--source") {
            options.update.source = value();
        } else if (options.command == Command::Update && arg == "--require-attestation") {
            options.update.require_attestation = true;
        } else if (options.command == Command::Uninstall && arg == "--dry-run") {
            options.uninstall.dry_run = true;
        } else if (options.command == Command::Uninstall && arg == "--yes") {
            options.uninstall.yes = true;
        } else if (arg.starts_with('-')) {
            throw std::runtime_error("unknown option for " + name + ": " + std::string(arg));
        } else {
            throw std::runtime_error(name + " takes no files");
        }
    }
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
    } else if (command == "--version" || command == "version") {
        options.command = Command::Version;
    } else if (command == "--help" || command == "help") {
        options.command = Command::Help;
    } else if (command == "doctor") {
        options.command = Command::Doctor;
    } else if (command == "update") {
        options.command = Command::Update;
    } else if (command == "uninstall") {
        options.command = Command::Uninstall;
    } else {
        throw std::runtime_error(commands);
    }
    if (options.command >= Command::Version) {
        read_lifecycle_options(options, argc, argv);
        return options;
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
                   arg == "--max-pending-requests" || arg == "--parallel-workers") {
            // build --native records these in the executable's policy.
            if (!runs && !builds) throw std::runtime_error(std::string(arg) + " applies only to run and test");
            if (builds && !first_policy_flag) first_policy_flag = arg;
            const auto value = parse_limit(arg, next());
            if (arg == "--max-tasks") options.limits.max_tasks = value;
            else if (arg == "--task-quantum") options.limits.task_quantum = value;
            else if (arg == "--max-queued-messages") options.limits.max_queued_messages = value;
            else if (arg == "--max-pending-requests") options.limits.max_pending_requests = value;
            else if (value > 1024) throw std::runtime_error("--parallel-workers is at most 1024");
            else options.limits.parallel_workers = static_cast<std::uint32_t>(value);
        } else if (arg == "--task-dump" || arg.starts_with("--task-dump=")) {
            if (!runs) throw std::runtime_error("--task-dump applies only to run and test");
            const auto form = arg == "--task-dump" ? std::string_view("text") : arg.substr(12);
            if (form == "text") options.task_dump = neura::nvm::TaskDumpFormat::Text;
            else if (form == "json") options.task_dump = neura::nvm::TaskDumpFormat::Json;
            else throw std::runtime_error("--task-dump expects text or json");
        } else if (arg == "--task-safety") {
            if (options.command != Command::Check) throw std::runtime_error("--task-safety applies only to check");
            options.task_safety = true;
        } else if (arg == "--trace-events") {
            if (options.command != Command::Run) throw std::runtime_error("--trace-events applies only to run");
            options.trace_events = fs::path(next());
        } else if (arg == "--trace-wall-time") {
            if (options.command != Command::Run) throw std::runtime_error("--trace-wall-time applies only to run");
            options.trace_wall_time = true;
        } else if (arg == "--virtual-time") {
            if (!runs) throw std::runtime_error("--virtual-time applies only to run and test");
            options.virtual_time = true;
        } else if (arg == "--schedules" || arg == "--schedule-seed" || arg == "--save-schedule" || arg == "--schedule") {
            if (options.command != Command::Test) throw std::runtime_error(std::string(arg) + " applies only to test");
            if (arg == "--schedules") {
                options.schedules = parse_limit(arg, next());
                options.exploration_options = true;
            } else if (arg == "--schedule-seed") {
                const std::string_view digits(next());
                const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(),
                                                          options.schedule_seed);
                if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()) {
                    throw std::runtime_error("expected an unsigned integer after --schedule-seed");
                }
                options.exploration_options = true;
            } else if (arg == "--save-schedule") {
                options.save_schedule = fs::path(next());
                options.exploration_options = true;
            } else {
                if (options.schedule) throw std::runtime_error("--schedule given more than once");
                options.schedule = fs::path(next());
            }
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
        if (options.virtual_time) throw std::runtime_error("--each cannot be combined with --virtual-time");
    }
    if (options.virtual_time && options.replay) {
        throw std::runtime_error("--virtual-time cannot be combined with --replay: a replay takes its clock from the trace");
    }
    if (options.schedule && options.exploration_options) {
        throw std::runtime_error("--schedule cannot be combined with --schedules, --schedule-seed, or --save-schedule");
    }
    if (options.record && options.replay) {
        throw std::runtime_error("--record and --replay cannot be combined");
    }
    if (runs) options.gpu.read_environment();
    options.gpu.validate();
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
            std::cerr << "neura: note: the image uses " << group.flag << " services, which are denied; they fail with CapabilityDenied\n";
        }
    }
}

void package_native(const Options& options, std::span<const std::uint8_t> bytes) {
    constexpr const char* static_interop_error =
        "--static: the image uses native interop, which the static launcher does not provide";
    if (options.static_launcher && options.allow_native_requested) throw std::runtime_error(static_interop_error);
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
    // The static launcher has no native interop, so the default policy it
    // records grants every group except native code.
    auto capabilities = options.capabilities;
    if (descriptor->flavor == 1) capabilities.native = false;
    if (descriptor->flavor == 1 && (options.allow_native_requested || image.native_library ||
        std::ranges::any_of(image.imports, [](const auto& import) { return import.name.starts_with("neura.ffi."); }))) {
        throw std::runtime_error(static_interop_error);
    }
    const neura::launcher::Policy policy{capabilities, options.limits, options.sealed};
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
    run_options.virtual_time = options.virtual_time;
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
    // `note` follows the test's name: which schedule failed, or how many ran.
    const auto finished = [&](std::size_t i, const neura::nvm::RunResult& result, const std::string& note = {}) {
        // Each test is its own run: files and sockets it left open close now.
        host.services.end_run();
        // A test that ends the process with a nonzero status through
        // Std.Program.Process.exit fails.
        if (result.completed && result.status != 0) {
            ++failed;
            std::cout << "FAIL " << names[i] << note << ": the test exited with status " << result.status << '\n';
            return;
        }
        if (result.completed) {
            std::cout << "PASS " << names[i] << note;
            if (options.stats) {
                std::cout << " (steps " << result.steps << ", peak heap cells " << result.peak_heap_cells << ")";
            }
            std::cout << '\n';
            return;
        }
        ++failed;
        std::cout << "FAIL " << names[i] << note << ": ";
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
    // One test, alone, under a chooser (null: the reference scheduler) and
    // its own virtual clock when virtual time is on.
    const auto run_test = [&](std::size_t i, neura::nvm::ScheduleChooser* chooser, std::uint64_t quantum,
                              bool virtual_time) {
        neura::nvm::VirtualTaskClock virtual_clock;
        neura::nvm::TaskClock* clock = virtual_time ? &virtual_clock : nullptr;
        host.services.set_clock(clock);
        auto test_limits = limits;
        test_limits.chooser = chooser;
        test_limits.task_quantum = quantum;
        // The start functions share the entry signature, so any of them can
        // be the entry of a validated image.
        image.entry = static_cast<std::uint32_t>(i);
        auto result = neura::nvm::run(image, bindings, {}, test_limits, clock);
        host.services.set_clock(nullptr);
        return result;
    };
    const auto passed = [](const neura::nvm::RunResult& result) { return result.completed && result.status == 0; };
    std::size_t ran = tests.size();
    if (options.schedule) {
        // Reproduction: the one test of the saved schedule, under exactly
        // its choices, quantum, and clock.
        const auto saved = neura::nvm::read_schedule(*options.schedule);
        const auto found = std::ranges::find(names, saved.test);
        if (found == names.end()) {
            throw std::runtime_error(options.schedule->string() + ": the schedule is for test " + saved.test +
                                     ", which these files do not define");
        }
        const auto i = static_cast<std::size_t>(found - names.begin());
        neura::nvm::RecordedChooser chooser(saved.choices);
        const auto result = run_test(i, &chooser, saved.quantum, saved.virtual_time || options.virtual_time);
        if (chooser.diverged()) {
            host.services.end_run();
            ++failed;
            std::cout << "FAIL " << names[i] << " [saved schedule]: the schedule does not fit this program: at decision "
                      << chooser.diverged_tick() << " fewer tasks could run than its choice needs\n";
        } else {
            finished(i, result, " [saved schedule]");
        }
        ran = 1;
    } else if (auto* device = options.schedules > 1 || options.virtual_time || options.save_schedule
                                  ? nullptr : gpu.device()) {
        neura::nvm::Fleet fleet(image, {limits, device});
        for (std::size_t i = 0; i < tests.size(); ++i) fleet.add(static_cast<std::uint32_t>(i), {});
        neura::nvm::run_fleet(fleet, bindings,
                              [&](std::size_t i, const neura::nvm::RunResult& result) { finished(i, result); });
    } else {
        // Schedule 1 is the reference scheduler's. Each later one varies
        // which runnable task goes next, from the seed. Exploration is
        // bounded: it says nothing about schedules it did not run.
        std::uint64_t explored = 0;
        bool saved = false;
        for (std::size_t i = 0; i < tests.size(); ++i) {
            auto result = run_test(i, nullptr, limits.task_quantum, options.virtual_time);
            std::uint64_t count = 1;
            std::string note;
            neura::nvm::SavedSchedule failing{names[i], options.schedule_seed, limits.task_quantum,
                                              options.virtual_time, {}};
            for (std::uint64_t k = 1; passed(result) && k < options.schedules; ++k) {
                host.services.end_run();
                neura::nvm::ExploringChooser chooser(options.schedule_seed + k);
                result = run_test(i, &chooser, limits.task_quantum, options.virtual_time);
                // No decision offered a choice: the test has one schedule.
                if (chooser.offered() == 0) break;
                ++count;
                failing.seed = options.schedule_seed + k;
                failing.choices = chooser.choices();
            }
            explored += count;
            if (options.schedules > 1) {
                note = passed(result) ? " [" + std::to_string(count) + (count == 1 ? " schedule]" : " schedules]")
                                      : " [schedule " + std::to_string(count) + " of " +
                                            std::to_string(options.schedules) + ", seed " +
                                            std::to_string(failing.seed) + "]";
            }
            if (!passed(result) && options.save_schedule && !saved) {
                neura::nvm::write_schedule(*options.save_schedule, failing);
                saved = true;
                std::cerr << "neura: note: the failing schedule of " << names[i] << " was written to "
                          << options.save_schedule->string() << '\n';
            }
            finished(i, result, note);
        }
        if (options.schedules > 1) {
            std::cout << "explored " << explored << " schedule(s) of " << tests.size() << " test(s), seed "
                      << options.schedule_seed << '\n';
        }
    }
    std::cout << ran << " test(s), " << ran - failed << " passed, " << failed << " failed\n";
    std::cout.flush();
    return gpu.finish(failed == 0 ? 0 : 1, options.stats);
}

} // namespace

// `check --task-safety`: for every function of the linked program, whether
// a task may call it (Concurrency.md section 5.2) and whether it can be the
// worker of a parallel map (section 12), with the first reason when not.
// The analysis runs on the specialized program, so a generic function is
// listed once for each specialization the program uses.
int task_safety_report(neura::IrProgram program) {
    neura::require_valid_ir(program, "before specialization");
    neura::materialize_reference_storage(program);
    neura::specialize(program);
    const neura::TaskAnalysis analysis(program);
    const auto file_of = [&](neura::SymbolId id) {
        for (std::size_t hops = 0; id < program.symbols.size() && hops < program.symbols.size(); ++hops) {
            if (program.symbols[id].kind == neura::SymbolKind::File) return id;
            id = program.symbols[id].owner;
        }
        return neura::no_symbol;
    };
    const auto qualified = [&](neura::SymbolId id) {
        std::string name = program.symbols[id].name;
        for (auto owner = program.symbols[id].owner; owner < program.symbols.size();
             owner = program.symbols[owner].owner) {
            if (program.symbols[owner].kind == neura::SymbolKind::File) break;
            name = program.symbols[owner].name + "." + name;
        }
        return name;
    };
    const auto place = [](const neura::Span& span) {
        const auto slash = span.file.find_last_of("/\\");
        return (slash == std::string::npos ? span.file : span.file.substr(slash + 1)) + ":" +
               std::to_string(span.line) + ":" + std::to_string(span.column);
    };
    std::map<std::string, std::vector<std::string>> by_file;
    std::size_t safe = 0;
    std::size_t isolated = 0;
    std::size_t total = 0;
    std::set<neura::SymbolId> with_body;
    for (const auto& body : program.bodies) with_body.insert(body.owner);
    for (neura::SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != neura::SymbolKind::Function || !with_body.contains(id)) continue;
        const auto file = file_of(id);
        if (file == neura::no_symbol) continue;
        // Functions the compiler generates (closures, initializers) are
        // covered through the functions that use them.
        if (qualified(id).find('$') != std::string::npos) continue;
        const auto& source = program.symbols[file];
        const auto label = source.library_path.empty() ? fs::path(source.name).filename().string() : source.library_path;
        std::string line = "  " + qualified(id) + ": ";
        ++total;
        if (const auto reason = analysis.unsafe(id)) {
            line += "not task-safe: " + reason->operation + " at " + place(reason->span);
        } else if (const auto impure = analysis.not_isolated(id)) {
            ++safe;
            line += "task-safe; not for a parallel map: " + impure->operation + " at " + place(impure->span);
        } else {
            ++safe;
            ++isolated;
            line += "task-safe";
        }
        by_file[label].push_back(std::move(line));
    }
    for (auto& [label, lines] : by_file) {
        std::sort(lines.begin(), lines.end());
        std::cout << label << "\n";
        for (const auto& line : lines) std::cout << line << "\n";
    }
    std::cout << total << " function(s): " << safe << " task-safe, of which " << isolated
              << " can also be the worker of a parallel map\n";
    return 0;
}

namespace {

// Commands after which an update notice may appear: the ones that do their
// work and exit. run and test execute user programs, and the lifecycle
// commands manage the install themselves.
bool shows_update_notice(Command command) {
    switch (command) {
    case Command::Check: case Command::Docs: case Command::EmitIr: case Command::Build: case Command::Inspect:
    case Command::Disassemble: case Command::Assemble: case Command::Decompile: case Command::CBindings:
        return true;
    default:
        return false;
    }
}

int dispatch(const Options& options) {
    switch (options.command) {
    case Command::Version:
        std::cout << neura::lifecycle::version_line() << "\n";
        return 0;
    case Command::Help:
        usage(std::cout);
        return 0;
    case Command::Doctor:
        return neura::lifecycle::doctor(options.executable, {options.library});
    case Command::Update:
        return neura::lifecycle::update(options.executable, options.update);
    case Command::Uninstall:
        return neura::lifecycle::uninstall(options.executable, options.uninstall);
    case Command::Check: {
        auto program = load_source_program(options);
        if (options.task_safety) return task_safety_report(std::move(program));
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
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(std::cerr);
        return 1;
    }
    try {
        const auto options = read_options(argc, argv);
        const int status = dispatch(options);
        if (status == 0 && shows_update_notice(options.command)) neura::lifecycle::update_notice(options.executable);
        return status;
    } catch (const neura::Diagnostic& error) {
        const auto& span = error.span;
        std::cerr << span.file << ':' << span.line << ':' << span.column << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}
