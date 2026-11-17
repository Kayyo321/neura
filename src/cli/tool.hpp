#pragma once

// The registry of the Neura tools and the rules that tie them together
// (docs/design/Products030.md): which executable serves each `neura`
// command, the component it belongs to, the dispatch token that `neura`
// passes to a tool, and the version and help of a tool run by itself. It
// depends on nothing but the generated version header, so the dispatcher
// links only this.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neura::tool {

enum class Component { Core, Sdk, Service };

// The documentation name of a component: "Neura core", "Neura-sdk", ...
std::string_view component_name(Component component);

// One `neura <command>`: the executable that serves it, the command word
// passed to that executable, and the component that ships it.
struct Command {
    std::string_view command;
    std::string_view tool;
    std::string_view word;
    Component component;
};

// One executable next to `neura`. Its first word is its default command.
struct Tool {
    std::string_view name;
    Component component;
    std::span<const std::string_view> words;
};

std::span<const Command> commands();
std::span<const Tool> tools();
const Command* find_command(std::string_view command);
const Tool* find_tool(std::string_view name);

// The service component's executable; `neura runtime` needs it.
inline constexpr std::string_view service_executable = "neurad";

// The 0.2 message for a missing or unknown command.
inline constexpr const char* command_list =
    "expected check, docs, emit-ir, build, inspect, run, test, disassemble, assemble, decompile, c-bindings, "
    "doctor, update, uninstall, explain, tags, version, or help";

// The line `neura --version` prints, without the newline.
std::string version_line();

// The usage text of `neura help`.
const char* usage_text();
void usage(std::ostream& out);

// The file name of an executable on this platform (".exe" on Windows).
std::string executable_file(std::string_view name);

// The real path of the running executable: /proc/self/exe on Linux, the
// loader's path on macOS, else `launch` resolved against PATH.
std::filesystem::path executable_path(const std::filesystem::path& launch);

// The argument `neura` puts first when it starts a tool.
inline constexpr std::string_view dispatch_prefix = "--neura-dispatch=";
std::string dispatch_token();

// A tool's start: strips the dispatch token, checks that it names this
// build, and answers `--help` and `--version` given alone. The result holds
// an exit status when the tool must stop, else the argument vector of the
// command, whose element 1 is always one of the tool's command words.
struct Start {
    std::optional<int> status;
    std::vector<std::string> arguments;
};
Start start(const Tool& tool, int argc, char** argv);

// The descriptor of the shared lease this process holds on its installed
// generation (docs/design/Lifecycle030.md, "Generations in use"), or -1.
int lease_descriptor();

// Pointers into `arguments` for code that takes argc and argv.
std::vector<char*> argv_of(std::vector<std::string>& arguments);

} // namespace neura::tool
