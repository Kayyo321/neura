// A scripted HTTP/2 client for the server host's tests (h2c with prior
// knowledge), built on the pinned nghttp2. Neither nghttp nor a Python h2
// package is available on the test machines, and curl cannot interleave
// streams, stall a window, reset a stream or ignore the server's stream
// limit on purpose. Each scenario prints one JSON object on standard output;
// tests/server/server_host_test.py checks it.
//
//   neura_http2_test_client --port P --scenario S [options]
//
// Scenarios:
//   fetch       one request: --method, --path, --header name:value
//               (repeatable), --body or --body-stdin 1; the whole body is
//               printed (other scenarios print at most 4 KiB of each)
//   interleave  --count requests to --path at once on one connection
//   stall       SETTINGS_INITIAL_WINDOW_SIZE --window and no window updates
//               for --hold-ms while GET --path streams; GET /hello is sent
//               during the stall; then the window opens
//   reset       GET --path, RST_STREAM(CANCEL) after --after-ms, then
//               GET /hello on the same connection
//   headers     GET --path with an x-big field of --header-bytes bytes
//   streams     --count requests to --path sent before the server's
//               SETTINGS are read, ignoring its stream limit
//   drain       --count requests to --path; SIGTERM to --pid after
//               --after-ms; one more request once GOAWAY has arrived
//   load        keeps --count streams to --path in flight on one connection
//               for --seconds (closed loop); prints the rate and latency
//               percentiles instead of the streams

#include <nghttp2/nghttp2.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#ifdef MSG_NOSIGNAL
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0; // SO_NOSIGPIPE is set on the socket instead.
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    int port = 0;
    std::string scenario;
    std::string method = "GET";
    std::string path = "/hello";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    bool has_body = false;
    int count = 1;
    int window = 65535;
    int hold_ms = 0;
    int after_ms = 0;
    int header_bytes = 0;
    int pid = 0;
    int timeout_ms = 30000;
    int seconds = 10;
};

struct StreamResult {
    std::int32_t id = 0;
    std::string path;
    int status = 0;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::uint64_t bytes = 0;
    bool closed = false;
    std::uint32_t error = 0; // The RST_STREAM error code, or 0.
    double opened_ms = 0;
    double closed_ms = 0;
};

struct Request {
    std::string body;
    std::size_t offset = 0;
};

class Client {
public:
    Client(const Options& options, int socket) : options_(options), socket_(socket), started_(Clock::now()) {}

    ~Client() {
        if (session_) nghttp2_session_del(session_);
        for (auto& [id, request] : requests_) delete request;
    }

