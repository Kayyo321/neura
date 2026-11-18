#pragma once

// The application host services of a server image (docs/design/Backend030.md,
// "Provider ABI"): neura.http.server.* and neura.app.*. The services are part
// of every runner's registry so that a server image binds anywhere. Only a
// request worker of neura-server-host supplies an exchange; everywhere else,
// and whenever the capability group is not granted, each call answers error 14.

#include "host.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neura::nvm {

using HeaderPairs = std::vector<std::pair<std::string, std::string>>;

// What neura.http.server.next answers.
struct ServerRequest {
    static constexpr std::uint32_t request = 1;
    static constexpr std::uint32_t startup_check = 2;

    std::uint32_t kind = request;
    std::string request_id;
    std::string method;
    std::string target;
    HeaderPairs headers;      // Lowercase names, in arrival order.
    std::int64_t body_length = 0;
    std::string peer;         // The numeric address of the client.
    std::string scheme;       // "http" or "https".
    std::int64_t deadline = 0; // Unix time in nanoseconds.
};

// The worker's side of one assigned request. Every method returns zero or a
// host error code. Implementations check nothing the services already check.
class ServerExchange {
public:
    virtual ~ServerExchange() = default;
    // The assigned request. Called at most once per execution.
    virtual std::optional<ServerRequest> next() = 0;
    // Up to `max` further bytes of the request body into `out`, waiting for
    // them if needed; empty at its end. Nonzero when the body cannot be read
    // to its end (the deadline passed or the client left).
    virtual std::uint32_t read_body(std::size_t max, std::string& out) = 0;
    // Sends the response head and its first body bytes; with `complete` the
    // body ends there.
    virtual std::uint32_t respond(std::uint32_t status, const HeaderPairs& headers, std::string_view body,
                                  bool complete) = 0;
    virtual std::uint32_t write_body(std::string_view bytes) = 0;
    virtual std::uint32_t close_body() = 0;
    // The configuration value of `key`, when the host exposes one.
    virtual std::optional<std::string> config(std::string_view key) = 0;
    virtual void log(std::uint32_t level, std::string_view message, const HeaderPairs& fields) = 0;
    // Adds `value` to the counter `name` (kind 1) or sets the gauge `name`
    // (kind 2) in the host's metrics.
    virtual std::uint32_t metric(std::string_view name, std::uint32_t kind, std::int64_t value) = 0;
};

// The most bytes of response body one execution may send (Apps030 S-7).
inline constexpr std::uint64_t default_response_bytes = 8ull * 1024 * 1024;

// neura.http.server.next() -> [uint32 kind, string id, string method, string target,
//                              string[list] headers, int64 body length, string peer,
//                              string scheme, int64 deadline]
// neura.http.server.body.read(int32 max) -> uint8[list]
// neura.http.server.respond(uint32 status, string[list] headers, uint8[list] body,
//                           bool complete) -> void
// neura.http.server.body.write(uint8[list] bytes) -> void
// neura.http.server.body.close() -> void
// neura.app.config(string key) -> [bool found, string value]
// neura.app.log(uint32 level, string message, string[list] fields) -> void
// neura.app.metric(string name, uint32 kind, int64 value) -> void
//
// Header and log fields are flat lists of alternating names and values.
// All version 1 with a uint32 error. The http.server group needs the
// http_server capability, the app group the app capability. A call out of
// order (a second next, a respond before next or after the head, a body
// write after the body ended), a status outside 100 through 599, or a header
// name or value the HTTP wire cannot carry answers invalid_input (4). A
// complete response body given to respond beyond `response_bytes` (S-7, the
// buffered body) answers too_large (16); a streamed body (respond with
// `complete` false, then body.write) is not buffered and not counted. A
// metric name outside [A-Za-z_:][A-Za-z0-9_:]* (at most 128 bytes), a kind
// other than 1 (counter) or 2 (gauge), or a negative counter increment
// answers invalid_input.
class ApplicationServices {
public:
    ApplicationServices(ServerExchange* exchange, bool http_server, bool app,
                        std::uint64_t response_bytes = default_response_bytes);
    ApplicationServices(const ApplicationServices&) = delete;
    ApplicationServices& operator=(const ApplicationServices&) = delete;
    void register_all(HostRegistry& registry);

private:
    struct State;
    std::vector<std::unique_ptr<HostService>> services_;
    std::shared_ptr<State> state_;
};

// Whether `name` is an HTTP field name (an RFC 9110 token) and `value` a
// field value without CR, LF or NUL.
bool valid_header_name(std::string_view name);
bool valid_header_value(std::string_view value);
// Whether `name` is a Prometheus metric name of at most 128 bytes.
bool valid_metric_name(std::string_view name);

} // namespace neura::nvm
