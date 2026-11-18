// The SQLite provider: SQLite 3.54.1 compiled in from the pinned
// amalgamation, in WAL mode with foreign keys on. Placeholders are $1, $2,
// ...; SQLite takes them as named parameters, so the provider binds each by
// its name and rejects any other form. A statement's busy timeout is the
// time left until its deadline, and a progress handler interrupts it at the
// deadline or when its session is cancelled.

#include "nvm/providers.hpp"
#include "nvr/format.hpp"
#include "sql/sql_providers.hpp"

#include <sqlite3.h>

#include <cmath>
#include <cstring>

namespace neura::sql {

namespace host_error = nvm::host_error;

bool sqlite_available() { return true; }

namespace {

// The wait the progress handler of a connection checks.
struct Watch {
    const Wait* wait = nullptr;
};

int on_progress(void* context) {
    const auto* watch = static_cast<Watch*>(context);
    if (!watch->wait) return 0;
    return watch->wait->expired() || watch->wait->cancel_requested() ? 1 : 0;
}

class SqliteConnection;

class SqliteCursor final : public Cursor {
public:
    SqliteCursor(SqliteConnection& connection, sqlite3_stmt* statement, std::vector<std::string> columns)
        : connection_(connection), statement_(statement), columns_(std::move(columns)) {}
    ~SqliteCursor() override;

    const std::vector<std::string>& columns() const override { return columns_; }
    Status fetch(std::size_t max, const Wait& wait, std::vector<Value>& values, bool& more) override;

private:
    SqliteConnection& connection_;
    sqlite3_stmt* statement_;
    std::vector<std::string> columns_;
    bool started_ = false;
    bool pending_ = false; // A row was stepped to and not yet returned.
    bool done_ = false;
};

class SqliteConnection final : public Connection {
public:
    explicit SqliteConnection(sqlite3* db) : db_(db) {
        sqlite3_progress_handler(db_, 1000, on_progress, &watch_);
    }
    ~SqliteConnection() override { sqlite3_close_v2(db_); }

    Status execute(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                   std::int64_t& affected) override {
        sqlite3_stmt* statement = nullptr;
        if (auto status = prepare(sql, parameters, wait, statement); !status.ok()) return status;
        const bool read_only = sqlite3_stmt_readonly(statement) != 0;
        int code;
        do {
            code = step(statement, wait);
        } while (code == SQLITE_ROW);
        Status status;
        if (code != SQLITE_DONE) status = failure(code, wait);
        sqlite3_finalize(statement);
        if (status.ok()) affected = read_only ? 0 : sqlite3_changes64(db_);
        return status;
    }

    Status open(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                std::unique_ptr<Cursor>& cursor) override {
        sqlite3_stmt* statement = nullptr;
        if (auto status = prepare(sql, parameters, wait, statement); !status.ok()) return status;
        std::vector<std::string> columns;
        const int count = sqlite3_column_count(statement);
        for (int i = 0; i < count; ++i) {
            const char* name = sqlite3_column_name(statement, i);
            columns.emplace_back(name ? name : "");
        }
        cursor = std::make_unique<SqliteCursor>(*this, statement, std::move(columns));
        return Status::success();
    }

    Status begin(const Wait& wait) override {
        // IMMEDIATE takes the write lock at once, so a transaction never
        // fails later on upgrading a read lock (docs/runtime/Sql.md).
        return run("BEGIN IMMEDIATE", wait);
    }

    Status commit(const Wait& wait) override {
        auto status = run("COMMIT", wait);
        if (!status.ok() && !sqlite3_get_autocommit(db_)) {
            // A failed COMMIT (a deferred constraint, a busy reader) leaves
            // the transaction open; it ends rolled back, as on PostgreSQL.
            Wait cleanup;
            cleanup.deadline = Clock::now() + std::chrono::seconds(5);
            (void)run("ROLLBACK", cleanup);
            if (status.code == host_error::other) status.code = host_error::outcome_unknown;
        }
        return status;
    }

    Status rollback(const Wait& wait) override {
        if (sqlite3_get_autocommit(db_)) return Status::success(); // SQLite already rolled it back.
        return run("ROLLBACK", wait);
    }

    Status execute_script(std::string_view sql, const Wait& wait) override {
        watch_.wait = &wait;
        sqlite3_busy_timeout(db_, std::max(1, milliseconds_left(wait)));
        char* message = nullptr;
        const std::string text(sql);
        const int code = sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &message);
        watch_.wait = nullptr;
        Status status;
        if (code != SQLITE_OK) {
            status = classify(code, message ? message : sqlite3_errstr(code), wait);
        }
        sqlite3_free(message);
        return status;
    }

    bool reset(const Wait& wait) override {
        if (open_statements_ != 0) return false;
        if (!sqlite3_get_autocommit(db_) && !run("ROLLBACK", wait).ok()) return false;
        return sqlite3_get_autocommit(db_) != 0;
    }

