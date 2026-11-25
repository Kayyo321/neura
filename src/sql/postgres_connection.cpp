// The PostgreSQL provider: libpq 18.7 in non-blocking mode, against servers
// 16, 17 and 18. Statements are sent with PQsendQueryParams and read in
// single-row mode, so a cursor holds one row at a time; the provider polls
// the connection's socket until the statement's deadline and then cancels
// it with PQcancelBlocking. Built only with NEURA_ENABLE_POSTGRES; without
// it every PostgreSQL pool is refused with Unsupported.

#include "nvm/providers.hpp"
#include "nvr/format.hpp"
#include "sql/sql_providers.hpp"

#ifdef NEURA_SQL_POSTGRES
#include "platform/socket.hpp"
#include <libpq-fe.h>
#endif

#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#ifdef __APPLE__
#include <locale>
#include <sstream>
#endif

namespace neura::sql {

namespace host_error = nvm::host_error;

#ifndef NEURA_SQL_POSTGRES

#ifdef _WIN32
constexpr auto postgres_unavailable = "the PostgreSQL provider is unavailable on Windows in Neura 0.3.0";
#endif

bool postgres_available() { return false; }

Status check_postgres_url(const PoolConfig& config) {
    if (config.url.starts_with("postgresql://") || config.url.starts_with("postgres://") ||
        config.url.find('=') != std::string::npos) {
#ifdef _WIN32
        return Status::failure(host_error::unsupported, postgres_unavailable);
#else
        return Status::failure(host_error::unsupported,
                               "this build has no PostgreSQL provider; configure it with -DNEURA_ENABLE_POSTGRES=ON");
#endif
    }
    return Status::failure(host_error::invalid_input, "the url is sqlite:<path> or a postgresql:// URI");
}

Status open_postgres(const PoolConfig&, const Wait&, std::unique_ptr<Connection>&) {
#ifdef _WIN32
    return Status::failure(host_error::unsupported, postgres_unavailable);
#else
    return Status::failure(host_error::unsupported, "this build has no PostgreSQL provider");
#endif
}

#else

bool postgres_available() { return true; }

namespace {

bool loopback(std::string_view host) {
    return host.empty() || host == "localhost" || host == "127.0.0.1" || host == "::1" || host.starts_with('/');
}

struct ParsedOptions {
    std::map<std::string, std::string, std::less<>> values;
};

Status parse_url(const std::string& url, ParsedOptions& parsed) {
    char* error = nullptr;
    PQconninfoOption* options = PQconninfoParse(url.c_str(), &error);
    if (!options) {
        std::string message = error ? error : "cannot parse the connection string";
        PQfreemem(error);
        while (!message.empty() && (message.back() == '\n' || message.back() == ' ')) message.pop_back();
        return Status::failure(host_error::invalid_input, message);
    }
    for (auto* option = options; option->keyword; ++option) {
        if (option->val) parsed.values[option->keyword] = option->val;
    }
    PQconninfoFree(options);
    return Status::success();
}

// Every host of a comma-separated list is a loopback address.
bool all_loopback(const ParsedOptions& parsed) {
    for (const auto* key : {"host", "hostaddr"}) {
        const auto found = parsed.values.find(key);
        if (found == parsed.values.end()) continue;
        std::string_view rest = found->second;
        while (true) {
            const auto comma = rest.find(',');
            if (!loopback(rest.substr(0, comma))) return false;
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    return true;
}

// Waits for the connection's socket: 1 ready, 0 when the wait ended, -1 on
// an error. Polls in slices so that a cancel request is seen.
int wait_socket(PGconn* connection, short events, const Wait& wait) {
    // PQsocket exposes an int, with -1 for no socket. On Windows this is the
    // same conversion to SOCKET that libpq's own FD_SET uses in PQsocketPoll.
    const auto socket = static_cast<platform::native_socket>(PQsocket(connection));
    if (socket == platform::invalid_socket) return -1;
    platform::PollItem entry{socket, events, 0};
    for (;;) {
        if (wait.cancel_requested()) return 0;
        const int left = milliseconds_left(wait);
        if (left == 0) return 0;
        const int ready = platform::poll(std::span(&entry, 1), std::min(left, 50));
        if (ready > 0) return (entry.revents & platform::poll_invalid) ? -1 : 1;
        if (ready < 0 && !platform::last_socket_error().interrupted()) return -1;
    }
}

std::string trimmed(const char* text) {
    std::string message = text ? text : "";
    while (!message.empty() && (message.back() == '\n' || message.back() == ' ')) message.pop_back();
    return message;
}

// The type names of the column types that do not convert, for the
// Unsupported detail.
std::string type_name(Oid type) {
    switch (type) {
    case 18: return "char";
    case 19: return "name";
    case 26: return "oid";
    case 114: return "json";
    case 142: return "xml";
    case 650: return "cidr";
    case 790: return "money";
    case 869: return "inet";
    case 1082: return "date";
    case 1083: return "time";
    case 1114: return "timestamp";
    case 1186: return "interval";
    case 1266: return "timetz";
    case 1560: return "bit";
    case 1562: return "varbit";
    case 3802: return "jsonb";
    default: return "type oid " + std::to_string(type);
    }
}

bool supported(Oid type) {
    switch (type) {
    case 16:   // bool
    case 17:   // bytea
    case 20:   // int8
    case 21:   // int2
    case 23:   // int4
    case 25:   // text
    case 700:  // float4
    case 701:  // float8
    case 1042: // bpchar
    case 1043: // varchar
    case 1184: // timestamptz, as text
    case 1700: // numeric, as text
    case 2950: // uuid, as text
        return true;
    default:
        return false;
    }
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// One field of a text-format row.
bool convert(Oid type, const char* text, int length, Value& value) {
    const std::string_view field(text, static_cast<std::size_t>(length));
    switch (type) {
    case 16:
        value.kind = nvm::sql_kind::boolean;
        value.integer = field == "t" ? 1 : 0;
        return field == "t" || field == "f";
    case 20:
    case 21:
    case 23: {
        value.kind = nvm::sql_kind::integer;
        const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value.integer);
        return error == std::errc{} && end == field.data() + field.size();
    }
    case 700:
    case 701: {
        value.kind = nvm::sql_kind::real;
        if (field == "NaN") value.real = std::nan("");
        else if (field == "Infinity") value.real = HUGE_VAL;
        else if (field == "-Infinity") value.real = -HUGE_VAL;
        else {
#ifdef __APPLE__
            // Xcode 16's libc++ has no floating-point from_chars; the C
            // locale's stream conversion reads PostgreSQL's float8 text
            // (correctly rounded, through strtod) and must use every byte.
            std::istringstream stream{std::string(field)};
            stream.imbue(std::locale::classic());
            stream >> value.real;
            if (stream.fail() || stream.peek() != std::char_traits<char>::eof()) return false;
#else
            const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value.real);
            if (error != std::errc{} || end != field.data() + field.size()) return false;
#endif
        }
        return true;
    }
    case 17: {
        value.kind = nvm::sql_kind::bytes;
        if (!field.starts_with("\\x") || field.size() % 2 != 0) return false;
        value.bytes.reserve((field.size() - 2) / 2);
        for (std::size_t i = 2; i < field.size(); i += 2) {
            const int high = hex_digit(field[i]);
            const int low = hex_digit(field[i + 1]);
            if (high < 0 || low < 0) return false;
            value.bytes.push_back(static_cast<char>(high * 16 + low));
        }
        return true;
    }
    default:
        value.kind = nvr::valid_utf8(field) ? nvm::sql_kind::text : nvm::sql_kind::bytes;
        value.bytes.assign(field);
        return true;
    }
}

class PostgresConnection;

class PostgresCursor final : public Cursor {
public:
    PostgresCursor(PostgresConnection& connection, PGresult* first, std::vector<std::string> columns,
                   std::vector<Oid> types)
        : connection_(connection), pending_(first), columns_(std::move(columns)), types_(std::move(types)),
          done_(first == nullptr) {}
    ~PostgresCursor() override;

    const std::vector<std::string>& columns() const override { return columns_; }
    Status fetch(std::size_t max, std::size_t budget, const Wait& wait, std::vector<Value>& values,
                 bool& more) override;

private:
    PostgresConnection& connection_;
    PGresult* pending_; // A row result not yet returned, or the end.
    std::vector<std::string> columns_;
    std::vector<Oid> types_;
    bool done_ = false;
};

class PostgresConnection final : public Connection {
public:
    explicit PostgresConnection(PGconn* connection) : connection_(connection) {}
    ~PostgresConnection() override { PQfinish(connection_); }

    Status execute(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                   std::int64_t& affected) override {
        if (auto status = send(sql, parameters, wait, false); !status.ok()) return status;
        Status status;
        affected = 0;
        while (PGresult* result = next(wait, status)) {
            if (status.ok()) {
                status = check(result, wait);
                // INSERT, UPDATE, DELETE and MERGE count their rows, with or
                // without RETURNING; a query affects none, as on SQLite.
                const std::string_view tag = PQcmdStatus(result);
                const char* count = PQcmdTuples(result);
                if (status.ok() && count && *count && !tag.starts_with("SELECT")) {
                    affected = std::strtoll(count, nullptr, 10);
                }
            }
            PQclear(result);
        }
        return status;
    }

    Status open(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait,
                std::unique_ptr<Cursor>& cursor) override {
        if (auto status = send(sql, parameters, wait, true); !status.ok()) return status;
        Status status;
        PGresult* first = next(wait, status);
        if (!first) return status.ok() ? Status::failure(host_error::other, "no result") : status;
        status = check(first, wait);
        const auto kind = PQresultStatus(first);
        if (status.ok() && kind != PGRES_SINGLE_TUPLE && kind != PGRES_TUPLES_OK) {
            // A statement without rows: an empty cursor.
            PQclear(first);
            drain(wait);
            first = nullptr;
            cursor = std::make_unique<PostgresCursor>(*this, nullptr, std::vector<std::string>{}, std::vector<Oid>{});
            return Status::success();
        }
        std::vector<std::string> columns;
        std::vector<Oid> types;
        if (status.ok()) {
            const int count = PQnfields(first);
            for (int i = 0; i < count; ++i) {
                columns.emplace_back(PQfname(first, i));
                types.push_back(PQftype(first, i));
                if (!supported(types.back())) {
                    status = Status::failure(host_error::unsupported,
                                             "column " + columns.back() + " has type " + type_name(types.back()));
                    break;
                }
            }
        }
        if (!status.ok()) {
            PQclear(first);
            drain(wait);
            return status;
        }
        if (kind == PGRES_TUPLES_OK) {
            // No row at all: the end is already here.
            PQclear(first);
            drain(wait);
            first = nullptr;
        }
        cursor = std::make_unique<PostgresCursor>(*this, first, std::move(columns), std::move(types));
        cursor_opened();
        return Status::success();
    }

    Status begin(const Wait& wait) override { return command("BEGIN", wait); }

    Status commit(const Wait& wait) override {
        const bool failed = PQtransactionStatus(connection_) == PQTRANS_INERROR;
        if (!send_simple("COMMIT")) return lost_commit();
        Status status;
        std::string tag;
        bool answered = false;
        while (PGresult* result = next(wait, status)) {
            answered = true;
            if (status.ok()) {
                status = check(result, wait);
                tag = PQcmdStatus(result);
            }
            PQclear(result);
        }
        if (!answered || PQstatus(connection_) != CONNECTION_OK) return lost_commit();
        if (status.code == host_error::connection_reset) return lost_commit();
        if (status.code == host_error::timed_out || status.code == host_error::cancelled) {
            // The answer did not arrive in time: the outcome is unknown.
            broken_ = true;
            return Status::failure(host_error::outcome_unknown, "COMMIT did not complete before the deadline");
        }
        if (status.ok() && (failed || tag == "ROLLBACK")) {
            return Status::failure(host_error::invalid_input,
                                   "a statement of the transaction failed; COMMIT rolled it back");
        }
        return status;
    }

    Status rollback(const Wait& wait) override {
        if (PQtransactionStatus(connection_) == PQTRANS_IDLE) return Status::success();
        return command("ROLLBACK", wait);
    }

    Status execute_script(std::string_view sql, const Wait& wait) override {
        if (!send_simple(std::string(sql))) return lost("cannot send the script");
        Status status;
        while (PGresult* result = next(wait, status)) {
            if (status.ok()) status = check(result, wait);
            PQclear(result);
        }
        return status;
    }

    bool reset(const Wait& wait) override {
        if (open_cursor_ || broken_ || PQstatus(connection_) != CONNECTION_OK) return false;
        if (PQtransactionStatus(connection_) != PQTRANS_IDLE && !command("ROLLBACK", wait).ok()) return false;
        if (!command("DISCARD ALL", wait).ok()) return false;
        return PQtransactionStatus(connection_) == PQTRANS_IDLE;
    }

    bool usable() const override { return !broken_ && PQstatus(connection_) == CONNECTION_OK; }
    std::string_view provider() const override { return "postgresql"; }

    // The next result of the statement, or null at its end. Waits for the
    // socket until the deadline; then cancels the statement and reports
    // TimedOut or Cancelled, reading what remains.
    PGresult* next(const Wait& wait, Status& status) {
        for (;;) {
            if (!PQisBusy(connection_)) {
                PGresult* result = PQgetResult(connection_);
                if (!result && PQstatus(connection_) != CONNECTION_OK) {
                    broken_ = true;
                    if (status.ok()) status = lost("the connection was lost").status;
                }
                return result;
            }
            const int ready = wait_socket(connection_, platform::poll_in, wait);
            if (ready < 0) {
                broken_ = true;
                if (status.ok()) status = lost("the connection was lost").status;
                return nullptr;
            }
            if (ready == 0) {
                if (status.ok()) status = wait.interrupted();
                cancel();
                // Read the rest of the cancelled statement, for at most 5 s.
                Wait after;
                after.deadline = Clock::now() + std::chrono::seconds(5);
                while (PGresult* rest = next_until(after)) PQclear(rest);
                return nullptr;
            }
            if (!PQconsumeInput(connection_)) {
                broken_ = true;
                if (status.ok()) status = lost("the connection was lost").status;
                return nullptr;
            }
        }
    }

    // Reads and discards the rest of the current statement.
    void drain(const Wait& wait) {
        Wait limit = wait;
        limit.deadline = std::min(wait.deadline, Clock::now() + std::chrono::seconds(30));
        Status ignored;
        while (PGresult* result = next(limit, ignored)) PQclear(result);
        if (!ignored.ok() && ignored.code != host_error::connection_reset) {
            // The statement could not be read to its end; the connection's
            // state is unknown.
            broken_ = true;
        }
    }

    Status check(PGresult* result, const Wait& wait) const {
        switch (PQresultStatus(result)) {
        case PGRES_COMMAND_OK:
        case PGRES_TUPLES_OK:
        case PGRES_SINGLE_TUPLE:
        case PGRES_TUPLES_CHUNK:
        case PGRES_EMPTY_QUERY:
            return Status::success();
        default:
            break;
        }
        const std::string state = PQresultErrorField(result, PG_DIAG_SQLSTATE) ? PQresultErrorField(result, PG_DIAG_SQLSTATE) : "";
        const std::string message = trimmed(PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY)
                                                ? PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY)
                                                : PQresultErrorMessage(result));
        if (state.starts_with("23")) {
            const char* constraint = PQresultErrorField(result, PG_DIAG_CONSTRAINT_NAME);
            return Status::failure(host_error::constraint_violation, constraint ? constraint : message);
        }
        if (state == "40001" || state == "40P01") return Status::failure(host_error::serialization, message);
        if (state == "57014") return wait.interrupted();
        if (state == "25P02") {
            return Status::failure(host_error::invalid_input, "a statement of the transaction failed; roll it back");
        }
        if (state == "08P01") return Status::failure(host_error::invalid_input, message);
        if (state.starts_with("08") || state == "57P01" || state == "57P02" || state == "57P03") {
            return Status::failure(host_error::connection_reset, message);
        }
        if (state.starts_with("53")) return Status::failure(host_error::busy, message);
        if (state.starts_with("28") || state == "42501") return Status::failure(host_error::permission_denied, message);
        if (state.starts_with("42") || state.starts_with("22") || state.starts_with("0A") || state.starts_with("2")) {
            return Status::failure(host_error::invalid_input, message);
        }
        return Status::failure(host_error::other, message);
    }

