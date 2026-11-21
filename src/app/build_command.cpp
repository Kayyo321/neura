// `neura build --project <manifest> --target <name>` (docs/design/Projects030.md,
// "Commands"), served by neurac beside today's single-file build.

#include "app/project.hpp"

#include "cli/tool.hpp"
#include "project/files.hpp"

#include <algorithm>
#include <iostream>

namespace neura::app {

// A project build takes no input file (docs/design/Projects030.md, "Commands"). With an input file the
// command is the single-file build, where --target names a native launcher and needs --native.
bool is_project_build(const std::vector<std::string>& arguments) {
    bool project = false, target = false, native = false, input = false;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        if (arg == "--") {
            input |= i + 1 < arguments.size();
            break;
        }
        project |= arg == "--project" || arg.starts_with("--project=");
        target |= arg == "--target" || arg.starts_with("--target=");
        native |= arg == "--native";
        static constexpr std::string_view takes_value[] = {
            "--project", "--target", "--out", "--web-runtime", "-o", "-I", "--launcher", "--max-errors",
            "--max-tasks", "--task-quantum", "--max-queued-messages", "--max-pending-requests", "--parallel-workers"};
        if (std::ranges::find(takes_value, std::string_view(arg)) != std::end(takes_value)) {
            ++i;
        } else if (!arg.empty() && arg[0] != '-') {
            input = true;
        }
    }
    return project || (target && !native && !input);
}

namespace {

constexpr std::string_view usage =
    "usage: neura build --project <manifest> --target <name> [--out <directory>] [--update-lock]\n"
    "                   [--no-optimize] [--no-lift-hints] [--web-runtime <directory>]\n"
    "\n"
    "Builds the target's bundle into dist/<name> of the project (or --out). Without --project the manifest is\n"
    "neura.toml of the current directory. A desktop target builds its web target first and embeds it.\n";

} // namespace

int project_build_main(const std::vector<std::string>& arguments) {
    std::optional<std::string> project;
    BuildOptions options;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        const auto value = [&](std::string_view name) -> std::optional<std::string> {
            if (arg == name) {
                if (i + 1 >= arguments.size()) {
                    std::cerr << "neura build: " << name << " needs a value\n";
                    return std::nullopt;
                }
                return arguments[++i];
            }
            return arg.substr(name.size() + 1);
        };
        const auto is = [&](std::string_view name) {
            return arg == name || (arg.size() > name.size() && arg.starts_with(name) && arg[name.size()] == '=');
        };
        if (arg == "--help" || arg == "-h") {
            std::cout << usage;
            return 0;
        } else if (is("--project")) {
            project = value("--project");
            if (!project) return 1;
        } else if (is("--target")) {
            const auto v = value("--target");
            if (!v) return 1;
            options.target = *v;
        } else if (is("--out")) {
            const auto v = value("--out");
            if (!v) return 1;
            options.out = *v;
        } else if (is("--web-runtime")) {
            const auto v = value("--web-runtime");
            if (!v) return 1;
            options.web_runtime = *v;
        } else if (arg == "--update-lock") {
            options.update_lock = true;
        } else if (arg == "--source-map") {
            options.source_map = true;
        } else if (arg == "--no-optimize") {
            options.no_optimize = true;
        } else if (arg == "--no-lift-hints") {
            options.no_lift_hints = true;
        } else {
            std::cerr << "neura build: unknown option for a project build: " << arg << '\n' << usage;
            return 1;
        }
    }
    if (options.target.empty()) {
        std::cerr << "neura build: --target <name> is required with --project\n" << usage;
        return 1;
    }
    const auto manifest = find_manifest(project, std::cerr);
    if (!manifest) return 1;
    try {
        options.neura = tool_directory() / tool::executable_file("neura");
        auto loaded = load_project(*manifest);
        check_target(loaded, options.target);
        check_lock(loaded, options.update_lock);
        const auto result = build_target(loaded, options);
        std::cout << display_path(result.directory) << ": bundle " << result.digest.hex() << '\n';
        if (fs::exists(result.directory / "404.html")) {
            // Case 12: history routing needs the static host's help.
            std::cout << "history routing: the host must answer every application path with index.html, or serve "
                         "404.html (a copy of it) for unknown paths\n";
        }
        return 0;
    } catch (const Diagnostic& diagnostic) {
        print_diagnostic(std::cerr, diagnostic);
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "neura build: " << error.what() << '\n';
        return 1;
    }
}

} // namespace neura::app