    bool open(bool manual_window, std::uint32_t peer_max_streams) {
        nghttp2_session_callbacks* callbacks = nullptr;
        nghttp2_option* option = nullptr;
        if (nghttp2_session_callbacks_new(&callbacks) != 0 || nghttp2_option_new(&option) != 0) return false;
        nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data);
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_close);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame);
        if (manual_window) nghttp2_option_set_no_auto_window_update(option, 1);
        if (peer_max_streams != 0) nghttp2_option_set_peer_max_concurrent_streams(option, peer_max_streams);
        const int created = nghttp2_session_client_new2(&session_, callbacks, this, option);
        nghttp2_option_del(option);
        nghttp2_session_callbacks_del(callbacks);
        if (created != 0) return false;
        std::vector<nghttp2_settings_entry> settings{{NGHTTP2_SETTINGS_ENABLE_PUSH, 0}};
        if (manual_window) settings.push_back({NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, static_cast<std::uint32_t>(options_.window)});
        return nghttp2_submit_settings(session_, NGHTTP2_FLAG_NONE, settings.data(), settings.size()) == 0;
    }

    // Submits a request; its stream identifier, or a negative nghttp2 error.
    std::int32_t submit(const std::string& method, const std::string& path,
                        const std::vector<std::pair<std::string, std::string>>& extra, const std::string* body) {
        const std::string authority = "127.0.0.1:" + std::to_string(options_.port);
        std::vector<std::pair<std::string, std::string>> fields{
            {":method", method}, {":scheme", "http"}, {":authority", authority}, {":path", path}};
        fields.insert(fields.end(), extra.begin(), extra.end());
        std::vector<nghttp2_nv> nva;
        for (auto& [name, value] : fields) {
            nva.push_back({reinterpret_cast<std::uint8_t*>(name.data()), reinterpret_cast<std::uint8_t*>(value.data()),
                           name.size(), value.size(), NGHTTP2_NV_FLAG_NONE});
        }
        auto* request = new Request{body ? *body : std::string{}, 0};
        nghttp2_data_provider2 provider{};
        provider.source.ptr = request;
        provider.read_callback = read_request_body;
        const auto id = nghttp2_submit_request2(session_, nullptr, nva.data(), nva.size(), body ? &provider : nullptr,
                                                nullptr);
        if (id < 0) {
            delete request;
            return id;
        }
        requests_[id] = request;
        auto& result = streams_[id];
        result.id = id;
        result.path = path;
        result.opened_ms = elapsed_ms();
        order_.push_back(id);
        return id;
    }

    void reset(std::int32_t id) { (void)nghttp2_submit_rst_stream(session_, NGHTTP2_FLAG_NONE, id, NGHTTP2_CANCEL); }

    // Writes what the session has to send; false when the connection failed.
    bool flush() {
        for (;;) {
            const std::uint8_t* data = nullptr;
            const auto size = nghttp2_session_mem_send2(session_, &data);
            if (size < 0) return false;
            if (size == 0) return true;
            std::size_t sent = 0;
            while (sent < static_cast<std::size_t>(size)) {
                const auto wrote = ::send(socket_, data + sent, static_cast<std::size_t>(size) - sent, send_flags);
                if (wrote < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN) {
                        pollfd entry{socket_, POLLOUT, 0};
                        (void)::poll(&entry, 1, 100);
                        continue;
                    }
                    return false;
                }
                sent += static_cast<std::size_t>(wrote);
            }
        }
    }

    // Waits up to `wait_ms` for input and feeds it to the session; false when
    // the connection has ended.
    bool pump(int wait_ms) {
        if (!flush()) return end("write failed");
        pollfd entry{socket_, POLLIN, 0};
        const int ready = ::poll(&entry, 1, wait_ms);
        if (ready <= 0) return true;
        char buffer[16384];
        const auto got = ::recv(socket_, buffer, sizeof buffer, 0);
        if (got == 0) return end("peer closed");
        if (got < 0) return errno == EINTR || errno == EAGAIN ? true : end("read failed");
        if (nghttp2_session_mem_recv2(session_, reinterpret_cast<const std::uint8_t*>(buffer),
                                      static_cast<std::size_t>(got)) < 0) {
            return end("protocol error");
        }
        for (auto& [id, size] : to_consume_) (void)nghttp2_session_consume(session_, id, size);
        to_consume_.clear();
        return flush() || end("write failed");
    }

    // Opens every window held so far, and stops holding.
    void release() {
        holding_ = false;
        for (auto& [id, size] : held_) {
            if (id == 0) continue;
            (void)nghttp2_session_consume(session_, id, size);
        }
        held_.clear();
        (void)flush();
    }

    // Forgets the closed streams; the latency of each 200, in milliseconds,
    // and the count of other outcomes.
    void collect(std::vector<double>& latencies, std::uint64_t& other) {
        for (auto entry = streams_.begin(); entry != streams_.end();) {
            if (!entry->second.closed) {
                ++entry;
                continue;
            }
            if (entry->second.status == 200 && entry->second.error == 0) {
                latencies.push_back(entry->second.closed_ms - entry->second.opened_ms);
            } else {
                ++other;
            }
            if (const auto request = requests_.find(entry->first); request != requests_.end()) {
                delete request->second;
                requests_.erase(request);
            }
            entry = streams_.erase(entry);
        }
        order_.clear();
    }

    std::size_t open_streams() const { return streams_.size(); }

    bool all_closed() const {
        for (const auto& [id, result] : streams_) {
            if (!result.closed) return false;
        }
        return true;
    }

    double elapsed_ms() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
    }

    std::uint64_t bytes(std::int32_t id) const {
        const auto found = streams_.find(id);
        return found == streams_.end() ? 0 : found->second.bytes;
    }
    const StreamResult* stream(std::int32_t id) const {
        const auto found = streams_.find(id);
        return found == streams_.end() ? nullptr : &found->second;
    }

    void hold() { holding_ = true; }
    bool goaway() const { return goaway_; }
    bool ended() const { return !problem_.empty(); }

    std::string json(const std::string& extra) const {
        std::string out = "{\"streams\":[";
        bool first = true;
        for (const auto id : order_) {
            const auto& result = streams_.at(id);
            if (!first) out += ',';
            first = false;
            out += "{\"id\":" + std::to_string(id) + ",\"path\":" + quote(result.path) +
                   ",\"status\":" + std::to_string(result.status) + ",\"bytes\":" + std::to_string(result.bytes) +
                   ",\"closed\":" + (result.closed ? "true" : "false") + ",\"error\":" + std::to_string(result.error) +
                   ",\"opened_ms\":" + std::to_string(result.opened_ms) +
                   ",\"closed_ms\":" + std::to_string(result.closed_ms) + ",\"body\":" + quote(result.body) +
                   ",\"headers\":{";
            bool first_header = true;
            for (const auto& [name, value] : result.headers) {
                if (!first_header) out += ',';
                first_header = false;
                out += quote(name) + ":" + quote(value);
            }
            out += "}}";
        }
        out += "],\"goaway\":" + std::string(goaway_ ? "true" : "false") +
               ",\"goaway_last_stream\":" + std::to_string(goaway_last_) +
               ",\"goaway_error\":" + std::to_string(goaway_error_) + ",\"connection\":" + quote(problem_) + extra +
               "}";
        return out;
    }

    static std::string quote(std::string_view text) {
        std::string out = "\"";
        for (const unsigned char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += static_cast<char>(c);
            } else if (c < 0x20 || c >= 0x7f) {
                char escape[8];
                std::snprintf(escape, sizeof escape, "\\u%04x", c);
                out += escape;
            } else {
                out += static_cast<char>(c);
            }
        }
        return out + "\"";
    }

