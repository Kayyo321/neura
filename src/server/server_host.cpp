// neura-server-host: the native host of a server image (docs/design/Backend030.md,
// "The server host and request workers"). It owns the listeners, TLS, the
// HTTP wire and the request workers, and runs no application code: every
// request is executed by a fresh worker process that runs the whole image.

#include "cli/http_client_options.hpp"
#include "cli/tool.hpp"
#include "nvr/format.hpp"
#include "platform/text.hpp"
#include "runner/runner.hpp"
#include "server/http_listener.hpp"
#include "server/json_line.hpp"
#include "server/request_worker.hpp"
#include "server/sql_host.hpp"
#include "server/worker_messages.hpp"
#include "server/worker_zygote.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
// The Windows host (docs/design/Windows030.md, W27): workers start with exec
// through platform::Process, every worker is in one job, and every pollable
// is a socket that WSAPoll waits on.
#include "platform/console.hpp"
#include "platform/environment.hpp"
#include "platform/error.hpp"
#include "platform/process.hpp"
#include "platform/self.hpp"
#include "platform/socket.hpp"
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/syscall.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifndef _WIN32
extern char** environ;
#endif

namespace {

namespace fs = std::filesystem;
using namespace neura::server;
using namespace std::chrono_literals;
using worker::Channel;
using worker::Clock;
using worker::FrameKind;

constexpr std::string_view usage =
    "usage: neura-server-host [options] <image.nvr>\n"
    "\n"
    "Serves a server image over HTTP/1.1, and HTTP/2 in builds with it (docs/runtime/ServerHost.md).\n"
    "Each request runs in a fresh worker process that executes the whole image once.\n"
    "\n"
    "  --listen <address>:<port>      plain HTTP listener (repeatable; port 0 asks for one)\n"
    "  --tls-listen <address>:<port>  HTTPS listener; needs --cert and --key (repeatable)\n"
#ifdef _WIN32
    "  --cert <file> --key <file>     PEM certificate chain and private key, reread on --reload\n"
#else
    "  --cert <file> --key <file>     PEM certificate chain and private key, reread on SIGHUP\n"
#endif
    "  --no-http2                     TLS listeners offer only http/1.1 (default: h2 and http/1.1)\n"
    "  --h2c                          plain listeners also accept HTTP/2 with prior knowledge\n"
    "  --max-streams N                S-14, concurrent HTTP/2 streams per connection (default 100)\n"
    "  --admin-listen <address>:<port> admin listener for GET /livez, /readyz and /metrics\n"
    "                                 (default: none; give it a loopback address)\n"
    "  --config <key>=<value>         a configuration value for neura.app.config (repeatable)\n"
    "  --config-file <file>           key=value lines; # starts a comment\n"
    "  --config-env-prefix <prefix>   environment variables <prefix><key> override both\n"
    "  --max-workers N                S-1, workers running at once (default 8, at most 1024)\n"
    "  --ready-workers N              S-2, started workers waiting for a request (default 2)\n"
    "  --max-queue N                  S-3, requests waiting for a worker (default 64)\n"
    "  --max-connections N            S-4 (default 1024)\n"
    "  --max-head-bytes N             S-5 (default 16384)\n"
    "  --max-body-bytes N             S-6 (default 1048576)\n"
    "  --max-response-bytes N         S-7 (default 8388608)\n"
    "  --request-timeout SECONDS      S-8 (default 30)\n"
    "  --drain-timeout SECONDS        S-12 drain (default 30)\n"
    "  --max-steps N                  NVM steps one request's execution may take (default 100000000);\n"
    "                                 past it the execution traps and the request is answered 500\n"
    "  --worker-tier 0|1|2            highest execution tier of a worker (default: the runner's, 2)\n"
    "  --worker-jit off|on|auto       native compilation of a worker's tier-2 regions (default auto)\n"
    "  --worker-start zygote|exec     fork workers from a process that loaded the image once (zygote,\n"
    "                                 the default on Linux), or start each with exec\n"
    "  --allow-fs[=prefix,...], --allow-net, --allow-env\n"
    "  --allow-http-client[=origin,...], --deny-http-client\n"
    "                                 grant the workers that group (default: none)\n"
    "  --record-dir <directory>       record each request's host requests to <id>.trace\n"
    "  --sql-pool <name>=<url>        an SQL pool: sqlite:<path> or postgresql://... (repeatable);\n"
    "                                 settings and passwords are sql.<name>.* configuration keys\n"
    "  --no-startup-check             skip the startup check request\n"
    "\n"
    "neura-server-host migrate [options] <image.nvr> applies the pools' pending migrations and exits.\n"
#ifdef _WIN32
    "Ctrl+C and Ctrl+Break (and closing the console) drain and exit; neura-server-host --reload <pid>\n"
    "makes the host with that process identifier reread the TLS files.\n";
#else
    "SIGTERM and SIGINT drain and exit; SIGHUP reloads the TLS files.\n";
#endif

// ---------------------------------------------------------------------------
// Options

struct HostOptions {
    fs::path image;
    std::vector<http::ListenEndpoint> endpoints;
    std::vector<http::ListenEndpoint> admin_endpoints;
    std::optional<fs::path> certificate;
    std::optional<fs::path> key;
    std::map<std::string, std::string, std::less<>> config;
    std::optional<std::string> env_prefix;
    std::size_t max_workers = 8;
    std::size_t ready_workers = 2;
    std::size_t max_queue = 64;
    http::ListenerLimits limits;
    std::chrono::seconds drain_timeout{30};
    std::chrono::seconds grace{5};
    neura::nvm::Capabilities capabilities;
    std::optional<fs::path> record_directory;
    bool startup_check = true;
    bool h2c = false;
    std::optional<std::uint64_t> max_steps;
    std::optional<std::uint8_t> worker_tier;
    std::optional<std::string> worker_jit;
    // SQL pools (docs/runtime/Sql.md): --sql-pool values; the sql.* keys
    // move from `config` to the pools and never reach a worker.
    std::vector<std::string> sql_pools;
    fs::path config_base = fs::current_path();
    bool migrate_only = false;
    bool zygote = zygote_supported();
};

std::uint64_t parse_count(std::string_view option, std::string_view text, std::uint64_t low, std::uint64_t high) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < low || value > high) {
        throw std::runtime_error(std::string(option) + " expects a number from " + std::to_string(low) + " to " +
                                 std::to_string(high));
    }
    return value;
}

http::ListenEndpoint parse_endpoint(std::string_view option, std::string_view text) {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos || colon == 0) {
        throw std::runtime_error(std::string(option) + " expects <address>:<port>, such as 127.0.0.1:8080");
    }
    auto address = std::string(text.substr(0, colon));
    if (address.size() > 2 && address.front() == '[' && address.back() == ']') address = address.substr(1, address.size() - 2);
    http::ListenEndpoint endpoint;
    endpoint.address = std::move(address);
    endpoint.port = static_cast<std::uint16_t>(parse_count(option, text.substr(colon + 1), 0, 65535));
    return endpoint;
}

void read_config_file(const fs::path& path, HostOptions& options) {
#ifdef _WIN32
    const auto display = neura::platform::utf8_path(path);
#else
    const auto display = path.string();
#endif
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read the configuration file " + display);
    std::string line;
    std::size_t number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == first) {
            throw std::runtime_error(display + ":" + std::to_string(number) + ": expected key=value");
        }
        auto key = line.substr(first, equals - first);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        options.config[key] = line.substr(equals + 1);
    }
}

