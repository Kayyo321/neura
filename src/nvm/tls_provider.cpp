// TLS streams for the standard library (docs/runtime/NVM.md, "TLS services"):
// a client stream that verifies its peer, and a server stream accepted from
// a TCP listener, both with ALPN. Built with OpenSSL 4.1 when the build has
// NEURA_ENABLE_SERVER_TLS (NEURA_NVM_TLS); otherwise each call with the net
// capability answers Unsupported (13). None of these services overlaps.

#include "provider_internal.hpp"
#include "platform/socket.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <optional>
#include <string>

#ifdef NEURA_NVM_TLS
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#if !defined(SO_NOSIGPIPE)
#include <pthread.h>
#include <signal.h>
#endif
#endif
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include "tls_material.hpp"
#endif

namespace neura::nvm::provider {

namespace {

HostType stream_result() { return tuple({int64(), text()}); }

HostSignature connect_extended() {
    return {"neura.net.tls.connect", 2, {text(), int32(), int64(), text(), text_list(), text(), int32(), int32(),
                                         text(), text(), boolean()}, stream_result(), uint32()};
}

HostSignature connect_deadline() {
    return signature("neura.net.tls.connect_deadline", {text(), int32(), int64(), text(), text_list(), text()},
                     stream_result());
}

HostSignature accept_extended() {
    return {"neura.net.tls.accept", 2, {int64(), text(), text(), text_list(), int64(), int32(), int32(), text(),
                                        boolean()}, stream_result(), uint32()};
}

HostSignature local_address() {
    return signature("neura.net.tls.local_address", {int64()}, tuple({text(), int32()}));
}

HostSignature shutdown_write() {
    return signature("neura.net.tls.shutdown_write", {int64(), int64()}, void_type());
}

HostSignature peer_chain() {
    return signature("neura.net.tls.peer_chain", {int64()}, HostType(Kind::List, std::vector<HostType>{byte_list()}));
}

HostSignature engine_open() {
    return signature("neura.net.tls.engine.open", {boolean(), text(), text_list(), text(), text(), text()}, int64());
}

HostSignature engine_exchange() {
    return signature("neura.net.tls.engine.exchange", {int64(), int32(), byte_list(), byte_list()},
                     tuple({int32(), byte_list(), byte_list(), uint32()}));
}

HostSignature dtls_open() {
    return signature("neura.net.dtls.engine.open", {boolean(), text(), text_list(), text(), text(), text(),
                      text(), int32(), text(), int32(), int64()}, int64());
}

HostSignature dtls_exchange() {
    return signature("neura.net.dtls.engine.exchange", {int64(), int32(), byte_list(), text(), int32(),
                      byte_list(), int64()}, tuple({int32(), HostType(Kind::List, std::vector<HostType>{byte_list()}),
                                                  byte_list(), int64(), uint32()}));
}

HostSignature dtls_release() {
    return signature("neura.net.dtls.engine.release", {int64()}, void_type());
}

HostSignature engine_release() {
    return signature("neura.net.tls.engine.release", {int64()}, void_type());
}

} // namespace

#ifdef NEURA_NVM_TLS

namespace {

// OpenSSL's socket BIO can raise SIGPIPE on a closed socket. Suppress it only
// on the host-calling thread, preserving a signal that was already pending.
class SocketWrites {
public:
#if !defined(_WIN32) && !defined(SO_NOSIGPIPE)
    SocketWrites() {
        sigemptyset(&blocked_);
        sigaddset(&blocked_, SIGPIPE);
        changed_ = pthread_sigmask(SIG_BLOCK, &blocked_, &saved_) == 0;
        sigset_t pending;
        pending_ = sigpending(&pending) != 0 || sigismember(&pending, SIGPIPE) == 1;
    }

    ~SocketWrites() {
        if (!changed_) return;
        if (!pending_) {
            timespec zero{};
            while (sigtimedwait(&blocked_, nullptr, &zero) < 0 && errno == EINTR) {}
        }
        pthread_sigmask(SIG_SETMASK, &saved_, nullptr);
    }

private:
    sigset_t blocked_{};
    sigset_t saved_{};
    bool changed_ = false;
    bool pending_ = false;
#else
    SocketWrites() = default;
    ~SocketWrites() = default;
#endif
};

} // namespace

constexpr std::int64_t default_transfer_timeout = 30'000'000'000LL;

struct TlsStream {
    NativeSocket descriptor = invalid_native_socket;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    std::string alpn;
    SSL_CTX* context = nullptr;
    SSL* ssl = nullptr;
    // Datagram engines have their own addressed BIO and (on a server) listener.
    // They never expose a native descriptor or enter the stream TLS services.
    bool datagram = false;
    SSL* listener = nullptr;
    BIO* datagram_wire = nullptr;
    BIO_ADDR* datagram_peer = nullptr;
    int pending_operation = 0;
    std::uint32_t terminal_error = 0;
    bool key_update_started = false;
    bool write_closed = false;
    std::int64_t write_timeout = default_transfer_timeout;
    ~TlsStream() {
        if (ssl) {
            if (SSL_is_init_finished(ssl) && descriptor != invalid_native_socket) {
                [[maybe_unused]] SocketWrites protection;
                SSL_shutdown(ssl);
            }
            SSL_free(ssl);
        }
        if (listener) SSL_free(listener);
        if (datagram_wire) BIO_free(datagram_wire);
        if (datagram_peer) BIO_ADDR_free(datagram_peer);
        if (context) SSL_CTX_free(context);
        if (descriptor != invalid_native_socket) platform::close_socket(descriptor);
    }
};

void TlsDeleter::operator()(TlsStream* stream) const { delete stream; }

namespace {

using Clock = std::chrono::steady_clock;

// A deadline `nanos` from now; zero waits without limit. Saturate before adding.
Clock::time_point deadline_of(std::int64_t nanos) {
    const auto now = Clock::now();
    const auto limit = Clock::time_point::max();
    const auto duration = std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(nanos));
    return nanos > 0 && duration < limit - now ? now + duration : limit;
}

std::uint32_t socket_error(int error) {
#ifdef _WIN32
    return host_error::from_wsa(error);
#else
    return host_error::from_errno(error);
#endif
}

int wait_for(NativeSocket descriptor, short events, Clock::time_point deadline) {
    for (;;) {
        int milliseconds = -1;
        if (deadline != Clock::time_point::max()) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count();
            if (left <= 0) return 0;
            milliseconds = timeout_milliseconds(left);
        }
        platform::PollItem entry{descriptor, events, 0};
        const int ready = platform::poll({&entry, 1}, milliseconds);
        if (ready >= 0 || !platform::last_socket_error().interrupted()) return ready;
    }
}

