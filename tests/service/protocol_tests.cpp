// Unit tests for the codec of the runtime service's protocol
// (docs/design/Service030.md, "Protocol"): round trips of every message
// kind and of the launch envelope, the malformed messages of edge case 57,
// the message limit, the reasons and their fallback, and application IDs.
//
// Usage: neura_service_protocol_tests

#include "service/protocol.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace neura::service;

int failures = 0;

void check(bool condition, const std::string& what, int line) {
    if (condition) return;
    ++failures;
    std::cout << "FAIL line " << line << ": " << what << '\n';
}

#define CHECK(condition) check((condition), #condition, __LINE__)

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

// Whether decoding `wire` with `descriptors` fails as malformed.
bool malformed(const std::string& wire, std::size_t descriptors = 0) {
    try {
        decode(bytes_of(wire), descriptors);
    } catch (const ProtocolError&) {
        return true;
    }
    return false;
}

Message hello() {
    Message message;
    message.kind = Kind::Hello;
    message.sequence = 7;
    message.add_u16(Tag::ProtocolLow, 1).add_u16(Tag::ProtocolHigh, 1).add(Tag::Build, "neura 0.3.0 (b)")
        .add(Tag::Installation, "0123456789abcdef").add(Tag::Generation, "dev").add_u8(Tag::Purpose, 1);
    return message;
}

Envelope sample_envelope() {
    Envelope envelope;
    for (std::size_t i = 0; i < 32; ++i) envelope.digest[i] = static_cast<std::uint8_t>(i * 7);
    envelope.image_size = 123456;
    envelope.image_path = "/tmp/a b/\xc3\xa9.nvr";
    envelope.arguments = {"x", "", std::string("with\0nul", 8), "--"};
    envelope.environment = {"PATH=/usr/bin", "EMPTY=", "NEURA_JIT=off"};
    envelope.mode = {"NEURA_JIT=off", "--jit=auto"};
    envelope.fs = true;
    envelope.clock = true;
    envelope.native = true;
    envelope.raw = true;
    envelope.fs_prefixes = {"/home/u/data", "/tmp"};
    envelope.limits = {{"max-steps", 1000}, {"max-stack-cells", 1000}, {"tier", 1}};
    envelope.flags = {"stats", "tier-stats"};
    envelope.task_dump = 2;
    envelope.trace_events = "events.jsonl";
    envelope.umask = 027;
    envelope.resource_limits = {{1, 1024, 4096}, {2, 8 << 20, unlimited}};
    envelope.nice = -3;
    envelope.ignored_signals = 1u << 13;
    envelope.blocked_signals = 0;
    envelope.lifetime = Lifetime::Detached;
    envelope.client_clock = 987654321;
    return envelope;
}