HostOptions read_options(int argc, char** argv) {
    HostOptions options;
    std::vector<std::string> config_values;
    std::vector<fs::path> config_files;
    std::vector<std::size_t> tls_endpoints;
    const auto value_of = [&](int& i, std::string_view arg, std::string_view name) -> std::string {
        if (arg.size() > name.size() && arg[name.size()] == '=') return std::string(arg.substr(name.size() + 1));
        if (i + 1 >= argc) throw std::runtime_error(std::string(name) + " expects a value");
        return argv[++i];
    };
    const auto is = [](std::string_view arg, std::string_view name) {
        return arg == name || (arg.starts_with(name) && arg.size() > name.size() && arg[name.size()] == '=');
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "neura-server-host: the host that neura serve starts ("
                      << neura::tool::component_name(neura::tool::Component::Core) << ")\n"
                      << usage;
            std::exit(0);
        } else if (is(arg, "--listen")) {
            options.endpoints.push_back(parse_endpoint("--listen", value_of(i, arg, "--listen")));
        } else if (is(arg, "--tls-listen")) {
            tls_endpoints.push_back(options.endpoints.size());
            options.endpoints.push_back(parse_endpoint("--tls-listen", value_of(i, arg, "--tls-listen")));
        } else if (is(arg, "--admin-listen")) {
            options.admin_endpoints.push_back(parse_endpoint("--admin-listen", value_of(i, arg, "--admin-listen")));
        } else if (is(arg, "--cert")) {
            options.certificate = neura::platform::path_from_utf8(value_of(i, arg, "--cert"));
        } else if (is(arg, "--key")) {
            options.key = neura::platform::path_from_utf8(value_of(i, arg, "--key"));
        } else if (is(arg, "--config")) {
            config_values.push_back(value_of(i, arg, "--config"));
        } else if (is(arg, "--config-file")) {
            config_files.push_back(neura::platform::path_from_utf8(value_of(i, arg, "--config-file")));
        } else if (is(arg, "--config-env-prefix")) {
            options.env_prefix = value_of(i, arg, "--config-env-prefix");
        } else if (is(arg, "--max-workers")) {
            options.max_workers = parse_count(arg, value_of(i, arg, "--max-workers"), 1, 1024);
        } else if (is(arg, "--ready-workers")) {
            options.ready_workers = parse_count(arg, value_of(i, arg, "--ready-workers"), 0, 64);
        } else if (is(arg, "--max-queue")) {
            options.max_queue = parse_count(arg, value_of(i, arg, "--max-queue"), 0, 65536);
        } else if (is(arg, "--max-connections")) {
            options.limits.connections = parse_count(arg, value_of(i, arg, "--max-connections"), 1, 1'000'000);
        } else if (is(arg, "--max-head-bytes")) {
            options.limits.http.request.head_bytes =
                parse_count(arg, value_of(i, arg, "--max-head-bytes"), 1024, 1024 * 1024);
        } else if (is(arg, "--max-body-bytes")) {
            options.limits.http.request.body_bytes =
                parse_count(arg, value_of(i, arg, "--max-body-bytes"), 0, 1024ull * 1024 * 1024);
        } else if (is(arg, "--max-response-bytes")) {
            options.limits.http.response_bytes =
                parse_count(arg, value_of(i, arg, "--max-response-bytes"), 0, 1024ull * 1024 * 1024);
        } else if (is(arg, "--request-timeout")) {
            options.limits.http.request_timeout =
                std::chrono::seconds(parse_count(arg, value_of(i, arg, "--request-timeout"), 1, 3600));
        } else if (is(arg, "--drain-timeout")) {
            options.drain_timeout = std::chrono::seconds(parse_count(arg, value_of(i, arg, "--drain-timeout"), 0, 3600));
        } else if (is(arg, "--worker-tier")) {
            options.worker_tier = static_cast<std::uint8_t>(parse_count(arg, value_of(i, arg, "--worker-tier"), 0, 2));
        } else if (is(arg, "--worker-jit")) {
            auto mode = value_of(i, arg, "--worker-jit");
            if (mode != "off" && mode != "on" && mode != "auto") throw std::runtime_error("--worker-jit expects off, on or auto");
            options.worker_jit = std::move(mode);
        } else if (is(arg, "--worker-start")) {
            const auto mode = value_of(i, arg, "--worker-start");
            if (mode != "zygote" && mode != "exec") throw std::runtime_error("--worker-start expects zygote or exec");
            if (mode == "zygote" && !zygote_supported()) {
                throw std::runtime_error("--worker-start zygote needs Linux");
            }
            options.zygote = mode == "zygote";
        } else if (is(arg, "--max-steps")) {
            options.max_steps = parse_count(arg, value_of(i, arg, "--max-steps"), 1, std::uint64_t{1} << 62);
        } else if (arg == "--allow-fs") {
            options.capabilities.fs = true;
            options.capabilities.fs_prefixes.clear();
        } else if (arg.starts_with("--allow-fs=")) {
            options.capabilities.fs = true;
            std::string list(arg.substr(11));
            std::stringstream stream(list);
            std::string prefix;
            while (std::getline(stream, prefix, ',')) {
                if (!prefix.empty()) options.capabilities.fs_prefixes.push_back(prefix);
            }
        } else if (arg == "--allow-net") {
            options.capabilities.net = true;
        } else if (neura::cli::parse_http_client_option(arg, options.capabilities)) {
            continue;
        } else if (arg == "--allow-env") {
            options.capabilities.env = true;
        } else if (is(arg, "--record-dir")) {
            options.record_directory = fs::absolute(neura::platform::path_from_utf8(value_of(i, arg, "--record-dir")));
        } else if (arg == "--no-startup-check") {
            options.startup_check = false;
        } else if (is(arg, "--sql-pool")) {
            options.sql_pools.push_back(value_of(i, arg, "--sql-pool"));
        } else if (i == 1 && arg == "migrate") {
            options.migrate_only = true;
        } else if (arg == "--no-http2") {
            options.limits.http.http2.tls = false;
        } else if (arg == "--h2c") {
            options.h2c = true;
            options.limits.http.http2.cleartext = true;
        } else if (is(arg, "--max-streams")) {
            options.limits.http.http2.max_concurrent_streams =
                static_cast<std::uint32_t>(parse_count(arg, value_of(i, arg, "--max-streams"), 1, 1000));
        } else if (arg.starts_with('-')) {
            throw std::runtime_error("unknown option: " + std::string(arg) + "\n" + std::string(usage));
        } else if (!options.image.empty()) {
            throw std::runtime_error("neura-server-host takes one image");
        } else {
            options.image = fs::absolute(neura::platform::path_from_utf8(arg));
        }
    }
    if (options.image.empty()) throw std::runtime_error(std::string(usage));
#if defined(_WIN32) && !defined(NEURA_SERVER_TLS_ENABLED)
    if (!tls_endpoints.empty()) throw std::runtime_error("TLS listeners are unavailable on Windows in Neura 0.3.0");
#endif
#ifndef NEURA_SERVER_HTTP2_ENABLED
#ifdef _WIN32
    if (options.h2c) throw std::runtime_error("--h2c: HTTP/2 is unavailable on Windows in Neura 0.3.0");
#else
    if (options.h2c) throw std::runtime_error("--h2c: this server build has no HTTP/2");
#endif
#endif
    if (options.endpoints.empty() && !options.migrate_only) {
        throw std::runtime_error("at least one --listen or --tls-listen is required");
    }
    if (!tls_endpoints.empty()) {
        if (!options.certificate || !options.key) throw std::runtime_error("--tls-listen needs --cert and --key");
        for (const auto index : tls_endpoints) {
            options.endpoints[index].tls = http::TlsFiles{fs::absolute(*options.certificate), fs::absolute(*options.key)};
        }
    } else if (options.certificate || options.key) {
        throw std::runtime_error("--cert and --key are used only with --tls-listen");
    }
    for (const auto& file : config_files) read_config_file(file, options);
    if (!config_files.empty()) options.config_base = fs::absolute(config_files.back()).parent_path();
    for (const auto& text : config_values) {
        const auto equals = text.find('=');
        if (equals == std::string::npos || equals == 0) throw std::runtime_error("--config expects key=value");
        options.config[text.substr(0, equals)] = text.substr(equals + 1);
    }
    return options;
}

// ---------------------------------------------------------------------------
// Workers

fs::path host_executable(const char* argv0) {
#ifdef __linux__
    std::error_code error;
    auto path = fs::read_symlink("/proc/self/exe", error);
    if (!error) return path;
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) return fs::canonical(buffer.c_str());
#elif defined(_WIN32)
    // The module's own path (GetModuleFileNameW): argv[0] may lack the
    // directory and the .exe that CreateProcessW needs.
    std::error_code error;
    auto path = neura::platform::executable_path(error);
    if (!error && !path.empty()) return path;
#endif
    return fs::absolute(argv0);
}

// A path in a worker's command line.
std::string argument_path(const fs::path& path) {
#ifdef _WIN32
    return neura::platform::native_utf8_path(path);
#else
    return path.string();
#endif
}

#ifdef _WIN32
// A worker's process: its identifier, and its handle, which the host's set
// of live workers owns and closes only once the worker has been reaped.
struct Worker {
    worker_pid pid = 0;
    HANDLE handle = nullptr;
    std::unique_ptr<Channel> channel;
};
#else
struct Worker {
    pid_t pid = -1;
    std::unique_ptr<Channel> channel;
};
#endif

std::atomic<bool> draining{false};

class Host {
public:
    Host(HostOptions options, fs::path executable, SqlHost& sql)
        : options_(std::move(options)), executable_(std::move(executable)), sql_(sql) {
        const auto bytes = neura::runner::read_file(options_.image);
        neura::util::Sha256 digest;
        digest.update(std::span<const std::uint8_t>(bytes));
        image_digest_ = digest.finish();
        nvr_check(bytes);
        std::random_device device;
        id_prefix_ = (static_cast<std::uint64_t>(device()) << 32) | device();
        if (const char* text = std::getenv("NEURA_SERVER_TEST_REFUSE_ASSIGN")) {
            refusals_left_ = std::strtol(text, nullptr, 10);
        }
        worker_arguments_ = worker_arguments();
#ifdef _WIN32
        job_ = create_worker_job();
        // WSAPoll waits on sockets only, so the reaper's wake-up is a socket
        // pair, not a pipe.
        std::error_code error;
        const auto pair = neura::platform::socket_pair(error);
        if (error) {
            throw std::runtime_error("cannot create the reaper's wake sockets: " + neura::platform::error_message(error));
        }
        wake_[0] = pair[0];
        wake_[1] = pair[1];
        for (const auto end : wake_) (void)neura::platform::set_nonblocking(end);
#else
        if (options_.zygote) start_zygote();
        if (::pipe(wake_) != 0) throw std::runtime_error("cannot create the reaper's wake pipe");
        for (const int end : wake_) {
            ::fcntl(end, F_SETFD, FD_CLOEXEC);
            ::fcntl(end, F_SETFL, ::fcntl(end, F_GETFL) | O_NONBLOCK);
        }
#endif
        reaper_ = std::jthread([this](std::stop_token stop) { reap(stop); });
        if (options_.ready_workers > 0) spawner_ = std::jthread([this](std::stop_token stop) { keep_ready(stop); });
    }

    ~Host() {
        spawner_.request_stop();
        ready_changed_.notify_all();
        if (spawner_.joinable()) spawner_.join();
        {
            std::lock_guard lock(ready_mutex_);
            ready_.clear(); // Closing the channels ends the ready workers.
        }
        reaper_.request_stop();
        wake_reaper();
        if (reaper_.joinable()) reaper_.join();
#ifdef _WIN32
        // Closing the job ends the workers that remain (the ready ones the
        // reaper never had); the wake sockets go after the reaper.
        {
            std::lock_guard lock(pids_mutex_);
            live_.clear();
        }
        if (job_ != nullptr) ::CloseHandle(job_);
        for (const auto end : wake_) (void)neura::platform::close_socket(end);
#else
        std::lock_guard lock(zygote_mutex_);
        zygote_.reset();
#endif
    }

    // The startup check (Backend030): one worker runs `serve`, which answers
    // the check without calling a route. True when it answered below 400
    // and exited with status 0.
    bool check_startup() {
        auto worker = spawn();
        if (!worker) return false;
        neura::nvm::ServerRequest request;
        request.kind = neura::nvm::ServerRequest::startup_check;
        request.request_id = next_id();
        request.method = "GET";
        request.target = "/";
        request.scheme = "http";
        request.peer = "127.0.0.1";
        const auto timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(options_.limits.http.request_timeout);
        request.deadline = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count() + timeout.count();
        RecordingWriter writer;
        const auto outcome = exchange(*worker, request, nullptr, writer, Clock::now() + options_.limits.http.request_timeout);
#ifdef _WIN32
        worker->channel.reset();
        const auto status = reap_now(*worker);
        const bool exited = status.success();
#else
        const pid_t pid = worker->pid;
        worker->channel.reset();
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        {
            std::lock_guard lock(pids_mutex_);
            live_.erase(pid);
        }
        const bool exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
        if (!outcome.done || writer.status >= 400 || !exited) {
            log(4, request.request_id, "startup check failed",
                {{"status", std::to_string(writer.status)}, {"worker_exit", describe_exit(status)}});
            return false;
        }
        ready_for_requests_ = true;
        return true;
    }

