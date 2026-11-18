#include "server/http_tls.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace {

using namespace neura::server::http;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using std::string;
using std::string_view;

constexpr string_view get = "GET /parts?q=bolt HTTP/1.1\r\nHost: localhost\r\n\r\n";
constexpr string_view closing_head = "HEAD /parts/7 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
constexpr string_view closing_get = "GET /parts/7 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
constexpr string_view continued = "HTTP/1.1 100 Continue\r\n\r\n";

void check(bool condition, string_view message) {
    if (condition) return;
    if (const auto error = ERR_peek_last_error()) {
        std::array<char, 256> detail{};
        ERR_error_string_n(error, detail.data(), detail.size());
        throw std::runtime_error(string(message) + ": " + detail.data());
    }
    throw std::runtime_error(string(message));
}

class Scratch {
public:
    Scratch() {
        auto name = (fs::temp_directory_path() / "neura-http-tls-XXXXXX").string();
        check(::mkdtemp(name.data()) != nullptr, "could not create private certificate directory");
        dir = name;
    }
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(dir, ignored);
    }
    fs::path dir;
};

struct KeyPair {
    fs::path certificate;
    fs::path key;
};

KeyPair make_key_pair(const fs::path& directory, string_view name, long serial) {
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
    using Extension = std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)>;
    const auto close_file = [](std::FILE* file) { std::fclose(file); };
    using File = std::unique_ptr<std::FILE, decltype(close_file)>;
    Key key(EVP_EC_gen("prime256v1"), EVP_PKEY_free);
    Cert cert(X509_new(), X509_free);
    check(key && cert && X509_set_version(cert.get(), 2) == 1 &&
          ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial) == 1 &&
          X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60) &&
          X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600) &&
          X509_set_pubkey(cert.get(), key.get()) == 1, "could not create test certificate");
    auto* subject = X509_get_subject_name(cert.get());
    constexpr unsigned char host[] = "localhost";
    check(subject && X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, host, -1, -1, 0) == 1 &&
          X509_set_issuer_name(cert.get(), subject) == 1, "could not set test certificate names");
    Extension san(X509V3_EXT_conf_nid(nullptr, nullptr, NID_subject_alt_name, "DNS:localhost"), X509_EXTENSION_free);
    check(san && X509_add_ext(cert.get(), san.get(), -1) == 1 && X509_sign(cert.get(), key.get(), EVP_sha256()) > 0,
          "could not sign test certificate");

    const KeyPair paths{directory / (string(name) + "-cert.pem"), directory / (string(name) + "-key.pem")};
    const File cert_file(std::fopen(paths.certificate.c_str(), "w"), close_file);
    const File key_file(std::fopen(paths.key.c_str(), "w"), close_file);
    check(cert_file && key_file && PEM_write_X509(cert_file.get(), cert.get()) == 1 &&
          PEM_write_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1,
          "could not write test credentials");
    return paths;
}

