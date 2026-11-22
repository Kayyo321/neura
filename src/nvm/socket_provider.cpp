// Socket services beyond TCP streams (docs/runtime/NVM.md, "Socket services"):
// UDP, Unix-domain stream and datagram sockets, socket options, name
// resolution through the system resolver, and sending a file to a socket.
// They share the socket ID table of neura.net.*, so neura.net.read, write,
// accept, local_port and close work on the streams and listeners they open.
// Stream relay, copy-avoidance send and segmented send overlap through
// readiness waits. Other waiting calls hold the scheduler for their timeout.

#include "provider_internal.hpp"

#include "platform/socket.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <optional>

#ifdef _WIN32
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <mstcpip.h>
#include <iphlpapi.h>

// Windows 10 2004 winsock constants are missing from older MinGW headers.
#ifndef UDP_SEND_MSG_SIZE
#define UDP_SEND_MSG_SIZE 2
#define UDP_RECV_MAX_COALESCED_SIZE 3
#define UDP_COALESCED_INFO 3
#endif
#ifndef SIO_UDP_NETRESET
#define SIO_UDP_NETRESET _WSAIOW(IOC_VENDOR, 15)
#endif
#include <span>
#include <string>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#if defined(IP_RECVIF)
#include <net/if_dl.h>
#endif
#endif
#ifdef __linux__
#include <linux/io_uring.h>
#include <linux/udp.h>
#include <sys/epoll.h>
#include <sys/syscall.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#endif
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include <sys/event.h>
#define NEURA_HAS_KQUEUE 1
#endif
#ifndef IPPROTO_MPTCP
#define IPPROTO_MPTCP 262
#endif

namespace neura::nvm::provider {

namespace {

HostType address_result() { return tuple({byte_list(), text(), int32()}); }
HostType segments_result() { return tuple({byte_list(), text(), int32(), int32()}); }
// This capability check is a host observation, so replay uses its recorded
// outcome. The drivers apply a successful selection without probing again.
HostCompletion select_event_loop(std::int32_t code) {
    if (code < 1 || code > 4) return fail(host_error::invalid_input);
    if (code == 4) return HostCompletion::ok();
#ifndef _WIN32
    int descriptor = -1;
#ifdef __linux__
    if (code == 1) {
        io_uring_params parameters{};
        descriptor = static_cast<int>(::syscall(__NR_io_uring_setup, 2, &parameters));
    } else if (code == 2) {
        descriptor = ::epoll_create1(EPOLL_CLOEXEC);
    }
#endif
#ifdef NEURA_HAS_KQUEUE
    if (code == 3) descriptor = ::kqueue();
#endif
    if (descriptor >= 0) {
        ::close(descriptor);
        return HostCompletion::ok();
    }
#endif
    return fail(host_error::unsupported);
}

using SocketClock = std::chrono::steady_clock;
SocketClock::time_point socket_deadline(std::int64_t timeout_nanos) {
    const auto now = SocketClock::now();
    const auto timeout = std::chrono::duration_cast<SocketClock::duration>(std::chrono::nanoseconds(timeout_nanos));
    return timeout > SocketClock::time_point::max() - now ? SocketClock::time_point::max() : now + timeout;
}

HostType option_numbers() { return HostType(Kind::List, std::vector<HostType>{int32()}); }
HostType option_values() { return HostType(Kind::List, std::vector<HostType>{int64()}); }

} // namespace

#ifdef _WIN32

// Winsock 2 (W14). UDP, Unix-domain streams (AF_UNIX, Windows 10 1803 and
// later), the options of the W14 table, resolution through GetAddrInfoW, and
// send_file through TransmitFile (a buffered copy to a Unix-domain stream).
// The sockets are blocking, as on POSIX. Windows AF_UNIX has stream sockets
// only and no abstract namespace. Linux's explicit pipe and splice v1
// services return Unsupported; splice v2 relays two streams on every host.

namespace {

HostCompletion wsa_failure() { return fail(host_error::from_wsa(::WSAGetLastError())); }

// Starts Winsock before the first socket call of the process; its failure
// code otherwise.
std::optional<std::uint32_t> start_sockets() {
    if (const auto error = platform::ensure_sockets()) return host_error::from_wsa(error.value());
    return std::nullopt;
}

bool valid_port(std::int32_t port) { return port >= 0 && port <= 65535; }

// A socket that child processes do not inherit, as FD_CLOEXEC keeps a
// descriptor from them. Overlapped, as socket() makes it, for TransmitFile.
// An IPv6 socket is dual-stack, as Linux and macOS make it by default
// (IPV6_V6ONLY 0): udp.bind("::", port) also receives IPv4 datagrams.
// Windows turns IPV6_V6ONLY on by default.
NativeSocket new_socket(int family, int type, int protocol = 0) {
    if (protocol == 0 && family != AF_UNIX && type != SOCK_RAW) {
        protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
    }
    const auto socket =
        ::WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (socket != invalid_native_socket && family == AF_INET6 && type != SOCK_RAW) {
        const DWORD v6_only = 0;
        ::setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6_only), sizeof v6_only);
    }
    return socket;
}

// A numeric IPv4 or IPv6 address, "localhost", or the empty host (any
// address, for binding). Names other than localhost go through
// neura.net.resolve first, so the datagram services never block on DNS.
bool numeric_address(const std::string& host, std::int32_t port, sockaddr_storage& out, int& length) {
    if (host.find('\0') != std::string::npos) return false;
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

// A Unix-domain path: nothing when `out` holds it, or the error code. The
// checks and codes are those of the POSIX form off Linux: an empty or too
// long path or one with NUL is InvalidInput, and an abstract name ('@') is
// Unsupported. The path is passed in UTF-8, the code page of Neura's
// executables (W07); text that is not UTF-8 cannot name a file there and is
// Unsupported, as such a file name is.
std::optional<std::uint32_t> unix_address(const std::string& path, sockaddr_un& out, int& length) {
    std::memset(&out, 0, sizeof out);
    out.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof out.sun_path || path.find('\0') != std::string::npos) {
        return host_error::invalid_input;
    }
    if (path.front() == '@' || !platform::is_valid_utf8(path)) return host_error::unsupported;
    std::memcpy(out.sun_path, path.data(), path.size());
    length = static_cast<int>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    return std::nullopt;
}

// Windows refuses a connection to a Unix-domain path that does not exist
// with WSAECONNREFUSED; POSIX answers ENOENT, which is NotFound.
std::uint32_t unix_connect_error(const std::string& path, int error) {
    if (error == WSAECONNREFUSED) {
        const auto name = platform::try_to_utf16(path);
        if (name && ::GetFileAttributesW(name->c_str()) == INVALID_FILE_ATTRIBUTES) {
            const DWORD missing = ::GetLastError();
            if (missing == ERROR_FILE_NOT_FOUND || missing == ERROR_PATH_NOT_FOUND) return host_error::not_found;
        }
    }
    return host_error::from_wsa(error);
}

// Waits until `socket` has `events` or the deadline passes: 1 ready, 0 timed
// out, -1 failed (the Winsock error is set). A zero timeout waits without
// limit.
int wait_for_events(NativeSocket socket, short events, std::int64_t timeout_nanos) {
    const bool limited = timeout_nanos > 0;
    const auto deadline = socket_deadline(limited ? timeout_nanos : 0);
    for (;;) {
        int milliseconds = -1;
        if (limited) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return 0;
            milliseconds = timeout_milliseconds(left);
        }
        platform::PollItem entry{socket, events, 0};
        const int ready = platform::poll(std::span(&entry, 1), milliseconds);
        if (ready > 0) return 1;
        if (ready == 0) {
            if (!limited) continue;
            return 0;
        }
        if (::WSAGetLastError() != WSAEINTR) return -1;
    }
}

int wait_readable(NativeSocket socket, std::int64_t timeout_nanos) {
    return wait_for_events(socket, platform::poll_in, timeout_nanos);
}

