#include "neura-ir/ir.hpp"
#include "source_loader.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct Options {
    bool emit_ir = false;
    std::vector<fs::path> files;
    std::vector<fs::path> roots;
};

void usage(std::ostream& out) {
    out << "usage: neura <check|emit-ir> [-I source-root] <file.nra>...\n";
}

Options read_options(int argc, char** argv) {
    if (argc < 2 || (std::string_view(argv[1]) != "check" && std::string_view(argv[1]) != "emit-ir")) {
        throw std::runtime_error("expected check or emit-ir");
    }

    Options options;
    options.emit_ir = std::string_view(argv[1]) == "emit-ir";
    bool positional_only = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (!positional_only && arg == "--") {
            positional_only = true;
        } else if (!positional_only && arg == "-I") {
            if (++i == argc) {
                throw std::runtime_error("expected a source root after -I");
            }
            const auto root = fs::canonical(argv[i]);
            if (!fs::is_directory(root)) {
                throw std::runtime_error("source root is not a directory: " + root.string());
            }
            options.roots.push_back(root);
        } else if (!positional_only && arg.starts_with('-')) {
            throw std::runtime_error("unknown option: " + std::string(arg));
        } else {
            options.files.push_back(fs::canonical(argv[i]));
        }
    }
    if (options.files.empty()) {
        throw std::runtime_error("expected at least one input file");
    }
    for (const auto& file : options.files) {
        options.roots.push_back(file.parent_path());
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }

    try {
        const auto options = read_options(argc, argv);
        const auto program = neura::lower(neura::read_sources(options.files, options.roots));
        if (options.emit_ir) {
            neura::print_ir(std::cout, program);
        }
        return 0;
    } catch (const neura::Diagnostic& error) {
        const auto& span = error.span;
        std::cerr << span.file << ':' << span.line << ':' << span.column
                  << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}
