// General QUIC connections for Std.Net.Quic (docs/runtime/NVM.md, "QUIC
// services"), on the pinned OpenSSL 4.1 QUIC stack, in builds with the TLS
// provider (NEURA_NVM_TLS). Elsewhere every call with the net capability
// answers Unsupported (13).
//
// This is milestone A1-0 of lane net-a (lead RULINGS-03): the slice that
// shows the P1 model works. Connection and stream state live here; the
// library holds immutable handles of IDs that any task may use.
//
// - IDs carry a generation: a slot freed by close and reused by a later
//   endpoint or connection gets a new generation, so the old ID never reaches
//   the new object. An unknown or stale ID answers NotFound (1), which the
//   QUIC library reports as a closed handle.
// - At most one operation per direction is pending: one accept per endpoint,
//   one accept_stream per connection, and on each stream one read and one
//   write. A second one answers Busy (17) at once and is never queued.
// - Every waiting call overlaps and can be cancelled. Its wait source is the
//   UDP socket in the direction OpenSSL asks for; its wake-up is OpenSSL's
//   next timer (SSL_get_event_timeout), so retransmissions and the idle
//   timer run although no datagram arrives.
// - Live endpoints and connections are serviced on every driver check
//   through BackgroundWork, whether or not a call is pending on them, so data
//   a completed write accepted is always sent and acknowledgments go out.
// - Nothing calls SSL_CTX_set_max_early_data: with it set on the server,
//   0 of 3 resumptions succeeded in the lane's probe (quic_probe3), against
//   3 of 3 without it. Early data is not offered.
#include "provider_internal.hpp"

#include "platform/socket.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#ifdef NEURA_NVM_TLS
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <openssl/err.h>
#include <openssl/quic.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include "tls_material.hpp"
#endif

namespace neura::nvm::provider {

#if defined(NEURA_NVM_TLS) && !defined(OPENSSL_NO_QUIC)
namespace {

using SteadyClock = std::chrono::steady_clock;
constexpr std::int64_t none = PendingOperation::no_deadline;
constexpr std::int32_t max_read_bytes = 1 << 20;
// Slot indices take the low 24 bits of an ID and the generation the rest.
constexpr std::int64_t slot_bits = 24;
constexpr std::size_t max_slots = (std::size_t{1} << slot_bits) - 1;

// Peer-reported stream and connection endings, carried in the success value
// of stream_read and stream_write so that their application codes reach the
// library (a host binding's error is a bare uint32).
enum class Ending : std::int32_t { None = 0, StreamReset = 1, PeerClosed = 2, TransportClosed = 3 };

struct Ssl {
    SSL* ssl = nullptr;
    explicit Ssl(SSL* owned = nullptr) : ssl(owned) {}
    Ssl(const Ssl&) = delete;
    Ssl& operator=(const Ssl&) = delete;
    ~Ssl() { if (ssl) SSL_free(ssl); }
};

struct Stream {
    std::unique_ptr<Ssl> ssl;
    bool reading = false;
    bool writing = false;
};

struct Endpoint {
    platform::native_socket socket = platform::invalid_socket;
    SSL_CTX* context = nullptr;
    std::unique_ptr<Ssl> listener;
    std::string alpn_wire;
    bool accepting = false;
    ~Endpoint() {
        listener.reset();
        if (context) SSL_CTX_free(context);
        if (socket != platform::invalid_socket) BIO_closesocket(static_cast<int>(socket));
    }
};

struct Connection {
    // A client connection owns its context and UDP socket; a server one
    // shares the endpoint's.
    SSL_CTX* context = nullptr;
    platform::native_socket socket = platform::invalid_socket;
    std::int64_t endpoint = 0;
    std::unique_ptr<Ssl> ssl;
    std::map<std::uint64_t, Stream> streams;
    std::string alpn;
    bool accepting = false;
    ~Connection() {
        streams.clear();
        ssl.reset();
        if (context) SSL_CTX_free(context);
        if (socket != platform::invalid_socket) BIO_closesocket(static_cast<int>(socket));
    }
};

using Object = std::variant<std::unique_ptr<Endpoint>, std::unique_ptr<Connection>>;

// Generation-checked IDs: id = generation << 24 | (slot + 1).
class Table {
public:
    std::optional<std::int64_t> insert(Object object) {
        std::size_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            if (slots_.size() >= max_slots || slots_.size() >= max_open_ids) return std::nullopt;
            index = slots_.size();
            slots_.emplace_back();
        }
        auto& slot = slots_[index];
        ++slot.generation;
        slot.object = std::move(object);
        return static_cast<std::int64_t>(slot.generation) << slot_bits | static_cast<std::int64_t>(index + 1);
    }

    template<typename T> T* find(std::int64_t id) {
        auto* slot = live(id);
        if (!slot) return nullptr;
        auto* held = std::get_if<std::unique_ptr<T>>(&*slot->object);
        return held ? held->get() : nullptr;
    }

    bool erase(std::int64_t id) {
        auto* slot = live(id);
        if (!slot) return false;
        slot->object.reset();
        free_.push_back(static_cast<std::size_t>((id & ((std::int64_t{1} << slot_bits) - 1)) - 1));
        return true;
    }

    template<typename F> void each(F&& visit) {
        for (auto& slot : slots_) {
            if (slot.object) std::visit([&](auto& held) { visit(*held); }, *slot.object);
        }
    }

