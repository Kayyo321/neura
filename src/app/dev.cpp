// neura-dev: `neura dev [--project <manifest>] --target <name> [--port <n>]`
// (docs/design/Projects030.md, "Commands"). Builds the target into
// dist/dev/<name> marked `dev`, then serves it and rebuilds on change, in the
// foreground: a static HTTP server on 127.0.0.1 for a web target, the server
// host for a server target, the desktop host for a desktop target. A watcher
// compares the digests of the project's files every 250 ms. On an interrupt,
// a hangup, a termination request or the end of standard input it stops and
// reaps everything it started. It registers nothing with the system.
//
// On Windows (W10, W32) each host runs in a new process group and a
// kill-on-close job: it is stopped with Ctrl+Break, and the job is ended after
// two seconds. Ctrl+C and Ctrl+Break stop neura dev, and closing the console
// is its hangup. A host is stopped before its bundle is rebuilt, because
// Windows does not replace a file that a running program holds.

#include "app/project.hpp"
#include "cli/tool.hpp"
#include "project/files.hpp"
#include "tooling/json.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <map>
#include <set>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include "platform/console.hpp"
#include "platform/error.hpp"
#include "platform/file.hpp"
#include "platform/process.hpp"
#include "platform/socket.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <atomic>
#include <thread>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace {

namespace fs = std::filesystem;
using namespace neura;
using namespace neura::project;
using tooling::Json;

constexpr std::string_view usage =
    "usage: neura dev [--project <manifest>] --target <name> [--port <n>] [--web-runtime <directory>]\n"
    "                 [-- host arguments...]\n"
    "\n"
    "Builds the target into dist/dev/<name>, runs it, and rebuilds when a file of the project changes, in the\n"
    "foreground. A web target is served on http://127.0.0.1:4310 (--port; 0 asks the system for a port) and the\n"
    "page reloads after each build, or swaps its stylesheets when only they changed. A server target runs under\n"
    "neura-server-host, a desktop target under the desktop host; each is restarted after a build. Stop with an\n"
    "interrupt or the end of input; everything started is stopped.\n";

// The page's side of the reload: it polls the development server.
constexpr std::string_view reload_script = R"(// neura dev: reloads the page after a rebuild, or swaps the stylesheets when only they changed.
(() => {
    let version = null;
    const poll = async () => {
        try {
            const response = await fetch('/__neura/dev', { cache: 'no-store' });
            const state = await response.json();
            if (version !== null && state.version !== version) {
                if (!state.stylesOnly) {
                    location.reload();
                    return;
                }
                const links = [...document.querySelectorAll('link[rel="stylesheet"]')];
                state.styles.forEach((href, index) => {
                    if (links[index]) {
                        links[index].href = href;
                    } else {
                        const link = document.createElement('link');
                        link.rel = 'stylesheet';
                        link.href = href;
                        document.head.append(link);
                    }
                });
                links.slice(state.styles.length).forEach(link => link.remove());
            }
            version = state.version;
        } catch {
            // The development server is restarting or gone; keep polling.
        }
        setTimeout(poll, 250);
    };
    poll();
})();
)";

#ifdef _WIN32
// Set by the console handler and the input reader, which run on threads of
// their own.
std::atomic<int> stop_requested{0};

// Ctrl+C is the interrupt, Ctrl+Break the termination request and closing
// the console, logging off or shutting down the hangup (W10): each stops
// neura dev. The system ends the process as soon as the handler returns from
// a hangup, so the handler waits instead while the main thread stops the
// hosts and returns; the hosts' jobs end with the process in any case.
bool stop_on_console_event(platform::ConsoleEvent event) {
    stop_requested = 1;
    if (event != platform::ConsoleEvent::ctrl_c && event != platform::ConsoleEvent::ctrl_break) ::Sleep(INFINITE);
    return true;
}

// The end of standard input stops neura dev. WSAPoll waits for sockets only,
// so a thread reads the input; when neura dev stops for another reason, the
// thread is left in its read and ends with the process.
void watch_input() {
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE || ::GetFileType(input) == FILE_TYPE_UNKNOWN) return;
    std::thread([input] {
        char buffer[256];
        std::error_code error;
        while (platform::read_some(input, buffer, sizeof buffer, error) > 0) {}
        stop_requested = 1;
    }).detach();
}
#else
volatile std::sig_atomic_t stop_requested = 0;
void on_signal(int) { stop_requested = 1; }
#endif