private:
    bool end(const char* problem) {
        if (problem_.empty()) problem_ = problem;
        return false;
    }

    static Client& of(void* user) { return *static_cast<Client*>(user); }

    static int on_header(nghttp2_session*, const nghttp2_frame* frame, const std::uint8_t* name, std::size_t name_size,
                         const std::uint8_t* value, std::size_t value_size, std::uint8_t, void* user) {
        auto& client = of(user);
        const auto found = client.streams_.find(frame->hd.stream_id);
        if (found == client.streams_.end()) return 0;
        const std::string field(reinterpret_cast<const char*>(name), name_size);
        const std::string text(reinterpret_cast<const char*>(value), value_size);
        if (field == ":status") {
            const int status = std::atoi(text.c_str());
            // A 1xx head is followed by the final one.
            if (status >= 200 || found->second.status == 0) found->second.status = status;
        } else {
            found->second.headers.emplace_back(field, text);
        }
        return 0;
    }

    static int on_data(nghttp2_session*, std::uint8_t, std::int32_t id, const std::uint8_t* data, std::size_t size,
                       void* user) {
        auto& client = of(user);
        const auto found = client.streams_.find(id);
        if (found != client.streams_.end()) {
            found->second.bytes += size;
            const std::size_t cap = client.options_.scenario == "fetch" ? SIZE_MAX : 4096;
            if (found->second.body.size() < cap) {
                found->second.body.append(reinterpret_cast<const char*>(data),
                                          std::min(size, cap - found->second.body.size()));
            }
        }
        if (client.holding_) client.held_[id] += size;
        else client.to_consume_[id] += size;
        return 0;
    }

    static int on_close(nghttp2_session*, std::int32_t id, std::uint32_t error, void* user) {
        auto& client = of(user);
        const auto found = client.streams_.find(id);
        if (found != client.streams_.end()) {
            found->second.closed = true;
            found->second.error = error;
            found->second.closed_ms = client.elapsed_ms();
        }
        return 0;
    }

    static int on_frame(nghttp2_session*, const nghttp2_frame* frame, void* user) {
        auto& client = of(user);
        if (frame->hd.type == NGHTTP2_GOAWAY) {
            client.goaway_ = true;
            client.goaway_last_ = frame->goaway.last_stream_id;
            client.goaway_error_ = frame->goaway.error_code;
        }
        return 0;
    }

    static nghttp2_ssize read_request_body(nghttp2_session*, std::int32_t, std::uint8_t* output, std::size_t size,
                                           std::uint32_t* flags, nghttp2_data_source* source, void*) {
        auto& request = *static_cast<Request*>(source->ptr);
        const auto count = std::min(size, request.body.size() - request.offset);
        std::memcpy(output, request.body.data() + request.offset, count);
        request.offset += count;
        if (request.offset == request.body.size()) *flags |= NGHTTP2_DATA_FLAG_EOF;
        return static_cast<nghttp2_ssize>(count);
    }

    const Options& options_;
    int socket_;
    Clock::time_point started_;
    nghttp2_session* session_ = nullptr;
    std::map<std::int32_t, StreamResult> streams_;
    std::map<std::int32_t, Request*> requests_;
    std::vector<std::int32_t> order_;
    std::map<std::int32_t, std::size_t> to_consume_;
    std::map<std::int32_t, std::size_t> held_;
    bool holding_ = false;
    bool goaway_ = false;
    std::int32_t goaway_last_ = 0;
    std::uint32_t goaway_error_ = 0;
    std::string problem_;
};

