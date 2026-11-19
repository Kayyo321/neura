#include "server/worker_wire.hpp"

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace neura::server::worker;
using Bytes = std::vector<std::uint8_t>;
using Input = std::span<const std::uint8_t>;
constexpr auto host = Direction::HostToWorker;
constexpr auto worker = Direction::WorkerToHost;

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

Bytes literal_header(std::uint32_t length, std::uint8_t kind) {
    return {static_cast<std::uint8_t>(length), static_cast<std::uint8_t>(length >> 8),
            static_cast<std::uint8_t>(length >> 16), static_cast<std::uint8_t>(length >> 24), kind};
}

Bytes wire(std::uint8_t kind, Input body) {
    auto bytes = literal_header(static_cast<std::uint32_t>(body.size() + 1), kind);
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}

struct Observed {
    std::optional<FrameHeader> header;
    Bytes body;
};

ReadResult feed(FrameDecoder& decoder, Input& input, Observed& observed) {
    for (;;) {
        const auto result = decoder.consume(input);
        check(result.consumed <= input.size(), "decoder consumed past its input");
        if (result.event == FrameEvent::BodyData) {
            check(observed.header.has_value(), "received body data before a validated header");
            check(result.consumed != 0 && result.consumed == result.body_data.size() &&
                  result.body_data.data() == input.data(), "body data must alias exactly the consumed input");
            observed.body.insert(observed.body.end(), result.body_data.begin(), result.body_data.end());
        } else {
            check(result.body_data.empty(), "non-body event has body bytes");
        }
        input = input.subspan(result.consumed);

        if (result.event == FrameEvent::HeaderReady) {
            check(!observed.header, "header was announced more than once");
            observed.header = decoder.header();
        } else if (result.event != FrameEvent::BodyData) {
            if (result.event == FrameEvent::NeedInput) check(input.empty(), "NeedInput left readable bytes");
            return result;
        }
    }
}

void check_frame(const Observed& observed, FrameKind kind, Input payload) {
    check(observed.header && observed.header->kind == kind && observed.header->body_bytes == payload.size(),
          "decoded frame header differs");
    check(std::ranges::equal(observed.body, payload), "decoded body bytes differ");
}

void accepted(FrameKind kind, std::uint8_t kind_byte, Direction direction, Input body) {
    const auto bytes = wire(kind_byte, body);
    const auto encoded = encode_header(direction, {kind, body.size()});
    check(encoded.error == FrameError::None &&
          std::ranges::equal(encoded.bytes, Input(bytes).first(frame_header_bytes)), "encoded length or kind byte differs");

    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        FrameDecoder decoder(direction);
        Observed observed;
        auto first = Input(bytes).first(split);
        const auto partial = feed(decoder, first, observed);
        check(first.empty() && partial.event == (split == bytes.size() ? FrameEvent::Complete : FrameEvent::NeedInput),
              "wrong event on split frame");
        auto rest = Input(bytes).subspan(split);
        check(feed(decoder, rest, observed).event == FrameEvent::Complete && rest.empty(), "frame did not complete after split");
        check_frame(observed, kind, body);
        check(decoder.finish_input().event == FrameEvent::Complete, "EOF erased a completed frame");
    }

    FrameDecoder decoder(direction);
    Observed observed;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        auto byte = Input(bytes).subspan(index, 1);
        const auto expected = index + 1 == bytes.size() ? FrameEvent::Complete : FrameEvent::NeedInput;
        check(feed(decoder, byte, observed).event == expected && byte.empty(), "byte-at-a-time read changed framing");
    }
    check_frame(observed, kind, body);
}

