#include "provider_internal.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

#ifdef _WIN32
#include "platform/socket.hpp"
#include "platform/text.hpp"
#include "platform/windows.hpp"

#include <map>
#include <mstcpip.h>
#include <mutex>
#include <span>
#include <string>
#else
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

// Winsock 2 (W14). The services keep the POSIX contract: the sockets of the
// table are blocking, a pending operation waits for readiness through
// WSAPoll (PosixWait names a SOCKET here) and then makes one non-blocking
// call, and failures map through host_error::from_wsa.

namespace {

HostCompletion wsa_failure() { return fail(host_error::from_wsa(::WSAGetLastError())); }

// Starts Winsock before the first socket call of the process; its failure
// code otherwise.
std::optional<std::uint32_t> start_sockets() {
    if (const auto error = platform::ensure_sockets()) return host_error::from_wsa(error.value());
    return std::nullopt;
}

// GetAddrInfoW failures, as getaddrinfo's: an unknown name is NotFound.
std::uint32_t resolution_error(int code) {
    switch (code) {
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA:
        return host_error::not_found;
    case WSATRY_AGAIN:
        return host_error::timed_out;
    default:
        return host_error::other;
    }
}

struct Addresses {
    ADDRINFOW* list = nullptr;
    ~Addresses() {
        if (list != nullptr) ::FreeAddrInfoW(list);
    }
};

// The error of a failed resolution, or nothing when `out` holds addresses.
// The name goes to GetAddrInfoW as UTF-16, which also encodes an
// internationalized name; text that is not UTF-8 names no host.
std::optional<std::uint32_t> resolve(const std::string& host, std::int32_t port, bool passive, Addresses& out) {
    if (const auto failure = start_sockets()) return failure;
    ADDRINFOW hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (passive) hints.ai_flags = AI_PASSIVE;
    const auto service = std::to_wstring(port);
    std::optional<std::wstring> name;
    if (!(passive && host.empty())) {
        name = platform::try_to_utf16(host);
        if (!name) return host_error::not_found;
    }
    if (const int code = ::GetAddrInfoW(name ? name->c_str() : nullptr, service.c_str(), &hints, &out.list);
        code != 0) {
        return resolution_error(code);
    }
    return std::nullopt;
}

// A stream socket that child processes do not inherit, as FD_CLOEXEC keeps
// a descriptor from them. Overlapped, as socket() makes it, for TransmitFile.
// An IPv6 socket is dual-stack, as Linux and macOS make it by default
// (IPV6_V6ONLY 0): a listener on "::" also accepts IPv4, and a connection
// to an IPv4-mapped address works. Windows turns IPV6_V6ONLY on by default.
NativeSocket new_socket(int family) {
    const auto socket =
        ::WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (socket != invalid_native_socket && family == AF_INET6) {
        const DWORD v6_only = 0;
        ::setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6_only), sizeof v6_only);
    }
    return socket;
}