// Repeats an SSL operation until it completes, waiting for the socket as it asks.
template<typename Operation>
int drive(SSL* ssl, NativeSocket descriptor, Clock::time_point deadline, std::uint32_t& error, Operation operation) {
    for (;;) {
        if (deadline != Clock::time_point::max() && Clock::now() >= deadline) {
            error = host_error::timed_out;
            return 0;
        }
        ERR_clear_error();
#ifdef _WIN32
        WSASetLastError(0);
#else
        errno = 0;
#endif
        // Preserve the socket error across the signal-mask guard's destructor.
        int result = 0;
        platform::SocketError last_error;
        {
            [[maybe_unused]] SocketWrites protection;
            result = operation();
            last_error = platform::last_socket_error();
        }
        if (result > 0) return result;
        const int reason = SSL_get_error(ssl, result);
        short events = 0;
        if (reason == SSL_ERROR_WANT_READ) events = platform::poll_in;
        else if (reason == SSL_ERROR_WANT_WRITE) events = platform::poll_out;
        else {
            error = reason == SSL_ERROR_ZERO_RETURN ? 0 : reason == SSL_ERROR_SYSCALL && last_error.code != 0
                ? socket_error(last_error.code) : host_error::connection_reset;
            return 0;
        }
        const int ready = wait_for(descriptor, events, deadline);
        if (ready == 0) {
            error = host_error::timed_out;
            return 0;
        }
        if (ready < 0) {
            error = socket_error(platform::last_socket_error().code);
            return 0;
        }
    }
}

// ALPN protocol names in wire form: each preceded by its length.
std::optional<std::string> alpn_wire(const HostValue& names) {
    std::string wire;
    for (const auto& name : names.elements) {
        if (name.bytes.empty() || name.bytes.size() > 255) return std::nullopt;
        wire += static_cast<char>(name.bytes.size());
        wire += name.bytes;
    }
    return wire;
}

std::string selected_protocol(SSL* ssl) {
    const unsigned char* data = nullptr;
    unsigned int size = 0;
    SSL_get0_alpn_selected(ssl, &data, &size);
    return data ? std::string(reinterpret_cast<const char*>(data), size) : std::string();
}

bool prepare_socket(NativeSocket descriptor) {
    if (!platform::set_no_inherit(descriptor) || !platform::set_nonblocking(descriptor)) return false;
#ifdef SO_NOSIGPIPE
    int one = 1;
    return ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) == 0;
#else
    return true;
#endif
}

NativeSocket connect_tcp(const std::string& host, std::int32_t port, Clock::time_point deadline,
                         std::uint32_t& error) {
    if (const auto start = platform::ensure_sockets()) {
        error = socket_error(start.value());
        return invalid_native_socket;
    }
    struct Resolution {
        std::mutex mutex;
        std::condition_variable ready;
        bool done = false;
        int error = 0;
        addrinfo* addresses = nullptr;
        ~Resolution() { if (addresses) ::freeaddrinfo(addresses); }
    };
    // Heap storage avoids inherited locked mutexes after fork. A stuck system
    // resolver retains one worker, and later calls fail promptly instead of
    // accumulating threads. A child gets a fresh slot on its first call.
    struct ResolverSlot {
#ifdef _WIN32
        const std::uint64_t owner = GetCurrentProcessId();
#else
        const std::uint64_t owner = static_cast<std::uint64_t>(::getpid());
#endif
        std::atomic<bool> busy{false};
    };
    static std::atomic<ResolverSlot*> current{new ResolverSlot};
    auto* slot = current.load();
#ifndef _WIN32
    if (slot->owner != static_cast<std::uint64_t>(::getpid())) {
        auto replacement = std::make_unique<ResolverSlot>();
        if (current.compare_exchange_strong(slot, replacement.get())) slot = replacement.release();
        // The inherited slot belongs to a vanished parent worker. Do not
        // destroy any parent-owned synchronization state in the child.
    }
#endif
    bool idle = false;
    if (!slot->busy.compare_exchange_strong(idle, true)) {
        error = host_error::timed_out;
        return invalid_native_socket;
    }
    auto resolution = std::make_shared<Resolution>();
    auto* acquired = slot;
    try {
        std::thread([resolution, acquired, host, service = std::to_string(port)] {
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* found = nullptr;
            const auto result = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &found);
            {
                std::lock_guard lock(resolution->mutex);
                resolution->addresses = found;
                resolution->error = result;
                resolution->done = true;
            }
            acquired->busy.store(false);
            resolution->ready.notify_one();
        }).detach();
    } catch (...) {
        acquired->busy.store(false);
        error = host_error::other;
        return invalid_native_socket;
    }
    std::unique_lock lock(resolution->mutex);
    if (!resolution->ready.wait_until(lock, std::min(deadline, Clock::now() + std::chrono::seconds(30)), [&] { return resolution->done; })) {
        error = host_error::timed_out;
        return invalid_native_socket;
    }
    if (resolution->error != 0) {
        error = host_error::not_found;
        return invalid_native_socket;
    }
    const auto* found = resolution->addresses;
    error = host_error::connection_refused;
    for (const auto* entry = found; entry; entry = entry->ai_next) {
        const auto descriptor = ::socket(entry->ai_family, SOCK_STREAM, 0);
        if (descriptor == invalid_native_socket) {
            error = socket_error(platform::last_socket_error().code);
            continue;
        }
        if (prepare_socket(descriptor)) {
            if (::connect(descriptor, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) == 0) return descriptor;
            const auto problem = platform::last_socket_error();
            error = socket_error(problem.code);
            if (problem.would_block()) {
                const int ready = wait_for(descriptor, platform::poll_out, deadline);
                int pending_error = 0;
#ifdef _WIN32
                int length = sizeof pending_error;
                auto* result = reinterpret_cast<char*>(&pending_error);
#else
                socklen_t length = sizeof pending_error;
                auto* result = &pending_error;
#endif
                if (ready > 0 && ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, result, &length) == 0 &&
                    pending_error == 0) return descriptor;
                error = ready == 0 ? host_error::timed_out : socket_error(pending_error ? pending_error :
                                                                            platform::last_socket_error().code);
            }
        } else {
            error = socket_error(platform::last_socket_error().code);
        }
        platform::close_socket(descriptor);
        if (error == host_error::timed_out) break;
    }
    return invalid_native_socket;
}

