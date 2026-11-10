#include "provider_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neura::nvm::provider {

#ifdef _WIN32

void add_net_services(const Adder& add, StandardServices::Tables&, const Capabilities& capabilities) {
    const auto unsupported = [&capabilities](const HostRequest&) {
        return capabilities.net ? fail(host_error::unsupported) : denied();
    };
    add(signature("neura.net.connect", {text(), int32(), int64()}, int64()), unsupported);
    add(signature("neura.net.listen", {text(), int32()}, int64()), unsupported);
    add(signature("neura.net.local_port", {int64()}, int32()), unsupported);
    add(signature("neura.net.accept", {int64()}, int64()), unsupported);
    add(signature("neura.net.read", {int64(), int32(), int64()}, byte_list()), unsupported);
    add(signature("neura.net.write", {int64(), byte_list()}, int32()), unsupported);
    add(signature("neura.net.close", {int64()}, void_type()), unsupported);
}

void close_sockets(StandardServices::Tables& tables) {
    tables.sockets.clear();
    tables.next_socket = 1;
}

#else

namespace {

HostCompletion errno_failure() { return fail(host_error::from_errno(errno)); }

// getaddrinfo failures: an unknown name is NotFound.
std::uint32_t resolution_error(int code) {
    switch (code) {
    case EAI_NONAME:
#ifdef EAI_NODATA
#if EAI_NODATA != EAI_NONAME
    case EAI_NODATA:
#endif
#endif
        return host_error::not_found;
    case EAI_AGAIN:
        return host_error::timed_out;
    case EAI_SYSTEM:
        return host_error::from_errno(errno);
    default:
        return host_error::other;
    }
}

struct Addresses {
    addrinfo* list = nullptr;
    ~Addresses() {
        if (list != nullptr) ::freeaddrinfo(list);
    }
};

bool set_blocking(int descriptor, bool blocking) {
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(descriptor, F_SETFL, blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) == 0;
}

int new_socket(int family) {
    const int descriptor = ::socket(family, SOCK_STREAM, 0);
    if (descriptor >= 0) ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    if (descriptor >= 0) {
        int one = 1;
        ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    }
#endif
    return descriptor;
}

// Waits for `events` on `descriptor`. Returns 1 when ready, 0 on timeout,
// and -1 with errno on failure. A deadline of zero waits without limit.
int wait_for(int descriptor, short events, std::chrono::steady_clock::time_point deadline, bool limited) {
    for (;;) {
        int milliseconds = -1;
        if (limited) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0) return 0;
            milliseconds = timeout_milliseconds(left);
        }
        pollfd entry{descriptor, events, 0};
        const int ready = ::poll(&entry, 1, milliseconds);
        if (ready >= 0 || errno != EINTR) return ready;
    }
}

// Connects one socket to `address`, honoring the deadline. Returns the
// descriptor, or -1 with `error` set to an error code.
int connect_one(const addrinfo& address, std::chrono::steady_clock::time_point deadline, bool limited,
                std::uint32_t& error) {
    const int descriptor = new_socket(address.ai_family);
    if (descriptor < 0) {
        error = host_error::from_errno(errno);
        return -1;
    }
    if (!set_blocking(descriptor, false)) {
        error = host_error::from_errno(errno);
        ::close(descriptor);
        return -1;
    }
    if (::connect(descriptor, address.ai_addr, address.ai_addrlen) != 0) {
        if (errno != EINPROGRESS && errno != EINTR) {
            error = host_error::from_errno(errno);
            ::close(descriptor);
            return -1;
        }
        const int ready = wait_for(descriptor, POLLOUT, deadline, limited);
        if (ready <= 0) {
            error = ready == 0 ? host_error::timed_out : host_error::from_errno(errno);
            ::close(descriptor);
            return -1;
        }
        int failure = 0;
        socklen_t length = sizeof failure;
        if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &failure, &length) != 0 || failure != 0) {
            error = host_error::from_errno(failure != 0 ? failure : errno);
            ::close(descriptor);
            return -1;
        }
    }
    if (!set_blocking(descriptor, true)) {
        error = host_error::from_errno(errno);
        ::close(descriptor);
        return -1;
    }
    return descriptor;
}

bool valid_port(std::int32_t port) { return port >= 0 && port <= 65535; }

} // namespace

