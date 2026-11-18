#include "server/http_wire.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace neura::server::http {
namespace {

using std::string_view;

constexpr std::size_t max_header_fields = 100;

bool digit(char ch) { return ch >= '0' && ch <= '9'; }

char ascii_lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

bool alpha(char ch) { return ascii_lower(ch) >= 'a' && ascii_lower(ch) <= 'z'; }
bool ows(char ch) { return ch == ' ' || ch == '\t'; }

bool ascii_equal(string_view left, string_view right) {
    return std::ranges::equal(left, right, {}, ascii_lower, ascii_lower);
}

int hex_digit(char ch) {
    if (digit(ch)) return ch - '0';
    ch = ascii_lower(ch);
    return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

bool token_char(char ch) {
    return alpha(ch) || digit(ch) || string_view("!#$%&'*+-.^_`|~").find(ch) != string_view::npos;
}

bool field_value_char(unsigned char ch) {
    return ch == '\t' || (ch >= ' ' && ch != 0x7f);
}

void skip_ows(string_view& input) {
    while (!input.empty() && ows(input.front())) input.remove_prefix(1);
}

string_view trim_ows(string_view input) {
    skip_ows(input);
    while (!input.empty() && ows(input.back())) input.remove_suffix(1);
    return input;
}

string_view take_until(string_view& input, char delimiter) {
    const auto position = input.find(delimiter);
    const auto part = input.substr(0, position);
    input.remove_prefix(position == string_view::npos ? input.size() : position + 1);
    return part;
}

string_view take_token(string_view& input) {
    const auto end = std::ranges::find_if_not(input, token_char);
    const auto token = input.substr(0, static_cast<std::size_t>(end - input.begin()));
    input.remove_prefix(token.size());
    return token;
}

bool take_char(string_view& input, char expected) {
    if (input.empty() || input.front() != expected) return false;
    input.remove_prefix(1);
    return true;
}

// RFC 9110 quoted-string and quoted-pair, shared by coding parameters and
// chunk extensions. In particular, separators inside quotes are data.
bool take_parameter_value(string_view& input) {
    if (!take_char(input, '"')) return !take_token(input).empty();
    while (!input.empty()) {
        const char ch = input.front();
        input.remove_prefix(1);
        if (ch == '"') return true;
        if (ch == '\\') {
            if (input.empty() || !field_value_char(input.front())) return false;
            input.remove_prefix(1);
        } else if (!field_value_char(ch)) {
            return false;
        }
    }
    return false;
}

bool take_parameter(string_view& input, bool value_required) {
    skip_ows(input);
    if (!take_char(input, ';')) return false;
    skip_ows(input);
    if (take_token(input).empty()) return false;
    auto value = input;
    skip_ows(value);
    if (!take_char(value, '=')) return !value_required;
    skip_ows(value);
    if (!take_parameter_value(value)) return false;
    input = value;
    return true;
}

struct Field {
    string_view name;
    string_view value;
};

std::optional<Field> parse_field(string_view line) {
    const auto name = take_token(line);
    if (name.empty() || !take_char(line, ':') || !std::ranges::all_of(line, field_value_char)) {
        return std::nullopt;
    }
    return Field{name, trim_ows(line)};
}

// URI syntax is checked while still in wire form. Routing receives the original
// target and owns interpretation of its path and query.
bool uri_chars(string_view input, string_view extra) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (alpha(ch) || digit(ch) || string_view("-._~!$&'()*+,;=").find(ch) != string_view::npos ||
            extra.find(ch) != string_view::npos) continue;
        if (ch != '%' || input.size() - i < 3 || hex_digit(input[i + 1]) < 0 || hex_digit(input[i + 2]) < 0) {
            return false;
        }
        i += 2;
    }
    return true;
}

