#pragma once

#include "server/http_wire.hpp"

#include <chrono>
#include <functional>
#include <span>

namespace neura::server::http {

using ConnectionClock = std::chrono::steady_clock;

struct ConnectionLimits {
    RequestLimits request;
    std::size_t response_bytes = 8 * 1024 * 1024; // Apps030 S-7
    std::chrono::milliseconds head_timeout{std::chrono::seconds{10}};
    std::chrono::milliseconds request_timeout{std::chrono::seconds{30}}; // Apps030 S-8
};

enum class StreamStatus { Ready, End, TimedOut, Failed };

struct StreamRead {
    StreamStatus status;
    std::size_t size = 0; // A Ready result contains at least one byte.
};

class ConnectionStream {
public:
    virtual ~ConnectionStream() = default;
    virtual StreamRead read(std::span<char> output, ConnectionClock::time_point deadline) = 0;
    // Ready means the whole input was sent. A failure can follow a partial write.
    virtual StreamStatus write_all(std::string_view input, ConnectionClock::time_point deadline) = 0;
    virtual void close_after_response() = 0;
};

struct BufferedResponse {
    int status = 200;
    std::string body;
};

// Called once per complete request, in connection order. The request head
// and decoded body are valid for the duration of the call.
using BufferedResponder = std::function<BufferedResponse(const RequestHead&, std::string_view)>;

enum class ConnectionEnd {
    PeerClosed,
    TruncatedRequest,
    HeadTimeout,
    RequestTimeout,
    RejectedRequest,
    ResponseClosed,
    StreamFailure,
};

// Runs the buffered exchange on one stream. The caller owns the transport.
// The next head's deadline starts when the previous response is written. The
// request deadline bounds transport waits and checks the responder's return.
ConnectionEnd serve_connection(ConnectionStream& stream, const BufferedResponder& respond,
                               const ConnectionLimits& limits = {});

} // namespace neura::server::http
