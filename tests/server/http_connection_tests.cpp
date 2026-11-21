#include "server/http_tcp.hpp"

#include <array>
#include <cerrno>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include "platform/socket.hpp"
#include "platform/windows.hpp"
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using namespace neura::server::http;
using namespace std::chrono_literals;
using std::string;
using std::string_view;

constexpr string_view get = "GET /parts?q=bolt HTTP/1.1\r\nHost: example.test\r\n\r\n";
constexpr string_view post = "POST /parts HTTP/1.1\r\nHost: example.test\r\n";

void check(bool condition, string_view message) {
    if (!condition) throw std::runtime_error(string(message));
}

// The calls that differ on Winsock: a SOCKET closed with closesocket, char
// option values, int lengths, DWORD timeouts and WSAGetLastError.
#ifdef _WIN32
using native_socket = neura::platform::native_socket;
constexpr native_socket no_socket = neura::platform::invalid_socket;
void close_socket(native_socket socket) { ::closesocket(socket); }
bool interrupted() { return ::WSAGetLastError() == WSAEINTR; }
int set_int_option(native_socket socket, int level, int name, int value) {
    return ::setsockopt(socket, level, name, reinterpret_cast<const char*>(&value), sizeof value);
}
int set_timeout(native_socket socket, int name, std::chrono::milliseconds timeout) {
    const DWORD value = static_cast<DWORD>(timeout.count());
    return ::setsockopt(socket, SOL_SOCKET, name, reinterpret_cast<const char*>(&value), sizeof value);
}
int send_some(native_socket socket, string_view input) {
    return ::send(socket, input.data(), static_cast<int>(input.size()), 0);
}
int receive_some(native_socket socket, char* output, std::size_t size) {
    return ::recv(socket, output, static_cast<int>(size), 0);
}
constexpr int shut_writes = SD_SEND;
#else
using native_socket = int;
constexpr native_socket no_socket = -1;
void close_socket(native_socket socket) { ::close(socket); }
bool interrupted() { return errno == EINTR; }
int set_int_option(native_socket socket, int level, int name, int value) {
    return ::setsockopt(socket, level, name, &value, sizeof value);
}
int set_timeout(native_socket socket, int name, std::chrono::milliseconds timeout) {
    const timeval value{static_cast<time_t>(timeout.count() / 1000),
                        static_cast<suseconds_t>(timeout.count() % 1000 * 1000)};
    return ::setsockopt(socket, SOL_SOCKET, name, &value, sizeof value);
}
ssize_t send_some(native_socket socket, string_view input) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return ::send(socket, input.data(), input.size(), flags);
}
ssize_t receive_some(native_socket socket, char* output, std::size_t size) { return ::recv(socket, output, size, 0); }
constexpr int shut_writes = SHUT_WR;
#endif

class Socket {
public:
    Socket() = default;
    explicit Socket(native_socket descriptor) : descriptor_(descriptor) {
        check(descriptor_ != no_socket, "could not open TCP socket");
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : descriptor_(other.release()) {}
    Socket& operator=(Socket&& other) noexcept {
        if (descriptor_ != no_socket) close_socket(descriptor_);
        descriptor_ = other.release();
        return *this;
    }
    ~Socket() { if (descriptor_ != no_socket) close_socket(descriptor_); }

    native_socket get() const { return descriptor_; }
    native_socket release() { return std::exchange(descriptor_, no_socket); }

private:
    native_socket descriptor_ = no_socket;
};

class Loopback {
public:
    template <typename Respond>
    explicit Loopback(Respond respond, ConnectionLimits limits = {}) {
#ifdef _WIN32
        check(!neura::platform::ensure_sockets(), "could not start Winsock");
#endif
        const Socket listener(::socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0, "loopback bind failed");
        check(::listen(listener.get(), 1) == 0, "loopback listen failed");
        socklen_t length = sizeof address;
        check(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) == 0, "no ephemeral port assigned");

        client_ = Socket(::socket(AF_INET, SOCK_STREAM, 0));
        check(set_timeout(client_.get(), SO_RCVTIMEO, 3s) == 0 && set_timeout(client_.get(), SO_SNDTIMEO, 3s) == 0,
              "could not set test socket deadlines");
#ifdef SO_NOSIGPIPE
        check(set_int_option(client_.get(), SOL_SOCKET, SO_NOSIGPIPE, 1) == 0, "could not suppress SIGPIPE");
#endif
        check(::connect(client_.get(), reinterpret_cast<const sockaddr*>(&address), length) == 0, "loopback connect failed");
        Socket accepted(::accept(listener.get(), nullptr, nullptr));
        check(set_int_option(accepted.get(), SOL_SOCKET, SO_SNDBUF, 4096) == 0, "could not set server send buffer");

        std::packaged_task task([socket = std::move(accepted), responder = std::move(respond), limits]() mutable {
            return serve_tcp_connection(socket.release(), responder, limits);
        });
        end_ = task.get_future();
        server_ = std::jthread(std::move(task));
    }

