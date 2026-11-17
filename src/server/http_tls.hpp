#pragma once

#include "server/http_connection.hpp"

#include <filesystem>
#include <memory>

namespace neura::server::http {

class TlsContext;
using TlsContextHandle = std::shared_ptr<const TlsContext>;

// Validates a PEM certificate chain and its private key in a new context.
// Throws std::runtime_error on failure. Publishing a new handle leaves any
// previously copied handles and their connections usable.
TlsContextHandle load_tls_context(const std::filesystem::path& certificate_chain,
                                  const std::filesystem::path& key);

enum class TlsHandshake { Complete, TimedOut, Failed };

struct TlsConnectionEnd {
    TlsHandshake handshake;
    ConnectionEnd http = ConnectionEnd::StreamFailure; // Defined after a complete handshake.
};

// Takes ownership of the accepted POSIX socket and a snapshot of its context.
// The HTTP head deadline starts once the bounded TLS handshake is complete.
TlsConnectionEnd serve_tls_connection(int descriptor, TlsContextHandle context,
                                     const BufferedResponder& respond, const ConnectionLimits& limits = {},
                                     std::chrono::milliseconds handshake_timeout = std::chrono::seconds{10});

} // namespace neura::server::http