// The sender's numeric address and port.
void sender_of(const sockaddr_storage& from, int length, std::string& address, std::int32_t& port) {
    char host[NI_MAXHOST] = {};
    char service[NI_MAXSERV] = {};
    if (::getnameinfo(reinterpret_cast<const sockaddr*>(&from), length, host, sizeof host, service, sizeof service,
                      NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        address = host;
        port = std::atoi(service);
    }
}

// The largest datagram that UDP_RECV_MAX_COALESCED_SIZE lets the stack
// build: 65,535 bytes less the 8-byte UDP header, the value MsQuic uses.
constexpr DWORD max_coalesced_bytes = 65535 - 8;

// The socket's protocol: option 15, and the protocol that a TCP or UDP level
// option needs.
std::optional<int> protocol_of(NativeSocket socket) {
    WSAPROTOCOL_INFOW info{};
    int length = sizeof info;
    if (::getsockopt(socket, SOL_SOCKET, SO_PROTOCOL_INFOW, reinterpret_cast<char*>(&info), &length) != 0) {
        return std::nullopt;
    }
    return info.iProtocol;
}

struct OptionSpec {
    int level;
    int name;
};

// The option numbers of neura.net.option.set and get (W14). 1 is
// SO_EXCLUSIVEADDRUSE, because SO_REUSEADDR on Windows lets another socket
// take over a bound port; 2 (SO_REUSEPORT) and 5 (TCP_USER_TIMEOUT, whose
// TCP_MAXRT counterpart counts whole seconds) have no Windows form; 13 and
// 14 are the UDP segmentation options of Windows 10 2004; 15 is read from
// SO_PROTOCOL_INFOW (protocol_of).
std::optional<OptionSpec> option_of(std::int32_t option) {
    switch (option) {
    case 1: return OptionSpec{SOL_SOCKET, SO_EXCLUSIVEADDRUSE};
    case 3: return OptionSpec{IPPROTO_TCP, TCP_NODELAY};
    case 4: return OptionSpec{IPPROTO_TCP, TCP_FASTOPEN};
    case 6: return OptionSpec{SOL_SOCKET, SO_KEEPALIVE};
    case 7: return OptionSpec{IPPROTO_TCP, TCP_KEEPIDLE};
    case 8: return OptionSpec{IPPROTO_TCP, TCP_KEEPINTVL};
    case 9: return OptionSpec{IPPROTO_TCP, TCP_KEEPCNT};
    case 10: return OptionSpec{SOL_SOCKET, SO_RCVBUF};
    case 11: return OptionSpec{SOL_SOCKET, SO_SNDBUF};
    case 12: return OptionSpec{SOL_SOCKET, SO_BROADCAST};
    // A send of more than this many bytes leaves as datagrams of this size
    // (0 turns it off), as UDP_SEGMENT does.
    case 13: return OptionSpec{IPPROTO_UDP, UDP_SEND_MSG_SIZE};
    // Receive coalescing, a switch as UDP_GRO is: on sets the largest
    // coalesced datagram, off sets 0, and reading it gives 1 or 0.
    case 14: return OptionSpec{IPPROTO_UDP, UDP_RECV_MAX_COALESCED_SIZE};
    case 15: return OptionSpec{SOL_SOCKET, SO_PROTOCOL_INFOW};
    default: return std::nullopt;
    }
}

bool known_option(std::int32_t option) { return option >= 1 && option <= 15; }
bool read_only_option(std::int32_t option) { return option == 15; }

// Whether the option's level fits the socket: a TCP option of a UDP or Unix
// socket, or a UDP option of a stream, is Unsupported, as ENOPROTOOPT and
// EOPNOTSUPP are on POSIX, where Winsock answers WSAEINVAL as it does for a
// value it refuses.
bool option_fits(NativeSocket socket, const OptionSpec& spec) {
    if (spec.level != IPPROTO_TCP && spec.level != IPPROTO_UDP) return true;
    const auto protocol = protocol_of(socket);
    return !protocol || *protocol == spec.level || (*protocol == IPPROTO_MPTCP && spec.level == IPPROTO_TCP);
}

bool unsupported_option_error(int error) { return error == WSAENOPROTOOPT || error == WSAEOPNOTSUPP; }

// A manual-reset event for the OVERLAPPED of one call.
struct Event {
    Event() : handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
    ~Event() {
        if (handle != nullptr) ::CloseHandle(handle);
    }
    HANDLE handle;
};

OVERLAPPED at_offset(std::uint64_t position, HANDLE event) {
    OVERLAPPED overlapped{};
    overlapped.Offset = static_cast<DWORD>(position);
    overlapped.OffsetHigh = static_cast<DWORD>(position >> 32);
    overlapped.hEvent = event;
    ::ResetEvent(event);
    return overlapped;
}

// Sends `count` bytes of `file` from `offset` to a TCP stream with
// TransmitFile, which reads at the offset of its OVERLAPPED. Adds what it
// sent to `sent`. Returns 0, or the Winsock error of the call that failed.
int transmit(NativeSocket socket, HANDLE file, std::int64_t offset, std::int64_t count, std::int64_t& sent) {
    Event event;
    if (event.handle == nullptr) return static_cast<int>(::GetLastError());
    while (sent < count) {
        // TransmitFile sends at most 2^31 - 2 bytes a call.
        const auto chunk = static_cast<DWORD>(std::min<std::int64_t>(count - sent, 1 << 30));
        auto overlapped = at_offset(static_cast<std::uint64_t>(offset + sent), event.handle);
        if (!::TransmitFile(socket, file, chunk, 0, &overlapped, nullptr, 0)) {
            const int error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING) return error;
        }
        DWORD done = 0;
        DWORD flags = 0;
        if (!::WSAGetOverlappedResult(socket, &overlapped, &done, TRUE, &flags)) return ::WSAGetLastError();
        if (done == 0) break;
        sent += done;
    }
    return 0;
}

// The same through a 64 KiB buffer, for a stream that TransmitFile does not
// serve: on an AF_UNIX stream its transfer never completes. Returns 0, or the
// Win32 or Winsock error of the call that failed.
int copy(NativeSocket socket, HANDLE file, std::int64_t offset, std::int64_t count, std::int64_t& sent) {
    Event event;
    if (event.handle == nullptr) return static_cast<int>(::GetLastError());
    std::string buffer(64 * 1024, '\0');
    int error = 0;
    while (error == 0 && sent < count) {
        const auto want = static_cast<DWORD>(std::min<std::int64_t>(count - sent, buffer.size()));
        auto overlapped = at_offset(static_cast<std::uint64_t>(offset + sent), event.handle);
        DWORD got = 0;
        if (!::ReadFile(file, buffer.data(), want, &got, &overlapped) &&
            (::GetLastError() != ERROR_IO_PENDING || !::GetOverlappedResult(file, &overlapped, &got, TRUE))) {
            if (::GetLastError() != ERROR_HANDLE_EOF) error = static_cast<int>(::GetLastError());
            break;
        }
        if (got == 0) break;
        DWORD done = 0;
        while (done < got) {
            const int wrote = ::send(socket, buffer.data() + done, static_cast<int>(got - done), 0);
            if (wrote >= 0) {
                done += static_cast<DWORD>(wrote);
            } else if (::WSAGetLastError() != WSAEINTR) {
                error = ::WSAGetLastError();
                break;
            }
        }
        if (error == 0) sent += got;
    }
    return error;
}

} // namespace

