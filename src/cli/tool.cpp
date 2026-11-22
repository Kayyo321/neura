#include "tool.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <system_error>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include "platform/console.hpp"
#include "platform/self.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"
#else
#include <unistd.h>
#endif

namespace neura::tool {

namespace fs = std::filesystem;

namespace {

LeaseHandle lease = no_lease;

// A path as a message shows it.
std::string path_text(const fs::path& path) {
#ifdef _WIN32
    return platform::native_utf8_path(path);
#else
    return path.string();
#endif
}

#ifdef _WIN32
// Ends the process with the status a POSIX shell reports for the signal that
// the console event stands for (W10), as that signal's default action
// would: at once, without flushing streams another thread may hold.
bool end_on_console_event(platform::ConsoleEvent event) {
    switch (event) {
    case platform::ConsoleEvent::ctrl_c: std::_Exit(130);
    case platform::ConsoleEvent::ctrl_break: std::_Exit(131);
    default: std::_Exit(129);
    }
}

// Windows 10 version 2004 (build 19041) or later (W03). RtlGetVersion
// reports the real version whatever the manifest declares.
bool supported_windows() {
    using GetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    const auto symbol = ntdll ? reinterpret_cast<void (*)()>(::GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    const auto get_version = reinterpret_cast<GetVersion>(symbol);
    if (get_version == nullptr) return true;
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof info;
    if (get_version(&info) != 0) return true;
    return info.dwMajorVersion > 10 || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 19041);
}

// What every tool sets before it reads or writes anything (W10, W17).
void set_up_windows_process() {
    platform::set_unattended_error_mode();
    platform::set_binary_stdio();
    platform::install_console_streams();
    // DLLs come from the executable's directory and System32, never from
    // the working directory or PATH.
    ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    // A handler that the tool installs later takes the events before this
    // one (on_console_event replaces it; the C runtime's for signal(SIGINT)
    // runs first).
    platform::on_console_event(end_on_console_event);
}
#endif

void tool_help(const Tool& tool, std::ostream& out) {
    out << tool.name << ": the direct form of";
    bool first = true;
    for (const auto& command : commands()) {
        if (command.tool != tool.name || command.command.starts_with("-")) continue;
        out << (first ? " " : ", ") << "neura " << command.command;
        first = false;
    }
    out << " (" << component_name(tool.component) << ")\n";
    for (std::size_t i = 0; i < tool.words.size(); ++i) {
        out << (i == 0 ? "usage: " : "       ") << tool.name << ' ' << (i == 0 ? "[" : "") << tool.words[i]
            << (i == 0 ? "]" : "") << " ...\n";
    }
    if (!tool.words.empty())
        out << "The first argument selects one of these commands; without one, the tool runs " << tool.words.front()
            << ".\n";
    out << "`neura help` lists the options of every command, which mean the same here.\n";
}

} // namespace

void usage(std::ostream& out) {
    out << usage_text();
}

fs::path executable_path(const fs::path& launch) {
    std::error_code error;
#ifdef _WIN32
    if (auto actual = platform::executable_path(error); !actual.empty()) return actual;
#endif
#ifdef __linux__
    if (const auto actual = fs::canonical("/proc/self/exe", error); !actual.empty()) return actual;
#endif
#ifdef __APPLE__
    {
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
            buffer.resize(std::strlen(buffer.c_str()));
            if (const auto actual = fs::canonical(buffer, error); !actual.empty()) return actual;
        }
    }
#endif
    if (launch.has_parent_path()) return fs::canonical(launch, error);
    const auto* path = std::getenv("PATH");
    if (!path) return {};
#ifdef _WIN32
    constexpr char separator = ';';
    auto file = launch;
    if (!file.has_extension()) file += ".exe";
#else
    constexpr char separator = ':';
    const auto& file = launch;
#endif
    std::string_view remaining(path);
    for (;;) {
        const auto end = remaining.find(separator);
        const auto candidate = fs::path(remaining.substr(0, end)) / file;
        if (fs::is_regular_file(candidate, error)) return fs::canonical(candidate, error);
        if (end == std::string_view::npos) return {};
        remaining.remove_prefix(end + 1);
    }
}

Start start(const Tool& tool, int argc, char** argv) {
    Start result;
#ifdef _WIN32
    set_up_windows_process();
    if (!supported_windows()) {
        std::cerr << "neura: Neura needs Windows 10 version 2004 or later\n";
        result.status = 1;
        return result;
    }
#endif
    // The lease comes first: before the tool opens any other file of its
    // generation (docs/design/Lifecycle030.md).
#ifdef __linux__
    {
        char self[4096];
        const auto length = ::readlink("/proc/self/exe", self, sizeof self - 1);
        if (length > 0) {
            self[length] = '\0';
            bool removed = false;
            lease = take_lease(self, removed);
            if (removed) {
                std::cerr << removed_generation_message;
                result.status = 1;
                return result;
            }
        }
    }
#elif defined(_WIN32)
    {
        // The module's own path: argv[0] is whatever the parent passed.
        std::error_code error;
        const auto self = platform::executable_path(error);
        bool removed = false;
        if (!self.empty()) lease = take_lease(self, removed);
        if (removed) {
            std::cerr << removed_generation_message;
            result.status = 1;
            return result;
        }
    }
#else
    {
        const auto self = executable_path(argc > 0 ? argv[0] : "");
        bool removed = false;
        lease = take_lease(self.c_str(), removed);
        if (removed) {
            std::cerr << removed_generation_message;
            result.status = 1;
            return result;
        }
    }
#endif
    int first = 1;
    if (argc > 1 && std::string_view(argv[1]).starts_with(dispatch_prefix)) {
        const auto claimed = std::string_view(argv[1]).substr(dispatch_prefix.size());
        const auto own = version_line();
        if (claimed != own) {
            const auto self = executable_path(argv[0]);
            std::cerr << "neura: error: " << (self.empty() ? std::string(tool.name) : path_text(self)) << " is from "
                      << own << ", but the neura that started it is " << claimed << "; every tool in "
                      << (self.empty() ? std::string("its directory") : path_text(self.parent_path()))
                      << " must come from one build. Reinstall Neura\n";
            result.status = 1;
            return result;
        }
        first = 2;
    } else if (argc == 2 && std::string_view(argv[1]) == "--help") {
        tool_help(tool, std::cout);
        result.status = 0;
        return result;
    } else if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << version_line() << '\n';
        result.status = 0;
        return result;
    }
    result.arguments.emplace_back(argc > 0 ? argv[0] : tool.name);
    bool named = false;
    if (first < argc) {
        for (const auto& word : tool.words) named = named || word == argv[first];
    }
    if (!named && !tool.words.empty()) result.arguments.emplace_back(tool.words.front());
    for (int i = first; i < argc; ++i) result.arguments.emplace_back(argv[i]);
    return result;
}

LeaseHandle lease_descriptor() { return lease; }

std::vector<char*> argv_of(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    for (auto& argument : arguments) pointers.push_back(argument.data());
    pointers.push_back(nullptr);
    return pointers;
}

} // namespace neura::tool
