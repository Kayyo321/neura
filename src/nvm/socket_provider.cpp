// Socket services beyond TCP streams (docs/runtime/NVM.md, "Socket services"):
// UDP, Unix-domain stream and datagram sockets, socket options, name
// resolution through the system resolver, and sending a file to a socket.
// They share the socket ID table of neura.net.*, so neura.net.read, write,
// accept, local_port and close work on the streams and listeners they open.
// None of them overlaps: in an image with tasks a waiting call holds up the
// other tasks for at most its timeout.

#include "provider_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <linux/udp.h>
#include <sys/sendfile.h>
#endif
#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

namespace neura::nvm::provider {

namespace {

HostType address_result() { return tuple({byte_list(), text(), int32()}); }
HostType segments_result() { return tuple({byte_list(), text(), int32(), int32()}); }

} // namespace

#ifdef _WIN32

void add_socket_services(const Adder& add, StandardServices::Tables&, const Capabilities& capabilities) {
    const auto unsupported = [&capabilities](const HostRequest&) {
        return capabilities.net ? fail(host_error::unsupported) : denied();
    };
    add(signature("neura.net.udp.bind", {text(), int32()}, int64()), unsupported);
    add(signature("neura.net.udp.send_to", {int64(), text(), int32(), byte_list()}, int32()), unsupported);
    add(signature("neura.net.udp.receive_from", {int64(), int32(), int64()}, address_result()), unsupported);
    add(signature("neura.net.unix.connect", {text()}, int64()), unsupported);
    add(signature("neura.net.unix.listen", {text()}, int64()), unsupported);
    add(signature("neura.net.unix.datagram", {text()}, int64()), unsupported);
    add(signature("neura.net.unix.send_to", {int64(), text(), byte_list()}, int32()), unsupported);
    add(signature("neura.net.unix.receive_from", {int64(), int32(), int64()}, address_result()), unsupported);
    add(signature("neura.net.option.set", {int64(), int32(), int64()}, void_type()), unsupported);
    add(signature("neura.net.option.get", {int64(), int32()}, int64()), unsupported);
    add(signature("neura.net.resolve", {text(), int32()}, text_list()), unsupported);
    add(signature("neura.net.send_file", {int64(), int64(), int64(), int64()}, int64()), unsupported);
    add(signature("neura.net.udp.receive_segments", {int64(), int32(), int64()}, segments_result()), unsupported);
    add(signature("neura.net.mptcp.listen", {text(), int32()}, int64()), unsupported);
    add(signature("neura.net.mptcp.connect", {text(), int32(), int64()}, int64()), unsupported);
    add(signature("neura.net.pipe", {}, tuple({int64(), int64()})), unsupported);
    add(signature("neura.net.splice", {int64(), int64(), int64()}, int64()), unsupported);
}

#else

namespace {

HostCompletion errno_failure() { return fail(host_error::from_errno(errno)); }

bool valid_port(std::int32_t port) { return port >= 0 && port <= 65535; }

int new_socket(int family, int type) {
    const int descriptor = ::socket(family, type, 0);
    if (descriptor < 0) return descriptor;
    ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    return descriptor;
}

// A numeric IPv4 or IPv6 address, "localhost", or the empty host (any
// address, for binding). Names other than localhost go through
// neura.net.resolve first, so the datagram services never block on DNS.
bool numeric_address(const std::string& host, std::int32_t port, sockaddr_storage& out, socklen_t& length) {
    std::memset(&out, 0, sizeof out);
    const std::string text = host.empty() ? "0.0.0.0" : host == "localhost" ? "127.0.0.1" : host;
    auto* v4 = reinterpret_cast<sockaddr_in*>(&out);
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&out);
    if (::inet_pton(AF_INET, text.c_str(), &v4->sin_addr) == 1) {
        v4->sin_family = AF_INET;
        v4->sin_port = htons(static_cast<std::uint16_t>(port));
        length = sizeof(sockaddr_in);
        return true;
    }
    if (::inet_pton(AF_INET6, text.c_str(), &v6->sin6_addr) == 1) {
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(static_cast<std::uint16_t>(port));
        length = sizeof(sockaddr_in6);
        return true;
    }
    return false;
}

// A Unix-domain address. On Linux a path that starts with '@' names the
// abstract namespace; the '@' becomes the leading NUL byte.
bool unix_address(const std::string& path, sockaddr_un& out, socklen_t& length) {
    std::memset(&out, 0, sizeof out);
    out.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof out.sun_path) return false;
#ifdef __linux__
    if (path.front() == '@') {
        if (path.find('\0') != std::string::npos) return false;
        std::memcpy(out.sun_path + 1, path.data() + 1, path.size() - 1);
        length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
        return true;
    }
#endif
    if (path.find('\0') != std::string::npos || path.front() == '@') return false;
    std::memcpy(out.sun_path, path.data(), path.size());
    length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    return true;
}

