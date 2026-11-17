#include "protocol.hpp"

#include <algorithm>
#include <charconv>
#include <set>

namespace neura::service {

namespace {

struct KindInfo {
    Kind kind;
    std::string_view name;
    std::size_t descriptors;
    std::initializer_list<Tag> required;
};

const KindInfo kinds[] = {
    {Kind::Hello, "Hello", 0,
     {Tag::ProtocolLow, Tag::ProtocolHigh, Tag::Build, Tag::Installation, Tag::Generation, Tag::Purpose}},
    {Kind::Welcome, "Welcome", 0, {Tag::Protocol, Tag::Epoch, Tag::Build, Tag::Installation, Tag::Generation, Tag::State}},
    {Kind::Prepare, "Prepare", 0,
     {Tag::RequestId, Tag::ImageDigest, Tag::ImageSize, Tag::Policy, Tag::Umask, Tag::Nice, Tag::IgnoredSignals,
      Tag::BlockedSignals, Tag::Lifetime, Tag::ClientClock}},
    {Kind::Prepared, "Prepared", 0, {Tag::Token, Tag::ValidMilliseconds, Tag::ImageNeeded, Tag::Path}},
    {Kind::Image, "Image", 1, {Tag::Token, Tag::Descriptors}},
    {Kind::Commit, "Commit", 4, {Tag::Token, Tag::Descriptors}},
    {Kind::Committed, "Committed", 0, {Tag::ApplicationId}},
    {Kind::Outcome, "Outcome", 0, {Tag::ApplicationId, Tag::Terminal, Tag::ExitStatus}},
    {Kind::Rejected, "Rejected", 0, {Tag::Reason, Tag::Sentence, Tag::NotStarted}},
    {Kind::Cancel, "Cancel", 0, {Tag::Token}},
    {Kind::Signal, "Signal", 0, {Tag::ApplicationId, Tag::SignalNumber}},
    {Kind::Query, "Query", 0, {}},
    {Kind::Status, "Status", 0, {Tag::Terminal}},
    {Kind::List, "List", 0, {}},
    {Kind::Listing, "Listing", 0, {Tag::State, Tag::ApplicationCount, Tag::CacheBytes, Tag::ReadyWorkers}},
    {Kind::Stop, "Stop", 0, {Tag::ApplicationId, Tag::GraceMilliseconds}},
    {Kind::Shutdown, "Shutdown", 0, {Tag::Terminate}},
    {Kind::Retire, "Retire", 0, {}},
    {Kind::CacheClear, "CacheClear", 0, {}},
    {Kind::WorkerReady, "WorkerReady", 0, {Tag::Build, Tag::Pid}},
    {Kind::WorkerAssign, "WorkerAssign", 5,
     {Tag::Descriptors, Tag::ApplicationId, Tag::ImageDigest, Tag::ImageSize, Tag::Policy, Tag::Umask, Tag::Nice,
      Tag::IgnoredSignals, Tag::BlockedSignals, Tag::Lifetime, Tag::ClientClock}},
    {Kind::WorkerStarted, "WorkerStarted", 0, {}},
    {Kind::WorkerFinished, "WorkerFinished", 0, {Tag::Terminal, Tag::ExitStatus}},
};

const KindInfo* info(std::uint16_t kind) {
    for (const auto& entry : kinds) {
        if (static_cast<std::uint16_t>(entry.kind) == kind) return &entry;
    }
    return nullptr;
}

constexpr std::uint16_t list_tags[] = {
    static_cast<std::uint16_t>(Tag::Feature),      static_cast<std::uint16_t>(Tag::Time),
    static_cast<std::uint16_t>(Tag::Record),       static_cast<std::uint16_t>(Tag::Limit),
    static_cast<std::uint16_t>(Tag::Argument),     static_cast<std::uint16_t>(Tag::Environment),
    static_cast<std::uint16_t>(Tag::Mode),         static_cast<std::uint16_t>(Tag::FsPrefix),
    static_cast<std::uint16_t>(Tag::RunLimit),     static_cast<std::uint16_t>(Tag::RunFlag),
    static_cast<std::uint16_t>(Tag::ResourceLimit),
};

std::uint16_t get_u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t fixed(std::string_view bytes, std::size_t width, Tag tag) {
    if (bytes.size() != width) {
        throw ProtocolError("field " + std::to_string(static_cast<unsigned>(tag)) + " has " +
                            std::to_string(bytes.size()) + " bytes, expected " + std::to_string(width));
    }
    return get_u64(std::string(bytes) + std::string(8 - width, '\0'), 0);
}

} // namespace

std::string_view name(Kind kind) {
    const auto* entry = info(static_cast<std::uint16_t>(kind));
    return entry ? entry->name : "unknown";
}

bool known_kind(std::uint16_t kind) { return info(kind) != nullptr; }

std::size_t descriptors_of(Kind kind) {
    const auto* entry = info(static_cast<std::uint16_t>(kind));
    return entry ? entry->descriptors : 0;
}

bool known_tag(std::uint16_t tag) {
    return (tag >= 1 && tag <= 32) || (tag >= 40 && tag <= 59);
}

bool list_tag(std::uint16_t tag) { return std::ranges::find(list_tags, tag) != std::end(list_tags); }

void put_u16(std::string& out, std::uint16_t value) {
    out += static_cast<char>(value & 0xff);
    out += static_cast<char>(value >> 8);
}

void put_u32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out += static_cast<char>((value >> (8 * i)) & 0xff);
}

