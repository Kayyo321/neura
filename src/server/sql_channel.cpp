#include "server/sql_channel.hpp"

#include "nvm/providers.hpp"

namespace neura::server::worker {

namespace {

constexpr unsigned max_depth = 8;

class Writer {
public:
    void u8(std::uint8_t value) { out.push_back(value); }
    void u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void u64(std::uint64_t value) {
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
    void text(std::string_view bytes) {
        u32(static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    void value(const nvm::HostValue& value) {
        u64(value.bits);
        text(value.bytes);
        u32(static_cast<std::uint32_t>(value.elements.size()));
        for (const auto& element : value.elements) this->value(element);
    }
    std::vector<std::uint8_t> out;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> in) : in_(in) {}
    bool u8(std::uint8_t& value) {
        if (left() < 1) return false;
        value = in_[at_++];
        return true;
    }
    bool u32(std::uint32_t& value) {
        if (left() < 4) return false;
        value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in_[at_++]) << (8 * i);
        return true;
    }
    bool u64(std::uint64_t& value) {
        if (left() < 8) return false;
        value = 0;
        for (int i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(in_[at_++]) << (8 * i);
        return true;
    }
    bool text(std::string& value) {
        std::uint32_t size = 0;
        if (!u32(size) || left() < size) return false;
        value.assign(reinterpret_cast<const char*>(in_.data()) + at_, size);
        at_ += size;
        return true;
    }
    bool value(nvm::HostValue& value, unsigned depth = 0) {
        if (depth > max_depth) return false;
        std::uint32_t count = 0;
        if (!u64(value.bits) || !text(value.bytes) || !u32(count)) return false;
        // Each element takes at least 16 bytes.
        if (count > left() / 16) return false;
        value.elements.resize(count);
        for (auto& element : value.elements) {
            if (!this->value(element, depth + 1)) return false;
        }
        return true;
    }
    std::size_t left() const { return in_.size() - at_; }

private:
    std::span<const std::uint8_t> in_;
    std::size_t at_ = 0;
};

} // namespace

std::vector<std::uint8_t> encode_sql_arguments(nvm::SqlOperation operation,
                                               const std::vector<nvm::HostValue>& arguments) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(operation));
    writer.u32(static_cast<std::uint32_t>(arguments.size()));
    for (const auto& argument : arguments) writer.value(argument);
    return std::move(writer.out);
}

std::vector<std::uint8_t> encode_sql_call(const SqlCall& call) {
    Writer writer;
    writer.u32(call.call);
    writer.u32(sql_call_operation);
    auto out = std::move(writer.out);
    const auto rest = encode_sql_arguments(call.operation, call.arguments);
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

std::optional<SqlCall> decode_sql_call(std::span<const std::uint8_t> body) {
    Reader reader(body);
    SqlCall call;
    std::uint32_t operation = 0, sql_operation = 0, count = 0;
    if (!reader.u32(call.call) || !reader.u32(operation) || operation != sql_call_operation ||
        !reader.u32(sql_operation) || sql_operation < 1 || sql_operation > nvm::sql_operation_count ||
        !reader.u32(count) || count > 16) {
        return std::nullopt;
    }
    call.operation = static_cast<nvm::SqlOperation>(sql_operation);
    call.arguments.resize(count);
    for (auto& argument : call.arguments) {
        if (!reader.value(argument)) return std::nullopt;
    }
    if (reader.left() != 0) return std::nullopt;
    return call;
}

std::vector<std::uint8_t> encode_sql_return(std::uint32_t call, const nvm::SqlAnswer& answer) {
    Writer writer;
    writer.u32(call);
    writer.u32(0);
    writer.u8(answer.completion.success ? 1 : 0);
    writer.value(answer.completion.value);
    writer.text(answer.detail);
    return std::move(writer.out);
}

std::optional<nvm::SqlAnswer> decode_sql_answer(std::span<const std::uint8_t> payload) {
    Reader reader(payload);
    std::uint8_t success = 0;
    nvm::SqlAnswer answer;
    if (!reader.u8(success) || success > 1 || !reader.value(answer.completion.value) || !reader.text(answer.detail) ||
        reader.left() != 0) {
        return std::nullopt;
    }
    answer.completion.success = success == 1;
    return answer;
}

std::optional<nvm::SqlAnswer> decode_sql_return(std::uint32_t call, std::span<const std::uint8_t> body) {
    Reader reader(body);
    std::uint32_t returned = 0, transport = 0;
    if (!reader.u32(returned) || returned != call || !reader.u32(transport) || transport != 0) return std::nullopt;
    return decode_sql_answer(body.subspan(8));
}

nvm::SqlAnswer ChannelSql::call(nvm::SqlOperation operation, const std::vector<nvm::HostValue>& arguments) {
    const auto failed = [](std::uint32_t code, std::string detail) {
        return nvm::SqlAnswer{nvm::HostCompletion::failed(nvm::HostValue{code, {}, {}}), std::move(detail)};
    };
    const auto body = encode_sql_arguments(operation, arguments);
    // The Call adds its identifier and operation (8 bytes) to the arguments.
    if (body.size() + 8 + frame_header_bytes > default_frame_bytes) {
        return failed(nvm::host_error::too_large, "the statement and its parameters exceed the channel's 1 MiB frame");
    }
    std::uint32_t code = 0;
    const auto payload = transport_(body, code);
    if (!payload) {
        return failed(code != 0 ? code : nvm::host_error::connection_reset, "the host channel failed");
    }
    auto answer = decode_sql_answer(*payload);
    if (!answer) return failed(nvm::host_error::connection_reset, "malformed SQL answer");
    return std::move(*answer);
}

} // namespace neura::server::worker
