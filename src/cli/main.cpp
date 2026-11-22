// neura: the dispatcher of the Neura tools (docs/design/Products030.md).
//
// It answers --version, help and capabilities itself and starts the sibling
// tool of every other command from the directory of its own real
// executable, with an argument vector: never through a shell, never from
// the working directory or PATH. It links the tool registry and nothing of
// the compiler or the runtime, and uses no streams or std::filesystem, so it
// stays small when a portable release links the C++ runtime into it. On
// Windows, which has no exec, it starts the tool with the platform layer's
// process code and std::filesystem paths, waits, and returns its status.

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
#include "platform/console.hpp"
#include "platform/error.hpp"
#include "platform/process.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <filesystem>
#include <io.h>
#include <optional>
#else
#include <climits>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

namespace tool = neura::tool;

void print(std::FILE* stream, std::string_view text) {
#ifdef _WIN32
    // A console shows UTF-8 text correctly only through WriteConsoleW, and
    // its code page is left as the user set it (W17). Pipes and files get
    // the bytes.
    const int descriptor = ::_fileno(stream);
    if (neura::platform::is_terminal(descriptor)) {
        if (const auto wide = neura::platform::try_to_utf16(text)) {
            std::fflush(stream);
            DWORD written = 0;
            if (::WriteConsoleW(reinterpret_cast<HANDLE>(::_get_osfhandle(descriptor)), wide->data(),
                                static_cast<DWORD>(wide->size()), &written, nullptr)) {
                return;
            }
        }
    }
#endif
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
namespace fs = std::filesystem;
namespace platform = neura::platform;

// The real path of this executable, kept for its lease.
fs::path self_path;

// The directory as UTF-8 in native form, which messages show; the other
// functions turn it back into a path.
std::string directory_of_self(const char* launch) {
    self_path = tool::executable_path(launch ? launch : "");
    return self_path.empty() ? std::string() : platform::native_utf8_path(self_path.parent_path());
}

fs::path in_bin(const std::string& bin, std::string_view name) {
    return platform::path_from_utf8(bin) / tool::executable_file(name);
}

bool regular_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

bool present(const std::string& bin, std::string_view name) {
    return !bin.empty() && regular_file(in_bin(bin, name));
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

// The real path of this executable, kept for its lease.
std::string self_path;

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
    self_path = self;
    const auto slash = self.rfind('/');
    return slash == std::string::npos ? std::string() : self.substr(0, slash == 0 ? 1 : slash);
}

bool present(const std::string& bin, std::string_view name) {
    return !bin.empty() && regular_file(bin + "/" + tool::executable_file(name));
}
#endif

#ifdef _WIN32
// The text of `path`, at most about 4 MiB of it; nullopt when it cannot be
// opened.
std::optional<std::string> read_text(const fs::path& path) {
    std::FILE* stream = ::_wfopen(path.c_str(), L"rb");
    if (stream == nullptr) return std::nullopt;
    std::string text;
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1, sizeof buffer, stream)) > 0 && text.size() < (std::size_t{4} << 20)) text.append(buffer, count);
    std::fclose(stream);
    return text;
}

// As the POSIX functions below, with std::filesystem paths: whether the
// generation of `bin` is an unmigrated one that a 0.2 updater placed.
bool unmigrated_generation(const std::string& bin) {
    const auto generation = platform::path_from_utf8(bin).parent_path();
    const auto versions = generation.parent_path();
    if (versions.filename() != L"versions" || !regular_file(generation / L"share/neura/components/core.txt")) return false;
    const auto marker = read_text(versions.parent_path() / L"neura-install.txt");
    return marker && marker->starts_with("neura-install 1\n");
}

// Whether the generation of `bin` has component manifests; `declared` is set
// when one of them has the line `tool <name>`.
bool manifests_declare(const std::string& bin, std::string_view name, bool& declared) {
    declared = false;
    const auto directory = platform::path_from_utf8(bin).parent_path() / L"share/neura/components";
    std::error_code error;
    fs::directory_iterator listing(directory, error);
    if (error) return false;
    const std::string wanted = "tool " + std::string(name) + "\n";
    bool any = false;
    for (; listing != fs::directory_iterator(); listing.increment(error)) {
        const auto file = listing->path().filename().native();
        if (file.size() < 5 || !file.ends_with(L".txt")) continue;
        const auto text = read_text(listing->path());
        if (!text) continue;
        any = true;
        if (text->find(wanted) != std::string::npos) declared = true;
    }
    return any;
}
#else
// Whether the generation of `bin` is one that a 0.2 updater placed and that
// is not migrated: a schema 1 marker at the root and a core component
// manifest in the generation (docs/Install.md, "From 0.2"), the lifecycle's
// own test, read from the files alone. Every service start is refused there.
bool unmigrated_generation(const std::string& bin) {
    const auto parent = bin.substr(0, bin.rfind('/'));
    const auto versions = parent.substr(0, parent.rfind('/'));
    const auto root = versions.substr(0, versions.rfind('/'));
    if (versions.size() < 9 || versions.compare(versions.size() - 9, 9, "/versions") != 0 ||
        !regular_file(parent + "/share/neura/components/core.txt")) {
        return false;
    }
    bool unmigrated = false;
    if (std::FILE* marker = std::fopen((root + "/neura-install.txt").c_str(), "rb")) {
        char line[32] = {};
        unmigrated = std::fgets(line, sizeof line, marker) && std::string_view(line) == "neura-install 1\n";
        std::fclose(marker);
    }
    return unmigrated;
}

