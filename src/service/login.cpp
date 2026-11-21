// `neura runtime enable` and `disable` (docs/design/Service030.md, "Starting,
// retiring and login start"): the start of the supervisor at login, through
// the user's service manager. Linux uses `systemctl --user`; macOS uses
// `launchctl` and is written but untested. Windows has no user service
// manager: login start is a value of the user's Run key, which the shell
// runs at logon (W29).

#include "service.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include "cli/tool.hpp"
#include "platform/error.hpp"
#include "platform/process.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace neura::service {

#ifdef _WIN32

namespace {

using cli::RuntimeError;

constexpr const wchar_t* run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring value_name(const Installation& installation) { return platform::to_utf16("neurad-" + installation.id); }

// The command a login start runs: the active generation's neurad of an
// installation, or the source build's own, with the arguments quoted as
// CommandLineToArgvW reads them.
std::string start_command(const Installation& installation) {
    const auto neurad = installation.installed
                            ? installation.root / "current" / "bin" / tool::executable_file("neurad")
                            : installation.bin / tool::executable_file("neurad");
    return platform::command_line({platform::native_utf8_path(neurad), "--foreground", "--root",
                                   platform::native_utf8_path(installation.root)});
}

} // namespace

std::string login_location(const Installation& installation) {
    return "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\neurad-" + installation.id;
}

bool login_registered(const Installation& installation) {
    return ::RegGetValueW(HKEY_CURRENT_USER, run_key, value_name(installation).c_str(), RRF_RT_ANY, nullptr, nullptr,
                          nullptr) == ERROR_SUCCESS;
}

int login_command(const Installation& installation, bool enable, const std::vector<std::string>& arguments) {
    if (!arguments.empty()) {
        throw RuntimeError("unexpected argument for neura runtime " + std::string(enable ? "enable" : "disable") + ": " +
                               arguments.front(),
                           1);
    }
    const auto name = value_name(installation);
    if (!enable) {
        if (!login_registered(installation)) {
            std::cout << "login start is not registered\n";
            return 0;
        }
        const auto status = ::RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, name.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            throw RuntimeError("cannot remove " + login_location(installation) + ": " +
                                   platform::error_message(static_cast<int>(status)),
                               1);
        }
        // Nothing is stopped: a running supervisor stays until it exits.
        std::cout << "login start removed: " << login_location(installation) << '\n';
        return 0;
    }
    if (installation.unmigrated) {
        throw RuntimeError("finish the 0.3.0 install first: run \"neura update --migrate-install\"", status_not_run);
    }
    const auto command = platform::to_utf16(start_command(installation));
    const auto status = ::RegSetKeyValueW(HKEY_CURRENT_USER, run_key, name.c_str(), REG_SZ, command.c_str(),
                                          static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) {
        throw RuntimeError("cannot register " + login_location(installation) + ": " +
                               platform::error_message(static_cast<int>(status)) + "; nothing was changed",
                           1);
    }
    std::cout << "login start registered: " << login_location(installation) << '\n';
    return 0;
}

#else

namespace {

using cli::RuntimeError;

// A program on PATH, or nothing; never a shell.
std::optional<fs::path> on_path(std::string_view name) {
    const char* path = std::getenv("PATH");
    if (!path) return std::nullopt;
    std::string_view rest(path);
    while (true) {
        const auto colon = rest.find(':');
        const auto directory = rest.substr(0, colon);
        if (!directory.empty()) {
            const auto candidate = fs::path(std::string(directory)) / std::string(name);
            if (::access(candidate.c_str(), X_OK) == 0) return candidate;
        }
        if (colon == std::string_view::npos) break;
        rest.remove_prefix(colon + 1);
    }
    return std::nullopt;
}

// Runs a program with its output discarded and returns its exit status.
int run_program(const fs::path& program, const std::vector<std::string>& arguments) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", 0, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", 1, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", 1, 0);
    std::vector<std::string> owned{program.string()};
    owned.insert(owned.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : owned) argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), process_environment());
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) return 127;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

// The command a login start runs: the active generation's neurad of an
// installation, or the source build's own.
std::pair<fs::path, fs::path> start_command(const Installation& installation) {
    if (installation.installed) return {installation.root / "current/bin/neurad", installation.root};
    return {installation.bin / "neurad", installation.root};
}

#if !defined(__APPLE__)
// A systemd quoted word: backslash, quote and the specifier sign escaped.
std::string unit_word(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') out += '\\';
        if (c == '%') out += '%';
        out += c;
    }
    return out + "\"";
}