int round_trips() {
    const auto wire = encode(hello());
    const auto decoded = decode(bytes_of(wire), 0);
    CHECK(decoded.kind == Kind::Hello);
    CHECK(decoded.sequence == 7);
    CHECK(decoded.u16(Tag::ProtocolHigh) == 1);
    CHECK(decoded.get(Tag::Build) == "neura 0.3.0 (b)");
    CHECK(encode(decoded) == wire);

    // The envelope, with byte-string arguments and a NUL inside one.
    Message prepare;
    prepare.kind = Kind::Prepare;
    prepare.sequence = 2;
    std::string request(16, '\x5a');
    prepare.add(Tag::RequestId, request);
    const auto envelope = sample_envelope();
    put_envelope(prepare, envelope);
    const auto read = read_envelope(decode(bytes_of(encode(prepare)), 0));
    CHECK(read.digest == envelope.digest);
    CHECK(read.image_size == envelope.image_size);
    CHECK(read.image_path == envelope.image_path);
    CHECK(read.arguments == envelope.arguments);
    CHECK(read.environment == envelope.environment);
    CHECK(read.mode == envelope.mode);
    CHECK(read.fs && !read.net && !read.env && read.clock && !read.entropy && read.native && read.raw);
    CHECK(read.fs_prefixes == envelope.fs_prefixes);
    CHECK(read.limits == envelope.limits);
    CHECK(read.flag("stats") && read.flag("tier-stats") && !read.flag("profile"));
    CHECK(read.task_dump == 2);
    CHECK(read.trace_events == envelope.trace_events);
    CHECK(read.umask == 027);
    CHECK(read.resource_limits.size() == 2 && read.resource_limits[1].hard == unlimited &&
          read.resource_limits[0].soft == 1024);
    CHECK(read.nice == -3);
    CHECK(read.ignored_signals == envelope.ignored_signals);
    CHECK(read.lifetime == Lifetime::Detached);
    CHECK(read.client_clock == envelope.client_clock);

    // Every kind with its required fields round-trips; descriptors counted.
    Message commit;
    commit.kind = Kind::Commit;
    commit.add(Tag::Token, std::string(32, 't')).add_u8(Tag::Descriptors, 4);
    CHECK(decode(bytes_of(encode(commit)), 4).get(Tag::Token) == std::string(32, 't'));
    Message image;
    image.kind = Kind::Image;
    image.add(Tag::Token, std::string(32, 't')).add_u8(Tag::Descriptors, 1);
    CHECK(!malformed(encode(image), 1));
    Message rejected;
    rejected.kind = Kind::Rejected;
    rejected.add(Tag::Reason, "admission-pending").add(Tag::Sentence, "16 launches are prepared").add_u8(Tag::NotStarted, 1);
    CHECK(decode(bytes_of(encode(rejected)), 0).get(Tag::Reason) == "admission-pending");
    Message list;
    list.kind = Kind::List;
    CHECK(!malformed(encode(list)));

    // An extension field is ignored, and two of them are fine.
    auto extended = hello();
    extended.fields.push_back({0x8001, "future"});
    extended.fields.push_back({0x8001, "again"});
    const auto ignored = decode(bytes_of(encode(extended)), 0);
    CHECK(ignored.fields.size() == hello().fields.size());

    // A list field may repeat.
    Message listing;
    listing.kind = Kind::Listing;
    listing.add_u8(Tag::State, 1).add_u32(Tag::ApplicationCount, 2).add_u64(Tag::CacheBytes, 0)
        .add_u32(Tag::ReadyWorkers, 1).add(Tag::Record, "{}").add(Tag::Record, "{}");
    CHECK(decode(bytes_of(encode(listing)), 0).all(Tag::Record).size() == 2);
    return failures;
}

int malformed_messages() {
    const auto wire = encode(hello());
    // Edge case 57: a message of 2 MiB.
    {
        std::string big = wire;
        big[0] = 0;
        big[1] = 0;
        big[2] = 0x20;  // 2 MiB
        big[3] = 0;
        CHECK(malformed(big));
        bool thrown = false;
        try {
            announced_length(std::span<const std::uint8_t, 4>(bytes_of(big).first<4>()));
        } catch (const ProtocolError&) {
            thrown = true;
        }
        CHECK(thrown);
        Message huge = hello();
        huge.add(Tag::Extension, std::string(max_message_bytes, 'x'));
        thrown = false;
        try {
            encode(huge);
        } catch (const ProtocolError&) {
            thrown = true;
        }
        CHECK(thrown);
    }
    // A field length past the end.
    {
        Message message = hello();
        auto text = encode(message);
        // The first field's length word follows the 16-byte header and its tag.
        text[header_bytes + 2] = '\x7f';
        CHECK(malformed(text));
    }
    // A truncated message and one with trailing bytes the length does not cover.
    CHECK(malformed(wire.substr(0, wire.size() - 1)));
    CHECK(malformed(wire + "x"));
    // An unknown kind.
    {
        auto text = wire;
        text[4] = '\x63';
        text[5] = '\x00';
        CHECK(malformed(text));
    }
    // Nonzero flags.
    {
        auto text = wire;
        text[6] = 1;
        CHECK(malformed(text));
    }
    // An unknown field below 0x8000.
    {
        auto message = hello();
        message.fields.push_back({0x7fff, "x"});
        CHECK(malformed(encode(message)));
    }
    // A repeated field that is not a list.
    {
        auto message = hello();
        message.add(Tag::Build, "again");
        CHECK(malformed(encode(message)));
    }
    // A missing required field.
    {
        auto message = hello();
        message.fields.pop_back();
        CHECK(malformed(encode(message)));
    }
    // A Commit with three descriptors, declared or received.
    {
        Message commit;
        commit.kind = Kind::Commit;
        commit.add(Tag::Token, std::string(32, 't')).add_u8(Tag::Descriptors, 4);
        CHECK(malformed(encode(commit), 3));
        CHECK(malformed(encode(commit), 5));
        Message three;
        three.kind = Kind::Commit;
        three.add(Tag::Token, std::string(32, 't')).add_u8(Tag::Descriptors, 3);
        CHECK(malformed(encode(three), 3));
        // Descriptors with a kind that carries none.
        CHECK(malformed(encode(hello()), 1));
    }
    // A field of the wrong width.
    {
        Message message;
        message.kind = Kind::Hello;
        message.add(Tag::ProtocolLow, "x").add_u16(Tag::ProtocolHigh, 1).add(Tag::Build, "b")
            .add(Tag::Installation, "i").add(Tag::Generation, "g").add_u8(Tag::Purpose, 1);
        const auto decoded = decode(bytes_of(encode(message)), 0);
        bool thrown = false;
        try {
            decoded.u16(Tag::ProtocolLow);
        } catch (const ProtocolError&) {
            thrown = true;
        }
        CHECK(thrown);
    }
    // Envelope values out of range.
    {
        Message prepare;
        prepare.kind = Kind::Prepare;
        prepare.add(Tag::RequestId, std::string(16, 'r'));
        auto envelope = sample_envelope();
        envelope.environment = {"NOEQUALS"};
        put_envelope(prepare, envelope);
        bool thrown = false;
        try {
            read_envelope(decode(bytes_of(encode(prepare)), 0));
        } catch (const ProtocolError&) {
            thrown = true;
        }
        CHECK(thrown);
    }
    return failures;
}

