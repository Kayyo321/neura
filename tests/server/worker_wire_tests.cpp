#include "server/worker_wire.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
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

} // namespace

int main() {
    constexpr std::pair<std::string_view, void (*)()> suites[] = {
        {"kinds and directions", kinds_and_directions},
        {"pipelining", pipelined},
        {"size limits", bounds},
        {"EOF", eof},
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
