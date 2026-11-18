#pragma once

// The SQL pools and providers behind neura.sql.* (docs/design/Backend030.md,
// "SQL" and "Migrations"; docs/runtime/Sql.md). The engine lives in
// neura-server-host, which owns every connection: a request worker holds
// only the handles of its session, and the host rolls back and returns its
// connections when the request ends. `neura run` and `neura test` use the
// same engine in process with --sql-pool.
//
// Providers: SQLite (compiled in from the pinned amalgamation) and, in
// builds with NEURA_ENABLE_POSTGRES, PostgreSQL over libpq in non-blocking
// mode.

#include "nvm/sql_services.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura::sql {

using Clock = std::chrono::steady_clock;

// One SQL value; `kind` is an nvm::sql_kind number. Bool and Int use
// `integer`, Float `real`, Text and Bytes `bytes`.
struct Value {
    std::uint8_t kind = nvm::sql_kind::null;
    std::int64_t integer = 0;
    double real = 0;
    std::string bytes;
};

// Zero, or a host error code with its detail text.
struct Status {
    std::uint32_t code = 0;
    std::string detail;

    bool ok() const { return code == 0; }
    static Status success() { return {}; }
    static Status failure(std::uint32_t code, std::string detail) { return {code, std::move(detail)}; }
};

// What a statement call may wait for: its deadline, and a flag another
// thread sets to cancel it.
struct Wait {
    Clock::time_point deadline = Clock::time_point::max();
    const std::atomic<bool>* cancelled = nullptr;

    bool expired() const { return Clock::now() >= deadline; }
    bool cancel_requested() const { return cancelled && cancelled->load(); }
    // TimedOut or Cancelled, whichever ended the wait.
    Status interrupted() const;
};

// The rows of an open statement, read in pages.
class Cursor {
public:
    virtual ~Cursor() = default;
    virtual const std::vector<std::string>& columns() const = 0;
    // Appends up to `max` rows, row by row, to `values`; `more` tells
    // whether another row follows. Stops early, before a row that would
    // take the values past `budget` bytes (16 per value and its payload),
    // but always returns at least one row when there is one.
    virtual Status fetch(std::size_t max, std::size_t budget, const Wait& wait, std::vector<Value>& values,
                         bool& more) = 0;
};

class Connection {
public:
    virtual ~Connection() = default;
    virtual Status execute(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                           std::int64_t& affected) = 0;
    // One statement whose rows are read through the cursor. A connection has
    // at most one open cursor; it must be destroyed before the next call.
    virtual Status open(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                        std::unique_ptr<Cursor>& cursor) = 0;
    virtual Status begin(const Wait& wait) = 0;
    // OutcomeUnknown when the connection failed after COMMIT was sent.
    virtual Status commit(const Wait& wait) = 0;
    virtual Status rollback(const Wait& wait) = 0;
    // Statements separated by semicolons, without parameters: a migration.
    virtual Status execute_script(std::string_view sql, const Wait& wait) = 0;
    // Back to a clean state for the next lease: no transaction, no open
    // statement, no session state. False when that cannot be established;
    // the pool then closes the connection.
    virtual bool reset(const Wait& wait) = 0;
    // Whether the connection can still be used.
    virtual bool usable() const = 0;
    // The database: "sqlite" or "postgresql".
    virtual std::string_view provider() const = 0;
};

enum class MigrateMode { Manual, OnStart };

struct PoolConfig {
    std::string name;
    // sqlite:<path>, sqlite::memory:, or a postgresql:// or postgres:// URI
    // or key=value connection string without a password.
    std::string url;
    // The password, from the host's configuration only. Never logged.
    std::optional<std::string> password;
    std::size_t max_connections = 8;          // S-9
    std::size_t max_waiting = 64;             // S-10 acquires waiting
    std::chrono::seconds acquire_timeout{5};  // S-10 seconds
    std::size_t page_rows = 1000;             // S-11
    std::size_t page_bytes = 1000 * 1000;     // below the worker channel's C-2 frame
    std::optional<std::filesystem::path> migrations;
    MigrateMode migrate = MigrateMode::Manual;

    // The URL with any password replaced by "***", for messages.
    std::string redacted_url() const;
};

class Pool {
public:
    explicit Pool(PoolConfig config);
    ~Pool();
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    const PoolConfig& config() const { return config_; }
    // A connection, waiting at most until `wait.deadline` and the pool's
    // S-10 timeout: PoolExhausted when S-10 acquires already wait, TimedOut
    // after the wait, ConnectionRefused when a new connection fails.
    Status acquire(const Wait& wait, std::chrono::nanoseconds limit, std::unique_ptr<Connection>& connection);
    // Takes a connection back: reset and kept, or closed when it cannot be
    // reset or `discard` is set.
    void give_back(std::unique_ptr<Connection> connection, bool discard);
    // Opens one connection to check the configuration.
    Status check();

