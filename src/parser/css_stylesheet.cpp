#include "css_stylesheet.hpp"

#include "island_positions.hpp"
#include "util/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace neura {
namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }
bool line_end(char c) { return c == '\n' || c == '\r' || c == '\f'; }
bool space(char c) { return line_end(c) || c == ' ' || c == '\t'; }
bool name_start(char c) {
    const auto folded = lower(c);
    return (folded >= 'a' && folded <= 'z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
}
bool name_char(char c) { return name_start(c) || digit(c) || c == '-'; }

int hex_digit(char c) {
    if (digit(c)) return c - '0';
    c = lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

std::string folded(std::string_view name) {
    std::string result(name);
    std::ranges::transform(result, result.begin(), lower);
    return result;
}

void utf8(std::string& text, unsigned code) {
    if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) code = 0xfffd;
    if (code < 0x80) {
        text += static_cast<char>(code);
        return;
    }
    const int bits = code < 0x800 ? 6 : code < 0x10000 ? 12 : 18;
    const unsigned prefix = code < 0x800 ? 0xc0 : code < 0x10000 ? 0xe0 : 0xf0;
    text += static_cast<char>(prefix | (code >> bits));
    for (int shift = bits - 6; shift >= 0; shift -= 6) {
        text += static_cast<char>(0x80 | ((code >> shift) & 0x3f));
    }
}

enum class Kind { Whitespace, Comment, Ident, At, Hash, String, Url, Number, Delim };
constexpr auto no_match = std::numeric_limits<std::size_t>::max();

struct CssToken {
    Kind kind;
    std::size_t begin, end;
    std::string name{};
    char delimiter{};
    std::size_t match = no_match;
};

char close_for(char c) {
    switch (c) {
    case '(': return ')';
    case '[': return ']';
    case '{': return '}';
    default: return '\0';
    }
}

class CssTokenizer {
public:
    CssTokenizer(std::string_view source, const IslandPositions& positions) : source_(source), positions_(positions) {}

    std::vector<CssToken> tokenize() {
        std::vector<CssToken> result;
        std::vector<std::size_t> open;
        while (!done()) {
            auto token = next();
            if (token.kind == Kind::Delim) {
                const auto c = token.delimiter;
                if (close_for(c)) {
                    open.push_back(result.size());
                } else if (c == ')' || c == ']' || c == '}') {
                    if (open.empty() || close_for(result[open.back()].delimiter) != c) {
                        fail(token.begin, token.end, "this closing delimiter does not match a stylesheet opener");
                    }
                    result[open.back()].match = result.size();
                    open.pop_back();
                }
            }
            result.push_back(std::move(token));
        }
        if (!open.empty()) {
            const auto& token = result[open.back()];
            fail(token.begin, token.end, std::string("close this delimiter with '") + close_for(token.delimiter) + "'");
        }
        return result;
    }

private:
    bool done() const { return offset_ == source_.size(); }
    char peek(std::size_t ahead = 0) const {
        return ahead < source_.size() - offset_ ? source_[offset_ + ahead] : '\0';
    }

    [[noreturn]] void fail(std::size_t begin, std::size_t end, std::string reason) const {
        throw Diagnostic(positions_.span(begin, end), "malformed stylesheet").with_code("E0253")
            .label(std::move(reason));
    }

    bool escape_at(std::size_t ahead = 0) const {
        return peek(ahead) == '\\' && ahead + 1 < source_.size() - offset_ && !line_end(peek(ahead + 1));
    }

    bool starts_ident(std::size_t ahead = 0) const {
        const auto first = peek(ahead);
        if (name_start(first) || escape_at(ahead)) return true;
        return first == '-' && (name_start(peek(ahead + 1)) || peek(ahead + 1) == '-' || escape_at(ahead + 1));
    }

    bool starts_number() const {
        const std::size_t sign = peek() == '+' || peek() == '-' ? 1 : 0;
        return digit(peek(sign)) || (peek(sign) == '.' && digit(peek(sign + 1)));
    }

    void whitespace() {
        while (space(peek())) ++offset_;
    }

    void one_space() {
        if (peek() == '\r' && peek(1) == '\n') ++offset_;
        ++offset_;
    }

    void escape(std::string* decoded = nullptr) {
        ++offset_; // backslash
        if (hex_digit(peek()) >= 0) {
            unsigned code = 0;
            for (unsigned count = 0; count < 6; ++count) {
                const int part = hex_digit(peek());
                if (part < 0) break;
                code = (code << 4) | static_cast<unsigned>(part);
                ++offset_;
            }
            if (space(peek())) one_space();
            if (decoded) utf8(*decoded, code);
        } else {
            if (decoded) *decoded += peek();
            ++offset_;
        }
    }

    std::string name() {
        std::string result;
        while (name_char(peek()) || escape_at()) {
            if (escape_at()) escape(&result);
            else result += source_[offset_++];
        }
        return result;
    }

    void string() {
        const auto start = offset_;
        const char quote = source_[offset_++];
        while (!done() && !line_end(peek())) {
            if (peek() == quote) {
                ++offset_;
                return;
            }
            if (peek() == '\\') {
                if (escape_at()) escape();
                else if (line_end(peek(1))) {
                    ++offset_;
                    one_space();
                } else break;
            } else ++offset_;
        }
        fail(start, start + 1, "this CSS string has no closing quote");
    }

    void url(std::size_t start) {
        const auto opening = offset_;
        while (!done()) {
            if (peek() == ')') {
                ++offset_;
                return;
            }
            if (escape_at()) escape();
            else ++offset_;
        }
        fail(start, opening, "this url( has no closing ')'");
    }

    void number() {
        if (peek() == '+' || peek() == '-') ++offset_;
        while (digit(peek())) ++offset_;
        if (peek() == '.' && digit(peek(1))) {
            ++offset_;
            while (digit(peek())) ++offset_;
        }
        if (lower(peek()) == 'e') {
            const std::size_t sign = peek(1) == '+' || peek(1) == '-' ? 1 : 0;
            if (digit(peek(1 + sign))) {
                offset_ += 1 + sign;
                while (digit(peek())) ++offset_;
            }
        }
    }

    CssToken next() {
        const auto begin = offset_;
        const auto c = peek();
        if (space(c)) {
            whitespace();
            return {Kind::Whitespace, begin, offset_};
        }
        if (c == '/' && peek(1) == '*') {
            const auto end = source_.find("*/", offset_ + 2);
            if (end == std::string_view::npos) fail(begin, begin + 2, "this CSS comment has no closing '*/'");
            offset_ = end + 2;
            return {Kind::Comment, begin, offset_};
        }
        if (c == '"' || c == '\'') {
            string();
            return {Kind::String, begin, offset_};
        }
        if (c == '@' && starts_ident(1)) {
            ++offset_;
            auto spelling = name();
            return {Kind::At, begin, offset_, std::move(spelling)};
        }
        if (c == '#' && (name_char(peek(1)) || escape_at(1))) {
            ++offset_;
            name();
            return {Kind::Hash, begin, offset_};
        }
        if (starts_number()) {
            number();
            std::string unit;
            if (starts_ident()) unit = name();
            else if (peek() == '%') {
                ++offset_;
                unit = "%";
            }
            return {Kind::Number, begin, offset_, std::move(unit)};
        }
        if (starts_ident()) {
            auto spelling = name();
            if (peek() == '(' && folded(spelling) == "url") {
                const auto paren = offset_++;
                whitespace();
                if (peek() != '"' && peek() != '\'') {
                    url(begin);
                    return {Kind::Url, begin, offset_};
                }
                offset_ = paren;
            }
            return {Kind::Ident, begin, offset_, std::move(spelling)};
        }
        return {Kind::Delim, begin, ++offset_, {}, c};
    }

    std::string_view source_;
    const IslandPositions& positions_;
    std::size_t offset_{};
};

bool trivia(const CssToken& token) { return token.kind == Kind::Whitespace || token.kind == Kind::Comment; }

bool one_of(std::string_view value, std::initializer_list<std::string_view> names) {
    return std::ranges::find(names, value) != names.end();
}

// Animation keywords are taken by their other fields first. For example,
// in '1s linear linear' only the second 'linear' is an animation name.
enum AnimationField : unsigned { Timing = 1, Iteration = 2, Direction = 4, Fill = 8, Play = 16 };

unsigned animation_keyword(std::string_view name) {
    if (one_of(name, {"ease", "linear", "ease-in", "ease-out", "ease-in-out", "step-start", "step-end"})) return Timing;
    if (name == "infinite") return Iteration;
    if (one_of(name, {"normal", "reverse", "alternate", "alternate-reverse"})) return Direction;
    if (one_of(name, {"none", "forwards", "backwards", "both"})) return Fill;
    if (one_of(name, {"running", "paused"})) return Play;
    return 0;
}

bool reserved_animation_name(std::string_view name) {
    return one_of(folded(name), {"none", "initial", "inherit", "unset", "revert", "revert-layer", "default"});
}

class ModuleParser {
public:
    ModuleParser(const std::vector<CssToken>& tokens, const IslandPositions& positions, StylesheetBody& result)
        : tokens_(tokens), positions_(positions), result_(result) {}

    void parse() {
        enum class Mode { Rules, Style, Keyframes };
        struct Block { std::size_t next, end; Mode mode; };
        std::vector<Block> blocks{{0, tokens_.size(), Mode::Rules}};
        while (!blocks.empty()) {
            auto& block = blocks.back();
            const auto first = skip(block.next, block.end);
            if (first == block.end) {
                blocks.pop_back();
                continue;
            }
            const bool at_rule = tokens_[first].kind == Kind::At;
            const auto colon = tokens_[first].kind == Kind::Ident ? skip(first + 1, block.end) : block.end;
            const bool colon_after_name = colon < block.end && delim(colon, ':');
            const bool property = block.mode == Mode::Style && colon_after_name;
            const bool custom = colon_after_name && tokens_[first].name.starts_with("--");
            auto last = boundary(first, block.end, !custom);
            const auto value = property ? skip(colon + 1, block.end) : block.end;
            // A brace as the entire component value of a declaration is
            // opaque, as are all blocks in a custom property.
            if (property && last == value && last < block.end && delim(last, '{')) {
                const auto after = skip(next(last), block.end);
                if (after == block.end || delim(after, ';')) last = after;
            }
            if (last == block.end || delim(last, ';')) {
                if (property && !custom) declaration(first, colon + 1, last);
                block.next = last == block.end ? last : last + 1;
                continue;
            }

            const auto close = tokens_[last].match;
            auto mode = Mode::Style;
            if (at_rule) {
                auto rule = folded(tokens_[first].name);
                if (rule == "keyframes") {
                    const auto name = skip(first + 1, last);
                    if (name < last && tokens_[name].kind == Kind::Ident && skip(name + 1, last) == last &&
                        !reserved_animation_name(tokens_[name].name)) {
                        add(CssName::Kind::Keyframes, name);
                    }
                    mode = Mode::Keyframes;
                } else {
                    if (rule == "scope") selector(first + 1, last);
                    if (rule == "supports") supports_selectors(first + 1, last);
                    mode = one_of(rule, {"font-face", "font-feature-values", "font-palette-values", "property",
                                         "counter-style", "page", "position-try"}) ? Mode::Style : block.mode;
                }
            } else if (block.mode != Mode::Keyframes) {
                selector(first, last);
            }
            block.next = close + 1;
            blocks.push_back({last + 1, close, mode});
        }
    }

private:
    bool delim(std::size_t index, char c) const {
        return tokens_[index].kind == Kind::Delim && tokens_[index].delimiter == c;
    }

    std::size_t skip(std::size_t index, std::size_t end) const {
        while (index < end && trivia(tokens_[index])) ++index;
        return index;
    }

    std::size_t skip_comments(std::size_t index, std::size_t end) const {
        while (index < end && tokens_[index].kind == Kind::Comment) ++index;
        return index;
    }

    std::size_t next(std::size_t index) const {
        return tokens_[index].match == no_match ? index + 1 : tokens_[index].match + 1;
    }

    std::size_t boundary(std::size_t start, std::size_t end, bool stop_at_block) const {
        for (auto i = start; i < end; i = next(i)) {
            if (delim(i, ';') || (stop_at_block && delim(i, '{'))) return i;
        }
        return end;
    }

    bool function(std::size_t index, std::size_t end) const {
        return index + 1 < end && tokens_[index].kind == Kind::Ident && delim(index + 1, '(') &&
               tokens_[index].end == tokens_[index + 1].begin;
    }

    void add(CssName::Kind kind, std::size_t index) {
        const auto& token = tokens_[index];
        result_.names.push_back({kind, token.name, positions_.span(token.begin, token.end)});
    }

    void marker(std::size_t start, std::size_t end) {
        result_.global_markers.push_back(positions_.span(tokens_[start].begin, tokens_[end].end));
    }

    [[noreturn]] void malformed(std::size_t colon, std::size_t name, std::string reason) const {
        throw Diagnostic(positions_.span(tokens_[colon].begin, tokens_[name].end),
                         "malformed CSS module construct").with_code("E0256")
            .label(std::move(reason)).help("write ':global(.name)' to keep a selector's global name");
    }

    void selector(std::size_t start, std::size_t end) {
        for (auto i = start; i < end;) {
            if (delim(i, '[')) {
                i = next(i);
                continue;
            }
            if (function(i, end) && folded(tokens_[i].name) == "url") {
                i = next(i + 1);
                continue;
            }
            if (!delim(i, '.') && !delim(i, ':')) {
                ++i;
                continue;
            }
            const auto name = skip_comments(i + 1, end);
            if (name < end && tokens_[name].kind == Kind::Ident) {
                if (delim(i, '.')) {
                    add(CssName::Kind::Class, name);
                } else if (i == start || !delim(i - 1, ':') || tokens_[i - 1].end != tokens_[i].begin) {
                    const auto pseudo = folded(tokens_[name].name);
                    if (pseudo == "local") malformed(i, name, "CSS modules already make class names local");
                    if (pseudo == "global") {
                        if (!function(name, end)) malformed(i, name, "':global' needs a parenthesized selector");
                        const auto close = tokens_[name + 1].match;
                        marker(i, i);
                        marker(name, name + 1);
                        marker(close, close);
                        i = close + 1;
                        continue;
                    }
                }
            }
            ++i;
        }
    }

    void supports_selectors(std::size_t start, std::size_t end) {
        for (auto i = start; i < end; ++i) {
            if (function(i, end) && folded(tokens_[i].name) == "selector") {
                const auto close = tokens_[i + 1].match;
                selector(i + 2, close);
                i = close;
            }
        }
    }

    void declaration(std::size_t property, std::size_t start, std::size_t end) {
        const auto name = folded(tokens_[property].name);
        if (name != "animation" && name != "animation-name") return;
        // !important is outside the animation value.
        auto tail = end;
        while (tail > start && trivia(tokens_[tail - 1])) --tail;
        if (tail > start && tokens_[tail - 1].kind == Kind::Ident && folded(tokens_[tail - 1].name) == "important") {
            --tail;
            while (tail > start && trivia(tokens_[tail - 1])) --tail;
            if (tail > start && delim(tail - 1, '!')) end = tail - 1;
        }
        while (start < end) {
            auto last = start;
            while (last < end && !delim(last, ',')) last = next(last);
            const auto first = skip(start, last);
            if (name == "animation-name") {
                if (first < last && tokens_[first].kind == Kind::Ident && skip(first + 1, last) == last &&
                    !reserved_animation_name(tokens_[first].name)) {
                    add(CssName::Kind::Animation, first);
                }
            } else {
                shorthand(first, last);
            }
            if (last == end) break;
            start = last + 1;
        }
    }

    void shorthand(std::size_t start, std::size_t end) {
        unsigned fields = 0;
        auto name = no_match;
        bool has_name = false;
        for (auto i = start; i < end; i = skip(next(i), end)) {
            const auto& token = tokens_[i];
            if (function(i, end)) {
                const auto word = folded(token.name);
                if (one_of(word, {"linear", "steps", "cubic-bezier"})) fields |= Timing;
                ++i; // the iteration skips the arguments
                continue;
            }
            if (token.kind == Kind::Number && token.name.empty()) fields |= Iteration;
            if (token.kind == Kind::Ident) {
                const auto category = animation_keyword(folded(token.name));
                if (category && !(fields & category)) {
                    fields |= category;
                    continue;
                }
                if (reserved_animation_name(token.name)) continue;
                if (has_name) return; // structurally valid, but no unique name in this value
                name = i;
                has_name = true;
            } else if (token.kind == Kind::String) {
                if (has_name) return;
                has_name = true;
            }
        }
        if (name != no_match) add(CssName::Kind::Animation, name);
    }

    const std::vector<CssToken>& tokens_;
    const IslandPositions& positions_;
    StylesheetBody& result_;
};

// Decode the CSS escapes of a string body or an unquoted URL. An escaped
// line break inside a string continues it and contributes nothing.
std::string decode_css(std::string_view raw) {
    std::string result;
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] != '\\' || i + 1 == raw.size()) {
            result += raw[i++];
            continue;
        }
        ++i;
        if (line_end(raw[i])) {
            i += raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n' ? 2 : 1;
            continue;
        }
        if (hex_digit(raw[i]) < 0) {
            result += raw[i++];
            continue;
        }
        unsigned code = 0;
        for (unsigned count = 0; count < 6 && i < raw.size() && hex_digit(raw[i]) >= 0; ++count, ++i)
            code = (code << 4) | static_cast<unsigned>(hex_digit(raw[i]));
        if (i < raw.size() && space(raw[i])) i += raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n' ? 2 : 1;
        utf8(result, code);
    }
    return result;
}

