// One DNS transaction on one RFC 9250 QUIC stream. Uses the same OpenSSL
// build and network capability as the TLS services.
#include "provider_internal.hpp"

#include "platform/socket.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>

#ifdef NEURA_NVM_TLS
#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#endif
#include <openssl/err.h>
#include <openssl/quic.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace neura::nvm::provider {
namespace {

#if defined(NEURA_NVM_TLS) && !defined(OPENSSL_NO_QUIC)
using Clock = std::chrono::steady_clock;
using Context = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;
using Ssl = std::unique_ptr<SSL, decltype(&SSL_free)>;
using Peer = std::unique_ptr<BIO_ADDR, decltype(&BIO_ADDR_free)>;

constexpr auto exchange_timeout = std::chrono::seconds(5);
constexpr unsigned char doq_alpn[] = {3, 'd', 'o', 'q'};
constexpr std::size_t dns_header_size = 12;

std::uint32_t socket_error() {
#ifdef _WIN32
    return host_error::from_wsa(platform::last_socket_error().code);
#else
    return host_error::from_errno(platform::last_socket_error().code);
#endif
}

Peer peer_address(const std::string& host, std::int32_t port) {
    Peer peer(BIO_ADDR_new(), BIO_ADDR_free);
    if (!peer) return peer;
    in_addr ipv4{};
    in6_addr ipv6{};
    const auto network_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &ipv4) == 1) {
        if (BIO_ADDR_rawmake(peer.get(), AF_INET, &ipv4, sizeof ipv4, network_port) == 1) return peer;
    } else if (::inet_pton(AF_INET6, host.c_str(), &ipv6) == 1) {
        if (BIO_ADDR_rawmake(peer.get(), AF_INET6, &ipv6, sizeof ipv6, network_port) == 1) return peer;
    }
    peer.reset();
    return peer;
}

bool verify_identity(SSL* ssl, const std::string& name) {
    auto* parameters = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(parameters, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    auto address = peer_address(name, 0);
    if (address) return X509_VERIFY_PARAM_set1_ip_asc(parameters, name.c_str()) == 1;
    return SSL_set_tlsext_host_name(ssl, name.c_str()) == 1 && SSL_set1_dnsname(ssl, name.c_str()) == 1;
}

std::size_t framed_size(const std::string& payload) {
    return (static_cast<unsigned char>(payload[0]) << 8) | static_cast<unsigned char>(payload[1]);
}

// Wait for OpenSSL's next wire event or retransmission, bounded by the
// transaction deadline. The socket and the SSL objects are nonblocking.
std::uint32_t wait_wire(SSL* connection, platform::native_socket descriptor, Clock::time_point deadline) {
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now()).count();
        if (left <= 0) return host_error::timed_out;
        auto milliseconds = timeout_milliseconds(left);
        timeval timer{};
        int no_timer = 0;
        if (SSL_get_event_timeout(connection, &timer, &no_timer) != 1) return host_error::connection_reset;
        if (!no_timer && timer.tv_sec < INT_MAX / 1000) {
            const auto retry_ms = static_cast<int>(timer.tv_sec * 1000 + (timer.tv_usec + 999) / 1000);
            milliseconds = std::min(milliseconds, retry_ms);
        }
        short events = SSL_net_read_desired(connection) ? platform::poll_in : 0;
        if (SSL_net_write_desired(connection)) events |= platform::poll_out;
        platform::PollItem descriptor_wait{descriptor, events, 0};
        const auto ready = platform::poll(std::span(&descriptor_wait, 1), milliseconds);
        if (ready < 0 && platform::last_socket_error().interrupted()) continue;
        if (ready < 0) return socket_error();
        if (ready > 0 && (descriptor_wait.revents & platform::poll_invalid) != 0) return host_error::other;
        if (ready > 0 && (descriptor_wait.revents & platform::poll_error) != 0) {
            int error = 0;
#ifdef _WIN32
            int length = sizeof error;
#else
            socklen_t length = sizeof error;
#endif
            if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0) {
                return socket_error();
            }
#ifdef _WIN32
            if (error == WSAECONNRESET) return host_error::connection_refused;
            if (error != 0) return host_error::from_wsa(error);
#else
            if (error != 0) return host_error::from_errno(error);
#endif
        }
        if (SSL_handle_events(connection) != 1) {
            return SSL_get_verify_result(connection) != X509_V_OK ? host_error::permission_denied
                                                                  : host_error::connection_reset;
        }
        return 0;
    }
}

// A QUIC stream reports SSL_ERROR_ZERO_RETURN for its FIN. Only the final
// read expects it; earlier FIN truncates the record.
template<typename Operation>
std::uint32_t drive(SSL* connection, SSL* ssl, platform::native_socket descriptor, Clock::time_point deadline,
                    Operation operation, bool expect_fin = false) {
    for (;;) {
        if (Clock::now() >= deadline) return host_error::timed_out;
        ERR_clear_error();
        const int result = operation();
        if (result == 1) return expect_fin ? host_error::invalid_input : 0;
        const int reason = SSL_get_error(ssl, result);
        if (reason == SSL_ERROR_ZERO_RETURN) return expect_fin ? 0 : host_error::unexpected_end;
        if (reason != SSL_ERROR_WANT_READ && reason != SSL_ERROR_WANT_WRITE) {
            if (SSL_get_verify_result(connection) != X509_V_OK) return host_error::permission_denied;
            return host_error::connection_reset;
        }
        if (const auto error = wait_wire(connection, descriptor, deadline)) return error;
    }
}

