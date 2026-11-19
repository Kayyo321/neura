#pragma once

// The provider entry points of the SQL engine. Internal to src/sql.

#include "sql/sql_engine.hpp"

namespace neura::sql {

// Opens a connection of `config` (whose URL starts with sqlite:), or
// returns a failure.
Status open_sqlite(const PoolConfig& config, const Wait& wait, std::unique_ptr<Connection>& connection);
// The same for postgresql:// URLs and key=value strings. In a build without
// NEURA_ENABLE_POSTGRES it answers Unsupported.
Status open_postgres(const PoolConfig& config, const Wait& wait, std::unique_ptr<Connection>& connection);
// Checks a PostgreSQL connection string without connecting: it parses, it
// holds no password, and it lowers sslmode only for a loopback host.
Status check_postgres_url(const PoolConfig& config);

// Whether the URL names an SQLite database.
inline bool is_sqlite_url(std::string_view url) { return url.starts_with("sqlite:"); }

// Milliseconds left until the deadline, at least 1 and at most INT32_MAX;
// 0 when it has passed.
int milliseconds_left(const Wait& wait);

// The detail texts of misuse that both providers report the same way.
inline constexpr std::string_view one_cursor_detail = "a cursor is open on this connection; close it first";

} // namespace neura::sql