// --- Children, each the leader of its own process group ----------------------------------------------------

#ifdef _WIN32
struct Child {
    platform::Process process;
};

// The name a message gives a host: its file name, without .exe on Windows.
std::string host_name(const std::string& program) {
    auto name = detail::path_text(detail::path_from_text(program).filename());
    if (name.size() > 4) {
        auto suffix = name.substr(name.size() - 4);
        std::ranges::transform(suffix, suffix.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (suffix == ".exe") name.resize(name.size() - 4);
    }
    return name;
}

Child start_child(const std::vector<std::string>& command) {
    platform::Spawn spawn;
    std::error_code error;
    try {
        spawn.program = platform::path_from_utf8(command.front());
    } catch (const std::system_error& invalid) {
        error = invalid.code();
    }
    spawn.arguments = command;
    spawn.new_process_group = true;
    spawn.job = platform::JobMode::kill_on_close;
    Child child;
    if (!error) child.process = platform::Process::start(spawn, error);
    if (!child.process) {
        std::cerr << "neura dev: cannot start " << command.front() << ": " << platform::error_message(error) << '\n';
    }
    return child;
}

// Stops the child and everything in its job: Ctrl+Break to its process group
// (the hosts' termination request), then the job is ended after two seconds,
// and what the child started ends with it.
void stop_child(Child& child) {
    if (!child.process) return;
    std::error_code error;
    if (!child.process.wait(std::chrono::milliseconds(0), error)) {
        std::error_code sent;
        if (child.process.interrupt(platform::ConsoleEvent::ctrl_break, sent))
            child.process.wait(std::chrono::milliseconds(2000), error);
    }
    child.process.kill_tree();
    child.process.wait(std::nullopt, error);
    child.process = platform::Process{};
}

bool child_exited(Child& child) {
    if (!child.process) return false;
    std::error_code error;
    const auto status = child.process.wait(std::chrono::milliseconds(0), error);
    if (!status) return false;
    std::cerr << "neura dev: the host ended ("
              << (status->kind == platform::ExitStatus::Kind::crashed ? status->describe()
                                                                       : "status " + std::to_string(status->shell_status()))
              << "); it starts again after the next change\n";
    child.process.kill_tree();
    child.process = platform::Process{};
    return true;
}
#else
struct Child {
    pid_t pid = -1;
};

Child start_child(const std::vector<std::string>& command) {
    std::vector<char*> argv;
    for (const auto& argument : command) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    posix_spawnattr_setpgroup(&attributes, 0);
    sigset_t empty, defaults;
    sigemptyset(&empty);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGHUP);
    sigaddset(&defaults, SIGPIPE);
    posix_spawnattr_setsigmask(&attributes, &empty);
    posix_spawnattr_setsigdefault(&attributes, &defaults);
    pid_t pid = -1;
    const int error = ::posix_spawn(&pid, command.front().c_str(), nullptr, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    if (error != 0) {
        std::cerr << "neura dev: cannot start " << command.front() << ": " << std::strerror(error) << '\n';
        return {};
    }
    return {pid};
}

// Stops the child's whole process group: SIGTERM, then SIGKILL after two
// seconds, and reaps the child.
void stop_child(Child& child) {
    if (child.pid <= 0) return;
    ::kill(-child.pid, SIGTERM);
    for (int i = 0; i < 200; ++i) {
        int status = 0;
        const auto done = ::waitpid(child.pid, &status, WNOHANG);
        if (done == child.pid || (done < 0 && errno == ECHILD)) {
            // The group may hold processes the leader started; end them too.
            ::kill(-child.pid, SIGKILL);
            child.pid = -1;
            return;
        }
        ::usleep(10000);
    }
    ::kill(-child.pid, SIGKILL);
    while (::waitpid(child.pid, nullptr, 0) < 0 && errno == EINTR) {}
    child.pid = -1;
}

bool child_exited(Child& child) {
    if (child.pid <= 0) return false;
    int status = 0;
    if (::waitpid(child.pid, &status, WNOHANG) == child.pid) {
        std::cerr << "neura dev: the host ended (status "
                  << (WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status))
                  << "); it starts again after the next change\n";
        ::kill(-child.pid, SIGKILL);
        child.pid = -1;
        return true;
    }
    return false;
}
#endif

// --- The watcher ---------------------------------------------------------------------------------------

using Snapshot = std::map<std::string, std::string>;

