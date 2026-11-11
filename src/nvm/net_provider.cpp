#include "provider_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

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

void add_net_services(const Adder& add, const OverlapAdder&, StandardServices::Tables&,
                      const Capabilities& capabilities) {
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

#ifndef NEURA_NUMERIC_ADDRESSES_ONLY
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
#endif

#ifdef NEURA_NUMERIC_ADDRESSES_ONLY
// The fully static launcher does not use the C library's resolver: in a
// static program getaddrinfo loads, at run time, the name-service libraries
// of the C library it was linked with, which another machine may not have.
// It accepts numeric IPv4 and IPv6 addresses and "localhost", and the empty
// host of a listener; any other name is Unsupported.
struct NumericAddress {
    addrinfo info{};
    sockaddr_storage storage{};
};

struct Addresses {
    addrinfo* list = nullptr;
    ~Addresses() {
        while (list != nullptr) {
            auto* next = list->ai_next;
            // `info` is the first member of the node that holds it.
            delete reinterpret_cast<NumericAddress*>(list);
            list = next;
        }
    }
};

// The error of a failed resolution, or nothing when `out` holds addresses.
std::optional<std::uint32_t> resolve(const std::string& host, std::int32_t port, bool passive, Addresses& out) {
    std::vector<const char*> candidates;
    if (host.empty() && passive) {
        candidates = {"0.0.0.0", "::"};
    } else if (host == "localhost") {
        candidates = {"127.0.0.1", "::1"};
    } else {
        candidates = {host.c_str()};
    }
    addrinfo** tail = &out.list;
    for (const char* text : candidates) {
        auto node = std::make_unique<NumericAddress>();
        auto* v4 = reinterpret_cast<sockaddr_in*>(&node->storage);
        auto* v6 = reinterpret_cast<sockaddr_in6*>(&node->storage);
        if (::inet_pton(AF_INET, text, &v4->sin_addr) == 1) {
            v4->sin_family = AF_INET;
            v4->sin_port = htons(static_cast<std::uint16_t>(port));
            node->info.ai_family = AF_INET;
            node->info.ai_addrlen = sizeof(sockaddr_in);
        } else if (::inet_pton(AF_INET6, text, &v6->sin6_addr) == 1) {
            v6->sin6_family = AF_INET6;
            v6->sin6_port = htons(static_cast<std::uint16_t>(port));
            node->info.ai_family = AF_INET6;
            node->info.ai_addrlen = sizeof(sockaddr_in6);
        } else {
            return host_error::unsupported;
        }
        node->info.ai_socktype = SOCK_STREAM;
        node->info.ai_addr = reinterpret_cast<sockaddr*>(&node->storage);
        *tail = &node.release()->info;
        tail = &(*tail)->ai_next;
    }
    return std::nullopt;
}
#else
struct Addresses {
    addrinfo* list = nullptr;
    ~Addresses() {
        if (list != nullptr) ::freeaddrinfo(list);
    }
};

// The error of a failed resolution, or nothing when `out` holds addresses.
std::optional<std::uint32_t> resolve(const std::string& host, std::int32_t port, bool passive, Addresses& out) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (passive) hints.ai_flags = AI_PASSIVE;
    const auto service = std::to_string(port);
    const char* node = passive && host.empty() ? nullptr : host.c_str();
    if (const int code = ::getaddrinfo(node, service.c_str(), &hints, &out.list); code != 0) {
        return resolution_error(code);
    }
    return std::nullopt;
}
#endif

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

// Closes a descriptor when the last owner is destroyed. A pending operation
// holds one, so the operation releases it however it ends.
struct Descriptor {
    explicit Descriptor(int descriptor) : fd(descriptor) {}
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    ~Descriptor() {
        if (fd >= 0) ::close(fd);
    }
    int fd;
};
using OwnedDescriptor = std::shared_ptr<Descriptor>;

// A second reference to the socket, so that a pending operation keeps a valid
// descriptor even when the program closes the socket meanwhile.
OwnedDescriptor duplicate(int descriptor) {
    const int copy = ::fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    return copy < 0 ? nullptr : std::make_shared<Descriptor>(copy);
}

// Whether `events` are ready on `descriptor`, without waiting. Hang-up and
// error count as ready: the next call reports them.
bool ready_now(int descriptor, short events) {
    pollfd entry{descriptor, events, 0};
    for (;;) {
        const int ready = ::poll(&entry, 1, 0);
        if (ready >= 0) return ready > 0;
        if (errno != EINTR) return true;
    }
}

// A connection in progress, trying the addresses of the name in order.
struct Connecting {
    Addresses addresses;
    const addrinfo* next = nullptr;
    OwnedDescriptor socket;
    std::uint32_t error = host_error::not_found;
};

enum class Started { Connected, Waiting, Failed };