void add_socket_transport_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities) {
    auto* sockets = &tables;
    const auto* granted = &capabilities;
    const auto remember = [sockets](NativeSocket socket, bool listener, bool datagram) {
        const auto id = sockets->next_socket++;
        sockets->sockets[id] = {socket, listener, datagram};
        return HostCompletion::ok(int64_value(id));
    };
    const auto find = [sockets](const HostValue& id) -> StandardServices::Tables::Socket* {
        const auto found = sockets->sockets.find(as_int64(id));
        return found == sockets->sockets.end() || found->second.raw ? nullptr : &found->second;
    };
    const auto full = [sockets] { return sockets->sockets.size() >= max_open_ids; };
    const auto unsupported = [granted](const HostRequest&) {
        return granted->net ? fail(host_error::unsupported) : denied();
    };

    const auto udp_bind = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        if (const auto failure = start_sockets()) return fail(*failure);
        sockaddr_storage address{};
        int length = 0;
        if (!valid_port(port) || !numeric_address(host, port, address, length)) return fail(host_error::invalid_input);
        if (full()) return fail(host_error::other);
        const auto socket = new_socket(address.ss_family, SOCK_DGRAM);
        if (socket == invalid_native_socket) return wsa_failure();
        if (request.arguments.size() == 4) {
            if (const auto failure = set_socket_options(socket, request.arguments.at(2), request.arguments.at(3))) {
                platform::close_socket(socket);
                return fail(*failure);
            }
        }
        if (::bind(socket, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            const auto failure = wsa_failure();
            platform::close_socket(socket);
            return failure;
        }
        // A datagram to a port that nothing serves makes Windows fail the
        // socket's next receive with WSAECONNRESET (or WSAENETRESET when its
        // time to live ran out); an unconnected POSIX socket reports neither.
        for (const DWORD code : {static_cast<DWORD>(SIO_UDP_CONNRESET), static_cast<DWORD>(SIO_UDP_NETRESET)}) {
            BOOL report = FALSE;
            DWORD bytes = 0;
            ::WSAIoctl(socket, code, &report, sizeof report, nullptr, 0, &bytes, nullptr, nullptr);
        }
        return remember(socket, false, true);
    };
    add(signature("neura.net.udp.bind", {text(), int32()}, int64()), udp_bind);
    add({"neura.net.udp.bind", 2, {text(), int32(), option_numbers(), option_values()}, int64(), uint32()}, udp_bind);

    add(signature("neura.net.udp.send_to", {int64(), text(), int32(), byte_list()}, int32()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto& host = request.arguments.at(1).bytes;
            const auto port = as_int32(request.arguments.at(2));
            const auto& bytes = request.arguments.at(3).bytes;
            sockaddr_storage address{};
            int length = 0;
            if (!socket || !socket->datagram || socket->raw || !valid_port(port) || !numeric_address(host, port, address, length)) {
                return fail(host_error::invalid_input);
            }
            const auto size = static_cast<int>(std::min<std::size_t>(bytes.size(), INT_MAX));
            int sent;
            do {
                sent = ::sendto(socket->descriptor, bytes.data(), size, 0, reinterpret_cast<sockaddr*>(&address), length);
            } while (sent < 0 && ::WSAGetLastError() == WSAEINTR);
            if (sent < 0) return wsa_failure();
            return HostCompletion::ok(int32_value(sent));
        });

    // One datagram: the bytes, the sender's address text, and its port. A
    // datagram longer than the maximum is truncated to it, as on POSIX, where
    // Winsock fills the buffer and reports WSAEMSGSIZE.
    add(signature("neura.net.udp.receive_from", {int64(), int32(), int64()}, address_result()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto max = as_int32(request.arguments.at(1));
            const auto timeout = as_int64(request.arguments.at(2));
            if (!socket || !socket->datagram || socket->raw || max < 1 || timeout < 0) return fail(host_error::invalid_input);
            const int ready = wait_readable(socket->descriptor, timeout);
            if (ready == 0) return fail(host_error::timed_out);
            if (ready < 0) return wsa_failure();
            std::string bytes(std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes), '\0');
            sockaddr_storage from{};
            int length = sizeof from;
            int got;
            int error = 0;
            do {
                length = sizeof from;
                got = ::recvfrom(socket->descriptor, bytes.data(), static_cast<int>(bytes.size()), 0,
                                 reinterpret_cast<sockaddr*>(&from), &length);
                error = got < 0 ? ::WSAGetLastError() : 0;
            } while (got < 0 && error == WSAEINTR);
            if (got < 0 && error == WSAEMSGSIZE) got = static_cast<int>(bytes.size());
            if (got < 0) return fail(host_error::from_wsa(error));
            bytes.resize(static_cast<std::size_t>(got));
            std::string address;
            std::int32_t port = 0;
            sender_of(from, length, address, port);
            return HostCompletion::ok(tuple_value({bytes_value(std::move(bytes)), bytes_value(std::move(address)),
                                                   int32_value(port)}));
        });

    add(signature("neura.net.unix.connect", {text()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& path = request.arguments.at(0).bytes;
        sockaddr_un address{};
        int length = 0;
        if (const auto failure = unix_address(path, address, length)) return fail(*failure);
        if (const auto failure = start_sockets()) return fail(*failure);
        if (full()) return fail(host_error::other);
        const auto socket = new_socket(AF_UNIX, SOCK_STREAM);
        if (socket == invalid_native_socket) return wsa_failure();
        int result;
        int error = 0;
        do {
            result = ::connect(socket, reinterpret_cast<sockaddr*>(&address), length);
            error = result != 0 ? ::WSAGetLastError() : 0;
        } while (result != 0 && error == WSAEINTR);
        if (result != 0) {
            platform::close_socket(socket);
            return fail(unix_connect_error(path, error));
        }
        return remember(socket, false, false);
    });

    add(signature("neura.net.unix.listen", {text()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        sockaddr_un address{};
        int length = 0;
        if (const auto failure = unix_address(request.arguments.at(0).bytes, address, length)) return fail(*failure);
        if (const auto failure = start_sockets()) return fail(*failure);
        if (full()) return fail(host_error::other);
        const auto socket = new_socket(AF_UNIX, SOCK_STREAM);
        if (socket == invalid_native_socket) return wsa_failure();
        if (::bind(socket, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::listen(socket, 128) != 0) {
            const auto failure = wsa_failure();
            platform::close_socket(socket);
            return failure;
        }
        return remember(socket, true, false);
    });

    // Windows AF_UNIX has no datagram sockets.
    add(signature("neura.net.unix.datagram", {text()}, int64()), unsupported);
    add(signature("neura.net.unix.send_to", {int64(), text(), byte_list()}, int32()), unsupported);
    add(signature("neura.net.unix.receive_from", {int64(), int32(), int64()}, address_result()), unsupported);

    add(signature("neura.net.option.set", {int64(), int32(), int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        if (!socket || socket->pipe) return fail(host_error::invalid_input);
        const auto error = set_socket_option(socket->descriptor, as_int32(request.arguments.at(1)),
                                             as_int64(request.arguments.at(2)));
        return error ? fail(*error) : HostCompletion::ok();
    });

    add(signature("neura.net.option.get", {int64(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        const auto option = as_int32(request.arguments.at(1));
        if (!socket || !known_option(option)) return fail(host_error::invalid_input);
        const auto spec = option_of(option);
        if (!spec || !option_fits(socket->descriptor, *spec)) return fail(host_error::unsupported);
        if (option == 15) {
            const auto protocol = protocol_of(socket->descriptor);
            if (!protocol) return wsa_failure();
            return HostCompletion::ok(int64_value(*protocol));
        }
        // Some options are a one-byte BOOLEAN, which fills the low byte.
        DWORD number = 0;
        int length = sizeof number;
        if (::getsockopt(socket->descriptor, spec->level, spec->name, reinterpret_cast<char*>(&number), &length) != 0) {
            return unsupported_option_error(::WSAGetLastError()) ? fail(host_error::unsupported) : wsa_failure();
        }
        if (option == 14) number = number != 0 ? 1 : 0;
        return HostCompletion::ok(int64_value(static_cast<std::int32_t>(number)));
    });

    add(signature("neura.net.resolve", {text(), int32()}, text_list()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto family = as_int32(request.arguments.at(1));
        if (!usable_text(host) || (family != 0 && family != 4 && family != 6)) return fail(host_error::invalid_input);
        if (const auto failure = start_sockets()) return fail(*failure);
        // Text that is not UTF-8 names no host.
        const auto name = platform::try_to_utf16(host);
        if (!name) return fail(host_error::not_found);
        ADDRINFOW hints{};
        hints.ai_family = family == 4 ? AF_INET : family == 6 ? AF_INET6 : AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        ADDRINFOW* found = nullptr;
        if (const int code = ::GetAddrInfoW(name->c_str(), nullptr, &hints, &found); code != 0) {
            return fail(code == WSAHOST_NOT_FOUND ? host_error::not_found : host_error::other);
        }
        HostValue list;
        for (const auto* entry = found; entry != nullptr; entry = entry->ai_next) {
            char text_form[NI_MAXHOST] = {};
            if (::getnameinfo(entry->ai_addr, static_cast<socklen_t>(entry->ai_addrlen), text_form, sizeof text_form,
                              nullptr, 0, NI_NUMERICHOST) != 0) {
                continue;
            }
            const std::string address = text_form;
            const bool seen = std::any_of(list.elements.begin(), list.elements.end(),
                                          [&](const HostValue& value) { return value.bytes == address; });
            if (!seen) list.elements.push_back(bytes_value(address));
        }
        ::FreeAddrInfoW(found);
        return HostCompletion::ok(std::move(list));
    });

    // A datagram, or with option 14 several coalesced datagrams of one size,
    // and that size: the bytes, the sender's address and port, and the
    // segment size (UDP_COALESCED_INFO, or the byte count when the stack did
    // not coalesce). WSARecvMsg is Winsock's recvmsg.
    add(signature("neura.net.udp.receive_segments", {int64(), int32(), int64()}, segments_result()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto max = as_int32(request.arguments.at(1));
            const auto timeout = as_int64(request.arguments.at(2));
            if (!socket || !socket->datagram || socket->raw || max < 1 || timeout < 0) return fail(host_error::invalid_input);
            GUID guid = WSAID_WSARECVMSG;
            LPFN_WSARECVMSG receive_message = nullptr;
            DWORD returned = 0;
            if (::WSAIoctl(socket->descriptor, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof guid,
                           &receive_message, sizeof receive_message, &returned, nullptr, nullptr) != 0 ||
                receive_message == nullptr) {
                return wsa_failure();
            }
            const int ready = wait_readable(socket->descriptor, timeout);
            if (ready == 0) return fail(host_error::timed_out);
            if (ready < 0) return wsa_failure();
            std::string bytes(std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes), '\0');
            sockaddr_storage from{};
            WSABUF data{static_cast<ULONG>(bytes.size()), bytes.data()};
            alignas(WSACMSGHDR) char control[WSA_CMSG_SPACE(sizeof(DWORD))] = {};
            WSAMSG message{};
            DWORD got = 0;
            int error = 0;
            do {
                message = WSAMSG{};
                message.name = reinterpret_cast<sockaddr*>(&from);
                message.namelen = sizeof from;
                message.lpBuffers = &data;
                message.dwBufferCount = 1;
                message.Control = WSABUF{sizeof control, control};
                got = 0;
                error = receive_message(socket->descriptor, &message, &got, nullptr, nullptr) != 0 ? ::WSAGetLastError()
                                                                                                    : 0;
            } while (error == WSAEINTR);
            if (error == WSAEMSGSIZE) {
                got = static_cast<DWORD>(bytes.size());
            } else if (error != 0) {
                return fail(host_error::from_wsa(error));
            }
            bytes.resize(static_cast<std::size_t>(got));
            std::int32_t segment = static_cast<std::int32_t>(got);
            for (auto* header = WSA_CMSG_FIRSTHDR(&message); header; header = WSA_CMSG_NXTHDR(&message, header)) {
                if (header->cmsg_level == IPPROTO_UDP && header->cmsg_type == UDP_COALESCED_INFO) {
                    DWORD size = 0;
                    std::memcpy(&size, WSA_CMSG_DATA(header), sizeof size);
                    if (size > 0) segment = static_cast<std::int32_t>(size);
                }
            }
            std::string address;
            std::int32_t port = 0;
            sender_of(from, message.namelen, address, port);
            return HostCompletion::ok(tuple_value({bytes_value(std::move(bytes)), bytes_value(std::move(address)),
                                                   int32_value(port), int32_value(segment)}));
        });

    // Ask Winsock for MPTCP by protocol number. A host with no protocol
    // provider answers Unsupported; a TCP fallback would silently ignore
    // the caller's transport choice.
    const auto mptcp_failure = [] {
        const auto error = ::WSAGetLastError();
        if (error == WSAEPROTONOSUPPORT || error == WSAESOCKTNOSUPPORT || error == WSAEPROTOTYPE ||
            error == WSAEINVAL || error == WSAEOPNOTSUPP || error == WSAEAFNOSUPPORT) {
            return fail(host_error::unsupported);
        }
        return fail(host_error::from_wsa(error));
    };
    const auto mptcp_listen = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto port = as_int32(request.arguments.at(1));
        if (const auto failure = start_sockets()) return fail(*failure);
        sockaddr_storage address{};
        int length = 0;
        if (!valid_port(port) || !numeric_address(request.arguments.at(0).bytes, port, address, length)) {
            return fail(host_error::invalid_input);
        }
        if (full()) return fail(host_error::other);
        const auto socket = new_socket(address.ss_family, SOCK_STREAM, IPPROTO_MPTCP);
        if (socket == invalid_native_socket) return mptcp_failure();
        if (request.arguments.size() == 4) {
            if (const auto failure = set_socket_options(socket, request.arguments.at(2), request.arguments.at(3))) {
                platform::close_socket(socket);
                return fail(*failure);
            }
        }
        if (::bind(socket, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::listen(socket, 128) != 0) {
            const auto failure = wsa_failure();
            platform::close_socket(socket);
            return failure;
        }
        return remember(socket, true, false);
    };
    add(signature("neura.net.mptcp.listen", {text(), int32()}, int64()), mptcp_listen);
    add({"neura.net.mptcp.listen", 2, {text(), int32(), option_numbers(), option_values()}, int64(), uint32()},
        mptcp_listen);

    add(signature("neura.net.mptcp.connect", {text(), int32(), int64()}, int64()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto port = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (const auto failure = start_sockets()) return fail(*failure);
        sockaddr_storage address{};
        int length = 0;
        if (!valid_port(port) || timeout < 0 ||
            !numeric_address(request.arguments.at(0).bytes, port, address, length)) {
            return fail(host_error::invalid_input);
        }
        if (full()) return fail(host_error::other);
        const auto socket = new_socket(address.ss_family, SOCK_STREAM, IPPROTO_MPTCP);
        if (socket == invalid_native_socket) return mptcp_failure();
        const auto close_with = [socket](HostCompletion completion) {
            platform::close_socket(socket);
            return completion;
        };
        if (!platform::set_nonblocking(socket)) return close_with(wsa_failure());
        if (::connect(socket, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            const int error = ::WSAGetLastError();
            if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
                return close_with(fail(host_error::from_wsa(error)));
            }
            const int ready = wait_for_events(socket, platform::poll_out, timeout);
            if (ready == 0) return close_with(fail(host_error::timed_out));
            if (ready < 0) return close_with(wsa_failure());
            int problem = 0;
            int size = sizeof problem;
            if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&problem), &size) != 0) {
                return close_with(wsa_failure());
            }
            if (problem != 0) return close_with(fail(host_error::from_wsa(problem)));
        }
        if (!platform::set_nonblocking(socket, false)) return close_with(wsa_failure());
        return remember(socket, false, false);
    });

    // splice(2) and its pipes are Linux's.
    add(signature("neura.net.pipe", {}, tuple({int64(), int64()})), unsupported);
    add(signature("neura.net.splice", {int64(), int64(), int64()}, int64()), unsupported);

    // Sends `count` bytes of an open file from `offset` to a connected
    // stream: TransmitFile to a TCP stream, a 64 KiB copy otherwise. Needs
    // both the net and the fs groups. It stops at the end of the file, and
    // the file's position does not move, as with sendfile(2) and pread.
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
            // TransmitFile refuses a count past the end of the file: send what
            // the file holds from the offset.
            LARGE_INTEGER size{};
            if (!::GetFileSizeEx(file->second.descriptor, &size)) return fail(host_error::from_win32(::GetLastError()));
            const auto wanted = std::min<std::int64_t>(count, std::max<std::int64_t>(size.QuadPart - offset, 0));
            // A read at an offset can move the position of a synchronous
            // handle, so it is put back.
            LARGE_INTEGER position{};
            if (!::SetFilePointerEx(file->second.descriptor, LARGE_INTEGER{}, &position, FILE_CURRENT)) {
                return fail(host_error::from_win32(::GetLastError()));
            }
            std::int64_t sent = 0;
            const int error = protocol_of(socket->descriptor) == IPPROTO_TCP
                ? transmit(socket->descriptor, file->second.descriptor, offset, wanted, sent)
                : copy(socket->descriptor, file->second.descriptor, offset, wanted, sent);
            ::SetFilePointerEx(file->second.descriptor, position, nullptr, FILE_BEGIN);
            if (error != 0 && sent == 0) return fail(host_error::from_wsa(error));
            return HostCompletion::ok(int64_value(sent));
        });
}

