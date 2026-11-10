#include "trace_file.hpp"

#include <array>
#include <optional>
#include <charconv>
#include <utility>

namespace neura::nvm {
namespace {

constexpr std::array<std::pair<TrapKind, std::string_view>, 18> trap_kinds{{
    {TrapKind::Program, "program"},
    {TrapKind::GraphOverflow, "graph-overflow"},
    {TrapKind::NonBinaryLane, "non-binary-lane"},
    {TrapKind::InvalidSelector, "invalid-selector"},
    {TrapKind::UninitializedSlot, "uninitialized-slot"},
    {TrapKind::UninitializedField, "uninitialized-field"},
    {TrapKind::UninitializedGlobal, "uninitialized-global"},
    {TrapKind::InvalidHandle, "invalid-handle"},
    {TrapKind::NilReference, "nil-reference"},
    {TrapKind::InvalidCast, "invalid-cast"},
    {TrapKind::ReferenceType, "reference-type"},
    {TrapKind::IndexOutOfBounds, "index-out-of-bounds"},
    {TrapKind::InvalidText, "invalid-text"},
    {TrapKind::InvalidCompletion, "invalid-completion"},
    {TrapKind::StepLimit, "step-limit"},
    {TrapKind::FrameLimit, "frame-limit"},
    {TrapKind::HeapLimit, "heap-limit"},
    {TrapKind::HandleLimit, "handle-limit"},
}};

constexpr char hex_digits[] = "0123456789abcdef";

void write_string(std::string& out, std::string_view bytes) {
    out += '"';
    for (const char c : bytes) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte >= 0x20 && byte < 0x7f) {
                out += c;
            } else {
                out += "\\x";
                out += hex_digits[byte >> 4];
                out += hex_digits[byte & 15];
            }
        }
    }
    out += '"';
}

void write_value(std::string& out, const HostValue& value) {
    const bool has_bits = value.bits != 0;
    const bool has_bytes = !value.bytes.empty();
    const bool has_elements = !value.elements.empty();
    if (int(has_bits) + int(has_bytes) + int(has_elements) > 1) {
        throw std::invalid_argument("a host value mixes numeric, byte, and element contents");
    }
    if (has_elements) {
        out += '[';
        for (std::size_t i = 0; i < value.elements.size(); ++i) {
            if (i != 0) out += ", ";
            write_value(out, value.elements[i]);
        }
        out += ']';
    } else if (has_bytes) {
        write_string(out, value.bytes);
    } else {
        out += std::to_string(value.bits);
    }
}

void write_list(std::string& out, const std::vector<HostValue>& values) {
    HostValue list;
    list.elements = values;
    if (values.empty()) {
        out += "[]";
    } else {
        write_value(out, list);
    }
}

class Reader {
public:
    Reader(std::string_view text, std::size_t line) : text_(text), line_(line) {}

    [[noreturn]] void error(const std::string& message) const { throw TraceFormatError(line_, message); }

    bool at_end() const { return position_ == text_.size(); }

    void skip_spaces() {
        while (position_ < text_.size() && text_[position_] == ' ') ++position_;
    }

    void expect(char c) {
        if (position_ >= text_.size() || text_[position_] != c) {
            error(std::string("expected '") + c + "'");
        }
        ++position_;
    }

    std::string_view word() {
        skip_spaces();
        const auto start = position_;
        while (position_ < text_.size() && text_[position_] != ' ') ++position_;
        if (start == position_) error("expected a field");
        return text_.substr(start, position_ - start);
    }

    std::uint64_t number(std::string_view token) const {
        std::uint64_t value = 0;
        const auto [end, failure] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (failure != std::errc{} || end != token.data() + token.size()) {
            error("expected an unsigned decimal number, found '" + std::string(token) + "'");
        }
        return value;
    }

    std::uint64_t number() {
        skip_spaces();
        const auto start = position_;
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        return number(text_.substr(start, position_ - start));
    }

    static int hex(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string string() {
        skip_spaces();
        expect('"');
        std::string bytes;
        for (;;) {
            if (position_ >= text_.size()) error("unterminated string");
            const char c = text_[position_++];
            if (c == '"') return bytes;
            if (c != '\\') {
                bytes += c;
                continue;
            }
            if (position_ >= text_.size()) error("unterminated escape");
            const char e = text_[position_++];
            switch (e) {
            case '"':
            case '\\':
                bytes += e;
                break;
            case 'n':
                bytes += '\n';
                break;
            case 'r':
                bytes += '\r';
                break;
            case 't':
                bytes += '\t';
                break;
            case 'x': {
                const int high = position_ < text_.size() ? hex(text_[position_]) : -1;
                const int low = position_ + 1 < text_.size() ? hex(text_[position_ + 1]) : -1;
                if (high < 0 || low < 0) error("expected two hexadecimal digits after \\x");
                bytes += static_cast<char>(high * 16 + low);
                position_ += 2;
                break;
            }
            default:
                error(std::string("unknown escape \\") + e);
            }
        }
    }

    HostValue value(unsigned depth = 0) {
        if (depth > 128) error("values nest deeper than 128");
        skip_spaces();
        if (position_ >= text_.size()) error("expected a value");
        HostValue result;
        const char c = text_[position_];
        if (c == '"') {
            result.bytes = string();
        } else if (c == '[') {
            result.elements = list(depth);
        } else {
            result.bits = number();
        }
        return result;
    }

    std::vector<HostValue> list(unsigned depth = 0) {
        skip_spaces();
        expect('[');
        std::vector<HostValue> values;
        skip_spaces();
        if (position_ < text_.size() && text_[position_] == ']') {
            ++position_;
            return values;
        }
        for (;;) {
            values.push_back(value(depth + 1));
            skip_spaces();
            if (position_ < text_.size() && text_[position_] == ']') {
                ++position_;
                return values;
            }
            expect(',');
        }
    }

    void finish() {
        skip_spaces();
        if (!at_end()) error("unexpected text '" + std::string(text_.substr(position_)) + "'");
    }

private:
    std::string_view text_;
    std::size_t line_;
    std::size_t position_ = 0;
};

TrapKind trap_kind(const Reader& reader, std::string_view name) {
    for (const auto& [kind, token] : trap_kinds) {
        if (token == name) return kind;
    }
    reader.error("unknown trap kind '" + std::string(name) + "'");
}

bool valid_import_name(std::string_view name) {
    if (name.empty()) return false;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte <= 0x20 || byte >= 0x7f || c == '"' || c == '[') return false;
    }
    return true;
}

} // namespace

