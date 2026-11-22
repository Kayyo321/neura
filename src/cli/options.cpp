#include "options.hpp"
#include "tool.hpp"

#include "jit/jit.hpp"
#include "util/diagnostic.hpp"
#include "util/diagnostic_codes.hpp"
#include "util/suggest.hpp"

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

neura::nvm::Capabilities granted_capabilities() {
    neura::nvm::Capabilities capabilities;
    capabilities.fs = capabilities.net = capabilities.env = capabilities.clock = capabilities.entropy = true;
    capabilities.native = true;
    return capabilities;
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

namespace {

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
    } else if (arg == "--max-stack-cells") {
        options.limits.max_stack_cells = parse_limit(arg, next());
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
    } else if (arg == "--allow-raw") {
        options.capabilities.raw = true;
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
    } else if (arg == "--deny-raw") {
        options.capabilities.raw = false;
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
    } else if (arg == "--sql-pool") {
        options.sql_pools.emplace_back(next());
        options.capabilities.sql = true;
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
        } else if (options.command == Command::Update && arg == "--nightly") {
            options.update.nightly = true;
        } else if (options.command == Command::Update && arg == "--source") {
            options.update.source = value();
            options.update.source_given = true;
        } else if (options.command == Command::Update && arg == "--install-from") {
            options.update.install_from = value();
        } else if (options.command == Command::Update && arg == "--add") {
            options.update.add = value();
        } else if (options.command == Command::Update && arg == "--force") {
            options.update.force = true;
        } else if (options.command == Command::Update && arg == "--components") {
            options.update.components = value();
        } else if (options.command == Command::Update && arg == "--migrate-install") {
            options.update.migrate = true;
        } else if (options.command == Command::Update && arg == "--rollback") {
            options.update.rollback = true;
        } else if (options.command == Command::Update && arg == "--repair") {
            options.update.repair = true;
        } else if ((options.command == Command::Update || options.command == Command::Uninstall) && arg == "--terminate") {
            options.update.terminate = true;
            options.uninstall.terminate = true;
        } else if (options.command == Command::Uninstall && arg == "--component") {
            options.uninstall.component = value();
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

} // namespace

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
    } else if (command == "tags") {
        // The tag catalogue (docs/lang/Tags.md), or one tag's entry.
        options.command = Command::Tags;
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg.starts_with('-')) throw std::runtime_error("unknown option for tags: " + std::string(arg));
            if (!options.explain_code.empty()) throw std::runtime_error("tags takes at most one tag name");
            options.explain_code = arg.starts_with('#') ? arg.substr(1) : arg;
        }
        return options;
    } else if (command == "explain" || command == "--explain") {
        options.command = Command::Explain;
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--list") options.explain_list = true;
            else if (arg.starts_with('-')) throw std::runtime_error("unknown option for explain: " + std::string(arg));
            else if (!options.explain_code.empty()) throw std::runtime_error("explain takes one code");
            else options.explain_code = arg;
        }
        if (options.explain_list == !options.explain_code.empty())
            throw std::runtime_error("explain expects a code, such as `neura explain E0401`, or --list");
        return options;
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
        } else if (arg == "--list") {
            if (options.command != Command::Test) throw std::runtime_error("--list applies only to test");
            options.list_tests = true;
        } else if (arg.starts_with("--format=")) {
            if (options.command != Command::Test) throw std::runtime_error("--format applies only to test --list");
            const auto format = arg.substr(9);
            if (format != "json" && format != "human") throw std::runtime_error("--format expects json or human");
            options.test_format_json = format == "json";
            options.test_format_set = true;
        } else if (arg == "--coverage" || arg.starts_with("--coverage=")) {
            if (options.command != Command::Test) throw std::runtime_error("--coverage applies only to test");
            const std::string_view path = arg == "--coverage" ? std::string_view(next()) : arg.substr(11);
            if (path.empty()) throw std::runtime_error("--coverage expects an output JSON path");
            options.test_coverage = fs::path(path);
        } else if (arg == "--workspace-root") {
            if (options.command != Command::Test) throw std::runtime_error("--workspace-root applies only to test");
            const auto directory = fs::canonical(next());
            if (!fs::is_directory(directory)) throw std::runtime_error("--workspace-root expects a directory");
            options.test_workspace = directory;
        } else if (arg == "--filter" || arg.starts_with("--filter=")) {
            if (options.command != Command::Test) throw std::runtime_error("--filter applies only to test");
            const std::string_view filter = arg == "--filter" ? std::string_view(next()) : arg.substr(9);
            if (filter.empty()) throw std::runtime_error("--filter expects an exact test id");
            options.test_filters.emplace_back(filter);
        } else if (arg.starts_with("--reporter=")) {
            if (options.command != Command::Test) throw std::runtime_error("--reporter applies only to test");
            const auto reporter = arg.substr(11);
            if (reporter != "json" && reporter != "human") throw std::runtime_error("--reporter expects json or human");
            options.test_reporter_json = reporter == "json";
        } else if (arg.starts_with("--error-format=")) {
            const auto format = arg.substr(15);
            if (format == "human") options.diagnostics.format = neura::cli::ErrorFormat::Human;
            else if (format == "short") options.diagnostics.format = neura::cli::ErrorFormat::Short;
            else if (format == "json") options.diagnostics.format = neura::cli::ErrorFormat::Json;
            else throw std::runtime_error("--error-format expects human, short, or json");
        } else if (arg.starts_with("--color=")) {
            const auto choice = arg.substr(8);
            if (choice == "auto") options.diagnostics.color = neura::cli::ColorChoice::Auto;
            else if (choice == "always") options.diagnostics.color = neura::cli::ColorChoice::Always;
            else if (choice == "never") options.diagnostics.color = neura::cli::ColorChoice::Never;
            else throw std::runtime_error("--color expects auto, always, or never");
        } else if (arg.starts_with("-W") || arg == "--warn-library") {
            if (!compiles || options.command == Command::CBindings)
                throw std::runtime_error(std::string(arg) + " applies only to check, emit-ir, build, and test");
            if (arg == "--warn-library") {
                options.diagnostics.library_warnings = true;
            } else if (arg == "-Werror") {
                options.diagnostics.warnings_are_errors = true;
            } else {
                const bool enable = !arg.starts_with("-Wno-");
                const auto warning = arg.substr(enable ? 2 : 5);
                if (warning.empty() || warning.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") !=
                                           std::string_view::npos)
                    throw std::runtime_error("expected a warning name after " + std::string(enable ? "-W" : "-Wno-"));
                if (!neura::find_warning(warning)) {
                    std::vector<std::string> names;
                    for (const auto& entry : neura::warning_codes) names.emplace_back(entry.name);
                    std::string message = "unknown warning '" + std::string(warning) + "'";
                    if (const auto close = neura::closest_name(warning, names))
                        message += "; did you mean " + *close + "?";
                    else message += "; `neura explain --list` lists the warnings and their names";
                    throw std::runtime_error(message);
                }
                options.diagnostics.warnings.insert_or_assign(std::string(warning), enable);
            }
        } else if (arg == "--max-errors" || arg.starts_with("--max-errors=")) {
            if (!compiles) throw std::runtime_error("--max-errors applies only to check, emit-ir, build, and test");
            const std::string_view text = arg == "--max-errors" ? std::string_view(next()) : arg.substr(13);
            std::size_t limit = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), limit);
            if (error != std::errc{} || end != text.data() + text.size() || limit == 0)
                throw std::runtime_error("expected a positive integer after --max-errors");
            neura::error_limit() = limit;
        } else if (arg == "--fix") {
            if (options.command != Command::Check) throw std::runtime_error("--fix applies only to check");
            options.fix = true;
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
        } else if (arg == "--no-lift-hints") {
            if (options.command != Command::Build) {
                throw std::runtime_error("--no-lift-hints applies only to build");
            }
            options.lift_hints = false;
        } else if (arg == "--tier=0" || arg == "--tier=1" || arg == "--tier=2") {
            if (!runs) throw std::runtime_error("--tier applies only to run and test");
            options.limits.tier = static_cast<std::uint8_t>(arg.back() - '0');
        } else if (arg.starts_with("--tier=")) {
            throw std::runtime_error("--tier expects 0, 1, or 2");
        } else if (arg.starts_with("--jit=")) {
            if (!runs) throw std::runtime_error("--jit applies only to run and test");
            if (!neura::jit::parse_mode(std::string(arg.substr(6)), options.limits.jit)) {
                throw std::runtime_error("--jit expects off, on, or auto");
            }
        } else if (arg == "--tier-stats") {
            if (!runs) throw std::runtime_error("--tier-stats applies only to run and test");
            options.tier_stats = true;
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
        } else if (arg == "--show-derived" || arg.starts_with("--show-derived=")) {
            if (options.command != Command::Check) throw std::runtime_error("--show-derived applies only to check");
            if (arg == "--show-derived=") throw std::runtime_error("--show-derived= needs a module name");
            options.show_derived = arg == "--show-derived" ? "" : std::string(arg.substr(15));
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
        } else if (arg == "--runtime" || arg.starts_with("--runtime=")) {
            // Tests and debugging are direct in 0.3.0 (Service030.md).
            if (options.command == Command::Test) {
                throw RuntimeError("neura test runs directly; --runtime applies only to run", 1);
            }
            if (options.command != Command::Run) throw std::runtime_error("--runtime applies only to run");
            const auto mode = arg == "--runtime" ? std::string_view() : arg.substr(10);
            if (mode == "off") options.runtime = RuntimeMode::Off;
            else if (mode == "auto") options.runtime = RuntimeMode::Auto;
            else if (mode == "required") options.runtime = RuntimeMode::Required;
            else throw RuntimeError("--runtime expects off, auto, or required", 1);
        } else if (arg == "--detach" || arg.starts_with("--stdout=") || arg.starts_with("--stderr=") ||
                   arg.starts_with("--stdin=") || arg.starts_with("--runtime-report=")) {
            if (options.command != Command::Run) {
                throw std::runtime_error(std::string(arg.substr(0, arg.find('='))) + " applies only to run");
            }
            const auto value = [&](std::string_view prefix) {
                const auto path = arg.substr(prefix.size());
                if (path.empty()) throw RuntimeError(std::string(prefix) + " expects a file", 1);
                return fs::path(path);
            };
            if (arg == "--detach") options.detach = true;
            else if (arg.starts_with("--stdout=")) options.detach_stdout = value("--stdout=");
            else if (arg.starts_with("--stderr=")) options.detach_stderr = value("--stderr=");
            else if (arg.starts_with("--stdin=")) options.detach_stdin = value("--stdin=");
            else options.runtime_report = value("--runtime-report=");
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
    if (options.test_coverage) {
        if (options.list_tests) throw std::runtime_error("--coverage cannot be combined with --list");
        // Count every source-mapped block, without optimizer removal or
        // lifted/JIT regions bypassing the reference block observer.
        options.optimize = false;
        options.limits.tier = 0;
        options.limits.jit = neura::jit::Mode::Off;
        options.limits.parallel_workers = 1;
        options.gpu = {};
        options.gpu.mode = neura::cli::GpuMode::Off;
    }
    if (options.test_format_set && !options.list_tests)
        throw std::runtime_error("--format requires test --list");
    if (options.list_tests && options.test_reporter_json)
        throw std::runtime_error("--reporter=json cannot be combined with --list; use --format=json");
    if (options.test_format_json || options.test_reporter_json)
        options.diagnostics.format = neura::cli::ErrorFormat::Json;
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
    if (options.show_derived && (options.fix || options.task_safety))
        throw std::runtime_error("--show-derived cannot be combined with --fix or --task-safety");
    if (options.fix && options.task_safety) {
        throw std::runtime_error("--fix cannot be combined with --task-safety");
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
    if (options.detach && (!options.runtime || *options.runtime == RuntimeMode::Off)) {
        throw RuntimeError("--detach needs --runtime=auto or --runtime=required", 1);
    }
    if (options.detach && (!options.detach_stdout || !options.detach_stderr)) {
        throw RuntimeError("--detach needs --stdout and --stderr", 1);
    }
    if (!options.detach && (options.detach_stdout || options.detach_stderr || options.detach_stdin)) {
        throw RuntimeError("--stdout, --stderr and --stdin apply only with --detach", 1);
    }
    if (!options.sql_pools.empty() && options.runtime && *options.runtime != RuntimeMode::Off) {
        throw RuntimeError("--sql-pool applies only to a direct run; run without --runtime", 1);
    }
#if defined(_WIN32) && !defined(NEURA_HAS_SERVICE)
    // A build without the runtime service (cmake/windows/service.cmake).
    if (options.runtime && *options.runtime != RuntimeMode::Off) {
        throw RuntimeError("the service is not available in this build; run without --runtime", 5);
    }
    if (options.runtime_report) {
        throw RuntimeError("--runtime-report needs the runtime service, which this build does not have", 1);
    }
#endif
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

} // namespace neura::cli
