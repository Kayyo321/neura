#include "source_loader.hpp"

#include "lexer/lexer.hpp"
#include "module_path.hpp"

#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>

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

fs::path find_module(const Expr& import, const std::vector<fs::path>& roots) {
    if (import.text == "SysIo" || import.text == "Test") {
        return {};
    }
    const auto relative = module_path(import.text);
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
        throw Diagnostic(import.span, "ambiguous import '" + import.text + "'");
    }
    return candidates.empty() ? fs::path{} : *candidates.begin();
}

} // namespace

std::vector<ParsedFile> read_sources(const std::vector<fs::path>& files,
                                     const std::vector<fs::path>& roots) {
    auto pending = files;
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
        std::vector<const Expr*> imports;
        for (const auto& statement : syntax.statements) {
            collect_imports(statement, imports);
        }
        std::set<std::string> seen_imports;
        for (const auto* import : imports) {
            if (!seen_imports.insert(import->text).second) {
                continue;
            }
            auto dependency = find_module(*import, roots);
            if (!dependency.empty()) {
                syntax.imports.push_back({import->text, dependency.generic_string()});
                pending.push_back(std::move(dependency));
            }
        }
        parsed.push_back(std::move(syntax));
    }
    return parsed;
}

} // namespace neura
