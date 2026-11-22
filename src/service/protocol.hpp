#pragma once

// The local protocol of the runtime service (docs/design/Service030.md,
// "Protocol"): framing, fields, the message kinds, the launch envelope, the
// reasons of a refusal, the terminal states, and application IDs. Pure data
// handling: no socket, no file and no process is touched here, so the codec
// is tested on its own (tests/service/protocol_tests.cpp).
//
// A message is length:u32 (the bytes that follow), kind:u16, flags:u16
// (zero), sequence:u64, then fields; a field is tag:u16, length:u32, bytes.
// Integers are little-endian. Descriptors travel beside a message
// (SCM_RIGHTS), and a message that carries any declares how many in its
// `descriptors` field.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neura::service {

// Protocol 1.0: an independent version axis of the release.
inline constexpr std::uint16_t protocol_major = 1;
inline constexpr std::uint16_t protocol_minor = 0;
inline constexpr std::string_view protocol_text = "1.0";

// The baseline's service limits (docs/design/Release030.md).
inline constexpr std::uint32_t max_message_bytes = 1'048'576;
inline constexpr std::size_t max_applications = 64;
inline constexpr std::size_t max_pending = 16;
inline constexpr std::uint64_t max_cache_bytes = std::uint64_t{256} << 20;
inline constexpr std::size_t max_terminal_records = 1'024;
inline constexpr std::uint64_t record_lifetime_seconds = 24 * 60 * 60;
inline constexpr std::size_t ready_workers = 1;
// The header: length, kind, flags, sequence.
inline constexpr std::size_t header_bytes = 4 + 2 + 2 + 8;

enum class Kind : std::uint16_t {
    Hello = 1,
    Welcome = 2,
    Prepare = 3,
    Prepared = 4,
    Image = 5,
    Commit = 6,
    Committed = 7,
    Outcome = 8,
    Rejected = 9,
    Cancel = 10,
    Signal = 11,
    Query = 12,
    Status = 13,
    List = 14,
    Listing = 15,
    Stop = 16,
    Shutdown = 17,
    Retire = 18,
    CacheClear = 19,
    // The private channel between a supervisor and its workers. It has no
    // version: a worker refuses a supervisor of another build.
    WorkerReady = 0x100,
    WorkerAssign = 0x101,
    WorkerStarted = 0x102,
    WorkerFinished = 0x103,
};

std::string_view name(Kind kind);
bool known_kind(std::uint16_t kind);
// How many descriptors a message of this kind carries.
std::size_t descriptors_of(Kind kind);

// Field tags. Tags at or above 0x8000 are optional extensions a receiver
// that does not know them ignores; an unknown tag below it is malformed.
enum class Tag : std::uint16_t {
    ProtocolLow = 1,
    ProtocolHigh = 2,
    Build = 3,
    Installation = 4,
    Generation = 5,
    Purpose = 6,
    Feature = 7,         // list
    Protocol = 8,
    Epoch = 9,
    State = 10,
    Descriptors = 11,
    RequestId = 12,
    Token = 13,
    ValidMilliseconds = 14,
    ImageNeeded = 15,
    Path = 16,
    ApplicationId = 17,
    Terminal = 18,
    ExitStatus = 19,
    Time = 20,           // list: name and nanoseconds
    Reason = 21,
    Sentence = 22,
    NotStarted = 23,
    SignalNumber = 24,
    GraceMilliseconds = 25,
    Terminate = 26,
    Record = 27,         // list: one JSON record each
    Limit = 28,          // list: name and value, the supervisor's limits
    ApplicationCount = 29,
    CacheBytes = 30,
    ReadyWorkers = 31,
    TrapClass = 32,
    // The launch envelope.
    ImageDigest = 40,
    ImageSize = 41,
    ImagePath = 42,
    Argument = 43,       // list
    Environment = 44,    // list: NAME=VALUE
    Mode = 45,           // list: NAME=VALUE of the mode summary
    Policy = 46,
    FsPrefix = 47,       // list
    RunLimit = 48,       // list: name and value
    RunFlag = 49,        // list: name
    TaskDump = 50,
    TraceEvents = 51,
    Umask = 52,
    ResourceLimit = 53,  // list: resource, soft, hard
    Nice = 54,
    IgnoredSignals = 55,
    BlockedSignals = 56,
    Lifetime = 57,
    ClientClock = 58,
    Pid = 59,
    // The extension range starts here.
    Extension = 0x8000,
};

bool known_tag(std::uint16_t tag);
bool list_tag(std::uint16_t tag);

struct Field {
    std::uint16_t tag = 0;
    std::string bytes;
};

struct Message {
    Kind kind = Kind::Hello;
    std::uint64_t sequence = 0;
    std::vector<Field> fields;

    // Builders.
    Message& add(Tag tag, std::string_view bytes);
    Message& add_u8(Tag tag, std::uint8_t value);
    Message& add_u16(Tag tag, std::uint16_t value);
    Message& add_u32(Tag tag, std::uint32_t value);
    Message& add_u64(Tag tag, std::uint64_t value);
    Message& add_i64(Tag tag, std::int64_t value);
    // Readers. `get` throws ProtocolError when the field is missing.
    const Field* find(Tag tag) const;
    bool has(Tag tag) const { return find(tag) != nullptr; }
    std::vector<std::string_view> all(Tag tag) const;
    std::string_view get(Tag tag) const;
    std::uint8_t u8(Tag tag) const;
    std::uint16_t u16(Tag tag) const;
    std::uint32_t u32(Tag tag) const;
    std::uint64_t u64(Tag tag) const;
    std::int64_t i64(Tag tag) const;
};