    void cursor_opened() { open_cursor_ = true; }
    void cursor_closed() { open_cursor_ = false; }

private:
    struct Lost {
        Status status;
        operator Status() const { return status; }
    };

    Lost lost(std::string detail) {
        broken_ = true;
        return {Status::failure(host_error::connection_reset, std::move(detail))};
    }

    Status lost_commit() {
        broken_ = true;
        return Status::failure(host_error::outcome_unknown,
                               "the connection failed after COMMIT was sent; the outcome is unknown");
    }

    PGresult* next_until(const Wait& wait) {
        Status ignored;
        for (;;) {
            if (!PQisBusy(connection_)) return PQgetResult(connection_);
            if (wait_socket(connection_, platform::poll_in, wait) <= 0 || !PQconsumeInput(connection_)) {
                broken_ = true;
                return nullptr;
            }
        }
    }

    void cancel() {
        PGcancelConn* request = PQcancelCreate(connection_);
        if (!request) return;
        if (!PQcancelBlocking(request)) broken_ = true;
        PQcancelFinish(request);
    }

    bool send_simple(const std::string& sql) {
        if (!PQsendQuery(connection_, sql.c_str())) return false;
        return flush(Wait{Clock::now() + std::chrono::seconds(30), nullptr});
    }

    bool flush(const Wait& wait) {
        for (;;) {
            const int pending = PQflush(connection_);
            if (pending == 0) return true;
            if (pending < 0) return false;
            if (wait_socket(connection_, platform::poll_in | platform::poll_out, wait) <= 0) return false;
            if (!PQconsumeInput(connection_)) return false;
        }
    }