    // The live connections accepted from `endpoint`.
    std::vector<std::int64_t> children_of(std::int64_t endpoint) {
        std::vector<std::int64_t> children;
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            const auto& slot = slots_[i];
            if (!slot.object) continue;
            const auto* connection = std::get_if<std::unique_ptr<Connection>>(&*slot.object);
            if (connection && (*connection)->endpoint == endpoint) {
                children.push_back(static_cast<std::int64_t>(slot.generation) << slot_bits |
                                   static_cast<std::int64_t>(i + 1));
            }
        }
        return children;
    }

    void clear() {
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].object) {
                slots_[i].object.reset();
                free_.push_back(i);
            }
        }
    }

private:
    struct Slot {
        std::uint64_t generation = 0;
        std::optional<Object> object;
    };

    Slot* live(std::int64_t id) {
        if (id <= 0) return nullptr;
        const auto index = (id & ((std::int64_t{1} << slot_bits) - 1)) - 1;
        const auto generation = static_cast<std::uint64_t>(id >> slot_bits);
        if (index < 0 || static_cast<std::size_t>(index) >= slots_.size()) return nullptr;
        auto& slot = slots_[static_cast<std::size_t>(index)];
        if (!slot.object || slot.generation != generation) return nullptr;
        return &slot;
    }

    std::vector<Slot> slots_;
    std::vector<std::size_t> free_;
};

std::int64_t nanos_until_event(SSL* ssl) {
    timeval timer{};
    int infinite = 1;
    if (!ssl || SSL_get_event_timeout(ssl, &timer, &infinite) != 1 || infinite) return -1;
    return static_cast<std::int64_t>(timer.tv_sec) * 1'000'000'000 + static_cast<std::int64_t>(timer.tv_usec) * 1'000;
}

PosixWait::Interest interest_of(SSL* ssl, platform::native_socket socket) {
    if (!ssl || socket == platform::invalid_socket) return {};
    const bool read = SSL_net_read_desired(ssl) == 1;
    const bool write = SSL_net_write_desired(ssl) == 1;
    const auto events = read && write ? PosixWait::Events::ReadWrite
                       : write        ? PosixWait::Events::Write
                                      : PosixWait::Events::Read;
    return {static_cast<std::intptr_t>(socket), events};
}

struct QuicState final : BackgroundWork {
    Table objects;

    std::vector<PosixWait::Interest> interests() override {
        std::vector<PosixWait::Interest> out;
        objects.each([&](auto& object) { out.push_back(interest(object)); });
        return out;
    }

    std::int64_t wake_at(std::int64_t now) override {
        auto earliest = none;
        objects.each([&](auto& object) {
            const auto nanos = nanos_until_event(handle(object));
            if (nanos >= 0) earliest = std::min(earliest, now + nanos);
        });
        return earliest;
    }

    void run() override {
        objects.each([](auto& object) {
            if (auto* ssl = handle(object)) static_cast<void>(SSL_handle_events(ssl));
        });
    }

    static SSL* handle(Endpoint& endpoint) { return endpoint.listener ? endpoint.listener->ssl : nullptr; }
    static SSL* handle(Connection& connection) { return connection.ssl ? connection.ssl->ssl : nullptr; }

    PosixWait::Interest interest(Endpoint& endpoint) { return interest_of(handle(endpoint), endpoint.socket); }
    PosixWait::Interest interest(Connection& connection) {
        return interest_of(handle(connection), socket_of(connection));
    }

    platform::native_socket socket_of(Connection& connection) {
        if (connection.socket != platform::invalid_socket) return connection.socket;
        auto* endpoint = objects.find<Endpoint>(connection.endpoint);
        return endpoint ? endpoint->socket : platform::invalid_socket;
    }
};

std::uint32_t socket_error() {
#ifdef _WIN32
    return host_error::from_wsa(platform::last_socket_error().code);
#else
    return host_error::from_errno(platform::last_socket_error().code);
#endif
}

std::unique_ptr<BIO_ADDR, decltype(&BIO_ADDR_free)> numeric_address(const std::string& host, std::int32_t port) {
    std::unique_ptr<BIO_ADDR, decltype(&BIO_ADDR_free)> address(BIO_ADDR_new(), BIO_ADDR_free);
    in_addr ipv4{};
    in6_addr ipv6{};
    const auto network_port = htons(static_cast<std::uint16_t>(port));
    if (address && ::inet_pton(AF_INET, host.c_str(), &ipv4) == 1 &&
        BIO_ADDR_rawmake(address.get(), AF_INET, &ipv4, sizeof ipv4, network_port) == 1) return address;
    if (address && ::inet_pton(AF_INET6, host.c_str(), &ipv6) == 1 &&
        BIO_ADDR_rawmake(address.get(), AF_INET6, &ipv6, sizeof ipv6, network_port) == 1) return address;
    address.reset();
    return address;
}

// ALPN protocols in wire form; empty when a name is empty or too long, or
// when there is none (QUIC requires ALPN).
std::string alpn_wire(const HostValue& names) {
    std::string wire;
    for (const auto& name : names.elements) {
        if (name.bytes.empty() || name.bytes.size() > 255) return {};
        wire.push_back(static_cast<char>(name.bytes.size()));
        wire += name.bytes;
    }
    return wire;
}