struct Socket {
    Socket() = default;
    explicit Socket(int fd) : descriptor(fd) { check(fd >= 0, "could not open TCP socket"); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : descriptor(other.release()) {}
    ~Socket() { if (descriptor >= 0) ::close(descriptor); }
    int release() { return std::exchange(descriptor, -1); }
    int descriptor = -1;
};

class Loopback {
public:
    Loopback(TlsContextHandle server_tls, BufferedResponder responder, ConnectionLimits limits = {},
             std::chrono::milliseconds handshake_timeout = 2s) {
        const Socket listener(::socket(AF_INET, SOCK_STREAM, 0));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(::bind(listener.descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof address) == 0 &&
              ::listen(listener.descriptor, 1) == 0, "could not listen on ephemeral loopback port");
        socklen_t address_size = sizeof address;
        check(::getsockname(listener.descriptor, reinterpret_cast<sockaddr*>(&address), &address_size) == 0,
              "could not get ephemeral port");
        client_.descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
        const timeval timeout{3, 0};
        check(client_.descriptor >= 0 &&
              ::setsockopt(client_.descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0 &&
              ::setsockopt(client_.descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) == 0 &&
              ::connect(client_.descriptor, reinterpret_cast<const sockaddr*>(&address), address_size) == 0,
              "could not open loopback client");
        Socket accepted(::accept(listener.descriptor, nullptr, nullptr));
        const int send_buffer = 4096;
        check(::setsockopt(accepted.descriptor, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof send_buffer) == 0,
              "could not set server send buffer");
        std::packaged_task task([socket = std::move(accepted), tls = std::move(server_tls), responder = std::move(responder),
                                 limits, handshake_timeout]() mutable {
            return serve_tls_connection(socket.release(), std::move(tls), responder, limits, handshake_timeout);
        });
        result_ = task.get_future();
        server_ = std::jthread(std::move(task));
    }

    int client() const { return client_.descriptor; }
    TlsConnectionEnd end() { return result_.get(); }

private:
    std::future<TlsConnectionEnd> result_;
    std::jthread server_;
    Socket client_; // Destroy before joining the server.
};

class Client {
public:
    Client(int socket, const fs::path& certificate, int version)
        : context_(SSL_CTX_new(TLS_client_method()), SSL_CTX_free), ssl_(nullptr, SSL_free) {
        check(context_ && SSL_CTX_set_min_proto_version(context_.get(), version) == 1 &&
              SSL_CTX_set_max_proto_version(context_.get(), version) == 1 &&
              SSL_CTX_load_verify_locations(context_.get(), certificate.c_str(), nullptr) == 1,
              "could not set client trust or TLS version");
        SSL_CTX_set_verify(context_.get(), SSL_VERIFY_PEER, nullptr);
        if (version == TLS1_1_VERSION) SSL_CTX_set_security_level(context_.get(), 0);
        ssl_.reset(SSL_new(context_.get()));
        constexpr unsigned char protocols[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
        check(ssl_ && SSL_set_fd(ssl_.get(), socket) == 1 && SSL_set1_host(ssl_.get(), "localhost") == 1 &&
              SSL_set_tlsext_host_name(ssl_.get(), "localhost") == 1 &&
              SSL_set_alpn_protos(ssl_.get(), protocols, sizeof protocols) == 0, "could not configure TLS client");
    }

    bool connect() {
        ERR_clear_error();
        return SSL_connect(ssl_.get()) == 1;
    }

    void verify(int version, long serial) const {
        const unsigned char* protocol = nullptr;
        unsigned size = 0;
        SSL_get0_alpn_selected(ssl_.get(), &protocol, &size);
        const auto* certificate = SSL_get0_peer_certificate(ssl_.get());
        check(SSL_version(ssl_.get()) == version && SSL_get_verify_result(ssl_.get()) == X509_V_OK &&
              certificate && ASN1_INTEGER_get(X509_get0_serialNumber(certificate)) == serial && size == 8 &&
              string_view(reinterpret_cast<const char*>(protocol), size) == "http/1.1", "wrong negotiated TLS parameters");
    }

    void send(string_view input) const {
        std::size_t count = 0;
        ERR_clear_error();
        check(SSL_write_ex(ssl_.get(), input.data(), input.size(), &count) == 1 && count == input.size(),
              "TLS client could not send the full request");
    }

    string receive(std::size_t size) const {
        string reply(size, '\0');
        std::size_t have = 0;
        while (have < reply.size()) {
            std::size_t count = 0;
            ERR_clear_error();
            check(SSL_read_ex(ssl_.get(), reply.data() + have, reply.size() - have, &count) == 1,
                  "TLS client did not receive the expected response");
            have += count;
        }
        return reply;
    }

    void finish_writes() const {
        ERR_clear_error();
        check(SSL_shutdown(ssl_.get()) >= 0, "TLS client could not send its close alert");
    }

    string receive_to_close() const {
        string reply;
        std::array<char, 4096> buffer;
        for (;;) {
            std::size_t count = 0;
            ERR_clear_error();
            const int read = SSL_read_ex(ssl_.get(), buffer.data(), buffer.size(), &count);
            if (read == 1) {
                reply.append(buffer.data(), count);
                continue;
            }
            check(SSL_get_error(ssl_.get(), read) == SSL_ERROR_ZERO_RETURN, "TLS server did not close with an authenticated alert");
            finish_writes();
            return reply;
        }
    }

private:
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context_;
    std::unique_ptr<SSL, decltype(&SSL_free)> ssl_;
};

BufferedResponse echo_target(const RequestHead& head, string_view) { return {200, head.target}; }
BufferedResponse unexpected(const RequestHead&, string_view) { throw std::runtime_error("request reached responder"); }

string ok(string_view body, bool head = false, bool close = false) {
    string reply = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
    if (close) reply += "Connection: close\r\n";
    reply += "\r\n";
    if (!head) reply += body;
    return reply;
}

void reject_credentials(const Scratch& scratch, const KeyPair& first, const KeyPair& second) {
    const auto missing = scratch.dir / "absent.pem";
    const auto malformed = scratch.dir / "malformed.pem";
    std::ofstream(malformed) << "not a PEM document\n";
    const std::pair<fs::path, fs::path> invalid[] = {
        {missing, first.key}, {first.certificate, missing}, {malformed, first.key},
        {first.certificate, malformed}, {first.certificate, second.key},
    };
    for (const auto& [certificate, key] : invalid) {
        bool rejected = false;
        try {
            load_tls_context(certificate, key);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "TLS startup accepted invalid certificate/key files");
    }
}

void protocol_and_http(TlsContextHandle context, const KeyPair& pair) {
    for (const int version : {TLS1_2_VERSION, TLS1_3_VERSION}) {
        Loopback server(context, echo_target);
        Client client(server.client(), pair.certificate, version);
        check(client.connect(), "trusted localhost handshake failed");
        client.verify(version, 1);
        client.send(string(get) + string(closing_head));
        check(client.receive_to_close() == ok("/parts?q=bolt") + ok("/parts/7", true, true),
              "TLS changed HTTP response framing or pipeline order");
        const auto end = server.end();
        check(end.handshake == TlsHandshake::Complete && end.http == ConnectionEnd::ResponseClosed,
              "completed TLS exchange did not close normally");
    }

#ifndef OPENSSL_NO_TLS1_1
    Loopback legacy(context, unexpected);
    Client old_client(legacy.client(), pair.certificate, TLS1_1_VERSION);
    check(!old_client.connect(), "server accepted TLS 1.1");
    check(ERR_GET_REASON(ERR_peek_last_error()) == SSL_R_TLSV1_ALERT_PROTOCOL_VERSION &&
          legacy.end().handshake == TlsHandshake::Failed, "legacy client did not receive a protocol-version rejection");
#endif

    Loopback rejected(context, unexpected);
    Client bad_request(rejected.client(), pair.certificate, TLS1_3_VERSION);
    check(bad_request.connect(), "rejection test handshake failed");
    bad_request.send("POST /parts HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n");
    check(bad_request.receive_to_close() == "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n" &&
          rejected.end().http == ConnectionEnd::RejectedRequest, "TLS changed the HTTP rejection response");
}

void expect_and_large_write(TlsContextHandle context, const KeyPair& pair) {
    const string body(256 * 1024, 'x');
    Loopback server(context, [&](const RequestHead&, string_view request) {
        check(request == "bolt", "TLS stream passed an incomplete decoded request body");
        return BufferedResponse{200, body};
    });
    Client client(server.client(), pair.certificate, TLS1_3_VERSION);
    check(client.connect(), "continue test handshake failed");
    client.send("POST /parts HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n"
                "Expect: 100-continue\r\nConnection: close\r\n\r\n");
    check(client.receive(continued.size()) == continued, "TLS did not flush 100 before the client sent its body");
    client.send("4\r\nbo");
    client.send("lt\r\n0\r\n\r\n");
    check(client.receive_to_close() == ok(body, false, true), "TLS partial writes lost or duplicated response bytes");
    check(server.end().http == ConnectionEnd::ResponseClosed, "buffered TLS response did not close");
}

void deadlines_and_truncation(TlsContextHandle context, const KeyPair& pair) {
    Loopback silent(context, unexpected, {}, 50ms);
    check(silent.end().handshake == TlsHandshake::TimedOut, "silent peer did not time out in the TLS handshake");
    char byte;
    check(::recv(silent.client(), &byte, 1, 0) == 0, "handshake timeout left the socket open");

    ConnectionLimits limits;
    limits.head_timeout = 100ms;
    Loopback slow_head(context, unexpected, limits);
    Client slow(slow_head.client(), pair.certificate, TLS1_3_VERSION);
    check(slow.connect(), "head deadline test handshake failed");
    slow.send("GET /pa");
    check(slow.receive_to_close().empty() && slow_head.end().http == ConnectionEnd::HeadTimeout,
          "TLS head read did not honor the HTTP deadline");

    Loopback truncated(context, unexpected);
    Client partial(truncated.client(), pair.certificate, TLS1_2_VERSION);
    check(partial.connect(), "truncation test handshake failed");
    partial.send("POST /parts HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4\r\n\r\nbo");
    partial.finish_writes();
    check(partial.receive_to_close().empty() && truncated.end().http == ConnectionEnd::TruncatedRequest,
          "authenticated TLS EOF did not discard the incomplete request");

    Loopback dropped(context, unexpected);
    Client raw_eof(dropped.client(), pair.certificate, TLS1_2_VERSION);
    check(raw_eof.connect(), "raw EOF test handshake failed");
    raw_eof.send("GET /pa");
    check(::shutdown(dropped.client(), SHUT_WR) == 0 && dropped.end().http == ConnectionEnd::StreamFailure,
          "unexpected TCP EOF was mistaken for an authenticated TLS close");
}

void context_snapshots(TlsContextHandle first, const KeyPair& first_pair, const KeyPair& second_pair) {
    std::atomic<TlsContextHandle> current(std::move(first));
    Loopback original(current.load(), echo_target);
    Client existing(original.client(), first_pair.certificate, TLS1_3_VERSION);
    check(existing.connect(), "original context handshake failed");
    existing.verify(TLS1_3_VERSION, 1);

    current.store(load_tls_context(second_pair.certificate, second_pair.key));
    const auto published = current.load();
    try {
        current.store(load_tls_context(second_pair.certificate, first_pair.key));
    } catch (const std::runtime_error&) {
        check(current.load() == published, "invalid context replaced the published certificate");
    }
    Loopback replacement(current.load(), echo_target);
    Client fresh(replacement.client(), second_pair.certificate, TLS1_2_VERSION);
    check(fresh.connect(), "replacement context handshake failed");
    fresh.verify(TLS1_2_VERSION, 2);
    fresh.send(closing_get);
    existing.send(closing_get);
    check(fresh.receive_to_close() == ok("/parts/7", false, true) && existing.receive_to_close() == ok("/parts/7", false, true),
          "publishing a context interrupted a live request");
    check(replacement.end().http == ConnectionEnd::ResponseClosed && original.end().http == ConnectionEnd::ResponseClosed,
          "connections did not finish on their context snapshots");
}

} // namespace

int main() {
    try {
        const Scratch scratch;
        const auto first = make_key_pair(scratch.dir, "first", 1);
        const auto second = make_key_pair(scratch.dir, "second", 2);
        reject_credentials(scratch, first, second);
        auto context = load_tls_context(first.certificate, first.key);
        protocol_and_http(context, first);
        expect_and_large_write(context, first);
        deadlines_and_truncation(context, first);
        context_snapshots(std::move(context), first, second);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