#else

namespace {

HostCompletion errno_failure() { return fail(host_error::from_errno(errno)); }

bool valid_port(std::int32_t port) { return port >= 0 && port <= 65535; }

int new_socket(int family, int type, int protocol = 0) {
    const int descriptor = ::socket(family, type, protocol);
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
    if (host.find('\0') != std::string::npos) return false;
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
    const auto deadline = socket_deadline(limited ? timeout_nanos : 0);
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
    const auto deadline = socket_deadline(limited ? timeout_nanos : 0);
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

void add_socket_transport_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities) {
    auto* sockets = &tables;
    const auto* granted = &capabilities;
    const auto remember = [sockets](int descriptor, bool listener, bool datagram) {
        const auto id = sockets->next_socket++;
        sockets->sockets[id] = {descriptor, listener, datagram};
        return HostCompletion::ok(int64_value(id));
    };
    const auto find = [sockets](const HostValue& id) -> StandardServices::Tables::Socket* {
        const auto found = sockets->sockets.find(as_int64(id));
        return found == sockets->sockets.end() || found->second.raw ? nullptr : &found->second;
    };
    const auto full = [sockets] { return sockets->sockets.size() >= max_open_ids; };

    // Datagram reception shared by UDP and Unix datagram sockets: the bytes,
    // the sender's address text, and its port (0 for a Unix sender).
    const auto receive_from = [=](const HostRequest& request, bool unix_socket) {
        if (!granted->net) return denied();
        auto* socket = find(request.arguments.at(0));
        const auto max = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!socket || !socket->datagram || socket->raw || max < 1 || timeout < 0) return fail(host_error::invalid_input);
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

    const auto udp_bind = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        sockaddr_storage address{};
        socklen_t length = 0;
        if (!valid_port(port) || !numeric_address(host, port, address, length)) return fail(host_error::invalid_input);
        if (full()) return fail(host_error::other);
        const int descriptor = new_socket(address.ss_family, SOCK_DGRAM);
        if (descriptor < 0) return errno_failure();
        if (request.arguments.size() == 4) {
            if (const auto failure = set_socket_options(descriptor, request.arguments.at(2), request.arguments.at(3))) {
                platform::close_socket(descriptor);
                return fail(*failure);
            }
        }
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, false, true);
    };
    add(signature("neura.net.udp.bind", {text(), int32()}, int64()), udp_bind);
    add({"neura.net.udp.bind", 2, {text(), int32(), option_numbers(), option_values()}, int64(), uint32()}, udp_bind);

    add(signature("neura.net.udp.send_to", {int64(), text(), int32(), byte_list()}, int32()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            auto* socket = find(request.arguments.at(0));
            const auto& host = request.arguments.at(1).bytes;
            const auto port = as_int32(request.arguments.at(2));
            const auto& bytes = request.arguments.at(3).bytes;
            sockaddr_storage address{};
            socklen_t length = 0;
            if (!socket || !socket->datagram || socket->raw || !valid_port(port) || !numeric_address(host, port, address, length)) {
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
        if (!socket || !socket->datagram || socket->raw) return fail(host_error::invalid_input);
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
        if (!socket || socket->pipe) return fail(host_error::invalid_input);
        const auto error = set_socket_option(socket->descriptor, as_int32(request.arguments.at(1)),
                                             as_int64(request.arguments.at(2)));
        return error ? fail(*error) : HostCompletion::ok();
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
            if (!socket || !socket->datagram || socket->raw || max < 1 || timeout < 0) return fail(host_error::invalid_input);
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

    const auto mptcp_listen = [=](const HostRequest& request) {
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
        if (request.arguments.size() == 4) {
            if (const auto failure = set_socket_options(descriptor, request.arguments.at(2), request.arguments.at(3))) {
                ::close(descriptor);
                return fail(*failure);
            }
        }
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), length) != 0 || ::listen(descriptor, 128) != 0) {
            const auto failure = errno_failure();
            ::close(descriptor);
            return failure;
        }
        return remember(descriptor, true, false);
    };
    add(signature("neura.net.mptcp.listen", {text(), int32()}, int64()), mptcp_listen);
    add({"neura.net.mptcp.listen", 2, {text(), int32(), option_numbers(), option_values()}, int64(), uint32()},
        mptcp_listen);

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
                    if (wrote < 0 && (errno == EINTR || errno == EAGAIN)) continue;
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

