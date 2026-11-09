#include "backend/converter.hpp"
#include "ir-optimization/optimizer.hpp"
#include "neura-ir/ir.hpp"
#include "neura-ir/nir_format.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/host.hpp"
#include "nvm/machine.hpp"
#include "nvr/format.hpp"
#include "source_loader.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
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
           "       neura run [--max-steps N] [--max-frames N] [--max-heap-cells N] <file.nvr> [-- args...]\n";
}

enum class Command { Check, EmitIr, Build, Run };

struct Options {
    Command command = Command::Check;
    bool optimize = true;
    bool source_map = true;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
    std::optional<fs::path> output;
    std::vector<std::string> arguments;
    neura::nvm::RunLimits limits;
};

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
        throw std::runtime_error("expected check, emit-ir, build, or run");
    }
    Options options;
    const std::string_view command = argv[1];
    if (command == "check") {
        options.command = Command::Check;
    } else if (command == "emit-ir") {
        options.command = Command::EmitIr;
    } else if (command == "build") {
        options.command = Command::Build;
    } else if (command == "run") {
        options.command = Command::Run;
    } else {
        throw std::runtime_error("expected check, emit-ir, build, or run");
    }
    const bool compiles = options.command != Command::Run;
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
            if (options.command != Command::EmitIr && options.command != Command::Build) {
                throw std::runtime_error("--no-optimize applies only to emit-ir and build");
            }
            options.optimize = false;
        } else if (arg == "--no-source-map") {
            if (options.command != Command::Build) {
                throw std::runtime_error("--no-source-map applies only to build");
            }
            options.source_map = false;
        } else if (arg == "-o") {
            if (options.command != Command::EmitIr && options.command != Command::Build) {
                throw std::runtime_error("-o applies only to emit-ir and build");
            }
            options.output = fs::path(next());
        } else if (options.command == Command::Run && arg == "--max-steps") {
            options.limits.max_steps = parse_limit(arg, next());
        } else if (options.command == Command::Run && arg == "--max-frames") {
            const auto value = parse_limit(arg, next());
            if (value > 0xffffffffU) {
                throw std::runtime_error("--max-frames is too large");
            }
            options.limits.max_frames = static_cast<std::uint32_t>(value);
        } else if (options.command == Command::Run && arg == "--max-heap-cells") {
            options.limits.max_heap_cells = parse_limit(arg, next());
        } else if (arg.starts_with('-')) {
            throw std::runtime_error("unknown option: " + std::string(arg));
        } else if (options.command == Command::Run && !options.files.empty()) {
            throw std::runtime_error("program arguments follow --");
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
        return neura::lower(neura::read_sources(options.files, roots));
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
    // Unoptimized emission skips verification so that IR produced by a
    // faulty lowering can still be inspected.
    if (options.optimize) {
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

int run(const Options& options) {
    const auto& path = options.files.front();
    neura::nvr::Image image;
    try {
        image = neura::nvr::load(read_file(path));
    } catch (const neura::nvr::LoadError& error) {
        throw std::runtime_error(path.string() + ": " + std::string(neura::nvr::describe(error.kind)) + ": " +
                                 error.what());
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    neura::nvm::StreamSink sink(stdout);
    neura::nvm::ConsoleWriteLine console(sink);
    neura::nvm::HostRegistry registry;
    registry.add(console);
    const auto bindings = neura::nvm::bind(image, registry);
    const auto result = neura::nvm::run(image, bindings, options.arguments, options.limits);
    if (!result.completed) {
        std::cerr << path.string() << ": trap: " << neura::nvm::describe(result.trap, image) << '\n';
        return trap_status;
    }
    return result.status;
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
            }()));
            return 0;
        case Command::EmitIr:
            return emit_ir(options);
        case Command::Build:
            return build(options);
        case Command::Run:
            return run(options);
        }
    } catch (const neura::Diagnostic& error) {
        const auto& span = error.span;
        std::cerr << span.file << ':' << span.line << ':' << span.column << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}
