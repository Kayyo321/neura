#include "island.hpp"

namespace neura::island {
namespace {

bool alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool name_char(char c) {
    return alpha(c) || (c >= '0' && c <= '9') || c == '_';
}

bool starts_form(const Token& token) {
    if (token.kind == TokenKind::Keyword) return token.text == "new" || token.text == "return";
    return token.kind == TokenKind::Symbol &&
           (token.text == "=" || token.text == "(" || token.text == "=>");
}

bool symbol(const std::vector<Token>& tokens, std::size_t index, std::string_view text) {
    return tokens[index].kind == TokenKind::Symbol && tokens[index].text == text;
}

std::optional<std::size_t> matching_open(const std::vector<Token>& tokens, std::size_t index,
                                         std::string_view open, std::string_view close) {
    std::size_t depth = 1;
    while (index > 0) {
        --index;
        if (symbol(tokens, index, close)) ++depth;
        else if (symbol(tokens, index, open) && --depth == 0) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> html_introducer(const std::vector<Token>& tokens, std::size_t index) {
    if (!symbol(tokens, index, ")")) return std::nullopt;
    auto open = matching_open(tokens, index, "(", ")");
    if (!open || *open == 0) return std::nullopt;
    if (symbol(tokens, *open - 1, ">")) {
        open = matching_open(tokens, *open - 1, "<", ">");
        if (!open || *open == 0) return std::nullopt;
    }
    const auto introducer = *open - 1;
    if (tokens[introducer].kind == TokenKind::Identifier && tokens[introducer].text == "html" &&
        (introducer == 0 || !symbol(tokens, introducer - 1, "."))) return introducer;
    return std::nullopt;
}

std::size_t declaration_offset(const std::vector<Token>& tokens, std::size_t introducer) {
    auto index = introducer;
    if (index > 0 && tokens[index - 1].kind == TokenKind::Keyword && tokens[index - 1].text == "new")
        --index;
    if (index > 0 && symbol(tokens, index - 1, "=")) {
        for (--index; index > 0;) {
            const auto& token = tokens[--index];
            if ((token.kind == TokenKind::Keyword && (token.text == "let" || token.text == "const")) ||
                (token.kind == TokenKind::Identifier && token.text == "last")) {
                if (index > 0 && tokens[index - 1].kind == TokenKind::Keyword && tokens[index - 1].text == "export")
                    --index;
                return tokens[index].span.offset;
            }
            if (token.kind == TokenKind::Symbol &&
                (token.text == ";" || token.text == "{" || token.text == "}" || token.text == "=")) break;
        }
    }
    return tokens[introducer].span.offset;
}

class BodyScanner {
public:
    BodyScanner(std::string_view source, std::size_t begin) : source_(source), offset_(begin) {}

    std::optional<std::size_t> embedded_neura(char end) {
        if (!neura_group(end)) return std::nullopt;
        return offset_ - 1;
    }

    std::optional<std::size_t> html() {
        std::vector<std::size_t> controls;
        std::size_t element_depth = 0;
        std::size_t tag = 0;
        bool in_tag = false;
        while (!done()) {
            const char c = peek();
            if (in_tag) {
                if (c == '"' || c == '\'') quoted(c, false);
                else if (c == '{') neura_group('}');
                else {
                    if (c == '}') {
                        if (controls.empty()) return offset_;
                        controls.pop_back();
                        in_tag = false;
                    }
                    if (c == '>') {
                        in_tag = false;
                        if (source_[tag] == '/') {
                            if (element_depth > 0) --element_depth;
                        } else if (alpha(source_[tag]) && source_[offset_ - 1] != '/') {
                            auto end = tag;
                            while (end < offset_ && (name_char(source_[end]) || source_[end] == '-')) ++end;
                            if (!html_void_element(source_.substr(tag, end - tag))) ++element_depth;
                        }
                    }
                    ++offset_;
                }
            } else if (remaining().starts_with("<!--")) {
                offset_ += 4;
                until("-->");
            } else if (c == '<' && (alpha(peek(1)) || peek(1) == '/' || peek(1) == '!' ||
                                     peek(1) == '?' || peek(1) == '{')) {
                in_tag = true;
                tag = ++offset_;
            } else if (c == '{') {
                neura_group('}');
            } else if (c == '}') {
                const auto outer_depth = controls.empty() ? 0 : controls.back();
                if (element_depth > outer_depth && text_continues_after_brace()) {
                    ++offset_;
                    continue;
                }
                if (controls.empty()) return offset_;
                controls.pop_back();
                ++offset_;
            } else if (c == '@' && alpha(peek(1)) && !text_word_char(source_[offset_ - 1])) {
                if (control_head()) controls.push_back(element_depth);
            } else {
                ++offset_;
            }
        }
        return std::nullopt;
    }

    std::optional<std::size_t> css() {
        std::vector<char> closes = {'}'};
        while (!done()) {
            const char c = peek();
            if (remaining().starts_with("/*")) {
                offset_ += 2;
                until("*/");
            } else if (c == '"' || c == '\'') {
                quoted(c, true);
            } else if (css_name_char(c) || c == '\\') {
                const auto name_start = offset_;
                const bool is_url = css_name_is_url();
                if (is_url && peek() == '(' && source_[name_start - 1] != '@' && source_[name_start - 1] != '#') {
                    ++offset_;
                    whitespace();
                    if (peek() == '"' || peek() == '\'') closes.push_back(')');
                    else css_url();
                }
            } else {
                if (c == '(') closes.push_back(')');
                else if (c == '[') closes.push_back(']');
                else if (c == '{') closes.push_back('}');
                else if (c == closes.back()) {
                    closes.pop_back();
                    if (closes.empty()) return offset_;
                }
                ++offset_;
            }
        }
        return std::nullopt;
    }

private:
    // Leave a stray text brace to the template parser when the same element
    // clearly continues. A brace at the declaration boundary still frames an
    // island with an unclosed start tag, so the parser can diagnose that tag.
    bool text_continues_after_brace() const {
        const auto next = source_.find_first_of("<{};", offset_ + 1);
        return next != std::string_view::npos && source_.substr(next).starts_with("</");
    }

    static bool text_word_char(char c) { return name_char(c) || c == '-' || static_cast<unsigned char>(c) >= 0x80; }

    bool done() const { return offset_ >= source_.size(); }
    char peek(std::size_t ahead = 0) const {
        return offset_ + ahead < source_.size() ? source_[offset_ + ahead] : '\0';
    }
    std::string_view remaining() const { return source_.substr(offset_); }

    void until(std::string_view terminator) {
        const auto end = source_.find(terminator, offset_);
        offset_ = end == std::string_view::npos ? source_.size() : end + terminator.size();
    }

    static bool line_end(char c) { return c == '\r' || c == '\n'; }
    static bool css_line_end(char c) { return line_end(c) || c == '\f'; }

    void whitespace() {
        while (peek() == ' ' || peek() == '\t' || css_line_end(peek())) ++offset_;
    }

    void escaped_byte() {
        ++offset_;
        if (!done()) {
            if (peek() == '\r' && peek(1) == '\n') ++offset_;
            ++offset_;
        }
    }

    void quoted(char quote, bool css) {
        ++offset_;
        while (!done()) {
            const char c = peek();
            if (c == quote) {
                ++offset_;
                return;
            }
            if (css && css_line_end(c)) return; // CSS bad-string token
            if (css && c == '\\') escaped_byte();
            else ++offset_;
        }
    }

    // Balance only the Neura delimiters that matter for this boundary. The
    // template parser lexes and parses the expression itself, including any
    // invalid tokens that this scan steps over.
    bool neura_group(char close) {
        std::vector<char> closes = {close};
        ++offset_;
        while (!done() && !closes.empty()) {
            const char c = peek();
            if (closes.back() == '"') {
                if (c == '\\' && !line_end(peek(1))) {
                    escaped_byte();
                    continue;
                }
                if (c == '{') closes.push_back('}');
                else if (c == '"' || line_end(c)) closes.pop_back();
            } else if (remaining().starts_with("//")) {
                while (!done() && !line_end(peek())) ++offset_;
                continue;
            } else if (remaining().starts_with("/*")) {
                offset_ += 2;
                until("*/");
                continue;
            } else if (remaining().starts_with("$\"")) {
                closes.push_back('"');
                ++offset_;
            } else if (c == '"' || c == '\'') {
                ++offset_;
                while (!done() && !line_end(peek()) && peek() != c) {
                    if (peek() == '\\' && !line_end(peek(1))) escaped_byte();
                    else ++offset_;
                }
                if (peek() == c) ++offset_;
                continue;
            } else if (c == '{') {
                closes.push_back('}');
            } else if (remaining().starts_with("</") && alpha(peek(2))) {
                // An end tag cannot appear in a Neura expression outside a literal or comment.
                return false;
            } else if (c == '(' && closes.back() == ')') {
                closes.push_back(')');
            } else if (c == closes.back()) {
                closes.pop_back();
            }
            ++offset_;
        }
        return closes.empty();
    }

    bool control_head() {
        const auto word = ++offset_;
        while (name_char(peek())) ++offset_;
        const bool otherwise = source_.substr(word, offset_ - word) == "else";
        whitespace();
        if (otherwise && remaining().starts_with("if") && !name_char(peek(2))) {
            offset_ += 2;
            whitespace();
        }
        if (peek() == '(') {
            neura_group(')');
            whitespace();
        } else if (peek() != '{') {
            // Frame the body of a malformed, unparenthesized head. The
            // template parser reports the missing parentheses at the head.
            const auto boundary = source_.find_first_of("<{}\r\n", offset_);
            if (boundary != std::string_view::npos && source_[boundary] == '{') offset_ = boundary;
        }
        if (peek() != '{') return false;
        ++offset_;
        return true;
    }

    static bool css_name_char(char c) {
        return text_word_char(c);
    }

    static int hex_digit(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    unsigned css_name_escape() {
        ++offset_; // backslash
        if (hex_digit(peek()) < 0) {
            return done() ? 0 : static_cast<unsigned char>(source_[offset_++]);
        }
        unsigned value = 0;
        for (int digits = 0; digits < 6 && hex_digit(peek()) >= 0; ++digits) {
            value = value * 16 + hex_digit(peek());
            ++offset_;
        }
        if (peek() == ' ' || peek() == '\t' || css_line_end(peek())) {
            if (peek() == '\r' && peek(1) == '\n') ++offset_;
            ++offset_;
        }
        return value;
    }

    bool css_name_is_url() {
        constexpr std::string_view url = "url";
        std::size_t chars = 0;
        bool matches = true;
        while (css_name_char(peek()) || (peek() == '\\' && !css_line_end(peek(1)))) {
            const auto c = peek() == '\\' ? css_name_escape() : static_cast<unsigned char>(source_[offset_++]);
            const auto lower = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
            matches = matches && chars < url.size() && lower == static_cast<unsigned char>(url[chars]);
            ++chars;
        }
        if (peek() == '\\') ++offset_; // A backslash before a newline is not a CSS escape.
        return matches && chars == url.size();
    }

    void css_url() {
        while (!done() && peek() != ')') {
            if (peek() == '\\' && !css_line_end(peek(1))) escaped_byte();
            else ++offset_;
        }
        if (!done()) ++offset_;
    }

    std::string_view source_;
    std::size_t offset_;
};

} // namespace

std::optional<Start> start(const std::vector<Token>& tokens) {
    if (tokens.empty()) return std::nullopt;
    const auto last = tokens.size() - 1;
    auto name = last;
    const auto& tail = tokens[last];
    const bool css_suffix = tail.kind == TokenKind::Identifier ||
                            (tail.kind == TokenKind::Keyword && tail.text == "module");
    if (last >= 2 && css_suffix && symbol(tokens, last - 1, ".") &&
        tokens[last - 2].kind == TokenKind::Identifier && tokens[last - 2].text == "css") name -= 2;
    if (name > 0 && tokens[name].kind == TokenKind::Identifier && starts_form(tokens[name - 1])) {
        if (tokens[name].text == "css") return Start{TokenKind::CssIsland, declaration_offset(tokens, name)};
        if (tokens[name].text == "html") return Start{TokenKind::HtmlIsland, declaration_offset(tokens, name)};
    }
    if (const auto html = html_introducer(tokens, last))
        return Start{TokenKind::HtmlIsland, declaration_offset(tokens, *html)};

    // A result annotation is reserved for the parser's malformed-factory
    // diagnostic. Look back through type tokens only, up to a statement boundary.
    for (auto i = tokens.size(); i > 0;) {
        --i;
        const auto& token = tokens[i];
        if (token.kind == TokenKind::Identifier ||
            (token.kind == TokenKind::Keyword && (token.text == "class" || token.text == "func" ||
                                                  token.text == "implements"))) continue;
        if (token.kind != TokenKind::Symbol) break;
        if (token.text == "->" && i > 0) {
            if (const auto html = html_introducer(tokens, i - 1))
                return Start{TokenKind::HtmlIsland, declaration_offset(tokens, *html)};
        } else if (token.text != "." && token.text != "&" && token.text != "<" && token.text != ">" &&
                   token.text != "[" && token.text != "]" && token.text != "(" && token.text != ")" &&
                   token.text != "," && token.text != ":") break;
    }
    return std::nullopt;
}

std::optional<std::size_t> close(std::string_view source, std::size_t body, TokenKind kind) {
    BodyScanner scanner(source, body);
    return kind == TokenKind::HtmlIsland ? scanner.html() : scanner.css();
}

std::optional<std::size_t> close_neura(std::string_view source, std::size_t open, char close) {
    return BodyScanner(source, open).embedded_neura(close);
}

bool html_void_element(std::string_view name) {
    constexpr std::string_view names[] = {"br", "col", "hr", "img", "input", "source", "track", "wbr"};
    for (const auto candidate : names) {
        if (name == candidate) return true;
    }
    return false;
}

std::size_t recover(std::string_view source, std::size_t open, std::size_t declaration) {
    const auto line_break = declaration == 0 ? std::string_view::npos : source.find_last_of("\r\n", declaration - 1);
    const auto first_line = line_break == std::string_view::npos ? 0 : line_break + 1;
    const auto indent_start = first_line == 0 && source.starts_with("\xEF\xBB\xBF") ? 3 : first_line;
    const auto indent = source.find_first_not_of(" \t", indent_start) - indent_start;
    for (auto end = source.find_first_of("\r\n", open); end != std::string_view::npos;) {
        auto line = end + 1;
        if (source[end] == '\r' && line < source.size() && source[line] == '\n') ++line;
        const auto content = source.find_first_not_of(" \t", line);
        if (content == std::string_view::npos) break;
        if (content - line <= indent) {
            const auto text = source.substr(content);
            if (text.starts_with('}')) return line;
            for (const std::string_view word : {"let", "last", "const", "export"}) {
                if (text.starts_with(word) && (text.size() == word.size() || !name_char(text[word.size()])))
                    return line;
            }
        }
        end = source.find_first_of("\r\n", content);
    }
    return source.size();
}

} // namespace neura::island