int connect_to(int port) {
#ifdef SOCK_CLOEXEC
    const int descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) return -1;
#else
    // macOS: no SOCK_CLOEXEC or MSG_NOSIGNAL; the flags are set apart.
    const int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) return -1;
    ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
    const int on = 1;
    ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {
        ::close(descriptor);
        return -1;
    }
    const int on = 1;
    (void)::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    return descriptor;
}

bool read_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (i + 1 >= argc) return false;
        const std::string value = argv[++i];
        if (arg == "--port") options.port = std::atoi(value.c_str());
        else if (arg == "--scenario") options.scenario = value;
        else if (arg == "--method") options.method = value;
        else if (arg == "--path") options.path = value;
        else if (arg == "--header") {
            const auto colon = value.find(':');
            if (colon == std::string::npos) return false;
            options.headers.emplace_back(value.substr(0, colon), value.substr(colon + 1));
        } else if (arg == "--body") {
            options.body = value;
            options.has_body = true;
        } else if (arg == "--body-stdin") {
            // The request body is standard input (any bytes).
            options.has_body = value == "1";
            if (options.has_body) {
                char chunk[65536];
                std::size_t got = 0;
                while ((got = std::fread(chunk, 1, sizeof chunk, stdin)) > 0) options.body.append(chunk, got);
            }
        } else if (arg == "--count") options.count = std::atoi(value.c_str());
        else if (arg == "--window") options.window = std::atoi(value.c_str());
        else if (arg == "--hold-ms") options.hold_ms = std::atoi(value.c_str());
        else if (arg == "--after-ms") options.after_ms = std::atoi(value.c_str());
        else if (arg == "--header-bytes") options.header_bytes = std::atoi(value.c_str());
        else if (arg == "--pid") options.pid = std::atoi(value.c_str());
        else if (arg == "--timeout-ms") options.timeout_ms = std::atoi(value.c_str());
        else if (arg == "--seconds") options.seconds = std::atoi(value.c_str());
        else return false;
    }
    return options.port > 0 && !options.scenario.empty();
}

// Pumps until `done`, the end of the connection, or the time limit; `step`
// runs each turn with the elapsed time.
template <typename Step, typename Done>
void run_until(Client& client, const Options& options, Step step, Done done) {
    const auto limit = static_cast<double>(options.timeout_ms);
    while (client.elapsed_ms() < limit) {
        step(client.elapsed_ms());
        if (done()) break;
        if (!client.pump(10)) break;
    }
}

