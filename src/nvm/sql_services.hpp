#pragma once

// The SQL host services of Std.Data.Sql (docs/design/Backend030.md, "SQL",
// and docs/runtime/Sql.md): neura.sql.*. Like the application services they
// are part of every runner's registry, so that an image that uses SQL binds
// anywhere and replays without a database. The services hold no connection
// themselves: each call is handed to an SqlExchange, which a request worker
// forwards to neura-server-host and the command-line runner answers with an
// in-process engine. Without an exchange, or without the sql capability
// group, every call answers error 14.

#include "host.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neura::nvm {

// The operation numbers, which are also the SQL operation numbers of the
// worker channel's Call frame (operation 2).
enum class SqlOperation : std::uint32_t {
    Pool = 1,
    Acquire = 2,
    Release = 3,
    Begin = 4,
    Commit = 5,
    Rollback = 6,
    Execute = 7,
    Query = 8,
    OpenCursor = 9,
    Fetch = 10,
    CloseCursor = 11,
};

inline constexpr std::uint32_t sql_operation_count = 11;

// The answer of one SQL call: the completion and, for a failure, the detail
// text (a constraint, column or type name, or the database's message, never
// row data). The library reads the detail with neura.sql.detail.
struct SqlAnswer {
    HostCompletion completion;
    std::string detail;
};

class SqlExchange {
public:
    virtual ~SqlExchange() = default;
    // Arguments arrive as the service's signature describes them, already
    // checked against it by the NVM.
    virtual SqlAnswer call(SqlOperation operation, const std::vector<HostValue>& arguments) = 0;
};

// The value encoding of parameters and rows. A host call cannot carry a list
// of tuples (E0801), so the values of a parameter list or of a page of rows
// cross as four parallel lists:
//   kinds  uint8[list]   one kind per value
//   bits   uint64[list]  Bool 0 or 1, Int the int64 bits, Float the float64
//                        bits, Text and Bytes the index into texts or blobs
//   texts  string[list]  the Text values, in order
//   blobs  uint8[list][list] the Bytes values, in order
// A page of rows lists its values row by row; the row count is the value
// count divided by the column count.
namespace sql_kind {
inline constexpr std::uint8_t null = 0;
inline constexpr std::uint8_t boolean = 1;
inline constexpr std::uint8_t integer = 2;
inline constexpr std::uint8_t real = 3;
inline constexpr std::uint8_t text = 4;
inline constexpr std::uint8_t bytes = 5;
} // namespace sql_kind

// neura.sql.pool(string name) -> void                          NotFound when the host has no such pool
// neura.sql.acquire(string pool, int64 wait_nanos) -> int64    0 waits the pool's S-10
// neura.sql.release(int64 connection) -> void
// neura.sql.begin / commit / rollback(int64 connection) -> void
// neura.sql.execute(int64 connection, string sql, values) -> int64 affected rows
// neura.sql.query(int64 connection, string sql, values, int32 limit)
//     -> [string[list] columns, values, bool more]
// neura.sql.open_cursor(int64 connection, string sql, values) -> [int64 cursor, string[list] columns]
// neura.sql.fetch(int64 cursor, int32 max) -> [values, bool more]
// neura.sql.close_cursor(int64 cursor) -> void
// neura.sql.detail() -> string                                 the detail of the last failed call
//
// `values` stands for the four lists above, as four parameters or four
// tuple fields. All version 1 with a uint32 error. The services do not
// overlap: a call holds up the other tasks of its execution until it ends.
class SqlServices {
public:
    SqlServices(SqlExchange* exchange, bool granted);
    SqlServices(const SqlServices&) = delete;
    SqlServices& operator=(const SqlServices&) = delete;
    void register_all(HostRegistry& registry);

private:
    struct State;
    std::vector<std::unique_ptr<HostService>> services_;
    std::shared_ptr<State> state_;
};

} // namespace neura::nvm
