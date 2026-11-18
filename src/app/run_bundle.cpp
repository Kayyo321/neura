// `neura run <bundle directory> [-- arguments]` (docs/design/Projects030.md,
// "Commands"): the bundle is verified before anything runs; a server bundle
// then runs under neura-server-host and a desktop bundle under the desktop
// host. A web bundle is not runnable.

#include "app/project.hpp"

#include "project/files.hpp"
#include "tooling/json.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace neura::app {
namespace {

using namespace project;
using tooling::Json;

std::optional<fs::path> on_path(const std::string& name) {
    if (const char* path = std::getenv("PATH")) {
        std::string_view rest(path);
        while (!rest.empty()) {
            const auto colon = rest.find(':');
            const auto entry = rest.substr(0, colon);
            if (!entry.empty()) {
                const auto candidate = fs::path(entry) / name;
#ifndef _WIN32
                if (::access(candidate.c_str(), X_OK) == 0) return candidate;
#endif
            }
            if (colon == std::string_view::npos) break;
            rest.remove_prefix(colon + 1);
        }
    }
    return std::nullopt;
}

int exec(std::vector<std::string> arguments, std::ostream& err) {
#ifdef _WIN32
    (void)arguments;
    err << "neura run: bundles run on POSIX systems only in Neura 0.3.0\n";
    return 1;
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

int run_server(const VerifiedBundle& bundle, const fs::path& root, const std::vector<std::string>& arguments,
               std::ostream& err) {
    auto host = tool_directory() / "neura-server-host";
    if (!fs::exists(host)) {
        const auto found = on_path("neura-server-host");
        if (!found) {
            err << "neura run: neura-server-host was found neither beside " << tool_directory().string()
                << " nor on PATH\n";
            return 1;
        }
        host = *found;
    }
    const auto settings_bytes = bundle.read_file("server.json");
    const auto settings = Json::parse(std::string_view(reinterpret_cast<const char*>(settings_bytes.data()), settings_bytes.size()));
    std::vector<std::string> command{host.string()};
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
            command.push_back("sql." + pool + ".migrations=" + (root / entry["migrations"].string()).string());
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
        case Capability::Env: command.emplace_back("--allow-env"); break;
        default: break;
        }
    }
    command.insert(command.end(), arguments.begin(), arguments.end());
    command.push_back((root / bundle.manifest().image.path).string());
    return exec(std::move(command), err);
}

int run_desktop(const fs::path& root, const std::vector<std::string>& arguments, std::ostream& err) {
    std::optional<fs::path> engine;
    if (const char* env = std::getenv("NEURA_DESKTOP_ENGINE"); env && *env) engine = fs::path(env);
    else if (const auto installed = install_prefix() / "lib/neura/desktop/electron/electron"; fs::exists(installed))
        engine = installed;
    fs::path host = install_prefix() / "share/neura/desktop/main.js";
    if (const char* env = std::getenv("NEURA_DESKTOP_HOST"); env && *env) host = fs::path(env) / "main.js";
    if (!engine || !fs::exists(*engine)) {
        err << "neura run: the desktop engine target component is not installed: set NEURA_DESKTOP_ENGINE to the "
               "pinned Electron 45.3.3 executable\n";
        return 1;
    }
    if (!fs::exists(host)) {
        err << "neura run: the desktop host is not installed at " << host.string()
            << " (set NEURA_DESKTOP_HOST to the directory holding main.js)\n";
        return 1;
    }
    std::vector<std::string> command{engine->string(), host.string(), "--neura-package=" + root.string()};
    command.insert(command.end(), arguments.begin(), arguments.end());
    return exec(std::move(command), err);
}

} // namespace

int run_bundle(const fs::path& directory, const std::vector<std::string>& arguments, std::ostream& out,
               std::ostream& err) {
    (void)out;
    try {
        BundleVerification verification;
        verification.supported_requirements = supported_requirements();
        auto bundle = verify_bundle_directory(directory, verification);
        const auto root = fs::canonical(directory);
        switch (bundle.manifest().kind) {
        case TargetKind::Web:
            err << "neura run: " << directory.string()
                << " is a web bundle, which runs in a browser: serve it with `neura dev --target <web target>` "
                   "while developing, or copy the directory to any static file host\n";
            return 1;
        case TargetKind::Server: return run_server(bundle, root, arguments, err);
        case TargetKind::Desktop: return run_desktop(root, arguments, err);
        }
    } catch (const Diagnostic& diagnostic) {
        print_diagnostic(err, diagnostic);
        return 1;
    } catch (const std::exception& error) {
        err << "neura run: " << error.what() << '\n';
        return 1;
    }
    return 1;
}

} // namespace neura::app
