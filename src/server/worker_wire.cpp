#include "server/worker_wire.hpp"

#include <algorithm>
#include <stdexcept>

namespace neura::server::worker {
namespace {

constexpr unsigned length_field_bytes = frame_header_bytes - 1;

FrameError check_direction(Direction direction, FrameKind kind) {
    switch (kind) {
    case FrameKind::Hello:
    case FrameKind::BodyChunk:
    case FrameKind::BodyEnd:
        return FrameError::None;
    case FrameKind::Assign:
    case FrameKind::Return:
    case FrameKind::Cancel:
        return direction == Direction::HostToWorker ? FrameError::None : FrameError::WrongDirection;
    case FrameKind::Call:
    case FrameKind::Head:
    case FrameKind::Done:
        return direction == Direction::WorkerToHost ? FrameError::None : FrameError::WrongDirection;
    }
    return FrameError::UnknownKind;
}

FrameError check_header(Direction direction, FrameHeader frame, FrameLimits limits) {
    if (const auto error = check_direction(direction, frame.kind); error != FrameError::None) return error;
    if (frame.kind == FrameKind::BodyChunk) {
        return frame.body_bytes <= max_body_chunk_bytes ? FrameError::None : FrameError::TooLarge;
    }
    // Subtract the fixed overhead before comparing so an untrusted size cannot wrap.
    if (limits.wire_bytes() < frame_header_bytes || frame.body_bytes > limits.wire_bytes() - frame_header_bytes) {
        return FrameError::TooLarge;
    }
    if ((frame.kind == FrameKind::BodyEnd || frame.kind == FrameKind::Done) && frame.body_bytes != 0) {
        return FrameError::UnexpectedBody;
    }
    return FrameError::None;
}

} // namespace

FrameLimits::FrameLimits(std::size_t wire_bytes) : wire_bytes_(wire_bytes) {
    if (wire_bytes > max_frame_bytes) throw std::invalid_argument("worker frame limit exceeds C-2 maximum");
}

EncodedHeader encode_header(Direction direction, FrameHeader frame, FrameLimits limits) {
    EncodedHeader result{check_header(direction, frame, limits)};
    if (result.error != FrameError::None) return result;

    const auto length = static_cast<std::uint32_t>(frame.body_bytes + 1);
    for (unsigned index = 0; index < length_field_bytes; ++index) {
        result.bytes[index] = static_cast<std::uint8_t>(length >> (8 * index));
    }
    result.bytes[length_field_bytes] = static_cast<std::uint8_t>(frame.kind);
    return result;
}

FrameDecoder::FrameDecoder(Direction direction, FrameLimits limits) : direction_(direction), limits_(limits) {}

ReadResult FrameDecoder::reject(FrameError error, std::size_t consumed) {
    state_ = State::Rejected;
    error_ = error;
    return {FrameEvent::Rejected, consumed, error};
}

ReadResult FrameDecoder::consume(std::span<const std::uint8_t> input) {
    if (state_ == State::Complete) return {FrameEvent::Complete};
    if (state_ == State::Closed) return {FrameEvent::Closed};
    if (state_ == State::Rejected) return {FrameEvent::Rejected, 0, error_};

    std::size_t consumed = 0;
    if (state_ == State::Length) {
        while (consumed < input.size() && length_bytes_ < length_field_bytes) {
            const auto byte = static_cast<std::uint32_t>(input[consumed++]);
            length_ |= byte << (8 * length_bytes_);
            ++length_bytes_;
        }
        if (length_bytes_ < length_field_bytes) return {FrameEvent::NeedInput, consumed};
        if (length_ == 0) return reject(FrameError::InvalidLength, consumed);
        // Until the kind arrives, a frame can use either bound. Reject a prefix
        // outside both bounds before accepting a kind or any body bytes.
        const auto largest_frame = std::max(limits_.wire_bytes(), max_body_chunk_bytes + frame_header_bytes);
        if (static_cast<std::uint64_t>(length_) + length_field_bytes > largest_frame) {
            return reject(FrameError::TooLarge, consumed);
        }
        state_ = State::Kind;
    }
    if (state_ == State::Kind) {
        if (consumed == input.size()) return {FrameEvent::NeedInput, consumed};
        header_ = {static_cast<FrameKind>(input[consumed++]), static_cast<std::size_t>(length_ - 1)};
        if (const auto error = check_header(direction_, header_, limits_); error != FrameError::None) {
            return reject(error, consumed);
        }
        remaining_ = header_.body_bytes;
        state_ = remaining_ == 0 ? State::Complete : State::Body;
        return {FrameEvent::HeaderReady, consumed};
    }

    if (input.empty()) return {FrameEvent::NeedInput};
    const auto count = std::min(remaining_, input.size());
    remaining_ -= count;
    if (remaining_ == 0) state_ = State::Complete;
    return {FrameEvent::BodyData, count, FrameError::None, input.first(count)};
}

ReadResult FrameDecoder::finish_input() {
    if (state_ == State::Length && length_bytes_ == 0) state_ = State::Closed;
    if (state_ == State::Length || state_ == State::Kind) return reject(FrameError::TruncatedHeader, 0);
    if (state_ == State::Body) return reject(FrameError::TruncatedBody, 0);
    return consume({});
}

} // namespace neura::server::worker