bool valid_ipv4(string_view input) {
    for (int i = 0; i < 4; ++i) {
        if ((input.find('.') == string_view::npos) != (i == 3)) return false;
        const auto part = take_until(input, '.');
        if (part.empty() || part.size() > 3 || (part.size() > 1 && part.front() == '0')) return false;
        unsigned octet = 0;
        for (char ch : part) {
            if (!digit(ch)) return false;
            octet = octet * 10 + static_cast<unsigned>(ch - '0');
        }
        if (octet > 255) return false;
    }
    return true;
}

// Zero groups is valid for the empty side of a compressed IPv6 literal.
int ipv6_groups(string_view input, bool allow_ipv4) {
    int groups = 0;
    if (input.ends_with(':')) return -1;
    while (!input.empty()) {
        const auto part = take_until(input, ':');
        if (part.find('.') != string_view::npos) {
            return allow_ipv4 && input.empty() && valid_ipv4(part) ? groups + 2 : -1;
        }
        if (part.empty() || part.size() > 4 || !std::ranges::all_of(part, [](char ch) { return hex_digit(ch) >= 0; })) {
            return -1;
        }
        if (++groups > 8) return -1;
    }
    return groups;
}

bool valid_ip_literal(string_view input) {
    if (input.starts_with('v') || input.starts_with('V')) {
        input.remove_prefix(1);
        if (input.find('.') == string_view::npos) return false;
        const auto version = take_until(input, '.');
        return !version.empty() && std::ranges::all_of(version, [](char ch) { return hex_digit(ch) >= 0; }) &&
               !input.empty() && input.find('%') == string_view::npos && uri_chars(input, ":");
    }
    const auto compressed = input.find("::");
    if (compressed == string_view::npos) return ipv6_groups(input, true) == 8;
    const auto left = ipv6_groups(input.substr(0, compressed), false);
    const auto right = ipv6_groups(input.substr(compressed + 2), true);
    return left >= 0 && right >= 0 && left + right < 8;
}

bool valid_authority(string_view input) {
    if (take_char(input, '[')) {
        if (input.find(']') == string_view::npos || !valid_ip_literal(take_until(input, ']'))) return false;
        if (input.empty()) return true;
        return take_char(input, ':') && std::ranges::all_of(input, digit);
    }
    const auto host = take_until(input, ':');
    return !host.empty() && uri_chars(host, "") && std::ranges::all_of(input, digit);
}

bool parse_target(string_view method, string_view input, string_view& absolute_authority) {
    if (input == "*") return method == "OPTIONS";
    if (input.starts_with('/')) return uri_chars(input, ":@/?");

    const auto scheme = take_until(input, ':');
    if ((!ascii_equal(scheme, "http") && !ascii_equal(scheme, "https")) || !input.starts_with("//")) return false;
    input.remove_prefix(2);
    const auto authority = input.substr(0, input.find_first_of("/?"));
    input.remove_prefix(authority.size());
    if (!valid_authority(authority) || !uri_chars(input, ":@/?")) return false;
    absolute_authority = authority;
    return true;
}

bool known_method(string_view method) {
    // HEAD is derived from GET and CORS middleware handles OPTIONS.
    constexpr string_view methods[] = {"GET", "HEAD", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"};
    return std::ranges::find(methods, method) != std::end(methods);
}

bool parse_lengths(string_view input, std::optional<string_view>& canonical) {
    do {
        const bool has_more = input.find(',') != string_view::npos;
        auto item = trim_ows(take_until(input, ','));
        if (item.empty() || !std::ranges::all_of(item, digit)) return false;
        item.remove_prefix(std::min(item.find_first_not_of('0'), item.size() - 1));
        if (canonical && *canonical != item) return false;
        canonical = item;
        if (!has_more) return true;
    } while (!input.empty());
    return false;
}

bool append_digit(std::uint64_t& number, unsigned next, unsigned base, std::uint64_t limit) {
    if (number > limit / base || (number == limit / base && next > limit % base)) return false;
    number = number * base + next;
    return true;
}

struct TransferCodings {
    bool chunked = false;
    bool unsupported = false;

    bool read(string_view input) {
        while (!input.empty()) {
            skip_ows(input);
            if (input.empty()) break;
            if (take_char(input, ',')) continue; // RFC 9110 list recipients skip empty members.
            const auto coding = take_token(input);
            if (coding.empty() || chunked) return false;
            chunked = ascii_equal(coding, "chunked");
            unsupported |= !chunked;
            skip_ows(input);
            while (input.starts_with(';')) {
                if (chunked || !take_parameter(input, true)) return false;
                skip_ows(input);
            }
            if (!input.empty() && !take_char(input, ',')) return false;
        }
        return true;
    }
};

bool read_connection(string_view input, bool& close, bool& upgrade) {
    while (!input.empty()) {
        const auto item = trim_ows(take_until(input, ','));
        if (!std::ranges::all_of(item, token_char)) return false;
        close |= ascii_equal(item, "close");
        upgrade |= ascii_equal(item, "upgrade");
    }
    return true;
}

} // namespace

