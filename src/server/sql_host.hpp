#pragma once

// The SQL side of neura-server-host (docs/runtime/Sql.md): pools from the
// host's options and configuration, migrations at start, and the answer to
// a worker's SQL call. Pool settings and secrets never reach a worker.

#include "nvm/sql_services.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace neura::sql {
class Engine;
class Session;
} // namespace neura::sql

namespace neura::server {

class SqlHost {
public:
    // `pool_options` are the values of --sql-pool; every configuration key
    // that starts with "sql." is a pool setting and is removed from
    // `config`. `base` resolves relative migration directories. Throws
    // std::runtime_error with every problem found.
    SqlHost(const std::vector<std::string>& pool_options, std::map<std::string, std::string, std::less<>>& config,
            const std::filesystem::path& base);
    ~SqlHost();

    bool enabled() const;
    // Opens one connection of each pool and checks its migrations: applies
    // them with migrate = "on_start" or when `apply_all`, and otherwise
    // fails while any is pending. Returns the problem, or nothing.
    std::optional<std::string> prepare(bool apply_all, std::vector<std::string>& messages);

    // A session for one request; its destructor rolls back and returns what
    // the request still holds.
    std::shared_ptr<sql::Session> session(std::chrono::steady_clock::time_point deadline);
    // Answers a Call body of operation 2 with a Return body.
    static std::vector<std::uint8_t> answer(sql::Session* session, std::span<const std::uint8_t> body);

private:
    std::unique_ptr<sql::Engine> engine_;
};

// Whether a Call body is an SQL call (operation 2).
bool is_sql_call(std::span<const std::uint8_t> body);

} // namespace neura::server