// Why unix_address refused a path: an abstract name off Linux is
// Unsupported (Std.Net.Core.UnixAddr), anything else InvalidInput.
HostCompletion unix_address_failure(const std::string& path) {
#ifndef __linux__
    if (!path.empty() && path.front() == '@' && path.size() < sizeof(sockaddr_un::sun_path) &&
        path.find('\0') == std::string::npos) {
        return fail(host_error::unsupported);
    }
#endif
    (void)path;
    return fail(host_error::invalid_input);
}

std::string unix_path(const sockaddr_un& address, socklen_t length) {
    const auto offset = offsetof(sockaddr_un, sun_path);
    if (length <= offset) return {};
    const auto size = static_cast<std::size_t>(length) - offset;
#ifdef __linux__
    if (address.sun_path[0] == '\0') return "@" + std::string(address.sun_path + 1, size - 1);
#else
    // No abstract namespace: a path that starts with NUL is an unnamed
    // sender, which BSD systems report with a zeroed address.
    if (address.sun_path[0] == '\0') return {};
#endif
    return std::string(address.sun_path, strnlen(address.sun_path, size));
}

// Waits for input: 1 ready, 0 timed out, -1 failed. Zero waits without limit.
int wait_readable(int descriptor, std::int64_t timeout_nanos) {
    const bool limited = timeout_nanos > 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(limited ? timeout_nanos : 0);
    for (;;) {
        int milliseconds = -1;
        if (limited) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return 0;
            milliseconds = timeout_milliseconds(left);
        }
        pollfd entry{descriptor, POLLIN, 0};
        const int ready = ::poll(&entry, 1, milliseconds);
        if (ready >= 0 || errno != EINTR) return ready;
    }
}

struct OptionSpec {
    int level;
    int name;
};

// The option numbers of neura.net.option.set and get.
std::optional<OptionSpec> option_of(std::int32_t option) {
    switch (option) {
    case 1: return OptionSpec{SOL_SOCKET, SO_REUSEADDR};
#ifdef SO_REUSEPORT
    case 2: return OptionSpec{SOL_SOCKET, SO_REUSEPORT};
#endif
    case 3: return OptionSpec{IPPROTO_TCP, TCP_NODELAY};
#ifdef TCP_FASTOPEN
    case 4: return OptionSpec{IPPROTO_TCP, TCP_FASTOPEN};
#endif
#ifdef TCP_USER_TIMEOUT
    case 5: return OptionSpec{IPPROTO_TCP, TCP_USER_TIMEOUT};
#endif
    case 6: return OptionSpec{SOL_SOCKET, SO_KEEPALIVE};
#ifdef TCP_KEEPIDLE
    case 7: return OptionSpec{IPPROTO_TCP, TCP_KEEPIDLE};
#elif defined(TCP_KEEPALIVE)
    case 7: return OptionSpec{IPPROTO_TCP, TCP_KEEPALIVE};
#endif
#ifdef TCP_KEEPINTVL
    case 8: return OptionSpec{IPPROTO_TCP, TCP_KEEPINTVL};
#endif
#ifdef TCP_KEEPCNT
    case 9: return OptionSpec{IPPROTO_TCP, TCP_KEEPCNT};
#endif
    case 10: return OptionSpec{SOL_SOCKET, SO_RCVBUF};
    case 11: return OptionSpec{SOL_SOCKET, SO_SNDBUF};
    case 12: return OptionSpec{SOL_SOCKET, SO_BROADCAST};
#ifdef UDP_SEGMENT
    // UDP generic segmentation: a send of more than this many bytes leaves
    // as datagrams of this size (0 turns it off).
    case 13: return OptionSpec{IPPROTO_UDP, UDP_SEGMENT};
#endif
#ifdef UDP_GRO
    // UDP receive coalescing: neura.net.udp.receive_segments may return
    // several datagrams of one size at once.
    case 14: return OptionSpec{IPPROTO_UDP, UDP_GRO};
#endif
#ifdef SO_PROTOCOL
    // The socket's protocol number (6 TCP, 17 UDP, 262 MPTCP); read only.
    case 15: return OptionSpec{SOL_SOCKET, SO_PROTOCOL};
#endif
    default: return std::nullopt;
    }
}