    bool usable() const override { return true; }
    std::string_view provider() const override { return "sqlite"; }

    // One step, with the deadline in force.
    int step(sqlite3_stmt* statement, const Wait& wait) {
        if (wait.expired() || wait.cancel_requested()) return SQLITE_INTERRUPT;
        watch_.wait = &wait;
        sqlite3_busy_timeout(db_, std::max(1, milliseconds_left(wait)));
        const int code = sqlite3_step(statement);
        watch_.wait = nullptr;
        return code;
    }

    Status failure(int code, const Wait& wait) const { return classify(code, sqlite3_errmsg(db_), wait); }

    Status classify(int code, std::string message, const Wait& wait) const {
        const int primary = code & 0xff;
        switch (primary) {
        case SQLITE_CONSTRAINT:
            return Status::failure(host_error::constraint_violation, std::move(message));
        case SQLITE_INTERRUPT:
            return wait.interrupted();
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            if (code == SQLITE_BUSY_SNAPSHOT) return Status::failure(host_error::serialization, std::move(message));
            if (wait.expired()) return wait.interrupted();
            return Status::failure(host_error::busy, std::move(message));
        case SQLITE_TOOBIG:
            return Status::failure(host_error::too_large, std::move(message));
        case SQLITE_ERROR:
        case SQLITE_MISMATCH:
        case SQLITE_RANGE:
        case SQLITE_MISUSE:
            return Status::failure(host_error::invalid_input, std::move(message));
        case SQLITE_READONLY:
        case SQLITE_PERM:
        case SQLITE_AUTH:
            return Status::failure(host_error::permission_denied, std::move(message));
        case SQLITE_CANTOPEN:
            return Status::failure(host_error::not_found, std::move(message));
        default:
            return Status::failure(host_error::other, std::move(message));
        }
    }

    void statement_opened() { ++open_statements_; }
    void statement_closed() { --open_statements_; }

private:
    Status run(const char* sql, const Wait& wait) {
        sqlite3_stmt* statement = nullptr;
        int code = sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr);
        if (code != SQLITE_OK) return failure(code, wait);
        code = step(statement, wait);
        Status status;
        if (code != SQLITE_DONE && code != SQLITE_ROW) status = failure(code, wait);
        sqlite3_finalize(statement);
        return status;
    }

    Status prepare(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                   sqlite3_stmt*& statement) {
        if (wait.expired() || wait.cancel_requested()) return wait.interrupted();
        const char* tail = nullptr;
        int code = sqlite3_prepare_v3(db_, sql.data(), static_cast<int>(sql.size()), 0, &statement, &tail);
        if (code != SQLITE_OK) return failure(code, wait);
        if (!statement) return Status::failure(host_error::invalid_input, "the SQL text holds no statement");
        // Only white space and comments may follow the one statement.
        const std::string_view rest(tail, static_cast<std::size_t>(sql.data() + sql.size() - tail));
        if (!rest.empty()) {
            sqlite3_stmt* next = nullptr;
            const std::string remaining(rest);
            const int more = sqlite3_prepare_v2(db_, remaining.c_str(), -1, &next, nullptr);
            const bool another = more != SQLITE_OK || next != nullptr;
            sqlite3_finalize(next);
            if (another) {
                sqlite3_finalize(statement);
                statement = nullptr;
                return Status::failure(host_error::invalid_input, "one statement per call; a migration may hold several");
            }
        }
        if (auto status = bind(statement, parameters); !status.ok()) {
            sqlite3_finalize(statement);
            statement = nullptr;
            return status;
        }
        return Status::success();
    }

