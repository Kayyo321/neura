#pragma once

// The operating-system side of the runtime service (docs/design/Service030.md):
// descriptors, sending and receiving protocol messages with descriptors
// beside them, deadlines, peer credentials, randomness, the files of an
// installation's service, the build identity, and the process settings a
// launch carries. On Windows (docs/design/Windows030.md, W29) a descriptor is
// a HANDLE or a SOCKET, handles travel in-band after DuplicateHandle into the
// receiving process, and the files live under %LOCALAPPDATA%/Neura.

#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neura::service {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

#ifdef _WIN32
// A HANDLE or a SOCKET: both are pointer-width, and both are invalid as
// all ones (INVALID_HANDLE_VALUE, INVALID_SOCKET).
using Descriptor = std::uintptr_t;
inline constexpr Descriptor invalid_descriptor = ~Descriptor{0};

// An owned handle or socket, closed when it goes away: CloseHandle, or
// closesocket for a socket.
class UniqueFd {
public:
    enum class Kind { handle, socket };

    UniqueFd() = default;
    explicit UniqueFd(Descriptor fd, Kind kind = Kind::handle) : fd_(fd), kind_(kind) {}
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, invalid_descriptor)), kind_(other.kind_) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, invalid_descriptor);
            kind_ = other.kind_;
        }
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() { reset(); }

    Descriptor get() const { return fd_; }
    Kind kind() const { return kind_; }
    // A null HANDLE is invalid too.
    explicit operator bool() const { return fd_ != invalid_descriptor && fd_ != 0; }
    Descriptor release() { return std::exchange(fd_, invalid_descriptor); }
    void reset(Descriptor fd = invalid_descriptor);
    void reset(Descriptor fd, Kind kind) {
        reset(fd);
        kind_ = kind;
    }

private:
    Descriptor fd_ = invalid_descriptor;
    Kind kind_ = Kind::handle;
};
#else
using Descriptor = int;
inline constexpr Descriptor invalid_descriptor = -1;

// An owned descriptor, closed when it goes away.
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_(fd) {}
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd() { reset(); }

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
    int release() { return std::exchange(fd_, -1); }
    void reset(int fd = -1);

private:
    int fd_ = -1;
};
#endif

// A failure of the operating system or of the peer: the connection is
// unusable. Not a protocol error, which is answered first.
class TransportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One message and the descriptors that came with it.
struct Received {
    Message message;
    std::vector<UniqueFd> descriptors;
};

// Reads one message at a time from a stream socket, never past its end, so
// that the descriptors of the next message stay queued. Feed it when the
// socket is readable; poll() returns a message once one is whole.
class MessageReader {
public:
    // Reads what is available without blocking. Returns false at the end
    // of the stream when no message is in progress; throws TransportError
    // on a broken stream or ProtocolError on a malformed message.
    bool fill(Descriptor fd);
    std::optional<Received> take();
    bool in_progress() const { return !buffer_.empty(); }

private:
    std::string buffer_;
    std::size_t need_ = 4;
    std::vector<UniqueFd> descriptors_;
    std::optional<Received> ready_;
};

// Sends one message with its descriptors in one sendmsg. Blocks until it
// is written or `deadline` passes (TransportError).
void send_message(Descriptor fd, const Message& message, const std::vector<Descriptor>& descriptors = {},
                  std::optional<Clock::time_point> deadline = std::nullopt);

#ifdef _WIN32
// The field that carries a message's handles on Windows: their values in
// the receiving process, u64 each, in order. The sender duplicates each
// handle into the receiver (DuplicateHandle with the same access) before it
// sends, and closes those duplicates again when the message is not sent. An
// extension tag, so the codec of protocol.hpp skips it.
inline constexpr std::uint16_t handles_tag = 0x8001;

// send_message to a receiver that is not the peer of an AF_UNIX connection,
// such as a worker behind its channel: `recipient` is its process HANDLE,
// with PROCESS_DUP_HANDLE access. The plain form sends the handles to the
// process at the other end of the connection (SIO_AF_UNIX_GETPEERPID).
void send_message(Descriptor fd, const Message& message, const std::vector<Descriptor>& descriptors,
                  std::optional<Clock::time_point> deadline, void* recipient);
#endif

// Waits for one whole message, until `deadline`. Nothing at the end of the
// stream; TransportError at the deadline.
std::optional<Received> receive_message(Descriptor fd, std::optional<Clock::time_point> deadline);

#ifdef _WIN32
// The process at the other end of an AF_UNIX connection (W29): whether the
// user SID of its token is this process's, and how its integrity level
// compares with this process's.
struct PeerIdentity {
    bool known = false;      // the peer's process and token could be read
    bool same_user = false;  // the user SID is this process's
    int integrity = 0;       // -1 lower than this process's, 0 the same, 1 higher
};
PeerIdentity peer_identity(Descriptor socket);
#else
// The user ID of the process at the other end of a Unix socket.
std::optional<std::uint32_t> peer_uid(int fd);
#endif

void random_bytes(std::span<std::uint8_t> out);