bool numeric_host(const std::string& host) {
    unsigned char buffer[16];
    return ::inet_pton(AF_INET, host.c_str(), buffer) == 1 || ::inet_pton(AF_INET6, host.c_str(), buffer) == 1;
}

int version_code(std::int32_t number) {
    if (number == 12) return TLS1_2_VERSION;
    if (number == 13) return TLS1_3_VERSION;
    return 0;
}

bool valid_versions(std::int32_t low, std::int32_t high) {
    return low <= high && version_code(low) != 0 && version_code(high) != 0;
}

bool init_context(TlsStream& stream, bool server, std::int32_t low, std::int32_t high, bool kernel) {
    stream.context = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
    if (!stream.context || SSL_CTX_set_min_proto_version(stream.context, version_code(low)) != 1 ||
        SSL_CTX_set_max_proto_version(stream.context, version_code(high)) != 1) return false;
    if (kernel) SSL_CTX_set_options(stream.context, SSL_OP_ENABLE_KTLS);
    return true;
}

bool load_trust(SSL_CTX* context, const std::string& bytes) { return tls_material::trust(context, bytes); }

std::uint32_t permitted_paths(const Capabilities& capabilities, const std::vector<ResolvedPath>& prefixes,
                              std::initializer_list<std::string*> paths) {
    for (auto* path : paths) {
        if (const auto error = tls_material::read(capabilities, prefixes, *path)) return error;
    }
    return 0;
}

bool load_identity(SSL_CTX* context, const std::string& certificate, const std::string& key) {
    return tls_material::identity(context, certificate, key);
}

bool set_identity_check(SSL* ssl, const std::string& name) {
    X509_VERIFY_PARAM_set_hostflags(SSL_get0_param(ssl), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    return numeric_host(name) ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), name.c_str()) == 1
                             : SSL_set_tlsext_host_name(ssl, name.c_str()) == 1 && SSL_set1_dnsname(ssl, name.c_str()) == 1;
}

bool attach_socket(TlsStream& stream) {
    // OpenSSL's socket BIO takes an int, including on Win64. Reject an unrepresentable handle.
    if (static_cast<std::uint64_t>(stream.descriptor) > INT_MAX) return false;
    stream.ssl = SSL_new(stream.context);
    return stream.ssl && SSL_set_fd(stream.ssl, static_cast<int>(stream.descriptor)) == 1;
}

int server_alpn(SSL*, const unsigned char** out, unsigned char* out_size, const unsigned char* in, unsigned int in_size,
                void* argument) {
    const auto* wire = static_cast<const std::string*>(argument);
    unsigned char* selected = nullptr;
    if (SSL_select_next_proto(&selected, out_size, reinterpret_cast<const unsigned char*>(wire->data()),
                              static_cast<unsigned int>(wire->size()), in, in_size) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    *out = selected;
    return SSL_TLSEXT_ERR_OK;
}

// Numeric addresses only: the supplied datagram socket already owns name resolution.
using DatagramAddress = std::unique_ptr<BIO_ADDR, decltype(&BIO_ADDR_free)>;
DatagramAddress datagram_address(const std::string& host, std::int32_t port) {
    DatagramAddress address(BIO_ADDR_new(), BIO_ADDR_free);
    unsigned char raw[16];
    if (!address || port < 1 || port > 65535 || !usable_text(host)) return {nullptr, BIO_ADDR_free};
    for (const auto family : {AF_INET, AF_INET6}) {
        const std::size_t size = family == AF_INET ? 4 : 16;
        if (::inet_pton(family, host.c_str(), raw) == 1 &&
            BIO_ADDR_rawmake(address.get(), family, raw, size, htons(static_cast<std::uint16_t>(port)))) return address;
    }
    return {nullptr, BIO_ADDR_free};
}

bool same_datagram_address(const BIO_ADDR* left, const BIO_ADDR* right) {
    unsigned char a[16], b[16];
    std::size_t as = sizeof a, bs = sizeof b;
    return left && right && BIO_ADDR_family(left) == BIO_ADDR_family(right) &&
        BIO_ADDR_rawport(left) == BIO_ADDR_rawport(right) && BIO_ADDR_rawaddress(left, a, &as) &&
        BIO_ADDR_rawaddress(right, b, &bs) && as == bs && std::memcmp(a, b, as) == 0;
}

// Zero is bounded as well: a quiet/lost peer cannot retain the scheduler indefinitely.
constexpr std::int64_t dtls_default_timeout = 30000000000LL;

std::int64_t datagram_wait(TlsStream& stream) {
    auto wait = std::max<std::int64_t>(1,
        std::chrono::duration_cast<std::chrono::nanoseconds>(stream.deadline - Clock::now()).count());
    for (auto* ssl : {stream.listener, stream.ssl}) {
        if (!ssl) continue;
        timeval timeout{};
        int infinite = 1;
        if (SSL_get_event_timeout(ssl, &timeout, &infinite) && !infinite) {
            // The absolute application deadline caps even a very large provider timer.
            const auto seconds = std::min<std::int64_t>(timeout.tv_sec, dtls_default_timeout / 1000000000);
            const auto nanos = seconds * 1000000000 + static_cast<std::int64_t>(timeout.tv_usec) * 1000;
            wait = std::min(wait, std::max<std::int64_t>(1, nanos));
        }
    }
    return wait;
}

} // namespace