void collect_references(std::string_view source, const std::vector<CssToken>& tokens,
                        const IslandPositions& positions, StylesheetBody& result) {
    const auto skip = [&](std::size_t index) {
        while (index < tokens.size() && trivia(tokens[index])) ++index;
        return index;
    };
    const auto is = [&](std::size_t index, char c) {
        return index < tokens.size() && tokens[index].kind == Kind::Delim && tokens[index].delimiter == c;
    };
    // The reference at `index`, or tokens.size() when there is none.
    const auto reference = [&](std::size_t index, CssReference::Kind kind, bool string_allowed) {
        if (index >= tokens.size()) return false;
        const auto& token = tokens[index];
        if (token.kind == Kind::Url) {
            auto inner = source.substr(token.begin, token.end - token.begin);
            inner = inner.substr(inner.find('(') + 1);
            inner.remove_suffix(1);
            while (!inner.empty() && space(inner.front())) inner.remove_prefix(1);
            while (!inner.empty() && space(inner.back())) inner.remove_suffix(1);
            result.references.push_back({kind, decode_css(inner), positions.span(token.begin, token.end), false});
            return true;
        }
        if (token.kind == Kind::Ident && folded(token.name) == "url" && is(index + 1, '(') &&
            tokens[index + 1].begin == token.end) {
            const auto argument = skip(index + 2);
            if (argument < tokens.size() && tokens[argument].kind == Kind::String) {
                const auto& text = tokens[argument];
                const auto body = source.substr(text.begin + 1, text.end - text.begin - 2);
                result.references.push_back({kind, decode_css(body), positions.span(text.begin, text.end), true});
                return true;
            }
            return false;
        }
        if (string_allowed && token.kind == Kind::String) {
            const auto body = source.substr(token.begin + 1, token.end - token.begin - 2);
            result.references.push_back({kind, decode_css(body), positions.span(token.begin, token.end), true});
            return true;
        }
        return false;
    };
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (token.kind == Kind::At && folded(token.name) == "namespace") {
            // A namespace URL names no resource.
            while (i < tokens.size() && !is(i, ';') && !is(i, '{')) ++i;
            continue;
        }
        if (token.kind == Kind::At && folded(token.name) == "import") {
            const auto target = skip(i + 1);
            if (reference(target, CssReference::Kind::Import, true))
                i = tokens[target].kind == Kind::Ident ? tokens[target + 1].match : target;
            continue;
        }
        if (token.kind == Kind::Url || (token.kind == Kind::Ident && folded(token.name) == "url")) {
            if (reference(i, CssReference::Kind::Url, false) && token.kind == Kind::Ident) i = tokens[i + 1].match;
            continue;
        }
        // In image-set(), a string that begins an option names an image as url() would. A string elsewhere in
        // the option, such as the MIME type in type("image/avif"), is not a reference.
        const bool image_set = token.kind == Kind::Ident &&
                               (folded(token.name) == "image-set" || folded(token.name) == "-webkit-image-set");
        if (image_set && is(i + 1, '(') && tokens[i + 1].begin == token.end && tokens[i + 1].match != no_match) {
            const auto close = tokens[i + 1].match;
            bool option_start = true;
            for (auto j = i + 2; j < close; ++j) {
                const auto& item = tokens[j];
                if (trivia(item)) continue;
                if (is(j, ',')) {
                    option_start = true;
                    continue;
                }
                if (item.kind == Kind::Url || (item.kind == Kind::Ident && folded(item.name) == "url")) {
                    if (reference(j, CssReference::Kind::Url, false) && item.kind == Kind::Ident)
                        j = tokens[j + 1].match;
                } else if (option_start && item.kind == Kind::String) {
                    reference(j, CssReference::Kind::Url, true);
                } else if (item.match != no_match) {
                    j = item.match;
                }
                option_start = false;
            }
            i = close;
        }
    }
}

