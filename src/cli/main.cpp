// neura: the dispatcher of the Neura tools (docs/design/Products030.md).
//
// It answers --version, help and capabilities itself and starts the sibling
// tool of every other command from the directory of its own real
// executable, with an argument vector: never through a shell, never from
// the working directory or PATH. It links the tool registry and nothing of
// the compiler or the runtime, and uses no streams or std::filesystem, so it
// stays small when a portable release links the C++ runtime into it.

#include "tool.hpp"
#include "neura/version.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <filesystem>
#include <process.h>
#else
#include <climits>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

namespace tool = neura::tool;

void print(std::FILE* stream, std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stream);
}

int fail(const std::string& message) {
    print(stderr, "neura: error: " + message + "\n");
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
        print(stdout, tool::version_line() + "\n");
    } else {
        print(stdout, tool::usage_text());
    }
    return 0;
}

#ifdef _WIN32
std::string directory_of_self(const char* launch) {
    const auto path = tool::executable_path(launch);
    return path.empty() ? std::string() : path.parent_path().string();
}

bool present(const std::string& bin, std::string_view name) {
    std::error_code error;
    return !bin.empty() && std::filesystem::is_regular_file(std::filesystem::path(bin) / tool::executable_file(name), error);
}
#else
// The real path of this executable, as tool::executable_path resolves it:
// /proc/self/exe on Linux, the loader's path on macOS, else argv[0] and PATH.
std::string real_path(const std::string& path) {
    char buffer[PATH_MAX];
    return ::realpath(path.c_str(), buffer) ? std::string(buffer) : std::string();
}

bool regular_file(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

std::string directory_of_self(const char* launch) {
    std::string self;
#ifdef __linux__
    self = real_path("/proc/self/exe");
#endif
#ifdef __APPLE__
    if (self.empty()) {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) self = real_path(buffer.c_str());
    }
#endif
    const std::string_view name(launch ? launch : "");
    if (self.empty() && name.find('/') != std::string_view::npos) self = real_path(std::string(name));
    if (self.empty() && !name.empty()) {
        const char* path = std::getenv("PATH");
        std::string_view remaining(path ? path : "");
        while (path && self.empty()) {
            const auto end = remaining.find(':');
            const auto candidate = std::string(remaining.substr(0, end)) + "/" + std::string(name);
            if (regular_file(candidate)) self = real_path(candidate);
            if (end == std::string_view::npos) break;
            remaining.remove_prefix(end + 1);
        }
    }
    const auto slash = self.rfind('/');
    return slash == std::string::npos ? std::string() : self.substr(0, slash == 0 ? 1 : slash);
}

bool present(const std::string& bin, std::string_view name) {
    return !bin.empty() && regular_file(bin + "/" + tool::executable_file(name));
}
#endif

std::string join(const std::string& bin, std::string_view name) {
#ifdef _WIN32
    return (std::filesystem::path(bin) / tool::executable_file(name)).string();
#else
    return (bin == "/" ? bin : bin + "/") + tool::executable_file(name);
#endif
}

// The 0.2 capability object, as tooling::Json printed it: keys sorted, no
// spaces. A feature whose tool is not installed is false.
int capabilities(const std::string& bin, int argc, char** argv) {
    if (argc != 3 || std::string_view(argv[2]) != "--json") {
        print(stderr, "neura: usage: neura capabilities --json\n");
        return 1;
    }
    const auto flag = [](bool value) { return value ? "true" : "false"; };
    const bool test = present(bin, "neura-test");
    std::string text = "{\"features\":{";
    text += std::string("\"coverage\":") + flag(test) + ",\"debugger\":" + flag(present(bin, "neura-debug")) +
            ",\"documentation\":" + flag(present(bin, "neura-doc")) + ",\"formatter\":" + flag(present(bin, "neura-fmt")) +
            ",\"imageInspection\":" + flag(present(bin, "neura-inspect")) + ",\"lsp\":" + flag(present(bin, "neura-lsp")) +
            ",\"testCoverage\":" + flag(test) + ",\"testDiscovery\":" + flag(test) + ",\"testFilter\":" + flag(test) +
            ",\"testReporter\":" + flag(test);
    // The runtime service component (docs/design/Service030.md, "Editors"),
    // from the files of the installation only: nothing is connected to or
    // started. An installation that a 0.2 updater produced and that is not
    // migrated (a schema 1 marker and a core manifest) refuses every service
    // start, so the component is reported as unmigrated there.
    std::string service = "{\"installed\":false,\"state\":\"missing\"}";
#ifndef _WIN32
    if (present(bin, tool::service_executable)) {
        const auto parent = bin.substr(0, bin.rfind('/'));
        const auto versions = parent.substr(0, parent.rfind('/'));
        const auto root = versions.substr(0, versions.rfind('/'));
        bool unmigrated = false;
        if (versions.size() >= 9 && versions.compare(versions.size() - 9, 9, "/versions") == 0 &&
            regular_file(parent + "/share/neura/components/core.txt")) {
            if (std::FILE* marker = std::fopen((root + "/neura-install.txt").c_str(), "rb")) {
                char line[32] = {};
                unmigrated = std::fgets(line, sizeof line, marker) && std::string_view(line) == "neura-install 1\n";
                std::fclose(marker);
            }
        }
        service = std::string("{\"installed\":true,\"protocol\":\"1.0\",\"state\":\"") +
                  (unmigrated ? "unmigrated" : "ready") + "\"}";
    }
#endif
    // The version holds only letters, digits, '.' and '-', which JSON needs
    // no escapes for (CMakeLists.txt checks it).
    text += std::string("},\"schemaVersion\":1,\"service\":") + service + ",\"version\":\"" + neura::version::full +
            "\"}\n";
    print(stdout, text);
    return 0;
}

int forward(const std::string& program, const tool::Command& command, int argc, char** argv) {
    std::vector<std::string> arguments{program, tool::dispatch_token(), std::string(command.word)};
    for (int i = 2; i < argc; ++i) arguments.emplace_back(argv[i]);
    std::fflush(stdout);
    std::fflush(stderr);
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
    const auto status = _spawnv(_P_WAIT, program.c_str(), quoted_pointers.data());
    if (status != -1) return static_cast<int>(status);
#else
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    ::execv(program.c_str(), pointers.data());
#endif
    return fail("cannot run " + program + ": " + std::strerror(errno));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print(stderr, tool::usage_text());
        return 1;
    }
    const std::string_view name = argv[1];
    if (name == "--version" || name == "version" || name == "--help" || name == "help") return builtin(name, argc, argv);
    const auto bin = directory_of_self(argv[0]);
    if (name == "capabilities") return capabilities(bin, argc, argv);
    const auto* command = tool::find_command(name);
    if (!command) return fail(tool::command_list);
    const auto where = bin.empty() ? std::string("the directory of neura (its path could not be determined)") : bin;
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
    return forward(join(bin, command->tool), *command, argc, argv);
}
