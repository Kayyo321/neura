// Tests of the worker channel (src/server/worker_messages.cpp) over a real
// socket pair: the channel never blocks in send(2) or recv(2), so a host and
// a worker that both send at once finish, and a send to a peer that never
// reads ends at its deadline. On Windows the pair is platform::socket_pair's
// AF_UNIX pair, which the host makes for each worker there (W27).

#include "server/worker_messages.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <sys/socket.h>
#endif

namespace {

using namespace neura::server::worker;
using namespace std::chrono_literals;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

// A blocking socket pair, like the one the host makes for each worker.
// Its buffers are made small, as macOS's are (8 KiB), so every large frame
// fills them.
#ifdef _WIN32
std::pair<neura::platform::native_socket, neura::platform::native_socket> socket_pair() {
    std::error_code error;
    const auto pair = neura::platform::socket_pair(error);
    check(!error, "socket_pair failed");
    for (const auto socket : pair) {
        const int size = 8 * 1024;
        (void)::setsockopt(socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&size), sizeof size);
        (void)::setsockopt(socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&size), sizeof size);
    }
    return {pair[0], pair[1]};
}
#else
std::pair<int, int> socket_pair() {
    int pair[2];
    check(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "socketpair failed");
    for (const int descriptor : pair) {
        const int size = 8 * 1024;
        (void)::setsockopt(descriptor, SOL_SOCKET, SO_SNDBUF, &size, sizeof size);
        (void)::setsockopt(descriptor, SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
    }
    return {pair[0], pair[1]};
}
#endif

WorkerIdentity identity() { return server_identity(ImageDigest{}); }

Hello hello(PeerRole role) {
    Hello value;
    value.role = role;
    value.identity = identity();
    return value;
}

// macOS ignores MSG_DONTWAIT on send(2) (it honours only a nonblocking
// socket there), so the channel must make its descriptor nonblocking.
#ifdef _WIN32
// Windows has neither MSG_DONTWAIT nor a way to read a socket's FIONBIO
// mode: a recv on an empty channel must fail with WSAEWOULDBLOCK at once. A
// channel that blocked would wait until its peer closes, after 5 s.
void descriptor_is_nonblocking() {
    const auto [host_end, worker_end] = socket_pair();
    const auto host = std::make_unique<Channel>(host_end, PeerRole::Host, identity());
    auto worker = std::make_unique<Channel>(worker_end, PeerRole::Worker, identity());
    const auto empty_recv = [](neura::platform::native_socket socket) {
        return std::async(std::launch::async, [socket] {
            char byte = 0;
            const int got = ::recv(socket, &byte, 1, 0);
            return got < 0 && ::WSAGetLastError() == WSAEWOULDBLOCK;
        });
    };
    auto host_read = empty_recv(host_end);
    auto worker_read = empty_recv(worker_end);
    const bool host_returned = host_read.wait_for(5s) == std::future_status::ready;
    const bool worker_returned = worker_read.wait_for(5s) == std::future_status::ready;
    worker.reset(); // Ends a recv of the host's that blocked.
    check(host_returned && host_read.get(), "the host's channel descriptor blocks");
    check(worker_returned && worker_read.get(), "the worker's channel descriptor blocks");
}
#else
void descriptor_is_nonblocking() {
    const auto [host_end, worker_end] = socket_pair();
    check(!(::fcntl(host_end, F_GETFL) & O_NONBLOCK), "the socket pair started nonblocking");
    Channel host(host_end, PeerRole::Host, identity());
    Channel worker(worker_end, PeerRole::Worker, identity());
    check(::fcntl(host_end, F_GETFL) & O_NONBLOCK, "the host's channel descriptor blocks");
    check(::fcntl(worker_end, F_GETFL) & O_NONBLOCK, "the worker's channel descriptor blocks");
}
#endif

// The worker streams 4 MB of response before it reads the 4 MB body the
// host sends, and the host reads the response only after the body
// (tests/server/server_host_test.py, /stream-first).
void both_send_at_once() {
    const auto [host_end, worker_end] = socket_pair();
    const auto deadline = Clock::now() + 20s;
    const std::vector<std::uint8_t> chunk(max_body_chunk_bytes, 'x');
    constexpr int chunks = 64;
    std::string worker_problem;
    std::thread worker_thread([&, worker_end = worker_end] {
        Channel channel(worker_end, PeerRole::Worker, identity());
        const auto run = [&]() -> std::string {
            if (channel.greet(hello(PeerRole::Worker), deadline) != Channel::Status::Ok) return "worker greeting";
            const auto assign = channel.receive(deadline);
            if (!assign || assign->kind != FrameKind::Assign) return "no Assign";
            channel.read_while_sending(true);
            if (channel.send(FrameKind::Head, encode_head({200, {}, -1}), deadline) != Channel::Status::Ok) {
                return "worker Head";
            }
            for (int n = 0; n < chunks; ++n) {
                if (channel.send(FrameKind::BodyChunk, chunk, deadline) != Channel::Status::Ok) return "worker body";
            }
            std::size_t received = 0;
            for (;;) {
                const auto frame = channel.receive(deadline);
                if (!frame) return "the request body ended early";
                if (frame->kind == FrameKind::BodyEnd) break;
                received += frame->body.size();
            }
            if (received != chunks * chunk.size()) return "the worker lost request body bytes";
            if (channel.send(FrameKind::BodyEnd, {}, deadline) != Channel::Status::Ok ||
                channel.send(FrameKind::Done, {}, deadline) != Channel::Status::Ok) {
                return "worker Done";
            }
            return {};
        };
        worker_problem = run();
    });
    Channel host(host_end, PeerRole::Host, identity());
    std::string host_problem;
    const auto run = [&]() -> std::string {
        if (host.greet(hello(PeerRole::Host), deadline) != Channel::Status::Ok) return "host greeting";
        neura::nvm::ServerRequest request;
        request.request_id = "r";
        request.method = "POST";
        request.target = "/stream-first";
        request.peer = "127.0.0.1";
        request.scheme = "http";
        request.body_length = -1;
        if (host.send(FrameKind::Assign, encode_assign(request), deadline) != Channel::Status::Ok) return "Assign";
        for (int n = 0; n < chunks; ++n) {
            if (host.send(FrameKind::BodyChunk, chunk, deadline) != Channel::Status::Ok) return "host body";
        }
        if (host.send(FrameKind::BodyEnd, {}, deadline) != Channel::Status::Ok) return "host BodyEnd";
        std::size_t received = 0;
        for (;;) {
            const auto frame = host.receive(deadline);
            if (!frame) return "the response ended early";
            if (frame->kind == FrameKind::Done) break;
            if (frame->kind == FrameKind::BodyChunk) received += frame->body.size();
        }
        if (received != chunks * chunk.size()) return "the host lost response bytes";
        return {};
    };
    host_problem = run();
    worker_thread.join();
    check(Clock::now() < deadline, "the exchange ran into its deadline");
    check(host_problem.empty(), host_problem);
    check(worker_problem.empty(), worker_problem);
}

// A send to a peer that never reads waits in poll, and ends at its deadline
// instead of sleeping in send(2).
void send_ends_at_deadline() {
    const auto [host_end, worker_end] = socket_pair();
    std::thread worker_thread([worker_end = worker_end] {
        Channel channel(worker_end, PeerRole::Worker, identity());
        (void)channel.greet(hello(PeerRole::Worker), Clock::now() + 10s);
        // Reads nothing more until the host has given up.
        std::this_thread::sleep_for(1500ms);
    });
    Channel host(host_end, PeerRole::Host, identity());
    check(host.greet(hello(PeerRole::Host), Clock::now() + 10s) == Channel::Status::Ok, "greeting");
    neura::nvm::ServerRequest request;
    request.request_id = "r";
    const auto started = Clock::now();
    const auto deadline = started + 300ms;
    Channel::Status status = host.send(FrameKind::Assign, encode_assign(request), deadline);
    const std::vector<std::uint8_t> chunk(max_body_chunk_bytes, 'y');
    for (int n = 0; n < 64 && status == Channel::Status::Ok; ++n) status = host.send(FrameKind::BodyChunk, chunk, deadline);
    const auto waited = Clock::now() - started;
    worker_thread.join();
    check(status == Channel::Status::TimedOut, "a send to a peer that never reads did not time out");
    check(waited < 1200ms, "a send to a peer that never reads outlived its deadline");
}

} // namespace

int main() {
    try {
        descriptor_is_nonblocking();
        both_send_at_once();
        send_ends_at_deadline();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
    std::cout << "worker channel checks passed\n";
    return 0;
}
