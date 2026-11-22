#include "socket.hpp"

#include "error.hpp"
#include "internal.hpp"

#include <cstddef>
#include <cstring>

#ifdef _WIN32
#include "dirs.hpp"
#include "file.hpp"
#include "windows.hpp"

#include <mutex>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace neura::platform {

std::error_code SocketError::error_code() const noexcept {
    return {code, std::system_category()};
}

std::string SocketError::message() const {
    return error_message(code);
}

#ifdef _WIN32

static_assert(sizeof(PollItem) == sizeof(WSAPOLLFD));
static_assert(offsetof(PollItem, socket) == offsetof(WSAPOLLFD, fd));
static_assert(offsetof(PollItem, events) == offsetof(WSAPOLLFD, events));
static_assert(offsetof(PollItem, revents) == offsetof(WSAPOLLFD, revents));
static_assert(poll_in == POLLIN && poll_out == POLLOUT && poll_error == POLLERR && poll_hangup == POLLHUP &&
              poll_invalid == POLLNVAL);
static_assert(invalid_socket == INVALID_SOCKET);

std::error_code ensure_sockets() noexcept {
    static std::once_flag once;
    static int result = 0;
    std::call_once(once, [] {
        WSADATA data{};
        result = ::WSAStartup(MAKEWORD(2, 2), &data);
    });
    return {result, std::system_category()};
}

bool close_socket(native_socket socket) noexcept {
    return ::closesocket(socket) == 0;
}

bool set_nonblocking(native_socket socket, bool nonblocking) noexcept {
    u_long mode = nonblocking ? 1 : 0;
    return ::ioctlsocket(socket, FIONBIO, &mode) == 0;
}

bool set_no_inherit(native_socket socket) noexcept {
    return ::SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0) != 0;
}

bool SocketError::would_block() const noexcept {
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS;
}

bool SocketError::interrupted() const noexcept {
    return code == WSAEINTR;
}

bool SocketError::connection_reset() const noexcept {
    return code == WSAECONNRESET || code == WSAECONNABORTED || code == WSAENETRESET || code == WSAESHUTDOWN;
}

SocketError last_socket_error() noexcept {
    return {::WSAGetLastError()};
}

int poll(std::span<PollItem> items, int timeout_ms) noexcept {
    return ::WSAPoll(reinterpret_cast<WSAPOLLFD*>(items.data()), static_cast<ULONG>(items.size()), timeout_ms);
}

