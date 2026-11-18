#pragma once

#include "server/http_connection.hpp"

namespace neura::server::http {

// Takes ownership of one accepted POSIX TCP socket, including on failure.
ConnectionEnd serve_tcp_connection(int descriptor, const Responder& respond,
                                   const ConnectionLimits& limits = {});
inline ConnectionEnd serve_tcp_connection(int descriptor, const BufferedResponder& respond,
                                          const ConnectionLimits& limits = {}) {
    return serve_tcp_connection(descriptor, buffered(respond, limits.response_bytes), limits);
}

} // namespace neura::server::http