void rejected(Input wire, Direction direction, FrameError expected, std::size_t consumed, FrameLimits limits = {}) {
    for (std::size_t split = 0; split <= wire.size(); ++split) {
        FrameDecoder decoder(direction, limits);
        Observed observed;
        auto prefix = wire.first(split);
        auto suffix = wire.subspan(split);
        auto result = feed(decoder, prefix, observed);
        if (result.event == FrameEvent::NeedInput) result = feed(decoder, suffix, observed);
        check(result.event == FrameEvent::Rejected && result.error == expected && !observed.header && observed.body.empty(),
              "invalid frame exposed a header or did not report its expected rejection");
        check(wire.size() - prefix.size() - suffix.size() == consumed, "rejection consumed beyond the invalid field");
        const auto again = decoder.consume(wire);
        check(again.event == FrameEvent::Rejected && again.error == expected && again.consumed == 0 &&
              decoder.finish_input().error == expected, "a framing rejection must remain terminal");
    }
}

void kinds_and_directions() {
    check(protocol_version == 1, "wrong worker protocol version");
    struct Case { FrameKind kind; std::uint8_t byte; bool to_worker; bool to_host; bool empty = false; };
    constexpr Case cases[] = {
        {FrameKind::Hello, 0x01, true, true},       {FrameKind::Assign, 0x02, true, false},
        {FrameKind::BodyChunk, 0x03, true, true},   {FrameKind::BodyEnd, 0x04, true, true, true},
        {FrameKind::Call, 0x05, false, true},       {FrameKind::Return, 0x06, true, false},
        {FrameKind::Cancel, 0x07, true, false},     {FrameKind::Head, 0x08, false, true},
        {FrameKind::Done, 0x09, false, true, true},
    };
    const Bytes opaque{0x5a, 0x00, 0xff, 0x03};
    for (const auto& fixture : cases) {
        const Input body = fixture.empty ? Input{} : Input(opaque);
        for (const auto& [direction, allowed] : {std::pair{host, fixture.to_worker}, std::pair{worker, fixture.to_host}}) {
            if (allowed) {
                accepted(fixture.kind, fixture.byte, direction, body);
            } else {
                rejected(wire(fixture.byte, body), direction, FrameError::WrongDirection, frame_header_bytes);
                check(encode_header(direction, {fixture.kind, body.size()}).error == FrameError::WrongDirection,
                      "encoder accepted a wrong-direction frame");
            }
        }
    }

    for (const auto byte : {0x00, 0x0a, 0xff}) {
        const auto tag = static_cast<std::uint8_t>(byte);
        for (const auto direction : {host, worker}) {
            rejected(wire(tag, opaque), direction, FrameError::UnknownKind, frame_header_bytes);
            check(encode_header(direction, {static_cast<FrameKind>(tag), opaque.size()}).error == FrameError::UnknownKind,
                  "encoder accepted a reserved kind byte");
        }
    }
    for (const auto kind : {FrameKind::BodyEnd, FrameKind::Done}) {
        rejected(wire(static_cast<std::uint8_t>(kind), opaque), worker, FrameError::UnexpectedBody, frame_header_bytes);
        check(encode_header(worker, {kind, 1}).error == FrameError::UnexpectedBody,
              "encoder put a body on an empty control frame");
    }
    rejected(literal_header(0, 0x01), host, FrameError::InvalidLength, 4);
}

