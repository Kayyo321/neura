#include "lexer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <optional>
#include <utility>

namespace neura {
namespace {

constexpr bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

constexpr bool is_name_char(char c) {
    return is_name_start(c) || is_digit(c);
}

constexpr auto keywords = std::to_array<std::string_view>({
    "let", "const", "export", "override", "func", "module", "class", "import", "new",
    "destroy", "if", "else", "while", "do", "for", "in", "break", "continue", "return", "true", "false",
    "as", "is", "nil", "enum", "match", "and", "or", "interface", "implements",
    "promote", "static", "singleton",
    // docs/design/Expressions030.md: the negation word and the type query.
    "not", "typeof",
});

constexpr auto compound_symbols = std::to_array<std::string_view>({
    "===", "<<=", ">>=",
    // docs/design/Expressions030.md, Value inequality: before "!=".
    "!==",
    "->", "=>", "==", "!=", "<=", ">=", "&&", "||",
    // docs/design/Matching030.md: optional access and coalescing.
    "?.", "??",
    "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=",
});

// Thrown inside the lexer after an error is recorded, to abandon the token
// being scanned; the lexer goes on with the next one.
struct Skipped {};

class Lexer {
public:
    Lexer(std::string_view source, const std::string& file) : source_(source), file_(file) {}

    std::vector<Token> run(std::vector<Diagnostic>* diagnostics = nullptr) {
        if (source_.starts_with("\xEF\xBB\xBF")) {
            offset_ = line_start_ = 3;
        }
        std::vector<Token> tokens;
        while (offset_ < source_.size()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                comment();
                continue;
            }
            const Span start = location();
            TokenKind kind;
            try {
                kind = scan(start);
            } catch (const Skipped&) {
                documentation_.reset();
                parts_.reset();
                continue;
            }
            const auto width = offset_ - start.offset;
            std::shared_ptr<const DocComment> documentation;
            if (documentation_ && start.line == documentation_end_line_ + 1) {
                documentation = std::make_shared<DocComment>(std::move(*documentation_));
            }
            documentation_.reset();
            tokens.push_back({kind, std::string(source_.substr(start.offset, width)),
                              {file_, start.line, start.column, start.offset, width},
                              std::move(documentation), std::exchange(parts_, nullptr)});
        }
        if (!errors_.empty()) {
            if (!diagnostics) throw combined();
            diagnostics->insert(diagnostics->end(), errors_.begin(), errors_.end());
        }
        tokens.push_back({TokenKind::End, {}, location()});
        return tokens;
    }

private:
    char peek(std::size_t ahead = 0) const {
        return offset_ + ahead < source_.size() ? source_[offset_ + ahead] : '\0';
    }

    char advance() {
        const char c = source_[offset_++];
        if (c == '\r' || c == '\n') line_start_ = offset_;
        if (c == '\r' || (c == '\n' && (offset_ < 2 || source_[offset_ - 2] != '\r'))) {
            ++line_;
            column_ = 1;
        } else if (c != '\n') {
            ++column_;
        }
        return c;
    }

    Span location() const { return {file_, line_, column_, offset_, 0}; }

    // The first error, carrying the others (Tooling.md, Diagnostics).
    Diagnostic combined() const {
        auto first = errors_.front();
        for (std::size_t i = 1; i < errors_.size(); ++i) first.also(errors_[i]);
        return first;
    }

    // Records a lexical error and goes on, up to error_limit() errors.
    void report(Diagnostic error) {
        errors_.push_back(std::move(error));
        if (errors_.size() >= error_limit()) throw combined();
    }

    // Skips the code point at the current offset, which is not a line end.
    void skip_code_point() {
        ++offset_;
        while (offset_ < source_.size() && (static_cast<unsigned char>(source_[offset_]) & 0xC0) == 0x80) ++offset_;
        ++column_;
    }