std::string selected_alpn(SSL* ssl) {
    const unsigned char* selected = nullptr;
    unsigned int length = 0;
    SSL_get0_alpn_selected(ssl, &selected, &length);
    return selected ? std::string(reinterpret_cast<const char*>(selected), length) : std::string();
}

int select_alpn(SSL*, const unsigned char** out, unsigned char* out_length, const unsigned char* offered,
                unsigned int offered_length, void* argument) {
    const auto& wire = static_cast<Endpoint*>(argument)->alpn_wire;
    unsigned char* chosen = nullptr;
    if (SSL_select_next_proto(&chosen, out_length, reinterpret_cast<const unsigned char*>(wire.data()),
                              static_cast<unsigned int>(wire.size()), offered, offered_length) !=
        OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    *out = chosen;
    return SSL_TLSEXT_ERR_OK;
}

bool verify_identity(SSL* ssl, const std::string& name) {
    auto* parameters = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (numeric_address(name, 0)) return X509_VERIFY_PARAM_set1_ip_asc(parameters, name.c_str()) == 1;
    return SSL_set_tlsext_host_name(ssl, name.c_str()) == 1 && SSL_set1_dnsname(ssl, name.c_str()) == 1;
}

// The code for a failed SSL call on `ssl`, part of `connection`.
std::uint32_t failure_of(SSL* connection) {
    if (connection && SSL_get_verify_result(connection) != X509_V_OK) return host_error::permission_denied;
    return host_error::connection_reset;
}

// How a stream or connection ended, from the peer's side, as [kind, code].
std::pair<Ending, std::int64_t> ending_of(SSL* connection, SSL* stream, bool reading) {
    const int state = reading ? SSL_get_stream_read_state(stream) : SSL_get_stream_write_state(stream);
    if (state == SSL_STREAM_STATE_RESET_REMOTE) {
        std::uint64_t code = 0;
        const int known = reading ? SSL_get_stream_read_error_code(stream, &code)
                                  : SSL_get_stream_write_error_code(stream, &code);
        return {Ending::StreamReset, known == 1 ? static_cast<std::int64_t>(code) : -1};
    }
    SSL_CONN_CLOSE_INFO info{};
    if (SSL_get_conn_close_info(connection, &info, sizeof info) == 1) {
        const bool transport = (info.flags & SSL_CONN_CLOSE_FLAG_TRANSPORT) != 0;
        return {transport ? Ending::TransportClosed : Ending::PeerClosed, static_cast<std::int64_t>(info.error_code)};
    }
    return {Ending::None, -1};
}

HostValue ending_value(HostValue first, Ending ending, std::int64_t code) {
    return tuple_value({std::move(first), int32_value(static_cast<std::int32_t>(ending)), int64_value(code)});
}

// One overlapping operation: attempt it until it finishes, starting from
// the state the caller prepared. `retry` returns the completion or nothing.
struct Operation {
    std::function<std::optional<HostCompletion>()> retry;
    std::function<HostCompletion(std::uint32_t)> settle; // Ends it early (cancel, deadline, closed).
    std::function<PosixWait::Interest()> interest;
    std::function<SSL*()> engine;                        // Whose timers bound the wait.
    // Whether OpenSSL already reports what the operation waits for. Another
    // operation or the background work can process the datagram that made it
    // ready, so the socket alone cannot be trusted to wake it.
    std::function<bool()> progress;
};

// Whether `ssl` reports any of `events` now, without processing events.
bool reports(SSL* ssl, std::uint64_t events) {
    if (!ssl) return true; // Gone: the attempt reports it.
    SSL_POLL_ITEM item{};
    item.desc = SSL_as_poll_descriptor(ssl);
    item.events = events;
    timeval zero{};
    std::size_t ready = 0;
    if (SSL_poll(&item, 1, sizeof item, &zero, SSL_POLL_FLAG_NO_HANDLE_EVENTS, &ready) != 1) return true;
    return ready > 0 && (item.revents & events) != 0;
}

PendingOperation pending_of(std::shared_ptr<Operation> operation, std::int64_t now, std::int64_t timeout) {
    PendingOperation pending;
    pending.wait_source = PosixWait{{}, [operation] { return operation->interest(); }};
    if (timeout > 0) pending.deadline = deadline_after(now, timeout);
    pending.attempt = [operation](bool cancelled) -> std::optional<HostCompletion> {
        if (auto done = operation->retry()) return done;
        if (cancelled) return operation->settle(host_error::cancelled);
        return std::nullopt;
    };
    pending.expire = [operation] { return operation->settle(host_error::timed_out); };
    pending.wake_at = [operation](std::int64_t at) {
        if (operation->progress && operation->progress()) return at;
        const auto nanos = nanos_until_event(operation->engine());
        return nanos < 0 ? none : at + nanos;
    };
    return pending;
}

// The same operation without tasks: wait on the socket and OpenSSL's timers
// here, up to the timeout (zero: none).
HostCompletion run_blocking(const std::shared_ptr<Operation>& operation, std::int64_t timeout) {
    const auto start = SteadyClock::now();
    for (;;) {
        if (auto done = operation->retry()) return *done;
        auto wait_ms = -1;
        if (timeout > 0) {
            const auto spent = std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - start).count();
            if (spent >= timeout) return operation->settle(host_error::timed_out);
            wait_ms = timeout_milliseconds(timeout - spent);
        }
        const auto timer = nanos_until_event(operation->engine());
        if (timer >= 0) {
            const auto timer_ms = timeout_milliseconds(std::max<std::int64_t>(timer, 1'000'000));
            wait_ms = wait_ms < 0 ? timer_ms : std::min(wait_ms, timer_ms);
        }
        const auto interest = operation->interest();
        if (interest.descriptor < 0) {
            if (wait_ms != 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        short events = interest.events == PosixWait::Events::Write ? platform::poll_out
                     : interest.events == PosixWait::Events::ReadWrite ? static_cast<short>(platform::poll_in | platform::poll_out)
                                                                         : platform::poll_in;
        platform::PollItem item{static_cast<platform::native_socket>(interest.descriptor), events, 0};
        const auto ready = platform::poll(std::span(&item, 1), wait_ms);
        if (ready < 0 && !platform::last_socket_error().interrupted()) return operation->settle(socket_error());
    }
}

class Services {
public:
    Services(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
             std::shared_ptr<QuicState> state)
        : granted_(&capabilities), prefixes_(&prefixes), state_(std::move(state)) {}

    HostCompletion listen(const HostRequest& request) {
        if (!granted_->net) return denied();
        const auto& args = request.arguments;
        const auto& host = args.at(0).bytes;
        const auto port = as_int32(args.at(1));
        auto certificate = args.at(2).bytes;
        auto key = args.at(3).bytes;
        auto endpoint = std::make_unique<Endpoint>();
        endpoint->alpn_wire = alpn_wire(args.at(4));
        if (port < 0 || port > 65535 || !usable_text(host) || endpoint->alpn_wire.empty()) {
            return fail(host_error::invalid_input);
        }
        if (const auto error = tls_material::read(*granted_, *prefixes_, certificate)) return fail(error);
        if (const auto error = tls_material::read(*granted_, *prefixes_, key)) return fail(error);
        const auto address = numeric_address(host, port);
        if (!address) return fail(host_error::invalid_input);
#ifdef _WIN32
        if (const auto error = platform::ensure_sockets()) return fail(host_error::from_wsa(error.value()));
#endif
        const int descriptor = BIO_socket(BIO_ADDR_family(address.get()), SOCK_DGRAM, IPPROTO_UDP, BIO_SOCK_NONBLOCK);
        if (descriptor < 0) return fail(socket_error());
        endpoint->socket = static_cast<platform::native_socket>(descriptor);
        platform::set_no_inherit(endpoint->socket);
        if (BIO_bind(descriptor, address.get(), BIO_SOCK_REUSEADDR) != 1) return fail(socket_error());
        endpoint->context = SSL_CTX_new(OSSL_QUIC_server_method());
        if (!endpoint->context || !tls_material::identity(endpoint->context, certificate, key)) {
            return fail(host_error::invalid_input);
        }
        SSL_CTX_set_alpn_select_cb(endpoint->context, select_alpn, endpoint.get());
        endpoint->listener = std::make_unique<Ssl>(SSL_new_listener(endpoint->context, 0));
        BIO* datagram = endpoint->listener->ssl ? BIO_new_dgram(descriptor, BIO_NOCLOSE) : nullptr;
        if (!datagram) return fail(host_error::other);
        SSL_set_bio(endpoint->listener->ssl, datagram, datagram);
        if (SSL_set_blocking_mode(endpoint->listener->ssl, 0) != 1 || SSL_listen(endpoint->listener->ssl) != 1) {
            return fail(host_error::other);
        }
        const auto bound = local_port(endpoint->socket);
        if (bound < 0) return fail(socket_error());
        const auto id = state_->objects.insert(std::move(endpoint));
        if (!id) return fail(host_error::other);
        return HostCompletion::ok(tuple_value({int64_value(*id), int32_value(bound)}));
    }

    std::variant<HostCompletion, PendingOperation> accept(const HostRequest& request, bool blocking) {
        if (!granted_->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        const auto timeout = as_int64(request.arguments.at(1));
        auto* endpoint = state_->objects.find<Endpoint>(id);
        if (!endpoint) return fail(host_error::not_found);
        if (timeout < 0) return fail(host_error::invalid_input);
        if (endpoint->accepting) return fail(host_error::busy);
        endpoint->accepting = true;
        auto state = state_;
        auto accepted = std::make_shared<std::unique_ptr<Connection>>();
        auto operation = std::make_shared<Operation>();
        const auto release = [state, id] {
            if (auto* live = state->objects.find<Endpoint>(id)) live->accepting = false;
        };
        operation->engine = [state, id]() -> SSL* {
            auto* live = state->objects.find<Endpoint>(id);
            return live ? QuicState::handle(*live) : nullptr;
        };
        operation->interest = [state, id]() -> PosixWait::Interest {
            auto* live = state->objects.find<Endpoint>(id);
            return live ? state->interest(*live) : PosixWait::Interest{};
        };
        operation->settle = [release](std::uint32_t code) {
            release();
            return fail(code);
        };
        operation->progress = [state, id, accepted]() {
            if (*accepted) return true; // Its handshake runs on its own connection's timers and datagrams.
            auto* live = state->objects.find<Endpoint>(id);
            return !live || reports(QuicState::handle(*live), SSL_POLL_EVENT_IC | SSL_POLL_EVENT_EL);
        };
        operation->retry = [state, id, accepted, release]() -> std::optional<HostCompletion> {
            auto* live = state->objects.find<Endpoint>(id);
            if (!live) {
                return fail(host_error::not_found);
            }
            auto* listener = QuicState::handle(*live);
            static_cast<void>(SSL_handle_events(listener));
            if (!*accepted) {
                SSL* incoming = SSL_accept_connection(listener, SSL_ACCEPT_CONNECTION_NO_BLOCK);
                if (!incoming) return std::nullopt;
                *accepted = std::make_unique<Connection>();
                (*accepted)->ssl = std::make_unique<Ssl>(incoming);
                (*accepted)->endpoint = id;
                static_cast<void>(SSL_set_blocking_mode(incoming, 0));
                static_cast<void>(SSL_set_default_stream_mode(incoming, SSL_DEFAULT_STREAM_MODE_NONE));
            }
            auto* ssl = (*accepted)->ssl->ssl;
            ERR_clear_error();
            const int done = SSL_accept(ssl);
            if (done != 1) {
                const int reason = SSL_get_error(ssl, done);
                if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) return std::nullopt;
                accepted->reset(); // A failed handshake is dropped; the endpoint keeps listening.
                return std::nullopt;
            }
            (*accepted)->alpn = selected_alpn(ssl);
            const auto alpn = (*accepted)->alpn;
            const auto connection = state->objects.insert(std::move(*accepted));
            release();
            if (!connection) return fail(host_error::other);
            return HostCompletion::ok(tuple_value({int64_value(*connection), bytes_value(alpn)}));
        };
        if (blocking) return run_blocking(operation, timeout);
        if (auto done = operation->retry()) return *done;
        return pending_of(operation, request.now, timeout);
    }

    std::variant<HostCompletion, PendingOperation> connect(const HostRequest& request, bool blocking) {
        if (!granted_->net) return denied();
        const auto& args = request.arguments;
        const auto& host = args.at(0).bytes;
        const auto port = as_int32(args.at(1));
        const auto timeout = as_int64(args.at(2));
        const auto& name = args.at(3).bytes;
        const auto wire = alpn_wire(args.at(4));
        auto ca_bundle = args.at(5).bytes;
        if (port < 1 || port > 65535 || timeout < 0 || !usable_text(host) || !usable_text(name) || wire.empty()) {
            return fail(host_error::invalid_input);
        }
        if (const auto error = tls_material::read(*granted_, *prefixes_, ca_bundle)) return fail(error);
        const auto peer = numeric_address(host, port);
        if (!peer) return fail(host_error::invalid_input);
#ifdef _WIN32
        if (const auto error = platform::ensure_sockets()) return fail(host_error::from_wsa(error.value()));
#endif
        auto connection = std::make_shared<std::unique_ptr<Connection>>(std::make_unique<Connection>());
        auto& owned = **connection;
        owned.context = SSL_CTX_new(OSSL_QUIC_client_method());
        if (!owned.context) return fail(host_error::other);
        SSL_CTX_set_verify(owned.context, SSL_VERIFY_PEER, nullptr);
        if (!tls_material::trust(owned.context, ca_bundle)) return fail(host_error::permission_denied);
        owned.ssl = std::make_unique<Ssl>(SSL_new(owned.context));
        auto* ssl = owned.ssl->ssl;
        if (!ssl || !verify_identity(ssl, name) ||
            SSL_set_alpn_protos(ssl, reinterpret_cast<const unsigned char*>(wire.data()),
                                static_cast<unsigned int>(wire.size())) != 0 ||
            SSL_set_default_stream_mode(ssl, SSL_DEFAULT_STREAM_MODE_NONE) != 1 ||
            SSL_set1_initial_peer_addr(ssl, peer.get()) != 1) {
            return fail(host_error::invalid_input);
        }
        const int descriptor = BIO_socket(BIO_ADDR_family(peer.get()), SOCK_DGRAM, IPPROTO_UDP, BIO_SOCK_NONBLOCK);
        if (descriptor < 0) return fail(socket_error());
        owned.socket = static_cast<platform::native_socket>(descriptor);
        platform::set_no_inherit(owned.socket);
        BIO* datagram = BIO_new_dgram(descriptor, BIO_NOCLOSE);
        if (!datagram) return fail(host_error::other);
        SSL_set_bio(ssl, datagram, datagram);
        if (SSL_set_blocking_mode(ssl, 0) != 1) return fail(host_error::other);
        auto state = state_;
        auto operation = std::make_shared<Operation>();
        operation->engine = [connection]() -> SSL* { return *connection ? (*connection)->ssl->ssl : nullptr; };
        operation->interest = [connection]() -> PosixWait::Interest {
            return *connection ? interest_of((*connection)->ssl->ssl, (*connection)->socket) : PosixWait::Interest{};
        };
        operation->settle = [connection](std::uint32_t code) {
            connection->reset();
            return fail(code);
        };
        operation->retry = [state, connection]() -> std::optional<HostCompletion> {
            if (!*connection) return fail(host_error::other);
            auto* ssl = (*connection)->ssl->ssl;
            ERR_clear_error();
            const int done = SSL_connect(ssl);
            if (done != 1) {
                const int reason = SSL_get_error(ssl, done);
                if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) return std::nullopt;
                const auto code = failure_of(ssl);
                connection->reset();
                return fail(code);
            }
            (*connection)->alpn = selected_alpn(ssl);
            if ((*connection)->alpn.empty()) {
                connection->reset();
                return fail(host_error::connection_reset);
            }
            const auto alpn = (*connection)->alpn;
            const auto id = state->objects.insert(std::move(*connection));
            if (!id) return fail(host_error::other);
            return HostCompletion::ok(tuple_value({int64_value(*id), bytes_value(alpn)}));
        };
        if (blocking) return run_blocking(operation, timeout);
        if (auto done = operation->retry()) return *done;
        return pending_of(operation, request.now, timeout);
    }

    HostCompletion open_stream(const HostRequest& request) {
        if (!granted_->net) return denied();
        auto* connection = state_->objects.find<Connection>(as_int64(request.arguments.at(0)));
        if (!connection) return fail(host_error::not_found);
        const bool bidirectional = as_bool(request.arguments.at(1));
        auto* ssl = connection->ssl->ssl;
        static_cast<void>(SSL_handle_events(ssl));
        SSL* stream = SSL_new_stream(ssl, SSL_STREAM_FLAG_NO_BLOCK | (bidirectional ? 0 : SSL_STREAM_FLAG_UNI));
        if (!stream) return fail(host_error::busy); // The peer's stream limit, or a closed connection.
        const auto number = SSL_get_stream_id(stream);
        connection->streams[number].ssl = std::make_unique<Ssl>(stream);
        return HostCompletion::ok(int64_value(static_cast<std::int64_t>(number)));
    }

    std::variant<HostCompletion, PendingOperation> accept_stream(const HostRequest& request, bool blocking) {
        if (!granted_->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        const auto timeout = as_int64(request.arguments.at(1));
        auto* connection = state_->objects.find<Connection>(id);
        if (!connection) return fail(host_error::not_found);
        if (timeout < 0) return fail(host_error::invalid_input);
        if (connection->accepting) return fail(host_error::busy);
        connection->accepting = true;
        auto state = state_;
        auto operation = connection_operation(id);
        operation->settle = [state, id](std::uint32_t code) {
            if (auto* live = state->objects.find<Connection>(id)) live->accepting = false;
            return fail(code);
        };
        operation->progress = [state, id]() {
            auto* live = state->objects.find<Connection>(id);
            return !live || reports(live->ssl->ssl, SSL_POLL_EVENT_IS | SSL_POLL_EVENT_EC);
        };
        operation->retry = [state, id]() -> std::optional<HostCompletion> {
            auto* live = state->objects.find<Connection>(id);
            if (!live) return fail(host_error::not_found);
            auto* ssl = live->ssl->ssl;
            static_cast<void>(SSL_handle_events(ssl));
            SSL* incoming = SSL_accept_stream(ssl, SSL_ACCEPT_STREAM_NO_BLOCK);
            if (!incoming) {
                SSL_CONN_CLOSE_INFO info{};
                if (SSL_get_conn_close_info(ssl, &info, sizeof info) == 1) {
                    live->accepting = false;
                    return fail(host_error::connection_reset);
                }
                return std::nullopt;
            }
            const auto number = SSL_get_stream_id(incoming);
            const bool bidirectional = SSL_get_stream_type(incoming) == SSL_STREAM_TYPE_BIDI;
            live->streams[number].ssl = std::make_unique<Ssl>(incoming);
            live->accepting = false;
            return HostCompletion::ok(
                tuple_value({int64_value(static_cast<std::int64_t>(number)), scalar(bidirectional ? 1 : 0)}));
        };
        if (blocking) return run_blocking(operation, timeout);
        if (auto done = operation->retry()) return *done;
        return pending_of(operation, request.now, timeout);
    }

    std::variant<HostCompletion, PendingOperation> stream_read(const HostRequest& request, bool blocking) {
        if (!granted_->net) return denied();
        const auto& args = request.arguments;
        const auto id = as_int64(args.at(0));
        const auto number = static_cast<std::uint64_t>(as_int64(args.at(1)));
        const auto maximum = as_int32(args.at(2));
        const auto timeout = as_int64(args.at(3));
        auto* connection = state_->objects.find<Connection>(id);
        if (!connection) return fail(host_error::not_found);
        const auto found = connection->streams.find(number);
        if (found == connection->streams.end() || maximum < 1 || timeout < 0) return fail(host_error::invalid_input);
        if (found->second.reading) return fail(host_error::busy);
        found->second.reading = true;
        auto state = state_;
        auto buffer = std::make_shared<std::string>(static_cast<std::size_t>(std::min(maximum, max_read_bytes)), '\0');
        const auto release = [state, id, number] {
            if (auto* live = state->objects.find<Connection>(id)) {
                if (auto at = live->streams.find(number); at != live->streams.end()) at->second.reading = false;
            }
        };
        auto operation = connection_operation(id);
        operation->settle = [release](std::uint32_t code) {
            release();
            return fail(code);
        };
        operation->progress = stream_progress(state, id, number, SSL_POLL_EVENT_R | SSL_POLL_EVENT_ER);
        operation->retry = [state, id, number, buffer, release]() -> std::optional<HostCompletion> {
            auto* live = state->objects.find<Connection>(id);
            if (!live) return fail(host_error::not_found);
            auto at = live->streams.find(number);
            if (at == live->streams.end()) return fail(host_error::not_found);
            auto* ssl = live->ssl->ssl;
            auto* stream = at->second.ssl->ssl;
            static_cast<void>(SSL_handle_events(ssl));
            std::size_t got = 0;
            ERR_clear_error();
            const int status = SSL_read_ex(stream, buffer->data(), buffer->size(), &got);
            if (status == 1) {
                release();
                return HostCompletion::ok(ending_value(bytes_value(buffer->substr(0, got)), Ending::None, -1));
            }
            const int reason = SSL_get_error(stream, status);
            if (reason == SSL_ERROR_ZERO_RETURN) {
                release();
                return HostCompletion::ok(ending_value(bytes_value({}), Ending::None, -1));
            }
            if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) return std::nullopt;
            const auto [ending, code] = ending_of(ssl, stream, true);
            release();
            if (ending == Ending::None) return fail(failure_of(ssl));
            return HostCompletion::ok(ending_value(bytes_value({}), ending, code));
        };
        if (blocking) return run_blocking(operation, timeout);
        if (auto done = operation->retry()) return *done;
        return pending_of(operation, request.now, timeout);
    }

    std::variant<HostCompletion, PendingOperation> stream_write(const HostRequest& request, bool blocking) {
        if (!granted_->net) return denied();
        const auto& args = request.arguments;
        const auto id = as_int64(args.at(0));
        const auto number = static_cast<std::uint64_t>(as_int64(args.at(1)));
        auto data = std::make_shared<std::string>(args.at(2).bytes);
        const bool fin = as_bool(args.at(3));
        const auto timeout = as_int64(args.at(4));
        auto* connection = state_->objects.find<Connection>(id);
        if (!connection) return fail(host_error::not_found);
        const auto found = connection->streams.find(number);
        if (found == connection->streams.end() || timeout < 0 || data->size() > INT_MAX) {
            return fail(host_error::invalid_input);
        }
        if (found->second.writing) return fail(host_error::busy);
        found->second.writing = true;
        auto state = state_;
        auto sent = std::make_shared<std::size_t>(0);
        const auto release = [state, id, number] {
            if (auto* live = state->objects.find<Connection>(id)) {
                if (auto at = live->streams.find(number); at != live->streams.end()) at->second.writing = false;
            }
        };
        auto operation = connection_operation(id);
        // An early end reports the prefix OpenSSL accepted, as a cancelled
        // or timed-out neura.net.write does; with nothing accepted, the code.
        operation->settle = [release, sent](std::uint32_t code) {
            release();
            if (*sent > 0) {
                return HostCompletion::ok(ending_value(int32_value(static_cast<std::int32_t>(*sent)), Ending::None, -1));
            }
            return fail(code);
        };
        operation->progress = stream_progress(state, id, number, SSL_POLL_EVENT_W | SSL_POLL_EVENT_EW);
        operation->retry = [state, id, number, data, fin, sent, release]() -> std::optional<HostCompletion> {
            auto* live = state->objects.find<Connection>(id);
            if (!live) return fail(host_error::not_found);
            auto at = live->streams.find(number);
            if (at == live->streams.end()) return fail(host_error::not_found);
            auto* ssl = live->ssl->ssl;
            auto* stream = at->second.ssl->ssl;
            static_cast<void>(SSL_handle_events(ssl));
            while (*sent < data->size()) {
                std::size_t accepted = 0;
                ERR_clear_error();
                const int status = SSL_write_ex(stream, data->data() + *sent, data->size() - *sent, &accepted);
                if (status == 1) {
                    *sent += accepted;
                    continue;
                }
                const int reason = SSL_get_error(stream, status);
                if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) return std::nullopt;
                const auto [ending, code] = ending_of(ssl, stream, false);
                release();
                if (ending == Ending::None) return fail(failure_of(ssl));
                return HostCompletion::ok(
                    ending_value(int32_value(static_cast<std::int32_t>(*sent)), ending, code));
            }
            if (fin && SSL_stream_conclude(stream, 0) != 1) {
                release();
                return fail(failure_of(ssl));
            }
            release();
            return HostCompletion::ok(ending_value(int32_value(static_cast<std::int32_t>(*sent)), Ending::None, -1));
        };
        if (blocking) return run_blocking(operation, timeout);
        if (auto done = operation->retry()) return *done;
        return pending_of(operation, request.now, timeout);
    }

    HostCompletion close(const HostRequest& request) {
        if (!granted_->net) return denied();
        const auto id = as_int64(request.arguments.at(0));
        const auto code = as_int64(request.arguments.at(1));
        const auto& reason = request.arguments.at(2).bytes;
        // The reason may be empty; it travels in CONNECTION_CLOSE, so it is bounded and has no NUL.
        if (code < 0 || reason.size() > 1024 || reason.find('\0') != std::string::npos) {
            return fail(host_error::invalid_input);
        }
        if (auto* connection = state_->objects.find<Connection>(id)) {
            SSL_SHUTDOWN_EX_ARGS close_args{static_cast<std::uint64_t>(code), reason.c_str()};
            static_cast<void>(SSL_shutdown_ex(connection->ssl->ssl, SSL_SHUTDOWN_FLAG_NO_BLOCK, &close_args,
                                              sizeof close_args));
            static_cast<void>(SSL_handle_events(connection->ssl->ssl));
        } else if (!state_->objects.find<Endpoint>(id)) {
            return fail(host_error::not_found);
        } else {
            // Connections accepted from this endpoint share its socket: they
            // end with it.
            for (const auto child : state_->objects.children_of(id)) state_->objects.erase(child);
        }
        state_->objects.erase(id);
        return HostCompletion::ok();
    }

private:
    // Whether a stream's direction (R or W) or its connection reports progress.
    static std::function<bool()> stream_progress(std::shared_ptr<QuicState> state, std::int64_t id,
                                                 std::uint64_t number, std::uint64_t events) {
        return [state, id, number, events]() {
            auto* live = state->objects.find<Connection>(id);
            if (!live) return true;
            const auto at = live->streams.find(number);
            if (at == live->streams.end()) return true;
            return reports(at->second.ssl->ssl, events) || reports(live->ssl->ssl, SSL_POLL_EVENT_EC);
        };
    }

    std::shared_ptr<Operation> connection_operation(std::int64_t id) {
        auto state = state_;
        auto operation = std::make_shared<Operation>();
        operation->engine = [state, id]() -> SSL* {
            auto* live = state->objects.find<Connection>(id);
            return live ? live->ssl->ssl : nullptr;
        };
        operation->interest = [state, id]() -> PosixWait::Interest {
            auto* live = state->objects.find<Connection>(id);
            return live ? state->interest(*live) : PosixWait::Interest{};
        };
        return operation;
    }

    static std::int32_t local_port(platform::native_socket socket) {
        sockaddr_storage address{};
#ifdef _WIN32
        int length = sizeof address;
#else
        socklen_t length = sizeof address;
#endif
        if (::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) return -1;
        if (address.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in&>(address).sin_port);
        if (address.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6&>(address).sin6_port);
        return -1;
    }

    const Capabilities* granted_;
    const std::vector<ResolvedPath>* prefixes_;
    std::shared_ptr<QuicState> state_;
};

} // namespace
#endif

