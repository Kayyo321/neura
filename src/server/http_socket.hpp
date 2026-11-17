#pragma once

#include "server/http_connection.hpp"

#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

namespace neura::server::http::socket_io {

inline bool configure(int descriptor) {
    const int status = ::fcntl(descriptor, F_GETFL);
    const int flags = ::fcntl(descriptor, F_GETFD);
    if (status < 0 || flags < 0 || ::fcntl(descriptor, F_SETFL, status | O_NONBLOCK) < 0 ||
        ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) < 0) return false;
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) < 0) return false;
#endif
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

inline StreamStatus wait_for(int descriptor, short events, ConnectionClock::time_point deadline) {
    for (;;) {
        const auto left = deadline - ConnectionClock::now();
        if (left <= ConnectionClock::duration::zero()) return StreamStatus::TimedOut;
        const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(left).count();
        pollfd entry{descriptor, events, 0};
        const int timeout = milliseconds > INT_MAX ? INT_MAX : static_cast<int>(milliseconds);
        const int ready = ::poll(&entry, 1, timeout);
        // A hangup or error goes back through recv/send to report the outcome.
        if (ready > 0) return (entry.revents & POLLNVAL) ? StreamStatus::Failed : StreamStatus::Ready;
        if (ready < 0 && errno != EINTR) return StreamStatus::Failed;
    }
}

} // namespace neura::server::http::socket_io
