#include "server/http_listener.hpp"
#include "server/http_socket.hpp"
#include "server/http_tcp.hpp"

#include <array>
#include <atomic>
#include <netdb.h>
#include <netinet/in.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace neura::server::http {
namespace {

class Socket {
public:
    explicit Socket(int descriptor) : descriptor_(descriptor) {}
    Socket(Socket&& other) noexcept : descriptor_(other.release()) {}
    ~Socket() { close(); }

    int get() const { return descriptor_; }
    int release() { return std::exchange(descriptor_, -1); }
    void close() { if (descriptor_ >= 0) ::close(release()); }

private:
    int descriptor_;
};

struct ConnectionSlots {
    std::atomic<std::size_t> used{0};
};

[[noreturn]] void system_failure(const std::string& operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

void validate_limits(const ListenerLimits& limits) {
    using namespace std::chrono_literals;
    constexpr std::uint64_t gibibyte = 1024ull * 1024 * 1024;
    if (limits.connections > 1'000'000 || limits.http.request.head_bytes > 1024 * 1024 ||
        limits.http.request.body_bytes > gibibyte || limits.http.response_bytes > gibibyte ||
        limits.http.head_timeout <= 0ms || limits.http.head_timeout > 10s ||
        limits.http.request_timeout <= 0ms || limits.http.request_timeout > 3600s || limits.handshake_timeout <= 0ms) {
        throw std::invalid_argument("HTTP listener limits exceed the host's supported range");
    }
}

std::vector<TlsContextHandle> load_contexts(std::span<const ListenEndpoint> endpoints) {
    std::vector<TlsContextHandle> contexts;
    contexts.reserve(endpoints.size());
    for (const auto& endpoint : endpoints) {
        if (!endpoint.tls) {
            contexts.emplace_back();
            continue;
        }
#ifdef NEURA_SERVER_TLS_ENABLED
        contexts.push_back(load_tls_context(endpoint.tls->certificate_chain, endpoint.tls->private_key));
#else
        throw std::invalid_argument("this server build has no TLS adapter");
#endif
    }
    return contexts;
}

std::string numeric_peer(const sockaddr_storage& address, socklen_t size) {
    std::array<char, NI_MAXHOST> host{};
    const auto result = ::getnameinfo(reinterpret_cast<const sockaddr*>(&address), size, host.data(), host.size(),
                                      nullptr, 0, NI_NUMERICHOST);
    if (result != 0) throw std::runtime_error("could not read the accepted peer address");
    return host.data();
}

Socket bind_socket(const ListenEndpoint& config) {
    const std::string service = std::to_string(config.port);
    const std::string description = "bind HTTP listener on " + config.address + " port " + service;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    addrinfo* resolved = nullptr;
    if (::getaddrinfo(config.address.c_str(), service.c_str(), &hints, &resolved) != 0) {
        throw std::invalid_argument("HTTP listener address must be a numeric IPv4 or IPv6 address");
    }
    const std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> address(resolved, ::freeaddrinfo);
#ifdef __linux__
    Socket socket(::socket(address->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_TCP));
    if (socket.get() < 0) system_failure("create HTTP listener socket");
#else
    Socket socket(::socket(address->ai_family, SOCK_STREAM, IPPROTO_TCP));
    if (socket.get() < 0 || !socket_io::configure(socket.get())) system_failure("configure HTTP listener socket");
#endif
    const int enabled = 1;
    if (::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) < 0) {
        system_failure("configure HTTP listener address reuse");
    }
    if (address->ai_family == AF_INET6 &&
        ::setsockopt(socket.get(), IPPROTO_IPV6, IPV6_V6ONLY, &enabled, sizeof enabled) < 0) {
        system_failure("configure HTTP listener IPv6 address");
    }
    if (::bind(socket.get(), address->ai_addr, address->ai_addrlen) < 0) system_failure(description);
    return socket;
}

std::uint16_t bound_port(int socket) {
    sockaddr_storage address{};
    socklen_t size = sizeof address;
    if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) < 0) system_failure("read HTTP listener port");
    if (address.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port);
    return ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port);
}

bool retry_accept(int error) {
    if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK || error == ECONNABORTED) return true;
#ifdef __linux__
    // Linux can report pending errors on the accepted connection here.
    return error == ENETDOWN || error == EPROTO || error == ENOPROTOOPT || error == EHOSTDOWN ||
           error == ENONET || error == EHOSTUNREACH || error == EOPNOTSUPP || error == ENETUNREACH;
#else
    return false;
#endif
}

int accept_socket(int listener, sockaddr_storage& address, socklen_t& size) {
#ifdef __linux__
    return ::accept4(listener, reinterpret_cast<sockaddr*>(&address), &size, SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
    return ::accept(listener, reinterpret_cast<sockaddr*>(&address), &size);
#endif
}

} // namespace

struct AcceptedConnection::State {
    State(Socket fd, std::shared_ptr<ConnectionSlots> pool, std::string address,
          TlsContextHandle context, const ListenerLimits& settings)
        : socket(std::move(fd)), slots(std::move(pool)), peer(std::move(address)), tls(std::move(context)), limits(settings) {
        slots->used.fetch_add(1, std::memory_order_relaxed);
    }