// Sets FD_CLOEXEC, or O_NONBLOCK. On Windows: clears HANDLE_FLAG_INHERIT,
// or sets a socket's non-blocking mode.
void set_cloexec(Descriptor fd);
void set_nonblocking(Descriptor fd, bool nonblocking);

// The build of this process's execution core: the version line and the
// GNU build ID of libneura-core (or, where it has none, the identity of the
// file), so a rebuilt tree is another build although its version line is
// the same. On Windows the GUID and age of the CodeView (RSDS) record of the
// module that holds the core, neura-core-<version>.dll, which /Brepro makes
// a hash of its content; without a record, the SHA-256 of the module's file.
std::string own_build();

// --- Files -----------------------------------------------------------------

// Where an installation's service lives (Service030.md, "Files").
struct Installation {
    // The real path of the install root, or of a source build's bin.
    fs::path root;
    // The directory name under versions/, or "dev" for a source build.
    std::string generation;
    // The first 16 hexadecimal digits of the SHA-256 of the root's path.
    std::string id;
    // The directory of neura, neura-run and neurad.
    fs::path bin;
    bool installed = false;
    // A schema 1 marker and a core manifest in the running generation.
    bool unmigrated = false;
};

// The installation of the executable at `executable` (its real path).
Installation installation_of(const fs::path& executable);
// The installation at an install root, as the lifecycle names it: its
// generation is the one `current` names, its bin that generation's.
Installation installation_at_root(const fs::path& root);

// <endpoint> for a generation of an installation.
std::string endpoint_of(const fs::path& root, std::string_view generation);

// The run directory: $XDG_RUNTIME_DIR when it is set, absolute, owned by
// this user and mode 0700; else ${TMPDIR:-/tmp}/neura-<uid>, created mode
// 0700 when `create`. Throws TransportError with a message when it is
// refused or the socket path would be too long.
//
// Windows (W29): %LOCALAPPDATA%/Neura/run (the known folder), created with a
// protected DACL that grants only this user when `create`. An endpoint whose
// socket path there would not fit in an AF_UNIX address (107 bytes) is under
// %TEMP%/neura-<the first 16 hexadecimal digits of the SHA-256 of this
// user's SID> instead; endpoint_files decides.
fs::path run_directory(bool create);

struct Endpoint {
    fs::path directory;  // <run>/neura/<endpoint>; on Windows <run>/<endpoint>
    fs::path socket;     // .../s
    fs::path lock;       // .../lock
    fs::path start;      // .../start
};
Endpoint endpoint_files(const fs::path& run, const fs::path& root, std::string_view generation);

// Creates <run>/neura and the endpoint directory, mode 0700, refusing a
// directory of another owner or mode or a symbolic link. On Windows a
// protected DACL that grants only this user stands for mode 0700.
void make_private_directory(const fs::path& directory);
// Whether `directory` is a real directory of this user with mode 0700.
bool private_directory(const fs::path& directory);

// ${XDG_STATE_HOME:-$HOME/.local/state}/neura/<id>; on Windows
// %LOCALAPPDATA%/Neura/state/<id>, below a private state directory.
fs::path state_root(const Installation& installation);
fs::path state_directory(const Installation& installation, std::string_view generation);
#ifdef _WIN32
// Creates %LOCALAPPDATA%/Neura/state and state_root() where they are
// missing, each with a protected DACL that grants only this user, which what
// is created below them inherits. Throws TransportError.
void make_state_root(const Installation& installation);
#endif

// Writes `text` to `path` through a temporary file and a rename.
void write_atomically(const fs::path& path, std::string_view text);
std::optional<std::string> read_text(const fs::path& path);

// The generations an installation lists: its marker's generations, or the
// running one.
std::vector<std::string> live_generations(const Installation& installation);
// Every generation that has a state directory.
std::vector<std::string> recorded_generations(const Installation& installation);

#ifndef _WIN32
// This process's environment, as `environ` holds it; macOS reaches it
// through _NSGetEnviron from a shared library.
char** process_environment();
#endif
// This process's environment as NAME=VALUE entries: `environ` on POSIX; on
// Windows the UTF-16 block as UTF-8 (platform::environment, without the
// per-drive "=C:" entries), which throws std::system_error for a variable
// that is not valid text.
std::vector<std::string> environment_entries();
// Removes every variable of this process's environment.
void clear_environment();

// --- Process settings --------------------------------------------------------

// The settings of this process that a launch carries: umask, soft and hard
// limits, nice value, ignored signals and the signal mask. Windows has no
// umask, resource limits, nice value or signal mask of that kind: only the
// priority class (GetPriorityClass) is captured, in the nice field, and the
// others keep their defaults.
void capture_process_settings(Envelope& envelope);

// Signal numbers as bits of the envelope's sets.
std::uint64_t signal_bit(int signal);

// The host's RLIMIT_* value of an envelope resource.
int resource_of(std::uint8_t resource);

// Nanoseconds of the steady clock since its epoch.
std::uint64_t clock_ns(Clock::time_point point);
Clock::time_point clock_at(std::uint64_t ns);

// Milliseconds as text with three decimals.
std::string milliseconds(std::uint64_t ns);

} // namespace neura::service