std::string scoped_name(std::string_view package, std::string_view key, std::string_view name) {
    util::Sha256 hash;
    constexpr std::string_view separator("\0", 1);
    hash.update(package);
    hash.update(separator);
    hash.update(key);
    hash.update(separator);
    hash.update(name);
    const auto bytes = hash.finish();
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz234567";
    auto result = std::string(name) + "__";
    for (unsigned digit = 0; digit < 10; ++digit) {
        const auto bit = digit * 5;
        const auto offset = bit / 8;
        const auto word = (static_cast<unsigned>(bytes[offset]) << 8) | bytes[offset + 1];
        result += alphabet[(word >> (11 - bit % 8)) & 31];
    }
    return result;
}

} // namespace

StylesheetBody parse_stylesheet(std::string_view bytes, Span origin, bool module) {
    const IslandPositions positions(bytes, std::move(origin));
    StylesheetBody result{positions.span(0, bytes.size()), {}, {}};
    const auto tokens = CssTokenizer(bytes, positions).tokenize();
    if (module) ModuleParser(tokens, positions, result).parse();
    collect_references(bytes, tokens, positions, result);
    return result;
}

RewrittenStylesheet rewrite_stylesheet(std::string_view bytes, const StylesheetBody& parsed,
                                       std::string_view package, std::string_view key) {
    return rewrite_stylesheet(bytes, parsed, package, key, {});
}

