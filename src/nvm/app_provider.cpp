#include "app_services.hpp"

#include "provider_internal.hpp"

#include <algorithm>

namespace neura::nvm {

using namespace provider;

namespace {

// Header and log fields cross as one list of alternating names and values:
// a host call cannot carry a list of tuples.
HostType header_list() { return text_list(); }

HostValue pairs_value(const HeaderPairs& pairs) {
    HostValue list;
    list.elements.reserve(pairs.size() * 2);
    for (const auto& [name, value] : pairs) {
        list.elements.push_back(bytes_value(name));
        list.elements.push_back(bytes_value(value));
    }
    return list;
}

std::optional<HeaderPairs> pairs_of(const HostValue& value, bool header) {
    if (value.elements.size() % 2 != 0) return std::nullopt;
    HeaderPairs pairs;
    pairs.reserve(value.elements.size() / 2);
    for (std::size_t i = 0; i < value.elements.size(); i += 2) {
        const auto& name = value.elements[i].bytes;
        const auto& text = value.elements[i + 1].bytes;
        if (header && (!valid_header_name(name) || !valid_header_value(text))) return std::nullopt;
        pairs.emplace_back(name, text);
    }
    return pairs;
}

} // namespace

bool valid_header_name(std::string_view name) {
    if (name.empty()) return false;
    constexpr std::string_view extra = "!#$%&'*+-.^_`|~";
    return std::all_of(name.begin(), name.end(), [&](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               extra.find(c) != std::string_view::npos;
    });
}

bool valid_header_value(std::string_view value) {
    return std::none_of(value.begin(), value.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte == '\r' || byte == '\n' || byte == 0 || (byte < 0x20 && byte != '\t') || byte == 0x7f;
    });
}

struct ApplicationServices::State {
    enum class Response { None, Head, Ended };
    ServerExchange* exchange = nullptr;
    bool http_server = false;
    bool app = false;
    std::uint64_t response_bytes = default_response_bytes;
    bool assigned = false;
    Response response = Response::None;
    std::uint64_t sent = 0;
};

ApplicationServices::ApplicationServices(ServerExchange* exchange, bool http_server, bool app,
                                         std::uint64_t response_bytes)
    : state_(std::make_shared<State>()) {
    state_->exchange = exchange;
    state_->http_server = http_server && exchange;
    state_->app = app && exchange;
    state_->response_bytes = response_bytes;
    const auto state = state_;
    const auto add = [this](HostSignature signature, FunctionService::Handler handler) {
        services_.push_back(std::make_unique<FunctionService>(std::move(signature), std::move(handler)));
    };

    add(signature("neura.http.server.next", {},
                  tuple({uint32(), text(), text(), text(), header_list(), int64(), text(), text(), int64()})),
        [state](const HostRequest&) {
            if (!state->http_server) return denied();
            if (state->assigned) return fail(host_error::invalid_input);
            state->assigned = true;
            auto request = state->exchange->next();
            if (!request) return fail(host_error::unexpected_end);
            return HostCompletion::ok(tuple_value({
                scalar(request->kind), bytes_value(request->request_id), bytes_value(request->method),
                bytes_value(request->target), pairs_value(request->headers), int64_value(request->body_length),
                bytes_value(request->peer), bytes_value(request->scheme), int64_value(request->deadline)}));
        });

    add(signature("neura.http.server.body.read", {int32()}, byte_list()), [state](const HostRequest& request) {
        if (!state->http_server) return denied();
        if (!state->assigned) return fail(host_error::invalid_input);
        const auto max = as_int32(request.arguments.at(0));
        if (max < 1) return fail(host_error::invalid_input);
        return HostCompletion::ok(bytes_value(state->exchange->read_body(
            std::min<std::size_t>(static_cast<std::size_t>(max), max_read_bytes))));
    });

    // Counts body bytes against S-7 before anything is sent.
    const auto admit = [state](std::size_t bytes) {
        if (bytes > state->response_bytes - std::min(state->sent, state->response_bytes)) return false;
        state->sent += bytes;
        return true;
    };

    add(signature("neura.http.server.respond", {uint32(), header_list(), byte_list(), boolean()}, void_type()),
        [state, admit](const HostRequest& request) {
            if (!state->http_server) return denied();
            if (!state->assigned || state->response != State::Response::None) return fail(host_error::invalid_input);
            const auto status = static_cast<std::uint32_t>(request.arguments.at(0).bits);
            const auto headers = pairs_of(request.arguments.at(1), true);
            if (status < 100 || status > 599 || !headers) return fail(host_error::invalid_input);
            const auto& body = request.arguments.at(2).bytes;
            if (!admit(body.size())) return fail(host_error::too_large);
            const bool complete = as_bool(request.arguments.at(3));
            state->response = complete ? State::Response::Ended : State::Response::Head;
            const auto code = state->exchange->respond(status, *headers, body, complete);
            return code == 0 ? HostCompletion::ok() : fail(code);
        });

    add(signature("neura.http.server.body.write", {byte_list()}, void_type()),
        [state, admit](const HostRequest& request) {
            if (!state->http_server) return denied();
            if (state->response != State::Response::Head) return fail(host_error::invalid_input);
            const auto& bytes = request.arguments.at(0).bytes;
            if (!admit(bytes.size())) return fail(host_error::too_large);
            const auto code = state->exchange->write_body(bytes);
            return code == 0 ? HostCompletion::ok() : fail(code);
        });

    add(signature("neura.http.server.body.close", {}, void_type()), [state](const HostRequest&) {
        if (!state->http_server) return denied();
        if (state->response != State::Response::Head) return fail(host_error::invalid_input);
        state->response = State::Response::Ended;
        const auto code = state->exchange->close_body();
        return code == 0 ? HostCompletion::ok() : fail(code);
    });

    add(signature("neura.app.config", {text()}, tuple({boolean(), text()})), [state](const HostRequest& request) {
        if (!state->app) return denied();
        auto value = state->exchange->config(request.arguments.at(0).bytes);
        if (!value) return HostCompletion::ok(tuple_value({scalar(0), bytes_value({})}));
        return HostCompletion::ok(tuple_value({scalar(1), bytes_value(std::move(*value))}));
    });

    add(signature("neura.app.log", {uint32(), text(), header_list()}, void_type()),
        [state](const HostRequest& request) {
            if (!state->app) return denied();
            const auto level = static_cast<std::uint32_t>(request.arguments.at(0).bits);
            const auto fields = pairs_of(request.arguments.at(2), false);
            if (level > 4 || !fields) return fail(host_error::invalid_input);
            state->exchange->log(level, request.arguments.at(1).bytes, *fields);
            return HostCompletion::ok();
        });
}

void ApplicationServices::register_all(HostRegistry& registry) {
    for (auto& service : services_) registry.add(*service);
}

} // namespace neura::nvm