void add_tls_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities,
                      const std::vector<ResolvedPath>& prefixes) {
    auto* state = &tables;
    const auto* granted = &capabilities;
    const auto* allowed_paths = &prefixes;
    const auto remember = [state](std::unique_ptr<TlsStream> stream) {
        const auto selection = selected_protocol(stream->ssl);
        const auto id = state->next_tls++;
        state->tls.emplace(id, std::unique_ptr<TlsStream, TlsDeleter>(stream.release()));
        return HostCompletion::ok(tuple_value({int64_value(id), bytes_value(selection)}));
    };
    const auto find = [state](const HostValue& id) -> TlsStream* {
        const auto found = state->tls.find(as_int64(id));
        return found == state->tls.end() || found->second->datagram ? nullptr : found->second.get();
    };

    const auto connect = [=](const HostRequest& request, bool retain_deadline) {
        if (!granted->net) return denied();
        const auto& args = request.arguments;
        const auto& host = args.at(0).bytes;
        const auto port = as_int32(args.at(1));
        const auto timeout = as_int64(args.at(2));
        const auto& name = args.at(3).bytes.empty() ? host : args.at(3).bytes;
        const auto alpn = alpn_wire(args.at(4));
        auto ca_file = args.at(5).bytes;
        const bool extended = args.size() > 6;
        const auto low = extended ? as_int32(args.at(6)) : 12;
        const auto high = extended ? as_int32(args.at(7)) : 13;
        auto certificate = extended ? args.at(8).bytes : std::string();
        auto key = extended ? args.at(9).bytes : std::string();
        const bool kernel = extended && as_bool(args.at(10));
        if (!usable_text(host) || !usable_text(name) || port < 1 || port > 65535 || timeout < 0 || !alpn ||
            !valid_versions(low, high) || certificate.empty() != key.empty()) return fail(host_error::invalid_input);
        if (const auto error = permitted_paths(*granted, *allowed_paths, {&ca_file, &certificate, &key})) return fail(error);
        if (state->tls.size() >= max_open_ids) return fail(host_error::other);
        const auto deadline = deadline_of(timeout == 0 ? default_transfer_timeout : timeout);
        auto stream = std::make_unique<TlsStream>();
        if (timeout > 0) stream->write_timeout = timeout;
        if (!init_context(*stream, false, low, high, kernel)) return fail(host_error::other);
        SSL_CTX_set_verify(stream->context, SSL_VERIFY_PEER, nullptr);
        if (!load_trust(stream->context, ca_file)) {
            if (ca_file.empty()) return fail(host_error::other);
            return fail(extended ? host_error::invalid_input : host_error::not_found);
        }
        if (!certificate.empty() && !load_identity(stream->context, certificate, key)) return fail(host_error::invalid_input);
        std::uint32_t error = 0;
        stream->descriptor = connect_tcp(host, port, deadline, error);
        if (stream->descriptor == invalid_native_socket) return fail(error);
        if (!attach_socket(*stream)) return fail(host_error::other);
        if (!set_identity_check(stream->ssl, name)) return fail(host_error::invalid_input);
        if (!alpn->empty() && SSL_set_alpn_protos(stream->ssl, reinterpret_cast<const unsigned char*>(alpn->data()),
                                                static_cast<unsigned int>(alpn->size())) != 0) {
            return fail(host_error::invalid_input);
        }
        if (drive(stream->ssl, stream->descriptor, deadline, error, [&] { return SSL_connect(stream->ssl); }) <= 0) {
            if (SSL_get_verify_result(stream->ssl) != X509_V_OK) return fail(host_error::permission_denied);
            return fail(error ? error : host_error::connection_reset);
        }
        if (retain_deadline) stream->deadline = deadline;
        return remember(std::move(stream));
    };
    const auto connect_stream = [=](const HostRequest& request) { return connect(request, false); };
    add(signature("neura.net.tls.connect", {text(), int32(), int64(), text(), text_list(), text()}, stream_result()),
        connect_stream);
    add(connect_extended(), connect_stream);
    add(connect_deadline(), [=](const HostRequest& request) { return connect(request, true); });

    const auto accept = [=](const HostRequest& request) {
        if (!granted->net || !granted->fs) return denied();
        const auto& args = request.arguments;
        const auto listener = state->sockets.find(as_int64(args.at(0)));
        auto certificate = args.at(1).bytes;
        auto key = args.at(2).bytes;
        const auto alpn = alpn_wire(args.at(3));
        const auto timeout = as_int64(args.at(4));
        const bool extended = args.size() > 5;
        const auto low = extended ? as_int32(args.at(5)) : 12;
        const auto high = extended ? as_int32(args.at(6)) : 13;
        auto client_ca = extended ? args.at(7).bytes : std::string();
        const bool kernel = extended && as_bool(args.at(8));
        if (listener == state->sockets.end() || !listener->second.listener || !usable_text(certificate) ||
            !usable_text(key) || !alpn || timeout < 0 || !valid_versions(low, high)) return fail(host_error::invalid_input);
        if (const auto error = permitted_paths(*granted, *allowed_paths, {&certificate, &key, &client_ca})) return fail(error);
        if (state->tls.size() >= max_open_ids) return fail(host_error::other);
        const auto deadline = deadline_of(timeout);
        auto stream = std::make_unique<TlsStream>();
        if (timeout > 0) stream->write_timeout = timeout;
        if (!init_context(*stream, true, low, high, kernel)) return fail(host_error::other);
        if (!load_identity(stream->context, certificate, key)) return fail(host_error::invalid_input);
        if (!client_ca.empty()) {
            if (!load_trust(stream->context, client_ca)) return fail(host_error::invalid_input);
            SSL_CTX_set_verify(stream->context, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
            if (!tls_material::client_names(stream->context, client_ca)) return fail(host_error::invalid_input);
        }
        stream->alpn = *alpn;
        if (!alpn->empty()) SSL_CTX_set_alpn_select_cb(stream->context, server_alpn, &stream->alpn);
        const int ready = wait_for(listener->second.descriptor, platform::poll_in, deadline);
        if (ready == 0) return fail(host_error::timed_out);
        if (ready < 0) return fail(socket_error(platform::last_socket_error().code));
        stream->descriptor = ::accept(listener->second.descriptor, nullptr, nullptr);
        if (stream->descriptor == invalid_native_socket || !prepare_socket(stream->descriptor)) {
            return fail(socket_error(platform::last_socket_error().code));
        }
        if (!attach_socket(*stream)) return fail(host_error::other);
        std::uint32_t error = 0;
        if (drive(stream->ssl, stream->descriptor, deadline, error, [&] { return SSL_accept(stream->ssl); }) <= 0) {
            if (SSL_get_verify_result(stream->ssl) != X509_V_OK ||
                ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE) {
                return fail(host_error::permission_denied);
            }
            return fail(error ? error : host_error::connection_reset);
        }
        return remember(std::move(stream));
    };
    add(signature("neura.net.tls.accept", {int64(), text(), text(), text_list(), int64()}, stream_result()), accept);
    add(accept_extended(), accept);

    // Addressed datagram engines preserve the caller's DatagramSocket abstraction.
    // The BIO's receive source address is installed only after matching the actual
    // sender supplied by receive_from; untrusted packet bytes never choose it.
    add(dtls_open(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& args = request.arguments;
        const auto server = as_bool(args.at(0));
        const auto& name = args.at(1).bytes;
        const auto alpn = alpn_wire(args.at(2));
        auto ca = args.at(3).bytes, certificate = args.at(4).bytes, key = args.at(5).bytes;
        auto local = datagram_address(args.at(6).bytes, as_int32(args.at(7)));
        auto peer = datagram_address(args.at(8).bytes, as_int32(args.at(9)));
        const auto timeout = as_int64(args.at(10));
        if (!alpn || !local || !peer || BIO_ADDR_family(local.get()) != BIO_ADDR_family(peer.get()) ||
            timeout < 0 || (!server && !usable_text(name)) || certificate.empty() != key.empty() ||
            (server && certificate.empty())) return fail(host_error::invalid_input);
        if (const auto error = permitted_paths(*granted, *allowed_paths, {&ca, &certificate, &key})) return fail(error);
        if (state->tls.size() >= max_open_ids) return fail(host_error::other);
        auto stream = std::make_unique<TlsStream>();
        stream->datagram = true;
        stream->deadline = deadline_of(timeout ? timeout : dtls_default_timeout);
        stream->context = SSL_CTX_new(server ? DTLS_server_method() : DTLS_client_method());
        if (!stream->context || SSL_CTX_set_min_proto_version(stream->context, DTLS1_3_VERSION) != 1 ||
            SSL_CTX_set_max_proto_version(stream->context, DTLS1_3_VERSION) != 1) return fail(host_error::unsupported);
        if (!server || !ca.empty()) {
            SSL_CTX_set_verify(stream->context, server ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
                                                       : SSL_VERIFY_PEER, nullptr);
            if (!load_trust(stream->context, ca)) return fail(host_error::invalid_input);
        }
        if (!certificate.empty() && !load_identity(stream->context, certificate, key)) return fail(host_error::invalid_input);
        if (server) {
            stream->alpn = *alpn;
            if (!alpn->empty()) SSL_CTX_set_alpn_select_cb(stream->context, server_alpn, &stream->alpn);
            // Do not set NO_VALIDATE: OpenSSL's default cookies bind the actual peer address.
            stream->listener = SSL_new_listener(stream->context, SSL_LISTENER_FLAG_SINGLE_THREAD |
                                                                SSL_LISTENER_FLAG_ADDRESS_VALIDATION);
            if (!stream->listener) return fail(host_error::other);
        } else {
            stream->ssl = SSL_new(stream->context);
            if (!stream->ssl) return fail(host_error::other);
        }
        auto* endpoint = server ? stream->listener : stream->ssl;
        BIO* protocol = nullptr;
        if (!BIO_new_bio_dgram_pair(&protocol, 262144, &stream->datagram_wire, 262144)) return fail(host_error::other);
        SSL_set_bio(endpoint, protocol, protocol);
        const auto caps = BIO_DGRAM_CAP_HANDLES_DST_ADDR | BIO_DGRAM_CAP_HANDLES_SRC_ADDR;
        if (!BIO_dgram_set_caps(protocol, caps) || !BIO_dgram_set_caps(stream->datagram_wire, caps) ||
            !BIO_dgram_set_mtu(protocol, 1200)) return fail(host_error::other);
        // On the wire half, the local address is the source of incoming packets.
        auto source = datagram_address(args.at(8).bytes, as_int32(args.at(9)));
        if (!source || !BIO_dgram_set0_local_addr(protocol, local.get())) return fail(host_error::other);
        local.release();
        if (!BIO_dgram_set0_local_addr(stream->datagram_wire, source.get())) return fail(host_error::other);
        source.release();
        stream->datagram_peer = peer.release();
        if (server) {
            if (!SSL_set_blocking_mode(endpoint, 0) || !SSL_listen(endpoint)) return fail(host_error::other);
        } else {
            SSL_set_connect_state(endpoint);
            SSL_set_options(endpoint, SSL_OP_NO_QUERY_MTU);
            SSL_set_mtu(endpoint, 1200);
            if (!set_identity_check(endpoint, name)) return fail(host_error::invalid_input);
            if (!alpn->empty() && SSL_set_alpn_protos(endpoint, reinterpret_cast<const unsigned char*>(alpn->data()),
                                                    static_cast<unsigned int>(alpn->size())) != 0)
                return fail(host_error::invalid_input);
        }
        const auto id = state->next_tls++;
        state->tls.emplace(id, std::unique_ptr<TlsStream, TlsDeleter>(stream.release()));
        return HostCompletion::ok(int64_value(id));
    });

    add(dtls_exchange(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& args = request.arguments;
        const auto found = state->tls.find(as_int64(args.at(0)));
        const auto operation = as_int32(args.at(1));
        const auto& inbound = args.at(2).bytes;
        const auto& message = args.at(5).bytes;
        const auto timeout = as_int64(args.at(6));
        if (found == state->tls.end() || !found->second->datagram || operation < 0 || operation > 4 ||
            inbound.size() > 65535 || message.size() > 16384 || timeout < 0 ||
            (operation != 2 && !message.empty())) return fail(host_error::invalid_input);
        auto& stream = *found->second;
        if (stream.terminal_error) return fail(stream.terminal_error);
        // DTLS application records are not fragmented like handshake messages.
        // Reject an oversized message before changing the pending operation.
        if (operation == 2 && (!stream.ssl || !SSL_is_init_finished(stream.ssl) ||
                              message.empty() || message.size() > DTLS_get_data_mtu(stream.ssl)))
            return fail(host_error::invalid_input);
        if (stream.pending_operation == -1) {
            stream.pending_operation = operation;
            stream.deadline = deadline_of(timeout ? timeout : dtls_default_timeout);
        }
        if (stream.pending_operation != operation) return fail(host_error::invalid_input);
        if (Clock::now() >= stream.deadline) {
            stream.terminal_error = host_error::timed_out;
            return fail(stream.terminal_error);
        }
        // Compare parsed numeric addresses, so IPv6 spelling cannot bypass filtering.
        if (!inbound.empty()) {
            auto sender = datagram_address(args.at(3).bytes, as_int32(args.at(4)));
            if (same_datagram_address(sender.get(), stream.datagram_peer) &&
                BIO_write(stream.datagram_wire, inbound.data(), static_cast<int>(inbound.size())) !=
                    static_cast<int>(inbound.size())) return fail(host_error::other);
        }
        ERR_clear_error();
        std::uint32_t failure = 0;
        int status = 0;
        std::string plaintext;
        // Drive the listener before its accepted connection. Both timer sets matter.
        if (stream.listener && !SSL_handle_events(stream.listener)) failure = host_error::connection_reset;
        if (!failure && !stream.ssl && stream.listener) {
            stream.ssl = SSL_accept_connection(stream.listener, SSL_ACCEPT_CONNECTION_NO_BLOCK);
            if (stream.ssl) {
                SSL_set_blocking_mode(stream.ssl, 0);
                SSL_set_options(stream.ssl, SSL_OP_NO_QUERY_MTU);
                SSL_set_mtu(stream.ssl, 1200);
            // A listener has no SSL_CONNECTION for SSL_get_error to inspect;
            // empty nonblocking accept is NULL without an error queue entry.
            } else if (ERR_peek_error() != 0) {
                failure = host_error::connection_reset;
            }
        }
        if (!failure && stream.ssl) {
            auto* ssl = stream.ssl;
            if (operation != 0 && !SSL_is_init_finished(ssl)) return fail(host_error::invalid_input);
            ERR_clear_error();
            int result = 0;
            if (!SSL_handle_events(ssl)) failure = host_error::connection_reset;
            if (!failure) {
                switch (operation) {
                case 0: result = SSL_do_handshake(ssl); break;
                case 1:
                    plaintext.resize(16384);
                    result = SSL_read(ssl, plaintext.data(), static_cast<int>(plaintext.size()));
                    plaintext.resize(result > 0 ? static_cast<std::size_t>(result) : 0);
                    break;
                case 2: result = SSL_write(ssl, message.data(), static_cast<int>(message.size())); break;
                case 3: result = SSL_shutdown(ssl); break;
                case 4:
                    if (!stream.key_update_started) {
                        if (!SSL_key_update(ssl, SSL_KEY_UPDATE_REQUESTED)) { failure = host_error::connection_reset; break; }
                        stream.key_update_started = true;
                    }
                    result = SSL_do_handshake(ssl);
                    break;
                }
                status = 1;
                if (result <= 0 && !(operation == 3 && result == 0)) {
                    const auto reason = SSL_get_error(ssl, result);
                    if (reason == SSL_ERROR_WANT_READ || reason == SSL_ERROR_WANT_WRITE) status = 0;
                    else if (reason == SSL_ERROR_ZERO_RETURN && operation == 1) status = 2;
                    else if (SSL_get_verify_result(ssl) != X509_V_OK ||
                             ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE)
                        failure = host_error::permission_denied;
                    else failure = host_error::connection_reset;
                }
            }
        }
        std::vector<HostValue> outgoing;
        std::size_t total = 0;
        while (const auto size = BIO_ctrl_pending(stream.datagram_wire)) {
            if (size > 65535 || total + size > 262144) { failure = host_error::other; break; }
            std::string packet(size, '\0');
            if (BIO_read(stream.datagram_wire, packet.data(), static_cast<int>(size)) != static_cast<int>(size)) {
                failure = host_error::other; break;
            }
            total += size;
            outgoing.push_back(bytes_value(std::move(packet)));
        }
        const auto wait = datagram_wait(stream);
        if (failure) stream.terminal_error = failure;
        else if (status != 0) {
            stream.pending_operation = -1;
            stream.key_update_started = false;
        }
        return HostCompletion::ok(tuple_value({int32_value(status), tuple_value(std::move(outgoing)),
                                               bytes_value(std::move(plaintext)), int64_value(wait), scalar(failure)}));
    });

    add(dtls_release(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto found = state->tls.find(as_int64(request.arguments.at(0)));
        if (found == state->tls.end() || !found->second->datagram) return fail(host_error::invalid_input);
        state->tls.erase(found);
        return HostCompletion::ok();
    });

    // The buffer engine leaves wire I/O to a caller-provided Neura stream.
    // Files and record processing use the same trust rules as socket TLS.
    add(engine_open(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& args = request.arguments;
        const auto server = as_bool(args.at(0));
        const auto& name = args.at(1).bytes;
        const auto alpn = alpn_wire(args.at(2));
        auto ca_file = args.at(3).bytes;
        auto certificate = args.at(4).bytes;
        auto key = args.at(5).bytes;
        if (!alpn || (!server && !usable_text(name)) || certificate.empty() != key.empty() ||
            (server && certificate.empty())) return fail(host_error::invalid_input);
        if (const auto error = permitted_paths(*granted, *allowed_paths, {&ca_file, &certificate, &key})) return fail(error);
        if (state->tls.size() >= max_open_ids) return fail(host_error::other);
        auto stream = std::make_unique<TlsStream>();
        if (!init_context(*stream, server, 13, 13, false)) return fail(host_error::other);
        if (!server || !ca_file.empty()) {
            SSL_CTX_set_verify(stream->context, server ? SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT
                                                       : SSL_VERIFY_PEER, nullptr);
            if (!load_trust(stream->context, ca_file)) return fail(host_error::invalid_input);
        }
        if (!certificate.empty() && !load_identity(stream->context, certificate, key)) return fail(host_error::invalid_input);
        if (server) {
            stream->alpn = *alpn;
            if (!alpn->empty()) SSL_CTX_set_alpn_select_cb(stream->context, server_alpn, &stream->alpn);
        }
        stream->ssl = SSL_new(stream->context);
        if (!stream->ssl) return fail(host_error::other);
        std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new(BIO_s_mem()), BIO_free);
        std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new(BIO_s_mem()), BIO_free);
        if (!input || !output) return fail(host_error::other);
        SSL_set_bio(stream->ssl, input.release(), output.release());
        if (server) {
            SSL_set_accept_state(stream->ssl);
        } else {
            SSL_set_connect_state(stream->ssl);
            if (!set_identity_check(stream->ssl, name)) return fail(host_error::invalid_input);
            if (!alpn->empty() && SSL_set_alpn_protos(stream->ssl, reinterpret_cast<const unsigned char*>(alpn->data()),
                                                    static_cast<unsigned int>(alpn->size())) != 0) {
                return fail(host_error::invalid_input);
            }
        }
        const auto id = state->next_tls++;
        state->tls.emplace(id, std::unique_ptr<TlsStream, TlsDeleter>(stream.release()));
        return HostCompletion::ok(int64_value(id));
    });

    add(engine_exchange(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& args = request.arguments;
        const auto* stream = find(args.at(0));
        const auto operation = as_int32(args.at(1));
        const auto& ciphertext = args.at(2).bytes;
        const auto& plaintext = args.at(3).bytes;
        if (!stream || stream->descriptor != invalid_native_socket || ciphertext.size() > 65536 ||
            plaintext.size() > 16384 || operation < 0 || operation > 3 ||
            (operation == 2 ? plaintext.empty() : !plaintext.empty())) return fail(host_error::invalid_input);
        auto* ssl = stream->ssl;
        if (!SSL_is_init_finished(ssl) && operation != 0) return fail(host_error::invalid_input);
        auto* input = SSL_get_rbio(ssl);
        auto* output = SSL_get_wbio(ssl);
        if (BIO_ctrl_pending(input) + ciphertext.size() > 131072) return fail(host_error::invalid_input);
        if (!ciphertext.empty() && BIO_write(input, ciphertext.data(), static_cast<int>(ciphertext.size())) !=
                                      static_cast<int>(ciphertext.size())) return fail(host_error::other);
        std::string decrypted;
        int result = 0;
        ERR_clear_error();
        switch (operation) {
        case 0: result = SSL_do_handshake(ssl); break;
        case 1:
            decrypted.resize(16384);
            result = SSL_read(ssl, decrypted.data(), static_cast<int>(decrypted.size()));
            decrypted.resize(result > 0 ? static_cast<std::size_t>(result) : 0);
            break;
        case 2: result = SSL_write(ssl, plaintext.data(), static_cast<int>(plaintext.size())); break;
        case 3: result = SSL_shutdown(ssl); break;
        }
        // 0 needs a read; 1 completed; 2 is a verified close_notify.
        int status = 1;
        std::uint32_t failure = 0;
        if (result <= 0 && !(operation == 3 && result == 0)) {
            const auto reason = SSL_get_error(ssl, result);
            if (reason == SSL_ERROR_WANT_READ) status = 0;
            else if (reason == SSL_ERROR_ZERO_RETURN && operation == 1) status = 2;
            else if (SSL_get_verify_result(ssl) != X509_V_OK ||
                     ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_PEER_DID_NOT_RETURN_A_CERTIFICATE) {
                failure = host_error::permission_denied;
            } else {
                failure = host_error::connection_reset;
            }
        }
        const auto available = BIO_ctrl_pending(output);
        if (available > INT_MAX) return fail(host_error::other);
        std::string outgoing(available, '\0');
        if (!outgoing.empty() && BIO_read(output, outgoing.data(), static_cast<int>(outgoing.size())) !=
                                    static_cast<int>(outgoing.size())) return fail(host_error::other);
        return HostCompletion::ok(tuple_value({int32_value(status), bytes_value(std::move(outgoing)),
                                                bytes_value(std::move(decrypted)), scalar(failure)}));
    });

    add(engine_release(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& id = request.arguments.at(0);
        const auto* stream = find(id);
        if (!stream || stream->descriptor != invalid_native_socket) return fail(host_error::invalid_input);
        state->tls.erase(as_int64(id));
        return HostCompletion::ok();
    });

    add(peer_chain(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto* stream = find(request.arguments.at(0));
        if (!stream) return fail(host_error::invalid_input);
        if (!SSL_is_init_finished(stream->ssl) ||
            !(SSL_get_verify_mode(stream->ssl) & SSL_VERIFY_PEER) ||
            SSL_get_verify_result(stream->ssl) != X509_V_OK) return fail(host_error::permission_denied);
        const auto* chain = SSL_get0_verified_chain(stream->ssl);
        if (!chain || sk_X509_num(chain) == 0) return fail(host_error::permission_denied);
        std::vector<HostValue> result;
        std::size_t total = 0;
        for (int i = 0; i < sk_X509_num(chain); ++i) {
            const auto* cert = sk_X509_value(chain, i);
            const auto size = i2d_X509(cert, nullptr);
            if (size <= 0 || static_cast<std::size_t>(size) > 16 * 1024 * 1024 - total)
                return fail(host_error::other);
            std::string der(static_cast<std::size_t>(size), '\0');
            auto* cursor = reinterpret_cast<unsigned char*>(der.data());
            if (i2d_X509(cert, &cursor) != size) return fail(host_error::other);
            total += der.size();
            result.push_back(bytes_value(std::move(der)));
        }
        return HostCompletion::ok(tuple_value(std::move(result)));
    });

    add(local_address(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto* stream = find(request.arguments.at(0));
        if (!stream || stream->descriptor == invalid_native_socket) return fail(host_error::invalid_input);
        sockaddr_storage address{};
#ifdef _WIN32
        int length = sizeof address;
#else
        socklen_t length = sizeof address;
#endif
        auto* native = reinterpret_cast<sockaddr*>(&address);
        if (::getsockname(stream->descriptor, native, &length) != 0) {
            return fail(socket_error(platform::last_socket_error().code));
        }
        char host[NI_MAXHOST];
        if (::getnameinfo(native, length, host, sizeof host, nullptr, 0, NI_NUMERICHOST) != 0) {
            return fail(host_error::other);
        }
        const auto port = address.ss_family == AF_INET
            ? reinterpret_cast<const sockaddr_in*>(&address)->sin_port
            : reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port;
        return HostCompletion::ok(tuple_value({bytes_value(host), int32_value(ntohs(port))}));
    });

    add(shutdown_write(), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = find(request.arguments.at(0));
        const auto timeout = as_int64(request.arguments.at(1));
        if (!stream || stream->descriptor == invalid_native_socket || timeout < 0) return fail(host_error::invalid_input);
        if (stream->write_closed) return HostCompletion::ok();
        std::uint32_t error = 0;
        const auto deadline = std::min(stream->deadline, deadline_of(timeout));
        const auto notified = drive(stream->ssl, stream->descriptor, deadline, error, [&] {
            const auto result = SSL_shutdown(stream->ssl);
            // 0 means our close_notify was sent and the peer's is still outstanding.
            return result == 0 ? 1 : result;
        });
        if (notified <= 0) return fail(error ? error : host_error::connection_reset);
        stream->write_closed = true;
        return HostCompletion::ok();
    });

    add(signature("neura.net.tls.kernel_offload", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto* stream = find(request.arguments.at(0));
        if (!stream || stream->descriptor == invalid_native_socket) return fail(host_error::invalid_input);
        return BIO_get_ktls_send(SSL_get_wbio(stream->ssl)) && BIO_get_ktls_recv(SSL_get_rbio(stream->ssl))
            ? HostCompletion::ok() : fail(host_error::unsupported);
    });

    add(signature("neura.net.tls.read", {int64(), int32(), int64()}, byte_list()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = find(request.arguments.at(0));
        const auto max = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!stream || stream->descriptor == invalid_native_socket || max < 1 || timeout < 0) return fail(host_error::invalid_input);
        std::string bytes(std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes), '\0');
        std::uint32_t error = 0;
        const auto deadline = std::min(stream->deadline, deadline_of(timeout == 0 ? default_transfer_timeout : timeout));
        const int got = drive(stream->ssl, stream->descriptor, deadline, error, [&] {
            return SSL_read(stream->ssl, bytes.data(), static_cast<int>(bytes.size()));
        });
        if (got <= 0) {
            if (error == 0) return HostCompletion::ok(bytes_value({})); // The peer closed the stream.
            return fail(error);
        }
        bytes.resize(static_cast<std::size_t>(got));
        return HostCompletion::ok(bytes_value(std::move(bytes)));
    });

    const auto write = [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = find(request.arguments.at(0));
        const auto& bytes = request.arguments.at(1).bytes;
        const auto timeout = request.arguments.size() > 2 ? as_int64(request.arguments.at(2))
            : stream ? stream->write_timeout : 0;
        if (timeout <= 0) return fail(host_error::invalid_input);
        if (!stream || stream->descriptor == invalid_native_socket) return fail(host_error::invalid_input);
        if (stream->write_closed) return fail(host_error::connection_reset);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = static_cast<int>(std::min<std::size_t>(bytes.size(), INT32_MAX));
        std::uint32_t error = 0;
        const int sent = drive(stream->ssl, stream->descriptor, std::min(stream->deadline, deadline_of(timeout)), error,
                               [&] { return SSL_write(stream->ssl, bytes.data(), size); });
        if (sent <= 0) return fail(error ? error : host_error::connection_reset);
        return HostCompletion::ok(int32_value(sent));
    };
    add(signature("neura.net.tls.write", {int64(), byte_list()}, int32()), write);
    add({"neura.net.tls.write", 2, {int64(), byte_list(), int64()}, int32(), uint32()}, write);

    add(signature("neura.net.tls.close", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        const auto& id = request.arguments.at(0);
        const auto* stream = find(id);
        if (!stream || stream->descriptor == invalid_native_socket) return fail(host_error::invalid_input);
        state->tls.erase(as_int64(id));
        return HostCompletion::ok();
    });
}

