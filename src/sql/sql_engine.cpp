#include "sql/sql_engine.hpp"

#include "nvm/providers.hpp"
#include "sql/sql_providers.hpp"

#include <algorithm>
#include <charconv>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace neura::sql {

namespace host_error = nvm::host_error;
using nvm::HostCompletion;
using nvm::HostValue;
using nvm::SqlAnswer;
using nvm::SqlOperation;

Status Wait::interrupted() const {
    if (cancel_requested()) return Status::failure(host_error::cancelled, "the statement was cancelled");
    return Status::failure(host_error::timed_out, "the statement passed the request's deadline");
}

int milliseconds_left(const Wait& wait) {
    if (wait.deadline == Clock::time_point::max()) return INT_MAX;
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(wait.deadline - Clock::now()).count();
    if (left <= 0) return 0;
    return left > INT_MAX ? INT_MAX : static_cast<int>(left);
}

std::string PoolConfig::redacted_url() const {
    // postgresql://user:secret@host/db -> postgresql://user:***@host/db
    auto text = url;
    const auto scheme = text.find("://");
    if (scheme != std::string::npos) {
        const auto at = text.find('@', scheme + 3);
        const auto colon = text.find(':', scheme + 3);
        if (at != std::string::npos && colon != std::string::npos && colon < at) {
            text.replace(colon + 1, at - colon - 1, "***");
        }
    }
    // key=value strings: password=... -> password=***
    for (std::size_t at = text.find("password="); at != std::string::npos; at = text.find("password=", at + 1)) {
        const auto start = at + 9;
        auto end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        text.replace(start, end - start, "***");
    }
    return text;
}

// ---------------------------------------------------------------------------
// Pools

Pool::Pool(PoolConfig config) : config_(std::move(config)) {
    if (config_.url == "sqlite::memory:") config_.max_connections = 1;
}

Pool::~Pool() = default;

std::size_t Pool::open_count() const {
    std::lock_guard lock(mutex_);
    return open_;
}

Status Pool::connect(const Wait& wait, std::unique_ptr<Connection>& connection) {
    auto status = is_sqlite_url(config_.url) ? open_sqlite(config_, wait, connection)
                                             : open_postgres(config_, wait, connection);
    if (!status.ok()) connection.reset();
    return status;
}

Status Pool::acquire(const Wait& wait, std::chrono::nanoseconds limit, std::unique_ptr<Connection>& connection) {
    auto deadline = Clock::now() + std::min<std::chrono::nanoseconds>(limit, config_.acquire_timeout);
    deadline = std::min(deadline, wait.deadline);
    std::unique_lock lock(mutex_);
    for (;;) {
        while (!idle_.empty()) {
            auto candidate = std::move(idle_.back());
            idle_.pop_back();
            if (candidate->usable()) {
                connection = std::move(candidate);
                return Status::success();
            }
            --open_;
        }
        if (open_ < config_.max_connections) {
            ++open_;
            lock.unlock();
            Wait connect_wait = wait;
            connect_wait.deadline = std::min(wait.deadline, Clock::now() + std::chrono::seconds(10));
            auto status = connect(connect_wait, connection);
            if (!status.ok()) {
                lock.lock();
                --open_;
                available_.notify_one();
            }
            return status;
        }
        if (waiting_ >= config_.max_waiting) {
            return Status::failure(host_error::pool_exhausted,
                                   "pool " + config_.name + " has " + std::to_string(waiting_) +
                                       " acquires waiting (S-10)");
        }
        ++waiting_;
        const bool woke = available_.wait_until(lock, deadline, [&] {
            return !idle_.empty() || open_ < config_.max_connections || wait.cancel_requested();
        });
        --waiting_;
        if (wait.cancel_requested()) return wait.interrupted();
        if (!woke) {
            return Status::failure(host_error::timed_out,
                                   "no connection of pool " + config_.name + " became free in time (S-10)");
        }
    }
}

void Pool::give_back(std::unique_ptr<Connection> connection, bool discard) {
    if (!connection) return;
    bool keep = !discard && connection->usable();
    if (keep) {
        Wait wait;
        wait.deadline = Clock::now() + std::chrono::seconds(5);
        keep = connection->reset(wait) && connection->usable();
    }
    std::lock_guard lock(mutex_);
    if (keep) {
        idle_.push_back(std::move(connection));
    } else {
        connection.reset();
        --open_;
    }
    available_.notify_one();
}