void put_u64(std::string& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out += static_cast<char>((value >> (8 * i)) & 0xff);
}

std::uint64_t get_u64(std::string_view bytes, std::size_t offset) {
    if (offset + 8 > bytes.size()) throw ProtocolError("a number passes the end of its field");
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) value = (value << 8) | static_cast<std::uint8_t>(bytes[offset + i]);
    return value;
}

std::string named_value(std::string_view name, std::uint64_t value) {
    std::string out;
    put_u64(out, value);
    out += name;
    return out;
}

std::pair<std::string, std::uint64_t> read_named_value(std::string_view bytes) {
    if (bytes.size() < 9) throw ProtocolError("a named value is too short");
    return {std::string(bytes.substr(8)), get_u64(bytes, 0)};
}

Message& Message::add(Tag tag, std::string_view bytes) {
    fields.push_back({static_cast<std::uint16_t>(tag), std::string(bytes)});
    return *this;
}

Message& Message::add_u8(Tag tag, std::uint8_t value) { return add(tag, std::string(1, static_cast<char>(value))); }

Message& Message::add_u16(Tag tag, std::uint16_t value) {
    std::string out;
    put_u16(out, value);
    return add(tag, out);
}

Message& Message::add_u32(Tag tag, std::uint32_t value) {
    std::string out;
    put_u32(out, value);
    return add(tag, out);
}

Message& Message::add_u64(Tag tag, std::uint64_t value) {
    std::string out;
    put_u64(out, value);
    return add(tag, out);
}

Message& Message::add_i64(Tag tag, std::int64_t value) { return add_u64(tag, static_cast<std::uint64_t>(value)); }

const Field* Message::find(Tag tag) const {
    for (const auto& field : fields) {
        if (field.tag == static_cast<std::uint16_t>(tag)) return &field;
    }
    return nullptr;
}

std::vector<std::string_view> Message::all(Tag tag) const {
    std::vector<std::string_view> out;
    for (const auto& field : fields) {
        if (field.tag == static_cast<std::uint16_t>(tag)) out.emplace_back(field.bytes);
    }
    return out;
}

std::string_view Message::get(Tag tag) const {
    const auto* field = find(tag);
    if (!field) {
        throw ProtocolError(std::string(name(kind)) + " lacks field " + std::to_string(static_cast<unsigned>(tag)));
    }
    return field->bytes;
}

std::uint8_t Message::u8(Tag tag) const { return static_cast<std::uint8_t>(fixed(get(tag), 1, tag)); }
std::uint16_t Message::u16(Tag tag) const { return static_cast<std::uint16_t>(fixed(get(tag), 2, tag)); }
std::uint32_t Message::u32(Tag tag) const { return static_cast<std::uint32_t>(fixed(get(tag), 4, tag)); }
std::uint64_t Message::u64(Tag tag) const { return fixed(get(tag), 8, tag); }
std::int64_t Message::i64(Tag tag) const { return static_cast<std::int64_t>(u64(tag)); }

std::string encode(const Message& message) {
    std::string body;
    put_u16(body, static_cast<std::uint16_t>(message.kind));
    put_u16(body, 0);
    put_u64(body, message.sequence);
    for (const auto& field : message.fields) {
        if (field.bytes.size() > max_message_bytes) throw ProtocolError("a field is longer than a message");
        put_u16(body, field.tag);
        put_u32(body, static_cast<std::uint32_t>(field.bytes.size()));
        body += field.bytes;
        if (body.size() > max_message_bytes) {
            throw ProtocolError("the message is longer than " + std::to_string(max_message_bytes) + " bytes");
        }
    }
    std::string out;
    put_u32(out, static_cast<std::uint32_t>(body.size()));
    return out + body;
}