    // The code point at the current offset as U+XXXX, for a label.
    std::string code_point() const {
        const auto lead = static_cast<unsigned char>(peek());
        std::uint32_t value = lead;
        std::size_t width = 1;
        if (lead >= 0xF0) { value = lead & 0x07; width = 4; }
        else if (lead >= 0xE0) { value = lead & 0x0F; width = 3; }
        else if (lead >= 0xC0) { value = lead & 0x1F; width = 2; }
        for (std::size_t i = 1; i < width; ++i) value = (value << 6) | (static_cast<unsigned char>(peek(i)) & 0x3F);
        constexpr char digits[] = "0123456789ABCDEF";
        std::string hex;
        for (int shift = value > 0xFFFF ? 20 : 12; shift >= 0; shift -= 4) hex += digits[(value >> shift) & 0xF];
        return "U+" + hex;
    }

    void comment() {
        const auto start = location();
        const auto indent = source_.substr(line_start_, offset_ - line_start_);
        const bool standalone_doc = peek(2) == '/' && indent.find_first_not_of(" \t") == std::string_view::npos;
        while (offset_ < source_.size() && peek() != '\n' && peek() != '\r') {
            advance_text();
        }
        if (!standalone_doc) {
            documentation_.reset();
            return;
        }
        auto text = source_.substr(start.offset + 3, offset_ - start.offset - 3);
        if (text.starts_with(' ')) text.remove_prefix(1);
        if (documentation_ && start.line == documentation_end_line_ + 1) {
            documentation_->text += '\n';
        } else {
            documentation_ = DocComment{start, {}};
        }
        documentation_->text += text;
        documentation_->span.length = offset_ - documentation_->span.offset;
        documentation_end_line_ = start.line;
    }

    void advance_text() {
        const auto lead = static_cast<unsigned char>(peek());
        if (lead < 0x80) {
            advance();
            return;
        }

        const std::size_t width = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        const auto second = static_cast<unsigned char>(peek(1));
        bool valid = lead >= 0xC2 && lead <= 0xF4
            && !(lead == 0xE0 && second < 0xA0)
            && !(lead == 0xED && second > 0x9F)
            && !(lead == 0xF0 && second < 0x90)
            && !(lead == 0xF4 && second > 0x8F);
        for (std::size_t i = 1; valid && i < width; ++i) {
            const auto continuation = static_cast<unsigned char>(peek(i));
            valid = continuation >= 0x80 && continuation <= 0xBF;
        }
        if (!valid) {
            auto invalid = location();
            invalid.length = 1;
            report(Diagnostic(std::move(invalid), "invalid UTF-8").with_code("E0102")
                .label("this byte does not begin a valid UTF-8 sequence")
                .note("Neura source files are UTF-8 text"));
            ++offset_;
            ++column_;
            return;
        }
        offset_ += width;
        ++column_;
    }

    TokenKind scan(const Span& start) {
        if (is_name_start(peek())) {
            do {
                advance();
            } while (is_name_char(peek()));
            const auto name = source_.substr(start.offset, offset_ - start.offset);
            return std::ranges::find(keywords, name) != keywords.end() ? TokenKind::Keyword
                                                                      : TokenKind::Identifier;
        }
        if (is_digit(peek())) {
            return number();
        }
        if (peek() == '"') {
            return string(start);
        }
        if (peek() == '$' && peek(1) == '"') {
            return interpolated(start);
        }
        if (peek() == '\'') {
            return character(start);
        }
        if (peek() == '.' && peek(1) == '.') {
            advance();
            advance();
            if (peek() == '.') advance();
            return TokenKind::Symbol;
        }
        const auto remaining = source_.substr(offset_);
        for (const auto op : compound_symbols) {
            if (remaining.starts_with(op)) {
                for (std::size_t i = 0; i < op.size(); ++i) advance();
                return TokenKind::Symbol;
            }
        }
        // '#' begins a tag (docs/lang/Tags.md).
        constexpr std::string_view symbols = "(){}[]:;,.?=+-*/%!<>&|^~#";
        if (symbols.find(peek()) != std::string_view::npos) {
            advance();
            return TokenKind::Symbol;
        }
        auto invalid = start;
        if (peek() >= '!' && peek() <= '~') {
            invalid.length = 1;
            report(Diagnostic(std::move(invalid), "invalid character '" + std::string(1, peek()) + "'")
                .with_code("E0101").label("not a token of the language"));
            advance();
            throw Skipped{};
        }
        const auto lead = static_cast<unsigned char>(peek());
        if (lead < 0x80) {
            invalid.length = 1;
            report(Diagnostic(std::move(invalid), "invalid source character").with_code("E0101")
                .label("a control character is not valid here"));
            advance();
            throw Skipped{};
        }
        const auto before = offset_;
        advance_text();
        if (offset_ == before + 1 && errors_.size() && errors_.back().code() == "E0102") throw Skipped{};
        invalid.length = offset_ - before;
        offset_ = before;
        --column_;
        const auto name = code_point();
        const std::string text(source_.substr(before, invalid.length));
        auto error = Diagnostic(invalid, "invalid source character").with_code("E0101")
            .label(name + " is not valid here");
        // Typographic quotes, as a word processor writes them.
        if (text == "\u201C" || text == "\u201D")
            error.suggest("use an ASCII double quote", invalid, "\"", Applicability::MaybeIncorrect);
        else if (text == "\u2018" || text == "\u2019")
            error.note("a character literal uses the ASCII quote ', as in 'a', and a string uses '\"'");
        else if (text == "\u2212" || text == "\u2013")
            error.suggest("use an ASCII minus sign", invalid, "-", Applicability::MaybeIncorrect);
        else if (text == "\u00A0")
            error.suggest("use an ordinary space", invalid, " ", Applicability::MachineApplicable);
        report(std::move(error));
        skip_code_point();
        throw Skipped{};
    }