    Status command(const char* sql, const Wait& wait) {
        if (!send_simple(sql)) return lost("cannot send " + std::string(sql));
        Status status;
        while (PGresult* result = next(wait, status)) {
            if (status.ok()) status = check(result, wait);
            PQclear(result);
        }
        return status;
    }

    Status send(std::string_view sql, const std::vector<Value>& parameters, const Wait& wait, bool rows) {
        if (wait.expired() || wait.cancel_requested()) return wait.interrupted();
        if (parameters.size() > 65535) return Status::failure(host_error::invalid_input, "more than 65,535 parameters");
        std::vector<Oid> types(parameters.size(), 0);
        std::vector<std::string> texts(parameters.size());
        std::vector<const char*> values(parameters.size(), nullptr);
        std::vector<int> lengths(parameters.size(), 0);
        std::vector<int> formats(parameters.size(), 0);
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const auto& value = parameters[i];
            switch (value.kind) {
            case nvm::sql_kind::null:
                continue;
            case nvm::sql_kind::boolean:
                types[i] = 16;
                texts[i] = value.integer ? "t" : "f";
                break;
            case nvm::sql_kind::integer:
                types[i] = 20;
                texts[i] = std::to_string(value.integer);
                break;
            case nvm::sql_kind::real: {
                types[i] = 701;
                if (std::isnan(value.real)) texts[i] = "NaN";
                else if (std::isinf(value.real)) texts[i] = value.real > 0 ? "Infinity" : "-Infinity";
                else {
                    char buffer[64];
                    const auto [end, error] = std::to_chars(buffer, buffer + sizeof buffer, value.real);
                    texts[i].assign(buffer, end);
                }
                break;
            }
            case nvm::sql_kind::text:
                if (value.bytes.find('\0') != std::string::npos) {
                    return Status::failure(host_error::invalid_input, "PostgreSQL text cannot hold a NUL character");
                }
                texts[i] = value.bytes;
                break;
            case nvm::sql_kind::bytes:
                types[i] = 17;
                formats[i] = 1;
                values[i] = value.bytes.data();
                lengths[i] = static_cast<int>(value.bytes.size());
                continue;
            default:
                return Status::failure(host_error::invalid_input, "an unknown value kind");
            }
            values[i] = texts[i].c_str();
            lengths[i] = static_cast<int>(texts[i].size());
        }
        const std::string text(sql);
        if (!PQsendQueryParams(connection_, text.c_str(), static_cast<int>(parameters.size()), types.data(),
                               values.data(), lengths.data(), formats.data(), 0)) {
            return lost(trimmed(PQerrorMessage(connection_)));
        }
        if (rows && !PQsetSingleRowMode(connection_)) {
            drain(wait);
            return Status::failure(host_error::other, "cannot read the statement row by row");
        }
        if (!flush(wait)) return lost("cannot send the statement");
        return Status::success();
    }

