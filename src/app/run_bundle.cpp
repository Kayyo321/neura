// `neura run <bundle directory> [-- arguments]` (docs/design/Projects030.md,
// "Commands"): the bundle is verified before anything runs; a server bundle
// then runs under neura-server-host and a desktop bundle under the desktop
// host. A web bundle is not runnable.

#include "app/project.hpp"

#include "cli/tool.hpp"
#include "project/files.hpp"
#include "tooling/json.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include "platform/console.hpp"
#include "platform/environment.hpp"
#include "platform/error.hpp"
#include "platform/process.hpp"
#include "platform/text.hpp"
#else
#include <unistd.h>
#endif

namespace neura::app {
namespace {

using namespace project;
using tooling::Json;

#ifdef _WIN32
// The first file `name` names on PATH: entries are separated by ';' and may
// be quoted, and a name without an extension is tried with each extension of
// PATHEXT, as cmd.exe finds a command.
std::optional<fs::path> on_path(const std::string& name) {
    std::error_code error;
    const auto path = platform::get_env("PATH", error);
    if (!path) return std::nullopt;
    std::vector<std::string> extensions{""};
    if (fs::path(name).extension().empty()) {
        extensions.clear();
        const auto pathext = platform::get_env("PATHEXT", error).value_or(".COM;.EXE;.BAT;.CMD");
        for (std::string_view rest(pathext); !rest.empty();) {
            const auto semicolon = rest.find(';');
            if (const auto extension = rest.substr(0, semicolon); !extension.empty()) extensions.emplace_back(extension);
            if (semicolon == std::string_view::npos) break;
            rest.remove_prefix(semicolon + 1);
        }
    }
    for (std::string_view rest(*path); !rest.empty();) {
        const auto semicolon = rest.find(';');
        auto entry = rest.substr(0, semicolon);
        if (entry.size() >= 2 && entry.front() == '"' && entry.back() == '"') entry = entry.substr(1, entry.size() - 2);
        if (!entry.empty()) {
            try {
                const auto directory = platform::path_from_utf8(entry);
                for (const auto& extension : extensions) {
                    const auto candidate = directory / platform::path_from_utf8(name + extension);
                    if (fs::is_regular_file(candidate, error)) return candidate;
                }
            } catch (const std::system_error&) {
                // An entry that is not valid text names nothing.
            }
        }
        if (semicolon == std::string_view::npos) break;
        rest.remove_prefix(semicolon + 1);
    }
    return std::nullopt;
}
#else
std::optional<fs::path> on_path(const std::string& name) {
    if (const char* path = std::getenv("PATH")) {
        std::string_view rest(path);
        while (!rest.empty()) {
            const auto colon = rest.find(':');
            const auto entry = rest.substr(0, colon);
            if (!entry.empty()) {
                const auto candidate = fs::path(entry) / name;
                if (::access(candidate.c_str(), X_OK) == 0) return candidate;
            }
            if (colon == std::string_view::npos) break;
            rest.remove_prefix(colon + 1);
        }
    }
    return std::nullopt;
}
#endif

int exec(std::vector<std::string> arguments, std::ostream& err) {
#ifdef _WIN32
    // Windows has no exec: the host runs as a child in a kill-on-close job,
    // so it ends with this process, and its exit status is returned. Console
    // events reach both processes; this one ignores them and lets the host
    // decide (W10, W32).
    std::cout.flush();
    std::cerr.flush();
    platform::Spawn spawn;
    try {
        spawn.program = platform::path_from_utf8(arguments.front());
    } catch (const std::system_error& invalid) {
        err << "neura run: cannot run " << arguments.front() << ": " << platform::error_message(invalid.code()) << '\n';
        return 1;
    }
    spawn.arguments = std::move(arguments);
    spawn.job = platform::JobMode::kill_on_close;
    platform::ignore_console_events();
    std::error_code error;
    auto host = platform::Process::start(spawn, error);
    if (!host) {
        err << "neura run: cannot run " << spawn.arguments.front() << ": " << platform::error_message(error) << '\n';
        return 1;
    }
    const auto status = host.wait(std::nullopt, error);
    if (!status) {
        err << "neura run: cannot wait for " << spawn.arguments.front() << ": " << platform::error_message(error) << '\n';
        return 1;
    }
    return status->shell_status();
#else
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::cout.flush();
    // The host replaces this process, so signals reach it directly.
    ::execv(arguments.front().c_str(), argv.data());
    err << "neura run: cannot run " << arguments.front() << ": " << std::strerror(errno) << '\n';
    return 1;
#endif
}

std::optional<std::vector<std::string>> server_command(const VerifiedBundle& bundle, const fs::path& root,
                                                      const std::vector<std::string>& arguments, std::ostream& err) {
    auto host = tool_directory() / tool::executable_file("neura-server-host");
    if (!fs::exists(host)) {
        const auto found = on_path("neura-server-host");
        if (!found) {
            err << "neura run: neura-server-host was found neither beside " << display_path(tool_directory())
                << " nor on PATH\n";
            return std::nullopt;
        }
        host = *found;
    }
    const auto settings_bytes = bundle.read_file("server.json");
    const auto settings = Json::parse(std::string_view(reinterpret_cast<const char*>(settings_bytes.data()), settings_bytes.size()));
    std::vector<std::string> command{argument_path(host)};
    bool listens = false;
    for (const auto& argument : arguments) {
        if (argument == "--listen" || argument.starts_with("--listen=") || argument == "--tls-listen" ||
            argument.starts_with("--tls-listen=") || argument == "--no-http")
            listens = true;
    }
    if (!listens) {
        command.emplace_back("--listen");
        command.push_back(settings.contains("listen") ? settings["listen"].string() : "127.0.0.1:8080");
    }
    if (settings.contains("config_env_prefix")) {
        command.emplace_back("--config-env-prefix");
        command.push_back(settings["config_env_prefix"].string());
    }
    for (const auto& [pool, entry] : settings["sql"].fields()) {
        if (entry.contains("migrations")) {
            // The pool's URL comes from the operator (--sql-pool, a config file).
            command.emplace_back("--config");
            command.push_back("sql." + pool + ".migrations=" +
                              argument_path(root / detail::path_from_text(entry["migrations"].string())));
        }
        if (entry.contains("migrate")) {
            command.emplace_back("--config");
            command.push_back("sql." + pool + ".migrate=" + entry["migrate"].string());
        }
    }
    for (const auto capability : bundle.manifest().capabilities) {
        switch (capability) {
        case Capability::Fs: command.emplace_back("--allow-fs"); break;
        case Capability::Net: command.emplace_back("--allow-net"); break;
        case Capability::HttpClient: command.emplace_back("--allow-http-client"); break;
        case Capability::Env: command.emplace_back("--allow-env"); break;
        default: break;
        }
    }
    command.insert(command.end(), arguments.begin(), arguments.end());
    command.push_back(argument_path(root / detail::path_from_text(bundle.manifest().image.path)));
    return command;
}

std::optional<std::vector<std::string>> desktop_command(const fs::path& root, const std::vector<std::string>& arguments,
                                                       std::ostream& err) {
    std::optional<fs::path> engine;
    if (const char* env = std::getenv("NEURA_DESKTOP_ENGINE"); env && *env) engine = fs::path(env);
    else if (const auto installed = install_prefix() / "lib/neura/desktop/electron" / tool::executable_file("electron");
             fs::exists(installed))
        engine = installed;
    fs::path host = install_prefix() / "share/neura/hosts/desktop/main.js";
    if (!fs::exists(host)) host = install_prefix() / "hosts/desktop/main.js";
    if (const char* env = std::getenv("NEURA_DESKTOP_HOST"); env && *env) host = fs::path(env) / "main.js";
    if (!engine || !fs::exists(*engine)) {
        err << "neura run: the desktop engine target component is not installed: set NEURA_DESKTOP_ENGINE to the "
               "pinned Electron 45.3.3 executable\n";
        return std::nullopt;
    }
    if (!fs::exists(host)) {
        err << "neura run: the desktop host is not installed at " << display_path(host)
            << " (set NEURA_DESKTOP_HOST to the directory holding main.js)\n";
        return std::nullopt;
    }
    std::vector<std::string> command{argument_path(*engine), argument_path(host), "--neura-package=" + argument_path(root)};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}

} // namespace

std::optional<std::vector<std::string>> host_command(const fs::path& directory, const std::vector<std::string>& arguments,
                                                    std::ostream& err, bool allow_dev) {
    BundleVerification verification;
    verification.supported_requirements = supported_requirements();
    verification.allow_dev = allow_dev;
    auto bundle = verify_bundle_directory(directory, verification);
#ifdef _WIN32
    if (bundle.manifest().kind == TargetKind::Desktop) {
        err << "neura run: " << windows_desktop_unavailable << '\n';
        return std::nullopt;
    }
#endif
    const auto root = fs::canonical(directory);
    switch (bundle.manifest().kind) {
    case TargetKind::Web:
        err << "neura run: " << display_path(directory)
            << " is a web bundle, which runs in a browser: serve it with `neura dev --target <web target>` "
               "while developing, or copy the directory to any static file host\n";
        return std::nullopt;
    case TargetKind::Server: return server_command(bundle, root, arguments, err);
    case TargetKind::Desktop: return desktop_command(root, arguments, err);
    }
    return std::nullopt;
}

int run_bundle(const fs::path& directory, const std::vector<std::string>& arguments, std::ostream& out,
               std::ostream& err) {
    (void)out;
    try {
        auto command = host_command(directory, arguments, err, false);
        if (!command) return 1;
        return exec(std::move(*command), err);
    } catch (const Diagnostic& diagnostic) {
        print_diagnostic(err, diagnostic);
        return 1;
    } catch (const std::exception& error) {
        err << "neura run: " << error.what() << '\n';
        return 1;
    }
}

} // namespace neura::app