    std::size_t open_count() const;

private:
    Status connect(const Wait& wait, std::unique_ptr<Connection>& connection);

    PoolConfig config_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::vector<std::unique_ptr<Connection>> idle_;
    std::size_t open_ = 0;
    std::size_t waiting_ = 0;
};

// Parses `--sql-pool name=url`; throws std::runtime_error with the reason.
PoolConfig parse_pool_option(std::string_view text);
// Applies one configuration key below `sql.<pool>.`: url, password,
// password_env, max_connections, max_waiting, acquire_timeout, page_rows,
// migrations, migrate. Throws with the reason.
void apply_pool_setting(std::map<std::string, PoolConfig, std::less<>>& pools, std::string_view key,
                        std::string_view value, const std::filesystem::path& base);

// Whether the build has the provider.
bool sqlite_available();
bool postgres_available();

class Engine {
public:
    explicit Engine(std::vector<PoolConfig> pools);
    Pool* find(std::string_view name);
    std::vector<Pool*> pools();
    bool empty() const { return pools_.empty(); }

private:
    std::map<std::string, std::unique_ptr<Pool>, std::less<>> pools_;
};

// The handles of one execution: its leased connections, transactions and
// cursors. Every handle is a number valid in this session only; a number
// the session did not hand out, or one already released, is InvalidInput.
// Destroying the session closes its cursors, rolls back its transactions,
// and returns its connections to their pools after a reset.
class Session final : public nvm::SqlExchange {
public:
    explicit Session(Engine& engine, Clock::time_point deadline = Clock::time_point::max());
    ~Session() override;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    nvm::SqlAnswer call(nvm::SqlOperation operation, const std::vector<nvm::HostValue>& arguments) override;
    // Cancels the statement running now, and every later one. Thread-safe.
    void cancel() { cancelled_.store(true); }
    void set_deadline(Clock::time_point deadline) { deadline_ = deadline; }
    // Releases everything the session holds, as its destructor does.
    void end();
    std::size_t leases() const { return leases_.size(); }

private:
    struct Lease {
        Pool* pool = nullptr;
        std::unique_ptr<Connection> connection;
        bool transaction = false;
        std::int64_t cursor = 0;
    };
    struct OpenCursor {
        std::int64_t connection = 0;
        std::unique_ptr<Cursor> cursor;
    };

    Lease* lease(const nvm::HostValue& handle);
    Status statement_arguments(const std::vector<nvm::HostValue>& arguments, std::size_t first,
                               std::vector<Value>& values) const;
    void close_cursor_of(Lease& lease);

    Engine& engine_;
    Clock::time_point deadline_;
    std::atomic<bool> cancelled_{false};
    std::map<std::int64_t, Lease> leases_;
    std::map<std::int64_t, OpenCursor> cursors_;
    std::int64_t next_handle_ = 1;
};

// The in-process SQL of `neura run` and `neura test` --sql-pool: one engine
// and one session for the whole process, with no deadline. Each option is
// <name>=<url>; a migration directory is given as <name>.migrations=<dir>
// and is applied at start. Throws std::runtime_error on a bad option, a
// pool that does not open, or a failed migration.
class LocalSql {
public:
    explicit LocalSql(const std::vector<std::string>& options);
    ~LocalSql();
    nvm::SqlExchange* exchange() { return session_.get(); }

private:
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Session> session_;
};

// Encodes values as the four lists of nvm::sql_kind; `columns` > 0 checks
// that the count is a multiple of it.
std::vector<nvm::HostValue> encode_values(const std::vector<Value>& values);
// Decodes the four lists. Nothing when they do not agree.
std::optional<std::vector<Value>> decode_values(const nvm::HostValue& kinds, const nvm::HostValue& bits,
                                                const nvm::HostValue& texts, const nvm::HostValue& blobs);

// Migrations (docs/runtime/Sql.md#migrations): files NNNN_name.sql numbered
// from 0001 without gaps, applied in order, each in its own transaction,
// recorded in neura_migrations.
struct Migration {
    std::uint32_t version = 0;
    std::string name;      // The file name without its number and extension.
    std::string file;      // The file name.
    std::string sql;
    std::string checksum;  // SHA-256 of the file, lowercase hexadecimal.
};

// Reads and checks a migration directory. On an invalid set, returns an
// E1044 message naming the file.
std::optional<std::string> read_migrations(const std::filesystem::path& directory, std::vector<Migration>& out);

struct MigrationReport {
    std::vector<std::string> applied;  // Files applied by this call.
    std::vector<std::string> pending;  // Files still pending (when not applying).
};

// Applies the pending migrations (`apply`) or only lists them. Fails when an
// applied migration's checksum differs from the file's, or a version is
// applied that the directory lacks. Serializes with other hosts on a
// PostgreSQL advisory lock or SQLite's write lock.
Status migrate(Pool& pool, const std::vector<Migration>& migrations, bool apply, MigrationReport& report);

} // namespace neura::sql
