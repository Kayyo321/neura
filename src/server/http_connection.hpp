#pragma once

#include "server/http_wire.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace neura::server::http {

using ConnectionClock = std::chrono::steady_clock;

struct ConnectionLimits {
    RequestLimits request;
    std::size_t response_bytes = 8 * 1024 * 1024; // Apps030 S-7
    std::chrono::milliseconds head_timeout{std::chrono::seconds{10}};
    std::chrono::milliseconds request_timeout{std::chrono::seconds{30}}; // Apps030 S-8
    // Called once after each response the responder produced has been
    // written, or has failed to be written. The server host counts a
    // request as in flight until then, so a drain never cuts a response off.
    std::function<void()> response_finished{};
};

// Woken: a read given a wake descriptor returned because that descriptor
// became readable before the stream did.
enum class StreamStatus { Ready, End, TimedOut, Failed, Woken };

struct StreamRead {
    StreamStatus status;
    std::size_t size = 0; // A Ready result contains at least one byte.
};

class ConnectionStream {
public:
    virtual ~ConnectionStream() = default;
    virtual StreamRead read(std::span<char> output, ConnectionClock::time_point deadline) = 0;
    // As read(), and also returns Woken when `wake` (a descriptor, or -1 for
    // none) becomes readable first. The default ignores `wake`.
    virtual StreamRead read(std::span<char> output, ConnectionClock::time_point deadline, int wake) {
        (void)wake;
        return read(output, deadline);
    }
    // Ready means the whole input was sent. A failure can follow a partial write.
    virtual StreamStatus write_all(std::string_view input, ConnectionClock::time_point deadline) = 0;
    virtual void close_after_response() = 0;
};

struct BufferedResponse {
    int status = 200;
    std::string body;
    // Fields written after the status line. The connection owns framing:
    // it writes Content-Length and Connection itself and ignores any
    // Content-Length, Transfer-Encoding or Connection given here. A name
    // that is not a token or a value with CR, LF or NUL is a 500.
    std::vector<Header> headers{};
};

// Called once per complete request, in connection order. The request head
// and decoded body are valid for the duration of the call.
using BufferedResponder = std::function<BufferedResponse(const RequestHead&, std::string_view)>;

// The decoded body of the request being served, read as the responder asks
// for it. The connection reads the client only inside read(), so a request is
// handed to the responder at the end of its head and its body streams.
class RequestBody {
public:
    enum class Status {
        Data,     // `data` holds at least one decoded byte, valid until the next read().
        End,      // The body is complete.
        Rejected, // The body broke a rule (S-6 or chunk syntax); `status` is the answer.
        TimedOut, // The request deadline passed.
        Failed,   // The client closed or the stream failed.
        Woken,    // The wake descriptor became readable first; read again later.
    };
    struct Piece {
        Status status;
        std::string_view data{};
        int status_code = 0;
    };
    virtual ~RequestBody() = default;
    virtual Piece read() = 0;
    // While read() waits for the client, it also returns Woken when
    // `descriptor` becomes readable; -1 stops that.
    virtual void wake_on(int descriptor) { (void)descriptor; }
};

// The response of the request being served. Nothing reaches the client
// before start(). A body of known length is sent with Content-Length, any
// other with chunked transfer coding. The connection owns Content-Length,
// Transfer-Encoding and Connection, and ignores those fields in `headers`.
class ResponseWriter {
public:
    virtual ~ResponseWriter() = default;
    // Writes the status line and fields. A status outside 200 to 599, a
    // field name that is not a token, a value with a control character, or a
    // body length on 204, 205 or 304 is answered with 500 and closes the
    // connection, and start() returns Failed.
    virtual StreamStatus start(int status, const std::vector<Header>& headers,
                               std::optional<std::uint64_t> length) = 0;
    // Body bytes, written before write() returns (backpressure). Bytes past a
    // declared length fail. For HEAD, and for 204 and 304, nothing is sent.
    virtual StreamStatus write(std::string_view bytes) = 0;
    // Ends the body: the last chunk, or a check that the declared length was
    // sent. A response that is started and never finished closes the
    // connection without completing its body.
    virtual StreamStatus finish() = 0;
    virtual bool started() const = 0;
};

// Called once per request at the end of its head, in connection order, with
// the request deadline (S-8, from the end of the head). The responder reads
// the body and writes the response; what it leaves unread of a body is read
// and discarded before the next request. A responder that returns without
// starting a response is answered with 500.
using Responder = std::function<void(const RequestHead&, RequestBody&, ResponseWriter&, ConnectionClock::time_point)>;

// A responder that reads the whole body and then calls `respond`.
Responder buffered(BufferedResponder respond, std::size_t response_bytes = 8 * 1024 * 1024);

enum class ConnectionEnd {
    PeerClosed,
    TruncatedRequest,
    HeadTimeout,
    RequestTimeout,
    RejectedRequest,
    ResponseClosed,
    StreamFailure,
};

// Runs the exchange on one stream. The caller owns the transport. The next
// head's deadline starts when the previous response is written. The request
// deadline starts at the end of the head and bounds transport waits.
ConnectionEnd serve_connection(ConnectionStream& stream, const Responder& respond,
                               const ConnectionLimits& limits = {});
ConnectionEnd serve_connection(ConnectionStream& stream, const BufferedResponder& respond,
                               const ConnectionLimits& limits = {});

} // namespace neura::server::http
