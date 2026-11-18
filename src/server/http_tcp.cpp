#include "server/http_tcp.hpp"
#include "server/http_socket.hpp"
#ifdef NEURA_SERVER_HTTP2_ENABLED
#include "server/http2_connection.hpp"
#endif

#include <algorithm>
#include <array>
#include <unistd.h>

namespace neura::server::http {
namespace {

class TcpStream final : public ConnectionStream {
public:
    explicit TcpStream(int descriptor) : descriptor_(descriptor) {}
    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    ~TcpStream() override { ::close(descriptor_); }

    StreamRead read(std::span<char> output, ConnectionClock::time_point deadline) override {
        return read(output, deadline, -1);
    }

    StreamRead read(std::span<char> output, ConnectionClock::time_point deadline, int wake) override {
        while (ConnectionClock::now() < deadline) {
            const auto count = ::recv(descriptor_, output.data(), output.size(), 0);
            if (count > 0) return {StreamStatus::Ready, static_cast<std::size_t>(count)};
            if (count == 0) return {StreamStatus::End};
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return {StreamStatus::Failed};
            const auto status = socket_io::wait_for(descriptor_, POLLIN, deadline, wake);
            if (status != StreamStatus::Ready) return {status};
        }
        return {StreamStatus::TimedOut};
    }

    StreamStatus write_all(std::string_view input, ConnectionClock::time_point deadline) override {
        while (!input.empty()) {
            if (ConnectionClock::now() >= deadline) return StreamStatus::TimedOut;
            const auto count = socket_io::send(descriptor_, input.data(), input.size());
            if (count > 0) {
                input.remove_prefix(static_cast<std::size_t>(count));
                continue;
            }
            if (count == 0) return StreamStatus::Failed;
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) return StreamStatus::Failed;
            const auto status = socket_io::wait_for(descriptor_, POLLOUT, deadline);
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

ConnectionEnd serve_tcp_connection(int descriptor, const Responder& respond, const ConnectionLimits& limits) {
    TcpStream stream(descriptor);
    if (!socket_io::configure(descriptor)) return ConnectionEnd::StreamFailure;
#ifdef NEURA_SERVER_HTTP2_ENABLED
    if (limits.http2.cleartext) return serve_cleartext_connection(stream, respond, limits);
#endif
    return serve_connection(stream, respond, limits);
}

} // namespace neura::server::http
