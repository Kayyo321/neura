// neura-new: `neura new <directory> --template <name>` (docs/design/Projects030.md,
// "Commands"). Writes a project from a template of the SDK, then its lock.
// No network; the output is a function of the template, the name, the id and
// the SDK version.

#include "app/project.hpp"
#include "cli/tool.hpp"
#include "project/files.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace {

namespace fs = std::filesystem;
using namespace neura;

constexpr std::string_view templates[] = {"fullstack", "web", "server", "desktop"};

constexpr std::string_view usage =
    "usage: neura new <directory> --template <fullstack|web|server|desktop> [--name <name>] [--id <id>]\n"
    "\n"
    "Writes a new project from a template of the SDK into <directory>, which must not exist or be empty,\n"
    "and writes its neura.lock. The project name is the directory's name unless --name; the id is\n"
    "example.<name> unless --id. Nothing is fetched.\n";

// The SDK's templates: share/neura/templates of the installation, or
// templates/ of a source tree whose build directory holds this tool.
std::optional<fs::path> template_root() {
    if (const char* env = std::getenv("NEURA_TEMPLATES"); env && *env) return fs::path(env);
    for (const auto& candidate : {app::install_prefix() / "share/neura/templates", app::install_prefix() / "templates"}) {
        std::error_code error;
        if (fs::is_directory(candidate / "fullstack", error)) return candidate;
    }
    return std::nullopt;
}

std::string substitute(std::string text, const std::string& name, const std::string& id) {
    for (const auto& [key, value] : {std::pair<std::string, std::string>{"{{name}}", name}, {"{{id}}", id}}) {
        for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size()))
            text.replace(at, key.size(), value);
    }
    return text;
}

int create(const std::vector<std::string>& arguments) {
    std::optional<std::string> directory, template_name, name, id;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= arguments.size()) {
                std::cerr << "neura new: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return arguments[++i];
        };
        if (arg == "--help" || arg == "-h") {
            std::cout << usage;
            return 0;
        } else if (arg == "--template") {
            template_name = value();
            if (!template_name) return 1;
        } else if (arg.starts_with("--template=")) {
            template_name = arg.substr(11);
        } else if (arg == "--name") {
            name = value();
            if (!name) return 1;
        } else if (arg == "--id") {
            id = value();
            if (!id) return 1;
        } else if (!arg.starts_with('-') && !directory) {
            directory = arg;
        } else {
            std::cerr << "neura new: unexpected argument " << arg << '\n' << usage;
            return 1;
        }
    }
    if (!directory || !template_name) {
        std::cerr << usage;
        return 1;
    }
    if (std::ranges::find(templates, *template_name) == std::end(templates)) {
        std::cerr << "neura new: unknown template '" << *template_name << "'; the templates are fullstack, web, server "
                  << "and desktop\n";
        return 1;
    }
    const fs::path target(*directory);
    std::error_code error;
    if (fs::exists(target, error)) {
        if (!fs::is_directory(target, error) || !fs::is_empty(target, error)) {
            std::cerr << "neura new: " << target.string() << " is not an empty directory; nothing was written\n";
            return 1;
        }
    }
    if (!name) name = fs::absolute(target).lexically_normal().filename().string();
    if (name->empty()) name = fs::absolute(target).lexically_normal().parent_path().filename().string();
    if (!project::valid_project_name(*name)) {
        std::cerr << "neura new: '" << *name << "' is not a project name (lowercase letters, digits and hyphens); "
                  << "give one with --name\n";
        return 1;
    }
    if (!id) id = "example." + *name;
    if (!project::valid_project_id(*id)) {
        std::cerr << "neura new: '" << *id << "' is not a lowercase reverse-domain id; give one with --id\n";
        return 1;
    }
    const auto root = template_root();
    if (!root) {
        std::cerr << "neura new: the project templates are not installed (share/neura/templates)\n";
        return 1;
    }
    const auto source = *root / *template_name;
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::ranges::sort(files);
    fs::create_directories(target);
    for (const auto& file : files) {
        auto relative = file.lexically_relative(source);
        if (relative == "gitignore") relative = ".gitignore";
        app::write_file(target / relative, substitute(app::read_file(file), *name, *id));
    }
    try {
        auto loaded = app::load_project(target / "neura.toml");
        app::check_lock(loaded, true);
    } catch (const Diagnostic& diagnostic) {
        app::print_diagnostic(std::cerr, diagnostic);
        return 1;
    }
    std::cout << "Created " << *template_name << " project '" << *name << "' in " << target.string() << ".\n"
              << "Build a target with `neura build --project " << (target / "neura.toml").string()
              << " --target <name>`, serve one with `neura dev`, and see docs/apps/FirstProject.md.\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-new"), argc, argv);
    if (start.status) return *start.status;
    try {
        return create(start.arguments);
    } catch (const std::exception& error) {
        std::cerr << "neura new: " << error.what() << '\n';
        return 1;
    }
}
