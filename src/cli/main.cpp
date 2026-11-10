#include "backend/converter.hpp"
#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"
#include "nvm/trace.hpp"
#include "nvm/trace_file.hpp"
#include "nvr/format.hpp"
#include "nvr/names.hpp"
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

namespace fs = std::filesystem;

// Process status for a run that ends in a trap, distinct from load and
// usage errors (1). A completed run returns the entry function's status.
constexpr int trap_status = 70;

void usage(std::ostream& out) {
    out << "usage: neura check [-I source-root] <file.nra>...\n"
           "       neura emit-ir [-I source-root] [--no-optimize] <file.nra>... [-o <file.nir>]\n"
           "       neura emit-ir [--no-optimize] <file.nir>\n"
           "       neura build [-I source-root] [--no-optimize] [--no-source-map] <file.nra>... -o <file.nvr>\n"
           "       neura build [--no-optimize] [--no-source-map] <file.nir> -o <file.nvr>\n"
           "       neura run [--max-steps N] [--max-frames N] [--max-heap-cells N] [--stats] [--profile] [capabilities] "
           "[--record <file> | --replay <file>] <file.nvr> [-- args...]\n"
           "       neura test [-I source-root] [--no-optimize] [--max-steps N] [--max-frames N]\n"
           "                  [--max-heap-cells N] [--stats] [capabilities] <file.nra>...\n"
           "       neura disassemble <file.nvr> [-o <file.nvra>]\n"
           "       neura assemble <file.nvra> -o <file.nvr>\n"
           "       neura decompile [--no-provenance] <file.nvr> [-o <file>]\n"
           "\n"
           "capabilities: run and test allow only console input and output (and process exit) by\n"
           "default; a denied host service fails with error 14 (CapabilityDenied).\n"
           "  --allow-fs[=prefix,...]  files and directories, optionally only below the prefixes\n"
           "  --allow-net              TCP connections and listeners\n"
           "  --allow-env              environment variables\n"
           "  --allow-clock            wall and monotonic clocks and sleeping\n"
           "  --allow-entropy          operating-system entropy\n"
           "  --record <file>          (run) write every host request and response to a trace\n"
           "  --profile                (run) print the steps each function took\n"
           "  --replay <file>          (run) answer host requests from a trace instead of services\n";
}

enum class Command { Check, EmitIr, Build, Run, Test, Disassemble, Assemble, Decompile };

constexpr const char* commands = "expected check, emit-ir, build, run, test, disassemble, assemble, or decompile";

struct Options {
    Command command = Command::Check;
    bool optimize = true;
    bool source_map = true;
    bool provenance = true;
    // Report execution costs: steps, peak heap cells, and image size.
    bool stats = false;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
    fs::path library;
    std::optional<fs::path> output;
    std::vector<std::string> arguments;
    neura::nvm::RunLimits limits;
    neura::nvm::Capabilities capabilities;
    // A plain --allow-fs grants every path, whatever prefixes are also given.
    bool all_paths = false;
    std::optional<fs::path> record;
    std::optional<fs::path> replay;
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

fs::path standard_library(const fs::path& launch) {
    const auto executable = executable_path(launch);
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

Options read_options(int argc, char** argv) {
    if (argc < 2) {
        throw std::runtime_error(commands);
    }
    Options options;
    options.library = standard_library(argv[0]);
    const std::string_view command = argv[1];
    if (command == "check") {
        options.command = Command::Check;
    } else if (command == "emit-ir") {
        options.command = Command::EmitIr;
    } else if (command == "build") {
        options.command = Command::Build;
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
    } else {
        throw std::runtime_error(commands);
    }
    const bool runs = options.command == Command::Run || options.command == Command::Test;
    const bool compiles = options.command == Command::Check || options.command == Command::EmitIr ||
                          options.command == Command::Build || options.command == Command::Test;
    const bool single_input = options.command == Command::Run || options.command == Command::Disassemble ||
                              options.command == Command::Assemble || options.command == Command::Decompile;
    bool positional_only = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto next = [&]() -> const char* {
            if (++i == argc) {
                throw std::runtime_error("expected a value after " + std::string(arg));
            }
            return argv[i];
        };
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
        } else if (arg == "--no-source-map") {
            if (options.command != Command::Build) {
                throw std::runtime_error("--no-source-map applies only to build");
            }
            options.source_map = false;
        } else if (arg == "-o") {
            if (options.command != Command::EmitIr && options.command != Command::Build &&
                options.command != Command::Disassemble && options.command != Command::Assemble &&
                options.command != Command::Decompile) {
                throw std::runtime_error("-o applies only to emit-ir, build, disassemble, assemble, and decompile");
            }
            options.output = fs::path(next());
        } else if (runs && arg == "--max-steps") {
            options.limits.max_steps = parse_limit(arg, next());
        } else if (runs && arg == "--max-frames") {
            const auto value = parse_limit(arg, next());
            if (value > 0xffffffffU) {
                throw std::runtime_error("--max-frames is too large");
            }
            options.limits.max_frames = static_cast<std::uint32_t>(value);
        } else if (runs && arg == "--max-heap-cells") {
            options.limits.max_heap_cells = parse_limit(arg, next());
        } else if (options.command == Command::Run && arg == "--profile") {
            options.limits.profile = true;
        } else if (runs && arg == "--stats") {
            options.stats = true;
        } else if (runs && arg == "--allow-fs") {
            options.capabilities.fs = true;
            options.all_paths = true;
        } else if (runs && arg.starts_with("--allow-fs=")) {
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
        } else if (runs && arg == "--allow-net") {
            options.capabilities.net = true;
        } else if (runs && arg == "--allow-env") {
            options.capabilities.env = true;
        } else if (runs && arg == "--allow-clock") {
            options.capabilities.clock = true;
        } else if (runs && arg == "--allow-entropy") {
            options.capabilities.entropy = true;
        } else if (options.command == Command::Run && arg == "--record") {
            options.record = fs::path(next());
        } else if (options.command == Command::Run && arg == "--replay") {
            options.replay = fs::path(next());
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
    if (options.files.empty()) {
        throw std::runtime_error(options.command == Command::Run ? "expected an image file"
                                                                : "expected at least one input file");
    }
    if (options.command == Command::Build && !options.output) {
        throw std::runtime_error("build requires -o <file.nvr>");
    }
    if (options.command == Command::Assemble && !options.output) {
        throw std::runtime_error("assemble requires -o <file.nvr>");
    }
    if (options.record && options.replay) {
        throw std::runtime_error("--record and --replay cannot be combined");
    }
    if (options.all_paths) {
        options.capabilities.fs_prefixes.clear();
    }
    return options;
}

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (input.bad()) {
        throw std::runtime_error("error reading " + path.string());
    }
    return bytes;
}

// Writes through a temporary file and renames it into place, so a failed
// build never leaves a partial output.
void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot write " + path.string());
        }
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            fs::remove(temporary);
            throw std::runtime_error("error writing " + path.string());
        }
    }
    fs::rename(temporary, path);
}

