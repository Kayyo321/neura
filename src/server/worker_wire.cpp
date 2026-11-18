#include "server/worker_wire.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

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
    if (frame.kind == FrameKind::Hello && frame.body_bytes > max_hello_body_bytes) return FrameError::TooLarge;
    if ((frame.kind == FrameKind::BodyEnd || frame.kind == FrameKind::Done) && frame.body_bytes != 0) {
        return FrameError::UnexpectedBody;
    }
    return FrameError::None;
}

constexpr std::size_t version_bytes = 4;
constexpr std::size_t provider_count_bytes = 2;
constexpr std::size_t role_offset = version_bytes;
constexpr std::size_t digest_offset = role_offset + 1;

std::uint32_t read_le(std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        value |= static_cast<std::uint32_t>(bytes[index]) << (8 * index);
    }
    return value;
}

void append_le(std::vector<std::uint8_t>& bytes, std::uint32_t value, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
}

bool valid_peer_role(PeerRole role) {
    return role == PeerRole::Host || role == PeerRole::Worker;
}

bool valid_provider_name(std::string_view name) {
    return !name.empty() && name.size() <= max_provider_name_bytes && std::ranges::all_of(name, [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '.' || ch == '-';
    });
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

EncodedHello encode_hello(const Hello& hello) {
    if (hello.version != protocol_version) return {ProtocolError::UnsupportedVersion, {}};
    if (!valid_peer_role(hello.role)) return {ProtocolError::WrongPeerRole, {}};
    if (hello.identity.providers.size() > max_provider_versions) return {ProtocolError::MalformedHello, {}};

    std::size_t body_bytes = hello_prefix_bytes;
    for (const auto& [name, version] : hello.identity.providers) {
        if (!valid_provider_name(name)) return {ProtocolError::MalformedHello, {}};
        body_bytes += 1 + name.size() + version_bytes;
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(body_bytes);
    append_le(bytes, hello.version, version_bytes);
    bytes.push_back(static_cast<std::uint8_t>(hello.role));
    bytes.insert(bytes.end(), hello.identity.image.begin(), hello.identity.image.end());
    append_le(bytes, static_cast<std::uint32_t>(hello.identity.providers.size()), provider_count_bytes);
    for (const auto& [name, version] : hello.identity.providers) {
        bytes.push_back(static_cast<std::uint8_t>(name.size()));
        bytes.insert(bytes.end(), name.begin(), name.end());
        append_le(bytes, version, version_bytes);
    }
    return {ProtocolError::None, std::move(bytes)};
}

DecodedHello decode_hello(std::span<const std::uint8_t> body) {
    if (body.size() < version_bytes || body.size() > max_hello_body_bytes) return {ProtocolError::MalformedHello};
    const auto version = read_le(body.first(version_bytes));
    if (version != protocol_version) return {ProtocolError::UnsupportedVersion};
    if (body.size() < hello_prefix_bytes) return {ProtocolError::MalformedHello};
    const auto role = static_cast<PeerRole>(body[role_offset]);
    if (!valid_peer_role(role)) return {ProtocolError::WrongPeerRole};
    const auto count = read_le(body.subspan(hello_prefix_bytes - provider_count_bytes, provider_count_bytes));
    if (count > max_provider_versions) return {ProtocolError::MalformedHello};

    Hello hello{version, role, {}};
    std::ranges::copy(body.subspan(digest_offset, hello.identity.image.size()), hello.identity.image.begin());
    body = body.subspan(hello_prefix_bytes);
    std::string_view previous;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (body.empty()) return {ProtocolError::MalformedHello};
        const auto name_bytes = static_cast<std::size_t>(body.front());
        body = body.subspan(1);
        if (body.size() < name_bytes + version_bytes) return {ProtocolError::MalformedHello};
        const std::string_view name(reinterpret_cast<const char*>(body.data()), name_bytes);
        if (!valid_provider_name(name) || name <= previous) return {ProtocolError::MalformedHello};
        hello.identity.providers.emplace(name, read_le(body.subspan(name_bytes, version_bytes)));
        previous = name;
        body = body.subspan(name_bytes + version_bytes);
    }
    if (!body.empty()) return {ProtocolError::MalformedHello};
    return {ProtocolError::None, std::move(hello)};
}

ChannelSequence::ChannelSequence(WorkerIdentity expected, FrameLimits limits)
    : expected_(std::move(expected)), limits_(limits) {}

bool ChannelSequence::ready() const {
    return failure_.error == ProtocolError::None && host_hello_ && worker_hello_ && !assigned_;
}

ProtocolFailure ChannelSequence::reject_frame(FrameError error) {
    if (failure_.error == ProtocolError::None && error != FrameError::None) {
        failure_ = {ProtocolError::Framing, error};
    }
    return failure_;
}

ProtocolFailure ChannelSequence::accept(Direction direction, FrameHeader frame, std::span<const std::uint8_t> hello_body) {
    if (failure_.error != ProtocolError::None) return failure_;
    if (const auto error = check_header(direction, frame, limits_); error != FrameError::None) return reject_frame(error);
    failure_.error = frame.kind == FrameKind::Hello ? accept_hello(direction, frame.body_bytes, hello_body)
                                                   : advance(direction, frame.kind);
    return failure_;
}

ProtocolError ChannelSequence::accept_hello(Direction direction, std::size_t body_bytes, std::span<const std::uint8_t> body) {
    const bool from_host = direction == Direction::HostToWorker;
    bool& greeted = from_host ? host_hello_ : worker_hello_;
    if (greeted) return ProtocolError::DuplicateHello;
    if (body_bytes != body.size()) return ProtocolError::MalformedHello;
    const auto decoded = decode_hello(body);
    if (decoded.error != ProtocolError::None) return decoded.error;
    if (decoded.value.role != (from_host ? PeerRole::Host : PeerRole::Worker)) return ProtocolError::WrongPeerRole;
    if (decoded.value.identity.image != expected_.image) return ProtocolError::ImageMismatch;
    if (decoded.value.identity.providers != expected_.providers) return ProtocolError::ProviderMismatch;
    greeted = true;
    return ProtocolError::None;
}

ProtocolError ChannelSequence::advance_body(Direction direction, bool end) {
    if (direction == Direction::HostToWorker) {
        if (request_body_ended_) return ProtocolError::RequestBodyEnded;
        request_body_ended_ = end;
    } else {
        if (response_ == Response::AwaitingHead) return ProtocolError::ExpectedHead;
        if (response_ != Response::Body) return ProtocolError::ResponseBodyEnded;
        if (end) response_ = Response::BodyEnded;
    }
    return ProtocolError::None;
}

ProtocolError ChannelSequence::advance(Direction direction, FrameKind kind) {
    if (!host_hello_ || !worker_hello_) return ProtocolError::ExpectedHello;
    if (!assigned_ && kind != FrameKind::Assign) return ProtocolError::ExpectedAssign;
    switch (kind) {
    case FrameKind::Assign:
        if (assigned_) return ProtocolError::DuplicateAssign;
        assigned_ = true;
        return ProtocolError::None;
    case FrameKind::BodyChunk:
    case FrameKind::BodyEnd:
        return advance_body(direction, kind == FrameKind::BodyEnd);
    case FrameKind::Call:
        if (outstanding_calls_ == std::numeric_limits<std::uint64_t>::max()) return ProtocolError::TooManyCalls;
        ++outstanding_calls_;
        return ProtocolError::None;
    case FrameKind::Return:
        if (outstanding_calls_ == 0) return ProtocolError::UnexpectedReturn;
        --outstanding_calls_;
        return ProtocolError::None;
    case FrameKind::Cancel:
        if (cancelled_) return ProtocolError::DuplicateCancel;
        cancelled_ = true;
        return ProtocolError::None;
    case FrameKind::Head:
        if (response_ != Response::AwaitingHead) return ProtocolError::DuplicateHead;
        response_ = Response::Body;
        return ProtocolError::None;
    case FrameKind::Done:
        if (response_ == Response::AwaitingHead) return ProtocolError::ExpectedHead;
        if (response_ == Response::Body) return ProtocolError::ExpectedResponseBodyEnd;
        if (response_ == Response::Done) return ProtocolError::DuplicateDone;
        response_ = Response::Done;
        return ProtocolError::None;
    case FrameKind::Hello:
        break;
    }
    throw std::logic_error("invalid worker frame passed to request sequencing");
}

} // namespace neura::server::worker
