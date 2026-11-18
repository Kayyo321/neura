// Migrations of an SQL pool (docs/runtime/Sql.md#migrations): files
// NNNN_name.sql numbered from 0001 without gaps, applied forward only, in
// order, each in its own transaction, and recorded in neura_migrations.

#include "nvm/providers.hpp"
#include "sql/sql_engine.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <cstdlib>

namespace neura::sql {

namespace host_error = nvm::host_error;
namespace fs = std::filesystem;

namespace {

std::string e1044(const fs::path& directory, const std::string& reason) {
    return "error[E1044]: invalid migration set in " + directory.string() + ": " + reason;
}

bool only_space(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

std::string number_text(std::uint32_t version) {
    char text[16];
    std::snprintf(text, sizeof text, "%04u", version);
    return text;
}

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#ifdef _WIN32
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%SZ", &parts);
    return text;
}

Value text_value(std::string text) {
    Value value;
    value.kind = nvm::sql_kind::text;
    value.bytes = std::move(text);
    return value;
}

Value int_value(std::int64_t number) {
    Value value;
    value.kind = nvm::sql_kind::integer;
    value.integer = number;
    return value;
}

// A key the advisory lock of every Neura host shares: "neura.mg" as bytes.
constexpr const char* lock_sql = "SELECT pg_advisory_lock(7954879748133234535)";
constexpr const char* unlock_sql = "SELECT pg_advisory_unlock(7954879748133234535)";

} // namespace

std::optional<std::string> read_migrations(const fs::path& directory, std::vector<Migration>& out) {
    std::error_code error;
    if (!fs::is_directory(directory, error)) return e1044(directory, "it is not a directory");
    std::map<std::uint32_t, Migration> found;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (!entry.is_regular_file()) continue;
        const auto file = entry.path().filename().string();
        if (entry.path().extension() != ".sql") continue;
        const auto stem = file.substr(0, file.size() - 4);
        const bool shaped = stem.size() >= 6 && stem[4] == '_' &&
                            std::all_of(stem.begin(), stem.begin() + 4, [](char c) { return c >= '0' && c <= '9'; }) &&
                            std::all_of(stem.begin() + 5, stem.end(), [](char c) {
                                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                                       c == '_';
                            });
        if (!shaped) return e1044(directory, file + " does not match NNNN_name.sql");
        const auto version = static_cast<std::uint32_t>(std::stoul(stem.substr(0, 4)));
        if (version == 0) return e1044(directory, file + ": numbering starts at 0001");
        std::ifstream input(entry.path(), std::ios::binary);
        std::stringstream text;
        text << input.rdbuf();
        if (!input) return e1044(directory, "cannot read " + file);
        Migration migration;
        migration.version = version;
        migration.name = stem.substr(5);
        migration.file = file;
        migration.sql = text.str();
        if (only_space(migration.sql)) return e1044(directory, file + " is empty");
        migration.checksum = util::sha256_hex(migration.sql);
        if (const auto other = found.find(version); other != found.end()) {
            const auto& first = std::min(other->second.file, file);
            const auto& second = std::max(other->second.file, file);
            return e1044(directory, first + " and " + second + " repeat the number " + number_text(version));
        }
        found.emplace(version, std::move(migration));
    }
    if (error) return e1044(directory, "cannot list it: " + error.message());
    std::uint32_t expected = 1;
    for (auto& [version, migration] : found) {
        if (version != expected) {
            return e1044(directory, "number " + number_text(expected) + " is missing before " + migration.file);
        }
        ++expected;
        out.push_back(std::move(migration));
    }
    return std::nullopt;
}