    void send(string_view input) const {
        while (!input.empty()) {
            const auto count = send_some(client_.get(), input);
            if (count < 0 && interrupted()) continue;
            check(count > 0, "client could not send request");
            input.remove_prefix(static_cast<std::size_t>(count));
        }
    }

    void finish_writes() const { check(::shutdown(client_.get(), shut_writes) == 0, "client half-close failed"); }

    string receive(std::size_t size) const {
        string reply(size, '\0');
        std::size_t have = 0;
        while (have < reply.size()) {
            const auto count = receive_some(client_.get(), reply.data() + have, reply.size() - have);
            if (count < 0 && interrupted()) continue;
            check(count > 0, "client did not receive the expected number of response bytes");
            have += static_cast<std::size_t>(count);
        }
        return reply;
    }

    string receive_to_close() const {
        string reply;
        std::array<char, 4096> buffer;
        for (;;) {
            const auto count = receive_some(client_.get(), buffer.data(), buffer.size());
            if (count == 0) return reply;
            if (count < 0 && interrupted()) continue;
            check(count > 0, "connection did not close cleanly");
            reply.append(buffer.data(), static_cast<std::size_t>(count));
        }
    }

    ConnectionEnd end() { return end_.get(); }

private:
    std::future<ConnectionEnd> end_;
    std::jthread server_;
    Socket client_; // Close before joining the server, including if a test fails.
};

BufferedResponse unexpected_request(const RequestHead&, string_view) { throw std::runtime_error("request reached responder"); }

string ok(string_view body, bool head = false, bool close = false) {
    string reply = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    if (close) reply += "Connection: close\r\n";
    reply += "\r\n";
    if (!head) reply += body;
    return reply;
}

void check_error(string_view response, string_view status) {
    check(response.starts_with("HTTP/1.1 " + string(status) + " ") &&
          response.ends_with("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"), "wrong reject response");
}

void pipeline() {
    std::vector<string> requests;
    Loopback client([&](const RequestHead& head, string_view body) {
        check(body == (head.method == "POST" ? "bolt" : ""), "body crossed a pipeline boundary");
        requests.push_back(head.method + " " + head.target);
        return BufferedResponse{200, head.target};
    });
    const string wire = string(post) + "Content-Length: 4\r\n\r\nbolt" + string(get) +
        "HEAD /parts/7 HTTP/1.1\r\nHost: example.test\r\n\r\n" +
        "GET /last HTTP/1.1\r\nHost: example.test\r\nConnection: close\r\n\r\n" + string(get);
    client.send(wire);
    client.finish_writes();
    check(client.receive_to_close() == ok("/parts") + ok("/parts?q=bolt") + ok("/parts/7", true) + ok("/last", false, true),
          "pipelined responses were not ordered or HEAD sent a body");
    check(client.end() == ConnectionEnd::ResponseClosed, "Connection: close did not end the exchange");
    check(requests == std::vector<string>{"POST /parts", "GET /parts?q=bolt", "HEAD /parts/7", "GET /last"},
          "pipelined requests were dispatched out of order or after close");
}

void continue_and_chunked() {
    string body(32769, 'x');
    body[4] = '\0';
    ConnectionLimits limits;
    limits.request.body_bytes = body.size();
    Loopback client([&](const RequestHead& head, string_view received) {
        check(head.framing == BodyFraming::Chunked && received == body, "responder received the wrong decoded body");
        return BufferedResponse{200, "received"};
    }, limits);
    client.send(post.substr(0, 22));
    client.send(string(post.substr(22)) + "Transfer-Encoding: chunked\r\nExpect: 100-continue\r\n\r\n");
    constexpr string_view interim = "HTTP/1.1 100 Continue\r\n\r\n";
    check(client.receive(interim.size()) == interim, "server did not flush 100 before the client sent a body");
    client.send("8000\r\n" + body.substr(0, 32768));
    client.send("\r\n1\r\nx\r\n0\r\n\r\n");
    client.finish_writes();
    check(client.receive_to_close() == ok("received"), "chunked request did not receive a final response");
    check(client.end() == ConnectionEnd::PeerClosed, "clean EOF between requests was reported as an error");
}

void rejection() {
    struct Case { string wire; string_view status; RequestLimits limits; };
    const Case cases[] = {
        {string(get), "431", {get.size() - 1, 4}},
        {string(post) + "Content-Length: 5\r\nExpect: 100-continue\r\n\r\n", "413", {1024, 4}},
        {string(post) + "Transfer-Encoding: chunked\r\n\r\n2\r\nab\r\n3\r\n", "413", {1024, 4}},
        {string(post) + "Transfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n", "400", {}},
        {string(post) + "Upgrade: websocket\r\n\r\n", "501", {}},
    };
    for (const auto& fixture : cases) {
        Loopback client(unexpected_request, {fixture.limits});
        client.send(fixture.wire + string(get));
        client.finish_writes();
        check_error(client.receive_to_close(), fixture.status);
        check(client.end() == ConnectionEnd::RejectedRequest, "bad request did not close the connection");
    }

    Loopback queued(unexpected_request, {{1024, 4}});
    queued.send(string(post) + "Content-Length: 32768\r\n\r\n" + string(32768, 'x') + string(get));
    queued.finish_writes();
    check_error(queued.receive_to_close(), "413");
    check(queued.end() == ConnectionEnd::RejectedRequest, "unread TCP data prevented a clean rejection");
}