    // The responder of one connection, called at the end of a request's
    // head. The worker is assigned at once and the body streams to it; S-8
    // runs from the end of the head. The response streams back to the client
    // as the worker produces it, after the request body has been received.
    void handle(const http::RequestHead& head, http::RequestBody& body, http::ResponseWriter& writer,
                http::ConnectionClock::time_point head_deadline, const std::string& peer, bool secure) {
        // Paired with response_written(), which the connection calls once
        // this response has been written or has failed.
        ++in_flight_;
        const auto deadline = head_deadline - 50ms;
        if (draining.load() || !admit(deadline)) {
            count_response(503);
            ++metrics_.rejected;
            write_buffered(writer, unavailable());
            return;
        }
        // The S-1 slot is held until the request's worker has been reaped:
        // the reaper releases it then. Without a worker it is released here.
        bool slot_held = true;
        struct Release {
            Host& host;
            bool& held;
            ~Release() {
                if (held) host.release();
            }
        } release{*this, slot_held};

        neura::nvm::ServerRequest request;
        request.request_id = next_id();
        request.method = head.method;
        request.target = head.target;
        for (const auto& header : head.headers) request.headers.emplace_back(header.name, header.value);
        request.body_length = head.framing == http::BodyFraming::ContentLength ? static_cast<std::int64_t>(head.content_length)
                              : head.framing == http::BodyFraming::Chunked     ? -1
                                                                               : 0;
        request.peer = peer;
        request.scheme = secure ? "https" : "http";
        const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now());
        request.deadline = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count() + left.count();

        // A worker that provably ran nothing (its channel refused the
        // Assign) is replaced once; no request is run twice. The refused
        // worker is killed and reaped before its replacement starts, so the
        // slot never stands for two processes.
        Outcome outcome;
        std::optional<Worker> worker;
        // An HTTP/2 client that resets the stream cancels the request: the
        // writer calls this, and the worker is killed at once.
        std::atomic<bool> cancelled{false};
        for (int attempt = 0; attempt < 2; ++attempt) {
            worker = attempt == 0 ? take_worker() : spawn();
            if (!worker) break;
#ifdef _WIN32
            writer.on_cancel([this, pid = worker->pid, &cancelled] {
                cancelled = true;
                kill_worker(pid);
            });
#else
            writer.on_cancel([pid = worker->pid, &cancelled] {
                cancelled = true;
                ::kill(pid, SIGKILL);
            });
#endif
            outcome = exchange(*worker, request, &body, writer, deadline);
            writer.on_cancel({});
            // A request the client cancelled is not given a new worker.
            if (!outcome.unassigned || cancelled.load()) break;
            log(attempt == 0 ? 3 : 4, request.request_id,
                attempt == 0 ? "a worker refused its assignment; using a new one"
                             : "the new worker refused its assignment too");
            if (attempt == 0) ++metrics_.retried;
            discard(*worker);
            worker.reset();
        }
        if (!worker) {
            log(4, request.request_id, "could not start a request worker");
            count_response(500);
            if (!writer.started()) write_buffered(writer, error_response(500));
            return;
        }
        const worker_pid pid = worker->pid;
        // A worker past its deadline, one that broke the protocol, one whose
        // client or request body failed, is killed; one that closed its
        // channel is left its grace period to exit.
        if (outcome.timed_out || outcome.body_failed || outcome.client_failed || outcome.too_large ||
            worker->channel->status() == Channel::Status::Failed) {
#ifdef _WIN32
            kill_worker(pid);
#else
            ::kill(pid, SIGKILL);
#endif
        }
        worker->channel.reset();
        hand_to_reaper(pid, request.request_id, outcome.done, true);
        slot_held = false;
        if (cancelled.load()) {
            log(3, request.request_id, "the client cancelled the request; worker killed");
            return;
        }
        if (outcome.timed_out) {
            ++metrics_.timeouts;
            log(4, request.request_id, "request deadline passed; worker killed");
        } else if (outcome.too_large) {
            log(4, request.request_id, "response body exceeds S-7");
        } else if (outcome.body_failed) {
            log(3, request.request_id, "the request body could not be read; worker killed");
        } else if (outcome.client_failed) {
            log(3, request.request_id, "the client stopped receiving the response; worker killed");
        } else if (!outcome.done) {
            log(4, request.request_id, writer.started() ? "worker ended inside its response; connection closed"
                                                        : "worker ended without a response");
        }
        if (writer.started()) {
            count_response(outcome.done ? static_cast<int>(outcome.status) : 500);
            return;
        }
        if (outcome.body_failed) return; // The connection answers the body's failure.
        const int status = outcome.timed_out ? 504 : 500;
        count_response(status);
        write_buffered(writer, error_response(status));
    }

    // GET /livez, /readyz and /metrics on the admin listener.
    http::BufferedResponse admin(const http::RequestHead& head) {
        http::BufferedResponse response;
        response.headers.push_back({"Content-Type", "text/plain; charset=utf-8"});
        response.headers.push_back({"Cache-Control", "no-store"});
        const auto path = std::string_view(head.target).substr(0, head.target.find('?'));
        if (head.method != "GET" && head.method != "HEAD") {
            response.status = 405;
            response.headers.push_back({"Allow", "GET, HEAD"});
            return response;
        }
        if (path == "/livez") {
            response.body = "ok\n";
        } else if (path == "/readyz") {
            const bool ready = ready_for_requests_.load() && !draining.load();
            response.status = ready ? 200 : 503;
            response.body = ready ? "ready\n" : (draining.load() ? "draining\n" : "starting\n");
        } else if (path == "/metrics") {
            response.headers[0].value = "text/plain; version=0.0.4; charset=utf-8";
            response.body = metrics_text();
        } else {
            response.status = 404;
            response.body = "not found\n";
        }
        return response;
    }

    void mark_ready() { ready_for_requests_ = true; }

    void log(std::uint32_t level, std::string_view id, std::string_view message,
             const neura::nvm::HeaderPairs& fields = {}) {
        const auto line = json_log_line(level, id, message, fields);
        std::lock_guard lock(log_mutex_);
        std::cerr << line << std::flush;
    }

    std::size_t in_flight() const { return in_flight_.load(); }
    void response_written() { --in_flight_; }

    // S-1 slots in use. A request holds its slot from admission until its
    // worker has been reaped, so this is 0 only when every worker that took
    // a request has exited. Ready workers and the zygote hold none.
    std::size_t slots_held() {
        std::lock_guard lock(admission_mutex_);
        return running_;
    }

#ifdef _WIN32
    void kill_all() {
        std::lock_guard lock(pids_mutex_);
        for (auto& [pid, process] : live_) (void)process.terminate(kill_signal);
    }

    // Ends a worker as SIGKILL would: exit code 128 + 9, which its status
    // then describes as signal 9 (W10). A worker already reaped is left
    // alone; its handle is closed.
    void kill_worker(worker_pid pid) {
        std::lock_guard lock(pids_mutex_);
        if (const auto found = live_.find(pid); found != live_.end()) (void)found->second.terminate(kill_signal);
    }
#else
    void kill_all() {
        {
            std::lock_guard lock(pids_mutex_);
            for (const auto pid : live_) ::kill(pid, SIGKILL);
        }
        std::lock_guard lock(zygote_mutex_);
        if (zygote_) ::kill(zygote_->pid(), SIGKILL);
    }
#endif

private:
    struct Outcome {
        bool done = false;
        bool timed_out = false;
        bool too_large = false;
        bool unassigned = false;    // The channel ended before the Assign was taken.
        bool body_failed = false;   // The request body could not be read to its end.
        bool client_failed = false; // The response could not be written to the client.
        std::uint32_t status = 0;
    };

    // The response of a startup check, which goes to no client.
    struct RecordingWriter final : http::ResponseWriter {
        http::StreamStatus start(int code, const std::vector<http::Header>&, std::optional<std::uint64_t>) override {
            status = code;
            begun = true;
            return http::StreamStatus::Ready;
        }
        http::StreamStatus write(std::string_view) override { return http::StreamStatus::Ready; }
        http::StreamStatus finish() override { return http::StreamStatus::Ready; }
        bool started() const override { return begun; }
        int status = 0;
        bool begun = false;
    };

    static void write_buffered(http::ResponseWriter& writer, const http::BufferedResponse& response) {
        if (writer.start(response.status, response.headers, response.body.size()) != http::StreamStatus::Ready) return;
        if (writer.write(response.body) != http::StreamStatus::Ready) return;
        (void)writer.finish();
    }

    static void nvr_check(const std::vector<std::uint8_t>& bytes) {
        try {
            (void)neura::nvr::load(bytes);
        } catch (const neura::nvr::LoadError& error) {
            throw std::runtime_error(std::string("the image does not load: ") + error.what());
        }
    }

#ifdef _WIN32
    // "exited N", "signal N" (a worker the host ended) or "crashed 0x..."
    // (an NTSTATUS), W10.
    static std::string describe_exit(const neura::platform::ExitStatus& status) { return status.describe(); }
#else
    static std::string describe_exit(int status) {
        if (WIFEXITED(status)) return "status " + std::to_string(WEXITSTATUS(status));
        if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
        return "unknown";
    }