// The component manifests of this generation (docs/design/Lifecycle030.md),
// read only when a tool is missing, so that a run pays nothing for them.
// Returns whether the generation has manifests; `declared` is set when one
// of them has the line `tool <name>`.
bool manifests_declare(const std::string& bin, std::string_view name, bool& declared) {
    declared = false;
    const auto slash = bin.rfind('/');
    if (slash == std::string::npos) return false;
    const std::string directory = bin.substr(0, slash) + "/share/neura/components";
    DIR* listing = ::opendir(directory.c_str());
    if (listing == nullptr) return false;
    const std::string wanted = "tool " + std::string(name) + "\n";
    bool any = false;
    while (const dirent* entry = ::readdir(listing)) {
        const std::string_view file = entry->d_name;
        if (file.size() < 5 || !file.ends_with(".txt")) continue;
        std::FILE* stream = std::fopen((directory + "/" + std::string(file)).c_str(), "rb");
        if (stream == nullptr) continue;
        any = true;
        std::string text;
        char buffer[4096];
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof buffer, stream)) > 0 && text.size() < (std::size_t{4} << 20)) text.append(buffer, count);
        std::fclose(stream);
        if (text.find(wanted) != std::string::npos) declared = true;
    }
    ::closedir(listing);
    return any;
}
#endif

std::string join(const std::string& bin, std::string_view name) {
#ifdef _WIN32
    return platform::native_utf8_path(in_bin(bin, name));
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
    if (present(bin, tool::service_executable)) {
        service = std::string("{\"installed\":true,\"protocol\":\"1.0\",\"state\":\"") +
                  (unmigrated_generation(bin) ? "unmigrated" : "ready") + "\"}";
    }
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
    // Windows has no exec: start the tool, wait, and return its exit code
    // unchanged (W10). The tool runs in a job that ends with this process,
    // so a client that kills `neura lsp` ends neura-lsp too. Console events
    // reach both processes; this one ignores them and lets the tool decide.
    platform::Spawn spawn;
    spawn.program = platform::path_from_utf8(program);
    spawn.arguments = std::move(arguments);
    spawn.job = platform::JobMode::kill_on_close;
    platform::ignore_console_events();
    std::error_code error;
    auto child = platform::Process::start(spawn, error);
    if (!child) return fail("cannot run " + program + ": " + platform::error_message(error));
    const auto status = child.wait(std::nullopt, error);
    if (!status) return fail("cannot wait for " + program + ": " + platform::error_message(error));
    return status->shell_status();
#else
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    ::execv(program.c_str(), pointers.data());
    return fail("cannot run " + program + ": " + std::strerror(errno));
#endif
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // As tool::start sets up every tool: no error dialogs, and "\n" written
    // as one byte (W10, W17).
    neura::platform::set_unattended_error_mode();
    neura::platform::set_binary_stdio();
#endif
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
    // The dispatcher holds the lease of its generation until it becomes the
    // tool, which takes its own (close-on-exec). On Windows it holds it while
    // the tool runs; the tool does not inherit it.
    {
        bool removed = false;
#ifdef _WIN32
        if (!self_path.empty()) tool::take_lease(self_path, removed);
#else
        tool::take_lease(self_path.c_str(), removed);
#endif
        if (removed) {
            print(stderr, tool::removed_generation_message);
            return 1;
        }
    }
    const auto where = bin.empty() ? std::string("the directory of neura (its path could not be determined)") : bin;
    if (command->component == tool::Component::Service && !present(bin, tool::service_executable)) {
        // An installation that a 0.2 updater produced has no neurad (the
        // legacy archive holds none), and every service start there is
        // refused with the migration instruction first, as `run --runtime`
        // refuses (Service030.md case 60, Lifecycle030.md).
        if (argc > 2 && (std::string_view(argv[2]) == "start" || std::string_view(argv[2]) == "enable") &&
            unmigrated_generation(bin)) {
            print(stderr, "neura: runtime: finish the 0.3.0 install first: run \"neura update --migrate-install\"\n");
            return 5;
        }
        return fail("'neura " + std::string(name) + "' needs the Neura-runtime service component (" +
                    std::string(tool::service_executable) + "), which is not in " + where +
                    ". 'neura run' runs images directly without it");
    }
    if (!present(bin, command->tool)) {
        // In an installed generation the manifests say which components are
        // there: a tool that none declares belongs to a product that is not
        // installed here.
        bool declared = false;
        if (manifests_declare(bin, command->tool, declared) && !declared && command->component == tool::Component::Sdk) {
            print(stderr, "neura: \"" + std::string(name) + "\" needs Neura-sdk, which is not installed here\n");
            return 1;
        }
        const auto* entry = tool::find_tool(command->tool);
        const bool core = entry && entry->component == tool::Component::Core;
        return fail("'neura " + std::string(name) + "' needs " + std::string(command->tool) + ", which is not in " + where +
                    (core ? "; it is part of the Neura core, so this installation is incomplete. Reinstall Neura"
                          : "; it is part of the Neura-sdk component. Install the SDK, or reinstall Neura"));
    }
    return forward(join(bin, command->tool), *command, argc, argv);
}