RequestDecoder::RequestDecoder(RequestLimits limits) : limits_(limits) {}

int RequestDecoder::parse_request_line() {
    string_view rest = line_;
    const auto method = take_until(rest, ' ');
    const auto target = take_until(rest, ' ');
    if (method.empty() || !std::ranges::all_of(method, token_char) || target.empty()) return 400;
    if (rest != "HTTP/1.1") {
        const bool version = rest.size() == 8 && rest.starts_with("HTTP/") &&
                             digit(rest[5]) && rest[6] == '.' && digit(rest[7]);
        return version ? 505 : 400;
    }
    head_.method = method;
    head_.target = target;
    state_ = State::Headers;
    return 0;
}

int RequestDecoder::parse_header() {
    if (head_.headers.size() == max_header_fields) return 431;
    const auto field = parse_field(line_);
    if (!field) return 400;
    Header header{std::string(field->name), std::string(field->value)};
    std::ranges::transform(header.name, header.name.begin(), ascii_lower);
    head_.headers.push_back(std::move(header));
    return 0;
}

int RequestDecoder::validate_head() {
    std::size_t hosts = 0;
    bool has_length = false;
    bool has_coding = false;
    for (const auto& header : head_.headers) {
        if (header.name == "host") {
            if (++hosts > 1 || !valid_authority(header.value)) return 400;
        }
        has_length |= header.name == "content-length";
        has_coding |= header.name == "transfer-encoding";
    }
    if (hosts != 1 || (has_length && has_coding)) return 400;
    if (!known_method(head_.method)) return 501;
    string_view absolute_authority;
    if (!parse_target(head_.method, head_.target, absolute_authority)) return 400;

    std::optional<string_view> canonical_length;
    TransferCodings codings;
    bool upgrade = false;
    bool expect_continue = false;
    bool unsupported_expectation = false;
    for (const auto& header : head_.headers) {
        if (header.name == "content-length") {
            if (!parse_lengths(header.value, canonical_length)) return 400;
        } else if (header.name == "transfer-encoding") {
            if (!codings.read(header.value)) return 400;
        } else if (header.name == "connection") {
            if (!read_connection(header.value, head_.connection_close, upgrade)) return 400;
        } else if (header.name == "upgrade") {
            upgrade = true;
        } else if (header.name == "expect") {
            string_view expectations = header.value;
            while (!expectations.empty()) {
                const auto item = trim_ows(take_until(expectations, ','));
                if (item.empty()) continue;
                const bool supported = ascii_equal(item, "100-continue");
                expect_continue |= supported;
                unsupported_expectation |= !supported;
            }
        }
    }

    // Backend030 gives valid unsupported transfer codings 501. A repeated or
    // non-final chunked coding has already failed framing validation with 400.
    if (upgrade || codings.unsupported) return 501;
    if (has_coding && !codings.chunked) return 400;
    if (canonical_length) {
        for (char ch : *canonical_length) {
            if (!append_digit(remaining_, static_cast<unsigned>(ch - '0'), 10, limits_.body_bytes)) return 413;
        }
        head_.framing = BodyFraming::ContentLength;
        head_.content_length = remaining_;
    }
    if (unsupported_expectation) return 417;

    if (canonical_length) {
        // RFC 9110 permits accepting identical lengths (including list form).
        // Preserve the first field's position and expose one normalized decimal.
        const auto is_length = [](const Header& header) { return header.name == "content-length"; };
        auto first = std::ranges::find_if(head_.headers, is_length);
        first->value = std::string(*canonical_length);
        head_.headers.erase(std::remove_if(first + 1, head_.headers.end(), is_length), head_.headers.end());
    }
    if (!absolute_authority.empty()) {
        // RFC 9112 uses the target authority for absolute-form requests.
        for (auto& header : head_.headers) {
            if (header.name == "host") header.value = absolute_authority;
        }
    }

    if (codings.chunked) {
        head_.framing = BodyFraming::Chunked;
        state_ = State::ChunkSize;
    } else {
        state_ = remaining_ != 0 ? State::FixedBody : State::Complete;
    }
    head_.expect_continue = expect_continue && state_ != State::Complete;
    return 0;
}

