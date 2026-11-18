#pragma once

#include "server/http_connection.hpp"

#include <string_view>

namespace neura::server::http {

// The connection preface an HTTP/2 client sends first (RFC 9113, 3.4).
inline constexpr std::string_view http2_client_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

// Serves one HTTP/2 connection (Backend030, "HTTP/2 and decision 9"), in
// builds with NEURA_ENABLE_SERVER_HTTP2. `received` holds bytes already read
// from the stream, which must start the client preface. Each stream is
// served by `respond`, on its own thread, exactly as one HTTP/1.1 request:
// its head at HEADERS, its body from DATA as the responder reads it (stream
// flow control is the backpressure), its response as HEADERS and DATA
// frames. At most `limits.http2.max_concurrent_streams` streams are served
// at once; S-5 applies to the decoded header list. Every responder has
// returned before this function does.
ConnectionEnd serve_http2_connection(ConnectionStream& stream, const Responder& respond,
                                     const ConnectionLimits& limits, std::string_view received = {});

// Reads the start of a plain connection: one that begins with the client
// preface is served as HTTP/2 (h2c with prior knowledge), any other as
// HTTP/1.1, with the bytes read replayed to the HTTP/1.1 decoder.
ConnectionEnd serve_cleartext_connection(ConnectionStream& stream, const Responder& respond,
                                         const ConnectionLimits& limits);

} // namespace neura::server::http