void add_file(Snapshot& result, const fs::path& root, const fs::path& file) {
    try {
        result.emplace(detail::path_text(file.lexically_relative(root)), std::string(app::sha256_of(app::read_file(file)).hex()));
    } catch (const std::exception&) {
        // A file removed while it was read: the next poll sees it gone.
    }
}

void add_tree(Snapshot& result, const fs::path& root, const fs::path& directory) {
    std::error_code error;
    fs::recursive_directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
    for (; !error && it != end; it.increment(error)) {
        if (it->is_directory(error) && it->path().filename().string().starts_with('.')) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(error)) add_file(result, root, it->path());
    }
}

// The digests of everything a build reads: the manifest, the lock, every
// package's source and asset roots, and each target's stylesheets, icon and
// migrations. Other files of the project directory (logs, dist/) are not
// watched.
Snapshot snapshot(const app::Project& project) {
    Snapshot result;
    const auto& root = project.resolved.root;
    add_file(result, root, project.resolved.manifest);
    add_file(result, root, root / "neura.lock");
    for (const auto& [name, package] : project.resolved.packages) {
        add_tree(result, root, package.root.value);
        for (const auto& assets : package.assets) add_tree(result, root, assets.value);
    }
    for (const auto& [name, target] : project.resolved.targets) {
        for (const auto& style : target.styles) add_file(result, root, style.value);
        if (target.icon) add_file(result, root, target.icon->value);
        for (const auto& [pool, migrations] : target.migrations) add_tree(result, root, migrations.value);
    }
    return result;
}

// --- The static server -----------------------------------------------------------------------------------

struct BuildState {
    std::uint64_t version = 0;
    bool styles_only = false;
    std::vector<std::string> styles;
    std::map<std::string, std::pair<std::string, std::string>> files; // path -> (media type, sha256)
    bool history = false;
};

BuildState read_state(const fs::path& bundle) {
    BuildState state;
    const auto manifest = Json::parse(app::read_file(bundle / "bundle.json"));
    for (const auto& entry : manifest["files"].items())
        state.files[entry["path"].string()] = {entry["media_type"].string(), entry["sha256"].string()};
    state.history = manifest.contains("routing") && manifest["routing"].string() == "history";
    const auto page = app::read_file(bundle / "index.html");
    const std::string marker = "<link rel=\"stylesheet\" href=\"";
    for (auto at = page.find(marker); at != std::string::npos; at = page.find(marker, at + 1)) {
        const auto begin = at + marker.size();
        auto href = page.substr(begin, page.find('"', begin) - begin);
        if (href.starts_with("./")) href = href.substr(1);
        state.styles.push_back(href);
    }
    return state;
}

bool only_styles_changed(const BuildState& before, const BuildState& after) {
    static const std::set<std::string> generated{"index.html", "404.html", "bundle.json", "neura-dev.js"};
    std::set<std::string> paths;
    for (const auto& [path, _] : before.files) paths.insert(path);
    for (const auto& [path, _] : after.files) paths.insert(path);
    bool any = false;
    for (const auto& path : paths) {
        if (generated.contains(path)) continue;
        const auto a = before.files.find(path), b = after.files.find(path);
        const bool same = a != before.files.end() && b != after.files.end() && a->second == b->second;
        if (same) continue;
        if (!path.starts_with("styles/")) return false;
        any = true;
    }
    return any;
}

#ifdef _WIN32
using Socket = platform::native_socket;
constexpr Socket no_socket = platform::invalid_socket;
#else
using Socket = int;
constexpr Socket no_socket = -1;
#endif

void close_listener(Socket fd) {
#ifdef _WIN32
    platform::close_socket(fd);
#else
    ::close(fd);
#endif
}