#endif

    std::string next_id() {
        const auto number = sequence_.fetch_add(1) + 1;
        char text[40];
        std::snprintf(text, sizeof text, "%016llx-%08llx", static_cast<unsigned long long>(id_prefix_),
                      static_cast<unsigned long long>(number));
        return text;
    }

    static http::BufferedResponse error_response(int status) {
        http::BufferedResponse response;
        response.status = status;
        response.headers.push_back({"Content-Type", "application/json"});
        response.body = status == 504 ? R"({"error":"timeout"})" : R"({"error":"internal"})";
        return response;
    }

    static http::BufferedResponse unavailable() {
        http::BufferedResponse response;
        response.status = 503;
        response.headers.push_back({"Retry-After", "1"});
        response.headers.push_back({"Content-Type", "application/json"});
        response.body = R"({"error":"unavailable"})";
        return response;
    }

    // S-1 running and S-3 waiting.
    bool admit(Clock::time_point deadline) {
        std::unique_lock lock(admission_mutex_);
        if (running_ < options_.max_workers) {
            ++running_;
            return true;
        }
        if (waiting_ >= options_.max_queue) return false;
        ++waiting_;
        const bool got = slot_free_.wait_until(lock, deadline,
                                               [&] { return running_ < options_.max_workers || draining.load(); });
        --waiting_;
        if (!got || running_ >= options_.max_workers) return false;
        ++running_;
        return true;
    }

    void release() {
        {
            std::lock_guard lock(admission_mutex_);
            --running_;
        }
        slot_free_.notify_one();
    }

    // The worker's options after --worker or --zygote, without the image.
    std::vector<std::string> worker_arguments() const {
        std::vector<std::string> arguments;
        const auto& caps = options_.capabilities;
        if (caps.fs) {
            std::string option = "--allow-fs";
            for (std::size_t i = 0; i < caps.fs_prefixes.size(); ++i) {
                option += (i == 0 ? "=" : ",") + caps.fs_prefixes[i];
            }
            arguments.push_back(option);
        }
        if (caps.net) arguments.emplace_back("--allow-net");
        if (caps.http_client) {
            arguments.push_back(neura::cli::http_client_option(
                caps.http_client_origins ? &*caps.http_client_origins : nullptr));
        }
        if (caps.env) arguments.emplace_back("--allow-env");
        if (caps.sql) arguments.emplace_back("--allow-sql");
        if (options_.record_directory) arguments.push_back("--record-dir=" + argument_path(*options_.record_directory));
        arguments.push_back("--max-response-bytes=" + std::to_string(options_.limits.http.response_bytes));
        if (options_.max_steps) arguments.push_back("--max-steps=" + std::to_string(*options_.max_steps));
        if (options_.worker_tier) arguments.push_back("--tier=" + std::to_string(*options_.worker_tier));
        if (options_.worker_jit) arguments.push_back("--jit=" + *options_.worker_jit);
        arguments.push_back(argument_path(options_.image));
        return arguments;
    }

    // The test hook NEURA_SERVER_TEST_REFUSE_ASSIGN=N: the first N workers
    // started refuse their Assign (WorkerOptions::refuse_assign).
    bool take_refusal() {
        auto left = refusals_left_.load();
        while (left > 0 && !refusals_left_.compare_exchange_weak(left, left - 1)) {}
        return left > 0;
    }

#ifdef _WIN32
    // The job of every worker (W27): it ends them when its last handle
    // closes, with the host however the host ends, and ends a worker that
    // raises an unhandled exception without an error report dialog.
    static HANDLE create_worker_job() {
        const HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
        if (job == nullptr) {
            throw std::runtime_error("cannot create the job of the request workers: " +
                                     neura::platform::error_message(neura::platform::last_error()));
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
        if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
            const auto code = neura::platform::last_error();
            ::CloseHandle(job);
            throw std::runtime_error("cannot configure the job of the request workers: " +
                                     neura::platform::error_message(code));
        }
        return job;
    }

    // Starts `neura-server-host --worker --channel=<socket>` with `channel`
    // (the worker's end) inherited through the handle list and nothing else
    // but the standard streams. The image's console output goes to the
    // host's standard error, as on POSIX. In a new process group, so a Ctrl+C
    // or Ctrl+Break the host is sent is not sent to its workers; they ignore
    // the console's own events too (read_worker_options). The worker joins
    // the job before it is used: one that cannot is ended and not used.
    std::optional<neura::platform::Process> exec_worker(neura::platform::native_socket channel, bool refuse) {
        namespace platform = neura::platform;
        std::vector<std::string> arguments{argument_path(executable_), "--worker",
                                           "--channel=" + std::to_string(channel)};
        if (refuse) arguments.emplace_back("--test-refuse-assign");
        arguments.insert(arguments.end(), worker_arguments_.begin(), worker_arguments_.end());
        platform::Spawn spawn;
        spawn.program = executable_;
        spawn.arguments = std::move(arguments);
        const HANDLE error_stream = ::GetStdHandle(STD_ERROR_HANDLE);
        spawn.output = error_stream == nullptr || error_stream == INVALID_HANDLE_VALUE ? platform::Stdio::null()
                                                                                       : platform::Stdio::from(error_stream);
        spawn.inherit.push_back(reinterpret_cast<platform::native_file>(channel));
        spawn.new_process_group = true;
        std::error_code error;
        auto process = platform::Process::start(spawn, error);
        if (!process) {
            // The reason is logged once; each request that then gets no
            // worker is answered 500 and logged by handle().
            if (!start_failure_logged_.exchange(true)) {
                log(4, {}, "cannot start a request worker", {{"reason", platform::error_message(error)}});
            }
            return std::nullopt;
        }
        if (!::AssignProcessToJobObject(job_, process.native_handle())) {
            const auto code = platform::last_error();
            (void)process.terminate(kill_signal);
            (void)process.wait();
            log(4, {}, "cannot place a request worker in the host's job; it was ended",
                {{"reason", platform::error_message(code)}});
            return std::nullopt;
        }
        return process;
    }

    std::optional<Worker> spawn() {
        namespace platform = neura::platform;
        std::error_code error;
        const auto pair = platform::socket_pair(error);
        if (error) return std::nullopt;
        const bool refuse = take_refusal();
        auto process = exec_worker(pair[1], refuse);
        (void)platform::close_socket(pair[1]);
        if (!process) {
            (void)platform::close_socket(pair[0]);
            return std::nullopt;
        }
        Worker worker{process->pid(), process->native_handle(), nullptr};
        {
            std::lock_guard lock(pids_mutex_);
            live_.emplace(worker.pid, std::move(*process));
        }
        ++metrics_.starts;
        worker.channel = std::make_unique<Channel>(pair[0], worker::PeerRole::Host, worker::server_identity(image_digest_));
        worker::Hello hello;
        hello.role = worker::PeerRole::Host;
        hello.identity = worker::server_identity(image_digest_);
        if (worker.channel->greet(hello, Clock::now() + 10s) != Channel::Status::Ok) {
            log(4, {}, "request worker failed its greeting");
            discard(worker);
            return std::nullopt;
        }
        return worker;
    }

    // Waits for a worker the reaper does not watch and forgets it: its
    // handle is closed and its status returned. A status that cannot be read
    // counts as exited 0, as waitpid's failure does on POSIX.
    neura::platform::ExitStatus reap_now(const Worker& worker) {
        (void)::WaitForSingleObject(worker.handle, INFINITE);
        std::lock_guard lock(pids_mutex_);
        const auto found = live_.find(worker.pid);
        if (found == live_.end()) return neura::platform::ExitStatus::exited(0);
        std::error_code error;
        const auto status = found->second.wait(std::chrono::milliseconds{0}, error);
        live_.erase(found);
        return status.value_or(neura::platform::ExitStatus::exited(0));
    }

    // Kills a worker that ran nothing and waits for it, here: it holds no
    // slot of its own, so it is gone before anything replaces it.
    void discard(Worker& worker) {
        kill_worker(worker.pid);
        worker.channel.reset();
        (void)reap_now(worker);
        ++metrics_.abnormal;
    }

    // Whether a socket is readable now: 1, 0, or -1 on an error.
    static int poll_now(neura::platform::native_socket socket) {
        pollfd entry{socket, POLLIN, 0};
        return ::WSAPoll(&entry, 1, 0);
    }