int RequestDecoder::parse_chunk_size() {
    string_view extension = line_;
    const auto size_end = std::ranges::find_if(extension, [](char ch) { return hex_digit(ch) < 0; });
    const auto size = extension.substr(0, static_cast<std::size_t>(size_end - extension.begin()));
    if (size.empty()) return 400;
    extension.remove_prefix(size.size());
    // Bound chunk-size lines and the aggregate extensions separately from
    // decoded data. Ordinary chunk delimiters do not consume the body budget.
    if (extension.size() > limits_.head_bytes - chunk_ext_bytes_) return 413;
    chunk_ext_bytes_ += extension.size();
    while (!extension.empty()) {
        if (!take_parameter(extension, false)) return 400;
    }
    for (char ch : size) {
        if (!append_digit(remaining_, static_cast<unsigned>(hex_digit(ch)), 16, limits_.body_bytes - decoded_bytes_)) {
            return 413;
        }
    }
    state_ = remaining_ == 0 ? State::Trailers : State::ChunkBody;
    return 0;
}

int RequestDecoder::parse_trailer() {
    if (line_.empty()) {
        state_ = State::Complete;
        return 0;
    }
    if (++trailer_fields_ > max_header_fields) return 413;
    // RFC 9110 permits discarding trailers. Validate the wire syntax without
    // merging anything into the head or acting on trailer field semantics.
    return parse_field(line_) ? 0 : 400;
}

ReadResult RequestDecoder::reject(int status, std::size_t consumed) {
    state_ = State::Rejected;
    status_ = status;
    return {ReadEvent::Rejected, consumed, status, {}};
}