// Starts connections to the remaining addresses until one has connected or
// is in progress. Failed when none is left, with the last error in `error`.
Started start_next(Connecting& connecting) {
    while (connecting.next != nullptr) {
        const auto& address = *connecting.next;
        connecting.next = connecting.next->ai_next;
        const int descriptor = new_socket(address.ai_family);
        if (descriptor < 0) {
            connecting.error = host_error::from_errno(errno);
            continue;
        }
        if (!set_blocking(descriptor, false)) {
            connecting.error = host_error::from_errno(errno);
            ::close(descriptor);
            continue;
        }
        const bool connected = ::connect(descriptor, address.ai_addr, address.ai_addrlen) == 0;
        if (connected || errno == EINPROGRESS || errno == EINTR) {
            connecting.socket = std::make_shared<Descriptor>(descriptor);
            return connected ? Started::Connected : Started::Waiting;
        }
        connecting.error = host_error::from_errno(errno);
        ::close(descriptor);
    }
    return Started::Failed;
}


} // namespace

void add_net_services(const Adder& add, const OverlapAdder& overlap, StandardServices::Tables& tables,
                      const Capabilities& capabilities) {
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

    const auto connect_now_signature = signature("neura.net.connect", {text(), int32(), int64()}, int64());
    const FunctionService::Handler connect_now = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!usable_text(host) || !valid_port(port) || timeout < 0) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        const bool limited = timeout > 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(limited ? timeout : 0);
        Addresses addresses;
        if (const auto failure = resolve(host, port, false, addresses)) return fail(*failure);
        std::uint32_t error = host_error::not_found;
        for (const auto* address = addresses.list; address != nullptr; address = address->ai_next) {
            const int descriptor = connect_one(*address, deadline, limited, error);
            if (descriptor >= 0) return remember(descriptor, false);
            if (error == host_error::timed_out) break;
        }
        return fail(error);
    };

    add(signature("neura.net.listen", {text(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        if (host.find('\0') != std::string::npos || !valid_port(port)) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        Addresses addresses;
        if (const auto failure = resolve(host, port, true, addresses)) return fail(*failure);
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

    const auto accept_now_signature = signature("neura.net.accept", {int64()}, int64());
    const FunctionService::Handler accept_now = [=](const HostRequest& request) {
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
    };

    const auto read_now_signature = signature("neura.net.read", {int64(), int32(), int64()}, byte_list());
    const FunctionService::Handler read_now = [=](const HostRequest& request) {
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
    };

    const auto write_now_signature = signature("neura.net.write", {int64(), byte_list()}, int32());
    const FunctionService::Handler write_now = [=](const HostRequest& request) {
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
    };

    // The overlapping forms (Concurrency.md section 10.4). Each starts its
    // operation without blocking and returns its completion when it finished
    // at once. The sockets stay in blocking mode: a pending operation waits
    // for readiness and then makes one non-blocking call. Name resolution
    // still blocks the whole run for its duration.
    const auto begin_connect = [=](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!usable_text(host) || !valid_port(port) || timeout < 0) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        auto connecting = std::make_shared<Connecting>();
        if (const auto failure = resolve(host, port, false, connecting->addresses)) return fail(*failure);
        connecting->next = connecting->addresses.list;
        // The completion once a connection attempt has settled, or nothing
        // while one is in progress.
        const auto advance = [=](bool cancelled) -> std::optional<HostCompletion> {
            for (;;) {
                if (connecting->socket) {
                    if (!ready_now(connecting->socket->fd, POLLOUT)) {
                        if (cancelled) {
                            connecting->socket.reset();
                            return cancelled_answer();
                        }
                        return std::nullopt;
                    }
                    int failure = 0;
                    socklen_t length = sizeof failure;
                    if (::getsockopt(connecting->socket->fd, SOL_SOCKET, SO_ERROR, &failure, &length) != 0 ||
                        failure != 0) {
                        connecting->error = host_error::from_errno(failure != 0 ? failure : errno);
                        connecting->socket.reset();
                    } else {
                        const int descriptor = std::exchange(connecting->socket->fd, -1);
                        connecting->socket.reset();
                        if (!set_blocking(descriptor, true)) {
                            const auto error = host_error::from_errno(errno);
                            ::close(descriptor);
                            return fail(error);
                        }
                        if (sockets->sockets.size() >= max_open_ids) {
                            ::close(descriptor);
                            return fail(host_error::other);
                        }
                        return remember(descriptor, false);
                    }
                }
                switch (start_next(*connecting)) {
                case Started::Connected:
                    break;
                case Started::Waiting:
                    if (cancelled) {
                        connecting->socket.reset();
                        return cancelled_answer();
                    }
                    return std::nullopt;
                case Started::Failed:
                    return fail(connecting->error);
                }
            }
        };
        if (auto done = advance(false)) return *done;
        PendingOperation operation;
        operation.descriptor = connecting->socket->fd;
        operation.writable = true;
        operation.current_descriptor = [connecting] { return connecting->socket ? connecting->socket->fd : -1; };
        operation.attempt = advance;
        if (timeout > 0) {
            operation.deadline = deadline_after(request.now, timeout);
            operation.expire = [connecting] {
                connecting->socket.reset();
                return fail(host_error::timed_out);
            };
        }
        return operation;
    };

    const auto begin_accept = [=](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        auto* listener = lookup(request.arguments.at(0), true);
        if (listener == nullptr) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        const auto held = duplicate(listener->descriptor);
        if (!held) return errno_failure();
        const auto accept_once = [=](bool cancelled) -> std::optional<HostCompletion> {
            const auto found = sockets->sockets.find(id);
            // The program closed the listener while the request was pending.
            if (found == sockets->sockets.end() || !found->second.listener) return fail(host_error::invalid_input);
            if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
            if (ready_now(held->fd, POLLIN)) {
                // The listener's description is shared with the program's own
                // descriptor: make it non-blocking only for this call.
                set_blocking(held->fd, false);
                int descriptor = -1;
                do {
                    descriptor = ::accept(held->fd, nullptr, nullptr);
                } while (descriptor < 0 && errno == EINTR);
                const int failure = errno;
                set_blocking(held->fd, true);
                if (descriptor >= 0) {
                    ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
                    int one = 1;
                    ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                    set_blocking(descriptor, true);
                    return remember(descriptor, false);
                }
                if (failure != EAGAIN && failure != EWOULDBLOCK && failure != ECONNABORTED) {
                    return fail(host_error::from_errno(failure));
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = accept_once(false)) return *done;
        PendingOperation operation;
        operation.descriptor = held->fd;
        operation.readable = true;
        operation.attempt = accept_once;
        return operation;
    };

    const auto begin_read = [=](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        auto* stream = lookup(request.arguments.at(0), false);
        const auto max = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (stream == nullptr || max < 1 || timeout < 0) return fail(host_error::invalid_input);
        const auto held = duplicate(stream->descriptor);
        if (!held) return errno_failure();
        const auto count = std::min(static_cast<std::size_t>(max), max_read_bytes);
        const auto read_once = [=](bool cancelled) -> std::optional<HostCompletion> {
            const auto found = sockets->sockets.find(id);
            if (found == sockets->sockets.end() || found->second.listener) return fail(host_error::invalid_input);
            if (ready_now(held->fd, POLLIN)) {
                std::string buffer(count, '\0');
                for (;;) {
                    const auto received = ::recv(held->fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
                    if (received >= 0) {
                        buffer.resize(static_cast<std::size_t>(received));
                        return HostCompletion::ok(bytes_value(std::move(buffer)));
                    }
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    return errno_failure();
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = read_once(false)) return *done;
        PendingOperation operation;
        operation.descriptor = held->fd;
        operation.readable = true;
        operation.attempt = read_once;
        if (timeout > 0) {
            operation.deadline = deadline_after(request.now, timeout);
            operation.expire = [] { return fail(host_error::timed_out); };
        }
        return operation;
    };

    const auto begin_write = [=](const HostRequest& request) -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        auto* stream = lookup(request.arguments.at(0), false);
        const auto& bytes = request.arguments.at(1).bytes;
        if (stream == nullptr) return fail(host_error::invalid_input);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto held = duplicate(stream->descriptor);
        if (!held) return errno_failure();
        const auto data = std::make_shared<const std::string>(bytes);
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL | MSG_DONTWAIT;
#else
        constexpr int flags = MSG_DONTWAIT;
#endif
        // Writes what the socket takes now. A write that stores some bytes
        // completes with that count; one that can store none waits, and a
        // cancel then answers Cancelled with nothing written.
        const auto write_once = [=](bool cancelled) -> std::optional<HostCompletion> {
            const auto found = sockets->sockets.find(id);
            if (found == sockets->sockets.end() || found->second.listener) return fail(host_error::invalid_input);
            if (ready_now(held->fd, POLLOUT)) {
                const auto size = std::min<std::size_t>(data->size(), INT32_MAX);
                for (;;) {
                    const auto sent = ::send(held->fd, data->data(), size, flags);
                    if (sent >= 0) return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    return errno_failure();
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = write_once(false)) return *done;
        PendingOperation operation;
        operation.descriptor = held->fd;
        operation.writable = true;
        operation.attempt = write_once;
        return operation;
    };

    overlap(connect_now_signature, connect_now, begin_connect);
    overlap(accept_now_signature, accept_now, begin_accept);
    overlap(read_now_signature, read_now, begin_read);
    overlap(write_now_signature, write_now, begin_write);

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
