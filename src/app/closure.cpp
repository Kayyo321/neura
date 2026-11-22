// Package closures (docs/design/Projects030.md, "The project manifest"): a
// module belongs to the package whose root contains its file, and an import of
// a module of another package that is neither `Std` nor in the importer's
// `depends` closure is E1030 at the import. The check follows imports from the
// files it is given, through the project's packages, before the compiler runs.

#include "app/project.hpp"

#include "cli/module_path.hpp"
#include "project/files.hpp"

#include <algorithm>
#include <iostream>
#include <set>

namespace neura::app {
namespace {

using namespace project;

bool name_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool name_char(char c) { return name_start(c) || (c >= '0' && c <= '9'); }

struct Import {
    std::string module;
    Span span;
};

// The `import:Name.Name` forms of a source file, outside comments and
// string literals.
std::vector<Import> imports_of(const std::string& text, const std::string& file) {
    std::vector<Import> found;
    std::size_t line = 1, column = 1;
    const auto advance = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) {
            if (text[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
    };
    std::size_t i = 0;
    while (i < text.size()) {
        const auto start = i;
        if (text.compare(i, 2, "//") == 0) {
            const auto end = text.find('\n', i);
            i = end == std::string::npos ? text.size() : end;
        } else if (text.compare(i, 2, "/*") == 0) {
            const auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
        } else if (text[i] == '"') {
            ++i;
            while (i < text.size() && text[i] != '"' && text[i] != '\n') i += text[i] == '\\' ? 2 : 1;
            if (i < text.size()) ++i;
        } else if (text.compare(i, 7, "import:") == 0 && (i == 0 || !name_char(text[i - 1]))) {
            auto end = i + 7;
            while (end < text.size() && (name_char(text[end]) || (text[end] == '.' && end + 1 < text.size() &&
                                                                   name_start(text[end + 1]))))
                ++end;
            const auto module = text.substr(i + 7, end - i - 7);
            if (!module.empty() && module.front() >= 'A' && module.front() <= 'Z' && module.find('(') == std::string::npos)
                found.push_back({module, Span{file, line, column, i, end - i}});
            i = end;
        } else {
            ++i;
        }
        advance(start, i);
    }
    return found;
}

std::vector<std::string> closure_of(const Manifest& manifest, const std::string& package) {
    std::vector<std::string> order;
    std::vector<std::string> stack{package};
    while (!stack.empty()) {
        auto name = stack.back();
        stack.pop_back();
        if (std::ranges::find(order, name) != order.end()) continue;
        order.push_back(name);
        if (const auto found = manifest.packages.find(name); found != manifest.packages.end())
            for (const auto& dep : found->second.depends) stack.push_back(dep.value);
    }
    return order;
}

} // namespace

std::optional<std::string> package_of(const Project& project, const fs::path& file) {
    std::error_code error;
    const auto canonical = fs::weakly_canonical(file, error);
    std::optional<std::string> best;
    std::size_t depth = 0;
    for (const auto& [name, paths] : project.resolved.packages) {
        const auto relative = canonical.lexically_relative(paths.root.value);
        if (relative.empty() || *relative.begin() == "..") continue;
        const auto size = static_cast<std::size_t>(std::distance(paths.root.value.begin(), paths.root.value.end()));
        if (!best || size > depth) {
            best = name;
            depth = size;
        }
    }
    return best;
}

std::vector<fs::path> package_import_roots(const Project& project, const std::string& package) {
    std::vector<fs::path> roots;
    for (const auto& name : closure_of(project.manifest, package)) {
        const auto found = project.resolved.packages.find(name);
        if (found == project.resolved.packages.end()) continue;
        auto parent = found->second.root.value.parent_path();
        if (std::ranges::find(roots, parent) == roots.end()) roots.push_back(parent);
    }
    return roots;
}

std::vector<Diagnostic> check_closure(const Project& project, const std::vector<fs::path>& sources) {
    std::vector<Diagnostic> problems;
    std::set<fs::path> seen;
    std::vector<fs::path> work;
    for (const auto& source : sources) {
        std::error_code error;
        work.push_back(fs::weakly_canonical(source, error));
    }
    while (!work.empty()) {
        const auto file = work.back();
        work.pop_back();
        if (!seen.insert(file).second) continue;
        const auto importer = package_of(project, file);
        if (!importer) continue;
        const auto allowed = closure_of(project.manifest, *importer);
        std::string text;
        try {
            text = read_file(file);
        } catch (const std::exception&) {
            continue;
        }
        const auto shown = detail::path_text(file.lexically_relative(project.resolved.root));
        for (const auto& import : imports_of(text, shown)) {
            if (import.module == "Std" || import.module.starts_with("Std.")) continue;
            // The package whose root holds the module's file, as the
            // compiler's import roots (each root's parent) would find it.
            for (const auto& [name, paths] : project.resolved.packages) {
                const auto candidate = paths.root.value.parent_path() / module_path(import.module);
                const auto relative = candidate.lexically_relative(paths.root.value);
                std::error_code error;
                if (relative.empty() || *relative.begin() == ".." || !fs::is_regular_file(candidate, error)) continue;
                if (std::ranges::find(allowed, name) == allowed.end()) {
                    problems.push_back(
                        Diagnostic(import.span, "module '" + import.module + "' belongs to package '" + name +
                                                    "', which package '" + *importer + "' does not depend on")
                            .with_code("E1030")
                            .help("add \"" + name + "\" to [packages." + *importer +
                                  "] depends, or move the code to a package both depend on"));
                } else {
                    work.push_back(fs::weakly_canonical(candidate, error));
                }
                break;
            }
        }
    }
    return problems;
}

std::optional<std::vector<std::string>> project_check_arguments(const std::vector<std::string>& arguments) {
    std::vector<fs::path> sources;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        if (arguments[i] == "-I" || arguments[i] == "--project") return arguments;
        if (arguments[i].ends_with(".nra") && !arguments[i].starts_with('-')) sources.emplace_back(arguments[i]);
    }
    if (sources.empty()) return arguments;
    std::error_code error;
    auto directory = fs::weakly_canonical(fs::absolute(sources.front()), error).parent_path();
    std::optional<fs::path> manifest;
    for (auto at = directory; !at.empty(); at = at.parent_path()) {
        if (fs::is_regular_file(at / "neura.toml", error)) {
            manifest = at / "neura.toml";
            break;
        }
        if (at == at.root_path()) break;
    }
    if (!manifest) return arguments;
    Project project;
    try {
        project = load_project(*manifest);
    } catch (const std::exception&) {
        return arguments; // The manifest's own problems are reported by `neura build`.
    }
    const auto package = package_of(project, sources.front());
    if (!package) return arguments;
    if (auto problems = check_closure(project, sources); !problems.empty()) {
        for (const auto& problem : problems) print_diagnostic(std::cerr, problem);
        return std::nullopt;
    }
    auto result = arguments;
    std::vector<std::string> roots;
    for (const auto& root : package_import_roots(project, *package)) {
        roots.emplace_back("-I");
        roots.push_back(argument_path(root));
    }
    result.insert(result.begin() + 2, roots.begin(), roots.end());
    return result;
}

} // namespace neura::app