struct QuicTables {
#if defined(NEURA_NVM_TLS) && !defined(OPENSSL_NO_QUIC)
    std::shared_ptr<QuicState> state = std::make_shared<QuicState>();
#endif
};

void close_quic(StandardServices::Tables& tables) {
#if defined(NEURA_NVM_TLS) && !defined(OPENSSL_NO_QUIC)
    if (tables.quic) tables.quic->state->objects.clear();
#else
    (void)tables;
#endif
}

void add_quic_services(const QuicAdder& add, const QuicOverlapAdder& overlap, StandardServices::Tables& tables,
                       const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes) {
    if (!tables.quic) tables.quic = std::make_shared<QuicTables>();
    const auto* granted = &capabilities;
    const HostType endpoint_result = tuple({int64(), int32()});
    const HostType connection_result = tuple({int64(), text()});
    const HostType stream_result = tuple({int64(), boolean()});
    const HostType read_result = tuple({byte_list(), int32(), int64()});
    const HostType write_result = tuple({int32(), int32(), int64()});
    const auto listen_signature = signature("neura.net.quic.listen", {text(), int32(), text(), text(), text_list()},
                                            endpoint_result);
    const auto accept_signature = signature("neura.net.quic.accept", {int64(), int64()}, connection_result);
    const auto connect_signature = signature("neura.net.quic.connect",
                                             {text(), int32(), int64(), text(), text_list(), text()},
                                             connection_result);
    const auto open_signature = signature("neura.net.quic.open_stream", {int64(), boolean()}, int64());
    const auto accept_stream_signature = signature("neura.net.quic.accept_stream", {int64(), int64()}, stream_result);
    const auto read_signature = signature("neura.net.quic.stream_read", {int64(), int64(), int32(), int64()},
                                          read_result);
    const auto write_signature = signature("neura.net.quic.stream_write",
                                           {int64(), int64(), byte_list(), boolean(), int64()}, write_result);
    const auto close_signature = signature("neura.net.quic.close", {int64(), int64(), text()}, void_type());
#if defined(NEURA_NVM_TLS) && !defined(OPENSSL_NO_QUIC)
    auto state = tables.quic->state;
    auto services = std::make_shared<Services>(capabilities, prefixes, state);
    BackgroundWork* background = state.get();
    add(listen_signature, [services](const HostRequest& request) { return services->listen(request); }, background);
    add(open_signature, [services](const HostRequest& request) { return services->open_stream(request); },
        background);
    add(close_signature, [services](const HostRequest& request) { return services->close(request); }, background);
    const auto waiting = [&](HostSignature service,
                             std::variant<HostCompletion, PendingOperation> (Services::*start)(const HostRequest&,
                                                                                                bool)) {
        overlap(std::move(service),
            [services, start](const HostRequest& request) {
                return std::get<HostCompletion>(((*services).*start)(request, true));
            },
            [services, start](const HostRequest& request) { return ((*services).*start)(request, false); },
            background);
    };
    waiting(accept_signature, &Services::accept);
    waiting(connect_signature, &Services::connect);
    waiting(accept_stream_signature, &Services::accept_stream);
    waiting(read_signature, &Services::stream_read);
    waiting(write_signature, &Services::stream_write);
#else
    const auto unsupported = [granted](const HostRequest&) {
        return granted->net ? fail(host_error::unsupported) : denied();
    };
    const auto begin_unsupported = [unsupported](const HostRequest& request)
        -> std::variant<HostCompletion, PendingOperation> { return unsupported(request); };
    add(listen_signature, unsupported, nullptr);
    add(open_signature, unsupported, nullptr);
    add(close_signature, unsupported, nullptr);
    for (auto service : {accept_signature, connect_signature, accept_stream_signature, read_signature,
                         write_signature}) {
        overlap(std::move(service), unsupported, begin_unsupported, nullptr);
    }
#endif
    (void)granted;
}

} // namespace neura::nvm::provider