bool known_option(std::int32_t option) { return option >= 1 && option <= 15; }
bool read_only_option(std::int32_t option) { return option == 15; }

// Waits until `descriptor` has `events` or the deadline passes: 1 ready, 0
// timed out, -1 failed. A zero timeout waits without limit.
int wait_for_events(int descriptor, short events, std::int64_t timeout_nanos) {
    const bool limited = timeout_nanos > 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(limited ? timeout_nanos : 0);
    for (;;) {
        int milliseconds = -1;
        if (limited) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return 0;
            milliseconds = timeout_milliseconds(left);
        }
        pollfd entry{descriptor, events, 0};
        const int ready = ::poll(&entry, 1, milliseconds);
        if (ready >= 0 || errno != EINTR) return ready;
    }
}

} // namespace

void add_socket_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities) {
    auto* sockets = &tables;
    const auto* granted = &capabilities;
    const auto remember = [sockets](int descriptor, bool listener, bool datagram) {
        const auto id = sockets->next_socket++;
        sockets->sockets[id] = {descriptor, listener, datagram};
        return HostCompletion::ok(int64_value(id));
    };
    const auto find = [sockets](const HostValue& id) -> StandardServices::Tables::Socket* {
        const auto found = sockets->sockets.find(as_int64(id));
        return found == sockets->sockets.end() ? nullptr : &found->second;
    };
    const auto full = [sockets] { return sockets->sockets.size() >= max_open_ids; };

    // Datagram reception shared by UDP and Unix datagram sockets: the bytes,
    // the sender's address text, and its port (0 for a Unix sender).
    const auto receive_from = [=](const HostRequest& request, bool unix_socket) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        const auto max = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!socket || !socket->datagram || max < 1 || timeout < 0) return fail(host_error::invalid_input);
        const int ready = wait_readable(socket->descriptor, timeout);
        if (ready == 0) return fail(host_error::timed_out);
        if (ready < 0) return errno_failure();
        std::string bytes(std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes), '\0');
        sockaddr_storage from{};
        socklen_t length = sizeof from;
        ssize_t got;
        do {
            got = ::recvfrom(socket->descriptor, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&from),
                             &length);
        } while (got < 0 && errno == EINTR);
        if (got < 0) return errno_failure();
        bytes.resize(static_cast<std::size_t>(got));
        std::string address;
        std::int32_t port = 0;
        if (unix_socket) {
            address = unix_path(reinterpret_cast<const sockaddr_un&>(from), length);
        } else {
            char host[NI_MAXHOST] = {};
            char service[NI_MAXSERV] = {};
            if (::getnameinfo(reinterpret_cast<const sockaddr*>(&from), length, host, sizeof host, service,
                              sizeof service, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                address = host;
                port = std::atoi(service);
            }
        }
        return HostCompletion::ok(tuple_value({bytes_value(std::move(bytes)), bytes_value(std::move(address)),
                                               int32_value(port)}));
    };

    add(signature("neura.net.udp.bind", {text(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        sockaddr_storage address{};
        socklen_t length = 0;
        if (!valid_port(port) || !numeric_address(host, port, address, length)) return fail(host_error::invalid_input);
        if (full()) return fail(host_error::other);
        const int descriptor = new_socket(address.ss_family, SOCK_DGRAM);
        if (descriptor < 0) return errno_failure();
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, false, true);
    });

    add(signature("neura.net.udp.send_to", {int64(), text(), int32(), byte_list()}, int32()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto& host = request.arguments.at(1).bytes;
            const auto port = as_int32(request.arguments.at(2));
            const auto& bytes = request.arguments.at(3).bytes;
            sockaddr_storage address{};
            socklen_t length = 0;
            if (!socket || !socket->datagram || !valid_port(port) || !numeric_address(host, port, address, length)) {
                return fail(host_error::invalid_input);
            }
            ssize_t sent;
            do {
                sent = ::sendto(socket->descriptor, bytes.data(), bytes.size(), 0,
                                reinterpret_cast<sockaddr*>(&address), length);
            } while (sent < 0 && errno == EINTR);
            if (sent < 0) return errno_failure();
            return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
        });

    add(signature("neura.net.udp.receive_from", {int64(), int32(), int64()}, address_result()),
        [=](const HostRequest& request) { return receive_from(request, false); });

    add(signature("neura.net.unix.connect", {text()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        sockaddr_un address{};
        socklen_t length = 0;
        if (!unix_address(request.arguments.at(0).bytes, address, length)) {
            return unix_address_failure(request.arguments.at(0).bytes);
        }
        if (full()) return fail(host_error::other);
        const int descriptor = new_socket(AF_UNIX, SOCK_STREAM);
        if (descriptor < 0) return errno_failure();
        int result;
        do {
            result = ::connect(descriptor, reinterpret_cast<sockaddr*>(&address), length);
        } while (result != 0 && errno == EINTR);
        if (result != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, false, false);
    });

    add(signature("neura.net.unix.listen", {text()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        sockaddr_un address{};
        socklen_t length = 0;
        if (!unix_address(request.arguments.at(0).bytes, address, length)) {
            return unix_address_failure(request.arguments.at(0).bytes);
        }
        if (full()) return fail(host_error::other);
        const int descriptor = new_socket(AF_UNIX, SOCK_STREAM);
        if (descriptor < 0) return errno_failure();
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::listen(descriptor, 128) != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, true, false);
    });

    add(signature("neura.net.unix.datagram", {text()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& path = request.arguments.at(0).bytes;
        sockaddr_un address{};
        socklen_t length = 0;
        if (!path.empty() && !unix_address(path, address, length)) return unix_address_failure(path);
        if (full()) return fail(host_error::other);
        const int descriptor = new_socket(AF_UNIX, SOCK_DGRAM);
        if (descriptor < 0) return errno_failure();
        if (!path.empty() && ::bind(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, false, true);
    });

    add(signature("neura.net.unix.send_to", {int64(), text(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        sockaddr_un address{};
        socklen_t length = 0;
        if (!socket || !socket->datagram) return fail(host_error::invalid_input);
        if (!unix_address(request.arguments.at(1).bytes, address, length)) {
            return unix_address_failure(request.arguments.at(1).bytes);
        }
        const auto& bytes = request.arguments.at(2).bytes;
        ssize_t sent;
        do {
            sent = ::sendto(socket->descriptor, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&address),
                            length);
        } while (sent < 0 && errno == EINTR);
        if (sent < 0) return errno_failure();
        return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
    });

    add(signature("neura.net.unix.receive_from", {int64(), int32(), int64()}, address_result()),
        [=](const HostRequest& request) { return receive_from(request, true); });

    add(signature("neura.net.option.set", {int64(), int32(), int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        const auto option = as_int32(request.arguments.at(1));
        const auto value = as_int64(request.arguments.at(2));
        if (!socket || !known_option(option) || read_only_option(option) || value < 0 || value > INT32_MAX) {
            return fail(host_error::invalid_input);
        }
        const auto spec = option_of(option);
        if (!spec) return fail(host_error::unsupported);
        const int number = static_cast<int>(value);
        if (::setsockopt(socket->descriptor, spec->level, spec->name, &number, sizeof number) != 0) {
            return errno == ENOPROTOOPT || errno == EOPNOTSUPP ? fail(host_error::unsupported) : errno_failure();
        }
        return HostCompletion::ok();
    });

    add(signature("neura.net.option.get", {int64(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        const auto option = as_int32(request.arguments.at(1));
        if (!socket || !known_option(option)) return fail(host_error::invalid_input);
        const auto spec = option_of(option);
        if (!spec) return fail(host_error::unsupported);
        int number = 0;
        socklen_t length = sizeof number;
        if (::getsockopt(socket->descriptor, spec->level, spec->name, &number, &length) != 0) {
            return errno == ENOPROTOOPT || errno == EOPNOTSUPP ? fail(host_error::unsupported) : errno_failure();
        }
        return HostCompletion::ok(int64_value(number));
    });

    add(signature("neura.net.resolve", {text(), int32()}, text_list()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto family = as_int32(request.arguments.at(1));
        if (!usable_text(host) || (family != 0 && family != 4 && family != 6)) return fail(host_error::invalid_input);
        HostValue list;
#ifdef NEURA_NUMERIC_ADDRESSES_ONLY
        sockaddr_storage address{};
        socklen_t length = 0;
        if (!numeric_address(host, 0, address, length)) return fail(host_error::unsupported);
        char text_form[NI_MAXHOST] = {};
        ::getnameinfo(reinterpret_cast<sockaddr*>(&address), length, text_form, sizeof text_form, nullptr, 0,
                      NI_NUMERICHOST);
        list.elements.push_back(bytes_value(text_form));
#else
        addrinfo hints{};
        hints.ai_family = family == 4 ? AF_INET : family == 6 ? AF_INET6 : AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* found = nullptr;
        if (const int code = ::getaddrinfo(host.c_str(), nullptr, &hints, &found); code != 0) {
            return fail(code == EAI_NONAME ? host_error::not_found : host_error::other);
        }
        for (const auto* entry = found; entry != nullptr; entry = entry->ai_next) {
            char text_form[NI_MAXHOST] = {};
            if (::getnameinfo(entry->ai_addr, entry->ai_addrlen, text_form, sizeof text_form, nullptr, 0,
                              NI_NUMERICHOST) != 0) {
                continue;
            }
            const std::string address = text_form;
            const bool seen = std::any_of(list.elements.begin(), list.elements.end(),
                                          [&](const HostValue& value) { return value.bytes == address; });
            if (!seen) list.elements.push_back(bytes_value(address));
        }
        ::freeaddrinfo(found);
#endif
        return HostCompletion::ok(std::move(list));
    });

    // A datagram, or with UDP_GRO (option 14) several coalesced datagrams of
    // one size, and that size: the bytes, the sender's address and port, and
    // the segment size (the byte count when the kernel did not coalesce).
    add(signature("neura.net.udp.receive_segments", {int64(), int32(), int64()}, segments_result()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto max = as_int32(request.arguments.at(1));
            const auto timeout = as_int64(request.arguments.at(2));
            if (!socket || !socket->datagram || max < 1 || timeout < 0) return fail(host_error::invalid_input);
            const int ready = wait_readable(socket->descriptor, timeout);
            if (ready == 0) return fail(host_error::timed_out);
            if (ready < 0) return errno_failure();
            std::string bytes(std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes), '\0');
            sockaddr_storage from{};
            iovec vector{bytes.data(), bytes.size()};
            alignas(cmsghdr) char control[CMSG_SPACE(sizeof(int))] = {};
            msghdr message{};
            message.msg_name = &from;
            message.msg_namelen = sizeof from;
            message.msg_iov = &vector;
            message.msg_iovlen = 1;
            message.msg_control = control;
            message.msg_controllen = sizeof control;
            ssize_t got;
            do {
                got = ::recvmsg(socket->descriptor, &message, 0);
            } while (got < 0 && errno == EINTR);
            if (got < 0) return errno_failure();
            bytes.resize(static_cast<std::size_t>(got));
            std::int32_t segment = static_cast<std::int32_t>(got);
#ifdef UDP_GRO
            for (auto* header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
                if (header->cmsg_level == IPPROTO_UDP && header->cmsg_type == UDP_GRO) {
                    int size = 0;
                    std::memcpy(&size, CMSG_DATA(header), sizeof size);
                    if (size > 0) segment = size;
                }
            }
#endif
            char host[NI_MAXHOST] = {};
            char service[NI_MAXSERV] = {};
            std::string address;
            std::int32_t port = 0;
            if (::getnameinfo(reinterpret_cast<const sockaddr*>(&from), message.msg_namelen, host, sizeof host, service,
                              sizeof service, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                address = host;
                port = std::atoi(service);
            }
            return HostCompletion::ok(tuple_value({bytes_value(std::move(bytes)), bytes_value(std::move(address)),
                                                   int32_value(port), int32_value(segment)}));
        });

    // Multipath TCP (Linux 5.6 and later): a listener or a connection made
    // with IPPROTO_MPTCP, numeric addresses only. The streams are ordinary
    // entries of the socket table. A kernel without MPTCP, or with it turned
    // off (net.mptcp.enabled), answers unsupported.
    const auto mptcp_socket = [](int family) {
#ifdef __linux__
        const int descriptor = ::socket(family, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_MPTCP);
        return descriptor;
#else
        (void)family;
        errno = EPROTONOSUPPORT;
        return -1;
#endif
    };
    const auto mptcp_failure = [] {
        return errno == EPROTONOSUPPORT || errno == EINVAL || errno == ENOPROTOOPT || errno == EAFNOSUPPORT
            ? fail(host_error::unsupported) : errno_failure();
    };

    add(signature("neura.net.mptcp.listen", {text(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto port = as_int32(request.arguments.at(1));
        sockaddr_storage address{};
        socklen_t length = 0;
        if (!valid_port(port) || !numeric_address(request.arguments.at(0).bytes, port, address, length)) {
            return fail(host_error::invalid_input);
        }
        if (full()) return fail(host_error::other);
        const int descriptor = mptcp_socket(address.ss_family);
        if (descriptor < 0) return mptcp_failure();
        const int one = 1;
        ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::listen(descriptor, 128) != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, true, false);
    });

    add(signature("neura.net.mptcp.connect", {text(), int32(), int64()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto port = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        sockaddr_storage address{};
        socklen_t length = 0;
        if (!valid_port(port) || timeout < 0 ||
            !numeric_address(request.arguments.at(0).bytes, port, address, length)) {
            return fail(host_error::invalid_input);
        }
        if (full()) return fail(host_error::other);
        const int descriptor = mptcp_socket(address.ss_family);
        if (descriptor < 0) return mptcp_failure();
        const int flags = ::fcntl(descriptor, F_GETFL);
        ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK);
        if (::connect(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            if (errno != EINPROGRESS) {
                const auto failure = errno_failure();
                ::close(descriptor);
                return failure;
            }
            const int ready = wait_for_events(descriptor, POLLOUT, timeout);
            int error = 0;
            socklen_t size = sizeof error;
            if (ready <= 0 || ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &size) != 0 || error != 0) {
                if (ready > 0) errno = error;
                const auto failure = ready == 0 ? fail(host_error::timed_out) : errno_failure();
                ::close(descriptor);
                return failure;
            }
        }
        ::fcntl(descriptor, F_SETFL, flags);
        return remember(descriptor, false, false);
    });

    // A pipe for neura.net.splice: its read end and its write end, as two
    // entries of the socket table.
    add(signature("neura.net.pipe", {}, tuple({int64(), int64()})), [=](const HostRequest&) {
        if (!granted->net) return denied();
#ifdef __linux__
        if (sockets->sockets.size() + 2 > max_open_ids) return fail(host_error::other);
        int ends[2];
        if (::pipe2(ends, O_CLOEXEC) != 0) return errno_failure();
        const auto read_end = remember(ends[0], false, false);
        sockets->sockets[as_int64(read_end.value)].pipe = true;
        const auto write_end = remember(ends[1], false, false);
        sockets->sockets[as_int64(write_end.value)].pipe = true;
        return HostCompletion::ok(tuple_value({read_end.value, write_end.value}));
#else
        return fail(host_error::unsupported);
#endif
    });

    // Moves up to `count` bytes from one entry to another inside the kernel
    // with splice(2): a stream socket into a pipe's write end, or a pipe's
    // read end into a stream socket (or another pipe). The bytes never reach
    // the NVM. Returns the count moved; 0 at the end of the input. Waits for
    // the input to be readable and the output writable.
    add(signature("neura.net.splice", {int64(), int64(), int64()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
#ifdef __linux__
        auto* from = find(request.arguments.at(0));
        auto* to = find(request.arguments.at(1));
        const auto count = as_int64(request.arguments.at(2));
        if (!from || !to || from == to || count < 1 || (!from->pipe && !to->pipe) || from->listener ||
            to->listener || from->datagram || to->datagram) {
            return fail(host_error::invalid_input);
        }
        const auto chunk = static_cast<std::size_t>(std::min<std::int64_t>(count, 1 << 30));
        for (;;) {
            const auto moved = ::splice(from->descriptor, nullptr, to->descriptor, nullptr, chunk,
                                        SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
            if (moved >= 0) return HostCompletion::ok(int64_value(moved));
            if (errno == EINTR) continue;
            if (errno != EAGAIN) return errno_failure();
            // Both sides must be ready; waiting on either alone could spin.
            if (wait_for_events(from->descriptor, POLLIN, 0) < 0 || wait_for_events(to->descriptor, POLLOUT, 0) < 0) {
                return errno_failure();
            }
        }
#else
        (void)request;
        return fail(host_error::unsupported);
#endif
    });

    // Sends `count` bytes of an open file from `offset` to a connected
    // stream. Needs both the net and the fs groups. Linux uses sendfile(2);
    // elsewhere the bytes pass through a buffer in the provider.
    add(signature("neura.net.send_file", {int64(), int64(), int64(), int64()}, int64()),
        [=](const HostRequest& request) {
            if (!granted->net || !granted->fs) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto file = sockets->files.find(as_int64(request.arguments.at(1)));
            const auto offset = as_int64(request.arguments.at(2));
            const auto count = as_int64(request.arguments.at(3));
            if (!socket || socket->listener || socket->datagram || file == sockets->files.end() ||
                !file->second.readable || offset < 0 || count < 0) {
                return fail(host_error::invalid_input);
            }
            std::int64_t sent = 0;
#ifdef __linux__
            off_t position = static_cast<off_t>(offset);
            while (sent < count) {
                const auto chunk = static_cast<std::size_t>(std::min<std::int64_t>(count - sent, 1 << 30));
                const auto result = ::sendfile(socket->descriptor, file->second.descriptor, &position, chunk);
                if (result < 0) {
                    if (errno == EINTR) continue;
                    if (errno == EAGAIN) {
                        pollfd entry{socket->descriptor, POLLOUT, 0};
                        ::poll(&entry, 1, -1);
                        continue;
                    }
                    return sent > 0 ? HostCompletion::ok(int64_value(sent)) : errno_failure();
                }
                if (result == 0) break;
                sent += result;
            }
#else
            std::string buffer(64 * 1024, '\0');
            while (sent < count) {
                const auto want = static_cast<std::size_t>(std::min<std::int64_t>(count - sent, buffer.size()));
                const auto got = ::pread(file->second.descriptor, buffer.data(), want, offset + sent);
                if (got < 0 && errno == EINTR) continue;
                if (got < 0) return sent > 0 ? HostCompletion::ok(int64_value(sent)) : errno_failure();
                if (got == 0) break;
                std::size_t done = 0;
                while (done < static_cast<std::size_t>(got)) {
                    const auto wrote = ::send(socket->descriptor, buffer.data() + done, got - done, 0);
                    if (wrote < 0 && retry_send(errno)) continue;
                    if (wrote < 0) return sent > 0 ? HostCompletion::ok(int64_value(sent)) : errno_failure();
                    done += static_cast<std::size_t>(wrote);
                }
                sent += got;
            }
#endif
            return HostCompletion::ok(int64_value(sent));
        });
}

#endif

} // namespace neura::nvm::provider
