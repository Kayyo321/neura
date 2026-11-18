#include "server/http_tcp.hpp"

#include <array>
#include <cerrno>
#include <future>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

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

class Socket {
public:
    Socket() = default;
    explicit Socket(int descriptor) : descriptor_(descriptor) { check(descriptor_ >= 0, "could not open TCP socket"); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : descriptor_(other.release()) {}
    Socket& operator=(Socket&& other) noexcept {
        if (descriptor_ >= 0) ::close(descriptor_);
        descriptor_ = other.release();
        return *this;
    }
    ~Socket() { if (descriptor_ >= 0) ::close(descriptor_); }

    int get() const { return descriptor_; }
    int release() { return std::exchange(descriptor_, -1); }

private:
    int descriptor_ = -1;
};

class Loopback {
public:
    explicit Loopback(BufferedResponder respond, ConnectionLimits limits = {}) {
        const Socket listener(::socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0, "loopback bind failed");
        check(::listen(listener.get(), 1) == 0, "loopback listen failed");
        socklen_t length = sizeof address;
        check(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) == 0, "no ephemeral port assigned");

        client_ = Socket(::socket(AF_INET, SOCK_STREAM, 0));
        const timeval timeout{3, 0};
        check(::setsockopt(client_.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0 &&
              ::setsockopt(client_.get(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) == 0,
              "could not set test socket deadlines");
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        check(::setsockopt(client_.get(), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) == 0, "could not suppress SIGPIPE");
#endif
        check(::connect(client_.get(), reinterpret_cast<const sockaddr*>(&address), length) == 0, "loopback connect failed");
        Socket accepted(::accept(listener.get(), nullptr, nullptr));
        const int send_buffer = 4096;
        check(::setsockopt(accepted.get(), SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof send_buffer) == 0, "could not set server send buffer");

        std::packaged_task task([socket = std::move(accepted), responder = std::move(respond), limits]() mutable {
            return serve_tcp_connection(socket.release(), responder, limits);
        });
        end_ = task.get_future();
        server_ = std::jthread(std::move(task));
    }

    void send(string_view input) const {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        while (!input.empty()) {
            const auto count = ::send(client_.get(), input.data(), input.size(), flags);
            if (count < 0 && errno == EINTR) continue;
            check(count > 0, "client could not send request");
            input.remove_prefix(static_cast<std::size_t>(count));
        }
    }

    void finish_writes() const { check(::shutdown(client_.get(), SHUT_WR) == 0, "client half-close failed"); }

    string receive(std::size_t size) const {
        string reply(size, '\0');
        std::size_t have = 0;
        while (have < reply.size()) {
            const auto count = ::recv(client_.get(), reply.data() + have, reply.size() - have, 0);
            if (count < 0 && errno == EINTR) continue;
            check(count > 0, "client did not receive the expected number of response bytes");
            have += static_cast<std::size_t>(count);
        }
        return reply;
    }

    string receive_to_close() const {
        string reply;
        std::array<char, 4096> buffer;
        for (;;) {
            const auto count = ::recv(client_.get(), buffer.data(), buffer.size(), 0);
            if (count == 0) return reply;
            if (count < 0 && errno == EINTR) continue;
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