void add_net_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities) {
    auto* sockets = &tables;
    const auto* granted = &capabilities;
    const auto lookup = [sockets](const HostValue& id, bool listener) -> StandardServices::Tables::Socket* {
        const auto found = sockets->sockets.find(as_int64(id));
        if (found == sockets->sockets.end() || found->second.listener != listener) return nullptr;
        return &found->second;
    };
    const auto remember = [sockets](int descriptor, bool listener) {
        const auto id = sockets->next_socket++;
        sockets->sockets[id] = {descriptor, listener};
        return HostCompletion::ok(int64_value(id));
    };

    add(signature("neura.net.connect", {text(), int32(), int64()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!usable_text(host) || !valid_port(port) || timeout < 0) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        const bool limited = timeout > 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(limited ? timeout : 0);
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        Addresses addresses;
        const auto service = std::to_string(port);
        if (const int code = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses.list); code != 0) {
            return fail(resolution_error(code));
        }
        std::uint32_t error = host_error::not_found;
        for (const auto* address = addresses.list; address != nullptr; address = address->ai_next) {
            const int descriptor = connect_one(*address, deadline, limited, error);
            if (descriptor >= 0) return remember(descriptor, false);
            if (error == host_error::timed_out) break;
        }
        return fail(error);
    });

    add(signature("neura.net.listen", {text(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        if (host.find('\0') != std::string::npos || !valid_port(port)) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;
        Addresses addresses;
        const auto service = std::to_string(port);
        if (const int code = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints,
                                           &addresses.list);
            code != 0) {
            return fail(resolution_error(code));
        }
        std::uint32_t error = host_error::not_found;
        for (const auto* address = addresses.list; address != nullptr; address = address->ai_next) {
            const int descriptor = new_socket(address->ai_family);
            if (descriptor < 0) {
                error = host_error::from_errno(errno);
                continue;
            }
            int one = 1;
            ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (::bind(descriptor, address->ai_addr, address->ai_addrlen) == 0 && ::listen(descriptor, 128) == 0) {
                return remember(descriptor, true);
            }
            error = host_error::from_errno(errno);
            ::close(descriptor);
        }
        return fail(error);
    });

    add(signature("neura.net.local_port", {int64()}, int32()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto found = sockets->sockets.find(as_int64(request.arguments.at(0)));
        if (found == sockets->sockets.end()) return fail(host_error::invalid_input);
        sockaddr_storage address{};
        socklen_t length = sizeof address;
        if (::getsockname(found->second.descriptor, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return errno_failure();
        }
        std::uint16_t port = 0;
        if (address.ss_family == AF_INET) {
            port = ntohs(reinterpret_cast<const sockaddr_in&>(address).sin_port);
        } else if (address.ss_family == AF_INET6) {
            port = ntohs(reinterpret_cast<const sockaddr_in6&>(address).sin6_port);
        } else {
            return fail(host_error::unsupported);
        }
        return HostCompletion::ok(int32_value(port));
    });

    add(signature("neura.net.accept", {int64()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* listener = lookup(request.arguments.at(0), true);
        if (listener == nullptr) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        for (;;) {
            const int descriptor = ::accept(listener->descriptor, nullptr, nullptr);
            if (descriptor >= 0) {
                ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
                int one = 1;
                ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                return remember(descriptor, false);
            }
            if (errno != EINTR && errno != ECONNABORTED) return errno_failure();
        }
    });

    add(signature("neura.net.read", {int64(), int32(), int64()}, byte_list()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = lookup(request.arguments.at(0), false);
        const auto max = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (stream == nullptr || max < 1 || timeout < 0) return fail(host_error::invalid_input);
        if (timeout > 0) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeout);
            const int ready = wait_for(stream->descriptor, POLLIN, deadline, true);
            if (ready == 0) return fail(host_error::timed_out);
            if (ready < 0) return errno_failure();
        }
        std::string buffer(std::min(static_cast<std::size_t>(max), max_read_bytes), '\0');
        for (;;) {
            const auto count = ::recv(stream->descriptor, buffer.data(), buffer.size(), 0);
            if (count >= 0) {
                buffer.resize(static_cast<std::size_t>(count));
                return HostCompletion::ok(bytes_value(std::move(buffer)));
            }
            if (errno != EINTR) return errno_failure();
        }
    });

    add(signature("neura.net.write", {int64(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = lookup(request.arguments.at(0), false);
        const auto& bytes = request.arguments.at(1).bytes;
        if (stream == nullptr) return fail(host_error::invalid_input);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = std::min<std::size_t>(bytes.size(), INT32_MAX);
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        for (;;) {
            const auto count = ::send(stream->descriptor, bytes.data(), size, flags);
            if (count >= 0) return HostCompletion::ok(int32_value(static_cast<std::int32_t>(count)));
            if (errno != EINTR) return errno_failure();
        }
    });

    add(signature("neura.net.close", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto found = sockets->sockets.find(as_int64(request.arguments.at(0)));
        if (found == sockets->sockets.end()) return fail(host_error::invalid_input);
        const int descriptor = found->second.descriptor;
        sockets->sockets.erase(found);
        if (::close(descriptor) != 0 && errno != EINTR) return errno_failure();
        return HostCompletion::ok();
    });
}

void close_sockets(StandardServices::Tables& tables) {
    for (const auto& [id, socket] : tables.sockets) ::close(socket.descriptor);
    tables.sockets.clear();
    tables.next_socket = 1;
}

#endif

} // namespace neura::nvm::provider
