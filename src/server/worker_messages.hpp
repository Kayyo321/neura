#pragma once

// Typed bodies of the Assign and Head frames of the request-worker protocol
// (docs/design/Backend030.md, "Request-worker protocol"), and a blocking
// channel that sends and receives checked frames over one POSIX stream
// socket.

#include "nvm/app_services.hpp"
#include "server/worker_wire.hpp"

#include <chrono>
#include <optional>
#include <span>
#include <vector>

namespace neura::server::worker {

// Assign, version 1, all integers little-endian, each text a uint32 byte
// count and its bytes:
//   uint32 kind (1 request, 2 startup check), text request identifier,
//   int64 deadline (Unix nanoseconds), text peer, text scheme, text method,
//   text target, uint32 header count, that many (text name, text value),
//   int64 body length (-1 when unknown), uint32 lease count (0 in 0.3.0).
std::vector<std::uint8_t> encode_assign(const nvm::ServerRequest& request);
std::optional<nvm::ServerRequest> decode_assign(std::span<const std::uint8_t> body);

// Head, version 1: uint32 status, uint32 header count, that many
// (text name, text value), int64 body length: the exact byte count of a
// complete body, sent with Content-Length, or -1 for a body streamed with
// chunked transfer coding.
struct ResponseHead {
    std::uint32_t status = 0;
    nvm::HeaderPairs headers;
    std::int64_t body_length = -1;
};
std::vector<std::uint8_t> encode_head(const ResponseHead& head);
std::optional<ResponseHead> decode_head(std::span<const std::uint8_t> body);

struct Frame {
    FrameKind kind;
    std::vector<std::uint8_t> body;
};

using Clock = std::chrono::steady_clock;

// One endpoint of a worker channel. Every frame passes the channel's
// ChannelSequence, outgoing frames before they are written. The first
// failure ends the channel. Not thread-safe.
class Channel {
public:
    enum class Status { Ok, Closed, TimedOut, Failed };

    // Takes ownership of `descriptor`, a connected stream socket.
    Channel(int descriptor, PeerRole role, WorkerIdentity expected);
    ~Channel();
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    Status send(FrameKind kind, std::span<const std::uint8_t> body, Clock::time_point deadline);
    // The next complete frame; on any other status, nothing.
    std::optional<Frame> receive(Clock::time_point deadline);
    // The next frame if it is complete without waiting; nothing otherwise,
    // and the status changes only when the channel ended or failed.
    std::optional<Frame> receive_ready();
    Status status() const { return status_; }
    ProtocolFailure failure() const { return sequence_.failure(); }
    const ChannelSequence& sequence() const { return sequence_; }
    int descriptor() const { return descriptor_; }

    // Sends this side's Hello and checks the peer's.
    Status greet(const Hello& hello, Clock::time_point deadline);

    // While a send waits for the peer to read, read what the peer sends into
    // the receive buffer, so two endpoints that both send cannot block each
    // other. The worker sets it: what it buffers is bounded by the request
    // body the host sends (S-6).
    void read_while_sending(bool enabled) { read_while_sending_ = enabled; }

private:
    Status fill(Clock::time_point deadline, bool wait = true);
    std::optional<Frame> receive(Clock::time_point deadline, bool wait);

    int descriptor_;
    PeerRole role_;
    ChannelSequence sequence_;
    FrameDecoder decoder_;
    std::vector<std::uint8_t> buffer_;
    std::size_t offset_ = 0;
    Status status_ = Status::Ok;
    bool read_while_sending_ = false;
    Frame partial_{FrameKind::Hello, {}};
    bool peer_closed_ = false;
};

// The provider map both sides of a server channel expect.
WorkerIdentity server_identity(const ImageDigest& image);

} // namespace neura::server::worker