std::string_view trace_trap_kind_name(TrapKind kind) {
    for (const auto& [candidate, token] : trap_kinds) {
        if (candidate == kind) return token;
    }
    return "unknown";
}

std::string format_trace_value(const HostValue& value) {
    std::string text;
    write_value(text, value);
    return text;
}

std::string write_trace(const Trace& trace) {
    std::string out = "neura-trace " + std::to_string(trace_file_version) + "\n";
    for (const auto& entry : trace.entries) {
        if (!valid_import_name(entry.import)) {
            throw std::invalid_argument("import name '" + entry.import + "' cannot be written to a trace");
        }
        out += "request " + entry.import + " " + std::to_string(entry.version) + " ";
        write_list(out, entry.arguments);
        out += '\n';
        const auto& completion = entry.completion;
        if (completion.exit_status) {
            out += "exit " + std::to_string(*completion.exit_status);
        } else {
            out += completion.success ? "ok " : "error ";
            write_value(out, completion.value);
        }
        out += '\n';
    }
    const auto& outcome = trace.outcome;
    if (outcome.completed) {
        out += "outcome completed " + std::to_string(outcome.status) + "\n";
    } else {
        out += "outcome trap " + std::string(trace_trap_kind_name(outcome.trap)) + " " +
               std::to_string(outcome.reason) + " ";
        write_string(out, outcome.message);
        out += '\n';
    }
    return out;
}

Trace read_trace(std::string_view text) {
    Trace trace;
    std::size_t line_number = 0;
    std::size_t start = 0;
    bool header = false;
    bool outcome = false;
    std::optional<TraceEntry> pending;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        start = end + 1;
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line.front() == '#') continue;
        Reader reader(line, line_number);
        const auto keyword = reader.word();
        if (!header) {
            if (keyword != "neura-trace") reader.error("expected the header 'neura-trace " +
                                                       std::to_string(trace_file_version) + "'");
            const auto version = reader.number(reader.word());
            if (version != trace_file_version) {
                reader.error("unsupported trace version " + std::to_string(version) + "; expected " +
                             std::to_string(trace_file_version));
            }
            reader.finish();
            header = true;
            continue;
        }
        if (outcome) reader.error("text after the outcome line");
        if (pending) {
            auto& completion = pending->completion;
            if (keyword == "ok" || keyword == "error") {
                completion.success = keyword == "ok";
                completion.value = reader.value();
            } else if (keyword == "exit") {
                const auto status = reader.number(reader.word());
                if (status > 255) reader.error("exit status " + std::to_string(status) + " exceeds 255");
                completion = HostCompletion::terminate(static_cast<std::int32_t>(status));
            } else {
                reader.error("expected 'ok', 'error', or 'exit' after a request");
            }
            reader.finish();
            trace.entries.push_back(std::move(*pending));
            pending.reset();
            continue;
        }
        if (keyword == "request") {
            TraceEntry entry;
            entry.import = std::string(reader.word());
            if (!valid_import_name(entry.import)) reader.error("invalid import name");
            const auto version = reader.number(reader.word());
            if (version > 0xffffffffU) reader.error("version exceeds 32 bits");
            entry.version = static_cast<std::uint32_t>(version);
            entry.arguments = reader.list();
            reader.finish();
            pending = std::move(entry);
        } else if (keyword == "outcome") {
            const auto kind = reader.word();
            if (kind == "completed") {
                trace.outcome.completed = true;
                const auto token = reader.word();
                std::int32_t status = 0;
                const auto [end, failure] = std::from_chars(token.data(), token.data() + token.size(), status);
                if (failure != std::errc{} || end != token.data() + token.size()) {
                    reader.error("expected an int32 status, found '" + std::string(token) + "'");
                }
                trace.outcome.status = status;
            } else if (kind == "trap") {
                trace.outcome.completed = false;
                trace.outcome.trap = trap_kind(reader, reader.word());
                const auto reason = reader.number(reader.word());
                if (reason > 0xffffffffU) reader.error("reason exceeds 32 bits");
                trace.outcome.reason = static_cast<std::uint32_t>(reason);
                trace.outcome.message = reader.string();
            } else {
                reader.error("expected 'completed' or 'trap' after 'outcome'");
            }
            reader.finish();
            outcome = true;
        } else {
            reader.error("expected 'request' or 'outcome'");
        }
    }
    if (!header) throw TraceFormatError(line_number, "missing the header 'neura-trace 1'");
    if (pending) throw TraceFormatError(line_number, "the last request has no completion");
    if (!outcome) throw TraceFormatError(line_number, "missing the outcome line");
    return trace;
}

} // namespace neura::nvm