std::string unit_text(const Installation& installation) {
    const auto [neurad, root] = start_command(installation);
    std::ostringstream text;
    text << "# Written by neura runtime enable; removed by neura runtime disable.\n"
         << "[Unit]\n"
         << "Description=Neura runtime service of " << unit_word(root.string()) << "\n\n"
         << "[Service]\n"
         << "Type=simple\n"
         << "ExecStart=" << unit_word(neurad.string()) << " --foreground --root " << unit_word(root.string()) << "\n"
         << "Restart=no\n"
         << "KillMode=process\n\n"
         << "[Install]\n"
         << "WantedBy=default.target\n";
    return text.str();
}
#else
std::string plist_text(const Installation& installation) {
    const auto [neurad, root] = start_command(installation);
    const auto escape = [](const std::string& text) {
        std::string out;
        for (const char c : text) {
            if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else out += c;
        }
        return out;
    };
    std::ostringstream text;
    text << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         << "<plist version=\"1.0\">\n<dict>\n"
         << "  <key>Label</key><string>net.neura.neurad-" << installation.id << "</string>\n"
         << "  <key>ProgramArguments</key><array><string>" << escape(neurad.string())
         << "</string><string>--foreground</string><string>--root</string><string>" << escape(root.string())
         << "</string></array>\n"
         << "  <key>RunAtLoad</key><true/>\n  <key>KeepAlive</key><false/>\n"
         << "</dict>\n</plist>\n";
    return text.str();
}
#endif

} // namespace

fs::path login_unit_path(const Installation& installation) {
    const char* home = std::getenv("HOME");
    const fs::path base = home && *home == '/' ? fs::path(home) : fs::path("/");
#if defined(__APPLE__)
    return base / "Library/LaunchAgents" / ("net.neura.neurad-" + installation.id + ".plist");
#else
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const fs::path root = config && *config == '/' ? fs::path(config) : base / ".config";
    return root / "systemd/user" / ("neurad-" + installation.id + ".service");
#endif
}

int login_command(const Installation& installation, bool enable, const std::vector<std::string>& arguments) {
    if (!arguments.empty()) {
        throw RuntimeError("unexpected argument for neura runtime " + std::string(enable ? "enable" : "disable") + ": " +
                               arguments.front(),
                           1);
    }
    const auto unit = login_unit_path(installation);
    std::error_code error;
#if defined(__APPLE__)
    const auto manager = on_path("launchctl");
    const std::string domain = "gui/" + std::to_string(::getuid());
    const std::string label = "net.neura.neurad-" + installation.id;
#else
    const auto manager = on_path("systemctl");
    const std::string name = unit.filename().string();
#endif
    if (!enable) {
        if (!fs::exists(unit, error)) {
            std::cout << "login start is not registered\n";
            return 0;
        }
#if defined(__APPLE__)
        if (manager) run_program(*manager, {"bootout", domain + "/" + label});
#else
        if (manager) run_program(*manager, {"--user", "disable", name});
#endif
        fs::remove(unit, error);
#if !defined(__APPLE__)
        if (manager) run_program(*manager, {"--user", "daemon-reload"});
#endif
        // Nothing is stopped: a running supervisor stays until it exits.
        std::cout << "login start removed: " << unit.string() << '\n';
        return 0;
    }
    if (installation.unmigrated) {
        throw RuntimeError("finish the 0.3.0 install first: run \"neura update --migrate-install\"", status_not_run);
    }
#if defined(__APPLE__)
    const bool answers = manager && run_program(*manager, {"print", domain}) == 0;
#else
    const bool answers = manager && run_program(*manager, {"--user", "show-environment"}) == 0;
#endif
    if (!answers) {
        throw RuntimeError(
            "no user service manager answers here, so login start cannot be registered; nothing was changed. "
            "--runtime=auto and --runtime=required still start the service when a launch needs it",
            1);
    }
    fs::create_directories(unit.parent_path(), error);
    if (error) throw RuntimeError("cannot create " + unit.parent_path().string() + ": " + error.message(), 1);
#if defined(__APPLE__)
    write_atomically(unit, plist_text(installation));
    run_program(*manager, {"bootout", domain + "/" + label});
    const bool registered = run_program(*manager, {"bootstrap", domain, unit.string()}) == 0;
#else
    write_atomically(unit, unit_text(installation));
    run_program(*manager, {"--user", "daemon-reload"});
    const bool registered = run_program(*manager, {"--user", "enable", name}) == 0;
#endif
    if (!registered) {
        fs::remove(unit, error);
        throw RuntimeError("the service manager did not register " + unit.string() + "; nothing was changed", 1);
    }
    std::cout << "login start registered: " << unit.string() << '\n';
    return 0;
}

bool login_registered(const Installation& installation) {
    std::error_code error;
    return fs::exists(login_unit_path(installation), error);
}

#endif

} // namespace neura::service