void close_tls(StandardServices::Tables& tables) {
    tables.tls.clear();
    tables.next_tls = 1;
}

#else

struct TlsStream {};
void TlsDeleter::operator()(TlsStream* stream) const { delete stream; }

void add_tls_services(const Adder& add, StandardServices::Tables&, const Capabilities& capabilities,
                      const std::vector<ResolvedPath>&) {
    const auto unsupported = [&capabilities](const HostRequest&) {
        return capabilities.net ? fail(host_error::unsupported) : denied();
    };
    add(signature("neura.net.tls.connect", {text(), int32(), int64(), text(), text_list(), text()}, stream_result()),
        unsupported);
    add(connect_deadline(), unsupported);
    add(signature("neura.net.tls.accept", {int64(), text(), text(), text_list(), int64()}, stream_result()),
        unsupported);
    add(local_address(), unsupported);
    add(peer_chain(), unsupported);
    add(shutdown_write(), unsupported);
    add(dtls_open(), unsupported);
    add(dtls_exchange(), unsupported);
    add(dtls_release(), unsupported);
    add(engine_open(), unsupported);
    add(engine_exchange(), unsupported);
    add(engine_release(), unsupported);
    add(connect_extended(), unsupported);
    add(accept_extended(), unsupported);
    add(signature("neura.net.tls.kernel_offload", {int64()}, void_type()), unsupported);
    add(signature("neura.net.tls.read", {int64(), int32(), int64()}, byte_list()), unsupported);
    add(signature("neura.net.tls.write", {int64(), byte_list()}, int32()), unsupported);
    add({"neura.net.tls.write", 2, {int64(), byte_list(), int64()}, int32(), uint32()}, unsupported);
    add(signature("neura.net.tls.close", {int64()}, void_type()), unsupported);
}

void close_tls(StandardServices::Tables& tables) {
    tables.tls.clear();
    tables.next_tls = 1;
}

#endif

} // namespace neura::nvm::provider
