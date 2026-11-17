#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace neura::server::worker {

constexpr std::uint32_t protocol_version = 1; // Carried in the Hello body.
constexpr std::size_t frame_header_bytes = 5;
constexpr std::size_t default_frame_bytes = 1 * 1024 * 1024; // Apps030 C-2
constexpr std::size_t max_frame_bytes = 64 * 1024 * 1024;
constexpr std::size_t max_body_chunk_bytes = 64 * 1024;

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

} // namespace neura::server::worker