int values() {
    CHECK(name(Reason::AdmissionCache) == "admission-cache");
    CHECK(reason_named("token-expired") == Reason::TokenExpired);
    CHECK(!reason_named("bogus"));
    // The fallback column of the reasons table.
    CHECK(!falls_back(Reason::Protocol) && !falls_back(Reason::Identity) && !falls_back(Reason::ImageMismatch) &&
          !falls_back(Reason::TokenUnknown) && !falls_back(Reason::TokenExpired));
    CHECK(falls_back(Reason::UnsupportedOption) && falls_back(Reason::NotReproducible) &&
          falls_back(Reason::AdmissionApplications) && falls_back(Reason::AdmissionPending) &&
          falls_back(Reason::ImageTooLarge) && falls_back(Reason::AdmissionCache) && falls_back(Reason::Draining) &&
          falls_back(Reason::Retired) && falls_back(Reason::UnsupportedProtocol));
    for (std::uint8_t r = 1; r <= static_cast<std::uint8_t>(Reason::WorkerUnavailable); ++r) {
        CHECK(reason_named(name(static_cast<Reason>(r))) == static_cast<Reason>(r));
    }
    for (std::uint8_t t = 1; t <= static_cast<std::uint8_t>(Terminal::Expired); ++t) {
        CHECK(terminal_named(name(static_cast<Terminal>(t))) == static_cast<Terminal>(t));
    }
    CHECK(is_live(Terminal::Running) && !is_live(Terminal::Exited));

    Epoch epoch{};
    epoch[0] = 0xde;
    epoch[1] = 0xad;
    epoch[2] = 0xbe;
    epoch[3] = 0xef;
    const auto id = application_id("0.3.0-rc.1+1a2b3c4d5e6f", epoch, 42);
    CHECK(id == "0.3.0-rc.1+1a2b3c4d5e6f.deadbeef.42");
    const auto parsed = parse_application_id(id);
    CHECK(parsed && parsed->generation == "0.3.0-rc.1+1a2b3c4d5e6f" && parsed->epoch == "deadbeef" &&
          parsed->number == 42);
    CHECK(parse_application_id("dev.deadbeef.1").has_value());
    CHECK(!parse_application_id("dev.deadbeef.0"));
    CHECK(!parse_application_id("dev.deadbeef.01"));
    CHECK(!parse_application_id("dev.DEADBEEF.1"));
    CHECK(!parse_application_id("dev.deadbee.1"));
    CHECK(!parse_application_id("deadbeef.1"));
    CHECK(!parse_application_id(".deadbeef.1"));
    CHECK(!parse_application_id("a/b.deadbeef.1"));
    CHECK(!parse_application_id("12345"));
    CHECK(!parse_application_id(""));
    CHECK(hex(std::vector<std::uint8_t>{0, 255, 16}) == "00ff10");
    return failures;
}

} // namespace

int main() {
    round_trips();
    malformed_messages();
    values();
    if (failures != 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "service protocol passed\n";
    return 0;
}