    PGconn* connection_;
    bool broken_ = false;
    bool open_cursor_ = false;
};

PostgresCursor::~PostgresCursor() {
    const bool rest = !done_;
    if (pending_) PQclear(pending_);
    // Rows remain: read them to the end of the statement.
    if (rest) connection_.drain(Wait{Clock::now() + std::chrono::seconds(30), nullptr});
    connection_.cursor_closed();
}

Status PostgresCursor::fetch(std::size_t max, std::size_t budget, const Wait& wait, std::vector<Value>& values,
                             bool& more) {
    std::size_t rows = 0;
    std::size_t bytes = 0;
    while (pending_ && rows < max) {
        const int fields = PQnfields(pending_);
        std::size_t size = 0;
        // The text form's length bounds every converted payload.
        for (int i = 0; i < fields; ++i) {
            size += PQgetisnull(pending_, 0, i)
                        ? channel_bytes(nvm::sql_kind::null, 0)
                        : channel_bytes(nvm::sql_kind::bytes, static_cast<std::size_t>(PQgetlength(pending_, 0, i)));
        }
        if (rows > 0 && bytes + size > budget) break;
        bytes += size;
        for (int i = 0; i < fields; ++i) {
            Value value;
            if (!PQgetisnull(pending_, 0, i) &&
                !convert(types_[static_cast<std::size_t>(i)], PQgetvalue(pending_, 0, i), PQgetlength(pending_, 0, i),
                         value)) {
                return Status::failure(host_error::other, "column " + columns_[static_cast<std::size_t>(i)] +
                                                              " has a value that does not convert");
            }
            values.push_back(std::move(value));
        }
        PQclear(pending_);
        pending_ = nullptr;
        ++rows;
        // The next result: another row, or the end of the statement.
        Status status;
        PGresult* result = connection_.next(wait, status);
        if (!status.ok()) {
            if (result) PQclear(result);
            done_ = true;
            return status;
        }
        if (!result) {
            done_ = true;
            break;
        }
        if (PQresultStatus(result) == PGRES_SINGLE_TUPLE) {
            pending_ = result;
            continue;
        }
        status = connection_.check(result, wait);
        PQclear(result);
        connection_.drain(wait);
        done_ = true;
        if (!status.ok()) return status;
    }
    more = pending_ != nullptr;
    return Status::success();
}

} // namespace