std::uint32_t announced_length(std::span<const std::uint8_t, 4> header) {
    const auto length = get_u32(header.data());
    if (length > max_message_bytes) {
        throw ProtocolError("a message of " + std::to_string(length) + " bytes is longer than " +
                            std::to_string(max_message_bytes));
    }
    if (length < header_bytes - 4) throw ProtocolError("a message is shorter than its header");
    return length;
}

Message decode(std::span<const std::uint8_t> bytes, std::size_t received_descriptors) {
    if (bytes.size() < header_bytes) throw ProtocolError("a message is shorter than its header");
    const auto length = announced_length(bytes.first<4>());
    if (bytes.size() != std::size_t{length} + 4) throw ProtocolError("the message length does not match its bytes");
    const auto kind_number = get_u16(bytes.data() + 4);
    const auto* kind = info(kind_number);
    if (!kind) throw ProtocolError("unknown message kind " + std::to_string(kind_number));
    if (get_u16(bytes.data() + 6) != 0) throw ProtocolError("nonzero flags");
    Message message;
    message.kind = kind->kind;
    message.sequence = get_u64(std::string_view(reinterpret_cast<const char*>(bytes.data()) + 8, 8), 0);
    std::set<std::uint16_t> seen;
    std::size_t offset = header_bytes;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 6) throw ProtocolError("a field header passes the end of the message");
        const auto tag = get_u16(bytes.data() + offset);
        const auto field_length = get_u32(bytes.data() + offset + 2);
        offset += 6;
        if (field_length > bytes.size() - offset) throw ProtocolError("a field length passes the end of the message");
        const std::string_view value(reinterpret_cast<const char*>(bytes.data()) + offset, field_length);
        offset += field_length;
        if (tag >= static_cast<std::uint16_t>(Tag::Extension)) continue;
        if (!known_tag(tag)) throw ProtocolError("unknown field " + std::to_string(tag));
        if (!seen.insert(tag).second && !list_tag(tag)) {
            throw ProtocolError("field " + std::to_string(tag) + " is repeated and is not a list");
        }
        message.fields.push_back({tag, std::string(value)});
    }
    for (const auto tag : kind->required) {
        if (!message.has(tag)) {
            throw ProtocolError(std::string(kind->name) + " lacks field " + std::to_string(static_cast<unsigned>(tag)));
        }
    }
    const std::size_t declared = message.has(Tag::Descriptors) ? message.u8(Tag::Descriptors) : 0;
    if (declared != kind->descriptors || received_descriptors != declared) {
        throw ProtocolError(std::string(kind->name) + " carries " + std::to_string(received_descriptors) +
                            " descriptors, declares " + std::to_string(declared) + " and needs " +
                            std::to_string(kind->descriptors));
    }
    return message;
}

// --- Values ---------------------------------------------------------------

std::string_view name(SupervisorState state) {
    switch (state) {
    case SupervisorState::Accepting: return "accepting";
    case SupervisorState::Retiring: return "retiring";
    case SupervisorState::Draining: return "draining";
    }
    return "unknown";
}

std::string_view name(LaunchPath path) { return path == LaunchPath::Warm ? "warm" : "fresh"; }

namespace {

constexpr std::pair<Reason, std::string_view> reasons[] = {
    {Reason::Protocol, "protocol"},
    {Reason::Identity, "identity"},
    {Reason::UnsupportedProtocol, "unsupported-protocol"},
    {Reason::UnsupportedFeature, "unsupported-feature"},
    {Reason::UnsupportedOption, "unsupported-option"},
    {Reason::NotReproducible, "not-reproducible"},
    {Reason::AdmissionApplications, "admission-applications"},
    {Reason::AdmissionPending, "admission-pending"},
    {Reason::ImageTooLarge, "image-too-large"},
    {Reason::AdmissionCache, "admission-cache"},
    {Reason::ImageMismatch, "image-mismatch"},
    {Reason::Draining, "draining"},
    {Reason::Retired, "retired"},
    {Reason::TokenUnknown, "token-unknown"},
    {Reason::TokenExpired, "token-expired"},
    {Reason::WorkerUnavailable, "worker-unavailable"},
};

constexpr std::pair<Terminal, std::string_view> terminals[] = {
    {Terminal::Prepared, "prepared"},     {Terminal::Starting, "starting"},
    {Terminal::Running, "running"},       {Terminal::Exited, "exited"},
    {Terminal::Trapped, "trapped"},       {Terminal::LoadFailed, "load-failed"},
    {Terminal::Stopped, "stopped"},       {Terminal::ClientLost, "client-lost"},
    {Terminal::Signaled, "signaled"},     {Terminal::WorkerLost, "worker-lost"},
    {Terminal::Unknown, "unknown"},       {Terminal::Expired, "expired"},
};

} // namespace