void truncated() {
    for (const auto& incomplete : {
         string("GET /parts HTTP/1.1\r\nHost:"), string(post) + "Content-Length: 4\r\n\r\nabc",
         string(post) + "Transfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n"}) {
        Loopback client(unexpected_request);
        client.send(incomplete);
        client.finish_writes();
        check(client.receive_to_close().empty(), "truncated request should close with no reply");
        check(client.end() == ConnectionEnd::TruncatedRequest, "EOF did not identify a truncated request");
    }
}

void timeouts() {
    class SplitHead final : public ConnectionStream {
    public:
        StreamRead read(std::span<char> output, ConnectionClock::time_point deadline) override {
            deadlines.push_back(deadline);
            if (deadlines.size() > 2) return {StreamStatus::TimedOut};
            output[0] = deadlines.size() == 1 ? 'G' : 'E';
            return {StreamStatus::Ready, 1};
        }
        StreamStatus write_all(string_view, ConnectionClock::time_point) override {
            throw std::runtime_error("head timeout wrote a response");
        }
        void close_after_response() override { throw std::runtime_error("head timeout performed a response close"); }
        std::vector<ConnectionClock::time_point> deadlines;
    } split;
    check(serve_connection(split, unexpected_request) == ConnectionEnd::HeadTimeout, "read timeout did not close the head");
    check(split.deadlines.size() == 3 && split.deadlines[0] == split.deadlines[1] && split.deadlines[1] == split.deadlines[2],
          "a partial head reset its absolute read deadline");

    ConnectionLimits limits;
    limits.head_timeout = 100ms;
    Loopback head(unexpected_request, limits);
    head.send("GET /pa");
    check(head.receive_to_close().empty(), "monotonic head timeout should close with no reply");
    check(head.end() == ConnectionEnd::HeadTimeout, "TCP read did not honor the head deadline");

    limits.head_timeout = 3s;
    limits.request_timeout = 50ms;
    Loopback body(unexpected_request, limits);
    body.send(string(post) + "Content-Length: 3\r\n\r\na");
    check_error(body.receive_to_close(), "504");
    body.finish_writes();
    check(body.end() == ConnectionEnd::RequestTimeout, "body read did not honor the request deadline");

    // The server host's 504 for a request past S-8 starts after the
    // connection's deadline when the host is slow to see its own (50 ms
    // earlier): it still reaches the client whole.
    const Responder late = [](const RequestHead&, RequestBody&, ResponseWriter& writer,
                              ConnectionClock::time_point deadline) {
        std::this_thread::sleep_until(deadline + 100ms);
        const string answer = "{\"error\":\"timeout\"}";
        if (writer.start(504, {}, answer.size()) != StreamStatus::Ready) return;
        if (writer.write(answer) != StreamStatus::Ready) return;
        (void)writer.finish();
    };
    limits.request_timeout = 50ms;
    Loopback past(late, limits);
    past.send(get);
    past.finish_writes();
    const auto reply = past.receive_to_close();
    check(reply.starts_with("HTTP/1.1 504 ") && reply.ends_with("\r\n\r\n{\"error\":\"timeout\"}"),
          "a response started past the deadline did not reach the client: " + reply);
}

void response_limit() {
    const string large(256 * 1024, 'x');
    const auto responder = [&](const RequestHead&, string_view) { return BufferedResponse{200, large}; };
    ConnectionLimits limits;
    limits.response_bytes = large.size();
    Loopback exact(responder, limits);
    exact.send(get);
    exact.finish_writes();
    check(exact.receive_to_close() == ok(large), "partial socket writes lost buffered response bytes at S-7");
    check(exact.end() == ConnectionEnd::PeerClosed, "buffered response did not close cleanly");

    --limits.response_bytes;
    Loopback over(responder, limits);
    over.send(get);
    over.finish_writes();
    check_error(over.receive_to_close(), "500");
    check(over.end() == ConnectionEnd::ResponseClosed, "oversized response did not end the exchange");
}

} // namespace

int main() {
    constexpr std::pair<string_view, void (*)()> suites[] = {
        {"keep-alive pipeline", pipeline},
        {"continue and chunked", continue_and_chunked},
        {"rejection", rejection},
        {"truncation", truncated},
        {"deadlines", timeouts},
        {"response limit", response_limit},
    };
    for (const auto& [name, run] : suites) {
        try {
            run();
        } catch (const std::exception& error) {
            std::cerr << name << ": " << error.what() << '\n';
            return 1;
        }
    }
}