#else
    // Called with zygote_mutex_ held, or before any other thread runs.
    void start_zygote() {
        std::string problem;
        zygote_ = ZygoteClient::start(executable_, worker_arguments_, problem);
        if (!zygote_) log(4, {}, "the worker zygote did not start; starting workers with exec", {{"reason", problem}});
    }

    // A worker forked by the zygote, or -1. A zygote that has ended is
    // replaced, up to three times; after that, or when a new one does not
    // start, workers are started with exec.
    pid_t fork_from_zygote(int channel, bool refuse) {
        std::lock_guard lock(zygote_mutex_);
        const unsigned char flags = refuse ? ZygoteClient::refuse_assign_flag : 0;
        for (;;) {
            if (!zygote_) return -1;
            const pid_t pid = zygote_->fork_worker(channel, flags);
            if (pid > 0 || !zygote_->failed()) return pid;
            zygote_.reset();
            if (++zygote_restarts_ > 3) {
                log(4, {}, "the worker zygote ended again; starting workers with exec from now on");
                return -1;
            }
            log(4, {}, "the worker zygote ended; starting a new one");
            start_zygote();
        }
    }

    pid_t exec_worker(int channel, bool refuse) {
        std::vector<std::string> arguments{executable_.string(), "--worker"};
        if (refuse) arguments.emplace_back("--test-refuse-assign");
        arguments.insert(arguments.end(), worker_arguments_.begin(), worker_arguments_.end());
        std::vector<char*> argv;
        for (auto& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, channel, worker_channel_descriptor);
        // The image's console output goes to the host's standard error.
        posix_spawn_file_actions_adddup2(&actions, 2, 1);
        pid_t pid = -1;
        const int error = ::posix_spawn(&pid, executable_.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        return error == 0 ? pid : -1;
    }

    std::optional<Worker> spawn() {
        int pair[2];
#ifdef SOCK_CLOEXEC
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair) != 0) return std::nullopt;
#else
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) return std::nullopt;
        ::fcntl(pair[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(pair[1], F_SETFD, FD_CLOEXEC);
#endif
        // dup2 onto the channel descriptor clears close-on-exec, except when
        // the descriptor is already that number.
        if (pair[1] == worker_channel_descriptor) {
            const int moved = ::fcntl(pair[1], F_DUPFD_CLOEXEC, worker_channel_descriptor + 1);
            ::close(pair[1]);
            pair[1] = moved;
        }
        const bool refuse = take_refusal();
        pid_t pid = fork_from_zygote(pair[1], refuse);
        if (pid < 0) pid = exec_worker(pair[1], refuse);
        ::close(pair[1]);
        if (pid < 0) {
            ::close(pair[0]);
            return std::nullopt;
        }
        {
            std::lock_guard lock(pids_mutex_);
            live_.insert(pid);
        }
        ++metrics_.starts;
        Worker worker{pid, std::make_unique<Channel>(pair[0], worker::PeerRole::Host, worker::server_identity(image_digest_))};
        worker::Hello hello;
        hello.role = worker::PeerRole::Host;
        hello.identity = worker::server_identity(image_digest_);
        if (worker.channel->greet(hello, Clock::now() + 10s) != Channel::Status::Ok) {
            log(4, {}, "request worker failed its greeting");
            discard(worker);
            return std::nullopt;
        }
        return worker;
    }

    // Kills a worker that ran nothing and waits for it, here: it holds no
    // slot of its own, so it is gone before anything replaces it.
    void discard(Worker& worker) {
        ::kill(worker.pid, SIGKILL);
        worker.channel.reset();
        int status = 0;
        while (::waitpid(worker.pid, &status, 0) < 0 && errno == EINTR) {}
        {
            std::lock_guard lock(pids_mutex_);
            live_.erase(worker.pid);
        }
        ++metrics_.abnormal;
    }

    // Whether a descriptor is readable now: 1, 0, or -1 on an error.
    static int poll_now(int descriptor) {
        pollfd entry{descriptor, POLLIN, 0};
        return ::poll(&entry, 1, 0);
    }
#endif

    // A ready worker, or a new one. A ready worker whose channel is readable
    // has ended (it sends nothing after its greeting) and is discarded.
    std::optional<Worker> take_worker() {
        for (;;) {
            std::optional<Worker> worker;
            {
                std::lock_guard lock(ready_mutex_);
                if (!ready_.empty()) {
                    worker = std::move(ready_.front());
                    ready_.pop_front();
                }
            }
            ready_changed_.notify_all();
            if (!worker) return spawn();
            if (poll_now(worker->channel->descriptor()) == 0) return worker;
            log(3, {}, "a ready worker had ended; discarding it");
            ++metrics_.retried;
            discard(*worker);
        }
    }

    // Keeps S-2 greeted workers that have executed nothing.
    void keep_ready(std::stop_token stop) {
        for (;;) {
            {
                std::unique_lock lock(ready_mutex_);
                ready_changed_.wait(lock, [&] {
                    return stop.stop_requested() || draining.load() || ready_.size() < options_.ready_workers;
                });
                if (stop.stop_requested() || draining.load()) return;
            }
            auto worker = spawn();
            if (!worker) {
                std::this_thread::sleep_for(100ms);
                continue;
            }
            std::lock_guard lock(ready_mutex_);
            ready_.push_back(std::move(*worker));
        }
    }

    // Sends the assignment, then the request body as the client sends it,
    // then forwards the response to `writer` frame by frame, answering
    // calls, until Done, the deadline or the end of the channel.
    Outcome exchange(Worker& worker, const neura::nvm::ServerRequest& request, http::RequestBody* body,
                     http::ResponseWriter& writer, Clock::time_point deadline) {
        Outcome outcome;
        auto& channel = *worker.channel;
        // A call answered at or past the deadline (a statement the deadline
        // interrupted) ends the request as timed out, in either phase. So
        // does a channel that fails once the deadline has passed: the worker,
        // whose own wait for the call's Return ends at the same deadline, may
        // have answered and exited before the interrupted statement returned.
        bool deadline_ended = false;
        const auto failed = [&] {
            outcome.timed_out = deadline_ended || channel.status() == Channel::Status::TimedOut ||
                                Clock::now() >= deadline;
            return outcome;
        };
        // The request's SQL leases; leaving this function, by any path,
        // rolls back and returns them.
        const auto sql_session = sql_.session(deadline);
        const auto reply = [&](const std::vector<std::uint8_t>& call) {
            return is_sql_call(call) ? SqlHost::answer(sql_session.get(), call) : answer_call(call);
        };
        const auto assign = worker::encode_assign(request);
        if (channel.send(FrameKind::Assign, assign, deadline) != Channel::Status::Ok) {
            outcome.unassigned = channel.status() != Channel::Status::TimedOut;
            return failed();
        }
        // While the body streams, the worker's calls are answered as they
        // arrive; its first response frame waits until the body has ended.
        std::optional<worker::Frame> stashed;
        // Answers the calls that have arrived; false when the channel ended.
        const auto answer_ready_calls = [&] {
            while (!stashed) {
                auto frame = channel.receive_ready();
                if (!frame) return channel.status() == Channel::Status::Ok;
                if (frame->kind == FrameKind::Call) {
                    if (channel.send(FrameKind::Return, reply(frame->body), deadline) != Channel::Status::Ok) {
                        return false;
                    }
                    if (Clock::now() >= deadline) {
                        deadline_ended = true;
                        return false;
                    }
                    continue;
                }
                stashed = std::move(frame);
                body->wake_on(http::no_wake);
            }
            return true;
        };
        const auto channel_readable = [&] { return poll_now(channel.descriptor()) > 0; };
        if (body) body->wake_on(channel.descriptor());
        while (body) {
            const auto piece = body->read();
            if (piece.status == http::RequestBody::Status::End) break;
            if (piece.status == http::RequestBody::Status::Woken) {
                if (!answer_ready_calls()) return failed();
                continue;
            }
            if (piece.status != http::RequestBody::Status::Data) {
                outcome.body_failed = true;
                return outcome;
            }
            for (auto rest = piece.data; !rest.empty();) {
                const auto size = std::min(rest.size(), worker::max_body_chunk_bytes);
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(rest.data());
                if (channel.send(FrameKind::BodyChunk, std::span(bytes, size), deadline) != Channel::Status::Ok) {
                    return failed();
                }
                rest.remove_prefix(size);
            }
            // A client that keeps sending never lets read() wait, so calls are
            // also answered between pieces.
            if (!stashed && channel_readable() && !answer_ready_calls()) return failed();
        }
        if (body) body->wake_on(http::no_wake);
        if (channel.send(FrameKind::BodyEnd, {}, deadline) != Channel::Status::Ok) return failed();
        bool started = false;
        for (;;) {
            auto frame = stashed ? std::move(stashed) : channel.receive(deadline);
            stashed.reset();
            if (!frame) {
                if (channel.status() == Channel::Status::Failed) {
                    log(4, request.request_id, "request worker broke the protocol");
                }
                return failed();
            }
            switch (frame->kind) {
            case FrameKind::Head: {
                auto head = worker::decode_head(frame->body);
                if (!head || started) return outcome;
                if (head->body_length > static_cast<std::int64_t>(options_.limits.http.response_bytes)) {
                    outcome.too_large = true;
                    return outcome;
                }
                std::vector<http::Header> fields;
                fields.reserve(head->headers.size());
                for (auto& [name, value] : head->headers) fields.push_back({std::move(name), std::move(value)});
                const auto length = head->body_length >= 0
                    ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(head->body_length)) : std::nullopt;
                started = true;
                outcome.status = head->status;
                if (writer.start(static_cast<int>(head->status), fields, length) != http::StreamStatus::Ready) {
                    outcome.client_failed = true;
                    return outcome;
                }
                break;
            }
            case FrameKind::BodyChunk:
                if (!started) return outcome;
                if (writer.write(std::string_view(reinterpret_cast<const char*>(frame->body.data()),
                                                  frame->body.size())) != http::StreamStatus::Ready) {
                    outcome.client_failed = true;
                    return outcome;
                }
                break;
            case FrameKind::Call:
                if (channel.send(FrameKind::Return, reply(frame->body), deadline) != Channel::Status::Ok) {
                    return failed();
                }
                // A statement the deadline interrupted: the request is over.
                if (Clock::now() >= deadline) {
                    outcome.timed_out = true;
                    return outcome;
                }
                break;
            case FrameKind::Done:
                if (!started) return outcome;
                if (writer.finish() != http::StreamStatus::Ready) {
                    outcome.client_failed = true;
                    return outcome;
                }
                outcome.done = true;
                return outcome;
            default:
                break;
            }
        }
    }

    // A call of the worker: operation 1, a configuration lookup (the file and
    // command-line values, overridden by the environment variable
    // <prefix><key>); operation 2, a metric.
    std::vector<std::uint8_t> answer_call(const std::vector<std::uint8_t>& body) {
        const auto get = [&](std::size_t at) {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(body[at + i]) << (8 * i);
            return value;
        };
        std::vector<std::uint8_t> out;
        out.reserve(64);
        const auto put = [&](std::uint32_t value) {
            for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        };
        const std::uint32_t call = body.size() >= 4 ? get(0) : 0;
        put(call);
        const std::uint32_t operation = body.size() >= 8 ? get(4) : 0;
        if (operation == metric_operation) {
            // uint32 kind, int64 value, text name.
            if (body.size() < 24 || body.size() != 24 + std::size_t{get(20)}) {
                put(neura::nvm::host_error::invalid_input);
                return out;
            }
            const auto kind = get(8);
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; ++i) bits |= static_cast<std::uint64_t>(body[12 + i]) << (8 * i);
            const auto value = static_cast<std::int64_t>(bits);
            const std::string name(reinterpret_cast<const char*>(body.data()) + 24, get(20));
            if (!neura::nvm::valid_metric_name(name) || (kind != 1 && kind != 2) || (kind == 1 && value < 0)) {
                put(neura::nvm::host_error::invalid_input);
                return out;
            }
            std::lock_guard lock(metrics_mutex_);
            auto& entry = application_metrics_[name];
            if (entry.kind != 0 && entry.kind != kind) {
                put(neura::nvm::host_error::invalid_input);
                return out;
            }
            if (application_metrics_.size() > max_application_metrics) {
                application_metrics_.erase(name);
                put(neura::nvm::host_error::too_large);
                return out;
            }
            entry.kind = kind;
            entry.value = kind == 1 ? entry.value + value : value;
            put(0);
            return out;
        }
        if (body.size() < 12 || operation != config_operation || body.size() != 12 + std::size_t{get(8)}) {
            put(neura::nvm::host_error::invalid_input);
            out.push_back(0);
            put(0);
            return out;
        }
        const std::string key(reinterpret_cast<const char*>(body.data()) + 12, get(8));
        std::optional<std::string> value;
        // SQL settings and passwords stay in the host.
        if (key.starts_with("sql.")) {
            put(0);
            out.push_back(0);
            put(0);
            return out;
        }
        if (options_.env_prefix) {
#ifdef _WIN32
            // GetEnvironmentVariableW (W18): names match without case, and
            // a value that is not text is left out.
            std::error_code error;
            value = neura::platform::get_env(*options_.env_prefix + key, error);
#else
            if (const char* text = std::getenv((*options_.env_prefix + key).c_str())) value = text;
#endif
        }
        if (!value) {
            if (const auto found = options_.config.find(key); found != options_.config.end()) value = found->second;
        }
        put(0);
        out.push_back(value ? 1 : 0);
        put(value ? static_cast<std::uint32_t>(value->size()) : 0);
        if (value) {
            for (const char c : *value) out.push_back(static_cast<std::uint8_t>(c));
        }
        return out;
    }

    void count_response(int status) {
        const auto index = static_cast<std::size_t>(std::clamp(status / 100, 1, 5) - 1);
        ++metrics_.responses[index];
    }

    // The Prometheus text exposition of the host's counters and the
    // application's metrics.
    std::string metrics_text() {
        std::string text;
        const auto add = [&](std::string_view name, std::string_view type, std::string_view help,
                             const std::vector<std::pair<std::string, std::uint64_t>>& samples) {
            text += "# HELP ";
            text += name;
            text += ' ';
            text += help;
            text += "\n# TYPE ";
            text += name;
            text += ' ';
            text += type;
            text += '\n';
            for (const auto& [labels, value] : samples) {
                text += name;
                text += labels;
                text += ' ';
                text += std::to_string(value);
                text += '\n';
            }
        };
        std::vector<std::pair<std::string, std::uint64_t>> classes;
        for (std::size_t i = 0; i < metrics_.responses.size(); ++i) {
            classes.emplace_back("{class=\"" + std::to_string(i + 1) + "xx\"}", metrics_.responses[i].load());
        }
        add("neura_http_responses_total", "counter", "Responses by status class.", classes);
        add("neura_http_rejected_total", "counter", "Requests answered 503 by admission (S-1, S-3) or the drain.",
            {{"", metrics_.rejected.load()}});
        add("neura_http_timeouts_total", "counter", "Requests past their deadline (S-8).", {{"", metrics_.timeouts.load()}});
        add("neura_worker_starts_total", "counter", "Request workers started.", {{"", metrics_.starts.load()}});
        add("neura_worker_abnormal_exits_total", "counter", "Workers that trapped, failed or were killed.",
            {{"", metrics_.abnormal.load()}});
        add("neura_worker_retries_total", "counter", "Requests given a new worker because the ready one had ended.",
            {{"", metrics_.retried.load()}});
        std::size_t running = 0;
        std::size_t waiting = 0;
        {
            std::lock_guard lock(admission_mutex_);
            running = running_;
            waiting = waiting_;
        }
        std::size_t ready = 0;
        {
            std::lock_guard lock(ready_mutex_);
            ready = ready_.size();
        }
        add("neura_http_in_flight", "gauge", "Requests being answered.", {{"", in_flight_.load()}});
        add("neura_workers_running", "gauge", "Worker slots in use (S-1): a slot is held until its worker is reaped.",
            {{"", running}});
        add("neura_workers_ready", "gauge", "Started workers waiting for a request (S-2).", {{"", ready}});
        add("neura_requests_waiting", "gauge", "Requests waiting for a worker (S-3).", {{"", waiting}});
        add("neura_ready", "gauge", "1 after the startup check while not draining.",
            {{"", ready_for_requests_.load() && !draining.load() ? 1u : 0u}});
        std::lock_guard lock(metrics_mutex_);
        for (const auto& [name, entry] : application_metrics_) {
            text += "# TYPE " + name + (entry.kind == 1 ? " counter\n" : " gauge\n");
            text += name + ' ' + std::to_string(entry.value) + '\n';
        }
        return text;
    }

