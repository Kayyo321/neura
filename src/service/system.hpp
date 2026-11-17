#pragma once

// The operating-system side of the runtime service (docs/design/Service030.md):
// descriptors, sending and receiving protocol messages with descriptors
// beside them, deadlines, peer credentials, randomness, the files of an
// installation's service, the build identity, and the process settings a
// launch carries. POSIX only; the Windows build has no service.

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
    bool fill(int fd);
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
void send_message(int fd, const Message& message, const std::vector<int>& descriptors = {},
                  std::optional<Clock::time_point> deadline = std::nullopt);

// Waits for one whole message, until `deadline`. Nothing at the end of the
// stream; TransportError at the deadline.
std::optional<Received> receive_message(int fd, std::optional<Clock::time_point> deadline);

// The user ID of the process at the other end of a Unix socket.
std::optional<std::uint32_t> peer_uid(int fd);

void random_bytes(std::span<std::uint8_t> out);

// Sets FD_CLOEXEC, or O_NONBLOCK.
void set_cloexec(int fd);
void set_nonblocking(int fd, bool nonblocking);

// The build of this process's execution core: the version line and the
// GNU build ID of libneura-core (or, where it has none, the identity of the
// file), so a rebuilt tree is another build although its version line is
// the same.
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

// <endpoint> for a generation of an installation.
std::string endpoint_of(const fs::path& root, std::string_view generation);

// The run directory: $XDG_RUNTIME_DIR when it is set, absolute, owned by
// this user and mode 0700; else ${TMPDIR:-/tmp}/neura-<uid>, created mode
// 0700 when `create`. Throws TransportError with a message when it is
// refused or the socket path would be too long.
fs::path run_directory(bool create);

struct Endpoint {
    fs::path directory;  // <run>/neura/<endpoint>
    fs::path socket;     // .../s
    fs::path lock;       // .../lock
    fs::path start;      // .../start
};
Endpoint endpoint_files(const fs::path& run, const fs::path& root, std::string_view generation);

// Creates <run>/neura and the endpoint directory, mode 0700, refusing a
// directory of another owner or mode or a symbolic link.
void make_private_directory(const fs::path& directory);
// Whether `directory` is a real directory of this user with mode 0700.
bool private_directory(const fs::path& directory);

// ${XDG_STATE_HOME:-$HOME/.local/state}/neura/<id>
fs::path state_root(const Installation& installation);
fs::path state_directory(const Installation& installation, std::string_view generation);

// Writes `text` to `path` through a temporary file and a rename.
void write_atomically(const fs::path& path, std::string_view text);
std::optional<std::string> read_text(const fs::path& path);

// The generations an installation lists: its marker's versions, or the
// running one.
std::vector<std::string> live_generations(const Installation& installation);
// Every generation that has a state directory.
std::vector<std::string> recorded_generations(const Installation& installation);

// This process's environment, as `environ` holds it; macOS reaches it
// through _NSGetEnviron from a shared library.
char** process_environment();
// Removes every variable of this process's environment.
void clear_environment();

// --- Process settings --------------------------------------------------------

// The settings of this process that a launch carries: umask, soft and hard
// limits, nice value, ignored signals and the signal mask.
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