Status Pool::check() {
    Wait wait;
    wait.deadline = Clock::now() + std::chrono::seconds(10);
    std::unique_ptr<Connection> connection;
    auto status = acquire(wait, std::chrono::seconds(10), connection);
    if (!status.ok()) return status;
    give_back(std::move(connection), false);
    return Status::success();
}

// ---------------------------------------------------------------------------
// Configuration

namespace {

bool valid_pool_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::uint64_t parse_number(std::string_view key, std::string_view text, std::uint64_t low, std::uint64_t high) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < low || value > high) {
        throw std::runtime_error(std::string(key) + " expects a number from " + std::to_string(low) + " to " +
                                 std::to_string(high));
    }
    return value;
}

void check_url(const PoolConfig& config) {
    if (is_sqlite_url(config.url)) {
        if (config.url.size() == 7) throw std::runtime_error("pool " + config.name + ": sqlite: needs a path");
        if (config.password) throw std::runtime_error("pool " + config.name + ": an SQLite pool takes no password");
        return;
    }
    const auto status = check_postgres_url(config);
    if (!status.ok()) throw std::runtime_error("pool " + config.name + ": " + status.detail);
}

} // namespace

PoolConfig parse_pool_option(std::string_view text) {
    const auto equals = text.find('=');
    if (equals == std::string_view::npos) throw std::runtime_error("--sql-pool expects <name>=<url>");
    PoolConfig config;
    config.name = std::string(text.substr(0, equals));
    config.url = std::string(text.substr(equals + 1));
    if (!valid_pool_name(config.name)) {
        throw std::runtime_error("--sql-pool: a pool name is 1 to 64 letters, digits, '_' or '-'");
    }
    check_url(config);
    return config;
}

void apply_pool_setting(std::map<std::string, PoolConfig, std::less<>>& pools, std::string_view key,
                        std::string_view value, const std::filesystem::path& base) {
    // key is <pool>.<setting>
    const auto dot = key.find('.');
    if (dot == std::string_view::npos) throw std::runtime_error("sql." + std::string(key) + ": expected sql.<pool>.<setting>");
    const auto name = key.substr(0, dot);
    const auto setting = key.substr(dot + 1);
    if (!valid_pool_name(name)) throw std::runtime_error("sql." + std::string(key) + ": invalid pool name");
    auto& config = pools[std::string(name)];
    config.name = std::string(name);
    const auto full = "sql." + std::string(key);
    if (setting == "url") {
        config.url = std::string(value);
    } else if (setting == "password") {
        config.password = std::string(value);
    } else if (setting == "password_env") {
        const char* secret = std::getenv(std::string(value).c_str());
        if (!secret) throw std::runtime_error(full + ": the environment variable " + std::string(value) + " is not set");
        config.password = secret;
    } else if (setting == "max_connections") {
        config.max_connections = parse_number(full, value, 1, 1024);
    } else if (setting == "max_waiting") {
        config.max_waiting = parse_number(full, value, 0, 65536);
    } else if (setting == "acquire_timeout") {
        config.acquire_timeout = std::chrono::seconds(parse_number(full, value, 1, 600));
    } else if (setting == "page_rows") {
        config.page_rows = parse_number(full, value, 1, 1'000'000);
    } else if (setting == "migrations") {
        std::filesystem::path path{std::string(value)};
        config.migrations = path.is_absolute() ? path : base / path;
    } else if (setting == "migrate") {
        if (value == "manual") config.migrate = MigrateMode::Manual;
        else if (value == "on_start") config.migrate = MigrateMode::OnStart;
        else throw std::runtime_error(full + " is manual or on_start");
    } else {
        throw std::runtime_error("unknown SQL setting " + full);
    }
}

Engine::Engine(std::vector<PoolConfig> pools) {
    for (auto& config : pools) {
        if (config.url.empty()) throw std::runtime_error("pool " + config.name + " has no url");
        check_url(config);
        const auto name = config.name;
        if (!pools_.emplace(name, std::make_unique<Pool>(std::move(config))).second) {
            throw std::runtime_error("pool " + name + " is configured twice");
        }
    }
}

Pool* Engine::find(std::string_view name) {
    const auto found = pools_.find(name);
    return found == pools_.end() ? nullptr : found->second.get();
}

std::vector<Pool*> Engine::pools() {
    std::vector<Pool*> all;
    for (auto& [name, pool] : pools_) all.push_back(pool.get());
    return all;
}

