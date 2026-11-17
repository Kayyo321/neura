#include "toml.hpp"

#include <limits>
#include <utility>

namespace neura::project {
namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }

int hex_digit(char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool bare_key(char c) {
    return digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
}

bool horizontal(char c) { return c == ' ' || c == '\t'; }
bool newline_start(char c) { return c == '\n' || c == '\r'; }

bool raw_control(unsigned char c) { return (c < 0x20 && c != '\t') || c == 0x7f; }

bool unicode_scalar(std::uint32_t value) {
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

std::size_t utf8_width(std::string_view bytes) {
    if (bytes.empty()) return 0;
    const auto head = static_cast<unsigned char>(bytes.front());
    if (head < 0x80) return 1;
    std::size_t width;
    std::uint32_t code, minimum;
    if (head >= 0xc2 && head <= 0xdf) {
        width = 2;
        code = head & 0x1f;
        minimum = 0x80;
    } else if (head >= 0xe0 && head <= 0xef) {
        width = 3;
        code = head & 0x0f;
        minimum = 0x800;
    } else if (head >= 0xf0 && head <= 0xf4) {
        width = 4;
        code = head & 0x07;
        minimum = 0x10000;
    } else {
        return 0;
    }
    if (bytes.size() < width) return 0;
    for (std::size_t i = 1; i < width; ++i) {
        const auto tail = static_cast<unsigned char>(bytes[i]);
        if ((tail & 0xc0) != 0x80) return 0;
        code = (code << 6) | (tail & 0x3f);
    }
    return code >= minimum && unicode_scalar(code) ? width : 0;
}

void append_utf8(std::string& text, std::uint32_t code) {
    if (code < 0x80) {
        text.push_back(static_cast<char>(code));
        return;
    }
    const int shift = code < 0x800 ? 6 : code < 0x10000 ? 12 : 18;
    const std::uint32_t prefix = code < 0x800 ? 0xc0 : code < 0x10000 ? 0xe0 : 0xf0;
    text.push_back(static_cast<char>(prefix | (code >> shift)));
    for (int bits = shift - 6; bits >= 0; bits -= 6) {
        text.push_back(static_cast<char>(0x80 | ((code >> bits) & 0x3f)));
    }
}

struct Position {
    std::size_t offset{};
    std::size_t line{1};
    std::size_t column{1};
};

struct Key {
    std::string text;
    Span span;
};

// A header's implicit ancestors can later be defined. A dotted key defines
// its ancestors as it goes. Additional dotted keys may extend those tables.
enum class TableDefinition { Implicit, Dotted, Header };

class TomlReader {
public:
    TomlReader(std::string_view bytes, std::string_view file) : bytes_(bytes), file_(file) {}

    TomlDocument read() {
        auto current = add_table(span(position_), TableDefinition::Header);
        layout();
        while (!at_end()) {
            if (peek() == '[') {
                current = header();
            } else {
                assignment(current);
            }
            finish_line();
            layout();
        }
        return std::move(document_);
    }

private:
    bool at_end() const { return position_.offset == bytes_.size(); }
    std::string_view rest() const { return bytes_.substr(position_.offset); }
    char peek() const { return at_end() ? '\0' : bytes_[position_.offset]; }

    Span span(Position start) const { return span(start, position_.offset - start.offset); }
    Span span(Position start, std::size_t length) const {
        return {std::string(file_), start.line, start.column, start.offset, length};
    }

    [[noreturn]] static void fail(Span at, std::string message) {
        throw Diagnostic(std::move(at), std::move(message)).with_code("E1020");
    }

    [[noreturn]] static void conflict(const Key& key, const TomlEntry& prior, std::string message) {
        throw Diagnostic(key.span, std::move(message)).with_code("E1020")
            .label(prior.key_span, "first defined here");
    }

    std::size_t width() const {
        const auto count = utf8_width(rest());
        if (count == 0 && !at_end()) fail(span(position_, 1), "invalid UTF-8 in the project file");
        return count;
    }

    [[noreturn]] void unexpected(std::string message) const {
        const auto count = rest().starts_with("\r\n") ? 2 : width();
        fail(span(position_, count), std::move(message));
    }

    // Used for known ASCII grammar characters. Newlines and other text have
    // their own consumers to keep column and byte accounting separate.
    void advance() {
        ++position_.offset;
        ++position_.column;
    }

    bool take(char c) {
        if (at_end() || peek() != c) return false;
        advance();
        return true;
    }

    void expect(char c, std::string message) {
        if (!take(c)) unexpected(std::move(message));
    }

    std::string_view text_character() {
        if (raw_control(static_cast<unsigned char>(peek()))) unexpected("unescaped control character");
        const auto count = width();
        const auto result = rest().substr(0, count);
        position_.offset += count;
        ++position_.column;
        return result;
    }

    void line_break() {
        if (peek() == '\r' && !rest().starts_with("\r\n")) {
            unexpected("expected LF after CR");
        }
        position_.offset += peek() == '\r' ? 2u : 1u;
        ++position_.line;
        position_.column = 1;
    }

    void spaces() {
        while (horizontal(peek())) advance();
    }

    void comment() {
        if (!take('#')) return;
        while (!at_end() && !newline_start(peek())) text_character();
    }

    void layout() {
        for (;;) {
            spaces();
            comment();
            if (!newline_start(peek())) return;
            line_break();
        }
    }

    void finish_line() {
        spaces();
        comment();
        if (at_end()) return;
        if (!newline_start(peek())) unexpected("expected a comment or the end of the line");
        line_break();
    }

    void escape(std::string& result, Position slash) {
        if (at_end() || newline_start(peek())) {
            fail(span(slash), "expected an escape in a basic string");
        }
        const char c = peek();
        if (static_cast<unsigned char>(c) >= 0x80) {
            const auto count = width();
            fail(span(slash, position_.offset - slash.offset + count), "invalid basic string escape");
        }
        advance();
        switch (c) {
        case '"': result.push_back('"'); return;
        case '\\': result.push_back('\\'); return;
        case 'b': result.push_back('\b'); return;
        case 't': result.push_back('\t'); return;
        case 'n': result.push_back('\n'); return;
        case 'f': result.push_back('\f'); return;
        case 'r': result.push_back('\r'); return;
        case 'u':
        case 'U': {
            std::uint32_t code = 0;
            for (int i = 0; i < (c == 'u' ? 4 : 8); ++i) {
                const int value = hex_digit(peek());
                if (value < 0) unexpected("expected a hexadecimal digit in the Unicode escape");
                code = (code << 4) | static_cast<std::uint32_t>(value);
                advance();
            }
            if (!unicode_scalar(code)) fail(span(slash), "the Unicode escape must name a Unicode scalar value");
            append_utf8(result, code);
            return;
        }
        default:
            fail(span(slash), "invalid basic string escape");
        }
    }

    std::string basic_string() {
        if (rest().starts_with("\"\"\"")) fail(span(position_, 3), "multi-line strings are outside the project TOML subset");
        advance(); // opening quote
        std::string result;
        while (!at_end() && !newline_start(peek())) {
            const auto start = position_;
            if (take('"')) return result;
            if (take('\\')) {
                escape(result, start);
            } else {
                result.append(text_character());
            }
        }
        unexpected("expected a closing double quote on the same line");
    }

    Key key() {
        const auto start = position_;
        if (peek() == '"') {
            auto text = basic_string();
            return {std::move(text), span(start)};
        }
        if (!bare_key(peek())) unexpected("expected a bare or double-quoted key");
        while (bare_key(peek())) advance();
        return {std::string(bytes_.substr(start.offset, position_.offset - start.offset)), span(start)};
    }

    std::vector<Key> key_path() {
        std::vector<Key> path;
        do {
            spaces();
            path.push_back(key());
            spaces();
        } while (take('.'));
        return path;
    }

    TomlValue decimal_or_boolean() {
        const auto start = position_;
        while (!at_end() && !horizontal(peek()) && !newline_start(peek()) &&
               peek() != ']' && peek() != ',' && peek() != '#') {
            text_character();
        }
        if (position_.offset == start.offset) unexpected("expected a value");
        auto token = bytes_.substr(start.offset, position_.offset - start.offset);
        const auto at = span(start);
        if (token == "true") return {true, at};
        if (token == "false") return {false, at};

        const bool negative = token.front() == '-';
        if (negative || token.front() == '+') token.remove_prefix(1);
        const auto invalid = [&] { fail(at, "expected a basic string, decimal integer, or Boolean"); };
        if (token.empty() || !digit(token.front()) || (token.front() == '0' && token.size() != 1)) invalid();
        constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        const std::uint64_t limit = negative ? max + 1 : max;
        std::uint64_t magnitude = 0;
        for (std::size_t i = 0; i < token.size(); ++i) {
            const char c = token[i];
            if (c == '_' && i != 0 && i + 1 < token.size() && digit(token[i - 1]) && digit(token[i + 1])) {
                continue;
            }
            if (!digit(c)) invalid();
            const auto part = static_cast<std::uint64_t>(c - '0');
            if (magnitude > (limit - part) / 10) fail(at, "decimal integer is outside the signed 64-bit range");
            magnitude = magnitude * 10 + part;
        }
        if (negative && magnitude == max + 1) return {std::numeric_limits<std::int64_t>::min(), at};
        const auto number = static_cast<std::int64_t>(magnitude);
        return {negative ? -number : number, at};
    }

    TomlValue scalar() {
        const auto start = position_;
        switch (peek()) {
        case '"': {
            auto text = basic_string();
            return {std::move(text), span(start)};
        }
        case '\'': unexpected("literal strings are outside the project TOML subset");
        case '{': unexpected("inline tables are outside the project TOML subset");
        case '[': unexpected("nested arrays are outside the project TOML subset");
        default: return decimal_or_boolean();
        }
    }

    TomlValue value() {
        const auto start = position_;
        if (!take('[')) return scalar();
        TomlValue::Array items;
        layout();
        while (!take(']')) {
            auto item = scalar();
            if (std::holds_alternative<bool>(item.value)) fail(item.span, "arrays require strings or decimal integers");
            if (!items.empty() && item.value.index() != items.front().value.index()) {
                fail(item.span, "array elements must all have the same type");
            }
            items.push_back(std::move(item));
            layout();
            if (take(']')) break;
            expect(',', "expected ',' or ']' after an array element");
            layout();
        }
        return {std::move(items), span(start)};
    }

    TomlTable& table(TomlTableId id) { return document_.tables[static_cast<std::size_t>(id)]; }
    TableDefinition& definition(TomlTableId id) { return definitions_[static_cast<std::size_t>(id)]; }

    TomlTableId add_table(Span at, TableDefinition kind) {
        const auto id = TomlTableId{document_.tables.size()};
        document_.tables.push_back({{}, std::move(at)});
        definitions_.push_back(kind);
        return id;
    }

    TomlTableId parent(TomlTableId within, const Key& segment, TableDefinition origin) {
        auto found = table(within).entries.find(segment.text);
        if (found == table(within).entries.end()) {
            const auto id = add_table(segment.span, origin);
            table(within).entries.emplace(segment.text, TomlEntry{id, segment.span});
            return id;
        }
        auto& prior = found->second;
        if (const auto* id = std::get_if<TomlTableId>(&prior.item)) {
            if (origin == TableDefinition::Dotted) {
                switch (definition(*id)) {
                case TableDefinition::Header:
                    conflict(segment, prior, "a dotted key cannot extend a table defined by a header");
                case TableDefinition::Implicit:
                    definition(*id) = TableDefinition::Dotted;
                    table(*id).span = segment.span;
                    prior.key_span = segment.span;
                    break;
                case TableDefinition::Dotted:
                    break;
                }
            }
            return *id;
        }
        if (origin == TableDefinition::Implicit) {
            if (const auto* group = std::get_if<TomlTableArray>(&prior.item)) return group->back();
        }
        conflict(segment, prior, "the key conflicts with a previously defined value or table");
    }

    TomlTableId header() {
        const auto start = position_;
        advance(); // opening bracket
        const bool array = take('[');
        const auto path = key_path();
        expect(']', "expected ']' after the table name");
        if (array) expect(']', "expected ']]' after the array-of-tables name");
        const auto at = span(start);
        auto within = TomlTableId{0};
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            within = parent(within, path[i], TableDefinition::Implicit);
        }
        const auto& key = path.back();
        auto found = table(within).entries.find(key.text);
        if (found == table(within).entries.end()) {
            const auto id = add_table(at, TableDefinition::Header);
            if (array) {
                table(within).entries.emplace(key.text, TomlEntry{TomlTableArray{id}, key.span});
            } else {
                table(within).entries.emplace(key.text, TomlEntry{id, key.span});
            }
            return id;
        }
        auto& prior = found->second;
        if (array) {
            if (!std::holds_alternative<TomlTableArray>(prior.item)) {
                conflict(key, prior, "an array of tables conflicts with an earlier definition");
            }
            const auto id = add_table(at, TableDefinition::Header);
            auto& entry = table(within).entries.at(key.text);
            std::get<TomlTableArray>(entry.item).push_back(id);
            return id;
        }
        const auto* id = std::get_if<TomlTableId>(&prior.item);
        if (!id || definition(*id) != TableDefinition::Implicit) {
            conflict(key, prior, "the table conflicts with an earlier definition");
        }
        definition(*id) = TableDefinition::Header;
        table(*id).span = at;
        prior.key_span = key.span;
        return *id;
    }

    void assignment(TomlTableId within) {
        const auto path = key_path();
        expect('=', "expected '=' after the key");
        for (std::size_t i = 0; i + 1 < path.size(); ++i) {
            within = parent(within, path[i], TableDefinition::Dotted);
        }
        const auto& key = path.back();
        auto& entries = table(within).entries;
        if (const auto found = entries.find(key.text); found != entries.end()) {
            conflict(key, found->second, "the key is already defined");
        }
        spaces();
        entries.emplace(key.text, TomlEntry{value(), key.span});
    }

    std::string_view bytes_;
    std::string_view file_;
    Position position_;
    TomlDocument document_;
    std::vector<TableDefinition> definitions_;
};

} // namespace

TomlDocument parse_toml(std::string_view bytes, std::string_view file) {
    return TomlReader(bytes, file).read();
}

} // namespace neura::project