#ifdef _WIN32
    struct Exiting {
        worker_pid pid;
        std::string id;
        bool answered;
        bool slot; // Holds an S-1 slot until it is reaped.
        Clock::time_point kill_at;
        // The thread pool's wait for the worker's exit, which wakes the
        // reaper; null when it could not be registered (the reaper then
        // looks every 2 ms).
        HANDLE wait = nullptr;
        bool killed = false;
    };

    void hand_to_reaper(worker_pid pid, std::string id, bool answered, bool slot) {
        {
            std::lock_guard lock(reap_mutex_);
            exiting_.push_back({pid, std::move(id), answered, slot, Clock::now() + options_.grace});
        }

        wake_reaper();
    }

    void wake_reaper() {
        const char byte = 1;
        (void)::send(wake_[1], &byte, 1, 0); // A full socket already wakes it.
    }

    // Runs on a thread of the pool when a watched worker has exited.
    static void CALLBACK worker_exited(void* context, BOOLEAN timed_out) {
        (void)timed_out;
        static_cast<Host*>(context)->wake_reaper();
    }

    void watch(Exiting& entry) {
        std::lock_guard lock(pids_mutex_);
        const auto found = live_.find(entry.pid);
        if (found == live_.end() ||
            !::RegisterWaitForSingleObject(&entry.wait, found->second.native_handle(), worker_exited, this, INFINITE,
                                           WT_EXECUTEONLYONCE)) {
            entry.wait = nullptr;
        }
    }

    // Waits for every finished worker as it exits (its wait callback wakes
    // the reaper), releases the S-1 slot it held, and logs traps and
    // failures. A worker that does not exit within the grace period is
    // killed. The workers' handles are not waited on here: WSAPoll waits on
    // sockets only, and a WaitForMultipleObjects would be limited to 64.
    void reap(std::stop_token stop) {
        std::vector<Exiting> pending;
        for (;;) {
            {
                std::lock_guard lock(reap_mutex_);
                for (auto& entry : exiting_) {
                    watch(entry);
                    pending.push_back(std::move(entry));
                }
                exiting_.clear();
            }
            const bool stopping = stop.stop_requested();
            if (stopping && pending.empty()) return;
            const auto now = Clock::now();
            std::erase_if(pending, [&](Exiting& entry) {
                if (!finish(entry)) {
                    if (!entry.killed && (stopping || now >= entry.kill_at)) {
                        kill_worker(entry.pid);
                        entry.killed = true;
                    }
                    return false;
                }
                return true;
            });
            auto next = now + 1s;
            bool unwatched = false;
            for (const auto& entry : pending) {
                if (entry.wait == nullptr) unwatched = true;
                if (!entry.killed) next = std::min(next, entry.kill_at);
            }
            auto timeout = std::chrono::ceil<std::chrono::milliseconds>(next - Clock::now()).count();
            if (unwatched) timeout = std::min<long long>(timeout, 2);
            pollfd watched{wake_[0], POLLIN, 0};
            (void)::WSAPoll(&watched, 1, static_cast<int>(std::max<long long>(0, timeout)));
            if (watched.revents != 0) {
                char drain[64];
                while (::recv(wake_[0], drain, sizeof drain, 0) > 0) {}
            }
        }
    }

    // Reaps the worker if it has exited: true when it is gone. Its wait is
    // unregistered (after its callback, if that runs) before its handle
    // closes.
    bool finish(Exiting& entry) {
        std::optional<neura::platform::ExitStatus> status;
        {
            // With the set locked, kill_all never uses a closed handle.
            std::lock_guard lock(pids_mutex_);
            const auto found = live_.find(entry.pid);
            if (found != live_.end()) {
                std::error_code error;
                status = found->second.wait(std::chrono::milliseconds{0}, error);
                if (!status && !error) return false;
            }
            if (entry.wait != nullptr) {
                (void)::UnregisterWaitEx(entry.wait, INVALID_HANDLE_VALUE);
                entry.wait = nullptr;
            }
            if (found != live_.end()) live_.erase(found);
        }
        if (entry.slot) release();
        const bool abnormal = status && !status->success();
        if (abnormal) ++metrics_.abnormal;
        if (abnormal && !entry.id.empty()) {
            log(entry.answered ? 3 : 4, entry.id, "request worker ended abnormally", {{"worker_exit", describe_exit(*status)}});
        }
        return true;
    }
