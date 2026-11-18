// `neura serve`: builds a server program when given source, then runs it in
// the foreground under neura-server-host (docs/runtime/ServerHost.md). It is
// the closest form of `neura run <server bundle>` (docs/design/Projects030.md)
// until bundles exist: it adds no host behavior of its own.

#include "serve.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char** environ;

namespace neura::cli {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view serve_usage =
    "usage: neura serve [-I source-root] <file.nra>... | <file.nvr> [neura-server-host options]\n"
    "\n"
    "Builds the program when given .nra files, then serves the image with neura-server-host in the\n"
    "foreground. Without --listen or --tls-listen it listens on 127.0.0.1:8080. Every other option is\n"
    "the host's: neura-server-host --help lists them.\n";

fs::path own_executable(const char* argv0) {
#ifdef __linux__
    std::error_code error;
    auto path = fs::read_symlink("/proc/self/exe", error);
    if (!error) return path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) return fs::canonical(buffer.c_str());
#endif
    return fs::absolute(argv0);
}

// neura-server-host beside this executable, then on PATH.
std::optional<fs::path> find_host(const fs::path& self) {
    const auto beside = self.parent_path() / "neura-server-host";
    if (fs::exists(beside)) return beside;
    if (const char* path = std::getenv("PATH")) {
        std::string_view rest(path);
        while (!rest.empty()) {
            const auto colon = rest.find(':');
            const auto entry = rest.substr(0, colon);
            if (!entry.empty()) {
                const auto candidate = fs::path(entry) / "neura-server-host";
                if (::access(candidate.c_str(), X_OK) == 0) return candidate;
            }
            if (colon == std::string_view::npos) break;
            rest.remove_prefix(colon + 1);
        }
    }
    return std::nullopt;
}

int wait_for(pid_t pid) {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}

int spawn_and_wait(const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, arguments.front().c_str(), nullptr, nullptr, argv.data(), environ);
    if (error != 0) {
        std::cerr << "neura serve: cannot start " << arguments.front() << ": " << std::strerror(error) << '\n';
        return 1;
    }
    return wait_for(pid);
}

} // namespace

int run_serve(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::cerr << "neura serve: the server host runs on POSIX systems only\n";
    return 1;
#else
    std::vector<std::string> sources;
    std::vector<std::string> roots;
    std::optional<std::string> image;
    std::vector<std::string> host_options;
    bool listens = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << serve_usage;
            return 0;
        }
        if (arg == "-I" && i + 1 < argc) {
            roots.emplace_back(argv[++i]);
            continue;
        }
        if (!arg.starts_with('-') && arg.ends_with(".nra")) {
            sources.emplace_back(arg);
            continue;
        }
        if (!arg.starts_with('-') && arg.ends_with(".nvr")) {
            if (image) {
                std::cerr << "neura serve takes one image\n";
                return 1;
            }
            image = std::string(arg);
            continue;
        }
        // A host option; one that takes a separate value takes it along.
        host_options.emplace_back(arg);
        if (arg == "--listen" || arg == "--tls-listen") listens = true;
        if (arg.starts_with("--listen=") || arg.starts_with("--tls-listen=")) listens = true;
        static constexpr std::string_view with_value[] = {
            "--listen", "--tls-listen", "--admin-listen", "--cert", "--key", "--config", "--config-file",
            "--config-env-prefix", "--max-workers", "--ready-workers", "--max-queue", "--max-connections",
            "--max-head-bytes", "--max-body-bytes", "--max-response-bytes", "--request-timeout", "--drain-timeout",
            "--max-steps", "--record-dir", "--worker-tier", "--worker-jit"};
        for (const auto name : with_value) {
            if (arg == name && i + 1 < argc) {
                host_options.emplace_back(argv[++i]);
                break;
            }
        }
    }
    if (sources.empty() == !image.has_value()) {
        std::cerr << serve_usage;
        return 1;
    }
    const auto self = own_executable(argv[0]);
    const auto host = find_host(self);
    if (!host) {
        std::cerr << "neura serve: neura-server-host was found neither beside " << self.string()
                  << " nor on PATH\n";
        return 1;
    }
    std::string built;
    if (!sources.empty()) {
        // The image goes beside the first source, as `neura build` would name it.
        built = (fs::path(sources.front()).replace_extension(".nvr")).string();
        // This code runs in neura-run; the `neura` beside it starts neurac.
        std::vector<std::string> build{(self.parent_path() / "neura").string(), "build"};
        for (const auto& root : roots) {
            build.emplace_back("-I");
            build.push_back(root);
        }
        build.insert(build.end(), sources.begin(), sources.end());
        build.emplace_back("-o");
        build.push_back(built);
        if (const int status = spawn_and_wait(build); status != 0) return status;
        image = built;
    }
    std::vector<std::string> arguments{host->string()};
    if (!listens) {
        arguments.emplace_back("--listen");
        arguments.emplace_back("127.0.0.1:8080");
    }
    arguments.insert(arguments.end(), host_options.begin(), host_options.end());
    arguments.push_back(*image);
    std::vector<char*> host_argv;
    for (auto& argument : arguments) host_argv.push_back(argument.data());
    host_argv.push_back(nullptr);
    // The host replaces this process, so signals reach it directly.
    ::execv(host->c_str(), host_argv.data());
    std::cerr << "neura serve: cannot run " << host->string() << ": " << std::strerror(errno) << '\n';
    return 1;
#endif
}

} // namespace neura::cli
