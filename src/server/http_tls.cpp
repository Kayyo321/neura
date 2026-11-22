#include "server/http_tls.hpp"
#include "server/http_socket.hpp"
#ifdef NEURA_SERVER_HTTP2_ENABLED
#include "server/http2_connection.hpp"
#endif

#include <algorithm>
#include <array>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdexcept>
#include <unistd.h>
#include <utility>

#if OPENSSL_VERSION_MAJOR != 4 || OPENSSL_VERSION_MINOR != 1
#error The Neura server TLS adapter requires OpenSSL 4.1.
#endif

namespace neura::server::http {

using SslContext = std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)>;

class TlsContext {
public:
    explicit TlsContext(SslContext context) : native(std::move(context)) {}
    const SslContext native;
};

namespace {

using Ssl = std::unique_ptr<SSL, decltype(&SSL_free)>;
using Deadline = ConnectionClock::time_point;

struct Socket {
    explicit Socket(int fd) : descriptor(fd) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket() { if (descriptor >= 0) ::close(descriptor); }
    int descriptor;
};

bool retryable(int error) { return error == EINTR || error == EAGAIN || error == EWOULDBLOCK; }

// Use the same SIGPIPE-safe writes as the plain adapter. The BIO borrows the
// descriptor and never closes it.
int bio_write(BIO* bio, const char* data, std::size_t size, std::size_t* written) {
    BIO_clear_retry_flags(bio);
    *written = 0;
    const auto sent = socket_io::send(*static_cast<const int*>(BIO_get_data(bio)), data, size);
    if (sent > 0 || (sent == 0 && size == 0)) {
        *written = static_cast<std::size_t>(sent);
        return 1;
    }
    if (sent < 0 && retryable(errno)) BIO_set_retry_write(bio);
    return 0;
}

int bio_read(BIO* bio, char* data, std::size_t size, std::size_t* received) {
    BIO_clear_retry_flags(bio);
    *received = 0;
    const auto count = ::recv(*static_cast<const int*>(BIO_get_data(bio)), data, size, 0);
    if (count > 0) {
        *received = static_cast<std::size_t>(count);
        return 1;
    }
    if (count < 0 && retryable(errno)) BIO_set_retry_read(bio);
    return 0;
}

long bio_control(BIO*, int command, long, void*) { return command == BIO_CTRL_FLUSH ? 1 : 0; }

const BIO_METHOD* socket_bio_method() {
    using Method = std::unique_ptr<BIO_METHOD, decltype(&BIO_meth_free)>;
    static const Method method = [] {
        const int index = BIO_get_new_index();
        if (index < 0) return Method(nullptr, BIO_meth_free);
        Method bio(BIO_meth_new(index | BIO_TYPE_SOURCE_SINK | BIO_TYPE_DESCRIPTOR, "Neura server socket"), BIO_meth_free);
        if (bio && BIO_meth_set_read_ex(bio.get(), bio_read) && BIO_meth_set_write_ex(bio.get(), bio_write) &&
            BIO_meth_set_ctrl(bio.get(), bio_control)) return bio;
        return Method(nullptr, BIO_meth_free);
    }();
    return method.get();
}

int refuse_password(char*, int, int, void*) { return 0; }

// ALPN, in the server's order of preference: `h2` first when the context
// offers HTTP/2 (a non-null argument), then `http/1.1`. A client that offers
// neither gets no ALPN answer and is served HTTP/1.1.
int select_protocol(SSL*, const unsigned char** output, unsigned char* size,
                    const unsigned char* offered, unsigned offered_size, void* http2) {
    static constexpr unsigned char with_h2[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    static constexpr unsigned char http11[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    unsigned char* selected = nullptr;
    const auto* protocols = http2 ? with_h2 : http11;
    const unsigned protocols_size = http2 ? sizeof with_h2 : sizeof http11;
    if (SSL_select_next_proto(&selected, size, protocols, protocols_size, offered, offered_size) !=
        OPENSSL_NPN_NEGOTIATED) return SSL_TLSEXT_ERR_NOACK;
    *output = selected;
    return SSL_TLSEXT_ERR_OK;
}

[[noreturn]] void fail_context(std::string message) {
    if (const auto error = ERR_peek_last_error()) {
        std::array<char, 256> detail{};
        ERR_error_string_n(error, detail.data(), detail.size());
        message += ": ";
        message += detail.data();
    }
    ERR_clear_error();
    throw std::runtime_error(message);
}

class TlsStream final : public ConnectionStream {
public:
    TlsStream(int descriptor, Ssl ssl) : descriptor_(descriptor), ssl_(std::move(ssl)) {}

    TlsHandshake accept(Deadline deadline) {
        while (ConnectionClock::now() < deadline) {
            ERR_clear_error();
            const int result = SSL_accept(ssl_.get());
            if (result == 1) return TlsHandshake::Complete;
            const auto status = after_io(result, deadline);
            if (status == StreamStatus::TimedOut) return TlsHandshake::TimedOut;
            if (status != StreamStatus::Ready) return TlsHandshake::Failed;
        }
        return TlsHandshake::TimedOut;
    }

    StreamRead read(std::span<char> output, Deadline deadline) override { return read(output, deadline, -1); }

    StreamRead read(std::span<char> output, Deadline deadline, int wake) override {
        while (ConnectionClock::now() < deadline) {
            std::size_t count = 0;
            ERR_clear_error();
            const int result = SSL_read_ex(ssl_.get(), output.data(), output.size(), &count);
            if (result == 1) return {StreamStatus::Ready, count};
            const auto status = after_io(result, deadline, wake);
            if (status != StreamStatus::Ready) return {status};
        }
        return {StreamStatus::TimedOut};
    }

    StreamStatus write_all(std::string_view input, Deadline deadline) override {
        while (!input.empty()) {
            if (ConnectionClock::now() >= deadline) return StreamStatus::TimedOut;
            std::size_t count = 0;
            ERR_clear_error();
            const int result = SSL_write_ex(ssl_.get(), input.data(), input.size(), &count);
            if (result == 1) {
                input.remove_prefix(count);
                continue;
            }
            // WANT_READ/WRITE must retry with the same buffer and size.
            const auto status = after_io(result, deadline);
            if (status != StreamStatus::Ready) return status;
        }
        return StreamStatus::Ready;
    }

    // True when ALPN selected HTTP/2.
    bool http2() const {
        const unsigned char* protocol = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(ssl_.get(), &protocol, &size);
        return size == 2 && protocol[0] == 'h' && protocol[1] == '2';
    }

    void close_after_response() override {
        const auto deadline = ConnectionClock::now() + std::chrono::milliseconds{100};
        while (ConnectionClock::now() < deadline) {
            ERR_clear_error();
            const int result = SSL_shutdown(ssl_.get());
            if (result == 1) return;
            const int error = SSL_get_error(ssl_.get(), result);
            if (error == SSL_ERROR_WANT_READ) {
                // Our close alert was written. Consume any in-flight records
                // before waiting for the peer's alert, without parsing HTTP.
                std::array<char, 4096> discarded;
                std::size_t budget = 64 * 1024;
                while (budget != 0) {
                    const auto input = read(std::span(discarded).first(std::min(budget, discarded.size())), deadline);
                    if (input.status != StreamStatus::Ready) return;
                    budget -= input.size;
                }
                return;
            }
            if (error != SSL_ERROR_WANT_WRITE ||
                socket_io::wait_for(descriptor_, POLLOUT, deadline) != StreamStatus::Ready) return;
        }
    }

private:
    StreamStatus after_io(int result, Deadline deadline, int wake = -1) {
        switch (SSL_get_error(ssl_.get(), result)) {
        case SSL_ERROR_WANT_READ: return socket_io::wait_for(descriptor_, POLLIN, deadline, wake);
        case SSL_ERROR_WANT_WRITE: return socket_io::wait_for(descriptor_, POLLOUT, deadline, wake);
        case SSL_ERROR_ZERO_RETURN: return StreamStatus::End;
        default:
            ERR_clear_error();
            return StreamStatus::Failed;
        }
    }

    int descriptor_;
    Ssl ssl_;
};

} // namespace

TlsContextHandle load_tls_context(const std::filesystem::path& certificate_chain, const std::filesystem::path& key,
                                  bool offer_http2) {
    ERR_clear_error();
    SslContext context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
    if (!context) fail_context("could not create the server TLS context");
    if (!SSL_CTX_set_min_proto_version(context.get(), TLS1_2_VERSION) ||
        !SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION)) fail_context("could not enable TLS 1.2 and 1.3");
    SSL_CTX_set_default_passwd_cb(context.get(), refuse_password);
    if (SSL_CTX_use_certificate_chain_file(context.get(), certificate_chain.c_str()) != 1) {
        fail_context("could not load the server TLS certificate chain");
    }
    if (SSL_CTX_use_PrivateKey_file(context.get(), key.c_str(), SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(context.get()) != 1) fail_context("could not load a matching server TLS private key");

    // A raw socket EOF must not be reported to the decoder as a close alert.
    SSL_CTX_clear_options(context.get(), SSL_OP_IGNORE_UNEXPECTED_EOF);
    SSL_CTX_set_mode(context.get(), SSL_MODE_ENABLE_PARTIAL_WRITE);
#ifndef NEURA_SERVER_HTTP2_ENABLED
    offer_http2 = false;
#endif
    // The callback's argument only marks whether `h2` is offered.
    static int offered = 1;
    SSL_CTX_set_alpn_select_cb(context.get(), select_protocol, offer_http2 ? &offered : nullptr);
    return std::make_shared<const TlsContext>(std::move(context));
}

TlsConnectionEnd serve_tls_connection(int descriptor, TlsContextHandle context, const Responder& respond,
                                     const ConnectionLimits& limits, std::chrono::milliseconds handshake_timeout) {
    Socket socket(descriptor);
    const auto deadline = ConnectionClock::now() + handshake_timeout;
    if (!context || !socket_io::configure(descriptor)) return {TlsHandshake::Failed};

    ERR_clear_error();
    Ssl ssl(SSL_new(context->native.get()), SSL_free);
    const auto* method = socket_bio_method();
    BIO* bio = ssl && method ? BIO_new(method) : nullptr;
    if (!bio) return {TlsHandshake::Failed};
    BIO_set_data(bio, &socket.descriptor);
    BIO_set_init(bio, 1);
    SSL_set_bio(ssl.get(), bio, bio);
    TlsStream stream(descriptor, std::move(ssl));
    const auto handshake = stream.accept(deadline);
    if (handshake != TlsHandshake::Complete) return {handshake};

#ifdef NEURA_SERVER_HTTP2_ENABLED
    if (stream.http2()) {
        const auto result = serve_http2_connection(stream, respond, limits);
        stream.close_after_response();
        return {handshake, result};
    }
#endif
    const auto result = serve_connection(stream, respond, limits);
    if (result == ConnectionEnd::PeerClosed || result == ConnectionEnd::TruncatedRequest ||
        result == ConnectionEnd::HeadTimeout) stream.close_after_response();
    return {handshake, result};
}

} // namespace neura::server::http
