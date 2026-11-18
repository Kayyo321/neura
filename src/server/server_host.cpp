// neura-server-host: the native host of a server image (docs/design/Backend030.md,
// "The server host and request workers"). It owns the listeners, TLS, the
// HTTP wire and the request workers, and runs no application code: every
// request is executed by a fresh worker process that runs the whole image.

#include "nvr/format.hpp"
#include "runner/runner.hpp"
#include "server/http_listener.hpp"
#include "server/json_line.hpp"
#include "server/request_worker.hpp"
#include "server/worker_messages.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

extern char** environ;

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
    "Serves a server image over HTTP/1.1 (docs/runtime/ServerHost.md). Each request runs in a fresh\n"
    "worker process that executes the whole image once.\n"
    "\n"
    "  --listen <address>:<port>      plain HTTP listener (repeatable; port 0 asks for one)\n"
    "  --tls-listen <address>:<port>  HTTPS listener; needs --cert and --key (repeatable)\n"
    "  --cert <file> --key <file>     PEM certificate chain and private key, reread on SIGHUP\n"
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
    "  --allow-fs[=prefix,...], --allow-net, --allow-env\n"
    "                                 grant the workers that group (default: none)\n"
    "  --record-dir <directory>       record each request's host requests to <id>.trace\n"
    "  --no-startup-check             skip the startup check request\n"
    "\n"
    "SIGTERM and SIGINT drain and exit; SIGHUP reloads the TLS files.\n";

// ---------------------------------------------------------------------------
// Options

struct HostOptions {
    fs::path image;
    std::vector<http::ListenEndpoint> endpoints;
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
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read the configuration file " + path.string());
    std::string line;
    std::size_t number = 0;
    while (std::getline(input, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == first) {
            throw std::runtime_error(path.string() + ":" + std::to_string(number) + ": expected key=value");
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
            std::cout << usage;
            std::exit(0);
        } else if (is(arg, "--listen")) {
            options.endpoints.push_back(parse_endpoint("--listen", value_of(i, arg, "--listen")));
        } else if (is(arg, "--tls-listen")) {
            tls_endpoints.push_back(options.endpoints.size());
            options.endpoints.push_back(parse_endpoint("--tls-listen", value_of(i, arg, "--tls-listen")));
        } else if (is(arg, "--cert")) {
            options.certificate = value_of(i, arg, "--cert");
        } else if (is(arg, "--key")) {
            options.key = value_of(i, arg, "--key");
        } else if (is(arg, "--config")) {
            config_values.push_back(value_of(i, arg, "--config"));
        } else if (is(arg, "--config-file")) {
            config_files.emplace_back(value_of(i, arg, "--config-file"));
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
        } else if (arg == "--allow-env") {
            options.capabilities.env = true;
        } else if (is(arg, "--record-dir")) {
            options.record_directory = fs::absolute(value_of(i, arg, "--record-dir"));
        } else if (arg == "--no-startup-check") {
            options.startup_check = false;
        } else if (arg.starts_with('-')) {
            throw std::runtime_error("unknown option: " + std::string(arg) + "\n" + std::string(usage));
        } else if (!options.image.empty()) {
            throw std::runtime_error("neura-server-host takes one image");
        } else {
            options.image = fs::absolute(arg);
        }
    }
    if (options.image.empty()) throw std::runtime_error(std::string(usage));
    if (options.endpoints.empty()) throw std::runtime_error("at least one --listen or --tls-listen is required");
    if (!tls_endpoints.empty()) {
        if (!options.certificate || !options.key) throw std::runtime_error("--tls-listen needs --cert and --key");
        for (const auto index : tls_endpoints) {
            options.endpoints[index].tls = http::TlsFiles{fs::absolute(*options.certificate), fs::absolute(*options.key)};
        }
    } else if (options.certificate || options.key) {
        throw std::runtime_error("--cert and --key are used only with --tls-listen");
    }
    for (const auto& file : config_files) read_config_file(file, options);
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
#endif
    return fs::absolute(argv0);
}