void pipelined() {
    const Bytes greeting{0x01, 0x00, 0x00, 0x00};
    const Bytes payload{0xff, 0x00, 0x01, 0x02, 0xff};
    auto stream = wire(0x01, greeting);
    const auto chunk = wire(0x03, payload);
    const auto end = wire(0x04, {});
    stream.insert(stream.end(), chunk.begin(), chunk.end());
    stream.insert(stream.end(), end.begin(), end.end());

    FrameDecoder hello(host);
    const auto header = hello.consume(stream);
    check(header.event == FrameEvent::HeaderReady && header.consumed == frame_header_bytes,
          "the header must leave its entire body unread");
    const auto body = hello.consume(Input(stream).subspan(header.consumed));
    check(body.event == FrameEvent::BodyData && body.consumed == greeting.size(), "body consumed the next frame's header");
    auto unread = Input(stream).subspan(header.consumed + body.consumed);
    const auto complete = hello.consume(unread);
    check(complete.event == FrameEvent::Complete && complete.consumed == 0, "completion took pipelined bytes");

    Observed observed_chunk;
    FrameDecoder next(host);
    check(feed(next, unread, observed_chunk).event == FrameEvent::Complete && std::ranges::equal(unread, end),
          "second decoder did not leave the control frame unread");
    check_frame(observed_chunk, FrameKind::BodyChunk, payload);
    FrameDecoder last(host);
    const auto end_header = last.consume(unread);
    check(end_header.event == FrameEvent::HeaderReady && end_header.consumed == end.size() &&
          last.finish_input().event == FrameEvent::Complete, "empty control frame must complete at its header boundary");

    constexpr std::array kinds{FrameKind::Hello, FrameKind::BodyChunk, FrameKind::BodyEnd};
    const Input bodies[] = {greeting, payload, {}};
    for (std::size_t split = 0; split <= stream.size(); ++split) {
        FrameDecoder decoder(host);
        Observed observed;
        std::size_t frames = 0;
        for (auto input : {Input(stream).first(split), Input(stream).subspan(split)}) {
            for (;;) {
                const auto result = feed(decoder, input, observed);
                if (result.event == FrameEvent::NeedInput) break;
                check(result.event == FrameEvent::Complete && frames < kinds.size(), "wrong fragmented pipeline sequence");
                check_frame(observed, kinds[frames], bodies[frames]);
                ++frames;
                decoder = FrameDecoder(host);
                observed = {};
            }
        }
        check(frames == kinds.size() && decoder.finish_input().event == FrameEvent::Closed,
              "fragmented pipeline left an incomplete frame");
    }
}

void accepted_size(FrameKind kind, FrameLimits limits, std::size_t body_bytes) {
    const auto raw = literal_header(static_cast<std::uint32_t>(body_bytes + 1), static_cast<std::uint8_t>(kind));
    const auto encoded = encode_header(host, {kind, body_bytes}, limits);
    check(encoded.error == FrameError::None && std::ranges::equal(encoded.bytes, raw), "exact size bound failed to encode");
    FrameDecoder decoder(host, limits);
    const auto head = decoder.consume(raw);
    check(head.event == FrameEvent::HeaderReady && head.consumed == raw.size() &&
          decoder.header().body_bytes == body_bytes, "exact size bound failed to decode");

    // Stream the hard maximum through a small buffer. The decoder must never
    // need a buffer proportional to the announced length.
    const std::array<std::uint8_t, 8192> buffer{};
    for (std::size_t left = body_bytes; left != 0;) {
        const auto fragment = Input(buffer).first(std::min(left, buffer.size()));
        const auto data = decoder.consume(fragment);
        check(data.event == FrameEvent::BodyData && data.consumed == fragment.size() &&
              data.body_data.data() == buffer.data(), "body at an exact limit was not streamed");
        left -= data.consumed;
    }
    const auto complete = decoder.consume(buffer);
    check(complete.event == FrameEvent::Complete && complete.consumed == 0, "at-limit frame did not finish exactly");
}

