#pragma once

#include "server/http_tls.hpp"

#include <optional>

namespace neura::server::http {

struct TlsFiles {
    std::filesystem::path certificate_chain;
    std::filesystem::path private_key;
};

struct ListenEndpoint {
    std::string address; // An explicit numeric IPv4 or IPv6 address.
    std::uint16_t port = 0; // Zero asks the OS for an ephemeral port.
    std::optional<TlsFiles> tls;
};

struct BoundEndpoint {
    std::string address;
    std::uint16_t port;
    bool tls;
};

struct ListenerLimits {
    std::size_t connections = 1024; // Apps030 S-4, shared by all endpoints.
    ConnectionLimits http;
    std::chrono::milliseconds handshake_timeout = std::chrono::seconds{10};
};

struct ServeResult {
    ConnectionEnd http;
    std::optional<TlsHandshake> tls;
};

class AcceptedConnection {
public:
    AcceptedConnection(AcceptedConnection&&) noexcept;
    AcceptedConnection& operator=(AcceptedConnection&&) noexcept;
    ~AcceptedConnection();

    std::string_view peer_address() const;
    bool secure() const;
    // Consumes the socket and holds its S-4 slot until the exchange ends.
    ServeResult serve(const BufferedResponder& respond) &&;

private:
    friend class ListenerSet;
    struct State;
    explicit AcceptedConnection(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

enum class AcceptStatus { Accepted, TimedOut, Refused };

struct AcceptResult {
    AcceptStatus status;
    std::optional<AcceptedConnection> connection;
};

// Accept and reload run on the owning foreground loop. An accepted connection
// can be dispatched elsewhere and outlive the set that accepted it.
class ListenerSet {
public:
    // Loads all TLS pairs and binds all sockets before making the set available.
    // Any failure throws and destroys the entire incomplete set.
    explicit ListenerSet(std::span<const ListenEndpoint> endpoints, ListenerLimits limits = {});
    ListenerSet(ListenerSet&&) noexcept;
    ListenerSet& operator=(ListenerSet&&) noexcept;
    ~ListenerSet();

    std::span<const BoundEndpoint> endpoints() const;
    AcceptResult accept_until(ConnectionClock::time_point deadline);

    // Reopens the configured TLS files. A failure throws without replacing any
    // contexts. Previously accepted connections keep their snapshots.
    void reload_tls();

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace neura::server::http