Status migrate(Pool& pool, const std::vector<Migration>& migrations, bool apply, MigrationReport& report) {
    Wait wait;
    wait.deadline = Clock::now() + std::chrono::minutes(10);
    std::unique_ptr<Connection> connection;
    if (auto status = pool.acquire(wait, std::chrono::seconds(30), connection); !status.ok()) return status;
    const bool postgres = connection->provider() == "postgresql";
    struct Return {
        Pool& pool;
        std::unique_ptr<Connection>& connection;
        bool postgres;
        ~Return() {
            if (!connection) return;
            Wait wait;
            wait.deadline = Clock::now() + std::chrono::seconds(10);
            if (postgres && connection->usable()) (void)connection->execute_script(unlock_sql, wait);
            pool.give_back(std::move(connection), false);
        }
    } give_back{pool, connection, postgres};

    const auto failed = [&](const Status& status, const std::string& what) {
        return Status::failure(status.code, what + ": " + status.detail);
    };
    if (postgres) {
        if (auto status = connection->execute_script(lock_sql, wait); !status.ok()) {
            return failed(status, "cannot take the migration lock");
        }
    }
    std::int64_t ignored = 0;
    if (auto status = connection->execute(
            "CREATE TABLE IF NOT EXISTS neura_migrations (version INTEGER PRIMARY KEY, name TEXT NOT NULL, "
            "checksum TEXT NOT NULL, applied_at TEXT NOT NULL)",
            {}, wait, ignored);
        !status.ok()) {
        return failed(status, "cannot create neura_migrations");
    }
    for (;;) {
        if (auto status = connection->begin(wait); !status.ok()) return failed(status, "cannot begin a migration");
        struct Applied {
            std::int64_t version;
            std::string name;
            std::string checksum;
        };
        std::vector<Applied> applied;
        {
            std::unique_ptr<Cursor> cursor;
            auto status = connection->open("SELECT version, name, checksum FROM neura_migrations ORDER BY version", {},
                                           wait, cursor);
            std::vector<Value> values;
            bool more = true;
            while (status.ok() && more) status = cursor->fetch(1000, SIZE_MAX, wait, values, more);
            if (!status.ok()) {
                cursor.reset();
                (void)connection->rollback(wait);
                return failed(status, "cannot read neura_migrations");
            }
            for (std::size_t i = 0; i + 2 < values.size(); i += 3) {
                applied.push_back({values[i].integer, values[i + 1].bytes, values[i + 2].bytes});
            }
        }
        for (std::size_t i = 0; i < applied.size(); ++i) {
            const auto& row = applied[i];
            const auto refuse = [&](std::string reason) {
                (void)connection->rollback(wait);
                return Status::failure(host_error::invalid_input, std::move(reason));
            };
            if (row.version != static_cast<std::int64_t>(i + 1) || row.version > static_cast<std::int64_t>(migrations.size())) {
                return refuse("migration " + number_text(static_cast<std::uint32_t>(row.version)) + "_" + row.name +
                              " is applied to the database but is not in the migration directory");
            }
            const auto& file = migrations[i];
            if (file.checksum != row.checksum) {
                return refuse("the applied migration " + file.file + " was changed afterwards: applied with checksum " +
                              row.checksum + ", the file has " + file.checksum + "; nothing is repaired or re-run");
            }
        }
        if (applied.size() == migrations.size()) {
            (void)connection->rollback(wait);
            return Status::success();
        }
        if (!apply) {
            for (std::size_t i = applied.size(); i < migrations.size(); ++i) report.pending.push_back(migrations[i].file);
            (void)connection->rollback(wait);
            return Status::success();
        }
        const auto& next = migrations[applied.size()];
        if (auto status = connection->execute_script(next.sql, wait); !status.ok()) {
            (void)connection->rollback(wait);
            return failed(status, "migration " + next.file + " failed");
        }
        if (auto status = connection->execute(
                "INSERT INTO neura_migrations (version, name, checksum, applied_at) VALUES ($1, $2, $3, $4)",
                {int_value(next.version), text_value(next.name), text_value(next.checksum), text_value(utc_now())},
                wait, ignored);
            !status.ok()) {
            (void)connection->rollback(wait);
            return failed(status, "cannot record migration " + next.file);
        }
        if (auto status = connection->commit(wait); !status.ok()) return failed(status, "cannot commit " + next.file);
        report.applied.push_back(next.file);
    }
}

LocalSql::LocalSql(const std::vector<std::string>& options) {
    if (options.empty()) return;
    std::vector<PoolConfig> pools;
    std::map<std::string, fs::path, std::less<>> directories;
    std::map<std::string, std::string, std::less<>> passwords;
    for (const auto& option : options) {
        const auto equals = option.find('=');
        const auto dot = option.find('.');
        if (dot != std::string::npos && equals != std::string::npos && dot < equals) {
            const auto setting = option.substr(dot + 1, equals - dot - 1);
            const auto value = option.substr(equals + 1);
            if (setting == "migrations") {
                directories[option.substr(0, dot)] = fs::absolute(value);
                continue;
            }
            if (setting == "password_env") {
                const char* secret = std::getenv(value.c_str());
                if (!secret) throw std::runtime_error("--sql-pool " + option + ": the variable is not set");
                passwords[option.substr(0, dot)] = secret;
                continue;
            }
            throw std::runtime_error("--sql-pool " + option.substr(0, equals) + ": the settings are migrations and password_env");
        }
        pools.push_back(parse_pool_option(option));
    }
    for (auto& pool : pools) {
        if (const auto found = passwords.find(pool.name); found != passwords.end()) {
            pool.password = found->second;
            passwords.erase(found);
        }
        if (const auto found = directories.find(pool.name); found != directories.end()) {
            pool.migrations = found->second;
            directories.erase(found);
        }
    }
    if (!passwords.empty()) throw std::runtime_error("--sql-pool " + passwords.begin()->first + ".password_env names no pool");
    if (!directories.empty()) {
        throw std::runtime_error("--sql-pool " + directories.begin()->first + ".migrations names no pool");
    }
    engine_ = std::make_unique<Engine>(std::move(pools));
    for (auto* pool : engine_->pools()) {
        if (const auto status = pool->check(); !status.ok()) {
            throw std::runtime_error("pool " + pool->config().name + ": " + status.detail);
        }
        if (!pool->config().migrations) continue;
        std::vector<Migration> migrations;
        if (const auto problem = read_migrations(*pool->config().migrations, migrations)) {
            throw std::runtime_error(*problem);
        }
        MigrationReport report;
        if (const auto status = migrate(*pool, migrations, true, report); !status.ok()) {
            throw std::runtime_error("pool " + pool->config().name + ": " + status.detail);
        }
    }
    session_ = std::make_unique<Session>(*engine_);
}

LocalSql::~LocalSql() = default;

} // namespace neura::sql