bool loopback(const sockaddr& address) {
    if (address.sa_family == AF_INET) {
        return (ntohl(reinterpret_cast<const sockaddr_in&>(address).sin_addr.s_addr) >> 24) == 127;
    }
    if (address.sa_family != AF_INET6) return false;
    const auto* bytes = reinterpret_cast<const sockaddr_in6&>(address).sin6_addr.s6_addr;
    static constexpr unsigned char one[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    static constexpr unsigned char mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    return std::memcmp(bytes, one, sizeof one) == 0 || (std::memcmp(bytes, mapped, sizeof mapped) == 0 && bytes[12] == 127);
}

// Windows answers a reset to a SYN by sending the SYN again, so a connection
// to a port that nothing listens on is refused only after about two seconds,
// where POSIX refuses it at once. A loopback connection loses no packets:
// without SYN retransmissions it is refused at once there too, which also
// keeps a name such as localhost, whose ::1 comes first, from waiting before
// it tries 127.0.0.1. Other addresses keep the system's retransmissions.
void refuse_loopback_at_once(NativeSocket socket, const sockaddr& address) {
    if (!loopback(address)) return;
    TCP_INITIAL_RTO_PARAMETERS parameters{};
    parameters.Rtt = TCP_INITIAL_RTO_UNSPECIFIED_RTT;
    parameters.MaxSynRetransmissions = TCP_INITIAL_RTO_NO_SYN_RETRANSMISSIONS;
    DWORD bytes = 0;
    ::WSAIoctl(socket, SIO_TCP_INITIAL_RTO, &parameters, sizeof parameters, nullptr, 0, &bytes, nullptr, nullptr);
}

// Waits for `events` on `socket`. Returns 1 when ready, 0 on timeout, and
// -1 with the Winsock error set on failure. A deadline of zero waits without
// limit.
int wait_for(NativeSocket socket, short events, std::chrono::steady_clock::time_point deadline, bool limited) {
    for (;;) {
        int milliseconds = -1;
        if (limited) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();
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

// Whether `events` are ready on `socket`, without waiting. Hang-up and error
// count as ready, and so does a failed poll: the next call reports them.
bool ready_now(NativeSocket socket, short events) {
    platform::PollItem entry{socket, events, 0};
    return platform::poll(std::span(&entry, 1), 0) != 0;
}

// Connects one socket to `address`, honoring the deadline. Returns the
// socket, or invalid_native_socket with `error` set to an error code.
NativeSocket connect_one(const ADDRINFOW& address, std::chrono::steady_clock::time_point deadline, bool limited,
                         std::uint32_t& error) {
    const auto socket = new_socket(address.ai_family);
    if (socket == invalid_native_socket) {
        error = host_error::from_wsa(::WSAGetLastError());
        return invalid_native_socket;
    }
    const auto give_up = [&](int code) {
        error = host_error::from_wsa(code);
        platform::close_socket(socket);
        return invalid_native_socket;
    };
    refuse_loopback_at_once(socket, *address.ai_addr);
    if (!platform::set_nonblocking(socket, true)) return give_up(::WSAGetLastError());
    if (::connect(socket, address.ai_addr, static_cast<int>(address.ai_addrlen)) != 0) {
        const int code = ::WSAGetLastError();
        if (code != WSAEWOULDBLOCK) return give_up(code);
        const int ready = wait_for(socket, platform::poll_out, deadline, limited);
        if (ready == 0) {
            platform::close_socket(socket);
            error = host_error::timed_out;
            return invalid_native_socket;
        }
        if (ready < 0) return give_up(::WSAGetLastError());
        int failure = 0;
        int length = sizeof failure;
        if (::getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&failure), &length) != 0) {
            return give_up(::WSAGetLastError());
        }
        if (failure != 0) return give_up(failure);
    }
    if (!platform::set_nonblocking(socket, false)) return give_up(::WSAGetLastError());
    return socket;
}

bool valid_port(std::int32_t port) { return port >= 0 && port <= 65535; }

// Closes a socket when the last owner is destroyed: the socket of a
// connection in progress, which no table entry names yet.
struct OwnedSocket {
    explicit OwnedSocket(NativeSocket value) : socket(value) {}
    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;
    ~OwnedSocket() {
        if (socket != invalid_native_socket) platform::close_socket(socket);
    }
    NativeSocket socket;
};

// The sockets that pending operations hold. On POSIX a pending operation
// waits on a descriptor of its own (F_DUPFD_CLOEXEC), so the socket stays
// open if the program closes its ID meanwhile. A SOCKET has no cheap
// duplicate (WSADuplicateSocketW makes a new catalog entry), so a pending
// operation holds a reference instead: closing an ID removes it from the
// table at once, and closesocket waits for the last reference. A held socket
// stays open, so its value names it uniquely in every run of the process.
class SocketHolds {
public:
    static SocketHolds& instance() {
        // Never destroyed: an operation may end during the process's exit.
        static auto* holds = new SocketHolds;
        return *holds;
    }

    void hold(NativeSocket socket) {
        const std::lock_guard lock(mutex_);
        ++entries_[socket].count;
    }

    void release(NativeSocket socket) {
        bool close = false;
        {
            const std::lock_guard lock(mutex_);
            const auto found = entries_.find(socket);
            if (found == entries_.end() || --found->second.count != 0) return;
            close = found->second.closed;
            entries_.erase(found);
        }
        if (close) platform::close_socket(socket);
    }

    // Closes the table's reference: at once when nothing else holds the
    // socket. False, with the Winsock error set, when closesocket failed.
    bool close(NativeSocket socket) {
        {
            const std::lock_guard lock(mutex_);
            const auto found = entries_.find(socket);
            if (found != entries_.end()) {
                found->second.closed = true;
                return true;
            }
        }
        return ::closesocket(socket) == 0;
    }

private:
    struct Entry {
        std::size_t count = 0;
        // The table no longer names the socket.
        bool closed = false;
    };
    std::mutex mutex_;
    std::map<NativeSocket, Entry> entries_;
};

// A pending operation's reference to a socket of the table.
struct HeldSocket {
    explicit HeldSocket(NativeSocket value) : socket(value) { SocketHolds::instance().hold(socket); }
    HeldSocket(const HeldSocket&) = delete;
    HeldSocket& operator=(const HeldSocket&) = delete;
    ~HeldSocket() { SocketHolds::instance().release(socket); }
    NativeSocket socket;
};
using Held = std::shared_ptr<const HeldSocket>;

std::intptr_t wait_descriptor(NativeSocket socket) { return static_cast<std::intptr_t>(socket); }

// What a call made in non-blocking mode returned, and its Winsock error.
struct Attempted {
    long long result = -1;
    int error = 0;
};

// Winsock has no MSG_DONTWAIT, and a socket's mode cannot be read back. The
// sockets of the table stay blocking, as on POSIX, so a pending operation
// switches its socket to non-blocking mode for its one call and back.
template <class Call>
Attempted without_blocking(NativeSocket socket, Call call) {
    Attempted attempted;
    if (!platform::set_nonblocking(socket, true)) {
        attempted.error = ::WSAGetLastError();
        return attempted;
    }
    attempted.result = call();
    attempted.error = attempted.result < 0 ? ::WSAGetLastError() : 0;
    platform::set_nonblocking(socket, false);
    return attempted;
}

// An accepted socket: not inherited, and blocking even when its listener
// was in non-blocking mode for the call, whose mode it takes.
void prepare_accepted(NativeSocket socket) {
    platform::set_no_inherit(socket);
    platform::set_nonblocking(socket, false);
}

// recv() of up to buffer.size() bytes. A datagram longer than the buffer
// fills it and the rest is dropped, as on POSIX, where Winsock reports
// WSAEMSGSIZE.
long long receive(NativeSocket socket, std::string& buffer) {
    const int count = ::recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (count == SOCKET_ERROR && ::WSAGetLastError() == WSAEMSGSIZE) return static_cast<long long>(buffer.size());
    return count;
}

// A connection in progress, trying the addresses of the name in order.
struct Connecting {
    Addresses addresses;
    const ADDRINFOW* next = nullptr;
    std::shared_ptr<OwnedSocket> socket;
    std::uint32_t error = host_error::not_found;
};

enum class Started { Connected, Waiting, Failed };

// Starts connections to the remaining addresses until one has connected or
// is in progress. Failed when none is left, with the last error in `error`.
Started start_next(Connecting& connecting) {
    while (connecting.next != nullptr) {
        const auto& address = *connecting.next;
        connecting.next = connecting.next->ai_next;
        const auto socket = new_socket(address.ai_family);
        if (socket == invalid_native_socket) {
            connecting.error = host_error::from_wsa(::WSAGetLastError());
            continue;
        }
        refuse_loopback_at_once(socket, *address.ai_addr);
        if (!platform::set_nonblocking(socket, true)) {
            connecting.error = host_error::from_wsa(::WSAGetLastError());
            platform::close_socket(socket);
            continue;
        }
        const bool connected = ::connect(socket, address.ai_addr, static_cast<int>(address.ai_addrlen)) == 0;
        const int code = connected ? 0 : ::WSAGetLastError();
        if (connected || code == WSAEWOULDBLOCK) {
            connecting.socket = std::make_shared<OwnedSocket>(socket);
            return connected ? Started::Connected : Started::Waiting;
        }
        connecting.error = host_error::from_wsa(code);
        platform::close_socket(socket);
    }
    return Started::Failed;
}

} // namespace