    // Binds $1 .. $n by name. Any other placeholder form, a number above n,
    // or a parameter the statement does not use is InvalidInput.
    Status bind(sqlite3_stmt* statement, const std::vector<Value>& parameters) {
        const int count = sqlite3_bind_parameter_count(statement);
        std::vector<bool> used(parameters.size(), false);
        for (int index = 1; index <= count; ++index) {
            const char* raw = sqlite3_bind_parameter_name(statement, index);
            const std::string_view name = raw ? raw : "?";
            std::size_t number = 0;
            bool valid = name.size() >= 2 && name[0] == '$' && name[1] != '0' && name.size() <= 7;
            for (std::size_t i = 1; valid && i < name.size(); ++i) {
                if (name[i] < '0' || name[i] > '9') valid = false;
                else number = number * 10 + static_cast<std::size_t>(name[i] - '0');
            }
            if (!valid) {
                return Status::failure(host_error::invalid_input,
                                       "placeholder " + std::string(name) + ": write $1, $2, ... for parameters");
            }
            if (number > parameters.size()) {
                return Status::failure(host_error::invalid_input,
                                       "placeholder $" + std::to_string(number) + " has no parameter; " +
                                           std::to_string(parameters.size()) + " given");
            }
            used[number - 1] = true;
            const auto& value = parameters[number - 1];
            int code = SQLITE_OK;
            switch (value.kind) {
            case nvm::sql_kind::null:
                code = sqlite3_bind_null(statement, index);
                break;
            case nvm::sql_kind::boolean:
            case nvm::sql_kind::integer:
                code = sqlite3_bind_int64(statement, index, value.integer);
                break;
            case nvm::sql_kind::real:
                code = sqlite3_bind_double(statement, index, value.real);
                break;
            case nvm::sql_kind::text:
                code = sqlite3_bind_text64(statement, index, value.bytes.data(), value.bytes.size(), SQLITE_TRANSIENT,
                                           SQLITE_UTF8);
                break;
            case nvm::sql_kind::bytes:
                code = sqlite3_bind_blob64(statement, index, value.bytes.data(), value.bytes.size(), SQLITE_TRANSIENT);
                break;
            default:
                code = SQLITE_MISMATCH;
                break;
            }
            if (code != SQLITE_OK) return classify(code, sqlite3_errstr(code), Wait{});
        }
        for (std::size_t i = 0; i < used.size(); ++i) {
            if (!used[i]) {
                return Status::failure(host_error::invalid_input,
                                       "parameter $" + std::to_string(i + 1) + " is not used by the statement");
            }
        }
        return Status::success();
    }

    sqlite3* db_;
    Watch watch_;
    int open_statements_ = 0;
};

SqliteCursor::~SqliteCursor() {
    sqlite3_finalize(statement_);
    if (started_) connection_.statement_closed();
}

Status SqliteCursor::fetch(std::size_t max, const Wait& wait, std::vector<Value>& values, bool& more) {
    if (!started_) {
        started_ = true;
        connection_.statement_opened();
    }
    const auto columns = static_cast<int>(columns_.size());
    std::size_t rows = 0;
    // The statement runs even when no row is wanted.
    while (!done_ && (rows < max || !pending_)) {
        if (!pending_) {
            const int code = connection_.step(statement_, wait);
            if (code == SQLITE_DONE) {
                done_ = true;
                break;
            }
            if (code != SQLITE_ROW) {
                done_ = true;
                return connection_.failure(code, wait);
            }
            pending_ = true;
        }
        if (rows == max) break;
        for (int i = 0; i < columns; ++i) {
            Value value;
            switch (sqlite3_column_type(statement_, i)) {
            case SQLITE_INTEGER:
                value.kind = nvm::sql_kind::integer;
                value.integer = sqlite3_column_int64(statement_, i);
                break;
            case SQLITE_FLOAT:
                value.kind = nvm::sql_kind::real;
                value.real = sqlite3_column_double(statement_, i);
                break;
            case SQLITE_TEXT: {
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement_, i));
                value.bytes.assign(text ? text : "", static_cast<std::size_t>(sqlite3_column_bytes(statement_, i)));
                // Text that is not UTF-8 cannot be a Neura string.
                value.kind = nvr::valid_utf8(value.bytes) ? nvm::sql_kind::text : nvm::sql_kind::bytes;
                break;
            }
            case SQLITE_BLOB: {
                const auto* blob = static_cast<const char*>(sqlite3_column_blob(statement_, i));
                value.kind = nvm::sql_kind::bytes;
                if (blob) value.bytes.assign(blob, static_cast<std::size_t>(sqlite3_column_bytes(statement_, i)));
                break;
            }
            default:
                break;
            }
            values.push_back(std::move(value));
        }
        pending_ = false;
        ++rows;
    }
    more = pending_;
    return Status::success();
}

// sqlite:<path>; sqlite::memory: is one private in-memory database, so its
// pool has one connection.
std::string sqlite_path(const PoolConfig& config) { return config.url.substr(7); }

} // namespace

Status open_sqlite(const PoolConfig& config, const Wait& wait, std::unique_ptr<Connection>& connection) {
    const auto path = sqlite_path(config);
    sqlite3* db = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_EXRESCODE;
    const int code = sqlite3_open_v2(path.c_str(), &db, flags, nullptr);
    if (code != SQLITE_OK) {
        std::string message = db ? sqlite3_errmsg(db) : sqlite3_errstr(code);
        sqlite3_close_v2(db);
        return Status::failure(host_error::connection_refused, "cannot open " + path + ": " + message);
    }
    sqlite3_extended_result_codes(db, 1);
    auto opened = std::make_unique<SqliteConnection>(db);
    const char* setup = path == ":memory:" ? "PRAGMA foreign_keys = ON;"
                                           : "PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;";
    if (auto status = opened->execute_script(setup, wait); !status.ok()) {
        status.detail = "cannot set up " + path + ": " + status.detail;
        status.code = host_error::connection_refused;
        return status;
    }
    connection = std::move(opened);
    return Status::success();
}

} // namespace neura::sql