bool is_nir_input(const fs::path& path) {
    return path.extension() == ".nir";
}

// Loads the program from source files or from one .nir file.
neura::IrProgram load_program(const Options& options) {
    const bool nir = std::any_of(options.files.begin(), options.files.end(), is_nir_input);
    if (!nir) {
        auto roots = options.roots;
        for (const auto& file : options.files) {
            roots.push_back(file.parent_path());
        }
        return neura::lower(neura::read_sources(options.files, roots, options.library));
    }
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
        neura::specialize(program);
        neura::optimize(program);
    }
    neura::print_ir(std::cout, program);
    return 0;
}

int build(const Options& options) {
    neura::backend::BuildOptions build_options;
    build_options.optimize = options.optimize;
    build_options.source_map = options.source_map;
    const auto conversion = neura::backend::build(load_program(options), build_options);
    const auto bytes = neura::nvr::encode(conversion.image);
    // Load the encoded image before publishing it, so that a writer or
    // converter fault cannot produce an executable the runtime rejects.
    neura::nvr::load(bytes);
    write_file(options.output.value(), bytes);
    return 0;
}

neura::nvr::Image load_image(const fs::path& path) {
    try {
        return neura::nvr::load(read_file(path));
    } catch (const neura::nvr::LoadError& error) {
        throw std::runtime_error(path.string() + ": " + std::string(neura::nvr::describe(error.kind)) + ": " +
                                 error.what());
    }
}

