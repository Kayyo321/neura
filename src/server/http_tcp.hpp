#pragma once

#include "server/http_connection.hpp"

namespace neura::server::http {

// Takes ownership of one accepted POSIX TCP socket, including on failure.
ConnectionEnd serve_tcp_connection(int descriptor, const BufferedResponder& respond,
                                   const ConnectionLimits& limits = {});

} // namespace neura::server::http