Status check_postgres_url(const PoolConfig& config) {
    if (!config.url.starts_with("postgresql://") && !config.url.starts_with("postgres://") &&
        config.url.find('=') == std::string::npos) {
        return Status::failure(host_error::invalid_input, "the url is sqlite:<path> or a postgresql:// URI");
    }
    ParsedOptions parsed;
    if (auto status = parse_url(config.url, parsed); !status.ok()) return status;
    if (parsed.values.contains("password")) {
        return Status::failure(host_error::invalid_input,
                               "the url holds a password; give it as sql.<pool>.password or password_env in the "
                               "host's configuration");
    }
    const auto mode = parsed.values.find("sslmode");
    if (mode != parsed.values.end() && mode->second != "verify-full" && !all_loopback(parsed)) {
        return Status::failure(host_error::invalid_input,
                               "sslmode=" + mode->second + " is allowed only for a loopback host; use verify-full");
    }
    return Status::success();
}

Status open_postgres(const PoolConfig& config, const Wait& wait, std::unique_ptr<Connection>& connection) {
    ParsedOptions parsed;
    if (auto status = parse_url(config.url, parsed); !status.ok()) return status;
    std::vector<const char*> keywords{"dbname", "client_encoding", "application_name"};
    std::vector<const char*> values{config.url.c_str(), "UTF8", "neura"};
    if (!parsed.values.contains("sslmode")) {
        keywords.push_back("sslmode");
        values.push_back("verify-full");
    }
    if (config.password) {
        keywords.push_back("password");
        values.push_back(config.password->c_str());
    }
    keywords.push_back(nullptr);
    values.push_back(nullptr);
    PGconn* raw = PQconnectStartParams(keywords.data(), values.data(), 1);
    if (!raw) return Status::failure(host_error::other, "out of memory");
    auto fail = [&](std::string detail) {
        PQfinish(raw);
        return Status::failure(host_error::connection_refused, "cannot connect to " + config.redacted_url() + ": " + detail);
    };
    if (PQstatus(raw) == CONNECTION_BAD) return fail(trimmed(PQerrorMessage(raw)));
    for (PostgresPollingStatusType polling = PGRES_POLLING_WRITING;;) {
        if (polling == PGRES_POLLING_OK) break;
        if (polling == PGRES_POLLING_FAILED) return fail(trimmed(PQerrorMessage(raw)));
        // PQconnectPoll can replace the socket when moving to another address.
        const short events = polling == PGRES_POLLING_WRITING ? platform::poll_out : platform::poll_in;
        const int ready = wait_socket(raw, events, wait);
        if (ready == 0) return fail("the connection did not complete in time");
        if (ready < 0) return fail(trimmed(PQerrorMessage(raw)));
        polling = PQconnectPoll(raw);
    }
    if (PQsetnonblocking(raw, 1) != 0) return fail("cannot switch to non-blocking mode");
    // Server notices (such as "table does not exist, skipping") are not
    // errors; libpq would print them on standard error.
    PQsetNoticeProcessor(raw, [](void*, const char*) {}, nullptr);
    connection = std::make_unique<PostgresConnection>(raw);
    return Status::success();
}

#endif

} // namespace neura::sql