void send_all(Socket fd, std::string_view data) {
#ifdef _WIN32
    while (!data.empty()) {
        const auto sent = ::send(fd, data.data(), static_cast<int>(std::min<std::size_t>(data.size(), 1 << 30)), 0);
        if (sent <= 0) {
            if (sent < 0 && platform::last_socket_error().interrupted()) continue;
            return;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
#else
    while (!data.empty()) {
#ifdef MSG_NOSIGNAL
        const auto sent = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
#else
        const auto sent = ::send(fd, data.data(), data.size(), 0); // SIGPIPE is ignored
#endif
        if (sent <= 0) {
            if (sent < 0 && errno == EINTR) continue;
            return;
        }
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
#endif
}

void respond(Socket fd, int status, std::string_view reason, std::string_view type, std::string_view body, bool head) {
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + std::string(reason) + "\r\n" +
                           "Content-Type: " + std::string(type) + "\r\nContent-Length: " + std::to_string(body.size()) +
                           "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (!head) response += body;
    send_all(fd, response);
}

std::string percent_decode(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size() && std::isxdigit(static_cast<unsigned char>(text[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(text.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

// One request per connection, read with a short deadline; answered from the
// current bundle directory.
void serve_request(Socket fd, const fs::path& bundle, const BuildState& state) {
    std::string request;
    char buffer[4096];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 65536) {
#ifdef _WIN32
        platform::PollItem p{fd, platform::poll_in, 0};
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0 || platform::poll(std::span(&p, 1), static_cast<int>(left)) <= 0) return;
        const auto count = ::recv(fd, buffer, static_cast<int>(sizeof buffer), 0);
#else
        pollfd p{fd, POLLIN, 0};
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0 || ::poll(&p, 1, static_cast<int>(left)) <= 0) return;
        const auto count = ::recv(fd, buffer, sizeof buffer, 0);
#endif
        if (count <= 0) return;
        request.append(buffer, static_cast<std::size_t>(count));
    }
    const auto line_end = request.find("\r\n");
    const auto first = request.substr(0, line_end);
    const auto space1 = first.find(' '), space2 = first.rfind(' ');
    if (space1 == std::string::npos || space2 == space1) return respond(fd, 400, "Bad Request", "text/plain", "bad request\n", false);
    const auto method = first.substr(0, space1);
    auto target = first.substr(space1 + 1, space2 - space1 - 1);
    const bool head = method == "HEAD";
    if (method != "GET" && !head) return respond(fd, 405, "Method Not Allowed", "text/plain", "GET only\n", false);
    target = target.substr(0, target.find_first_of("?#"));
    if (target == "/__neura/dev") {
        Json::Array styles;
        for (const auto& href : state.styles) styles.emplace_back(href);
        const Json body{{"styles", std::move(styles)}, {"stylesOnly", state.styles_only}, {"version", state.version}};
        return respond(fd, 200, "OK", "application/json", body.dump(), head);
    }
    auto path = percent_decode(target);
    while (path.starts_with('/')) path.erase(0, 1);
    if (path.empty()) path = "index.html";
    auto found = state.files.find(path);
    if (found == state.files.end()) {
        // History routing: every application path is the page.
        if (state.history && path.find('.') == std::string::npos) found = state.files.find("index.html");
        if (found == state.files.end()) return respond(fd, 404, "Not Found", "text/plain", "not found\n", head);
    }
    try {
        const auto body = app::read_file(bundle / detail::path_from_text(found->first));
        respond(fd, 200, "OK", found->second.first, body, head);
    } catch (const std::exception&) {
        respond(fd, 404, "Not Found", "text/plain", "not found\n", head);
    }
}

#ifdef _WIN32
// The development server's socket on 127.0.0.1. SO_EXCLUSIVEADDRUSE keeps any
// other socket from binding the port while it is open (Windows' SO_REUSEADDR
// would let another program take it over).
Socket open_listener(std::uint16_t port, std::uint16_t& bound, std::error_code& error) {
    if ((error = platform::ensure_sockets())) return no_socket;
    const SOCKET fd = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (fd == INVALID_SOCKET) {
        error = platform::last_socket_error().error_code();
        return no_socket;
    }
    const BOOL on = TRUE;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = ::htons(port);
    address.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    if (::setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof on) != 0 ||
        ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(fd, 64) != 0) {
        error = platform::last_socket_error().error_code();
        platform::close_socket(fd);
        return no_socket;
    }
    int size = sizeof address;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size);
    bound = ::ntohs(address.sin_port);
    return fd;
}
#else
int open_listener(std::uint16_t port, std::uint16_t& bound) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(fd, 64) != 0) {
        const int saved = errno;
        ::close(fd);
        errno = saved;
        return -1;
    }
    socklen_t size = sizeof address;
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size);
    bound = ntohs(address.sin_port);
    return fd;
}
#endif

// --- The loop -----------------------------------------------------------------------------------------------

struct Options {
    std::optional<std::string> project;
    std::string target;
    std::uint16_t port = 4310;
    bool port_given = false;
    std::optional<fs::path> web_runtime;
    std::vector<std::string> host_arguments;
};