// ---------------------------------------------------------------------------
// Value encoding

std::vector<HostValue> encode_values(const std::vector<Value>& values) {
    HostValue kinds, bits, texts, blobs;
    kinds.bytes.reserve(values.size());
    bits.elements.reserve(values.size());
    for (const auto& value : values) {
        kinds.bytes.push_back(static_cast<char>(value.kind));
        HostValue word;
        switch (value.kind) {
        case nvm::sql_kind::boolean:
        case nvm::sql_kind::integer:
            word.bits = static_cast<std::uint64_t>(value.integer);
            break;
        case nvm::sql_kind::real:
            std::memcpy(&word.bits, &value.real, sizeof word.bits);
            break;
        case nvm::sql_kind::text:
            word.bits = texts.elements.size();
            texts.elements.push_back(HostValue{0, value.bytes, {}});
            break;
        case nvm::sql_kind::bytes:
            word.bits = blobs.elements.size();
            blobs.elements.push_back(HostValue{0, value.bytes, {}});
            break;
        default:
            break;
        }
        bits.elements.push_back(std::move(word));
    }
    return {std::move(kinds), std::move(bits), std::move(texts), std::move(blobs)};
}

std::optional<std::vector<Value>> decode_values(const HostValue& kinds, const HostValue& bits, const HostValue& texts,
                                                const HostValue& blobs) {
    if (kinds.bytes.size() != bits.elements.size()) return std::nullopt;
    std::vector<Value> values(kinds.bytes.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto& value = values[i];
        value.kind = static_cast<std::uint8_t>(kinds.bytes[i]);
        const auto word = bits.elements[i].bits;
        switch (value.kind) {
        case nvm::sql_kind::null:
            break;
        case nvm::sql_kind::boolean:
            if (word > 1) return std::nullopt;
            value.integer = static_cast<std::int64_t>(word);
            break;
        case nvm::sql_kind::integer:
            value.integer = static_cast<std::int64_t>(word);
            break;
        case nvm::sql_kind::real:
            std::memcpy(&value.real, &word, sizeof word);
            break;
        case nvm::sql_kind::text:
            if (word >= texts.elements.size()) return std::nullopt;
            value.bytes = texts.elements[word].bytes;
            break;
        case nvm::sql_kind::bytes:
            if (word >= blobs.elements.size()) return std::nullopt;
            value.bytes = blobs.elements[word].bytes;
            break;
        default:
            return std::nullopt;
        }
    }
    return values;
}

// ---------------------------------------------------------------------------
// Sessions

namespace {

SqlAnswer answer(const Status& status) {
    if (status.ok()) return {HostCompletion::ok(), {}};
    return {HostCompletion::failed(HostValue{status.code, {}, {}}), status.detail};
}

SqlAnswer answer_value(HostValue value) { return {HostCompletion::ok(std::move(value)), {}}; }

SqlAnswer invalid(std::string detail) {
    return answer(Status::failure(host_error::invalid_input, std::move(detail)));
}

HostValue tuple_of(std::vector<HostValue> elements) { return HostValue{0, {}, std::move(elements)}; }

HostValue text_list(const std::vector<std::string>& texts) {
    HostValue list;
    for (const auto& text : texts) list.elements.push_back(HostValue{0, text, {}});
    return list;
}

constexpr std::size_t argument_counts[] = {0, 1, 2, 1, 1, 1, 1, 6, 7, 6, 2, 1};

} // namespace

Session::Session(Engine& engine, Clock::time_point deadline) : engine_(engine), deadline_(deadline) {}

Session::~Session() { end(); }

void Session::end() {
    cursors_.clear();
    for (auto& [handle, lease] : leases_) {
        if (!lease.connection) continue;
        if (lease.transaction && lease.connection->usable()) {
            Wait wait;
            wait.deadline = Clock::now() + std::chrono::seconds(5);
            if (!lease.connection->rollback(wait).ok()) {
                lease.pool->give_back(std::move(lease.connection), true);
                continue;
            }
        }
        lease.pool->give_back(std::move(lease.connection), false);
    }
    leases_.clear();
}

Session::Lease* Session::lease(const HostValue& handle) {
    const auto found = leases_.find(static_cast<std::int64_t>(handle.bits));
    return found == leases_.end() ? nullptr : &found->second;
}

