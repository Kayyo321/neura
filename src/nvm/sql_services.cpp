#include "sql_services.hpp"

#include "provider_internal.hpp"

#include "nvr/format.hpp"

namespace neura::nvm {

using namespace provider;

namespace {

HostType uint8_list() { return HostType(Kind::List, std::vector<HostType>{HostType(Kind::Unsigned, 8)}); }
HostType uint64_list() { return HostType(Kind::List, std::vector<HostType>{HostType(Kind::Unsigned, 64)}); }
HostType blob_list() { return HostType(Kind::List, std::vector<HostType>{byte_list()}); }

// The four parallel lists of a value sequence.
std::vector<HostType> values() { return {uint8_list(), uint64_list(), text_list(), blob_list()}; }

std::vector<HostType> with(std::vector<HostType> head, const std::vector<HostType>& tail) {
    head.insert(head.end(), tail.begin(), tail.end());
    return head;
}

} // namespace

struct SqlServices::State {
    SqlExchange* exchange = nullptr;
    bool granted = false;
    std::string detail;
};

SqlServices::SqlServices(SqlExchange* exchange, bool granted) : state_(std::make_shared<State>()) {
    state_->exchange = exchange;
    state_->granted = granted && exchange;
    const auto state = state_;
    const auto add = [&](SqlOperation operation, std::string name, std::vector<HostType> parameters,
                         HostType success) {
        services_.push_back(std::make_unique<FunctionService>(
            signature(std::move(name), std::move(parameters), std::move(success)),
            [state, operation](const HostRequest& request) {
                if (!state->granted) {
                    state->detail = "the sql capability group is not granted; run the image with neura-server-host "
                                    "or neura run --sql-pool";
                    return denied();
                }
                auto answer = state->exchange->call(operation, request.arguments);
                state->detail = answer.completion.success ? std::string() : std::move(answer.detail);
                // The detail becomes a Neura string, which must be UTF-8.
                if (!nvr::valid_utf8(state->detail)) {
                    for (auto& c : state->detail) {
                        if (static_cast<unsigned char>(c) >= 0x80) c = '?';
                    }
                }
                return std::move(answer.completion);
            }));
    };
    const auto handle = int64();
    add(SqlOperation::Pool, "neura.sql.pool", {text()}, void_type());
    add(SqlOperation::Acquire, "neura.sql.acquire", {text(), int64()}, handle);
    add(SqlOperation::Release, "neura.sql.release", {handle}, void_type());
    add(SqlOperation::Begin, "neura.sql.begin", {handle}, void_type());
    add(SqlOperation::Commit, "neura.sql.commit", {handle}, void_type());
    add(SqlOperation::Rollback, "neura.sql.rollback", {handle}, void_type());
    add(SqlOperation::Execute, "neura.sql.execute", with({handle, text()}, values()), int64());
    add(SqlOperation::Query, "neura.sql.query", with(with({handle, text()}, values()), {int32()}),
        tuple(with(with({text_list()}, values()), {boolean()})));
    add(SqlOperation::OpenCursor, "neura.sql.open_cursor", with({handle, text()}, values()),
        tuple({int64(), text_list()}));
    add(SqlOperation::Fetch, "neura.sql.fetch", {handle, int32()}, tuple(with(values(), {boolean()})));
    add(SqlOperation::CloseCursor, "neura.sql.close_cursor", {handle}, void_type());
    // Answered here: the detail stays in the execution, so it is recorded
    // and replayed with the rest of the run.
    services_.push_back(std::make_unique<FunctionService>(
        signature("neura.sql.detail", {}, text()),
        [state](const HostRequest&) { return HostCompletion::ok(bytes_value(state->detail)); }));
}

void SqlServices::register_all(HostRegistry& registry) {
    for (auto& service : services_) registry.add(*service);
}

} // namespace neura::nvm