// A malformed message: the receiver answers one Rejected with reason
// `protocol` and closes the connection.
class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The bytes of a message, header included. Throws ProtocolError when the
// message would be longer than max_message_bytes.
std::string encode(const Message& message);

// The length the header at `header` announces (the bytes after the length
// word), checked against the limit.
std::uint32_t announced_length(std::span<const std::uint8_t, 4> header);

// Decodes one whole message: `bytes` is the header and the body. Checks the
// kind, the flags, every field's bounds, unknown and repeated tags, and the
// fields the kind requires. `received_descriptors` is how many descriptors
// came with it; a different number than the kind carries is malformed.
Message decode(std::span<const std::uint8_t> bytes, std::size_t received_descriptors);

// Little-endian helpers for nested values.
void put_u16(std::string& out, std::uint16_t value);
void put_u32(std::string& out, std::uint32_t value);
void put_u64(std::string& out, std::uint64_t value);
std::uint64_t get_u64(std::string_view bytes, std::size_t offset);

// A name and a number, as in the Time, Limit and RunLimit lists.
std::string named_value(std::string_view name, std::uint64_t value);
std::pair<std::string, std::uint64_t> read_named_value(std::string_view bytes);

// --- Values -------------------------------------------------------------

enum class Purpose : std::uint8_t { Launch = 1, Control = 2 };

enum class SupervisorState : std::uint8_t { Accepting = 1, Retiring = 2, Draining = 3 };
std::string_view name(SupervisorState state);

enum class LaunchPath : std::uint8_t { Warm = 1, Fresh = 2 };
std::string_view name(LaunchPath path);

enum class Lifetime : std::uint8_t { Foreground = 1, Detached = 2 };

// The reasons of Rejected, and whether `auto` runs directly after one.
enum class Reason : std::uint8_t {
    Protocol = 1,
    Identity,
    UnsupportedProtocol,
    UnsupportedFeature,
    UnsupportedOption,
    NotReproducible,
    AdmissionApplications,
    AdmissionPending,
    ImageTooLarge,
    AdmissionCache,
    ImageMismatch,
    Draining,
    Retired,
    TokenUnknown,
    TokenExpired,
    // A worker did not report ready in time; nothing was committed.
    WorkerUnavailable,
};
std::string_view name(Reason reason);
std::optional<Reason> reason_named(std::string_view text);
// True when `--runtime=auto` may run directly after this refusal.
bool falls_back(Reason reason);

enum class Terminal : std::uint8_t {
    Prepared = 1,
    Starting,
    Running,
    Exited,
    Trapped,
    LoadFailed,
    Stopped,
    ClientLost,
    Signaled,
    WorkerLost,
    Unknown,
    Expired,
};
std::string_view name(Terminal state);
std::optional<Terminal> terminal_named(std::string_view text);
bool is_live(Terminal state);

// Signals a client forwards, by their portable names.
enum class ForwardedSignal : std::uint8_t { Int = 1, Term, Hup, Quit, Winch };
std::string_view name(ForwardedSignal signal);

// --- Identifiers --------------------------------------------------------

using Epoch = std::array<std::uint8_t, 16>;
using RequestId = std::array<std::uint8_t, 16>;
using Token = std::array<std::uint8_t, 32>;

std::string hex(std::span<const std::uint8_t> bytes);

// <generation>.<first 8 hexadecimal digits of the epoch>.<number>
std::string application_id(std::string_view generation, const Epoch& epoch, std::uint64_t number);
struct ApplicationId {
    std::string generation;
    std::string epoch;  // 8 lowercase hexadecimal digits
    std::uint64_t number = 0;
};
// Parses an application ID; nothing for a malformed one.
std::optional<ApplicationId> parse_application_id(std::string_view text);

// --- The launch envelope --------------------------------------------------

struct ResourceLimit {
    std::uint8_t resource = 0;  // ResourceKind
    std::uint64_t soft = 0;
    std::uint64_t hard = 0;
};

// The resources the envelope carries, by a number of their own so that the
// encoding does not depend on the host's RLIMIT_* values.
enum class ResourceKind : std::uint8_t { Nofile = 1, Stack, Core, Fsize, As, Cpu };
inline constexpr std::uint64_t unlimited = ~std::uint64_t{0};

struct Envelope {
    std::array<std::uint8_t, 32> digest{};
    std::uint64_t image_size = 0;
    // Shown by ps and in messages only; never used to find the bytes.
    std::string image_path;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    std::vector<std::string> mode;
    // The resolved capability policy.
    bool fs = false, net = false, env = false, clock = false, entropy = false, native = false, raw = false;
    std::vector<std::string> fs_prefixes;
    // Every limit an option sets, by option name without its dashes, and
    // the runner's flags (stats, tier-stats, profile, trace-wall-time,
    // virtual-time).
    std::map<std::string, std::uint64_t> limits;
    std::vector<std::string> flags;
    std::uint8_t task_dump = 0;  // 0 none, 1 text, 2 json
    std::optional<std::string> trace_events;
    std::uint32_t umask = 022;
    std::vector<ResourceLimit> resource_limits;
    std::int64_t nice = 0;
    std::uint64_t ignored_signals = 0;
    std::uint64_t blocked_signals = 0;
    Lifetime lifetime = Lifetime::Foreground;
    std::uint64_t client_clock = 0;

    bool flag(std::string_view name) const;
};

void put_envelope(Message& message, const Envelope& envelope);
Envelope read_envelope(const Message& message);

} // namespace neura::service