    TokenKind number() {
        while (is_digit(peek())) {
            advance();
        }
        auto kind = TokenKind::Integer;
        if (peek() == '.' && is_digit(peek(1))) {
            kind = TokenKind::Float;
            advance();
            while (is_digit(peek())) {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            auto exponent = location();
            kind = TokenKind::Float;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (!is_digit(peek())) {
                exponent.length = offset_ - exponent.offset;
                report(Diagnostic(std::move(exponent), "expected digits in numeric exponent").with_code("E0105")
                    .label("an exponent needs at least one digit"));
                while (is_name_char(peek())) advance();
                throw Skipped{};
            }
            while (is_digit(peek())) {
                advance();
            }
        }
        if (is_name_start(peek())) {
            auto invalid = location();
            invalid.length = 1;
            report(Diagnostic(std::move(invalid), "unexpected character after numeric literal").with_code("E0105")
                .label("a number cannot continue with a letter")
                .help("separate the number from a following name with a space or an operator"));
            while (is_name_char(peek())) advance();
            throw Skipped{};
        }
        return kind;
    }

    // Character literals (docs/design/Expressions030.md, Character
    // literals): one printable ASCII byte or one escape between single
    // quotes. The token is an Integer whose text is the literal as written,
    // so the formatter writes it back; decode_character_literal gives its
    // value.
    static constexpr std::string_view character_escapes =
        "the escapes are \\n, \\r, \\t, \\0, \\\\, \\', \\\" and \\xHH with two hexadecimal digits";

    static constexpr bool is_hex(char c) {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    static constexpr std::size_t utf8_width(unsigned char lead) {
        return lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
    }

    TokenKind character(Span start) {
        // Find the closing quote on this line; a backslash takes the byte
        // after it, so '\'' closes at its third quote.
        std::size_t end = offset_ + 1;
        while (end < source_.size() && source_[end] != '\n' && source_[end] != '\r' && source_[end] != '\'') {
            if (source_[end] == '\\' && end + 1 < source_.size() && source_[end + 1] != '\n' &&
                source_[end + 1] != '\r')
                ++end;
            ++end;
        }
        const bool closed = end < source_.size() && source_[end] == '\'';
        const auto content = source_.substr(offset_ + 1, end - offset_ - 1);
        // Consumes the literal through its closing quote, or to the line end.
        const auto finish = [&] {
            advance();
            while (offset_ < end) advance_text();
            if (closed) advance();
        };
        const auto malformed = [&](Diagnostic error) {
            finish();
            report(std::move(error));
            throw Skipped{};
        };
        auto literal = start;
        literal.length = (closed ? end + 1 : end) - start.offset;
        if (!closed) {
            auto quote = start;
            quote.length = 1;
            finish();
            report(Diagnostic(quote, "unterminated character literal").with_code("E0261")
                .label("this character literal is not closed")
                .label(location(), offset_ >= source_.size() ? "the file ends here" : "the line ends here")
                .note("a character literal is one character or one escape between single quotes, such as 'a' "
                      "or '\\n'"));
            throw Skipped{};
        }
        if (content.empty()) {
            // Three quotes in a row most likely mean a quote character.
            if (peek(2) == '\'') {
                literal.length = 3;
                end = offset_ + 2;
                malformed(Diagnostic(literal, "empty character literal").with_code("E0261")
                    .label("the first two quotes enclose nothing")
                    .suggest("escape the quote character", literal, "'\\''", Applicability::MaybeIncorrect));
            }
            malformed(Diagnostic(literal, "empty character literal").with_code("E0261")
                .label("a character literal holds one character")
                .help("write one character or one escape, such as 'a' or '\\0'"));
        }
        const auto first = static_cast<unsigned char>(content.front());
        if (first == '\\') {
            const char kind = content.size() > 1 ? content[1] : '\0';
            if (kind == 'u') {
                malformed(Diagnostic(literal, "character literal is not one byte").with_code("E0260")
                    .label("a character literal has no \\u escape")
                    .help("use Std.Core.Character for a Unicode code point, or a string literal for text")
                    .note(std::string(character_escapes)));
            }
            std::size_t width = 2;
            bool valid = std::string_view("nrt0\\'\"").find(kind) != std::string_view::npos;
            if (kind == 'x') {
                valid = content.size() >= 4 && is_hex(content[2]) && is_hex(content[3]);
                width = 4;
            } else if (!valid) {
                width = 1 + utf8_width(static_cast<unsigned char>(kind));
            }
            if (!valid) {
                auto escape = start;
                escape.offset += 1;
                escape.column += 1;
                escape.length = std::min(width, content.size());
                malformed(Diagnostic(escape, kind == 'x' ? "invalid \\x escape in character literal"
                                                         : "unknown escape in character literal")
                    .with_code("E0261")
                    .label(kind == 'x' ? "\\x takes exactly two hexadecimal digits" : "unknown escape")
                    .note(std::string(character_escapes)));
            }
            if (content.size() == width) {
                finish();
                return TokenKind::Integer;
            }
        } else if (first >= 0x80) {
            if (content.size() == utf8_width(first)) {
                const auto before = errors_.size();
                finish();
                // Invalid UTF-8 is E0102, which advance_text reported.
                if (errors_.size() != before) throw Skipped{};
                report(Diagnostic(literal, "character literal is not one byte").with_code("E0260")
                    .label("this character is not ASCII")
                    .help("use Std.Core.Character for a Unicode code point, or a string literal for text")
                    .note("a character literal names one byte; \\xHH writes any byte"));
                throw Skipped{};
            }
        } else if (first < 0x20 || first == 0x7F) {
            auto error = Diagnostic(literal, "control character in character literal").with_code("E0261")
                .label(first == '\t' ? "a tab is written \\t" : "a control character is written as an escape")
                .note(std::string(character_escapes));
            if (first == '\t' && content.size() == 1)
                error.suggest("write the escape", literal, "'\\t'", Applicability::MachineApplicable);
            malformed(std::move(error));
        } else if (content.size() == 1) {
            finish();
            return TokenKind::Integer;
        }
        auto error = Diagnostic(literal, "character literal holds more than one character").with_code("E0261")
            .label("a character literal holds one character");
        if (content.find_first_of("\\\"") == std::string_view::npos)
            error.suggest("use double quotes for a string", literal, "\"" + std::string(content) + "\"",
                          Applicability::MaybeIncorrect);
        else
            error.help("use a string literal, in double quotes, for text");
        malformed(std::move(error));
    }

    TokenKind string(Span start) {
        advance();
        while (offset_ < source_.size() && peek() != '\n' && peek() != '\r') {
            if (peek() == '"') {
                advance();
                return TokenKind::String;
            }
            if (peek() == '\0') {
                auto invalid = location();
                invalid.length = 1;
                report(Diagnostic(std::move(invalid), "null byte in string literal").with_code("E0104")
                    .label("a string cannot contain a null byte"));
                advance();
                continue;
            }
            if (peek() == '\\') {
                auto escape = location();
                advance();
                constexpr std::string_view escapes = "nrt\\\"";
                if (escapes.find(peek()) == std::string_view::npos) {
                    const bool more = offset_ < source_.size() && peek() != '\n' && peek() != '\r';
                    escape.length = more ? 2 : 1;
                    auto error = Diagnostic(escape, "invalid string escape").with_code("E0104")
                        .label("unknown escape")
                        .note("the escapes are \\n, \\r, \\t, \\\\, and \\\"");
                    if (more) {
                        escape.length = 1;
                        error.suggest("to write a backslash, escape it", escape, "\\\\", Applicability::MaybeIncorrect);
                    }
                    report(std::move(error));
                    if (!more) continue;
                }
            }
            advance_text();
        }
        // The string runs to the end of its line: label the opening quote,
        // and offer the closing one where the line ends, before a final ';'
        // that more likely ends the statement than the text.
        Span end = location();
        auto close = end;
        auto last = close.offset;
        while (last > start.offset + 1 && (source_[last - 1] == ' ' || source_[last - 1] == '\t')) --last;
        if (last > start.offset + 1 && source_[last - 1] == ';') {
            close.column -= close.offset - (last - 1);
            close.offset = last - 1;
        }
        start.length = 1;
        report(Diagnostic(start, "unterminated string literal").with_code("E0103")
            .label("this string is not closed")
            .label(end, "the line ends here")
            .note("a string literal ends on the line where it starts")
            .suggest("close the string", close, "\"", Applicability::MaybeIncorrect));
        throw Skipped{};
    }

    // Interpolated strings (docs/design/Calls030.md, Interpolated strings).
    // The literal is one line. Outside holes it has the escapes of a string
    // literal, and `{{` and `}}` are braces. A hole is lexed with this
    // lexer, token by token, so a nested literal is one token of it; the
    // first ':' outside brackets begins the specification.
    static constexpr std::size_t interpolation_limit = 8;

    bool line_end() const { return offset_ >= source_.size() || peek() == '\n' || peek() == '\r'; }

    [[noreturn]] void unterminated(Span start) {
        Span end = location();
        start.length = 2;
        report(Diagnostic(start, "unterminated string literal").with_code("E0103")
            .label("this interpolated string is not closed")
            .label(end, "the line ends here")
            .note("an interpolated string, its holes included, ends on the line where it starts"));
        throw Skipped{};
    }

    void malformed(Span at, const std::string& message, const std::string& label) {
        report(Diagnostic(std::move(at), message).with_code("E0227").label(label));
    }

    [[noreturn]] void malformed_and_skip(Span at, const std::string& message, const std::string& label) {
        malformed(std::move(at), message, label);
        while (!line_end()) advance_text();
        throw Skipped{};
    }

    // Decodes one escape of the literal's text into `text`.
    void escape(std::string& text) {
        auto at = location();
        advance();
        constexpr std::string_view escapes = "nrt\\\"";
        const char c = peek();
        if (escapes.find(c) == std::string_view::npos || line_end()) {
            at.length = line_end() ? 1 : 2;
            report(Diagnostic(at, "invalid string escape").with_code("E0104")
                .label("unknown escape")
                .note("the escapes are \\n, \\r, \\t, \\\\, and \\\""));
            if (line_end()) return;
            advance_text();
            return;
        }
        text += c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c;
        advance();
    }

    TokenKind interpolated(Span start) {
        if (++interpolation_depth_ > interpolation_limit) {
            auto at = start;
            at.length = 2;
            --interpolation_depth_;
            malformed_and_skip(at, "interpolated strings nest deeper than eight",
                               "this literal is the ninth level of nesting");
        }
        struct Depth {
            std::size_t& depth;
            ~Depth() { --depth; }
        } depth{interpolation_depth_};
        advance();
        advance();
        auto parts = std::make_shared<std::vector<InterpolationPart>>();
        std::string text;
        Span text_start = location();
        const auto flush = [&] {
            if (location().offset == text_start.offset) return;
            auto span = text_start;
            span.length = offset_ - text_start.offset;
            parts->push_back({InterpolationPart::Kind::Text, span, std::exchange(text, {}), {}, {}, std::nullopt});
        };
        while (!line_end()) {
            const char c = peek();
            if (c == '"') {
                flush();
                advance();
                parts_ = std::move(parts);
                return TokenKind::String;
            }
            if (c == '\\') {
                escape(text);
                continue;
            }
            if (c == '{' && peek(1) == '{') {
                text += '{';
                advance();
                advance();
                continue;
            }
            if (c == '}' && peek(1) == '}') {
                text += '}';
                advance();
                advance();
                continue;
            }
            if (c == '{') {
                flush();
                parts->push_back(hole(start, false));
                text_start = location();
                continue;
            }
            if (c == '}') {
                auto at = location();
                at.length = 1;
                report(Diagnostic(at, "malformed interpolated string").with_code("E0227")
                    .label("a single '}' closes no hole")
                    .suggest("double it to write a brace", at, "}}", Applicability::MaybeIncorrect));
                text += '}';
                advance();
                continue;
            }
            if (c == '\0') {
                auto invalid = location();
                invalid.length = 1;
                report(Diagnostic(std::move(invalid), "null byte in string literal").with_code("E0104")
                    .label("a string cannot contain a null byte"));
                advance();
                continue;
            }
            const auto before = offset_;
            advance_text();
            text += source_.substr(before, offset_ - before);
        }
        unterminated(start);
    }

    static bool begins_expression(const Token& token) {
        switch (token.kind) {
        case TokenKind::Identifier:
        case TokenKind::Integer:
        case TokenKind::Float:
        case TokenKind::String:
            return true;
        case TokenKind::Keyword:
            return token.text == "new" || token.text == "func" || token.text == "match" || token.text == "import" ||
                   token.text == "true" || token.text == "false" || token.text == "nil";
        case TokenKind::Symbol:
            return token.text == "(" || token.text == "[" || token.text == "{" || token.text == "-" ||
                   token.text == "!" || token.text == "~" || token.text == "++" || token.text == "--";
        default:
            return false;
        }
    }

    // A hole from its '{' through its '}'. A nested hole, in a
    // specification, has no specification of its own.
    InterpolationPart hole(Span literal, bool nested) {
        InterpolationPart part{InterpolationPart::Kind::Hole, location(), {}, {}, {}, std::nullopt};
        advance();
        std::vector<char> brackets;
        std::optional<Span> conditional;
        std::optional<Span> question;
        while (true) {
            while (peek() == ' ' || peek() == '\t') advance();
            if (line_end()) unterminated(literal);
            if (peek() == '/' && (peek(1) == '/' || peek(1) == '*')) {
                auto at = location();
                at.length = 2;
                malformed_and_skip(at, "malformed interpolated string", "a hole contains no comment");
            }
            if (brackets.empty() && (peek() == '}' || peek() == ':')) break;
            const auto start = location();
            const auto kind = scan(start);
            const auto width = offset_ - start.offset;
            Token token{kind, std::string(source_.substr(start.offset, width)),
                        {file_, start.line, start.column, start.offset, width}, nullptr,
                        std::exchange(parts_, nullptr)};
            // A top-level '?' is a conditional's when the next token can
            // begin an expression, and otherwise a postfix propagation,
            // which ends no hole (docs/design/Matching030.md).
            if (question && !conditional && begins_expression(token)) conditional = question;
            question.reset();
            if (kind == TokenKind::Symbol && token.text == "?" && brackets.empty()) {
                question = token.span;
            } else if (kind == TokenKind::Symbol && token.text.size() == 1) {
                const char c = token.text.front();
                if (c == '(' || c == '[' || c == '{') {
                    brackets.push_back(c);
                } else if (c == ')' || c == ']' || c == '}') {
                    const char open = c == ')' ? '(' : c == ']' ? '[' : '{';
                    if (brackets.empty() || brackets.back() != open)
                        malformed_and_skip(token.span, "malformed interpolated string",
                                           brackets.empty() ? "this bracket closes nothing in the hole"
                                                            : "this bracket does not close the '" +
                                                                  std::string(1, brackets.back()) + "' before it");
                    brackets.pop_back();
                }
            }
            part.tokens.push_back(std::move(token));
        }
        const auto close = location();
        if (part.tokens.empty()) {
            auto at = part.span;
            at.length = offset_ - at.offset + 1;
            malformed_and_skip(at, "malformed interpolated string", "an empty hole");
        }
        if (conditional) {
            // The conditional runs to the hole's '}', past the ':' that ended
            // its expression.
            const auto& first = part.tokens.front().span;
            auto end = source_.find('}', offset_);
            const auto line = source_.find_first_of("\r\n", offset_);
            if (end == std::string_view::npos || (line != std::string_view::npos && line < end)) end = offset_;
            while (end > first.offset && (source_[end - 1] == ' ' || source_[end - 1] == '\t')) --end;
            Span expression{first.file, first.line, first.column, first.offset, end - first.offset};
            auto error = Diagnostic(*conditional, "malformed interpolated string").with_code("E0227")
                .label("a conditional at the top of a hole is written in parentheses")
                .note("the first ':' outside brackets begins the hole's specification");
            error.suggest("parenthesize the conditional", expression,
                          "(" + std::string(source_.substr(first.offset, expression.length)) + ")",
                          Applicability::MaybeIncorrect);
            report(std::move(error));
            while (!line_end()) advance_text();
            throw Skipped{};
        }
        part.tokens.push_back({TokenKind::End, {}, close});
        if (peek() == ':') {
            if (nested) {
                auto at = location();
                at.length = 1;
                malformed_and_skip(at, "malformed interpolated string",
                                   "a nested width or precision hole has no specification");
            }
            advance();
            const auto specification = location();
            std::string text;
            Span text_start = specification;
            const auto flush = [&] {
                if (location().offset == text_start.offset) return;
                auto span = text_start;
                span.length = offset_ - text_start.offset;
                part.specification.push_back(
                    {InterpolationPart::Kind::Text, span, std::exchange(text, {}), {}, {}, std::nullopt});
            };
            while (true) {
                if (line_end()) unterminated(literal);
                const char c = peek();
                if (c == '}') break;
                if (c == '"') {
                    auto at = location();
                    at.length = 1;
                    malformed_and_skip(at, "malformed interpolated string", "a specification holds no string");
                }
                if (c == '{') {
                    flush();
                    part.specification.push_back(hole(literal, true));
                    text_start = location();
                    continue;
                }
                const auto before = offset_;
                advance_text();
                text += source_.substr(before, offset_ - before);
            }
            flush();
            auto span = specification;
            span.length = offset_ - specification.offset;
            part.specification_span = span;
        }
        advance();
        part.span.length = offset_ - part.span.offset;
        return part;
    }

    std::shared_ptr<const std::vector<InterpolationPart>> parts_;
    std::size_t interpolation_depth_{};

    std::string_view source_;
    const std::string& file_;
    std::size_t offset_{};
    std::size_t line_{1};
    std::size_t column_{1};
    std::size_t line_start_{};
    std::optional<DocComment> documentation_;
    std::size_t documentation_end_line_{};
    std::vector<Diagnostic> errors_;
};

} // namespace

bool is_identifier(std::string_view spelling) {
    return !spelling.empty() && is_name_start(spelling.front()) &&
           std::ranges::all_of(spelling, is_name_char) && std::ranges::find(keywords, spelling) == keywords.end();
}

std::vector<Token> lex(std::string_view source, const std::string& file) {
    return Lexer(source, file).run();
}

std::vector<Token> lex_recovering(std::string_view source, const std::string& file,
                                  std::vector<Diagnostic>& diagnostics) {
    return Lexer(source, file).run(&diagnostics);
}

std::uint8_t decode_character_literal(std::string_view literal) {
    const auto text = literal.substr(1, literal.size() - 2);
    if (text.front() != '\\') return static_cast<std::uint8_t>(text.front());
    switch (text[1]) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '0': return 0;
    case 'x': {
        std::uint8_t value = 0;
        std::from_chars(text.data() + 2, text.data() + 4, value, 16);
        return value;
    }
    default: return static_cast<std::uint8_t>(text[1]);
    }
}

std::string decode_string_literal(std::string_view literal) {
    auto text = literal.substr(1, literal.size() - 2);
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto ch = text[i];
        if (ch == '\\') {
            ch = text[++i];
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
        }
        result += ch;
    }
    return result;
}

} // namespace neura
