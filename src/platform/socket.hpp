#pragma once

// Sockets on Winsock 2.2 and POSIX (W14). A socket is a pointer-width SOCKET
// on Windows and an int elsewhere; call ensure_sockets() before the first
// socket call of a process.

#include "self.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <system_error>

namespace neura::platform {

#ifdef _WIN32
using native_socket = std::uintptr_t; // SOCKET
inline constexpr native_socket invalid_socket = ~native_socket{0};
#else
using native_socket = int;
inline constexpr native_socket invalid_socket = -1;
#endif

// Starts Winsock 2.2 once per process (thread-safe; later calls return the
// first result). Always succeeds on POSIX.
std::error_code ensure_sockets() noexcept;

bool close_socket(native_socket socket) noexcept;
bool set_nonblocking(native_socket socket, bool nonblocking = true) noexcept;
// Keeps child processes from inheriting the socket (FD_CLOEXEC,
// HANDLE_FLAG_INHERIT).
bool set_no_inherit(native_socket socket) noexcept;

// The error of the last failed socket call on this thread: WSAGetLastError()
// on Windows, errno elsewhere.
struct SocketError {
    int code = 0;

    bool would_block() const noexcept;      // EWOULDBLOCK, EAGAIN, EINPROGRESS, WSAEWOULDBLOCK, WSAEINPROGRESS
    bool interrupted() const noexcept;      // EINTR, WSAEINTR
    bool connection_reset() const noexcept; // ECONNRESET, EPIPE, ECONNABORTED and the WSA equivalents
    std::error_code error_code() const noexcept;
    std::string message() const;
};
SocketError last_socket_error() noexcept;

// poll(2) or WSAPoll over sockets only. PollItem has the layout of pollfd and
// WSAPOLLFD. Request poll_in and poll_out only (WSAPoll rejects other request
// bits); poll_error, poll_hangup and poll_invalid are reported in revents.
struct PollItem {
    native_socket socket = invalid_socket;
    short events = 0;
    short revents = 0;
};

#ifdef _WIN32
inline constexpr short poll_in = 0x0100 | 0x0200; // POLLRDNORM | POLLRDBAND
inline constexpr short poll_out = 0x0010;         // POLLWRNORM
inline constexpr short poll_error = 0x0001;
inline constexpr short poll_hangup = 0x0002;
inline constexpr short poll_invalid = 0x0004;
#else
inline constexpr short poll_in = 0x0001;
inline constexpr short poll_out = 0x0004;
inline constexpr short poll_error = 0x0008;
inline constexpr short poll_hangup = 0x0010;
inline constexpr short poll_invalid = 0x0020;
#endif

// Waits up to `timeout_ms` (negative: forever) for readiness. Returns the
// number of ready items, 0 on timeout, or -1 (see last_socket_error()).
int poll(std::span<PollItem> items, int timeout_ms) noexcept;

// A connected pair of stream sockets, both non-inheritable and blocking.
// POSIX: socketpair(AF_UNIX, SOCK_STREAM). Windows: an AF_UNIX listener in a
// fresh private directory under the temporary directory, connect, accept,
// then the path and directory are removed; when AF_UNIX is unavailable,
// loopback TCP whose accepted side must first receive a random token (128
// bits) from the connecting side (peer_pid() does not work on such a pair).
// Returns {invalid_socket, invalid_socket} with `error` set on failure.
std::array<native_socket, 2> socket_pair(std::error_code& error);

// The process at the other end of a connected AF_UNIX socket:
// SIO_AF_UNIX_GETPEERPID on Windows, SO_PEERCRED on Linux, LOCAL_PEERPID on
// macOS (getpeereid gives no pid). Not supported elsewhere.
std::optional<process_id> peer_pid(native_socket socket, std::error_code& error);

} // namespace neura::platform