    ~State() {
        socket.close();
        slots->used.fetch_sub(1, std::memory_order_relaxed);
    }

    Socket socket;
    std::shared_ptr<ConnectionSlots> slots;
    std::string peer;
    TlsContextHandle tls;
    ListenerLimits limits;
};

struct ListenerSet::State {
    ListenerLimits limits;
    std::vector<ListenEndpoint> configurations;
    std::vector<BoundEndpoint> endpoints;
    std::vector<TlsContextHandle> contexts;
    std::vector<Socket> sockets;
    std::vector<pollfd> poll_entries;
    std::shared_ptr<ConnectionSlots> slots = std::make_shared<ConnectionSlots>();
    std::size_t next = 0;
};

AcceptedConnection::AcceptedConnection(std::unique_ptr<State> state) : state_(std::move(state)) {}
AcceptedConnection::AcceptedConnection(AcceptedConnection&&) noexcept = default;
AcceptedConnection& AcceptedConnection::operator=(AcceptedConnection&&) noexcept = default;
AcceptedConnection::~AcceptedConnection() = default;

std::string_view AcceptedConnection::peer_address() const { return state_ ? std::string_view(state_->peer) : std::string_view{}; }
bool AcceptedConnection::secure() const { return state_ && state_->tls; }

std::size_t AcceptedConnection::response_bytes() const {
    return state_ ? state_->limits.http.response_bytes : ConnectionLimits{}.response_bytes;
}

ServeResult AcceptedConnection::serve(const Responder& respond) && {
    auto state = std::move(state_);
    if (!state) throw std::logic_error("the accepted HTTP connection was already consumed");
#ifdef NEURA_SERVER_TLS_ENABLED
    if (state->tls) {
        const auto end = serve_tls_connection(state->socket.release(), std::move(state->tls), respond,
                                              state->limits.http, state->limits.handshake_timeout);
        return {end.http, end.handshake};
    }
#endif
    return {serve_tcp_connection(state->socket.release(), respond, state->limits.http), std::nullopt};
}

ListenerSet::ListenerSet(std::span<const ListenEndpoint> endpoints, ListenerLimits limits)
    : state_(std::make_unique<State>()) {
    if (endpoints.empty()) throw std::invalid_argument("at least one HTTP listener is required");
    validate_limits(limits);
    auto& state = *state_;
    state.limits = limits;
    state.configurations.assign(endpoints.begin(), endpoints.end());
    state.contexts = load_contexts(endpoints);
    state.sockets.reserve(endpoints.size());
    state.endpoints.reserve(endpoints.size());
    state.poll_entries.reserve(endpoints.size());
    for (const auto& config : endpoints) {
        auto socket = bind_socket(config);
        state.endpoints.push_back({config.address, bound_port(socket.get()), config.tls.has_value()});
        state.poll_entries.push_back({socket.get(), POLLIN, 0});
        state.sockets.push_back(std::move(socket));
    }
    for (const auto& socket : state.sockets) {
        if (::listen(socket.get(), SOMAXCONN) != 0) system_failure("start HTTP listener");
    }
}

ListenerSet::ListenerSet(ListenerSet&&) noexcept = default;
ListenerSet& ListenerSet::operator=(ListenerSet&&) noexcept = default;
ListenerSet::~ListenerSet() = default;

std::span<const BoundEndpoint> ListenerSet::endpoints() const { return state_->endpoints; }

AcceptResult ListenerSet::accept_until(ConnectionClock::time_point deadline) {
    auto& state = *state_;
    for (;;) {
        const int ready = socket_io::poll_until(state.poll_entries, deadline);
        if (ready == 0) return {AcceptStatus::TimedOut, std::nullopt};
        if (ready < 0) system_failure("wait for HTTP listener");
        const auto count = state.poll_entries.size();
        for (std::size_t offset = 0; offset < count; ++offset) {
            const auto index = (state.next + offset) % count;
            const auto& entry = state.poll_entries[index];
            if (entry.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("HTTP listener socket failed");
            if (!(entry.revents & POLLIN)) continue;
            if (ConnectionClock::now() >= deadline) return {AcceptStatus::TimedOut, std::nullopt};

            sockaddr_storage address{};
            socklen_t length = sizeof address;
            Socket accepted(accept_socket(entry.fd, address, length));
            if (accepted.get() < 0) {
                if (retry_accept(errno)) continue;
                system_failure("accept HTTP connection");
            }
            state.next = (index + 1) % count;
            if (state.slots->used.load(std::memory_order_relaxed) >= state.limits.connections) {
                return {AcceptStatus::Refused, std::nullopt};
            }
#ifndef __linux__
            if (!socket_io::configure(accepted.get())) system_failure("configure accepted HTTP socket");
#endif
            auto client = std::make_unique<AcceptedConnection::State>(std::move(accepted), state.slots,
                numeric_peer(address, length), state.contexts[index], state.limits);
            return {AcceptStatus::Accepted, AcceptedConnection(std::move(client))};
        }
    }
}

void ListenerSet::reload_tls() { state_->contexts = load_contexts(state_->configurations); }

} // namespace neura::server::http
