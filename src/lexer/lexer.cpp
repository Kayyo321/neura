#include "lexer.hpp"

#include <algorithm>
#include <array>
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
});

constexpr auto compound_symbols = std::to_array<std::string_view>({
    "===", "<<=", ">>=",
    "->", "=>", "==", "!=", "<=", ">=", "&&", "||",
    "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=",
});

class Lexer {
public:
    Lexer(std::string_view source, const std::string& file) : source_(source), file_(file) {}

    std::vector<Token> run() {
        if (source_.starts_with("\xEF\xBB\xBF")) {
            offset_ = 3;
        }
        std::vector<Token> tokens;
        while (offset_ < source_.size()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
                continue;
            }
            if (c == '/' && peek(1) == '/') {
                while (offset_ < source_.size() && peek() != '\n' && peek() != '\r') {
                    advance_text();
                }
                continue;
            }
            const Span start = location();
            const TokenKind kind = scan(start);
            const auto width = offset_ - start.offset;
            tokens.push_back({kind, std::string(source_.substr(start.offset, width)),
                              {file_, start.line, start.column, start.offset, width}});
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
        if (c == '\r' || (c == '\n' && (offset_ < 2 || source_[offset_ - 2] != '\r'))) {
            ++line_;
            column_ = 1;
        } else if (c != '\n') {
            ++column_;
        }
        return c;
    }

    Span location() const { return {file_, line_, column_, offset_, 0}; }

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
            throw Diagnostic(std::move(invalid), "invalid UTF-8");
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
        const auto remaining = source_.substr(offset_);
        for (const auto op : compound_symbols) {
            if (remaining.starts_with(op)) {
                for (std::size_t i = 0; i < op.size(); ++i) advance();
                return TokenKind::Symbol;
            }
        }
        constexpr std::string_view symbols = "(){}[]:;,.?=+-*/%!<>&|^~";
        if (symbols.find(peek()) != std::string_view::npos) {
            advance();
            return TokenKind::Symbol;
        }
        auto invalid = start;
        invalid.length = 1;
        if (peek() >= '!' && peek() <= '~') {
            throw Diagnostic(std::move(invalid), "invalid character '" + std::string(1, peek()) + "'");
        }
        throw Diagnostic(std::move(invalid), "invalid source character");
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
                throw Diagnostic(std::move(exponent), "expected digits in numeric exponent");
            }
            while (is_digit(peek())) {
                advance();
            }
        }
        if (is_name_start(peek())) {
            auto invalid = location();
            invalid.length = 1;
            throw Diagnostic(std::move(invalid), "unexpected character after numeric literal");
        }
        return kind;
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
                throw Diagnostic(std::move(invalid), "null byte in string literal");
            }
            if (peek() == '\\') {
                auto escape = location();
                advance();
                constexpr std::string_view escapes = "nrt\\\"";
                if (escapes.find(peek()) == std::string_view::npos) {
                    escape.length = offset_ < source_.size() ? 2 : 1;
                    throw Diagnostic(std::move(escape), "invalid string escape");
                }
            }
            advance_text();
        }
        start.length = offset_ - start.offset;
        throw Diagnostic(std::move(start), "unterminated string literal");
    }

    std::string_view source_;
    const std::string& file_;
    std::size_t offset_{};
    std::size_t line_{1};
    std::size_t column_{1};
};

} // namespace

std::vector<Token> lex(std::string_view source, const std::string& file) {
    return Lexer(source, file).run();
}

} // namespace neura
