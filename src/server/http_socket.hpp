#pragma once

#include "server/http_connection.hpp"

#ifdef _WIN32
// Winsock 2.2 (W14): pollfd, POLLIN and POLLOUT are WSAPoll's, and the
// socket calls report errors through WSAGetLastError (platform::
// last_socket_error), not errno. This header is private to the server's .cpp
// files.
#include "platform/socket.hpp"
#include "platform/windows.hpp"

#include <climits>
#else
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#endif

namespace neura::server::http::socket_io {

#ifdef _WIN32
// Non-blocking and not inherited by the workers the host starts. Windows has
// no SIGPIPE: a send to a closed connection fails with WSAECONNRESET.
inline bool configure(platform::native_socket descriptor) {
    if (!platform::set_nonblocking(descriptor) || !platform::set_no_inherit(descriptor)) return false;
    // As on POSIX: a response is written as whole pieces, which Nagle's
    // algorithm would hold back for the client's delayed acknowledgement.
    const BOOL no_delay = TRUE;
    (void)::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay),
                       sizeof no_delay);
    return true;
}

// At most INT_MAX bytes, the most one Winsock call takes.
inline int send(platform::native_socket descriptor, const void* data, std::size_t size) {
    const int count = size > INT_MAX ? INT_MAX : static_cast<int>(size);
    return ::send(descriptor, static_cast<const char*>(data), count, 0);
}

inline int recv(platform::native_socket descriptor, void* data, std::size_t size) {
    const int count = size > INT_MAX ? INT_MAX : static_cast<int>(size);
    return ::recv(descriptor, static_cast<char*>(data), count, 0);
}

// WSAPoll does not return early for a signal, so only a timeout that ends
// before the deadline (its clock is coarser than steady_clock) repeats.
inline int poll_until(std::span<pollfd> entries, ConnectionClock::time_point deadline) {
    for (;;) {
        const auto left = deadline - ConnectionClock::now();
        if (left <= ConnectionClock::duration::zero()) return 0;
        const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(left).count();
        const int timeout = milliseconds > INT_MAX ? INT_MAX : static_cast<int>(milliseconds);
        const int ready = ::WSAPoll(entries.data(), static_cast<ULONG>(entries.size()), timeout);
        if (ready != 0) return ready;
    }
}
#else
inline bool configure(int descriptor) {
    const int status = ::fcntl(descriptor, F_GETFL);
    const int flags = ::fcntl(descriptor, F_GETFD);
    if (status < 0 || flags < 0 || ::fcntl(descriptor, F_SETFL, status | O_NONBLOCK) < 0 ||
        ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) < 0) return false;
#endif
    // A response is written as whole pieces (head and body, or one chunk);
    // Nagle's algorithm would hold a piece back until the client's delayed
    // acknowledgement of the previous one, about 40 ms on Linux. Not a TCP
    // socket (a test's socket pair): nothing to turn off.
    const int no_delay = 1;
    (void)::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof no_delay);
    return true;
}

inline ssize_t send(int descriptor, const void* data, std::size_t size) {
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    return ::send(descriptor, data, size, flags);
}

inline int poll_until(std::span<pollfd> entries, ConnectionClock::time_point deadline) {
    for (;;) {
        const auto left = deadline - ConnectionClock::now();
        if (left <= ConnectionClock::duration::zero()) return 0;
        const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(left).count();
        const int timeout = milliseconds > INT_MAX ? INT_MAX : static_cast<int>(milliseconds);
        const int ready = ::poll(entries.data(), static_cast<nfds_t>(entries.size()), timeout);
        if (ready > 0 || (ready < 0 && errno != EINTR)) return ready;
    }
}
#endif

// Waits for `events` on `descriptor`, or for `wake` (when not no_wake) to
// become readable first, which answers Woken.
inline StreamStatus wait_for(platform::native_socket descriptor, short events, ConnectionClock::time_point deadline,
                             WakeDescriptor wake) {
    if (wake == no_wake) {
        pollfd entry{descriptor, events, 0};
        const int ready = poll_until(std::span(&entry, 1), deadline);
        if (ready == 0) return StreamStatus::TimedOut;
        return ready < 0 || (entry.revents & POLLNVAL) ? StreamStatus::Failed : StreamStatus::Ready;
    }
    pollfd entries[2] = {{descriptor, events, 0}, {wake, POLLIN, 0}};
    const int ready = poll_until(std::span(entries, 2), deadline);
    if (ready == 0) return StreamStatus::TimedOut;
    if (ready < 0 || (entries[0].revents & POLLNVAL)) return StreamStatus::Failed;
    if (entries[0].revents == 0 && entries[1].revents != 0) return StreamStatus::Woken;
    return StreamStatus::Ready;
}

inline StreamStatus wait_for(platform::native_socket descriptor, short events, ConnectionClock::time_point deadline) {
    pollfd entry{descriptor, events, 0};
    const int ready = poll_until(std::span(&entry, 1), deadline);
    if (ready == 0) return StreamStatus::TimedOut;
    // A hangup or error goes back through recv/send to report the outcome.
    return ready < 0 || (entry.revents & POLLNVAL) ? StreamStatus::Failed : StreamStatus::Ready;
}

} // namespace neura::server::http::socket_io