std::string_view name(Reason reason) {
    for (const auto& [value, text] : reasons) {
        if (value == reason) return text;
    }
    return "protocol";
}

std::optional<Reason> reason_named(std::string_view text) {
    for (const auto& [value, name] : reasons) {
        if (name == text) return value;
    }
    return std::nullopt;
}

bool falls_back(Reason reason) {
    switch (reason) {
    case Reason::Protocol:
    case Reason::Identity:
    case Reason::ImageMismatch:
    case Reason::TokenUnknown:
    case Reason::TokenExpired:
        return false;
    default:
        return true;
    }
}

std::string_view name(Terminal state) {
    for (const auto& [value, text] : terminals) {
        if (value == state) return text;
    }
    return "unknown";
}

std::optional<Terminal> terminal_named(std::string_view text) {
    for (const auto& [value, name] : terminals) {
        if (name == text) return value;
    }
    return std::nullopt;
}

bool is_live(Terminal state) {
    return state == Terminal::Prepared || state == Terminal::Starting || state == Terminal::Running;
}

std::string_view name(ForwardedSignal signal) {
    switch (signal) {
    case ForwardedSignal::Int: return "INT";
    case ForwardedSignal::Term: return "TERM";
    case ForwardedSignal::Hup: return "HUP";
    case ForwardedSignal::Quit: return "QUIT";
    case ForwardedSignal::Winch: return "WINCH";
    }
    return "?";
}

// --- Identifiers ----------------------------------------------------------

std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}

std::string application_id(std::string_view generation, const Epoch& epoch, std::uint64_t number) {
    return std::string(generation) + "." + hex(std::span(epoch).first(4)) + "." + std::to_string(number);
}

std::optional<ApplicationId> parse_application_id(std::string_view text) {
    const auto last = text.rfind('.');
    if (last == std::string_view::npos || last == 0) return std::nullopt;
    const auto second = text.rfind('.', last - 1);
    if (second == std::string_view::npos || second == 0) return std::nullopt;
    ApplicationId id;
    id.generation = std::string(text.substr(0, second));
    id.epoch = std::string(text.substr(second + 1, last - second - 1));
    const auto digits = text.substr(last + 1);
    if (id.epoch.size() != 8 || id.epoch.find_first_not_of("0123456789abcdef") != std::string::npos) return std::nullopt;
    if (digits.empty() || digits.size() > 19 || digits.front() == '0' ||
        digits.find_first_not_of("0123456789") != std::string_view::npos) {
        return std::nullopt;
    }
    std::from_chars(digits.data(), digits.data() + digits.size(), id.number);
    if (id.generation.find('/') != std::string::npos || id.generation.find('\0') != std::string::npos ||
        id.generation == "." || id.generation == "..") {
        return std::nullopt;
    }
    return id;
}

// --- The envelope -----------------------------------------------------------

bool Envelope::flag(std::string_view name) const { return std::ranges::find(flags, name) != flags.end(); }

