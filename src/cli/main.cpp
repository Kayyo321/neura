// neura: the dispatcher of the Neura tools (docs/design/Products030.md).
//
// It answers --version, help and capabilities itself and starts the sibling
// tool of every other command from the directory of its own real
// executable, with an argument vector: never through a shell, never from
// the working directory or PATH. It links the tool registry and nothing of
// the compiler or the runtime.

#include "tool.hpp"
#include "tooling/json.hpp"
#include "neura/version.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
namespace tool = neura::tool;

int fail(const std::string& message) {
    std::cerr << "neura: error: " << message << '\n';
    return 1;
}

// --version, version, --help and help take no arguments, as in 0.2.
int builtin(std::string_view name, int argc, char** argv) {
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.starts_with('-')) return fail("unknown option for " + std::string(name) + ": " + std::string(arg));
        return fail(std::string(name) + " takes no files");
    }
    if (name == "--version" || name == "version") {
        std::cout << tool::version_line() << '\n';
    } else {
        tool::usage(std::cout);
    }
    return 0;
}

bool present(const fs::path& bin, std::string_view name) {
    std::error_code error;
    return !bin.empty() && fs::is_regular_file(bin / tool::executable_file(name), error);
}

// The 0.2 capability object. A feature whose tool is not installed is false.
int capabilities(const fs::path& bin, int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[2]) != "--json") {
        std::cerr << "neura: usage: neura capabilities --json\n";
        return 1;
    }
    const bool lsp = present(bin, "neura-lsp");
    const bool fmt = present(bin, "neura-fmt");
    const bool debug = present(bin, "neura-debug");
    const bool test = present(bin, "neura-test");
    const bool docs = present(bin, "neura-doc");
    const bool inspect = present(bin, "neura-inspect");
    const neura::tooling::Json info = neura::tooling::Json::Object{
        {"schemaVersion", 1}, {"version", neura::version::full},
        {"features", neura::tooling::Json::Object{{"lsp", lsp}, {"formatter", fmt}, {"debugger", debug},
            {"testDiscovery", test}, {"testFilter", test}, {"testReporter", test}, {"coverage", test}, {"testCoverage", test}, {"documentation", docs}, {"imageInspection", inspect}}}
    };
    std::cout << info.dump() << '\n';
    return 0;
}

int forward(const fs::path& program, const tool::Command& command, int argc, char** argv) {
    std::vector<std::string> arguments{program.string(), tool::dispatch_token(), std::string(command.word)};
    for (int i = 2; i < argc; ++i) arguments.emplace_back(argv[i]);
    std::cout.flush();
    std::cerr.flush();
#ifdef _WIN32
    // Windows has no exec: start the tool, wait, and return its status.
    // _spawnv joins the arguments into one command line, so each is quoted.
    std::vector<std::string> quoted;
    for (const auto& argument : arguments) {
        std::string text = "\"";
        std::size_t slashes = 0;
        for (const char c : argument) {
            if (c == '\\') { ++slashes; continue; }
            text.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
            slashes = 0;
            text += c;
        }
        text.append(slashes * 2, '\\');
        quoted.push_back(text + "\"");
    }
    auto quoted_pointers = tool::argv_of(quoted);
    const auto status = _spawnv(_P_WAIT, program.string().c_str(), quoted_pointers.data());
    if (status != -1) return static_cast<int>(status);
#else
    auto pointers = tool::argv_of(arguments);
    ::execv(program.c_str(), pointers.data());
#endif
    return fail("cannot run " + program.string() + ": " + std::strerror(errno));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        tool::usage(std::cerr);
        return 1;
    }
    const std::string_view name = argv[1];
    if (name == "--version" || name == "version" || name == "--help" || name == "help") return builtin(name, argc, argv);
    const auto self = tool::executable_path(argv[0]);
    const auto bin = self.empty() ? fs::path() : self.parent_path();
    if (name == "capabilities") return capabilities(bin, argc, argv);
    const auto* command = tool::find_command(name);
    if (!command) return fail(tool::command_list);
    const auto where = bin.empty() ? std::string("the directory of neura (its path could not be determined)") : bin.string();
    if (command->component == tool::Component::Service && !present(bin, tool::service_executable)) {
        return fail("'neura " + std::string(name) + "' needs the Neura-runtime service component (" +
                    std::string(tool::service_executable) + "), which is not in " + where +
                    ". 'neura run' runs images directly without it");
    }
    if (!present(bin, command->tool)) {
        const auto* entry = tool::find_tool(command->tool);
        const bool core = entry && entry->component == tool::Component::Core;
        return fail("'neura " + std::string(name) + "' needs " + std::string(command->tool) + ", which is not in " + where +
                    (core ? "; it is part of the Neura core, so this installation is incomplete. Reinstall Neura"
                          : "; it is part of the Neura-sdk component. Install the SDK, or reinstall Neura"));
    }
    return forward(bin / tool::executable_file(command->tool), *command, argc, argv);
}