std::optional<app::BuildResult> build(app::Project& project, const Options& options) {
    app::BuildOptions build_options;
    build_options.target = options.target;
    build_options.out = project.resolved.root / "dist" / "dev" / options.target;
    build_options.dev = true;
    build_options.dev_reload = std::string(reload_script);
    build_options.web_runtime = options.web_runtime;
    try {
        build_options.neura = app::tool_directory() / tool::executable_file("neura");
        return app::build_target(project, build_options);
    } catch (const Diagnostic& diagnostic) {
        app::print_diagnostic(std::cerr, diagnostic);
    } catch (const std::exception& error) {
        std::cerr << "neura dev: " << error.what() << '\n';
    }
    return std::nullopt;
}

int develop(const Options& options) {
    const auto manifest = app::find_manifest(options.project, std::cerr);
    if (!manifest) return 1;
    app::Project project;
    try {
        project = app::load_project(*manifest);
        app::check_target(project, options.target);
        app::check_lock(project, false, true);
    } catch (const Diagnostic& diagnostic) {
        app::print_diagnostic(std::cerr, diagnostic);
        return 1;
    }
    const auto kind = project.manifest.targets.at(options.target).kind.value;

#ifdef _WIN32
    platform::on_console_event(stop_on_console_event);
#else
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGHUP, on_signal);
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // A web target's port is bound before the first build, so a busy port
    // fails at once and nothing is left behind.
    Socket listener = no_socket;
    std::uint16_t bound = 0;
    if (kind == TargetKind::Web) {
#ifdef _WIN32
        std::error_code error;
        listener = open_listener(options.port, bound, error);
        if (listener == no_socket) {
            std::cerr << "neura dev: cannot listen on 127.0.0.1:" << options.port << ": " << platform::error_message(error)
                      << '\n';
            return 1;
        }
#else
        listener = open_listener(options.port, bound);
        if (listener < 0) {
            std::cerr << "neura dev: cannot listen on 127.0.0.1:" << options.port << ": " << std::strerror(errno) << '\n';
            return 1;
        }
#endif
    }
    auto files = snapshot(project);
    auto result = build(project, options);
    if (!result) {
        if (listener != no_socket) close_listener(listener);
        return 1;
    }
    BuildState state = kind == TargetKind::Web ? read_state(result->directory) : BuildState{};
    state.version = 1;
    Child host;
    std::vector<std::string> host_arguments = options.host_arguments;
    if (kind == TargetKind::Server && options.port_given) {
        host_arguments.insert(host_arguments.begin(), {"--listen", "127.0.0.1:" + std::to_string(options.port)});
    }
    const auto start_host = [&] {
        try {
            const auto command = app::host_command(result->directory, host_arguments, std::cerr, true);
            if (command) {
                host = start_child(*command);
#ifdef _WIN32
                if (host.process) std::cout << "neura dev: started " << host_name(command->front()) << " (process "
                                            << host.process.pid() << ")" << std::endl;
#else
                if (host.pid > 0) std::cout << "neura dev: started " << fs::path(command->front()).filename().string()
                                            << " (process " << host.pid << ")" << std::endl;
#endif
            }
        } catch (const Diagnostic& diagnostic) {
            app::print_diagnostic(std::cerr, diagnostic);
        }
    };
    if (kind == TargetKind::Web) {
        std::cout << "neura dev: serving " << options.target << " at http://127.0.0.1:" << bound << "/" << std::endl;
    } else {
        start_host();
    }

#ifdef _WIN32
    watch_input();
#else
    const bool watch_input = ::isatty(STDIN_FILENO) || ::fcntl(STDIN_FILENO, F_GETFL) >= 0;
#endif
    auto next_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    int status = 0;
    while (!stop_requested) {
#ifdef _WIN32
        const auto wait = std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     next_poll - std::chrono::steady_clock::now()).count());
        if (listener != no_socket) {
            platform::PollItem item{listener, platform::poll_in, 0};
            const int ready = platform::poll(std::span(&item, 1), static_cast<int>(wait));
            if (ready < 0 && !platform::last_socket_error().interrupted()) break;
            if (ready > 0 && item.revents) {
                const auto client = ::accept(listener, nullptr, nullptr);
                if (client != INVALID_SOCKET) {
                    platform::set_no_inherit(client);
                    serve_request(client, result->directory, state);
                    platform::close_socket(client);
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<long long>(wait, 50)));
        }
