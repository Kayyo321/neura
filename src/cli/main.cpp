#include "lexer/lexer.hpp"
#include "module_path.hpp"
#include "neura-ir/ir.hpp"
#include "parser/syntax.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
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

void collect_imports(const neura::Expr& expression, std::vector<const neura::Expr*>& imports) {
    if (expression.kind == neura::Expr::Kind::Import) {
        imports.push_back(&expression);
    }
    for (const auto& operand : expression.operands) {
        collect_imports(operand, imports);
    }
}

void collect_imports(const neura::Statement& statement, std::vector<const neura::Expr*>& imports);

void collect_imports(const neura::Declaration& declaration, std::vector<const neura::Expr*>& imports) {
    if (declaration.initializer) {
        collect_imports(*declaration.initializer, imports);
    }
    for (const auto& member : declaration.members) {
        collect_imports(member, imports);
    }
    for (const auto& statement : declaration.body) {
        collect_imports(statement, imports);
    }
}

void collect_imports(const neura::Statement& statement, std::vector<const neura::Expr*>& imports) {
    if (statement.declaration) {
        collect_imports(*statement.declaration, imports);
    }
    for (const auto& expression : statement.expressions) {
        collect_imports(expression, imports);
    }
    for (const auto& child : statement.body) {
        collect_imports(child, imports);
    }
    for (const auto& child : statement.alternative) {
        collect_imports(child, imports);
    }
}

fs::path find_module(const neura::Expr& import, const std::vector<fs::path>& roots) {
    if (import.text == "SysIo" || import.text == "Test") {
        return {};
    }
    const auto relative = neura::module_path(import.text);
    std::set<fs::path> candidates;
    auto search = [&](const fs::path& root) {
        const auto path = root / relative;
        if (fs::is_regular_file(path)) {
            candidates.insert(fs::canonical(path));
        }
    };
    search(fs::path(import.span.file).parent_path());
    for (const auto& root : roots) {
        search(root);
    }
    if (candidates.size() > 1) {
        throw neura::Diagnostic(import.span, "ambiguous import '" + import.text + "'");
    }
    return candidates.empty() ? fs::path{} : *candidates.begin();
}

std::vector<neura::ParsedFile> read_sources(const Options& options) {
    auto pending = options.files;
    std::set<fs::path> visited;
    std::vector<neura::ParsedFile> files;
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto file = pending[i];
        if (!visited.insert(file).second) {
            continue;
        }
        if (!fs::is_regular_file(file)) {
            throw std::runtime_error("not a regular input file: " + file.string());
        }
        std::ifstream input(file, std::ios::binary);
        if (!input) {
            throw std::runtime_error("cannot read " + file.string());
        }
        const std::string source(std::istreambuf_iterator<char>{input}, {});
        if (input.bad()) {
            throw std::runtime_error("error reading " + file.string());
        }
        auto syntax = neura::parse(neura::lex(source, file.generic_string()));
        std::vector<const neura::Expr*> imports;
        for (const auto& statement : syntax.statements) {
            collect_imports(statement, imports);
        }
        std::set<std::string> seen_imports;
        for (const auto* import : imports) {
            if (!seen_imports.insert(import->text).second) {
                continue;
            }
            auto dependency = find_module(*import, options.roots);
            if (!dependency.empty()) {
                syntax.imports.push_back({import->text, dependency.generic_string()});
                pending.push_back(std::move(dependency));
            }
        }
        files.push_back(std::move(syntax));
    }
    return files;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }

    try {
        const auto options = read_options(argc, argv);
        const auto program = neura::lower(read_sources(options));
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
