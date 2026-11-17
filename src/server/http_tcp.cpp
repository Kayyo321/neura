#include "server/http_tcp.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace neura::server::http {
namespace {

bool configure_socket(int descriptor) {
    const int status = ::fcntl(descriptor, F_GETFL);
    const int flags = ::fcntl(descriptor, F_GETFD);
    if (status < 0 || flags < 0 || ::fcntl(descriptor, F_SETFL, status | O_NONBLOCK) < 0 ||
        ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) < 0) return false;
#endif
    return true;
}

StreamStatus wait_for(int descriptor, short events, ConnectionClock::time_point deadline) {
    for (;;) {
        const auto left = deadline - ConnectionClock::now();
        if (left <= ConnectionClock::duration::zero()) return StreamStatus::TimedOut;
        const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(left).count();
        pollfd entry{descriptor, events, 0};
        const int timeout = milliseconds > INT_MAX ? INT_MAX : static_cast<int>(milliseconds);
        const int ready = ::poll(&entry, 1, timeout);
        // A hangup or error goes back through recv/send to report the outcome.
        if (ready > 0) return (entry.revents & POLLNVAL) ? StreamStatus::Failed : StreamStatus::Ready;
        if (ready < 0 && errno != EINTR) return StreamStatus::Failed;
    }
}

class TcpStream final : public ConnectionStream {
public:
    explicit TcpStream(int descriptor) : descriptor_(descriptor) {}
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    ~TcpStream() override { ::close(descriptor_); }

    StreamRead read(std::span<char> output, ConnectionClock::time_point deadline) override {
        while (ConnectionClock::now() < deadline) {
            const auto count = ::recv(descriptor_, output.data(), output.size(), 0);
            if (count > 0) return {StreamStatus::Ready, static_cast<std::size_t>(count)};
            if (count == 0) return {StreamStatus::End};
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return {StreamStatus::Failed};
            const auto status = wait_for(descriptor_, POLLIN, deadline);
            if (status != StreamStatus::Ready) return {status};
        }
        return {StreamStatus::TimedOut};
    }

    StreamStatus write_all(std::string_view input, ConnectionClock::time_point deadline) override {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        while (!input.empty()) {
            if (ConnectionClock::now() >= deadline) return StreamStatus::TimedOut;
            const auto count = ::send(descriptor_, input.data(), input.size(), flags);
            if (count > 0) {
                input.remove_prefix(static_cast<std::size_t>(count));
                continue;
            }
            if (count == 0) return StreamStatus::Failed;
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return StreamStatus::Failed;
            const auto status = wait_for(descriptor_, POLLOUT, deadline);
            if (status != StreamStatus::Ready) return status;
        }
        return StreamStatus::Ready;
    }

    void close_after_response() override {
        // RFC 9112 recommends a half-close first so unread pipelined data does
        // not reset the TCP connection before the last response is received.
        if (::shutdown(descriptor_, SHUT_WR) != 0) return;
        const auto deadline = ConnectionClock::now() + std::chrono::milliseconds{100};
        std::array<char, 4096> discarded;
        std::size_t budget = 64 * 1024;
        while (budget != 0) {
            const auto input = read(std::span(discarded).first(std::min(budget, discarded.size())), deadline);
            if (input.status != StreamStatus::Ready) return;
            budget -= input.size;
        }
    }

private:
    int descriptor_;
};

} // namespace

ConnectionEnd serve_tcp_connection(int descriptor, const BufferedResponder& respond, const ConnectionLimits& limits) {
    TcpStream stream(descriptor);
    if (!configure_socket(descriptor)) return ConnectionEnd::StreamFailure;
    return serve_connection(stream, respond, limits);
}

} // namespace neura::server::http