#else
        std::vector<pollfd> watched;
        if (listener >= 0) watched.push_back({listener, POLLIN, 0});
        if (watch_input) watched.push_back({STDIN_FILENO, POLLIN, 0});
        const auto wait = std::max<long long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                     next_poll - std::chrono::steady_clock::now()).count());
        const int ready = ::poll(watched.data(), watched.size(), static_cast<int>(wait));
        if (ready < 0 && errno != EINTR) break;
        for (const auto& entry : watched) {
            if (!entry.revents) continue;
            if (entry.fd == listener) {
                const int client = ::accept(listener, nullptr, nullptr);
                if (client >= 0) {
                    ::fcntl(client, F_SETFD, FD_CLOEXEC);
                    serve_request(client, result->directory, state);
                    ::close(client);
                }
            } else if (entry.fd == STDIN_FILENO) {
                char buffer[256];
                const auto count = ::read(STDIN_FILENO, buffer, sizeof buffer);
                if (count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN)) stop_requested = 1;
            }
        }
#endif
        child_exited(host);
        if (std::chrono::steady_clock::now() < next_poll) continue;
        next_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        auto now = snapshot(project);
        if (now == files) continue;
        files = std::move(now);
        std::cout << "neura dev: a change; building " << options.target << std::endl;
        try {
            project = app::load_project(*manifest);
            app::check_target(project, options.target);
            app::check_lock(project, false, true);
        } catch (const Diagnostic& diagnostic) {
            app::print_diagnostic(std::cerr, diagnostic);
            continue;
        }
        files = snapshot(project);
#ifdef _WIN32
        // The running host holds files of dist/dev/<name>, which Windows does
        // not let the build replace, so it stops first; when the build fails,
        // the previous bundle, still in place, runs again (W32).
        const bool was_running = static_cast<bool>(host.process);
        if (kind != TargetKind::Web) stop_child(host);
        auto rebuilt = build(project, options);
        if (!rebuilt) {
            if (was_running) start_host();
            continue;
        }
#else
        auto rebuilt = build(project, options);
        if (!rebuilt) continue;
#endif
        result = rebuilt;
        if (kind == TargetKind::Web) {
            auto next = read_state(result->directory);
            next.styles_only = only_styles_changed(state, next);
            next.version = state.version + 1;
            state = std::move(next);
            std::cout << "neura dev: built " << result->digest.hex() << (state.styles_only ? "; stylesheets swapped" : "; page reloads")
                      << std::endl;
        } else {
            stop_child(host);
            start_host();
        }
    }
    stop_child(host);
    if (listener != no_socket) close_listener(listener);
    std::cout << "neura dev: stopped" << std::endl;
    return status;
}

int dev(const std::vector<std::string>& arguments) {
    Options options;
    for (std::size_t i = 2; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        const auto value = [&]() -> std::optional<std::string> {
            if (i + 1 >= arguments.size()) {
                std::cerr << "neura dev: " << arg << " needs a value\n";
                return std::nullopt;
            }
            return arguments[++i];
        };
        if (arg == "--help" || arg == "-h") {
            std::cout << usage;
            return 0;
        } else if (arg == "--") {
            options.host_arguments.assign(arguments.begin() + static_cast<long>(i) + 1, arguments.end());
            break;
        } else if (arg == "--project") {
            options.project = value();
            if (!options.project) return 1;
        } else if (arg == "--target") {
            const auto v = value();
            if (!v) return 1;
            options.target = *v;
        } else if (arg == "--web-runtime") {
            const auto v = value();
            if (!v) return 1;
            options.web_runtime = *v;
        } else if (arg == "--port") {
            const auto v = value();
            if (!v) return 1;
            char* end = nullptr;
            const auto number = std::strtoul(v->c_str(), &end, 10);
            if (v->empty() || *end || number > 65535) {
                std::cerr << "neura dev: --port takes a number from 0 to 65535\n";
                return 1;
            }
            options.port = static_cast<std::uint16_t>(number);
            options.port_given = true;
        } else {
            std::cerr << "neura dev: unexpected argument " << arg << '\n' << usage;
            return 1;
        }
    }
    if (options.target.empty()) {
        std::cerr << usage;
        return 1;
    }
    return develop(options);
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-dev"), argc, argv);
    if (start.status) return *start.status;
    try {
        return dev(start.arguments);
    } catch (const std::exception& error) {
        std::cerr << "neura dev: " << error.what() << '\n';
        return 1;
    }
}
