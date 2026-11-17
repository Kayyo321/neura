#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace neura::server::worker {

constexpr std::uint32_t protocol_version = 1; // Carried in the Hello body.
constexpr std::size_t frame_header_bytes = 5;
constexpr std::size_t default_frame_bytes = 1 * 1024 * 1024; // Apps030 C-2
constexpr std::size_t max_frame_bytes = 64 * 1024 * 1024;
constexpr std::size_t max_body_chunk_bytes = 64 * 1024;
constexpr std::size_t max_provider_versions = 256;
constexpr std::size_t max_provider_name_bytes = 255;

using ImageDigest = std::array<std::uint8_t, 32>;
constexpr std::size_t hello_prefix_bytes = 4 + 1 + ImageDigest{}.size() + 2;
constexpr std::size_t max_hello_body_bytes =
    hello_prefix_bytes + max_provider_versions * (1 + max_provider_name_bytes + 4);

enum class FrameKind : std::uint8_t {
    Hello = 0x01,
    Assign = 0x02,
    BodyChunk = 0x03,
    BodyEnd = 0x04,
    Call = 0x05,
    Return = 0x06,
    Cancel = 0x07,
    Head = 0x08,
    Done = 0x09,
};

enum class Direction { HostToWorker, WorkerToHost };

class FrameLimits {
public:
    FrameLimits() = default;
    explicit FrameLimits(std::size_t wire_bytes); // Throws if above the C-2 hard maximum.
    std::size_t wire_bytes() const { return wire_bytes_; }

private:
    std::size_t wire_bytes_ = default_frame_bytes;
};

enum class FrameError {
    None,
    InvalidLength,
    TooLarge,
    UnknownKind,
    WrongDirection,
    UnexpectedBody,
    TruncatedHeader,
    TruncatedBody,
};

struct FrameHeader {
    FrameKind kind;
    std::size_t body_bytes;
};

struct EncodedHeader {
    FrameError error;
    std::array<std::uint8_t, frame_header_bytes> bytes{}; // Defined on success.
};

// Encodes the length (kind and body) and the kind. The caller sends the body
// after the header without copying it into a second buffer.
EncodedHeader encode_header(Direction direction, FrameHeader frame, FrameLimits limits = {});

enum class FrameEvent { NeedInput, HeaderReady, BodyData, Complete, Closed, Rejected };

struct ReadResult {
    FrameEvent event;
    std::size_t consumed = 0;
    FrameError error = FrameError::None;
    std::span<const std::uint8_t> body_data{};
};

// One decoder per frame. Hand unconsumed input back to this decoder until
// Complete, then to the decoder for the next frame. A rejection ends the channel.
class FrameDecoder {
public:
    explicit FrameDecoder(Direction direction, FrameLimits limits = {});

    // HeaderReady stops before the first body byte. BodyData borrows from the
    // current input and is tentative until Complete, which may consume zero.
    ReadResult consume(std::span<const std::uint8_t> input);
    // Permanent EOF, after all buffered input has been passed to consume.
    ReadResult finish_input();
    FrameHeader header() const { return header_; } // Defined from HeaderReady.

private:
    enum class State { Length, Kind, Body, Complete, Closed, Rejected };
    ReadResult reject(FrameError error, std::size_t consumed);

    Direction direction_;
    FrameLimits limits_;
    State state_ = State::Length;
    FrameError error_ = FrameError::None;
    std::uint32_t length_ = 0;
    unsigned length_bytes_ = 0;
    FrameHeader header_{};
    std::size_t remaining_ = 0;
};

enum class PeerRole : std::uint8_t { Host = 0x01, Worker = 0x02 };

struct WorkerIdentity {
    ImageDigest image{};
    std::map<std::string, std::uint32_t, std::less<>> providers;

    bool operator==(const WorkerIdentity&) const = default;
};

struct Hello {
    std::uint32_t version = protocol_version;
    PeerRole role = PeerRole::Host;
    WorkerIdentity identity;

    bool operator==(const Hello&) const = default;
};

enum class ProtocolError {
    None,
    Framing,
    MalformedHello,
    UnsupportedVersion,
    WrongPeerRole,
    ImageMismatch,
    ProviderMismatch,
    ExpectedHello,
    DuplicateHello,
    ExpectedAssign,
    DuplicateAssign,
    RequestBodyEnded,
    DuplicateCancel,
    ExpectedHead,
    DuplicateHead,
    ResponseBodyEnded,
    ExpectedResponseBodyEnd,
    DuplicateDone,
    UnexpectedReturn,
    TooManyCalls,
};

struct EncodedHello {
    ProtocolError error;
    std::vector<std::uint8_t> bytes; // Body only, defined on success.
};

struct DecodedHello {
    ProtocolError error;
    Hello value{}; // Defined on success.
};

EncodedHello encode_hello(const Hello& hello);
DecodedHello decode_hello(std::span<const std::uint8_t> body);

struct ProtocolFailure {
    ProtocolError error = ProtocolError::None;
    FrameError framing = FrameError::None; // Specific failure if error is Framing.

    bool operator==(const ProtocolFailure&) const = default;
};

// One instance per endpoint, shared by both directions of its channel. The
// expected identity comes from the verified image and configured providers.
class ChannelSequence {
public:
    explicit ChannelSequence(WorkerIdentity expected, FrameLimits limits = {});

    // Observe a complete received frame or check a complete outgoing frame
    // before sending. Supply exact body bytes only for Hello. The transport
    // owns streaming, serialized writes and terminating the channel on error.
    ProtocolFailure accept(Direction direction, FrameHeader frame, std::span<const std::uint8_t> hello_body = {});
    ProtocolFailure reject_frame(FrameError error); // Pass a frame decoder's rejection here.

    ProtocolFailure failure() const { return failure_; }
    bool ready() const;
    bool response_done() const { return response_ == Response::Done; }

private:
    enum class Response { AwaitingHead, Body, BodyEnded, Done };
    ProtocolError accept_hello(Direction direction, std::size_t body_bytes, std::span<const std::uint8_t> body);
    ProtocolError advance(Direction direction, FrameKind kind);
    ProtocolError advance_body(Direction direction, bool end);

    WorkerIdentity expected_;
    FrameLimits limits_;
    ProtocolFailure failure_;
    bool host_hello_ = false;
    bool worker_hello_ = false;
    bool assigned_ = false;
    bool request_body_ended_ = false;
    bool cancelled_ = false;
    Response response_ = Response::AwaitingHead;
    std::uint64_t outstanding_calls_ = 0;
};

} // namespace neura::server::worker