std::optional<std::uint32_t> set_socket_option(NativeSocket socket, std::int32_t option, std::int64_t value) {
    if (!known_option(option) || read_only_option(option) || value < 0 || value > INT32_MAX) {
        return host_error::invalid_input;
    }
    const auto spec = option_of(option);
    if (!spec) return host_error::unsupported;
#ifdef _WIN32
    if (!option_fits(socket, *spec)) return host_error::unsupported;
    const DWORD number = option == 14 && value != 0 ? max_coalesced_bytes : static_cast<DWORD>(value);
    if (::setsockopt(socket, spec->level, spec->name, reinterpret_cast<const char*>(&number), sizeof number) != 0) {
        const int error = ::WSAGetLastError();
        return unsupported_option_error(error) ? host_error::unsupported : host_error::from_wsa(error);
    }
#else
    const int number = static_cast<int>(value);
    if (::setsockopt(socket, spec->level, spec->name, &number, sizeof number) != 0) {
        return errno == ENOPROTOOPT || errno == EOPNOTSUPP ? host_error::unsupported : host_error::from_errno(errno);
    }
#endif
    return std::nullopt;
}

std::optional<std::uint32_t> set_socket_options(NativeSocket socket, const HostValue& options, const HostValue& values) {
    if (options.elements.size() != values.elements.size() || options.elements.size() > 15) {
        return host_error::invalid_input;
    }
    for (std::size_t index = 0; index < options.elements.size(); ++index) {
        if (const auto error = set_socket_option(socket, as_int32(options.elements[index]),
                                                 as_int64(values.elements[index]))) {
            return error;
        }
    }
    return std::nullopt;
}

namespace {

#ifdef _WIN32
using AddressLength = int;
HostCompletion socket_failure() { return wsa_failure(); }
#else
using AddressLength = socklen_t;
HostCompletion socket_failure() { return errno_failure(); }
#endif

struct ScopedSocket {
    NativeSocket descriptor;
    ~ScopedSocket() {
        if (descriptor != invalid_native_socket) platform::close_socket(descriptor);
    }
};

constexpr int stream_send_flags =
#ifdef MSG_NOSIGNAL
    MSG_NOSIGNAL;
#else
    0;
#endif

constexpr int nonblocking_flags =
#ifdef MSG_DONTWAIT
    MSG_DONTWAIT;
#else
    0;
#endif

// Winsock has no MSG_DONTWAIT. Switch only for the duration of one scheduler
// attempt, as neura.net.read/write do; no outstanding operation owns a buffer.
struct NonblockingAttempt {
    explicit NonblockingAttempt(NativeSocket socket) : descriptor(socket) {
#ifdef _WIN32
        ready = platform::set_nonblocking(descriptor);
#endif
    }
    ~NonblockingAttempt() {
#ifdef _WIN32
        if (ready) platform::set_nonblocking(descriptor, false);
#endif
    }
    NativeSocket descriptor;
    bool ready = true;
};

// Adapt a nonblocking attempt for programs without tasks. Task images use
// begin() directly, so waiting here never blocks their scheduler.
HostCompletion finish_socket_operation(std::variant<HostCompletion, PendingOperation> started) {
    if (auto* done = std::get_if<HostCompletion>(&started)) return std::move(*done);
    auto& operation = std::get<PendingOperation>(started);
    for (;;) {
        const auto interest = std::get<PosixWait>(operation.wait_source).current();
        const short events = interest.events == PosixWait::Events::Read ? platform::poll_in : platform::poll_out;
        platform::PollItem item{static_cast<NativeSocket>(interest.descriptor), events, 0};
        if (platform::poll(std::span(&item, 1), -1) < 0 && !platform::last_socket_error().interrupted()) {
            return socket_failure();
        }
        if (auto done = operation.attempt(false)) return std::move(*done);
    }
}

bool connected_stream(const StandardServices::Tables::Socket* socket) {
    return socket && !socket->pipe && !socket->listener && !socket->datagram;
}

HostCompletion read_datagram(NativeSocket socket, std::size_t max, std::int64_t timeout) {
    const int ready = wait_readable(socket, timeout);
    if (ready == 0) return fail(host_error::timed_out);
    if (ready < 0) return socket_failure();
    std::string bytes(max, '\0');
    sockaddr_storage from{};
    AddressLength length = sizeof from;
    decltype(::recvfrom(socket, bytes.data(), 0, 0, nullptr, nullptr)) got;
    do {
        length = sizeof from;
        got = ::recvfrom(socket, bytes.data(), static_cast<int>(bytes.size()), 0,
                         reinterpret_cast<sockaddr*>(&from), &length);
    } while (got < 0 && platform::last_socket_error().interrupted());
#ifdef _WIN32
    if (got < 0 && ::WSAGetLastError() == WSAEMSGSIZE) got = static_cast<int>(bytes.size());
#endif
    if (got < 0) return socket_failure();
    bytes.resize(static_cast<std::size_t>(got));
    char address[NI_MAXHOST] = {};
    if (::getnameinfo(reinterpret_cast<sockaddr*>(&from), length, address, sizeof address,
                      nullptr, 0, NI_NUMERICHOST) != 0) {
        return fail(host_error::other);
    }
    return HostCompletion::ok(tuple_value({bytes_value(std::move(bytes)), bytes_value(address), int32_value(0)}));
}

HostCompletion raw_open(StandardServices::Tables& table, std::int32_t family, std::int32_t protocol) {
    if ((family != 4 && family != 6) || protocol < 0 || protocol > 255) return fail(host_error::invalid_input);
    if (table.sockets.size() >= max_open_ids) return fail(host_error::other);
#ifdef _WIN32
    if (const auto failure = start_sockets()) return fail(*failure);
#endif
    const auto descriptor = new_socket(family == 4 ? AF_INET : AF_INET6, SOCK_RAW, protocol);
    if (descriptor == invalid_native_socket) return socket_failure();
#ifdef _WIN32
    // Winsock requires a local bind before receive. It explicitly forbids
    // binding a raw TCP socket. Preserve that socket's permitted send path.
    if (protocol != IPPROTO_TCP) {
        sockaddr_storage address{};
        AddressLength size = 0;
        numeric_address(family == 4 ? "0.0.0.0" : "::", 0, address, size);
        if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address), size) != 0) {
            const auto failure = socket_failure();
            platform::close_socket(descriptor);
            return failure;
        }
    }
#endif
    const auto id = table.next_socket++;
    table.sockets[id] = {descriptor, false, true, false, true};
    return HostCompletion::ok(int64_value(id));
}

// Preserve the native distinction between EAGAIN and an actual ETIMEDOUT:
// both map to typed TimedOut, but only readiness backpressure should park.
std::optional<HostCompletion> nonblocking_failure() {
    const auto error = platform::last_socket_error();
    if (error.would_block() || error.interrupted()) return std::nullopt;
    return socket_failure();
}

// Each attempt runs on the scheduler thread without a handoff between
// peek, send and consume. Other pending reads cannot consume the peeked
// prefix in the middle of an attempt. Holding references also prevents close
// from recycling a descriptor. Embedders must not concurrently read the
// runtime-owned descriptors (the same rule as neura.net.read).
// Linux's private splice pipe cannot be cancelled safely after consuming
// input: an unsent suffix would disappear when that pipe closes. Use the
// bounded copy path until stream state can retain that suffix across calls.
std::optional<HostCompletion> relay_stream(NativeSocket from, NativeSocket to, std::int64_t count) {
    const auto size = static_cast<int>(std::min<std::int64_t>(count, 64 * 1024));
    std::string buffer(static_cast<std::size_t>(size), '\0');
    const auto offered = ::recv(from, buffer.data(), size, MSG_PEEK | nonblocking_flags);
    if (offered < 0) return nonblocking_failure();
    if (offered == 0) return HostCompletion::ok(int64_value(0));
    const auto sent = ::send(to, buffer.data(), static_cast<int>(offered), stream_send_flags | nonblocking_flags);
    if (sent < 0) return nonblocking_failure();
    // Exactly this prefix remains readable: no scheduler operation or other
    // runtime descriptor consumer runs between the peek and this receive.
    std::int64_t consumed = 0;
    while (consumed < sent) {
        const auto got = ::recv(from, buffer.data(), static_cast<int>(sent - consumed), nonblocking_flags);
        if (got < 0 && platform::last_socket_error().interrupted()) continue;
        if (got <= 0) return fail(host_error::connection_reset);
        consumed += got;
    }
    return HostCompletion::ok(int64_value(sent));
}

