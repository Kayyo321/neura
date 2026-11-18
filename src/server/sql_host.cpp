#include "server/sql_host.hpp"

#include "nvm/providers.hpp"
#include "server/sql_channel.hpp"

#ifdef NEURA_HAS_SQL
#include "sql/sql_engine.hpp"
#endif

#include <stdexcept>

namespace neura::server {

bool is_sql_call(std::span<const std::uint8_t> body) {
    return body.size() >= 8 && body[4] == worker::sql_call_operation && body[5] == 0 && body[6] == 0 && body[7] == 0;
}

#ifdef NEURA_HAS_SQL

SqlHost::SqlHost(const std::vector<std::string>& pool_options, std::map<std::string, std::string, std::less<>>& config,
                 const std::filesystem::path& base) {
    std::map<std::string, sql::PoolConfig, std::less<>> pools;
    std::string problems;
    for (const auto& option : pool_options) {
        try {
            auto pool = sql::parse_pool_option(option);
            auto& entry = pools[pool.name];
            entry.name = pool.name;
            entry.url = pool.url;
        } catch (const std::exception& error) {
            problems += std::string("\n  ") + error.what();
        }
    }
    for (auto it = config.begin(); it != config.end();) {
        if (!it->first.starts_with("sql.")) {
            ++it;
            continue;
        }
        try {
            sql::apply_pool_setting(pools, std::string_view(it->first).substr(4), it->second, base);
        } catch (const std::exception& error) {
            problems += std::string("\n  ") + error.what();
        }
        it = config.erase(it);
    }
    std::vector<sql::PoolConfig> list;
    for (auto& [name, pool] : pools) {
        if (pool.url.empty()) {
            problems += "\n  pool " + name + " has settings but no url (sql." + name + ".url or --sql-pool)";
            continue;
        }
        list.push_back(std::move(pool));
    }
    if (problems.empty()) {
        try {
            engine_ = std::make_unique<sql::Engine>(std::move(list));
        } catch (const std::exception& error) {
            problems += std::string("\n  ") + error.what();
        }
    }
    if (!problems.empty()) throw std::runtime_error("invalid SQL configuration:" + problems);
}

SqlHost::~SqlHost() = default;

bool SqlHost::enabled() const { return engine_ && !engine_->empty(); }

std::optional<std::string> SqlHost::prepare(bool apply_all, std::vector<std::string>& messages) {
    if (!engine_) return std::nullopt;
    for (auto* pool : engine_->pools()) {
        const auto& config = pool->config();
        if (const auto status = pool->check(); !status.ok()) {
            return "pool " + config.name + " (" + config.redacted_url() + "): " + status.detail;
        }
        if (!config.migrations) continue;
        std::vector<sql::Migration> migrations;
        if (const auto problem = sql::read_migrations(*config.migrations, migrations)) return *problem;
        const bool apply = apply_all || config.migrate == sql::MigrateMode::OnStart;
        sql::MigrationReport report;
        if (const auto status = sql::migrate(*pool, migrations, apply, report); !status.ok()) {
            return "pool " + config.name + ": " + status.detail;
        }
        for (const auto& file : report.applied) messages.push_back("pool " + config.name + ": applied " + file);
        if (!report.pending.empty()) {
            std::string list;
            for (const auto& file : report.pending) list += (list.empty() ? "" : ", ") + file;
            return "pool " + config.name + " has pending migrations (" + list +
                   "); apply them with `neura-server-host migrate` or set sql." + config.name + ".migrate=on_start";
        }
    }
    return std::nullopt;
}

std::shared_ptr<sql::Session> SqlHost::session(std::chrono::steady_clock::time_point deadline) {
    if (!enabled()) return nullptr;
    return std::make_shared<sql::Session>(*engine_, deadline);
}

std::vector<std::uint8_t> SqlHost::answer(sql::Session* session, std::span<const std::uint8_t> body) {
    const auto call = worker::decode_sql_call(body);
    std::uint32_t number = 0;
    if (body.size() >= 4) number = body[0] | (body[1] << 8) | (body[2] << 16) | (static_cast<std::uint32_t>(body[3]) << 24);
    nvm::SqlAnswer answer;
    if (!call) {
        answer = {nvm::HostCompletion::failed(nvm::HostValue{nvm::host_error::invalid_input, {}, {}}),
                  "malformed SQL call"};
    } else if (!session) {
        answer = {nvm::HostCompletion::failed(nvm::HostValue{nvm::host_error::capability_denied, {}, {}}),
                  "the host has no SQL pools"};
    } else {
        answer = session->call(call->operation, call->arguments);
    }
    auto encoded = worker::encode_sql_return(number, answer);
    if (encoded.size() + worker::frame_header_bytes > worker::default_frame_bytes) {
        answer = {nvm::HostCompletion::failed(nvm::HostValue{nvm::host_error::too_large, {}, {}}),
                  "the answer exceeds the channel's 1 MiB frame; read fewer rows at a time"};
        encoded = worker::encode_sql_return(number, answer);
    }
    return encoded;
}

#else

} // namespace neura::server

namespace neura::sql {
class Engine {};
class Session {};
} // namespace neura::sql

namespace neura::server {

SqlHost::SqlHost(const std::vector<std::string>& pool_options, std::map<std::string, std::string, std::less<>>& config,
                 const std::filesystem::path&) {
    bool any = !pool_options.empty();
    for (auto it = config.begin(); it != config.end();) {
        if (it->first.starts_with("sql.")) {
            any = true;
            it = config.erase(it);
        } else {
            ++it;
        }
    }
    if (any) throw std::runtime_error("this build has no SQL providers (NEURA_ENABLE_SQL is off)");
}

SqlHost::~SqlHost() = default;
bool SqlHost::enabled() const { return false; }
std::optional<std::string> SqlHost::prepare(bool, std::vector<std::string>&) { return std::nullopt; }
std::shared_ptr<sql::Session> SqlHost::session(std::chrono::steady_clock::time_point) { return nullptr; }

std::vector<std::uint8_t> SqlHost::answer(sql::Session*, std::span<const std::uint8_t> body) {
    std::uint32_t number = 0;
    if (body.size() >= 4) number = body[0] | (body[1] << 8) | (body[2] << 16) | (static_cast<std::uint32_t>(body[3]) << 24);
    return worker::encode_sql_return(
        number, {nvm::HostCompletion::failed(nvm::HostValue{nvm::host_error::capability_denied, {}, {}}),
                 "the host has no SQL pools"});
}

#endif

} // namespace neura::server
