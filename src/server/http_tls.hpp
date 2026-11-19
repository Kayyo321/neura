#pragma once

#include "server/http_connection.hpp"

#include <filesystem>
#include <memory>

namespace neura::server::http {

class TlsContext;
using TlsContextHandle = std::shared_ptr<const TlsContext>;

// Validates a PEM certificate chain and its private key in a new context.
// Throws std::runtime_error on failure. Publishing a new handle leaves any
// previously copied handles and their connections usable. With
// `offer_http2`, in builds with NEURA_ENABLE_SERVER_HTTP2, ALPN prefers `h2`
// and a connection that selects it is served as HTTP/2.
TlsContextHandle load_tls_context(const std::filesystem::path& certificate_chain,
                                  const std::filesystem::path& key, bool offer_http2 = false);

enum class TlsHandshake { Complete, TimedOut, Failed };

struct TlsConnectionEnd {
    TlsHandshake handshake;
    ConnectionEnd http = ConnectionEnd::StreamFailure; // Defined after a complete handshake.
};

// Takes ownership of the accepted POSIX socket and a snapshot of its context.
// The HTTP head deadline starts once the bounded TLS handshake is complete.
TlsConnectionEnd serve_tls_connection(int descriptor, TlsContextHandle context,
                                     const Responder& respond, const ConnectionLimits& limits = {},
                                     std::chrono::milliseconds handshake_timeout = std::chrono::seconds{10});
inline TlsConnectionEnd serve_tls_connection(int descriptor, TlsContextHandle context,
                                            const BufferedResponder& respond, const ConnectionLimits& limits = {},
                                            std::chrono::milliseconds handshake_timeout = std::chrono::seconds{10}) {
    return serve_tls_connection(descriptor, std::move(context), buffered(respond, limits.response_bytes), limits,
                                handshake_timeout);
}

} // namespace neura::server::http