template <typename Step>
void run_until_closed(Client& client, const Options& options, Step step) {
    run_until(client, options, step, [&] { return client.all_closed(); });
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!read_options(argc, argv, options)) {
        std::fprintf(stderr, "usage: neura_http2_test_client --port P --scenario S [options]\n");
        return 2;
    }
    const int socket = connect_to(options.port);
    if (socket < 0) {
        std::fprintf(stderr, "cannot connect to port %d\n", options.port);
        return 1;
    }
    Client client(options, socket);
    const auto& scenario = options.scenario;
    const bool manual = scenario == "stall";
    const std::uint32_t peer_streams = scenario == "streams" ? 1000 : 0;
    if (!client.open(manual, peer_streams)) {
        std::fprintf(stderr, "cannot create the nghttp2 session\n");
        return 1;
    }
    std::string extra;
    if (scenario == "fetch") {
        (void)client.submit(options.method, options.path, options.headers, options.has_body ? &options.body : nullptr);
        run_until_closed(client, options, [](double) {});
    } else if (scenario == "interleave" || scenario == "streams") {
        for (int i = 0; i < options.count; ++i) (void)client.submit("GET", options.path, {}, nullptr);
        run_until_closed(client, options, [](double) {});
    } else if (scenario == "stall") {
        client.hold();
        const auto big = client.submit("GET", options.path, {}, nullptr);
        std::int32_t hello = 0;
        std::uint64_t at_hold_end = 0;
        bool released = false;
        run_until_closed(client, options, [&](double now) {
            if (!hello && now >= options.hold_ms / 4.0) hello = client.submit("GET", "/hello", {}, nullptr);
            if (!released && now >= options.hold_ms) {
                at_hold_end = client.bytes(big);
                const auto* small = client.stream(hello);
                extra += ",\"hello_during_stall\":" + std::string(small && small->closed && small->status == 200 &&
                                                                         small->closed_ms < options.hold_ms
                                                                     ? "true"
                                                                     : "false");
                client.release();
                released = true;
            }
        });
        extra += ",\"bytes_at_hold_end\":" + std::to_string(at_hold_end);
    } else if (scenario == "reset") {
        const auto slow = client.submit("GET", options.path, {}, nullptr);
        bool reset = false;
        std::int32_t hello = 0;
        double reset_at = 0;
        run_until(
            client, options,
            [&](double now) {
                if (!reset && now >= options.after_ms) {
                    client.reset(slow);
                    reset = true;
                    reset_at = now;
                }
                if (reset && !hello && now >= reset_at + 300) hello = client.submit("GET", "/hello", {}, nullptr);
            },
            [&] { return hello != 0 && client.all_closed(); });
    } else if (scenario == "headers") {
        std::vector<std::pair<std::string, std::string>> fields{
            {"x-big", std::string(static_cast<std::size_t>(options.header_bytes), 'b')}};
        (void)client.submit("GET", options.path, fields, nullptr);
        run_until_closed(client, options, [](double) {});
    } else if (scenario == "drain") {
        for (int i = 0; i < options.count; ++i) (void)client.submit("GET", options.path, {}, nullptr);
        bool signalled = false;
        bool late = false;
        std::string late_result = "\"not tried\"";
        run_until(client, options, [&](double now) {
            if (!signalled && now >= options.after_ms) {
                if (options.pid > 0) ::kill(options.pid, SIGTERM);
                signalled = true;
            }
            if (!late && client.goaway()) {
                late = true;
                const auto id = client.submit("GET", "/hello", {}, nullptr);
                late_result = id < 0 ? Client::quote(nghttp2_strerror(id)) : "\"submitted\"";
            }
        }, [&] { return signalled && client.all_closed() && (client.goaway() || client.ended()); });
        extra += ",\"after_goaway\":" + late_result;
    } else if (scenario == "load") {
        std::vector<double> latencies;
        std::uint64_t other = 0;
        const double stop = options.seconds * 1000.0;
        while (client.elapsed_ms() < stop && !client.ended()) {
            client.collect(latencies, other);
            while (client.open_streams() < static_cast<std::size_t>(options.count)) {
                if (client.submit("GET", options.path, {}, nullptr) < 0) break;
            }
            if (!client.pump(5)) break;
        }
        const double took = client.elapsed_ms();
        client.collect(latencies, other);
        std::sort(latencies.begin(), latencies.end());
        const auto at = [&](double fraction) {
            if (latencies.empty()) return 0.0;
            const auto index = static_cast<std::size_t>(fraction * static_cast<double>(latencies.size() - 1) + 0.5);
            return latencies[std::min(index, latencies.size() - 1)];
        };
        std::printf("{\"completed\":%zu,\"other\":%llu,\"seconds\":%.3f,\"per_second\":%.1f,\"median_ms\":%.3f,"
                    "\"p95_ms\":%.3f,\"p99_ms\":%.3f,\"connection\":%s}\n",
                    latencies.size(), static_cast<unsigned long long>(other), took / 1000,
                    static_cast<double>(latencies.size()) * 1000 / took, at(0.5), at(0.95), at(0.99),
                    Client::quote(client.ended() ? "ended" : "").c_str());
        ::close(socket);
        return 0;
    } else {
        std::fprintf(stderr, "unknown scenario: %s\n", scenario.c_str());
        return 2;
    }
    std::printf("%s\n", client.json(extra).c_str());
    ::close(socket);
    return 0;
}