std::optional<SocketReference> hold_socket(NativeSocket descriptor) {
    auto owner = std::make_shared<HeldSocket>(descriptor);
    return SocketReference{descriptor, std::move(owner)};
}

void add_net_services(const Adder& add, const OverlapAdder& overlap, StandardServices::Tables& tables,
                      const Capabilities& capabilities) {
    auto* sockets = &tables;
    const auto* granted = &capabilities;
    const auto lookup = [sockets](const HostValue& id, bool listener) -> StandardServices::Tables::Socket* {
        const auto found = sockets->sockets.find(as_int64(id));
        if (found == sockets->sockets.end() || found->second.raw || found->second.listener != listener) return nullptr;
        return &found->second;
    };
    const auto remember = [sockets](NativeSocket socket, bool listener) {
        const auto id = sockets->next_socket++;
        sockets->sockets[id] = {socket, listener};
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
            const auto socket = connect_one(*address, deadline, limited, error);
            if (socket != invalid_native_socket) return remember(socket, false);
            if (error == host_error::timed_out) break;
        }
        return fail(error);
    };

    // SO_EXCLUSIVEADDRUSE where POSIX uses SO_REUSEADDR, which on Windows
    // would let another socket take over the bound port. An IPv6 listener is
    // dual-stack (new_socket), so the empty host, for which GetAddrInfoW
    // gives "::" first, and an explicit "::" also accept IPv4.
    const auto listen = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& host = request.arguments.at(0).bytes;
        const auto port = as_int32(request.arguments.at(1));
        if (host.find('\0') != std::string::npos || !valid_port(port)) return fail(host_error::invalid_input);
        if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
        Addresses addresses;
        if (const auto failure = resolve(host, port, true, addresses)) return fail(*failure);
        std::uint32_t error = host_error::not_found;
        for (const auto* address = addresses.list; address != nullptr; address = address->ai_next) {
            const auto socket = new_socket(address->ai_family);
            if (socket == invalid_native_socket) {
                error = host_error::from_wsa(::WSAGetLastError());
                continue;
            }
            const BOOL exclusive = TRUE;
            ::setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                         sizeof exclusive);
            if (request.arguments.size() == 4) {
                if (const auto failure = set_socket_options(socket, request.arguments.at(2), request.arguments.at(3))) {
                    platform::close_socket(socket);
                    error = *failure;
                    continue;
                }
            }
            if (::bind(socket, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0 &&
                ::listen(socket, 128) == 0) {
                return remember(socket, true);
            }
            error = host_error::from_wsa(::WSAGetLastError());
            platform::close_socket(socket);
        }
        return fail(error);
    };
    add(signature("neura.net.listen", {text(), int32()}, int64()), listen);
    add({"neura.net.listen", 3, {text(), int32(), HostType(Kind::List, std::vector<HostType>{int32()}),
                                  HostType(Kind::List, std::vector<HostType>{int64()})}, int64(), uint32()}, listen);

    add(signature("neura.net.local_port", {int64()}, int32()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto found = sockets->sockets.find(as_int64(request.arguments.at(0)));
        if (found == sockets->sockets.end() || found->second.raw) return fail(host_error::invalid_input);
        sockaddr_storage address{};
        int length = sizeof address;
        if (::getsockname(found->second.descriptor, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            return wsa_failure();
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
            const auto socket = ::accept(listener->descriptor, nullptr, nullptr);
            if (socket != invalid_native_socket) {
                prepare_accepted(socket);
                return remember(socket, false);
            }
            // WSAECONNRESET: the peer gave up before the accept, which POSIX
            // reports as ECONNABORTED.
            const int code = ::WSAGetLastError();
            if (code != WSAEINTR && code != WSAECONNRESET) return fail(host_error::from_wsa(code));
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
            const int ready = wait_for(stream->descriptor, platform::poll_in, deadline, true);
            if (ready == 0) return fail(host_error::timed_out);
            if (ready < 0) return wsa_failure();
        }
        std::string buffer(std::min(static_cast<std::size_t>(max), max_read_bytes), '\0');
        for (;;) {
            const auto count = receive(stream->descriptor, buffer);
            if (count >= 0) {
                buffer.resize(static_cast<std::size_t>(count));
                return HostCompletion::ok(bytes_value(std::move(buffer)));
            }
            if (::WSAGetLastError() != WSAEINTR) return wsa_failure();
        }
    };

    const auto write_now_signature = signature("neura.net.write", {int64(), byte_list()}, int32());
    const FunctionService::Handler write_now = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = lookup(request.arguments.at(0), false);
        const auto& bytes = request.arguments.at(1).bytes;
        if (stream == nullptr) return fail(host_error::invalid_input);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = static_cast<int>(std::min<std::size_t>(bytes.size(), INT32_MAX));
        for (;;) {
            const int count = ::send(stream->descriptor, bytes.data(), size, 0);
            if (count >= 0) return HostCompletion::ok(int32_value(count));
            if (::WSAGetLastError() != WSAEINTR) return wsa_failure();
        }
    };

    // The overlapping forms (Concurrency.md section 10.4), as on POSIX: each
    // starts its operation without blocking and returns its completion when
    // it finished at once, and a pending operation holds its socket.
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
                    if (!ready_now(connecting->socket->socket, platform::poll_out)) {
                        if (cancelled) {
                            connecting->socket.reset();
                            return cancelled_answer();
                        }
                        return std::nullopt;
                    }
                    int failure = 0;
                    int length = sizeof failure;
                    if (::getsockopt(connecting->socket->socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&failure),
                                     &length) != 0 ||
                        failure != 0) {
                        connecting->error = host_error::from_wsa(failure != 0 ? failure : ::WSAGetLastError());
                        connecting->socket.reset();
                    } else {
                        const auto socket = std::exchange(connecting->socket->socket, invalid_native_socket);
                        connecting->socket.reset();
                        if (!platform::set_nonblocking(socket, false)) {
                            const auto error = host_error::from_wsa(::WSAGetLastError());
                            platform::close_socket(socket);
                            return fail(error);
                        }
                        if (sockets->sockets.size() >= max_open_ids) {
                            platform::close_socket(socket);
                            return fail(host_error::other);
                        }
                        return remember(socket, false);
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
        operation.wait_source = PosixWait{{}, [connecting] {
            return PosixWait::Interest{connecting->socket ? wait_descriptor(connecting->socket->socket) : -1,
                                       PosixWait::Events::Write};
        }};
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
        const auto held = std::make_shared<const HeldSocket>(listener->descriptor);
        const auto accept_once = [=](bool cancelled) -> std::optional<HostCompletion> {
            const auto found = sockets->sockets.find(id);
            // The program closed the listener while the request was pending.
            if (found == sockets->sockets.end() || !found->second.listener) return fail(host_error::invalid_input);
            if (sockets->sockets.size() >= max_open_ids) return fail(host_error::other);
            if (ready_now(held->socket, platform::poll_in)) {
                NativeSocket socket = invalid_native_socket;
                const auto attempted = without_blocking(held->socket, [&] {
                    socket = ::accept(held->socket, nullptr, nullptr);
                    return socket == invalid_native_socket ? -1LL : 0LL;
                });
                if (socket != invalid_native_socket) {
                    prepare_accepted(socket);
                    return remember(socket, false);
                }
                if (attempted.error != WSAEWOULDBLOCK && attempted.error != WSAECONNRESET &&
                    attempted.error != WSAEINTR) {
                    return fail(host_error::from_wsa(attempted.error));
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = accept_once(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{wait_descriptor(held->socket), PosixWait::Events::Read}};
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
        const auto held = std::make_shared<const HeldSocket>(stream->descriptor);
        const auto count = std::min(static_cast<std::size_t>(max), max_read_bytes);
        const auto read_once = [=](bool cancelled) -> std::optional<HostCompletion> {
            const auto found = sockets->sockets.find(id);
            if (found == sockets->sockets.end() || found->second.listener) return fail(host_error::invalid_input);
            if (ready_now(held->socket, platform::poll_in)) {
                std::string buffer(count, '\0');
                for (;;) {
                    const auto received = without_blocking(held->socket, [&] { return receive(held->socket, buffer); });
                    if (received.result >= 0) {
                        buffer.resize(static_cast<std::size_t>(received.result));
                        return HostCompletion::ok(bytes_value(std::move(buffer)));
                    }
                    if (received.error == WSAEINTR) continue;
                    if (received.error == WSAEWOULDBLOCK) break;
                    return fail(host_error::from_wsa(received.error));
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = read_once(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{wait_descriptor(held->socket), PosixWait::Events::Read}};
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
        const auto held = std::make_shared<const HeldSocket>(stream->descriptor);
        const auto data = std::make_shared<const std::string>(bytes);
        // Writes what the socket takes now. A write that stores some bytes
        // completes with that count; one that can store none waits, and a
        // cancel then answers Cancelled with nothing written.
        const auto write_once = [=](bool cancelled) -> std::optional<HostCompletion> {
            const auto found = sockets->sockets.find(id);
            if (found == sockets->sockets.end() || found->second.listener) return fail(host_error::invalid_input);
            if (ready_now(held->socket, platform::poll_out)) {
                const auto size = static_cast<int>(std::min<std::size_t>(data->size(), INT32_MAX));
                for (;;) {
                    const auto sent = without_blocking(held->socket, [&] {
                        return static_cast<long long>(::send(held->socket, data->data(), size, 0));
                    });
                    if (sent.result >= 0) return HostCompletion::ok(int32_value(static_cast<std::int32_t>(sent.result)));
                    if (sent.error == WSAEINTR) continue;
                    if (sent.error == WSAEWOULDBLOCK) break;
                    return fail(host_error::from_wsa(sent.error));
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = write_once(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{wait_descriptor(held->socket), PosixWait::Events::Write}};
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
        if (found->second.raw && !granted->raw) return denied();
        const auto socket = found->second.descriptor;
        sockets->sockets.erase(found);
        if (!SocketHolds::instance().close(socket)) return wsa_failure();
        return HostCompletion::ok();
    });
}

void close_sockets(StandardServices::Tables& tables) {
    for (const auto& [id, socket] : tables.sockets) SocketHolds::instance().close(socket.descriptor);
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

bool drain_zerocopy_notices(int descriptor);

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
        if (ready > 0 && (entry.revents & POLLERR) != 0) {
            if (!drain_zerocopy_notices(descriptor)) {
                if ((entry.revents & ~POLLERR) != 0) return ready;
                continue;
            }
            entry.revents = 0;
            const int checked = ::poll(&entry, 1, 0);
            if (checked > 0) return checked;
            if (checked < 0 && errno != EINTR) return checked;
            // A completion notification alone is not stream data. Wait
            // again under the original deadline rather than blocking recv.
            continue;
        }
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

// SO_ZEROCOPY completion records make poll report POLLERR even when no
// payload is readable. Consume them at the readiness check, including when
// the application never sends again. These sockets do not enable IP_RECVERR
// or timestamp queues: the error queue here belongs to SO_ZEROCOPY. Real
// socket failures remain available through SO_ERROR and the next I/O call.
// Returns false when the bounded drain leaves the status of the queue unknown.
bool drain_zerocopy_notices(int descriptor) {
#if defined(__linux__) && defined(SO_ZEROCOPY)
    int enabled = 0;
    socklen_t length = sizeof enabled;
    if (::getsockopt(descriptor, SOL_SOCKET, SO_ZEROCOPY, &enabled, &length) != 0 || !enabled) return true;
    alignas(cmsghdr) std::array<char, 512> control{};
    msghdr notice{};
    notice.msg_control = control.data();
    // Bound one scheduling check even if another sender keeps producing
    // completions. Remaining records trigger another readiness check.
    for (int count = 0; count < 64; ++count) {
        notice.msg_controllen = control.size();
        notice.msg_flags = 0;
        if (::recvmsg(descriptor, &notice, MSG_ERRQUEUE | MSG_DONTWAIT) < 0) return errno != EINTR;
    }
    return false;
#else
    (void)descriptor;
    return true;
#endif
}

// Whether `events` are ready on `descriptor`, without waiting. Hang-up and
// error count as ready: the next call reports them.
bool ready_now(int descriptor, short events) {
    pollfd entry{descriptor, events, 0};
    for (;;) {
        const int ready = ::poll(&entry, 1, 0);
        if (ready >= 0) {
            if ((entry.revents & POLLERR) != 0) {
                if (!drain_zerocopy_notices(descriptor)) return (entry.revents & ~POLLERR) != 0;
                // Recheck after draining: a completion alone must not keep
                // a pending read runnable. HUP and real errors remain ready.
                entry.revents = 0;
                const int checked = ::poll(&entry, 1, 0);
                return checked < 0 || entry.revents != 0;
            }
            return ready > 0;
        }
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

std::optional<SocketReference> hold_socket(NativeSocket descriptor) {
    auto owner = duplicate(descriptor);
    if (!owner) return std::nullopt;
    return SocketReference{owner->fd, std::move(owner)};
}

void add_net_services(const Adder& add, const OverlapAdder& overlap, StandardServices::Tables& tables,
                      const Capabilities& capabilities) {
    auto* sockets = &tables;
    const auto* granted = &capabilities;
    const auto lookup = [sockets](const HostValue& id, bool listener) -> StandardServices::Tables::Socket* {
        const auto found = sockets->sockets.find(as_int64(id));
        if (found == sockets->sockets.end() || found->second.raw || found->second.listener != listener) return nullptr;
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

    const auto listen = [=](const HostRequest& request) {
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
            const int one = 1;
            ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (request.arguments.size() == 4) {
                if (const auto failure = set_socket_options(descriptor, request.arguments.at(2), request.arguments.at(3))) {
                    ::close(descriptor);
                    error = *failure;
                    continue;
                }
            }
            if (::bind(descriptor, address->ai_addr, address->ai_addrlen) == 0 && ::listen(descriptor, 128) == 0) {
                return remember(descriptor, true);
            }
            error = host_error::from_errno(errno);
            ::close(descriptor);
        }
        return fail(error);
    };
    add(signature("neura.net.listen", {text(), int32()}, int64()), listen);
    add({"neura.net.listen", 3, {text(), int32(), HostType(Kind::List, std::vector<HostType>{int32()}),
                                  HostType(Kind::List, std::vector<HostType>{int64()})}, int64(), uint32()}, listen);

    add(signature("neura.net.local_port", {int64()}, int32()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto found = sockets->sockets.find(as_int64(request.arguments.at(0)));
        if (found == sockets->sockets.end() || found->second.raw) return fail(host_error::invalid_input);
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
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        const auto budget = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(timeout));
        const auto deadline = started + std::min(budget, Clock::time_point::max() - started);
        std::string buffer(std::min(static_cast<std::size_t>(max), max_read_bytes), '\0');
        for (;;) {
            if (timeout > 0) {
                const int ready = wait_for(stream->descriptor, POLLIN, deadline, true);
                if (ready == 0) return fail(host_error::timed_out);
                if (ready < 0) return errno_failure();
            }
            const auto count = ::recv(stream->descriptor, buffer.data(), buffer.size(), timeout > 0 ? MSG_DONTWAIT : 0);
            if (count >= 0) {
                buffer.resize(static_cast<std::size_t>(count));
                return HostCompletion::ok(bytes_value(std::move(buffer)));
            }
            if (errno == EINTR || (timeout > 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) continue;
            return errno_failure();
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
            if (!retry_send(errno)) return errno_failure();
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
        operation.wait_source = PosixWait{{}, [connecting] {
            return PosixWait::Interest{connecting->socket ? connecting->socket->fd : -1, PosixWait::Events::Write};
        }};
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
        operation.wait_source = PosixWait{{held->fd, PosixWait::Events::Read}};
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
        operation.wait_source = PosixWait{{held->fd, PosixWait::Events::Read}};
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
                    if (retry_send(errno)) continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    return errno_failure();
                }
            }
            if (cancelled) return cancelled_answer();
            return std::nullopt;
        };
        if (auto done = write_once(false)) return *done;
        PendingOperation operation;
        operation.wait_source = PosixWait{{held->fd, PosixWait::Events::Write}};
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
        if (found->second.raw && !granted->raw) return denied();
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
