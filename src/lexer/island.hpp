#pragma once

#include "lexer.hpp"

#include <optional>

namespace neura::island {

struct Start {
    TokenKind kind;
    std::size_t declaration;
};

// The opening brace is the next byte to be lexed.
std::optional<Start> start(const std::vector<Token>& tokens);
// Offset of the closing brace, when the body contains one.
std::optional<std::size_t> close(std::string_view source, std::size_t body, TokenKind kind);
std::optional<std::size_t> close_neura(std::string_view source, std::size_t open, char close);
// Move past an identifier or number when looking for Neura delimiters.
// Returns the original offset for any other token. The lexer validates numbers.
std::size_t skip_neura_word(std::string_view source, std::size_t offset);
bool html_void_element(std::string_view name);
// Offset where an unfinished island ends in editor mode.
std::size_t recover(std::string_view source, std::size_t open, std::size_t declaration);

} // namespace neura::island