void put_envelope(Message& message, const Envelope& envelope) {
    message.add(Tag::ImageDigest, std::string_view(reinterpret_cast<const char*>(envelope.digest.data()), 32));
    message.add_u64(Tag::ImageSize, envelope.image_size);
    message.add(Tag::ImagePath, envelope.image_path);
    for (const auto& argument : envelope.arguments) message.add(Tag::Argument, argument);
    for (const auto& variable : envelope.environment) message.add(Tag::Environment, variable);
    for (const auto& variable : envelope.mode) message.add(Tag::Mode, variable);
    const std::uint8_t policy = (envelope.fs ? 1 : 0) | (envelope.net ? 2 : 0) | (envelope.env ? 4 : 0) |
                                (envelope.clock ? 8 : 0) | (envelope.entropy ? 16 : 0) | (envelope.native ? 32 : 0);
    message.add_u8(Tag::Policy, policy);
    for (const auto& prefix : envelope.fs_prefixes) message.add(Tag::FsPrefix, prefix);
    for (const auto& [name, value] : envelope.limits) message.add(Tag::RunLimit, named_value(name, value));
    for (const auto& flag : envelope.flags) message.add(Tag::RunFlag, flag);
    message.add_u8(Tag::TaskDump, envelope.task_dump);
    if (envelope.trace_events) message.add(Tag::TraceEvents, *envelope.trace_events);
    message.add_u32(Tag::Umask, envelope.umask);
    for (const auto& limit : envelope.resource_limits) {
        std::string bytes(1, static_cast<char>(limit.resource));
        put_u64(bytes, limit.soft);
        put_u64(bytes, limit.hard);
        message.add(Tag::ResourceLimit, bytes);
    }
    message.add_i64(Tag::Nice, envelope.nice);
    message.add_u64(Tag::IgnoredSignals, envelope.ignored_signals);
    message.add_u64(Tag::BlockedSignals, envelope.blocked_signals);
    message.add_u8(Tag::Lifetime, static_cast<std::uint8_t>(envelope.lifetime));
    message.add_u64(Tag::ClientClock, envelope.client_clock);
}

Envelope read_envelope(const Message& message) {
    Envelope envelope;
    const auto digest = message.get(Tag::ImageDigest);
    if (digest.size() != 32) throw ProtocolError("the image digest is not 32 bytes");
    std::copy(digest.begin(), digest.end(), envelope.digest.begin());
    envelope.image_size = message.u64(Tag::ImageSize);
    if (const auto* path = message.find(Tag::ImagePath)) envelope.image_path = path->bytes;
    for (const auto value : message.all(Tag::Argument)) envelope.arguments.emplace_back(value);
    for (const auto value : message.all(Tag::Environment)) {
        if (value.find('=') == std::string_view::npos || value.find('\0') != std::string_view::npos) {
            throw ProtocolError("an environment entry is not NAME=VALUE");
        }
        envelope.environment.emplace_back(value);
    }
    for (const auto value : message.all(Tag::Mode)) envelope.mode.emplace_back(value);
    const auto policy = message.u8(Tag::Policy);
    if (policy & ~0x3f) throw ProtocolError("unknown policy bits");
    envelope.fs = policy & 1;
    envelope.net = policy & 2;
    envelope.env = policy & 4;
    envelope.clock = policy & 8;
    envelope.entropy = policy & 16;
    envelope.native = policy & 32;
    for (const auto value : message.all(Tag::FsPrefix)) envelope.fs_prefixes.emplace_back(value);
    for (const auto value : message.all(Tag::RunLimit)) {
        auto [name, number] = read_named_value(value);
        if (!envelope.limits.emplace(std::move(name), number).second) throw ProtocolError("a run limit is repeated");
    }
    for (const auto value : message.all(Tag::RunFlag)) envelope.flags.emplace_back(value);
    if (message.has(Tag::TaskDump)) {
        envelope.task_dump = message.u8(Tag::TaskDump);
        if (envelope.task_dump > 2) throw ProtocolError("unknown task dump form");
    }
    if (const auto* trace = message.find(Tag::TraceEvents)) envelope.trace_events = trace->bytes;
    envelope.umask = message.u32(Tag::Umask);
    if (envelope.umask > 0777) throw ProtocolError("the umask is out of range");
    for (const auto value : message.all(Tag::ResourceLimit)) {
        if (value.size() != 17) throw ProtocolError("a resource limit is not 17 bytes");
        ResourceLimit limit{static_cast<std::uint8_t>(value[0]), get_u64(value, 1), get_u64(value, 9)};
        if (limit.resource < 1 || limit.resource > 6) throw ProtocolError("unknown resource");
        envelope.resource_limits.push_back(limit);
    }
    envelope.nice = message.i64(Tag::Nice);
    envelope.ignored_signals = message.u64(Tag::IgnoredSignals);
    envelope.blocked_signals = message.u64(Tag::BlockedSignals);
    const auto lifetime = message.u8(Tag::Lifetime);
    if (lifetime != 1 && lifetime != 2) throw ProtocolError("unknown lifetime");
    envelope.lifetime = static_cast<Lifetime>(lifetime);
    envelope.client_clock = message.u64(Tag::ClientClock);
    return envelope;
}

} // namespace neura::service
