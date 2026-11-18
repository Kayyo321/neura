// TLS streams for the standard library (docs/runtime/NVM.md, "TLS services"):
// a client stream that verifies its peer, and a server stream accepted from
// a TCP listener, both with ALPN. Built with OpenSSL 3.5 when the build has
// NEURA_ENABLE_SERVER_TLS (NEURA_NVM_TLS); otherwise each call with the net
// capability answers Unsupported (13). None of these services overlaps.

#include "provider_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#if defined(NEURA_NVM_TLS) && !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neura::nvm::provider {

namespace {

HostType stream_result() { return tuple({int64(), text()}); }

} // namespace

#if defined(NEURA_NVM_TLS) && !defined(_WIN32)

struct TlsStream {
    int descriptor = -1;
    SSL_CTX* context = nullptr;
    SSL* ssl = nullptr;
    ~TlsStream() {
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (context) SSL_CTX_free(context);
        if (descriptor >= 0) ::close(descriptor);
    }
};

void TlsDeleter::operator()(TlsStream* stream) const { delete stream; }

namespace {

using Clock = std::chrono::steady_clock;

// A deadline `nanos` from now; zero waits without limit.
Clock::time_point deadline_of(std::int64_t nanos) {
    return nanos > 0 ? Clock::now() + std::chrono::nanoseconds(nanos) : Clock::time_point::max();
}

// 1 ready, 0 timed out, -1 failed.
int wait_for(int descriptor, short events, Clock::time_point deadline) {
    for (;;) {
        int milliseconds = -1;
        if (deadline != Clock::time_point::max()) {
            const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count();
            if (left <= 0) return 0;
            milliseconds = timeout_milliseconds(left);
        }
        pollfd entry{descriptor, events, 0};
        const int ready = ::poll(&entry, 1, milliseconds);
        if (ready >= 0 || errno != EINTR) return ready > 0 ? 1 : ready;
    }
}

// Repeats an SSL operation until it completes, waiting for the socket as it
// asks. Returns the operation's result, or 0 with `error` set.
template<typename Operation>
int drive(SSL* ssl, int descriptor, Clock::time_point deadline, std::uint32_t& error, Operation operation) {
    for (;;) {
        ERR_clear_error();
        const int result = operation();
        if (result > 0) return result;
        const int reason = SSL_get_error(ssl, result);
        short events = 0;
        if (reason == SSL_ERROR_WANT_READ) events = POLLIN;
        else if (reason == SSL_ERROR_WANT_WRITE) events = POLLOUT;
        else {
            error = reason == SSL_ERROR_ZERO_RETURN ? 0 : reason == SSL_ERROR_SYSCALL && errno != 0
                ? host_error::from_errno(errno) : host_error::connection_reset;
            return 0;
        }
        const int ready = wait_for(descriptor, events, deadline);
        if (ready == 0) {
            error = host_error::timed_out;
            return 0;
        }
        if (ready < 0) {
            error = host_error::from_errno(errno);
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

int connect_tcp(const std::string& host, std::int32_t port, Clock::time_point deadline, std::uint32_t& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const auto service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0) {
        error = host_error::not_found;
        return -1;
    }
    error = host_error::connection_refused;
    int connected = -1;
    for (const auto* entry = found; entry && connected < 0; entry = entry->ai_next) {
        const int descriptor = ::socket(entry->ai_family, SOCK_STREAM, 0);
        if (descriptor < 0) continue;
        ::fcntl(descriptor, F_SETFD, FD_CLOEXEC);
        ::fcntl(descriptor, F_SETFL, ::fcntl(descriptor, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        if (::connect(descriptor, entry->ai_addr, entry->ai_addrlen) == 0) {
            connected = descriptor;
            break;
        }
        if (errno == EINPROGRESS) {
            const int ready = wait_for(descriptor, POLLOUT, deadline);
            int problem = 0;
            socklen_t length = sizeof problem;
            if (ready == 1 && ::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &problem, &length) == 0 && problem == 0) {
                connected = descriptor;
                break;
            }
            error = ready == 0 ? host_error::timed_out : host_error::from_errno(problem ? problem : errno);
        } else {
            error = host_error::from_errno(errno);
        }
        ::close(descriptor);
        if (error == host_error::timed_out) break;
    }
    ::freeaddrinfo(found);
    return connected;
}

bool numeric_host(const std::string& host) {
    unsigned char buffer[16];
    return ::inet_pton(AF_INET, host.c_str(), buffer) == 1 || ::inet_pton(AF_INET6, host.c_str(), buffer) == 1;
}

// One-time OpenSSL setup is implicit in 3.x; contexts are per stream so a
// stream's trust settings never leak into another.
SSL_CTX* new_context(bool server) {
    SSL_CTX* context = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());
    if (context) SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    return context;
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

} // namespace

void add_tls_services(const Adder& add, StandardServices::Tables& tables, const Capabilities& capabilities) {
    auto* state = &tables;
    const auto* granted = &capabilities;
    const auto remember = [state](std::unique_ptr<TlsStream> stream) {
        auto ssl = stream->ssl;
        const auto id = state->next_tls++;
        state->tls.emplace(id, std::unique_ptr<TlsStream, TlsDeleter>(stream.release()));
        return HostCompletion::ok(tuple_value({int64_value(id), bytes_value(selected_protocol(ssl))}));
    };
    const auto find = [state](const HostValue& id) -> TlsStream* {
        const auto found = state->tls.find(as_int64(id));
        return found == state->tls.end() ? nullptr : found->second.get();
    };

    // host, port, timeout, server name (empty: the host), ALPN names, a CA
    // file (empty: the system trust store). The peer is always verified,
    // and its name or address must match.
    add(signature("neura.net.tls.connect", {text(), int32(), int64(), text(), text_list(), text()}, stream_result()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
            const auto& host = request.arguments.at(0).bytes;
            const auto port = as_int32(request.arguments.at(1));
            const auto timeout = as_int64(request.arguments.at(2));
            const auto& name = request.arguments.at(3).bytes.empty() ? host : request.arguments.at(3).bytes;
            const auto alpn = alpn_wire(request.arguments.at(4));
            const auto& ca_file = request.arguments.at(5).bytes;
            if (!usable_text(host) || port < 1 || port > 65535 || timeout < 0 || !alpn ||
                name.find('\0') != std::string::npos || ca_file.find('\0') != std::string::npos) {
                return fail(host_error::invalid_input);
            }
            if (state->tls.size() >= max_open_ids) return fail(host_error::other);
            const auto deadline = deadline_of(timeout);
            auto stream = std::make_unique<TlsStream>();
            stream->context = new_context(false);
            if (!stream->context) return fail(host_error::other);
            SSL_CTX_set_verify(stream->context, SSL_VERIFY_PEER, nullptr);
            const bool trust = ca_file.empty() ? SSL_CTX_set_default_verify_paths(stream->context) == 1
                                               : SSL_CTX_load_verify_locations(stream->context, ca_file.c_str(), nullptr) == 1;
            if (!trust) return fail(ca_file.empty() ? host_error::other : host_error::not_found);
            std::uint32_t error = 0;
            stream->descriptor = connect_tcp(host, port, deadline, error);
            if (stream->descriptor < 0) return fail(error);
            stream->ssl = SSL_new(stream->context);
            if (!stream->ssl || SSL_set_fd(stream->ssl, stream->descriptor) != 1) return fail(host_error::other);
            // The name check must be in place, or the call fails: verification
            // never falls back to the chain alone.
            const bool named = numeric_host(name)
                ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(stream->ssl), name.c_str()) == 1
                : SSL_set_tlsext_host_name(stream->ssl, name.c_str()) == 1 && SSL_set1_host(stream->ssl, name.c_str()) == 1;
            if (!named) return fail(host_error::invalid_input);
            if (!alpn->empty()) {
                SSL_set_alpn_protos(stream->ssl, reinterpret_cast<const unsigned char*>(alpn->data()),
                                    static_cast<unsigned int>(alpn->size()));
            }
            if (drive(stream->ssl, stream->descriptor, deadline, error, [&] { return SSL_connect(stream->ssl); }) <= 0) {
                // A failed verification is PermissionDenied: the peer is not the one named.
                if (SSL_get_verify_result(stream->ssl) != X509_V_OK) return fail(host_error::permission_denied);
                return fail(error ? error : host_error::connection_reset);
            }
            return remember(std::move(stream));
        });

    // Accepts one connection from a TCP listener of neura.net.listen and
    // completes the server handshake with the PEM certificate chain and key
    // files and the ALPN names the server prefers, in order.
    add(signature("neura.net.tls.accept", {int64(), text(), text(), text_list(), int64()}, stream_result()),
        [=](const HostRequest& request) {
            if (!granted->net || !granted->fs) return denied();
            const auto listener = state->sockets.find(as_int64(request.arguments.at(0)));
            const auto& certificate = request.arguments.at(1).bytes;
            const auto& key = request.arguments.at(2).bytes;
            const auto alpn = alpn_wire(request.arguments.at(3));
            const auto timeout = as_int64(request.arguments.at(4));
            if (listener == state->sockets.end() || !listener->second.listener || !usable_text(certificate) ||
                !usable_text(key) || !alpn || timeout < 0) {
                return fail(host_error::invalid_input);
            }
            if (state->tls.size() >= max_open_ids) return fail(host_error::other);
            const auto deadline = deadline_of(timeout);
            auto stream = std::make_unique<TlsStream>();
            stream->context = new_context(true);
            if (!stream->context) return fail(host_error::other);
            if (SSL_CTX_use_certificate_chain_file(stream->context, certificate.c_str()) != 1 ||
                SSL_CTX_use_PrivateKey_file(stream->context, key.c_str(), SSL_FILETYPE_PEM) != 1 ||
                SSL_CTX_check_private_key(stream->context) != 1) {
                return fail(host_error::invalid_input);
            }
            auto wire = std::make_shared<std::string>(*alpn);
            if (!wire->empty()) SSL_CTX_set_alpn_select_cb(stream->context, server_alpn, wire.get());
            const int ready = wait_for(listener->second.descriptor, POLLIN, deadline);
            if (ready == 0) return fail(host_error::timed_out);
            stream->descriptor = ::accept(listener->second.descriptor, nullptr, nullptr);
            if (stream->descriptor < 0) return fail(host_error::from_errno(errno));
            ::fcntl(stream->descriptor, F_SETFD, FD_CLOEXEC);
            ::fcntl(stream->descriptor, F_SETFL, ::fcntl(stream->descriptor, F_GETFL, 0) | O_NONBLOCK);
            stream->ssl = SSL_new(stream->context);
            if (!stream->ssl || SSL_set_fd(stream->ssl, stream->descriptor) != 1) return fail(host_error::other);
            std::uint32_t error = 0;
            if (drive(stream->ssl, stream->descriptor, deadline, error, [&] { return SSL_accept(stream->ssl); }) <= 0) {
                return fail(error ? error : host_error::connection_reset);
            }
            // The callback's protocol list is needed only during the handshake.
            SSL_CTX_set_alpn_select_cb(stream->context, nullptr, nullptr);
            return remember(std::move(stream));
        });

    add(signature("neura.net.tls.read", {int64(), int32(), int64()}, byte_list()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = find(request.arguments.at(0));
        const auto max = as_int32(request.arguments.at(1));
        const auto timeout = as_int64(request.arguments.at(2));
        if (!stream || max < 1 || timeout < 0) return fail(host_error::invalid_input);
        std::string bytes(std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes), '\0');
        std::uint32_t error = 0;
        const int got = drive(stream->ssl, stream->descriptor, deadline_of(timeout), error, [&] {
            return SSL_read(stream->ssl, bytes.data(), static_cast<int>(bytes.size()));
        });
        if (got <= 0) {
            if (error == 0) return HostCompletion::ok(bytes_value({})); // The peer closed the stream.
            return fail(error);
        }
        bytes.resize(static_cast<std::size_t>(got));
        return HostCompletion::ok(bytes_value(std::move(bytes)));
    });

    add(signature("neura.net.tls.write", {int64(), byte_list()}, int32()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        auto* stream = find(request.arguments.at(0));
        const auto& bytes = request.arguments.at(1).bytes;
        if (!stream) return fail(host_error::invalid_input);
        if (bytes.empty()) return HostCompletion::ok(int32_value(0));
        const auto size = static_cast<int>(std::min<std::size_t>(bytes.size(), INT32_MAX));
        std::uint32_t error = 0;
        const int sent = drive(stream->ssl, stream->descriptor, Clock::time_point::max(), error,
                               [&] { return SSL_write(stream->ssl, bytes.data(), size); });
        if (sent <= 0) return fail(error ? error : host_error::connection_reset);
        return HostCompletion::ok(int32_value(sent));
    });

    add(signature("neura.net.tls.close", {int64()}, void_type()), [=](const HostRequest& request) {
        if (!granted->net) return denied();
        if (state->tls.erase(as_int64(request.arguments.at(0))) == 0) return fail(host_error::invalid_input);
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

void add_tls_services(const Adder& add, StandardServices::Tables&, const Capabilities& capabilities) {
    const auto unsupported = [&capabilities](const HostRequest&) {
        return capabilities.net ? fail(host_error::unsupported) : denied();
    };
    add(signature("neura.net.tls.connect", {text(), int32(), int64(), text(), text_list(), text()}, stream_result()),
        unsupported);
    add(signature("neura.net.tls.accept", {int64(), text(), text(), text_list(), int64()}, stream_result()),
        unsupported);
    add(signature("neura.net.tls.read", {int64(), int32(), int64()}, byte_list()), unsupported);
    add(signature("neura.net.tls.write", {int64(), byte_list()}, int32()), unsupported);
    add(signature("neura.net.tls.close", {int64()}, void_type()), unsupported);
}

void close_tls(StandardServices::Tables& tables) {
    tables.tls.clear();
    tables.next_tls = 1;
}

#endif

} // namespace neura::nvm::provider