#else
    struct Exiting {
        pid_t pid;
        std::string id;
        bool answered;
        bool slot; // Holds an S-1 slot until it is reaped.
        Clock::time_point kill_at;
        int pidfd = -1;
        bool killed = false;
    };

    void hand_to_reaper(pid_t pid, std::string id, bool answered, bool slot) {
        {
            std::lock_guard lock(reap_mutex_);
            exiting_.push_back({pid, std::move(id), answered, slot, Clock::now() + options_.grace});
        }

        wake_reaper();
    }

    void wake_reaper() {
        const char byte = 1;
        (void)!::write(wake_[1], &byte, 1); // A full pipe already wakes it.
    }

    // A descriptor that becomes readable when `pid` exits, or -1 where the
    // system has none (the reaper then looks every 2 ms).
    static int open_pidfd(pid_t pid) {
#if defined(__linux__) && defined(SYS_pidfd_open)
        const long descriptor = ::syscall(SYS_pidfd_open, pid, 0);
        if (descriptor >= 0) ::fcntl(static_cast<int>(descriptor), F_SETFD, FD_CLOEXEC);
        return descriptor >= 0 ? static_cast<int>(descriptor) : -1;
#else
        (void)pid;
        return -1;
#endif
    }

    // Waits for every finished worker as it exits (on its pidfd), releases
    // the S-1 slot it held, and logs traps and failures. A worker that does
    // not exit within the grace period is killed.
    void reap(std::stop_token stop) {
        std::vector<Exiting> pending;
        std::vector<pollfd> watched;
        for (;;) {
            {
                std::lock_guard lock(reap_mutex_);
                for (auto& entry : exiting_) {
                    entry.pidfd = open_pidfd(entry.pid);
                    pending.push_back(std::move(entry));
                }
                exiting_.clear();
            }
            const bool stopping = stop.stop_requested();
            if (stopping && pending.empty()) return;
            const auto now = Clock::now();
            std::erase_if(pending, [&](Exiting& entry) {
                if (!finish(entry)) {
                    if (!entry.killed && (stopping || now >= entry.kill_at)) {
                        ::kill(entry.pid, SIGKILL);
                        entry.killed = true;
                    }
                    return false;
                }
                if (entry.pidfd >= 0) ::close(entry.pidfd);
                return true;
            });
            watched.assign(1, pollfd{wake_[0], POLLIN, 0});
            auto next = now + 1s;
            bool unwatched = false;
            for (const auto& entry : pending) {
                if (entry.pidfd >= 0) watched.push_back({entry.pidfd, POLLIN, 0});
                else unwatched = true;
                if (!entry.killed) next = std::min(next, entry.kill_at);
            }
            auto timeout = std::chrono::ceil<std::chrono::milliseconds>(next - Clock::now()).count();
            if (unwatched) timeout = std::min<long long>(timeout, 2);
            (void)::poll(watched.data(), watched.size(), static_cast<int>(std::max<long long>(0, timeout)));
            if (watched[0].revents != 0) {
                char drain[64];
                while (::read(wake_[0], drain, sizeof drain) > 0) {}
            }
        }
    }

    // Reaps the worker if it has exited: true when it is gone.
    bool finish(const Exiting& entry) {
        int status = 0;
        pid_t done = 0;
        {
            // With the pid set locked, kill_all never signals a reaped pid.
            std::lock_guard lock(pids_mutex_);
            do {
                done = ::waitpid(entry.pid, &status, WNOHANG);
            } while (done < 0 && errno == EINTR);
            if (done == 0) return false;
            live_.erase(entry.pid);
        }
        if (entry.slot) release();
        const bool abnormal = done > 0 && !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        if (abnormal) ++metrics_.abnormal;
        if (abnormal && !entry.id.empty()) {
            log(entry.answered ? 3 : 4, entry.id, "request worker ended abnormally", {{"worker_exit", describe_exit(status)}});
        }
        return true;
    }
#endif

    HostOptions options_;
    fs::path executable_;
    SqlHost& sql_;
    worker::ImageDigest image_digest_{};
    std::uint64_t id_prefix_ = 0;
    std::atomic<std::uint64_t> sequence_{0};

    std::mutex admission_mutex_;
    std::condition_variable slot_free_;
    std::size_t running_ = 0;
    std::size_t waiting_ = 0;
    std::atomic<std::size_t> in_flight_{0};

    std::mutex ready_mutex_;
    std::condition_variable ready_changed_;
    std::deque<Worker> ready_;

#ifdef _WIN32
    // SIGKILL's number: a worker the host ends exits with 128 + 9.
    static constexpr int kill_signal = 9;

    std::mutex pids_mutex_;
    // Every started worker until it is reaped, with its process handle.
    std::map<worker_pid, neura::platform::Process> live_;
    HANDLE job_ = nullptr;
    std::atomic<bool> start_failure_logged_{false};

    std::mutex reap_mutex_;
    std::vector<Exiting> exiting_;
    neura::platform::native_socket wake_[2] = {neura::platform::invalid_socket, neura::platform::invalid_socket};

    std::vector<std::string> worker_arguments_;
#else
    std::mutex pids_mutex_;
    std::set<pid_t> live_;

    std::mutex reap_mutex_;
    std::vector<Exiting> exiting_;
    int wake_[2] = {-1, -1};

    std::vector<std::string> worker_arguments_;
    std::mutex zygote_mutex_;
    std::unique_ptr<ZygoteClient> zygote_;
    int zygote_restarts_ = 0;
#endif
    std::atomic<long> refusals_left_{0};

    struct Metrics {
        std::array<std::atomic<std::uint64_t>, 5> responses{};
        std::atomic<std::uint64_t> rejected{0};
        std::atomic<std::uint64_t> timeouts{0};
        std::atomic<std::uint64_t> starts{0};
        std::atomic<std::uint64_t> abnormal{0};
        std::atomic<std::uint64_t> retried{0};
    };
    struct ApplicationMetric {
        std::uint32_t kind = 0; // 1 counter, 2 gauge
        std::int64_t value = 0;
    };
    static constexpr std::uint32_t config_operation = 1;
    static constexpr std::uint32_t metric_operation = 3; // 2 is SQL (the sql-providers branch)
    static constexpr std::size_t max_application_metrics = 1024;
    Metrics metrics_;
    std::mutex metrics_mutex_;
    std::map<std::string, ApplicationMetric, std::less<>> application_metrics_;
    std::atomic<bool> ready_for_requests_{false};

    std::mutex log_mutex_;
    std::jthread reaper_;
    std::jthread spawner_;
};

// ---------------------------------------------------------------------------
// Signals and the foreground loop

#ifdef _WIN32
// Set from the console handler's thread and from the reload event.
std::atomic<bool> stop_requested{false};
std::atomic<bool> reload_requested{false};

// Console events stand for signals (W10): Ctrl+C and Ctrl+Break drain, as
// SIGINT and SIGTERM do. Closing the console, logoff and shutdown drain too,
// within the time the system gives: it ends a process once the handler of
// such an event returns, so this handler returns only when the drain has
// ended the process (or the system has).
bool drain_on_console_event(neura::platform::ConsoleEvent event) {
    stop_requested = true;
    if (event == neura::platform::ConsoleEvent::ctrl_c || event == neura::platform::ConsoleEvent::ctrl_break) {
        return true;
    }
    for (;;) ::Sleep(INFINITE);
}

void install_signals() {
    neura::platform::on_console_event(drain_on_console_event);
}

// Windows has no SIGHUP: the host rereads its TLS files when its reload
// event is set, which `neura-server-host --reload <pid>` does (W27). The
// event lives in the session's namespace, as a signal reaches only the
// processes its sender may signal.
std::wstring reload_event_name(neura::platform::process_id pid) {
    return L"Local\\neura-server-host-reload-" + std::to_wstring(pid);
}

// This host's reload event (auto-reset), or null with the reason.
HANDLE create_reload_event(std::string& problem) {
    const auto name = reload_event_name(neura::platform::current_process_id());
    const HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, name.c_str());
    if (event == nullptr) {
        problem = neura::platform::error_message(neura::platform::last_error());
        return nullptr;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another process made the event first and could set it at will.
        ::CloseHandle(event);
        problem = "another process created the event first";
        return nullptr;
    }
    return event;
}

// neura-server-host --reload <pid>: sets the reload event of that host.
int request_reload(std::string_view text) {
    neura::platform::process_id pid = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), pid);
    if (error != std::errc{} || end != text.data() + text.size() || pid == 0) {
        std::cerr << "neura-server-host: --reload expects the process identifier of a server host\n";
        return 1;
    }
    const HANDLE event = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, reload_event_name(pid).c_str());
    if (event == nullptr) {
        const auto code = neura::platform::last_error();
        if (code == ERROR_FILE_NOT_FOUND) {
            std::cerr << "neura-server-host: no server host with process identifier " << pid
                      << " runs in this session\n";
        } else {
            std::cerr << "neura-server-host: cannot reach the server host " << pid << ": "
                      << neura::platform::error_message(code) << '\n';
        }
        return 1;
    }
    const bool set = ::SetEvent(event) != 0;
    const auto code = neura::platform::last_error();
    ::CloseHandle(event);
    if (!set) {
        std::cerr << "neura-server-host: cannot ask the server host " << pid << " to reload: "
                  << neura::platform::error_message(code) << '\n';
        return 1;
    }
    return 0;
}
#else
volatile std::sig_atomic_t stop_requested = 0;
volatile std::sig_atomic_t reload_requested = 0;

extern "C" void on_signal(int signal) {
    if (signal == SIGHUP) reload_requested = 1;
    else stop_requested = 1;
}

void install_signals() {
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    for (const int signal : {SIGHUP, SIGINT, SIGTERM}) ::sigaction(signal, &action, nullptr);
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &ignore, nullptr);
}
#endif

struct Active {
    std::jthread thread;
    std::shared_ptr<std::atomic<bool>> finished;
};

// Serves the admin listener on its own thread until the drain ends the
// process: one short connection thread each, outside worker admission.
class AdminListener {
public:
    AdminListener(std::span<const http::ListenEndpoint> endpoints, Host& host) : host_(host) {
        http::ListenerLimits limits;
        limits.connections = 16;
        limits.http.request_timeout = 5s;
        limits.http.head_timeout = 5s;
        listeners_.emplace(endpoints, limits);
        thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    }

    std::span<const http::BoundEndpoint> endpoints() const { return listeners_->endpoints(); }

private:
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            http::AcceptResult accepted;
            try {
                accepted = listeners_->accept_until(http::ConnectionClock::now() + 200ms);
            } catch (const std::exception&) {
                continue;
            }
            if (accepted.status != http::AcceptStatus::Accepted) continue;
            std::thread([this, connection = std::move(*accepted.connection)]() mutable {
                const http::BufferedResponder respond = [this](const http::RequestHead& head, std::string_view) {
                    return host_.admin(head);
                };
                try {
                    (void)std::move(connection).serve(respond);
                } catch (const std::exception&) {
                }
            }).detach();
        }
    }

    Host& host_;
    std::optional<http::ListenerSet> listeners_;
    std::jthread thread_;
};

void print_endpoint(std::string_view what, const http::BoundEndpoint& endpoint) {
    const bool v6 = endpoint.address.find(':') != std::string::npos;
    std::cout << "neura-server-host: " << what << ' ' << (endpoint.tls ? "https" : "http") << "://" << (v6 ? "[" : "")
              << endpoint.address << (v6 ? "]" : "") << ':' << endpoint.port << '\n';
}

