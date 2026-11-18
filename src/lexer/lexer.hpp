#pragma once

#include "util/diagnostic.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

enum class TokenKind { Identifier, Integer, Float, String, Keyword, Symbol, End, HtmlIsland, CssIsland };

struct DocComment {
    Span span;
    std::string text;
};

struct InterpolationPart;

struct Token {
    TokenKind kind;
    // An island has the exact body bytes here and includes its braces in its span.
    // For an unfinished editor island the span ends after the available body.
    std::string text;
    Span span;
    std::shared_ptr<const DocComment> documentation{};
    // An interpolated string, `$"..."` (docs/design/Calls030.md), is one
    // String token whose text is the whole literal as written; its parts
    // are its text and its holes, which carry their own spans and tokens.
    std::shared_ptr<const std::vector<InterpolationPart>> parts{};
};

// One part of an interpolated string: decoded text, or a hole with the
// tokens of its expression (ending with an End token) and, after a ':', its
// specification as Text parts and nested Hole parts.
struct InterpolationPart {
    enum class Kind { Text, Hole };

    Kind kind;
    Span span;
    std::string text;
    std::vector<Token> tokens;
    std::vector<InterpolationPart> specification;
    std::optional<Span> specification_span;
};

std::vector<Token> lex(std::string_view source, const std::string& file);
// Lexes the Neura part of an HTML hole or control head, where a block comment
// is also allowed. The caller has already found the enclosing delimiters.
std::vector<Token> lex_template_neura(std::string_view source, const std::string& file);
// Editor mode retains valid tokens when another token is malformed.
std::vector<Token> lex_recovering(std::string_view source, const std::string& file,
                                  std::vector<Diagnostic>& diagnostics);
// Whether the whole spelling is one non-keyword source identifier.
bool is_identifier(std::string_view spelling);
// Decode a string token that has already passed lexical validation.
std::string decode_string_literal(std::string_view literal);
// A character literal, `'a'` or `'\n'` (docs/design/Expressions030.md), is an
// Integer token whose text is the literal as written, beginning with `'`.
inline bool is_character_literal(const Token& token) {
    return token.kind == TokenKind::Integer && token.text.starts_with('\'');
}
// The byte of a character literal that has already passed lexical validation.
std::uint8_t decode_character_literal(std::string_view literal);

} // namespace neura
