#pragma once

#include "util/diagnostic.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace neura {

enum class TokenKind { Identifier, Integer, Float, String, Keyword, Symbol, End };

struct Token {
    TokenKind kind;
    std::string text;
    Span span;
};

std::vector<Token> lex(std::string_view source, const std::string& file);

} // namespace neura