RewrittenStylesheet rewrite_stylesheet(std::string_view bytes, const StylesheetBody& parsed,
                                       std::string_view package, std::string_view key,
                                       const std::vector<std::string>& targets) {
    if (!targets.empty() && targets.size() != parsed.references.size())
        throw std::invalid_argument("stylesheet reference targets do not match the analysis");
    if (bytes.size() != parsed.span.length) throw std::invalid_argument("stylesheet bytes and analysis do not match");
    RewrittenStylesheet result;
    for (const auto& entry : parsed.names) {
        if (entry.kind == CssName::Kind::Animation) continue;
        auto& names = entry.kind == CssName::Kind::Class ? result.classes : result.keyframes;
        if (!names.contains(entry.name)) names.emplace(entry.name, scoped_name(package, key, entry.name));
    }
    struct Edit { std::size_t begin, end; std::string_view replacement; };
    std::vector<Edit> edits;
    std::vector<std::string> replaced;
    replaced.reserve(targets.size());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].empty()) continue;
        const auto& reference = parsed.references[i];
        std::string escaped;
        for (const char c : targets[i]) {
            const bool unsafe = c == '"' || c == '\'' || c == '\\' || c == '(' || c == ')' ||
                                static_cast<unsigned char>(c) <= 32 || c == 127;
            if (unsafe && (!reference.quoted || c == '"' || c == '\\' || static_cast<unsigned char>(c) < 32)) {
                constexpr std::string_view hex = "0123456789abcdef";
                escaped += '\\';
                if (static_cast<unsigned char>(c) >= 16) escaped += hex[static_cast<unsigned char>(c) >> 4];
                escaped += hex[static_cast<unsigned char>(c) & 15];
                escaped += ' ';
            } else {
                escaped += c;
            }
        }
        replaced.push_back(reference.quoted ? '"' + escaped + '"' : "url(" + escaped + ")");
        const auto begin = reference.span.offset - parsed.span.offset;
        edits.push_back({begin, begin + reference.span.length, replaced.back()});
    }
    for (const auto& marker : parsed.global_markers) {
        const auto begin = marker.offset - parsed.span.offset;
        edits.push_back({begin, begin + marker.length, {}});
    }
    for (const auto& entry : parsed.names) {
        const auto& names = entry.kind == CssName::Kind::Class ? result.classes : result.keyframes;
        if (const auto renamed = names.find(entry.name); renamed != names.end()) {
            const auto end = entry.span.offset - parsed.span.offset + entry.span.length;
            // Keep the original identifier spelling, including CSS escapes.
            edits.push_back({end, end, std::string_view(renamed->second).substr(entry.name.size())});
        }
    }
    std::ranges::sort(edits, [](const Edit& left, const Edit& right) {
        return std::pair{left.begin, left.end} < std::pair{right.begin, right.end};
    });
    std::size_t cursor = 0;
    for (const auto& edit : edits) {
        if (edit.begin < cursor || edit.end > bytes.size()) throw std::invalid_argument("invalid stylesheet edit range");
        result.text += bytes.substr(cursor, edit.begin - cursor);
        result.text += edit.replacement;
        cursor = edit.end;
    }
    result.text += bytes.substr(cursor);
    return result;
}

} // namespace neura