std::optional<HostCompletion> send_zerocopy(NativeSocket descriptor, const std::string& bytes) {
    if (bytes.empty()) return HostCompletion::ok(int32_value(0));
    if (bytes.size() > INT32_MAX) return fail(host_error::invalid_input);
#ifdef _WIN32
    // A readiness-driven send has no outstanding OVERLAPPED referencing a
    // caller buffer when cancellation destroys the pending operation.
    // Copy avoidance is a hint; keep the send/count contract on Winsock.
    const int sent = ::send(descriptor, bytes.data(), static_cast<int>(bytes.size()), 0);
    if (sent < 0) return nonblocking_failure();
    return HostCompletion::ok(int32_value(sent));
#elif defined(__linux__) && defined(MSG_ZEROCOPY) && defined(SO_ZEROCOPY)
    const int enable = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof enable) != 0) {
        return errno == ENOPROTOOPT || errno == EOPNOTSUPP ? fail(host_error::unsupported) : socket_failure();
    }
    // The private immutable range can be unmapped after send: the kernel
    // retains pinned pages, and no mutable Neura heap storage is shared.
    void* staging = ::mmap(nullptr, bytes.size(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (staging == MAP_FAILED) return socket_failure();
    std::memcpy(staging, bytes.data(), bytes.size());
    const auto sent = ::send(descriptor, staging, bytes.size(), MSG_ZEROCOPY | stream_send_flags | nonblocking_flags);
    const auto error = errno;
    ::munmap(staging, bytes.size());
    if (sent < 0) {
        errno = error;
        if (platform::last_socket_error().would_block() || platform::last_socket_error().interrupted()) {
            return std::nullopt;
        }
        return fail(host_error::from_errno(error));
    }
    return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
#else
    // The send contract remains usable when the platform has no copy-avoidance hint.
    const auto sent = ::send(descriptor, bytes.data(), static_cast<int>(bytes.size()),
                             stream_send_flags | nonblocking_flags);
    if (sent < 0) return nonblocking_failure();
    return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
#endif
}

// A typed setsockopt wrapper for the multicast configuration. Winsock
// declares optval as const char*, POSIX as const void*.
template<typename Value>
bool set_native_option(NativeSocket socket, int level, int option, const Value& value) {
    return ::setsockopt(socket, level, option, reinterpret_cast<const char*>(&value), sizeof value) == 0;
}

std::optional<HostCompletion> send_segments(NativeSocket descriptor, sockaddr_storage& target, AddressLength length,
                             const std::string& bytes, int segment) {
    if (bytes.empty()) return HostCompletion::ok(int32_value(0));
    auto* address = reinterpret_cast<sockaddr*>(&target);
#if defined(_WIN32) || defined(UDP_SEGMENT)
    if (bytes.size() <= 65507) {
#ifdef _WIN32
        DWORD original = 0;
        int size = sizeof original;
        const DWORD segment_size = static_cast<DWORD>(segment);
        if (::getsockopt(descriptor, IPPROTO_UDP, UDP_SEND_MSG_SIZE, reinterpret_cast<char*>(&original), &size) == 0 &&
            set_native_option(descriptor, IPPROTO_UDP, UDP_SEND_MSG_SIZE, segment_size)) {
            const int sent = ::sendto(descriptor, bytes.data(), static_cast<int>(bytes.size()),
                                      nonblocking_flags, address, length);
            const int error = sent < 0 ? ::WSAGetLastError() : 0;
            if (!set_native_option(descriptor, IPPROTO_UDP, UDP_SEND_MSG_SIZE, original)) return socket_failure();
            if (sent >= 0) return HostCompletion::ok(int32_value(sent));
            if (error != WSAEINVAL && error != WSAEMSGSIZE) {
                ::WSASetLastError(error);
                return nonblocking_failure();
            }
        }
#else
        alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(std::uint16_t))> control{};
        iovec data{const_cast<char*>(bytes.data()), bytes.size()};
        msghdr message{};
        message.msg_name = address;
        message.msg_namelen = length;
        message.msg_iov = &data;
        message.msg_iovlen = 1;
        message.msg_control = control.data();
        message.msg_controllen = control.size();
        auto* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = IPPROTO_UDP;
        header->cmsg_type = UDP_SEGMENT;
        header->cmsg_len = CMSG_LEN(sizeof(std::uint16_t));
        const auto segment_size = static_cast<std::uint16_t>(segment);
        std::memcpy(CMSG_DATA(header), &segment_size, sizeof segment_size);
        const auto sent = ::sendmsg(descriptor, &message, nonblocking_flags);
        if (sent >= 0) return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
        if (errno != EINVAL && errno != EMSGSIZE && errno != ENOPROTOOPT && errno != EOPNOTSUPP) {
            return nonblocking_failure();
        }
#endif
    }
#endif
    std::size_t total = 0;
    while (total < bytes.size() && total / static_cast<std::size_t>(segment) < 64) {
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(segment), bytes.size() - total);
        const auto sent = ::sendto(descriptor, bytes.data() + total, static_cast<int>(count), nonblocking_flags, address, length);
        if (sent < 0) {
            if (total > 0) return HostCompletion::ok(int32_value(static_cast<int>(total)));
            return nonblocking_failure();
        }
        if (static_cast<std::size_t>(sent) != count) return fail(host_error::other);
        total += count;
    }
    return HostCompletion::ok(int32_value(static_cast<std::int32_t>(total)));
}

constexpr std::uint16_t mdns_port = 5353;

// RFC 6762 section 11 deems multicast-destination responses on-link even
// across overlay subnets; its source-subnet test applies to unicast replies.
// Neura deliberately applies the stricter source-subnet admission policy to
// multicast too. Match only addresses configured on the ingress interface;
// a route or an address assigned to another interface is not sufficient.
class MdnsLinks {
public:
    struct Subnet {
        unsigned int interface_index;
        std::uint32_t address;
        std::uint32_t mask;
    };
    std::vector<Subnet> subnets;

    bool allows(in_addr source, unsigned int ingress) const {
        const auto host = ntohl(source.s_addr);
        if (ingress == 0 || host == 0 || (host & 0xf0000000U) == 0xe0000000U || host == 0xffffffffU) return false;
        return std::any_of(subnets.begin(), subnets.end(), [&](const auto& subnet) {
            return subnet.interface_index == ingress &&
                   (source.s_addr & subnet.mask) == (subnet.address & subnet.mask);
        });
    }

    std::optional<HostCompletion> load() {
        subnets.clear();
#ifdef _WIN32
        // GetAdaptersAddresses/OnLinkPrefixLength and IN_PKTINFO use the same
        // IPv4 IfIndex. Bound retries if addresses change during enumeration.
        ULONG needed = 15 * 1024;
        std::vector<std::max_align_t> storage;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (needed > 1024 * 1024) return fail(host_error::other);
            storage.resize((needed + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
            auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
            const ULONG result = ::GetAdaptersAddresses(AF_INET,
                GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                nullptr, adapters, &needed);
            if (result == ERROR_BUFFER_OVERFLOW) continue;
            if (result == ERROR_NO_DATA) return std::nullopt;
            if (result != NO_ERROR) return fail(host_error::from_win32(result));
            for (const auto* adapter = adapters; adapter; adapter = adapter->Next) {
                if (adapter->OperStatus != IfOperStatusUp || adapter->IfIndex == 0) continue;
                for (const auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
                    if (!entry->Address.lpSockaddr || entry->Address.iSockaddrLength < static_cast<int>(sizeof(sockaddr_in)) ||
                        entry->Address.lpSockaddr->sa_family != AF_INET || entry->OnLinkPrefixLength > 32) continue;
                    const auto* address = reinterpret_cast<const sockaddr_in*>(entry->Address.lpSockaddr);
                    const auto prefix = entry->OnLinkPrefixLength;
                    const auto mask = prefix == 0 ? 0U : 0xffffffffU << (32 - prefix);
                    subnets.push_back({adapter->IfIndex, address->sin_addr.s_addr, htonl(mask)});
                }
            }
            return std::nullopt;
        }
        return fail(host_error::other);
#else
        ifaddrs* addresses = nullptr;
        if (::getifaddrs(&addresses) != 0) return socket_failure();
        for (const auto* entry = addresses; entry; entry = entry->ifa_next) {
            if (!entry->ifa_addr || !entry->ifa_netmask || entry->ifa_addr->sa_family != AF_INET ||
                (entry->ifa_flags & IFF_UP) == 0) continue;
            const auto index = ::if_nametoindex(entry->ifa_name);
            if (index == 0) continue;
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            const auto* mask = reinterpret_cast<const sockaddr_in*>(entry->ifa_netmask);
            subnets.push_back({index, address->sin_addr.s_addr, mask->sin_addr.s_addr});
        }
        ::freeifaddrs(addresses);
        return std::nullopt;
#endif
    }
};

// The wildcard bind also admits unicast traffic. Read the IP destination
// from the ancillary data, since this querier requests multicast replies.
class MdnsReceiver {
public:
    explicit MdnsReceiver(NativeSocket socket) : socket_(socket) {}

    std::optional<HostCompletion> enable() {
        if (const auto failure = links_.load()) return failure;
#ifdef _WIN32
        const DWORD enabled = 1;
        if (!set_native_option(socket_, IPPROTO_IP, IP_PKTINFO, enabled)) return socket_failure();
        GUID guid = WSAID_WSARECVMSG;
        DWORD returned = 0;
        if (::WSAIoctl(socket_, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof guid, &receive_message_,
                       sizeof receive_message_, &returned, nullptr, nullptr) != 0) return socket_failure();
        if (!receive_message_) return fail(host_error::unsupported);
#elif defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
        const int enabled = 1;
        if (!set_native_option(socket_, IPPROTO_IP, IP_RECVDSTADDR, enabled) ||
            !set_native_option(socket_, IPPROTO_IP, IP_RECVIF, enabled)) return socket_failure();
#elif defined(IP_PKTINFO)
        const int enabled = 1;
        if (!set_native_option(socket_, IPPROTO_IP, IP_PKTINFO, enabled)) return socket_failure();
#else
        return fail(host_error::unsupported);
#endif
#if defined(__linux__) && defined(IP_MULTICAST_ALL)
        const int only_joined_groups = 0;
        if (!set_native_option(socket_, IPPROTO_IP, IP_MULTICAST_ALL, only_joined_groups) && errno != ENOPROTOOPT) {
            return socket_failure();
        }
#endif
        return std::nullopt;
    }