struct Worker {
    pid_t pid = -1;
    std::unique_ptr<Channel> channel;
};

std::atomic<bool> draining{false};

class Host {
public:
    Host(HostOptions options, fs::path executable)
        : options_(std::move(options)), executable_(std::move(executable)) {
        const auto bytes = neura::runner::read_file(options_.image);
        neura::util::Sha256 digest;
        digest.update(std::span<const std::uint8_t>(bytes));
        image_digest_ = digest.finish();
        nvr_check(bytes);
        std::random_device device;
        id_prefix_ = (static_cast<std::uint64_t>(device()) << 32) | device();
        reaper_ = std::jthread([this](std::stop_token stop) { reap(stop); });
        if (options_.ready_workers > 0) spawner_ = std::jthread([this](std::stop_token stop) { keep_ready(stop); });
    }

    ~Host() {
        spawner_.request_stop();
        ready_changed_.notify_all();
        if (spawner_.joinable()) spawner_.join();
        std::lock_guard lock(ready_mutex_);
        ready_.clear(); // Closing the channels ends the ready workers.
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
        const auto outcome = exchange(*worker, request, {}, Clock::now() + options_.limits.http.request_timeout);
        const pid_t pid = worker->pid;
        worker->channel.reset();
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        {
            std::lock_guard lock(pids_mutex_);
            live_.erase(pid);
        }
        const bool exited = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (!outcome.done || outcome.status >= 400 || !exited) {
            log(4, request.request_id, "startup check failed",
                {{"status", std::to_string(outcome.status)}, {"worker_exit", describe_exit(status)}});
            return false;
        }
        return true;
    }

    // The responder of one connection.
    http::BufferedResponse handle(const http::RequestHead& head, std::string_view body, const std::string& peer,
                                  bool secure) {
        // Paired with response_written(), which the connection calls once
        // this response has been written or has failed.
        ++in_flight_;
        const auto started = Clock::now();
        const auto deadline = started + options_.limits.http.request_timeout - 50ms;
        if (draining.load()) return unavailable();
        if (!admit(deadline)) return unavailable();
        struct Release {
            Host& host;
            ~Release() { host.release(); }
        } release{*this};

        neura::nvm::ServerRequest request;
        request.request_id = next_id();
        request.method = head.method;
        request.target = head.target;
        for (const auto& header : head.headers) request.headers.emplace_back(header.name, header.value);
        request.body_length = static_cast<std::int64_t>(body.size());
        request.peer = peer;
        request.scheme = secure ? "https" : "http";
        const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now());
        request.deadline = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::system_clock::now().time_since_epoch()).count() + left.count();

        auto worker = take_worker();
        if (!worker) {
            log(4, request.request_id, "could not start a request worker");
            return error_response(500);
        }
        auto outcome = exchange(*worker, request, body, deadline);
        const pid_t pid = worker->pid;
        // A worker past its deadline, or one that broke the protocol, is
        // killed; one that closed its channel is left its grace period to exit.
        if (outcome.timed_out || worker->channel->status() == Channel::Status::Failed) ::kill(pid, SIGKILL);
        worker->channel.reset();
        hand_to_reaper(pid, request.request_id, outcome.done);
        if (outcome.timed_out) {
            log(4, request.request_id, "request deadline passed; worker killed");
            return error_response(504);
        }
        if (!outcome.done) {
            log(4, request.request_id, outcome.too_large ? "response body exceeds S-7" : "worker ended without a response");
            return error_response(500);
        }
        http::BufferedResponse response;
        response.status = static_cast<int>(outcome.status);
        response.body = std::move(outcome.body);
        for (auto& [name, value] : outcome.headers) response.headers.push_back({std::move(name), std::move(value)});
        return response;
    }

    void log(std::uint32_t level, std::string_view id, std::string_view message,
             const neura::nvm::HeaderPairs& fields = {}) {
        const auto line = json_log_line(level, id, message, fields);
        std::lock_guard lock(log_mutex_);
        std::cerr << line << std::flush;
    }

    std::size_t in_flight() const { return in_flight_.load(); }
    void response_written() { --in_flight_; }

    void kill_all() {
        std::lock_guard lock(pids_mutex_);
        for (const auto pid : live_) ::kill(pid, SIGKILL);
    }