std::uint32_t read_exact(SSL* connection, SSL* stream, platform::native_socket descriptor, Clock::time_point deadline,
                         std::string& buffer) {
    std::size_t read = 0;
    while (read < buffer.size()) {
        std::size_t got = 0;
        if (const auto error = drive(connection, stream, descriptor, deadline, [&] {
                return SSL_read_ex(stream, buffer.data() + read, buffer.size() - read, &got);
            })) {
            return error;
        }
        if (got == 0) return host_error::unexpected_end;
        read += got;
    }
    return 0;
}

HostCompletion exchange(const std::string& host, std::int32_t port, const std::string& name,
                        const std::string& payload) {
    if (port < 1 || port > 65535 || !usable_text(host) || !usable_text(name) ||
        payload.size() < dns_header_size + 2 || payload.size() > 65537 ||
        framed_size(payload) != payload.size() - 2 || payload[2] != 0 || payload[3] != 0) {
        return fail(host_error::invalid_input);
    }
#ifdef _WIN32
    if (const auto error = platform::ensure_sockets()) return fail(host_error::from_wsa(error.value()));
#endif
    const auto peer = peer_address(host, port);
    if (!peer) return fail(host_error::invalid_input);
    const auto deadline = Clock::now() + exchange_timeout;
    Context context(SSL_CTX_new(OSSL_QUIC_client_method()), SSL_CTX_free);
    if (!context) return fail(host_error::other);
    SSL_CTX_set_verify(context.get(), SSL_VERIFY_PEER, nullptr);
#ifdef _WIN32
    if (SSL_CTX_load_verify_store(context.get(), "org.openssl.winstore:") != 1) {
        return fail(host_error::permission_denied);
    }
#else
    if (SSL_CTX_set_default_verify_paths(context.get()) != 1) return fail(host_error::permission_denied);
#endif
    Ssl ssl(SSL_new(context.get()), SSL_free);
    if (!ssl || !verify_identity(ssl.get(), name) ||
        SSL_set_alpn_protos(ssl.get(), doq_alpn, sizeof doq_alpn) != 0 ||
        SSL_set_default_stream_mode(ssl.get(), SSL_DEFAULT_STREAM_MODE_NONE) != 1 ||
        SSL_set1_initial_peer_addr(ssl.get(), peer.get()) != 1) {
        return fail(host_error::invalid_input);
    }

    const int descriptor = BIO_socket(BIO_ADDR_family(peer.get()), SOCK_DGRAM, IPPROTO_UDP, BIO_SOCK_NONBLOCK);
    if (descriptor < 0) return fail(socket_error());
    platform::set_no_inherit(static_cast<platform::native_socket>(descriptor));
    BIO* datagram = BIO_new_dgram(descriptor, BIO_CLOSE);
    if (datagram == nullptr) {
        BIO_closesocket(descriptor);
        return fail(host_error::other);
    }
    SSL_set_bio(ssl.get(), datagram, datagram);
    if (SSL_set_blocking_mode(ssl.get(), 0) != 1) return fail(host_error::other);
    if (const auto error = drive(ssl.get(), ssl.get(), descriptor, deadline, [&] { return SSL_connect(ssl.get()); })) {
        return fail(error);
    }
    const unsigned char* selected = nullptr;
    unsigned int selected_length = 0;
    SSL_get0_alpn_selected(ssl.get(), &selected, &selected_length);
    if (selected_length != 3 || std::memcmp(selected, doq_alpn + 1, 3) != 0) {
        return fail(host_error::connection_reset);
    }
    Ssl stream(SSL_new_stream(ssl.get(), SSL_STREAM_FLAG_ADVANCE), SSL_free);
    if (!stream) return fail(host_error::connection_reset);
    std::size_t written = 0;
    if (const auto error = drive(ssl.get(), stream.get(), descriptor, deadline, [&] {
            return SSL_write_ex(stream.get(), payload.data(), payload.size(), &written);
        })) {
        return fail(error);
    }
    if (written != payload.size() || SSL_stream_conclude(stream.get(), 0) != 1) {
        return fail(host_error::connection_reset);
    }

    std::string header(2, '\0');
    if (const auto error = read_exact(ssl.get(), stream.get(), descriptor, deadline, header)) return fail(error);
    std::string reply(framed_size(header), '\0');
    if (reply.size() < dns_header_size) return fail(host_error::invalid_input);
    if (const auto error = read_exact(ssl.get(), stream.get(), descriptor, deadline, reply)) return fail(error);
    if (reply[0] != 0 || reply[1] != 0 || (static_cast<unsigned char>(reply[2]) & 0x80) == 0) {
        return fail(host_error::invalid_input);
    }
    char extra = '\0';
    std::size_t got = 0;
    if (const auto error = drive(ssl.get(), stream.get(), descriptor, deadline, [&] {
            return SSL_read_ex(stream.get(), &extra, 1, &got);
        }, true)) {
        return fail(error);
    }
    static_cast<void>(SSL_shutdown_ex(ssl.get(), SSL_SHUTDOWN_FLAG_NO_BLOCK, nullptr, 0));
    return HostCompletion::ok(bytes_value(std::move(reply)));
}
#endif

} // namespace

void add_doq_services(const Adder& add, const Capabilities& capabilities) {
    const auto* granted = &capabilities;
    add(signature("neura.net.quic.dns_exchange", {text(), int32(), text(), byte_list()}, byte_list()),
        [=](const HostRequest& request) {
            if (!granted->net) return denied();
#if defined(NEURA_NVM_TLS) && !defined(OPENSSL_NO_QUIC)
            return exchange(request.arguments.at(0).bytes, as_int32(request.arguments.at(1)),
                            request.arguments.at(2).bytes, request.arguments.at(3).bytes);
#else
            (void)request;
            return fail(host_error::unsupported);
#endif
        });
}

} // namespace neura::nvm::provider