void write_text(const Options& options, const std::string& text) {
    if (options.output) {
        write_file(*options.output, std::vector<std::uint8_t>(text.begin(), text.end()));
    } else {
        std::cout << text;
    }
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

// The size of an image's executable circuits: graph, neuron, and edge counts.
std::string image_costs(const neura::nvr::Image& image) {
    std::uint64_t neurons = 0;
    std::uint64_t edges = 0;
    for (const auto& graph : image.graphs) {
        neurons += graph.nodes.size();
        for (const auto& node : graph.nodes) edges += node.edges.size();
    }
    return "graphs " + std::to_string(image.graphs.size()) + ", neurons " + std::to_string(neurons) + ", edges " +
           std::to_string(edges);
}

// The host services of neura run and neura test: the console line writer and
// the standard providers under the granted capabilities.
struct Host {
    neura::nvm::StreamSink output{stdout};
    neura::nvm::StreamSink error{stderr};
    neura::nvm::DescriptorSource input{0};
    neura::nvm::ConsoleWriteLine console{output};
    neura::nvm::StandardServices services;
    neura::nvm::HostRegistry registry;

    explicit Host(const neura::nvm::Capabilities& capabilities)
        : services(output, error, input, capabilities) {
        registry.add(console);
        services.register_all(registry);
    }
};

neura::nvm::RunResult result_of(const neura::nvm::Trace& trace) {
    neura::nvm::RunResult result;
    result.completed = trace.outcome.completed;
    result.status = trace.outcome.status;
    result.trap = trace.trap;
    result.steps = trace.steps;
    result.host_requests = trace.entries.size();
    result.peak_heap_cells = trace.peak_heap_cells;
    return result;
}

neura::nvm::Trace read_trace_file(const fs::path& path) {
    const auto bytes = read_file(path);
    try {
        return neura::nvm::read_trace(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    } catch (const neura::nvm::TraceFormatError& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

// The functions that took the most steps, with names from the image's NAME
// section when it has one.
void print_profile(const neura::nvr::Image& image, const neura::nvm::RunResult& result) {
    const auto names = neura::nvr::decode_names(image);
    std::vector<std::pair<std::uint64_t, std::size_t>> ranked;
    for (std::size_t f = 0; f < result.function_steps.size(); ++f) {
        if (result.function_steps[f] != 0) ranked.emplace_back(result.function_steps[f], f);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::cerr << "profile: steps by function\n";
    for (std::size_t i = 0; i < ranked.size() && i < 25; ++i) {
        const auto [steps, f] = ranked[i];
        std::cerr << "  " << steps << "  f" << f;
        if (names && f < names->functions.size()) std::cerr << "  " << names->functions[f];
        std::cerr << '\n';
    }
}

int run(const Options& options) {
    const auto& path = options.files.front();
    const auto image = load_image(path);
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    Host host(options.capabilities);
    neura::nvm::RunResult result;
    if (options.replay) {
        // No service runs: every response comes from the trace. Console
        // output is reproduced from the recorded requests so that the
        // replayed run prints what the recorded one printed.
        const auto expected = read_trace_file(*options.replay);
        const auto replayed = neura::nvm::replay(
            image, expected, options.arguments, options.limits, [&](const neura::nvm::TraceEntry& entry) {
                neura::nvm::replay_console_output(entry.import, entry.version, entry.arguments, entry.completion,
                                                  host.output, host.error);
            });
        if (!replayed.matches) {
            throw std::runtime_error("replay of " + options.replay->string() + " diverged: " + replayed.difference);
        }
        result = result_of(replayed.trace);
    } else if (options.record) {
        const auto bindings = neura::nvm::bind(image, host.registry);
        const auto trace = neura::nvm::record(image, bindings, options.arguments, options.limits);
        host.services.end_run();
        const auto text = neura::nvm::write_trace(trace);
        write_file(*options.record, std::vector<std::uint8_t>(text.begin(), text.end()));
        result = result_of(trace);
    } else {
        const auto bindings = neura::nvm::bind(image, host.registry);
        result = neura::nvm::run(image, bindings, options.arguments, options.limits);
        host.services.end_run();
    }
    if (options.stats) {
        std::cerr << "stats: steps " << result.steps << ", peak heap cells " << result.peak_heap_cells
                  << ", host requests " << result.host_requests << ", " << image_costs(image)
                  << ", image bytes " << fs::file_size(path) << '\n';
    }
    if (options.limits.profile) {
        print_profile(image, result);
    }
    if (!result.completed) {
        std::cerr << path.string() << ": trap: " << neura::nvm::describe(result.trap, image) << '\n';
        return trap_status;
    }
    return result.status;
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

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::set<std::string> test_labels;
    for (const auto& file : options.files) {
        test_labels.insert(neura::backend::source_label(program, file.generic_string()));
    }
    Host host(options.capabilities);
    const auto bindings = neura::nvm::bind(image, host.registry);
    std::size_t failed = 0;
    for (std::size_t i = 0; i < tests.size(); ++i) {
        // The start functions share the entry signature, so any of them can
        // be the entry of a validated image.
        image.entry = static_cast<std::uint32_t>(i);
        const auto result = neura::nvm::run(image, bindings, {}, options.limits);
        // Each test is its own run: files and sockets it left open close now.
        host.services.end_run();
        // A test that ends the process with a nonzero status through
        // Std.Program.Process.exit fails.
        if (result.completed && result.status != 0) {
            ++failed;
            std::cout << "FAIL " << names[i] << ": the test exited with status " << result.status << '\n';
            continue;
        }
        if (result.completed) {
            std::cout << "PASS " << names[i];
            if (options.stats) {
                std::cout << " (steps " << result.steps << ", peak heap cells " << result.peak_heap_cells << ")";
            }
            std::cout << '\n';
            continue;
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
    }
    std::cout << tests.size() << " test(s), " << tests.size() - failed << " passed, " << failed << " failed\n";
    std::cout.flush();
    return failed == 0 ? 0 : 1;
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
        case Command::Check:
            neura::lower(neura::read_sources(options.files, [&] {
                auto roots = options.roots;
                for (const auto& file : options.files) {
                    roots.push_back(file.parent_path());
                }
                return roots;
            }(), options.library));
            return 0;
        case Command::EmitIr:
            return emit_ir(options);
        case Command::Build:
            return build(options);
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
        }
    } catch (const neura::Diagnostic& error) {
        const auto& span = error.span;
        std::cerr << span.file << ':' << span.line << ':' << span.column << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}