int serve(HostOptions options, const char* argv0) {
    install_signals();
    // SQL pools and migrations, before any worker exists.
    SqlHost sql(options.sql_pools, options.config, options.config_base);
    {
        std::vector<std::string> messages;
        const auto problem = sql.prepare(options.migrate_only, messages);
        for (const auto& message : messages) std::cerr << json_log_line(2, {}, message, {}) << std::flush;
        if (problem) {
            std::cerr << "neura-server-host: " << *problem << '\n';
            return 1;
        }
    }
    if (options.migrate_only) {
        std::cout << "neura-server-host: migrations are up to date" << std::endl;
        return 0;
    }
    if (sql.enabled()) options.capabilities.sql = true;
    auto host = std::make_unique<Host>(options, host_executable(argv0), sql);
#ifdef _WIN32
    std::string reload_problem;
    const HANDLE reload_event = create_reload_event(reload_problem);
    if (reload_event == nullptr) {
        host->log(3, {}, "no reload event; --reload cannot reach this host", {{"reason", reload_problem}});
    }
#endif
    // The admin listener answers /livez, and /readyz with 503, while the
    // startup check runs.
    std::unique_ptr<AdminListener> admin;
    if (!options.admin_endpoints.empty()) {
        try {
            admin = std::make_unique<AdminListener>(options.admin_endpoints, *host);
        } catch (const std::exception& error) {
            std::cerr << "neura-server-host: admin listener: " << error.what() << '\n';
            return 1;
        }
        for (const auto& endpoint : admin->endpoints()) print_endpoint("admin on", endpoint);
        std::cout.flush();
    }
    if (options.startup_check && !host->check_startup()) {
        std::cerr << "neura-server-host: the startup check failed; not listening\n";
        return 1;
    }
    std::optional<http::ListenerSet> listeners;
    Host* counted = host.get();
    options.limits.http.response_finished = [counted] { counted->response_written(); };
    options.limits.http.draining = &draining;
    try {
        listeners.emplace(options.endpoints, options.limits);
    } catch (const std::exception& error) {
        std::cerr << "neura-server-host: " << error.what() << '\n';
        return 1;
    }
    host->mark_ready();
    for (const auto& endpoint : listeners->endpoints()) print_endpoint("listening on", endpoint);
    std::cout << "neura-server-host: ready" << std::endl;

    std::vector<Active> active;
    while (!stop_requested) {
#ifdef _WIN32
        if (reload_event != nullptr && ::WaitForSingleObject(reload_event, 0) == WAIT_OBJECT_0) reload_requested = true;
#endif
        if (reload_requested) {
            reload_requested = 0;
            try {
                listeners->reload_tls();
                host->log(2, {}, "reloaded the TLS certificate and key");
            } catch (const std::exception& error) {
                host->log(4, {}, std::string("TLS reload failed; keeping the previous files: ") + error.what());
            }
        }
        std::erase_if(active, [](Active& entry) { return entry.finished->load(); });
        http::AcceptResult accepted;
        try {
            accepted = listeners->accept_until(http::ConnectionClock::now() + 200ms);
        } catch (const std::exception& error) {
            host->log(4, {}, std::string("accept failed: ") + error.what());
            continue;
        }
        if (accepted.status != http::AcceptStatus::Accepted) continue;
        auto connection = std::move(*accepted.connection);
        auto finished = std::make_shared<std::atomic<bool>>(false);
        Host* raw = host.get();
        std::string peer(connection.peer_address());
        const bool secure = connection.secure();
        active.push_back({std::jthread([raw, finished, peer, secure, connection = std::move(connection)]() mutable {
                              const http::Responder respond =
                                  [raw, &peer, secure](const http::RequestHead& head, http::RequestBody& body,
                                                       http::ResponseWriter& writer,
                                                       http::ConnectionClock::time_point deadline) {
                                      raw->handle(head, body, writer, deadline, peer, secure);
                                  };
                              try {
                                  (void)std::move(connection).serve(respond);
                              } catch (const std::exception&) {
                              }
                              finished->store(true);
                          }),
                          finished});
    }

    // Drain: stop accepting, answer 503 to new requests, and wait up to S-12
    // until every request in flight has been answered and its response
    // written. Idle keep-alive connections are not waited for. A worker
    // still runs after it has sent its response: the rest of the image, and
    // with --record-dir the writing of its trace. Within the same deadline
    // the drain also waits until every such worker has been reaped (the
    // reaper still kills one that outlives its grace period); killing it
    // here would lose its trace.
    draining = true;
    listeners.reset();
    host->log(2, {}, "draining");
    const auto drain_deadline = Clock::now() + options.drain_timeout;
    while ((host->in_flight() > 0 || host->slots_held() > 0) && Clock::now() < drain_deadline) {
        std::this_thread::sleep_for(10ms);
    }
    const auto left = host->in_flight();
    if (left > 0) {
        host->log(4, {}, "drain timed out; killing the remaining workers", {{"in_flight", std::to_string(left)}});
    } else if (const auto exiting = host->slots_held(); exiting > 0) {
        host->log(3, {}, "drain timed out; killing workers that answered but have not exited",
                  {{"workers", std::to_string(exiting)}});
    }
    host->kill_all();
    host->log(2, {}, "stopped");
    std::cout.flush();
    std::cerr.flush();
    // Idle connection threads are not joined; the process ends here.
    std::_Exit(left > 0 ? 1 : 0);
}

// The options of --worker and --zygote, which the host writes.
WorkerOptions read_worker_options(int argc, char** argv) {
    WorkerOptions options;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--allow-fs") {
            options.capabilities.fs = true;
        } else if (arg.starts_with("--allow-fs=")) {
            options.capabilities.fs = true;
            std::stringstream stream{std::string(arg.substr(11))};
            std::string prefix;
            while (std::getline(stream, prefix, ',')) {
                if (!prefix.empty()) options.capabilities.fs_prefixes.push_back(prefix);
            }
        } else if (arg == "--allow-net") {
            options.capabilities.net = true;
        } else if (neura::cli::parse_http_client_option(arg, options.capabilities)) {
            continue;
        } else if (arg == "--allow-env") {
            options.capabilities.env = true;
        } else if (arg == "--allow-sql") {
            options.capabilities.sql = true;
        } else if (arg.starts_with("--record-dir=")) {
#ifdef _WIN32
            options.record_directory = neura::platform::path_from_utf8(arg.substr(13));
#else
            options.record_directory = std::string(arg.substr(13));
#endif
        } else if (arg.starts_with("--max-response-bytes=")) {
            options.response_bytes = std::stoull(std::string(arg.substr(21)));
        } else if (arg.starts_with("--max-steps=")) {
            options.max_steps = std::stoull(std::string(arg.substr(12)));
        } else if (arg.starts_with("--tier=")) {
            options.tier = static_cast<std::uint8_t>(std::stoul(std::string(arg.substr(7))));
        } else if (arg.starts_with("--jit=")) {
            options.jit = std::string(arg.substr(6));
        } else if (arg == "--test-refuse-assign") {
            options.refuse_assign = true;
#ifdef _WIN32
        } else if (arg.starts_with("--channel=")) {
            // The inherited socket of the worker's channel, by its value.
            const auto value = arg.substr(10);
            neura::platform::native_socket channel = neura::platform::invalid_socket;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), channel);
            if (error == std::errc{} && end == value.data() + value.size()) options.channel = channel;
        } else {
            options.image = neura::platform::path_from_utf8(arg);
        }
#else
        } else {
            options.image = arg;
        }
#endif
    }
#ifdef _WIN32
    // A worker leaves console events to the host: it ends when its channel
    // does. It runs in a process group of its own, so the host's Ctrl+Break
    // does not reach it, and it ignores the events the console sends to all.
    neura::platform::ignore_console_events();
#else
    // A worker leaves signals to the host: it ends when its channel does.
    // A zygote's workers inherit this.
    ::signal(SIGINT, SIG_IGN);
    ::signal(SIGPIPE, SIG_IGN);
#endif
    return options;
}

#ifdef _WIN32
// Until the host's drain or a worker's ignoring handler replaces it: the
// statuses a POSIX shell reports for the signals the events stand for (W10).
bool end_on_console_event(neura::platform::ConsoleEvent event) {
    switch (event) {
    case neura::platform::ConsoleEvent::ctrl_c: std::_Exit(130);
    case neura::platform::ConsoleEvent::ctrl_break: std::_Exit(131);
    default: std::_Exit(129);
    }
}

// What every Neura tool sets up first on Windows (tool::start, W10 and
// W17), for the host and its workers alike: no error dialogs, binary
// standard streams (the JSON log lines and the image's output keep their LF),
// UTF-8 console output, DLLs from the system directories only, and the
// default console handler. False on a Windows older than 10 version 2004
// (W03).
bool prepare_windows_process() {
    neura::platform::set_unattended_error_mode();
    neura::platform::set_binary_stdio();
    neura::platform::install_console_streams();
    ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    neura::platform::on_console_event(end_on_console_event);
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
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    if (!prepare_windows_process()) {
        std::cerr << "neura: Neura needs Windows 10 version 2004 or later\n";
        return 1;
    }
#endif
    try {
#ifdef _WIN32
        if (argc >= 2 && std::string_view(argv[1]) == "--reload") return request_reload(argc == 3 ? argv[2] : "");
        if (argc >= 2 && std::string_view(argv[1]).starts_with("--reload=")) {
            return request_reload(argc == 2 ? std::string_view(argv[1]).substr(9) : "");
        }
#endif
        if (argc >= 2 && std::string_view(argv[1]) == "--worker") return run_request_worker(read_worker_options(argc, argv));
        if (argc >= 2 && std::string_view(argv[1]) == "--zygote") return run_worker_zygote(read_worker_options(argc, argv));
        // A core tool (docs/design/Products030.md): `--version` alone prints
        // the version line of `neura`, and a host started from an installed
        // generation holds that generation's lease while it serves.
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::cout << neura::tool::version_line() << '\n';
            return 0;
        }
        auto options = read_options(argc, argv);
        bool removed = false;
        const auto self = neura::tool::executable_path(argv[0]);
#ifdef _WIN32
        // The handle stays open (not inherited) until the host exits.
        [[maybe_unused]] const auto lease = neura::tool::take_lease(self, removed);
#else
        // The descriptor stays open (close-on-exec) until the host exits.
        [[maybe_unused]] const int lease = neura::tool::take_lease(self.c_str(), removed);
#endif
        if (removed) {
            std::cerr << neura::tool::removed_generation_message;
            return 1;
        }
        return serve(std::move(options), argv[0]);
    } catch (const std::exception& error) {
        std::cerr << "neura-server-host: " << error.what() << '\n';
        return 1;
    }
}