namespace {

struct Socket {
    native_socket value = invalid_socket;
    Socket() = default;
    explicit Socket(native_socket socket) : value(socket) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket() {
        if (value != invalid_socket) ::closesocket(value);
    }
    native_socket release() {
        const native_socket socket = value;
        value = invalid_socket;
        return socket;
    }
};

std::error_code wsa_error() noexcept {
    return {::WSAGetLastError(), std::system_category()};
}

native_socket stream_socket(int family) noexcept {
    return ::WSASocketW(family, SOCK_STREAM, family == AF_UNIX ? 0 : IPPROTO_TCP, nullptr, 0,
                        WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
}

// An AF_UNIX pair through a listener in a private directory nobody else can
// reach, so the accepted connection is the one this function made.
bool unix_pair(std::array<native_socket, 2>& pair, std::error_code& error) {
    std::error_code ignored;
    const auto temporary = temp_directory(ignored);
    if (ignored) return false;
    const auto directory = temporary / unique_name("neura-pair-");
    const auto name = directory / L"s";
    const auto text = try_to_utf8(name.native());
    sockaddr_un address{};
    if (!text || text->size() >= sizeof address.sun_path) return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, text->data(), text->size());

    Socket listener(stream_socket(AF_UNIX));
    if (listener.value == invalid_socket) return false;
    if (!make_private_directory(directory, error)) return false;
    bool done = false;
    {
        Socket client;
        Socket accepted;
        if (::bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0 &&
            ::listen(listener.value, 1) == 0) {
            client.value = stream_socket(AF_UNIX);
            if (client.value != invalid_socket &&
                ::connect(client.value, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0) {
                accepted.value = ::accept(listener.value, nullptr, nullptr);
                if (accepted.value != invalid_socket && set_no_inherit(accepted.value)) {
                    pair = {accepted.release(), client.release()};
                    done = true;
                }
            }
        }
        if (!done) error = wsa_error();
    }
    ::closesocket(listener.release());
    ::DeleteFileW(name.c_str());
    ::RemoveDirectoryW(directory.c_str());
    return done;
}

// Loopback TCP for systems without AF_UNIX. Another local process could
// connect to the listener first, so the accepted side must receive a random
// token from the connecting side and come from its address.
bool tcp_pair(std::array<native_socket, 2>& pair, std::error_code& error) {
    Socket listener(stream_socket(AF_INET));
    if (listener.value == invalid_socket) {
        error = wsa_error();
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int length = sizeof address;
    BOOL exclusive = TRUE;
    ::setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                 sizeof exclusive);
    if (::bind(listener.value, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 ||
        ::listen(listener.value, 4) != 0 ||
        ::getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        error = wsa_error();
        return false;
    }
    Socket client(stream_socket(AF_INET));
    if (client.value == invalid_socket ||
        ::connect(client.value, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        error = wsa_error();
        return false;
    }
    sockaddr_in local{};
    length = sizeof local;
    if (::getsockname(client.value, reinterpret_cast<sockaddr*>(&local), &length) != 0) {
        error = wsa_error();
        return false;
    }
    const std::string token = unique_name("");
    if (::send(client.value, token.data(), static_cast<int>(token.size()), 0) != static_cast<int>(token.size())) {
        error = wsa_error();
        return false;
    }
    set_nonblocking(listener.value);
    for (int attempt = 0; attempt < 8; ++attempt) {
        PollItem item{listener.value, poll_in, 0};
        if (poll(std::span(&item, 1), 2000) <= 0) break;
        sockaddr_in peer{};
        length = sizeof peer;
        Socket accepted(::accept(listener.value, reinterpret_cast<sockaddr*>(&peer), &length));
        if (accepted.value == invalid_socket) continue;
        if (peer.sin_port != local.sin_port || peer.sin_addr.s_addr != local.sin_addr.s_addr) continue;
        set_nonblocking(accepted.value, false);
        set_no_inherit(accepted.value);
        std::string received(token.size(), '\0');
        std::size_t got = 0;
        while (got < received.size()) {
            PollItem wait{accepted.value, poll_in, 0};
            if (poll(std::span(&wait, 1), 2000) <= 0) break;
            const int count = ::recv(accepted.value, received.data() + got, static_cast<int>(received.size() - got), 0);
            if (count <= 0) break;
            got += static_cast<std::size_t>(count);
        }
        if (received != token) continue;
        BOOL nodelay = TRUE;
        ::setsockopt(accepted.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
        ::setsockopt(client.value, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
        pair = {accepted.release(), client.release()};
        return true;
    }
    error = std::make_error_code(std::errc::connection_refused);
    return false;
}

} // namespace

std::array<native_socket, 2> socket_pair(std::error_code& error) {
    error.clear();
    std::array<native_socket, 2> pair{invalid_socket, invalid_socket};
    if ((error = ensure_sockets())) return pair;
    if (unix_pair(pair, error)) {
        error.clear();
        return pair;
    }
    error.clear();
    if (!tcp_pair(pair, error)) pair = {invalid_socket, invalid_socket};
    return pair;
}

std::optional<process_id> peer_pid(native_socket socket, std::error_code& error) {
    error.clear();
    ULONG pid = 0;
    DWORD bytes = 0;
    if (::WSAIoctl(socket, SIO_AF_UNIX_GETPEERPID, nullptr, 0, &pid, sizeof pid, &bytes, nullptr, nullptr) != 0) {
        error = wsa_error();
        return std::nullopt;
    }
    return static_cast<process_id>(pid);
}

#else // POSIX

static_assert(sizeof(PollItem) == sizeof(pollfd));
static_assert(offsetof(PollItem, socket) == offsetof(pollfd, fd));
static_assert(offsetof(PollItem, events) == offsetof(pollfd, events));
static_assert(offsetof(PollItem, revents) == offsetof(pollfd, revents));
static_assert(poll_in == POLLIN && poll_out == POLLOUT && poll_error == POLLERR && poll_hangup == POLLHUP &&
              poll_invalid == POLLNVAL);

std::error_code ensure_sockets() noexcept {
    return {};
}

bool close_socket(native_socket socket) noexcept {
    return ::close(socket) == 0;
}

bool set_nonblocking(native_socket socket, bool nonblocking) noexcept {
    const int flags = ::fcntl(socket, F_GETFL);
    if (flags < 0) return false;
    const int wanted = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return wanted == flags || ::fcntl(socket, F_SETFL, wanted) == 0;
}

bool set_no_inherit(native_socket socket) noexcept {
    const int flags = ::fcntl(socket, F_GETFD);
    return flags >= 0 && ::fcntl(socket, F_SETFD, flags | FD_CLOEXEC) == 0;
}

bool SocketError::would_block() const noexcept {
    return code == EWOULDBLOCK || code == EAGAIN || code == EINPROGRESS;
}

bool SocketError::interrupted() const noexcept {
    return code == EINTR;
}

bool SocketError::connection_reset() const noexcept {
    return code == ECONNRESET || code == EPIPE || code == ECONNABORTED;
}

SocketError last_socket_error() noexcept {
    return {errno};
}

int poll(std::span<PollItem> items, int timeout_ms) noexcept {
    return ::poll(reinterpret_cast<pollfd*>(items.data()), static_cast<nfds_t>(items.size()), timeout_ms);
}

std::array<native_socket, 2> socket_pair(std::error_code& error) {
    error.clear();
    int ends[2] = {-1, -1};
#ifdef SOCK_CLOEXEC
    const int type = SOCK_STREAM | SOCK_CLOEXEC;
#else
    const int type = SOCK_STREAM;
#endif
    if (::socketpair(AF_UNIX, type, 0, ends) != 0) {
        error = {errno, std::system_category()};
        return {invalid_socket, invalid_socket};
    }
#ifndef SOCK_CLOEXEC
    set_no_inherit(ends[0]);
    set_no_inherit(ends[1]);
#endif
    return {ends[0], ends[1]};
}

std::optional<process_id> peer_pid(native_socket socket, std::error_code& error) {
    error.clear();
#if defined(__linux__)
    ucred credentials{};
    socklen_t length = sizeof credentials;
    if (::getsockopt(socket, SOL_SOCKET, SO_PEERCRED, &credentials, &length) != 0) {
        error = {errno, std::system_category()};
        return std::nullopt;
    }
    return static_cast<process_id>(credentials.pid);
#elif defined(__APPLE__)
    pid_t pid = 0;
    socklen_t length = sizeof pid;
    if (::getsockopt(socket, SOL_LOCAL, LOCAL_PEERPID, &pid, &length) != 0) {
        error = {errno, std::system_category()};
        return std::nullopt;
    }
    return static_cast<process_id>(pid);
#else
    (void)socket;
    error = std::make_error_code(std::errc::not_supported);
    return std::nullopt;
#endif
}

#endif

} // namespace neura::platform