    // A discarded datagram leaves the buffer empty. Incomplete ancillary or
    // payload data cannot prove the datagram is an intact multicast response.
    std::optional<HostCompletion> receive(in_addr group, std::string& bytes) const {
        sockaddr_in source{};
        std::size_t size = 0;
        bool multicast = false;
        unsigned int ingress = 0;
#ifdef _WIN32
        WSABUF data{static_cast<ULONG>(bytes.size()), bytes.data()};
        alignas(WSACMSGHDR) char control[WSA_CMSG_SPACE(sizeof(IN_PKTINFO))] = {};
        WSAMSG message{};
        message.name = reinterpret_cast<sockaddr*>(&source);
        message.namelen = sizeof source;
        message.lpBuffers = &data;
        message.dwBufferCount = 1;
        message.Control = WSABUF{sizeof control, control};
        DWORD got = 0;
        if (receive_message_(socket_, &message, &got, nullptr, nullptr) != 0) {
            const auto error = ::WSAGetLastError();
            if (error != WSAEINTR && error != WSAEMSGSIZE && error != WSAEWOULDBLOCK) {
                return fail(host_error::from_wsa(error));
            }
            bytes.clear();
            return std::nullopt;
        }
        size = got;
        if ((message.dwFlags & (MSG_TRUNC | MSG_CTRUNC)) == 0 && message.namelen >= static_cast<int>(sizeof source)) {
            for (auto* header = WSA_CMSG_FIRSTHDR(&message); header; header = WSA_CMSG_NXTHDR(&message, header)) {
                if (header->cmsg_level == IPPROTO_IP && header->cmsg_type == IP_PKTINFO &&
                    header->cmsg_len >= WSA_CMSG_LEN(sizeof(IN_PKTINFO))) {
                    IN_PKTINFO info{};
                    std::memcpy(&info, WSA_CMSG_DATA(header), sizeof info);
                    multicast = info.ipi_addr.s_addr == group.s_addr;
                    ingress = info.ipi_ifindex;
                    break;
                }
            }
        }
#elif (defined(IP_RECVDSTADDR) && defined(IP_RECVIF)) || defined(IP_PKTINFO)
#if defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
        using Destination = in_addr;
        constexpr int message_type = IP_RECVDSTADDR;
#else
        using Destination = in_pktinfo;
        constexpr int message_type = IP_PKTINFO;
#endif
        iovec data{bytes.data(), bytes.size()};
#if defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(Destination)) + CMSG_SPACE(sizeof(sockaddr_dl))] = {};
#else
        alignas(cmsghdr) char control[CMSG_SPACE(sizeof(Destination))] = {};
#endif
        msghdr message{};
        message.msg_name = &source;
        message.msg_namelen = sizeof source;
        message.msg_iov = &data;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof control;
        const auto got = ::recvmsg(socket_, &message, 0);
        if (got < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) return socket_failure();
            bytes.clear();
            return std::nullopt;
        }
        size = static_cast<std::size_t>(got);
        if ((message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) == 0 && message.msg_namelen >= sizeof source) {
            for (auto* header = CMSG_FIRSTHDR(&message); header; header = CMSG_NXTHDR(&message, header)) {
                if (header->cmsg_level == IPPROTO_IP && header->cmsg_type == message_type &&
                    header->cmsg_len >= CMSG_LEN(sizeof(Destination))) {
                    Destination info{};
                    std::memcpy(&info, CMSG_DATA(header), sizeof info);
#if defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
                    multicast = info.s_addr == group.s_addr;
#else
                    multicast = info.ipi_addr.s_addr == group.s_addr;
                    ingress = static_cast<unsigned int>(info.ipi_ifindex);
#endif
                }
#if defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
                if (header->cmsg_level == IPPROTO_IP && header->cmsg_type == IP_RECVIF &&
                    header->cmsg_len >= CMSG_LEN(offsetof(sockaddr_dl, sdl_data))) {
                    sockaddr_dl info{};
                    const auto payload = header->cmsg_len - CMSG_LEN(0);
                    std::memcpy(&info, CMSG_DATA(header), std::min<std::size_t>(payload, sizeof info));
                    ingress = info.sdl_index;
                }
#endif
            }
        }
#else
        (void)group;
#endif
        if (multicast && source.sin_family == AF_INET && source.sin_port == htons(mdns_port) &&
            links_.allows(source.sin_addr, ingress)) {
            bytes.resize(size);
        } else {
            bytes.clear();
        }
        return std::nullopt;
    }

private:
    NativeSocket socket_;
    MdnsLinks links_;
#ifdef _WIN32
    LPFN_WSARECVMSG receive_message_ = nullptr;
#endif
};

HostCompletion multicast_exchange(const std::string& bytes, std::int64_t window, const std::string& interface_address) {
    if (window <= 0 || window > 5'000'000'000LL || bytes.size() < 12 || bytes.size() > 9000 ||
        (!interface_address.empty() && !usable_text(interface_address))) {
        return fail(host_error::invalid_input);
    }
#ifdef _WIN32
    if (const auto error = start_sockets()) return fail(*error);
#endif
    ip_mreq group{};
    ::inet_pton(AF_INET, "224.0.0.251", &group.imr_multiaddr);
    if (!interface_address.empty() && ::inet_pton(AF_INET, interface_address.c_str(), &group.imr_interface) != 1) {
        return fail(host_error::invalid_input);
    }
    const ScopedSocket socket{new_socket(AF_INET, SOCK_DGRAM)};
    if (socket.descriptor == invalid_native_socket) return socket_failure();
    if (!platform::set_nonblocking(socket.descriptor)) return socket_failure();
    MdnsReceiver receiver(socket.descriptor);
    if (const auto failure = receiver.enable()) return *failure;
    const int enabled = 1;
    if (!set_native_option(socket.descriptor, SOL_SOCKET, SO_REUSEADDR, enabled)) return socket_failure();
#if defined(SO_REUSEPORT) && !defined(_WIN32)
    if (!set_native_option(socket.descriptor, SOL_SOCKET, SO_REUSEPORT, enabled)) return socket_failure();
#endif
    sockaddr_in bound{};
    bound.sin_family = AF_INET;
    bound.sin_port = htons(mdns_port);
    if (::bind(socket.descriptor, reinterpret_cast<sockaddr*>(&bound), sizeof bound) != 0) return socket_failure();
    if (!set_native_option(socket.descriptor, IPPROTO_IP, IP_ADD_MEMBERSHIP, group)) return socket_failure();
    if (!interface_address.empty() && !set_native_option(socket.descriptor, IPPROTO_IP, IP_MULTICAST_IF, group.imr_interface)) {
        return socket_failure();
    }
#ifdef _WIN32
    const DWORD ttl = 255;
#else
    const unsigned char ttl = 255;
#endif
    if (!set_native_option(socket.descriptor, IPPROTO_IP, IP_MULTICAST_TTL, ttl)) return socket_failure();
    auto target = bound;
    target.sin_addr = group.imr_multiaddr;
    const auto sent = ::sendto(socket.descriptor, bytes.data(), static_cast<int>(bytes.size()), 0,
                                reinterpret_cast<sockaddr*>(&target), sizeof target);
    if (sent < 0) return socket_failure();
    if (static_cast<std::size_t>(sent) != bytes.size()) return fail(host_error::other);

    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    HostValue packets;
    std::size_t remaining = 1 << 20;
    while (packets.elements.size() < 256 && remaining >= 12) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        const auto left = window - elapsed;
        if (left <= 0) break;
        const int ready = wait_readable(socket.descriptor, left);
        if (ready == 0) break;
        if (ready < 0) return socket_failure();
        std::string received(std::min<std::size_t>(9000, remaining), '\0');
        if (const auto failure = receiver.receive(group.imr_multiaddr, received)) return *failure;
        if (received.size() < 12 || received[0] != 0 || received[1] != 0 ||
            (static_cast<unsigned char>(received[2]) & 0x80) == 0) {
            continue;
        }
        remaining -= received.size();
        packets.elements.push_back(bytes_value(std::move(received)));
    }
    return HostCompletion::ok(std::move(packets));
}

} // namespace

