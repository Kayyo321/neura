#pragma once

#include "util/diagnostic.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

enum class TokenKind { Identifier, Integer, Float, String, Keyword, Symbol, End };

struct DocComment {
    Span span;
    std::string text;
};

struct Token {
    TokenKind kind;
    std::string text;
    Span span;
    std::shared_ptr<const DocComment> documentation{};
};

std::vector<Token> lex(std::string_view source, const std::string& file);
// Decode a string token that has already passed lexical validation.
std::string decode_string_literal(std::string_view literal);

} // namespace neura