private:
    struct Outcome {
        bool done = false;
        bool timed_out = false;
        bool too_large = false;
        std::uint32_t status = 0;
        neura::nvm::HeaderPairs headers;
        std::string body;
    };

    static void nvr_check(const std::vector<std::uint8_t>& bytes) {
        try {
            (void)neura::nvr::load(bytes);
        } catch (const neura::nvr::LoadError& error) {
            throw std::runtime_error(std::string("the image does not load: ") + error.what());
        }
    }

    static std::string describe_exit(int status) {
        if (WIFEXITED(status)) return "status " + std::to_string(WEXITSTATUS(status));
        if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
        return "unknown";
    }

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
        std::vector<std::string> arguments{executable_.string(), "--worker"};
        const auto& caps = options_.capabilities;
        if (caps.fs) {
            std::string option = "--allow-fs";
            for (std::size_t i = 0; i < caps.fs_prefixes.size(); ++i) {
                option += (i == 0 ? "=" : ",") + caps.fs_prefixes[i];
            }
            arguments.push_back(option);
        }
        if (caps.net) arguments.emplace_back("--allow-net");
        if (caps.env) arguments.emplace_back("--allow-env");
        if (options_.record_directory) arguments.push_back("--record-dir=" + options_.record_directory->string());
        arguments.push_back("--max-response-bytes=" + std::to_string(options_.limits.http.response_bytes));
        arguments.push_back(options_.image.string());
        std::vector<char*> argv;
        for (auto& argument : arguments) argv.push_back(argument.data());
        argv.push_back(nullptr);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pair[1], worker_channel_descriptor);
        // The image's console output goes to the host's standard error.
        posix_spawn_file_actions_adddup2(&actions, 2, 1);
        pid_t pid = -1;
        const int error = ::posix_spawn(&pid, executable_.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pair[1]);
        if (error != 0) {
            ::close(pair[0]);
            return std::nullopt;
        }
        {
            std::lock_guard lock(pids_mutex_);
            live_.insert(pid);
        }
        Worker worker{pid, std::make_unique<Channel>(pair[0], worker::PeerRole::Host, worker::server_identity(image_digest_))};
        worker::Hello hello;
        hello.role = worker::PeerRole::Host;
        hello.identity = worker::server_identity(image_digest_);
        if (worker.channel->greet(hello, Clock::now() + 10s) != Channel::Status::Ok) {
            log(4, {}, "request worker failed its greeting");
            ::kill(pid, SIGKILL);
            worker.channel.reset();
            hand_to_reaper(pid, {}, false);
            return std::nullopt;
        }
        return worker;
    }

    std::optional<Worker> take_worker() {
        {
            std::lock_guard lock(ready_mutex_);
            if (!ready_.empty()) {
                auto worker = std::move(ready_.front());
                ready_.pop_front();
                ready_changed_.notify_all();
                return worker;
            }
        }
        ready_changed_.notify_all();
        return spawn();
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

    // Sends the assignment and the body, then reads the response, answering
    // configuration calls, until Done, the deadline or the end of the channel.
    Outcome exchange(Worker& worker, const neura::nvm::ServerRequest& request, std::string_view body,
                     Clock::time_point deadline) {
        Outcome outcome;
        auto& channel = *worker.channel;
        const auto assign = worker::encode_assign(request);
        if (channel.send(FrameKind::Assign, assign, deadline) != Channel::Status::Ok) {
            outcome.timed_out = channel.status() == Channel::Status::TimedOut;
            return outcome;
        }
        for (std::string_view rest = body; !rest.empty();) {
            const auto size = std::min(rest.size(), worker::max_body_chunk_bytes);
            const std::vector<std::uint8_t> chunk(rest.begin(), rest.begin() + static_cast<std::ptrdiff_t>(size));
            if (channel.send(FrameKind::BodyChunk, chunk, deadline) != Channel::Status::Ok) {
                outcome.timed_out = channel.status() == Channel::Status::TimedOut;
                return outcome;
            }
            rest.remove_prefix(size);
        }
        if (channel.send(FrameKind::BodyEnd, {}, deadline) != Channel::Status::Ok) {
            outcome.timed_out = channel.status() == Channel::Status::TimedOut;
            return outcome;
        }
        for (;;) {
            auto frame = channel.receive(deadline);
            if (!frame) {
                outcome.timed_out = channel.status() == Channel::Status::TimedOut;
                if (channel.status() == Channel::Status::Failed) {
                    log(4, request.request_id, "request worker broke the protocol");
                }
                return outcome;
            }
            switch (frame->kind) {
            case FrameKind::Head: {
                auto head = worker::decode_head(frame->body);
                if (!head) return outcome;
                outcome.status = head->status;
                outcome.headers = std::move(head->headers);
                break;
            }
            case FrameKind::BodyChunk:
                if (outcome.body.size() + frame->body.size() > options_.limits.http.response_bytes) {
                    outcome.too_large = true;
                    return outcome;
                }
                outcome.body.append(frame->body.begin(), frame->body.end());
                break;
            case FrameKind::Call:
                if (channel.send(FrameKind::Return, answer_call(frame->body), deadline) != Channel::Status::Ok) {
                    return outcome;
                }
                break;
            case FrameKind::Done:
                outcome.done = true;
                return outcome;
            default:
                break;
            }
        }
    }

    // A configuration lookup: the file and command-line values, overridden
    // by the environment variable <prefix><key>.
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
        if (body.size() < 12 || get(4) != 1 || body.size() != 12 + std::size_t{get(8)}) {
            put(neura::nvm::host_error::invalid_input);
            out.push_back(0);
            put(0);
            return out;
        }
        const std::string key(reinterpret_cast<const char*>(body.data()) + 12, get(8));
        std::optional<std::string> value;
        if (options_.env_prefix) {
            if (const char* text = std::getenv((*options_.env_prefix + key).c_str())) value = text;
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

    struct Exiting {
        pid_t pid;
        std::string id;
        bool answered;
        Clock::time_point kill_at;
    };

    void hand_to_reaper(pid_t pid, std::string id, bool answered) {
        {
            std::lock_guard lock(reap_mutex_);
            exiting_.push_back({pid, std::move(id), answered, Clock::now() + options_.grace});
        }
        reap_changed_.notify_one();
    }

    // Waits for every finished worker. A worker that answered and does not
    // exit within the grace period is killed. Traps and failures are logged.
    void reap(std::stop_token stop) {
        for (;;) {
            std::vector<Exiting> batch;
            {
                std::unique_lock lock(reap_mutex_);
                reap_changed_.wait_for(lock, 20ms, [&] { return stop.stop_requested() || !exiting_.empty(); });
                if (stop.stop_requested() && exiting_.empty()) return;
                batch.swap(exiting_);
            }
            std::vector<Exiting> pending;
            for (auto& entry : batch) {
                int status = 0;
                const auto done = ::waitpid(entry.pid, &status, WNOHANG);
                if (done == 0) {
                    if (Clock::now() >= entry.kill_at) ::kill(entry.pid, SIGKILL);
                    pending.push_back(std::move(entry));
                    continue;
                }
                {
                    std::lock_guard lock(pids_mutex_);
                    live_.erase(entry.pid);
                }
                if (done > 0 && !(WIFEXITED(status) && WEXITSTATUS(status) == 0) && !entry.id.empty()) {
                    log(entry.answered ? 3 : 4, entry.id, "request worker ended abnormally",
                        {{"worker_exit", describe_exit(status)}});
                }
            }
            if (!pending.empty()) {
                std::lock_guard lock(reap_mutex_);
                exiting_.insert(exiting_.end(), std::make_move_iterator(pending.begin()),
                                std::make_move_iterator(pending.end()));
                if (stop.stop_requested()) {
                    for (auto& entry : exiting_) ::kill(entry.pid, SIGKILL);
                }
            }
        }
    }

    HostOptions options_;
    fs::path executable_;
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

    std::mutex pids_mutex_;
    std::set<pid_t> live_;

    std::mutex reap_mutex_;
    std::condition_variable reap_changed_;
    std::vector<Exiting> exiting_;

    std::mutex log_mutex_;
    std::jthread reaper_;
    std::jthread spawner_;
};

// ---------------------------------------------------------------------------
// Signals and the foreground loop

volatile std::sig_atomic_t stop_requested = 0;
volatile std::sig_atomic_t reload_requested = 0;

extern "C" void on_signal(int signal) {
    if (signal == SIGHUP) reload_requested = 1;
    else stop_requested = 1;
}

void install_signals() {
    struct sigaction action{};
    action.sa_handler = on_signal;
    ::sigemptyset(&action.sa_mask);
    for (const int signal : {SIGHUP, SIGINT, SIGTERM}) ::sigaction(signal, &action, nullptr);
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    ::sigaction(SIGPIPE, &ignore, nullptr);
}

struct Active {
    std::jthread thread;
    std::shared_ptr<std::atomic<bool>> finished;
};

int serve(HostOptions options, const char* argv0) {
    install_signals();
    auto host = std::make_unique<Host>(options, host_executable(argv0));
    if (options.startup_check && !host->check_startup()) {
        std::cerr << "neura-server-host: the startup check failed; not listening\n";
        return 1;
    }
    std::optional<http::ListenerSet> listeners;
    Host* counted = host.get();
    options.limits.http.response_finished = [counted] { counted->response_written(); };
    try {
        listeners.emplace(options.endpoints, options.limits);
    } catch (const std::exception& error) {
        std::cerr << "neura-server-host: " << error.what() << '\n';
        return 1;
    }
    for (const auto& endpoint : listeners->endpoints()) {
        const bool v6 = endpoint.address.find(':') != std::string::npos;
        std::cout << "neura-server-host: listening on " << (endpoint.tls ? "https" : "http") << "://"
                  << (v6 ? "[" : "") << endpoint.address << (v6 ? "]" : "") << ':' << endpoint.port << '\n';
    }
    std::cout << "neura-server-host: ready" << std::endl;

    std::vector<Active> active;
    while (!stop_requested) {
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
                              const http::BufferedResponder respond =
                                  [raw, &peer, secure](const http::RequestHead& head, std::string_view body) {
                                      return raw->handle(head, body, peer, secure);
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
    // written. Idle keep-alive connections are not waited for.
    draining = true;
    listeners.reset();
    host->log(2, {}, "draining");
    const auto drain_deadline = Clock::now() + options.drain_timeout;
    while (host->in_flight() > 0 && Clock::now() < drain_deadline) std::this_thread::sleep_for(10ms);
    const auto left = host->in_flight();
    if (left > 0) host->log(4, {}, "drain timed out; killing the remaining workers", {{"in_flight", std::to_string(left)}});
    host->kill_all();
    host->log(2, {}, "stopped");
    std::cout.flush();
    std::cerr.flush();
    // Idle connection threads are not joined; the process ends here.
    std::_Exit(left > 0 ? 1 : 0);
}

int worker_main(int argc, char** argv) {
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
        } else if (arg == "--allow-env") {
            options.capabilities.env = true;
        } else if (arg.starts_with("--record-dir=")) {
            options.record_directory = std::string(arg.substr(13));
        } else if (arg.starts_with("--max-response-bytes=")) {
            options.response_bytes = std::stoull(std::string(arg.substr(21)));
        } else {
            options.image = arg;
        }
    }
    // The worker leaves signals to the host: it ends when its channel does.
    ::signal(SIGINT, SIG_IGN);
    ::signal(SIGPIPE, SIG_IGN);
    return run_request_worker(options);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 2 && std::string_view(argv[1]) == "--worker") return worker_main(argc, argv);
        return serve(read_options(argc, argv), argv[0]);
    } catch (const std::exception& error) {
        std::cerr << "neura-server-host: " << error.what() << '\n';
        return 1;
    }
}