void add_socket_services(const Adder& add, const OverlapAdder& overlap, StandardServices::Tables& tables,
                         const Capabilities& capabilities) {
    add_socket_transport_services(add, tables, capabilities);
    auto* table = &tables;
    const auto* granted = &capabilities;
    const auto find = [table](const HostValue& id, bool raw = false) -> StandardServices::Tables::Socket* {
        const auto found = table->sockets.find(as_int64(id));
        return found == table->sockets.end() || found->second.raw != raw ? nullptr : &found->second;
    };

    add(signature("neura.net.shutdown", {int64(), int32()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto* socket = find(request.arguments.at(0));
        const auto how = as_int32(request.arguments.at(1));
        if (!connected_stream(socket) || how < 0 || how > 2) return fail(host_error::invalid_input);
#ifdef _WIN32
        constexpr int directions[] = {SD_RECEIVE, SD_SEND, SD_BOTH};
#else
        constexpr int directions[] = {SHUT_RD, SHUT_WR, SHUT_RDWR};
#endif
        if (::shutdown(socket->descriptor, directions[how]) != 0) return socket_failure();
        return HostCompletion::ok();
    });

    const auto local_address = [=](const HostRequest& request, bool raw) {
        if (!granted->net || (raw && !granted->raw)) return denied();
        const auto* socket = find(request.arguments.at(0), raw);
        if (!socket || socket->pipe) return fail(host_error::invalid_input);
        sockaddr_storage bound{};
        AddressLength length = sizeof bound;
        if (::getsockname(socket->descriptor, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
            return socket_failure();
        }
        if (bound.ss_family == AF_UNIX) {
            const auto& address = reinterpret_cast<const sockaddr_un&>(bound);
#ifdef _WIN32
            auto path = std::string(address.sun_path, strnlen(address.sun_path, sizeof address.sun_path));
#else
            auto path = unix_path(address, length);
#endif
            return HostCompletion::ok(tuple_value({bytes_value(std::move(path)), int32_value(0)}));
        }
        char address[NI_MAXHOST] = {};
        char port[NI_MAXSERV] = {};
        if (::getnameinfo(reinterpret_cast<sockaddr*>(&bound), length, address, sizeof address, port, sizeof port,
                          NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
            return fail(host_error::other);
        }
        return HostCompletion::ok(tuple_value({bytes_value(address), int32_value(std::atoi(port))}));
    };
    add(signature("neura.net.local_address", {int64()}, tuple({text(), int32()})),
        [=](const HostRequest& request) { return local_address(request, false); });
    add(signature("neura.net.raw.local_address", {int64()}, tuple({text(), int32()})),
        [=](const HostRequest& request) { return local_address(request, true); });

    add(signature("neura.net.raw.open", {int32(), int32()}, int64()), [=](const HostRequest& request) {
        if (!granted->net || !granted->raw) return denied();
        return raw_open(*table, as_int32(request.arguments.at(0)), as_int32(request.arguments.at(1)));
    });
    add(signature("neura.net.raw.send_to", {int64(), text(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->net || !granted->raw) return denied();
        const auto* socket = find(request.arguments.at(0), true);
        const auto& text = request.arguments.at(1).bytes;
        const auto& bytes = request.arguments.at(2).bytes;
        sockaddr_storage to{};
        AddressLength length = 0;
        if (!socket || !usable_text(text) || !numeric_address(text, 0, to, length) ||
            bytes.size() > 65535) {
            return fail(host_error::invalid_input);
        }
        decltype(::sendto(socket->descriptor, bytes.data(), 0, 0, nullptr, 0)) sent;
        do {
            sent = ::sendto(socket->descriptor, bytes.data(), static_cast<int>(bytes.size()), 0,
                             reinterpret_cast<sockaddr*>(&to), length);
        } while (sent < 0 && platform::last_socket_error().interrupted());
        return sent < 0 ? socket_failure() : HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent)));
    });
    add(signature("neura.net.raw.receive_from", {int64(), int32(), int64()}, address_result()),
        [=](const HostRequest& request) {
            if (!granted->net || !granted->raw) return denied();
            const auto* socket = find(request.arguments.at(0), true);
            const auto max = as_int32(request.arguments.at(1));
            const auto timeout = as_int64(request.arguments.at(2));
            if (!socket || max < 1 || timeout < 0) return fail(host_error::invalid_input);
            return read_datagram(socket->descriptor, std::min<std::size_t>(max, max_read_bytes), timeout);
        });

    const FunctionService::Begin begin_relay = [=](const HostRequest& request)
        -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto from_id = as_int64(request.arguments.at(0));
        const auto to_id = as_int64(request.arguments.at(1));
        const auto* from = find(request.arguments.at(0));
        const auto* to = find(request.arguments.at(1));
        const auto count = as_int64(request.arguments.at(2));
        if (!connected_stream(from) || !connected_stream(to) || from == to || count < 0) {
            return fail(host_error::invalid_input);
        }
        if (count == 0) return HostCompletion::ok(int64_value(0));
        const auto input = hold_socket(from->descriptor);
        const auto output = hold_socket(to->descriptor);
        if (!input || !output) return socket_failure();
        auto interest = std::make_shared<PosixWait::Interest>();
        const auto attempt = [=](bool cancelled) -> std::optional<HostCompletion> {
            if (!table->sockets.contains(from_id) || !table->sockets.contains(to_id)) {
                return fail(host_error::invalid_input);
            }
            if (cancelled) return cancelled_answer();
            NonblockingAttempt source(input->descriptor), target(output->descriptor);
            if (!source.ready || !target.ready) return socket_failure();
            // Probe source first to choose the wait that can make progress.
            char byte;
            const auto available = ::recv(input->descriptor, &byte, 1, MSG_PEEK | nonblocking_flags);
            *interest = {static_cast<std::intptr_t>(input->descriptor), PosixWait::Events::Read};
            if (available < 0) {
                if (platform::last_socket_error().would_block() || platform::last_socket_error().interrupted()) {
                    return std::nullopt;
                }
                return socket_failure();
            }
            if (available == 0) return HostCompletion::ok(int64_value(0));
            *interest = {static_cast<std::intptr_t>(output->descriptor), PosixWait::Events::Write};
            return relay_stream(input->descriptor, output->descriptor, count);
        };
        if (auto done = attempt(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{}, [interest] { return *interest; }};
        operation.attempt = attempt;
        return operation;
    };
    overlap({"neura.net.splice", 2, {int64(), int64(), int64()}, int64(), uint32()},
            [=](const HostRequest& request) { return finish_socket_operation(begin_relay(request)); }, begin_relay);

    const FunctionService::Begin begin_zerocopy = [=](const HostRequest& request)
        -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        const auto* socket = find(request.arguments.at(0));
        if (!connected_stream(socket)) return fail(host_error::invalid_input);
        const auto held = hold_socket(socket->descriptor);
        if (!held) return socket_failure();
        if (request.arguments.at(1).bytes.size() > INT32_MAX) return fail(host_error::invalid_input);
        const auto data = std::make_shared<const std::string>(request.arguments.at(1).bytes);
        const auto attempt = [=](bool cancelled) -> std::optional<HostCompletion> {
            if (!table->sockets.contains(id)) return fail(host_error::invalid_input);
            if (cancelled) return cancelled_answer();
            NonblockingAttempt nonblocking(held->descriptor);
            if (!nonblocking.ready) return socket_failure();
            return send_zerocopy(held->descriptor, *data);
        };
        if (auto done = attempt(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{static_cast<std::intptr_t>(held->descriptor), PosixWait::Events::Write}};
        operation.attempt = attempt;
        return operation;
    };
    overlap(signature("neura.net.send_zerocopy", {int64(), byte_list()}, int32()),
            [=](const HostRequest& request) { return finish_socket_operation(begin_zerocopy(request)); }, begin_zerocopy);

    const FunctionService::Begin begin_segments = [=](const HostRequest& request)
        -> std::variant<HostCompletion, PendingOperation> {
        if (!granted->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        const auto* socket = find(request.arguments.at(0));
        const auto& text = request.arguments.at(1).bytes;
        const auto port = as_int32(request.arguments.at(2));
        const auto& bytes = request.arguments.at(3).bytes;
        const auto segment = as_int32(request.arguments.at(4));
        sockaddr_storage target{};
        AddressLength length = 0;
        if (!socket || !socket->datagram || socket->raw || !usable_text(text) || !valid_port(port) ||
            !numeric_address(text, port, target, length) || segment < 1 || segment > 65507 || bytes.size() > INT32_MAX) {
            return fail(host_error::invalid_input);
        }
        const auto held = hold_socket(socket->descriptor);
        if (!held) return socket_failure();
        const auto data = std::make_shared<const std::string>(bytes);
        auto attempt = [=](bool cancelled) mutable -> std::optional<HostCompletion> {
            if (!table->sockets.contains(id)) return fail(host_error::invalid_input);
            if (cancelled) return cancelled_answer();
            NonblockingAttempt nonblocking(held->descriptor);
            if (!nonblocking.ready) return socket_failure();
            return send_segments(held->descriptor, target, length, *data, segment);
        };
        if (auto done = attempt(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{static_cast<std::intptr_t>(held->descriptor), PosixWait::Events::Write}};
        operation.attempt = attempt;
        return operation;
    };
    overlap(signature("neura.net.udp.send_segmented", {int64(), text(), int32(), byte_list(), int32()}, int32()),
            [=](const HostRequest& request) { return finish_socket_operation(begin_segments(request)); }, begin_segments);
    add(signature("neura.runtime.event_loop", {int32()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        return select_event_loop(as_int32(request.arguments.at(0)));
    });
    add(signature("neura.net.mdns.exchange", {byte_list(), int64(), text()},
                  HostType(Kind::List, std::vector<HostType>{byte_list()})), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        return multicast_exchange(request.arguments.at(0).bytes, as_int64(request.arguments.at(1)),
                                  request.arguments.at(2).bytes);
    });
}

} // namespace neura::nvm::provider