ReadResult RequestDecoder::consume(string_view input) {
    std::size_t consumed = 0;
    for (;;) {
        switch (state_) {
        case State::Complete: return {ReadEvent::Complete, consumed, 0, {}};
        case State::Rejected: return {ReadEvent::Rejected, consumed, status_, {}};
        case State::Closed: return {ReadEvent::Closed, consumed, 0, {}};
        case State::Incomplete: return {ReadEvent::Incomplete, consumed, 0, {}};
        default: break;
        }
        if (consumed == input.size()) return {ReadEvent::NeedInput, consumed, 0, {}};

        if (state_ == State::FixedBody || state_ == State::ChunkBody) {
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, input.size() - consumed));
            remaining_ -= count;
            if (state_ == State::ChunkBody) {
                decoded_bytes_ += count;
                if (remaining_ == 0) state_ = State::ChunkEndCR;
            } else if (remaining_ == 0) {
                state_ = State::Complete;
            }
            return {ReadEvent::BodyData, consumed + count, 0, input.substr(consumed, count)};
        }

        const char ch = input[consumed++];
        if (state_ == State::ChunkEndCR || state_ == State::ChunkEndLF) {
            if (state_ == State::ChunkEndCR && ch == '\r') {
                state_ = State::ChunkEndLF;
            } else if (state_ == State::ChunkEndLF && ch == '\n') {
                state_ = State::ChunkSize;
                chunk_line_bytes_ = 0;
            } else {
                return reject(400, consumed);
            }
            continue;
        }

        if (state_ == State::RequestLine || state_ == State::Headers) {
            if (head_bytes_ == limits_.head_bytes) return reject(431, consumed);
            ++head_bytes_;
        } else if (state_ == State::ChunkSize) {
            if (chunk_line_bytes_ == limits_.head_bytes) return reject(413, consumed);
            ++chunk_line_bytes_;
        } else if (state_ == State::Trailers) {
            if (trailer_bytes_ == limits_.head_bytes) return reject(413, consumed);
            ++trailer_bytes_;
        }

        if (!saw_cr_) {
            if (ch == '\n') return reject(400, consumed);
            saw_cr_ = ch == '\r';
            if (!saw_cr_) line_ += ch;
            continue;
        }
        if (ch != '\n') return reject(400, consumed);
        saw_cr_ = false;

        int status = 0;
        switch (state_) {
        case State::RequestLine:
            status = parse_request_line();
            break;
        case State::Headers:
            if (line_.empty()) {
                status = validate_head();
                if (status == 0) return {ReadEvent::HeadReady, consumed, 0, {}};
            } else {
                status = parse_header();
            }
            break;
        case State::ChunkSize:
            status = parse_chunk_size();
            break;
        case State::Trailers:
            status = parse_trailer();
            break;
        default: break;
        }
        if (status != 0) return reject(status, consumed);
        line_.clear();
    }
}

int check_request_head(RequestHead& head, const RequestLimits& limits) {
    if (head.headers.size() > max_header_fields) return 431;
    std::size_t hosts = 0;
    for (const auto& header : head.headers) {
        if (header.name == "host" && (++hosts > 1 || !valid_authority(header.value))) return 400;
    }
    if (hosts != 1) return 400;
    if (!known_method(head.method)) return 501;
    // HTTP/2 carries the target in :path, which is origin-form or "*".
    string_view absolute_authority;
    if (head.target.empty() || (head.target != "*" && !head.target.starts_with('/')) ||
        !parse_target(head.method, head.target, absolute_authority)) {
        return 400;
    }
    std::optional<string_view> canonical_length;
    bool unsupported_expectation = false;
    head.expect_continue = false;
    for (const auto& header : head.headers) {
        if (header.name == "content-length") {
            if (!parse_lengths(header.value, canonical_length)) return 400;
        } else if (header.name == "expect") {
            string_view expectations = header.value;
            while (!expectations.empty()) {
                const auto item = trim_ows(take_until(expectations, ','));
                if (item.empty()) continue;
                const bool supported = ascii_equal(item, "100-continue");
                head.expect_continue |= supported;
                unsupported_expectation |= !supported;
            }
        }
    }
    if (canonical_length) {
        std::uint64_t length = 0;
        for (char ch : *canonical_length) {
            if (!append_digit(length, static_cast<unsigned>(ch - '0'), 10, limits.body_bytes)) return 413;
        }
        head.framing = BodyFraming::ContentLength;
        head.content_length = length;
    }
    if (unsupported_expectation) return 417;
    if (canonical_length) {
        const std::string value(*canonical_length);
        const auto is_length = [](const Header& header) { return header.name == "content-length"; };
        auto first = std::ranges::find_if(head.headers, is_length);
        first->value = value;
        head.headers.erase(std::remove_if(first + 1, head.headers.end(), is_length), head.headers.end());
    }
    return 0;
}

ReadResult RequestDecoder::finish_input() {
    if (state_ == State::RequestLine && head_bytes_ == 0) {
        state_ = State::Closed;
    } else if (consume({}).event == ReadEvent::NeedInput) {
        state_ = State::Incomplete;
    }
    return consume({});
}

} // namespace neura::server::http
