#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

struct Span {
    std::string file;
    std::size_t line{1};
    std::size_t column{1};
    std::size_t offset{};
    std::size_t length{};
};

enum class TokenKind { Identifier, Integer, Float, String, Keyword, Symbol, End };

struct Token {
    TokenKind kind;
    std::string text;
    Span span;
};

class Diagnostic : public std::runtime_error {
public:
    Diagnostic(Span location, std::string message);
    Span span;
};

std::vector<Token> lex(std::string_view source, const std::string& file);

} // namespace neura