void bounds() {
    check(FrameLimits{}.wire_bytes() == 1024 * 1024 && max_frame_bytes == 64 * 1024 * 1024 &&
          max_body_chunk_bytes == 64 * 1024, "frame limits changed from the contract");
    for (const auto cap : {std::size_t{53}, default_frame_bytes, max_frame_bytes}) {
        const FrameLimits limits(cap);
        const auto at_limit = cap - frame_header_bytes;
        accepted_size(FrameKind::Assign, limits, at_limit);
        const auto excess = literal_header(static_cast<std::uint32_t>(at_limit + 2), 0x02);
        rejected(excess, host, FrameError::TooLarge, cap == 53 ? frame_header_bytes : 4, limits);
        check(encode_header(host, {FrameKind::Assign, at_limit + 1}, limits).error == FrameError::TooLarge,
              "ordinary at-limit plus one frame encoded");
    }
    for (const auto cap : {std::size_t{5}, default_frame_bytes}) {
        const FrameLimits limits(cap);
        accepted_size(FrameKind::BodyChunk, limits, max_body_chunk_bytes);
        const auto excess = literal_header(static_cast<std::uint32_t>(max_body_chunk_bytes + 2), 0x03);
        rejected(excess, host, FrameError::TooLarge, cap == 5 ? 4 : frame_header_bytes, limits);
        check(encode_header(host, {FrameKind::BodyChunk, max_body_chunk_bytes + 1}, limits).error == FrameError::TooLarge,
              "at-limit plus one chunk encoded");
    }
    rejected(literal_header(0xffff'ffff, 0x02), host, FrameError::TooLarge, 4, FrameLimits(max_frame_bytes));
    check(encode_header(host, {FrameKind::Assign, std::numeric_limits<std::size_t>::max()}).error == FrameError::TooLarge,
          "encoder overflowed while checking a huge body");
    rejected(literal_header(1, 0x04), host, FrameError::TooLarge, 5, FrameLimits(4));

    bool invalid_limit = false;
    try {
        const FrameLimits limits(max_frame_bytes + 1);
        (void) limits;
    } catch (const std::invalid_argument&) {
        invalid_limit = true;
    }
    check(invalid_limit, "configured C-2 above the hard maximum was accepted");
}

void eof() {
    const Bytes payload{0x00, 0xff, 0x01};
    const auto frame = wire(0x01, payload);
    FrameDecoder idle(worker);
    check(idle.consume({}).event == FrameEvent::NeedInput && idle.finish_input().event == FrameEvent::Closed,
          "idle EOF between frames must close cleanly");
    check(idle.consume(frame).event == FrameEvent::Closed && idle.consume(frame).consumed == 0,
          "closed decoder resumed on new input");

    for (std::size_t count = 1; count < frame.size(); ++count) {
        FrameDecoder decoder(worker);
        Observed observed;
        auto partial = Input(frame).first(count);
        check(feed(decoder, partial, observed).event == FrameEvent::NeedInput && partial.empty(),
              "fragment reported prematurely closed");
        const auto expected = count < frame_header_bytes ? FrameError::TruncatedHeader : FrameError::TruncatedBody;
        const auto end = decoder.finish_input();
        check(end.event == FrameEvent::Rejected && end.error == expected && end.consumed == 0,
              "partial frame at EOF did not report truncation");
        check(decoder.consume(frame).error == expected && decoder.consume(frame).consumed == 0,
              "truncated decoder resumed after EOF");
    }
}

// Independent vector: struct.pack('<IB32sH', 1, 1, sha256(b'abc').digest(), 2)
// followed by two u8-prefixed names and little-endian u32 versions.
constexpr std::array<std::uint8_t, 60> host_hello_wire{
    0x38, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01,
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    0x02, 0x00, 0x03, 0x61, 0x70, 0x70, 0x01, 0x00, 0x00, 0x00, 0x03, 0x73, 0x71, 0x6c, 0x04, 0x03, 0x02, 0x01,
};
constexpr auto worker_hello_wire = [] {
    auto bytes = host_hello_wire;
    bytes[9] = 0x02;
    return bytes;
}();
constexpr auto host_hello_body = Input(host_hello_wire).subspan(5);
constexpr auto worker_hello_body = Input(worker_hello_wire).subspan(5);

WorkerIdentity identity() {
    WorkerIdentity value;
    value.providers.emplace("sql", 0x01020304);
    value.providers.emplace("app", 1);
    std::ranges::copy(Input(host_hello_wire).subspan(10, 32), value.image.begin());
    return value;
}

void hello_codec() {
    for (const auto& [direction, role, body] : {
            std::tuple{host, PeerRole::Host, host_hello_body}, std::tuple{worker, PeerRole::Worker, worker_hello_body}}) {
        const Hello expected{1, role, identity()};
        const auto encoded = encode_hello(expected);
        check(encoded.error == ProtocolError::None && std::ranges::equal(encoded.bytes, body), "Hello wire vector differs");
        const auto decoded = decode_hello(body);
        check(decoded.error == ProtocolError::None && decoded.value == expected, "Hello changed the identity");
        accepted(FrameKind::Hello, 0x01, direction, body);
    }

    for (std::size_t size = 0; size < host_hello_body.size(); ++size) {
        check(decode_hello(host_hello_body.first(size)).error == ProtocolError::MalformedHello,
              "truncated Hello accepted or reported incorrectly");
    }
    const auto check_byte = [](std::size_t offset, std::uint8_t byte, ProtocolError expected) {
        Bytes altered(host_hello_body.begin(), host_hello_body.end());
        altered[offset] = byte;
        check(decode_hello(altered).error == expected, "Hello accepted an invalid field");
    };
    check_byte(0, 2, ProtocolError::UnsupportedVersion);
    check_byte(3, 1, ProtocolError::UnsupportedVersion);
    check_byte(4, 0xff, ProtocolError::WrongPeerRole);
    check_byte(37, 0, ProtocolError::MalformedHello); // Unclaimed entry bytes.
    check_byte(37, 3, ProtocolError::MalformedHello); // Missing entry.
    check_byte(38, 1, ProtocolError::MalformedHello); // 258 entries.
    check_byte(39, 0, ProtocolError::MalformedHello); // Empty name.
    check_byte(39, 255, ProtocolError::MalformedHello); // Short name or version.
    check_byte(40, 0, ProtocolError::MalformedHello);
    check_byte(40, 0x80, ProtocolError::MalformedHello);
    check_byte(40, 'z', ProtocolError::MalformedHello); // Noncanonical order.

    Bytes repeated(host_hello_body.begin(), host_hello_body.end());
    std::copy_n(host_hello_body.begin() + 40, 3, repeated.begin() + 48);
    check(decode_hello(repeated).error == ProtocolError::MalformedHello, "duplicate provider name accepted");
    auto trailing = Bytes(host_hello_body.begin(), host_hello_body.end());
    trailing.push_back(0);
    check(decode_hello(trailing).error == ProtocolError::MalformedHello, "Hello accepted trailing bytes");

    Hello extreme;
    auto encoded = encode_hello(extreme);
    auto decoded = decode_hello(encoded.bytes);
    check(encoded.error == ProtocolError::None && encoded.bytes.size() == 39 && decoded.error == ProtocolError::None &&
          decoded.value == extreme, "empty provider manifest did not round trip");
    for (std::size_t index = 0; index < max_provider_versions; ++index) {
        auto name = std::to_string(index);
        name.resize(max_provider_name_bytes, '_');
        extreme.identity.providers.emplace(name, std::numeric_limits<std::uint32_t>::max());
    }
    encoded = encode_hello(extreme);
    decoded = decode_hello(encoded.bytes);
    check(encoded.error == ProtocolError::None && encoded.bytes.size() == 66599 && decoded.error == ProtocolError::None &&
          decoded.value == extreme, "maximal provider manifest did not round trip");
    accepted_size(FrameKind::Hello, FrameLimits(66604), encoded.bytes.size());
    encoded.bytes.push_back(0);
    check(decode_hello(encoded.bytes).error == ProtocolError::MalformedHello, "oversized Hello body decoded");
    check(encode_header(host, {FrameKind::Hello, encoded.bytes.size()}).error == FrameError::TooLarge,
          "oversized Hello header encoded");
    rejected(literal_header(66601, 0x01), worker, FrameError::TooLarge, frame_header_bytes);
    extreme.identity.providers.emplace("excess", 1);
    check(encode_hello(extreme).error == ProtocolError::MalformedHello, "oversized provider count encoded");
    for (const auto& name : {std::string{}, std::string(256, 'a'), std::string("a/b"), std::string("a\0b", 3)}) {
        check(encode_hello({1, PeerRole::Worker, {{}, {{name, 1}}}}).error == ProtocolError::MalformedHello,
              "invalid provider name encoded");
    }
    check(encode_hello({2, PeerRole::Worker, {}}).error == ProtocolError::UnsupportedVersion,
          "unsupported Hello version encoded");
    check(encode_hello({1, static_cast<PeerRole>(0), {}}).error == ProtocolError::WrongPeerRole,
          "invalid role encoded");
}

ProtocolFailure deliver(ChannelSequence& channel, Direction direction, Input bytes) {
    FrameDecoder decoder(direction);
    Observed observed;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        auto input = bytes.subspan(index, 1);
        const auto read = feed(decoder, input, observed);
        if (read.event == FrameEvent::Rejected) return channel.reject_frame(read.error);
        if (read.event == FrameEvent::Complete) {
            check(index + 1 == bytes.size() && input.empty(), "test supplied more than one frame to deliver");
            const Input body = observed.header->kind == FrameKind::Hello ? Input(observed.body) : Input{};
            return channel.accept(direction, *observed.header, body);
        }
        check(read.event == FrameEvent::NeedInput && input.empty(), "fragment did not suspend before Complete");
    }
    const auto end = decoder.finish_input();
    check(end.event == FrameEvent::Rejected, "deliver needs a complete frame or a framing rejection");
    return channel.reject_frame(end.error);
}

void expect_terminal(ChannelSequence& channel, ProtocolFailure actual, ProtocolFailure expected) {
    check(actual == expected && channel.failure() == expected, "wrong terminal protocol error");
    check(!channel.ready() && deliver(channel, worker, worker_hello_wire) == expected &&
          channel.accept(host, {FrameKind::Assign, 0}) == expected &&
          channel.reject_frame(FrameError::TruncatedBody) == expected, "rejected channel resumed or changed its error");
}

void reject_identity() {
    const auto reject_hello = [](Direction direction, Input bytes, ProtocolFailure expected) {
        ChannelSequence channel(identity());
        const auto other = direction == host ? deliver(channel, worker, worker_hello_wire)
                                             : deliver(channel, host, host_hello_wire);
        check(other == ProtocolFailure{} && !channel.ready(), "one Hello enabled Assign");
        expect_terminal(channel, deliver(channel, direction, bytes), expected);
    };
    reject_hello(worker, host_hello_wire, {ProtocolError::WrongPeerRole});
    reject_hello(host, worker_hello_wire, {ProtocolError::WrongPeerRole});
    reject_hello(worker, Input(worker_hello_wire).first(4), {ProtocolError::Framing, FrameError::TruncatedHeader});
    reject_hello(worker, Input(worker_hello_wire).first(59), {ProtocolError::Framing, FrameError::TruncatedBody});
    reject_hello(worker, wire(0x01, worker_hello_body.first(54)), {ProtocolError::MalformedHello});

    auto changed = worker_hello_wire;
    changed[5] = 2;
    reject_hello(worker, changed, {ProtocolError::UnsupportedVersion});
    changed = worker_hello_wire;
    changed[41] ^= 1; // Last image digest byte.
    reject_hello(worker, changed, {ProtocolError::ImageMismatch});
    changed = worker_hello_wire;
    changed[59] ^= 1; // Most significant byte of one ABI version.
    reject_hello(worker, changed, {ProtocolError::ProviderMismatch});
    changed = worker_hello_wire;
    changed[53] = 't'; // A differently named provider with the same version.
    reject_hello(worker, changed, {ProtocolError::ProviderMismatch});
    auto extra_provider = identity();
    extra_provider.providers.emplace("tls", 1);
    reject_hello(worker, wire(0x01, encode_hello({1, PeerRole::Worker, extra_provider}).bytes),
                 {ProtocolError::ProviderMismatch});
    extra_provider.providers.erase("tls");
    extra_provider.providers.erase("sql");
    reject_hello(worker, wire(0x01, encode_hello({1, PeerRole::Worker, extra_provider}).bytes),
                 {ProtocolError::ProviderMismatch});

    ChannelSequence channel(identity());
    expect_terminal(channel, channel.accept(host, {FrameKind::Hello, 55}, host_hello_body.first(54)),
                    {ProtocolError::MalformedHello});
    ChannelSequence small_limit(identity(), FrameLimits(59));
    expect_terminal(small_limit, deliver(small_limit, host, host_hello_wire), {ProtocolError::Framing, FrameError::TooLarge});
}

void handshake(ChannelSequence& channel, bool worker_first = false) {
    check(!channel.ready() && !channel.response_done(), "new channel had an active request");
    const auto hello = [&](Direction direction) {
        return deliver(channel, direction, direction == host ? Input(host_hello_wire) : Input(worker_hello_wire));
    };
    check(hello(worker_first ? worker : host) == ProtocolFailure{} && !channel.ready(), "wrong first Hello state");
    check(hello(worker_first ? host : worker) == ProtocolFailure{} && channel.ready(), "two valid Hellos did not enable Assign");
}

void request_sequence() {
    struct Message { Direction direction; FrameKind kind; };
    constexpr Message assign{host, FrameKind::Assign}, request_chunk{host, FrameKind::BodyChunk},
        request_end{host, FrameKind::BodyEnd}, call{worker, FrameKind::Call}, ret{host, FrameKind::Return},
        cancel{host, FrameKind::Cancel}, head{worker, FrameKind::Head}, response_chunk{worker, FrameKind::BodyChunk},
        response_end{worker, FrameKind::BodyEnd}, done{worker, FrameKind::Done};
    const auto send = [](ChannelSequence& sequence, Message message) {
        const Bytes opaque{0x5a, 0x00, 0xff};
        const Input body = message.kind == FrameKind::BodyEnd || message.kind == FrameKind::Done ? Input{} : Input(opaque);
        return deliver(sequence, message.direction, wire(static_cast<std::uint8_t>(message.kind), body));
    };
    for (const Message message : {assign, request_chunk, request_end, call, ret, cancel, head, response_chunk, response_end, done}) {
        ChannelSequence fresh(identity());
        expect_terminal(fresh, send(fresh, message), {ProtocolError::ExpectedHello});
        ChannelSequence half(identity());
        check(deliver(half, host, host_hello_wire) == ProtocolFailure{}, "first Hello failed");
        expect_terminal(half, send(half, message), {ProtocolError::ExpectedHello});
        if (message.kind != FrameKind::Assign) {
            ChannelSequence unassigned(identity());
            handshake(unassigned);
            expect_terminal(unassigned, send(unassigned, message), {ProtocolError::ExpectedAssign});
        }
    }

    for (const auto& [direction, bytes] : {
            std::pair{host, Input(host_hello_wire)}, std::pair{worker, Input(worker_hello_wire)}}) {
        ChannelSequence duplicate(identity());
        check(deliver(duplicate, direction, bytes) == ProtocolFailure{}, "initial Hello failed");
        expect_terminal(duplicate, deliver(duplicate, direction, bytes), {ProtocolError::DuplicateHello});
    }

    const auto scenario = [&](std::initializer_list<Message> messages, bool worker_first = false) {
        ChannelSequence sequence(identity());
        handshake(sequence, worker_first);
        for (const auto message : messages) check(send(sequence, message) == ProtocolFailure{}, "valid message rejected");
        return sequence;
    };
    check(scenario({assign, request_chunk, request_chunk, request_end, call, call, ret, head, response_chunk,
                    response_chunk, response_end, done, ret}).response_done(), "response did not finish");
    check(scenario({assign, head, response_end, done, request_chunk, request_end, cancel, call, ret}, true).response_done(),
          "early empty response blocked request or cleanup frames");
    auto cancelled = scenario({assign, call, cancel, ret});
    check(!cancelled.ready() && !cancelled.response_done() && send(cancelled, head) == ProtocolFailure{} &&
          send(cancelled, response_end) == ProtocolFailure{} && send(cancelled, done) == ProtocolFailure{},
          "Cancel manufactured a response outcome");

    const auto invalid = [&](std::initializer_list<Message> messages, Message rejected_message, ProtocolError error) {
        auto channel = scenario(messages);
        expect_terminal(channel, send(channel, rejected_message), {error});
    };
    invalid({assign}, assign, ProtocolError::DuplicateAssign);
    invalid({assign, request_end}, request_end, ProtocolError::RequestBodyEnded);
    invalid({assign, request_end}, request_chunk, ProtocolError::RequestBodyEnded);
    invalid({assign}, response_chunk, ProtocolError::ExpectedHead);
    invalid({assign}, response_end, ProtocolError::ExpectedHead);
    invalid({assign}, done, ProtocolError::ExpectedHead);
    invalid({assign, head}, head, ProtocolError::DuplicateHead);
    invalid({assign, head}, done, ProtocolError::ExpectedResponseBodyEnd);
    invalid({assign, head, response_end}, response_chunk, ProtocolError::ResponseBodyEnded);
    invalid({assign, head, response_end}, response_end, ProtocolError::ResponseBodyEnded);
    invalid({assign, head, response_end, done}, done, ProtocolError::DuplicateDone);
    invalid({assign, head, response_end, done}, assign, ProtocolError::DuplicateAssign);
    invalid({assign, head, response_end, done}, response_chunk, ProtocolError::ResponseBodyEnded);
    invalid({assign, cancel}, cancel, ProtocolError::DuplicateCancel);
    invalid({assign}, ret, ProtocolError::UnexpectedReturn);
    invalid({assign, call, call, ret, ret}, ret, ProtocolError::UnexpectedReturn);

    auto wrong_direction = scenario({assign});
    expect_terminal(wrong_direction, deliver(wrong_direction, host, wire(0x08, {})),
                    {ProtocolError::Framing, FrameError::WrongDirection});
}

void paired_endpoints() {
    ChannelSequence host_endpoint(identity()), worker_endpoint(identity());
    check(host_endpoint.accept(host, {FrameKind::Hello, 55}, host_hello_body) == ProtocolFailure{} &&
          worker_endpoint.accept(worker, {FrameKind::Hello, 55}, worker_hello_body) == ProtocolFailure{},
          "endpoints could not send their Hellos independently");
    check(!host_endpoint.ready() && !worker_endpoint.ready(), "local Hello alone enabled Assign");
    check(deliver(host_endpoint, worker, worker_hello_wire) == ProtocolFailure{} &&
          deliver(worker_endpoint, host, host_hello_wire) == ProtocolFailure{} &&
          host_endpoint.ready() && worker_endpoint.ready(), "endpoints disagreed on readiness");

    const auto send = [](ChannelSequence& sender, ChannelSequence& receiver, Direction direction, FrameKind kind) {
        check(sender.accept(direction, {kind, 0}) == ProtocolFailure{} &&
              deliver(receiver, direction, wire(static_cast<std::uint8_t>(kind), {})) == ProtocolFailure{},
              "endpoints disagreed on a request lifecycle frame");
    };
    send(host_endpoint, worker_endpoint, host, FrameKind::Assign);
    send(worker_endpoint, host_endpoint, worker, FrameKind::Head);
    send(worker_endpoint, host_endpoint, worker, FrameKind::BodyEnd);

    // Cancel and Done cross in transit, so each endpoint observes its own send first.
    check(host_endpoint.accept(host, {FrameKind::Cancel, 0}) == ProtocolFailure{} &&
          worker_endpoint.accept(worker, {FrameKind::Done, 0}) == ProtocolFailure{}, "could not send crossing messages");
    check(deliver(host_endpoint, worker, wire(0x09, {})) == ProtocolFailure{} &&
          deliver(worker_endpoint, host, wire(0x07, {})) == ProtocolFailure{} &&
          host_endpoint.response_done() && worker_endpoint.response_done(), "crossing Cancel and Done disagreed");
    send(host_endpoint, worker_endpoint, host, FrameKind::BodyEnd);
}

} // namespace

int main() {
    constexpr std::pair<std::string_view, void (*)()> suites[] = {
        {"kinds and directions", kinds_and_directions},
        {"pipelining", pipelined},
        {"size limits", bounds},
        {"EOF", eof},
        {"Hello codec", hello_codec},
        {"identity handshake", reject_identity},
        {"request sequence", request_sequence},
        {"paired endpoints", paired_endpoints},
    };
    for (const auto& [name, run] : suites) {
        try {
            run();
        } catch (const std::exception& error) {
            std::cerr << name << ": " << error.what() << '\n';
            return 1;
        }
    }
}