Status Session::statement_arguments(const std::vector<HostValue>& arguments, std::size_t first,
                                    std::vector<Value>& values) const {
    auto decoded = decode_values(arguments[first], arguments[first + 1], arguments[first + 2], arguments[first + 3]);
    if (!decoded) return Status::failure(host_error::invalid_input, "the parameter lists do not agree");
    values = std::move(*decoded);
    return Status::success();
}

void Session::close_cursor_of(Lease& lease) {
    if (lease.cursor != 0) {
        cursors_.erase(lease.cursor);
        lease.cursor = 0;
    }
}

namespace {

std::size_t encoded_bytes(const std::vector<Value>& values) {
    std::size_t total = 0;
    for (const auto& value : values) total += 16 + value.bytes.size();
    return total;
}

} // namespace

SqlAnswer Session::call(SqlOperation operation, const std::vector<HostValue>& arguments) {
    const auto number = static_cast<std::uint32_t>(operation);
    if (number < 1 || number > nvm::sql_operation_count || arguments.size() != argument_counts[number]) {
        return invalid("malformed SQL call");
    }
    Wait wait;
    wait.deadline = deadline_;
    wait.cancelled = &cancelled_;
    if (cancelled_.load()) return answer(wait.interrupted());

    switch (operation) {
    case SqlOperation::Pool:
        if (!engine_.find(arguments[0].bytes)) {
            return answer(Status::failure(host_error::not_found, "no SQL pool named " + arguments[0].bytes));
        }
        return answer(Status::success());

    case SqlOperation::Acquire: {
        auto* pool = engine_.find(arguments[0].bytes);
        if (!pool) return answer(Status::failure(host_error::not_found, "no SQL pool named " + arguments[0].bytes));
        const auto requested = static_cast<std::int64_t>(arguments[1].bits);
        const auto limit = requested > 0 ? std::chrono::nanoseconds(requested)
                                         : std::chrono::nanoseconds(std::chrono::hours(1));
        std::unique_ptr<Connection> connection;
        const auto status = pool->acquire(wait, limit, connection);
        if (!status.ok()) return answer(status);
        const auto handle = next_handle_++;
        leases_[handle] = Lease{pool, std::move(connection), false, 0};
        return answer_value(HostValue{static_cast<std::uint64_t>(handle), {}, {}});
    }

    case SqlOperation::Release: {
        auto* held = lease(arguments[0]);
        if (!held) return invalid("unknown connection handle");
        close_cursor_of(*held);
        Status status;
        bool discard = false;
        if (held->transaction && held->connection->usable()) {
            status = held->connection->rollback(wait);
            discard = !status.ok();
        }
        held->pool->give_back(std::move(held->connection), discard);
        leases_.erase(static_cast<std::int64_t>(arguments[0].bits));
        return answer(Status::success());
    }

    case SqlOperation::Begin:
    case SqlOperation::Commit:
    case SqlOperation::Rollback: {
        auto* held = lease(arguments[0]);
        if (!held) return invalid("unknown connection handle");
        if (!held->connection->usable()) {
            return answer(Status::failure(host_error::connection_reset, "the connection was lost"));
        }
        if (held->cursor != 0) return invalid(std::string(one_cursor_detail));
        if (operation == SqlOperation::Begin) {
            if (held->transaction) return invalid("a transaction is already open on this connection");
            const auto status = held->connection->begin(wait);
            if (status.ok()) held->transaction = true;
            return answer(status);
        }
        if (!held->transaction) return invalid("no transaction is open on this connection");
        held->transaction = false;
        if (operation == SqlOperation::Commit) return answer(held->connection->commit(wait));
        return answer(held->connection->rollback(wait));
    }

    case SqlOperation::Execute: {
        auto* held = lease(arguments[0]);
        if (!held) return invalid("unknown connection handle");
        if (!held->connection->usable()) {
            return answer(Status::failure(host_error::connection_reset, "the connection was lost"));
        }
        if (held->cursor != 0) return invalid(std::string(one_cursor_detail));
        std::vector<Value> parameters;
        if (const auto status = statement_arguments(arguments, 2, parameters); !status.ok()) return answer(status);
        std::int64_t affected = 0;
        const auto status = held->connection->execute(arguments[1].bytes, parameters, wait, affected);
        if (!status.ok()) return answer(status);
        return answer_value(HostValue{static_cast<std::uint64_t>(affected), {}, {}});
    }

    case SqlOperation::Query:
    case SqlOperation::OpenCursor: {
        auto* held = lease(arguments[0]);
        if (!held) return invalid("unknown connection handle");
        if (!held->connection->usable()) {
            return answer(Status::failure(host_error::connection_reset, "the connection was lost"));
        }
        if (held->cursor != 0) return invalid(std::string(one_cursor_detail));
        const auto& config = held->pool->config();
        std::int32_t limit = 0;
        if (operation == SqlOperation::Query) {
            limit = static_cast<std::int32_t>(static_cast<std::uint32_t>(arguments[6].bits));
            if (limit < 0) return invalid("a negative row limit");
            if (static_cast<std::size_t>(limit) > config.page_rows) {
                return answer(Status::failure(host_error::too_large,
                                              "a page of " + std::to_string(limit) + " rows exceeds the pool's " +
                                                  std::to_string(config.page_rows) + " (S-11)"));
            }
        }
        std::vector<Value> parameters;
        if (const auto status = statement_arguments(arguments, 2, parameters); !status.ok()) return answer(status);
        std::unique_ptr<Cursor> cursor;
        auto status = held->connection->open(arguments[1].bytes, parameters, wait, cursor);
        if (!status.ok()) return answer(status);
        if (operation == SqlOperation::OpenCursor) {
            const auto handle = next_handle_++;
            const auto columns = text_list(cursor->columns());
            held->cursor = handle;
            cursors_[handle] = OpenCursor{static_cast<std::int64_t>(arguments[0].bits), std::move(cursor)};
            return answer_value(tuple_of({HostValue{static_cast<std::uint64_t>(handle), {}, {}}, columns}));
        }
        std::vector<Value> values;
        bool more = false;
        if (limit > 0) {
            status = cursor->fetch(static_cast<std::size_t>(limit), wait, values, more);
            if (!status.ok()) return answer(status);
        } else {
            // A limit of zero runs the statement and reads no row.
            std::vector<Value> none;
            status = cursor->fetch(0, wait, none, more);
            if (!status.ok()) return answer(status);
        }
        const auto columns = cursor->columns();
        cursor.reset();
        if (encoded_bytes(values) > config.page_bytes) {
            return answer(Status::failure(host_error::too_large,
                                          "the page holds more than " + std::to_string(config.page_bytes) +
                                              " bytes; read it with a cursor in smaller pages"));
        }
        auto encoded = encode_values(values);
        std::vector<HostValue> fields;
        fields.push_back(text_list(columns));
        for (auto& list : encoded) fields.push_back(std::move(list));
        fields.push_back(HostValue{more ? 1u : 0u, {}, {}});
        return answer_value(tuple_of(std::move(fields)));
    }

    case SqlOperation::Fetch: {
        const auto found = cursors_.find(static_cast<std::int64_t>(arguments[0].bits));
        if (found == cursors_.end()) return invalid("unknown cursor handle");
        const auto max = static_cast<std::int32_t>(static_cast<std::uint32_t>(arguments[1].bits));
        auto& held = leases_.at(found->second.connection);
        if (max < 1) return invalid("a fetch of fewer than one row");
        if (static_cast<std::size_t>(max) > held.pool->config().page_rows) {
            return answer(Status::failure(host_error::too_large,
                                          "a page of " + std::to_string(max) + " rows exceeds the pool's " +
                                              std::to_string(held.pool->config().page_rows) + " (S-11)"));
        }
        std::vector<Value> values;
        bool more = false;
        const auto status = found->second.cursor->fetch(static_cast<std::size_t>(max), wait, values, more);
        if (!status.ok()) return answer(status);
        if (encoded_bytes(values) > held.pool->config().page_bytes) {
            return answer(Status::failure(host_error::too_large,
                                          "the page holds more than " +
                                              std::to_string(held.pool->config().page_bytes) +
                                              " bytes; fetch fewer rows at a time"));
        }
        auto encoded = encode_values(values);
        std::vector<HostValue> fields;
        for (auto& list : encoded) fields.push_back(std::move(list));
        fields.push_back(HostValue{more ? 1u : 0u, {}, {}});
        return answer_value(tuple_of(std::move(fields)));
    }

    case SqlOperation::CloseCursor: {
        const auto found = cursors_.find(static_cast<std::int64_t>(arguments[0].bits));
        if (found == cursors_.end()) return invalid("unknown cursor handle");
        leases_.at(found->second.connection).cursor = 0;
        cursors_.erase(found);
        return answer(Status::success());
    }
    }
    return invalid("malformed SQL call");
}

} // namespace neura::sql
