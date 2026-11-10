#include "source_loader.hpp"

#include "lexer/lexer.hpp"
#include "module_path.hpp"

#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace neura {
namespace {

namespace fs = std::filesystem;

void collect_imports(const Declaration& declaration, std::vector<const Expr*>& imports);
void collect_imports(const Statement& statement, std::vector<const Expr*>& imports);

void collect_imports(const Expr& expression, std::vector<const Expr*>& imports) {
    if (expression.kind == Expr::Kind::Import) {
        imports.push_back(&expression);
    }
    for (const auto& operand : expression.operands) {
        collect_imports(operand, imports);
    }
    if (expression.lambda) {
        collect_imports(*expression.lambda, imports);
    }
    if (expression.match) collect_imports(*expression.match, imports);
}

void collect_imports(const Declaration& declaration, std::vector<const Expr*>& imports) {
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

void collect_imports(const Statement& statement, std::vector<const Expr*>& imports) {
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
    for (const auto& arm : statement.arms) {
        for (const auto& child : arm.body) collect_imports(child, imports);
        if (arm.value) collect_imports(*arm.value, imports);
    }
}

bool std_namespace(std::string_view name) {
    return name == "Std" || name.starts_with("Std.");
}

std::string library_path(const fs::path& path, const fs::path& library) {
    if (library.empty()) return {};
    const auto relative = path.lexically_relative(library);
    if (relative.empty() || *relative.begin() != "std") return {};
    return relative.generic_string();
}

bool internal_module(std::string_view name) {
    return name.ends_with(".Internal") || name.find(".Internal.") != std::string_view::npos;
}

fs::path find_module(const Expr& import, const std::vector<fs::path>& roots, const fs::path& library,
                     bool from_library) {
    const auto relative = module_path(import.text);
    if (std_namespace(import.text)) {
        if (internal_module(import.text) && !from_library) {
            throw Diagnostic(import.span, "cannot import internal standard library module '" + import.text + "'");
        }
        const auto path = library / relative;
        if (!library.empty() && fs::is_regular_file(path)) {
            const auto actual = fs::canonical(path);
            // Resolve within the trusted tree without allowing an alias
            // to bypass an Internal component of the actual module path.
            if (library_path(actual, library) == relative.generic_string()) return actual;
        }
        throw Diagnostic(import.span, "cannot resolve standard library import '" + import.text + "'");
    }
    std::set<fs::path> candidates;
    auto search = [&](const fs::path& root) {
        const auto path = root / relative;
        if (fs::is_regular_file(path)) {
            const auto actual = fs::canonical(path);
            if (library_path(actual, library).empty()) {
                candidates.insert(actual);
            }
        }
    };
    search(fs::path(import.span.file).parent_path());
    for (const auto& root : roots) {
        search(root);
    }
    if (candidates.size() > 1) {
        throw Diagnostic(import.span, "ambiguous import '" + import.text + "'");
    }
    return candidates.empty() ? fs::path{} : *candidates.begin();
}

std::vector<fs::path> find_wildcard_modules(const Expr& import, const std::vector<fs::path>& roots,
                                           const fs::path& library, bool from_library) {
    const auto prefix = import.text.substr(0, import.text.size() - 2);
    const auto standard = std_namespace(prefix);
    auto relative = module_path(prefix);
    relative.replace_extension();
    std::map<fs::path, fs::path> candidates;
    auto search = [&](const fs::path& root) {
        const auto directory = root / relative;
        if (!fs::is_directory(directory)) return;
        for (const auto& item : fs::directory_iterator(directory)) {
            const auto filename = item.path().filename();
            if (filename.extension() != ".nra" || !item.is_regular_file()) continue;
            const auto actual = fs::canonical(item.path());
            const auto trusted_path = library_path(actual, library);
            if (standard) {
                if (trusted_path != (relative / filename).generic_string() ||
                    (!from_library && filename == "internal.nra")) continue;
            } else if (!trusted_path.empty()) {
                continue;
            }
            const auto [previous, added] = candidates.emplace(filename, actual);
            if (!added && previous->second != actual)
                throw Diagnostic(import.span, "ambiguous wildcard import '" + import.text + "'");
        }
    };
    if (standard) {
        if (internal_module(prefix) && !from_library)
            throw Diagnostic(import.span, "cannot import internal standard library module '" + prefix + "'");
        if (!library.empty()) search(library);
    } else {
        search(fs::path(import.span.file).parent_path());
        for (const auto& root : roots) search(root);
    }
    std::vector<fs::path> modules;
    for (const auto& [filename, actual] : candidates) modules.push_back(actual);
    if (modules.empty())
        throw Diagnostic(import.span, "cannot resolve wildcard import '" + import.text + "'");
    return modules;
}

} // namespace

std::vector<ParsedFile> read_sources(const std::vector<fs::path>& files,
                                     const std::vector<fs::path>& roots, const fs::path& library) {
    const auto trusted = library.empty() ? fs::path{} : fs::canonical(library);
    std::vector<fs::path> pending;
    for (const auto& file : files) pending.push_back(fs::canonical(file));
    std::set<fs::path> visited;
    std::vector<ParsedFile> parsed;
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
        auto syntax = parse(lex(source, file.generic_string()));
        syntax.library_path = library_path(file, trusted);
        std::vector<const Expr*> imports;
        for (const auto& statement : syntax.statements) {
            collect_imports(statement, imports);
        }
        std::set<std::string> seen_imports;
        if (syntax.untyped_variadic) {
            const Expr dependency{Expr::Kind::Import, *syntax.untyped_variadic,
                                  "Std.Core.Variadic", {}, {}, std::nullopt};
            auto path = find_module(dependency, roots, trusted, !syntax.library_path.empty());
            syntax.imports.push_back({dependency.text, path.generic_string()});
            seen_imports.insert(dependency.text);
            pending.push_back(std::move(path));
        }
        for (const auto* import : imports) {
            if (!seen_imports.insert(import->text).second) {
                continue;
            }
            if (import->text.ends_with(".*")) {
                for (auto& dependency : find_wildcard_modules(*import, roots, trusted, !syntax.library_path.empty())) {
                    syntax.imports.push_back({import->text, dependency.generic_string()});
                    pending.push_back(std::move(dependency));
                }
                continue;
            }
            auto dependency = find_module(*import, roots, trusted, !syntax.library_path.empty());
            if (!dependency.empty()) {
                syntax.imports.push_back({import->text, dependency.generic_string()});
                pending.push_back(std::move(dependency));
            }
        }
        parsed.push_back(std::move(syntax));
    }
    std::map<std::string_view, const ParsedFile*, std::less<>> by_path;
    for (const auto& source : parsed) by_path.emplace(source.file, &source);
    for (auto& source : parsed) {
        for (auto& import : source.imports) {
            if (!import.name.ends_with(".*")) continue;
            const auto filename = fs::path(import.file).filename();
            for (const auto& statement : by_path.at(import.file)->statements) {
                const auto& decl = statement.declaration;
                if (decl && decl->kind == Declaration::Kind::Module && module_path(decl->name) == filename) {
                    import.target_module = decl->name;
                    break;
                }
            }
        }
    }
    return parsed;
}

} // namespace neura
