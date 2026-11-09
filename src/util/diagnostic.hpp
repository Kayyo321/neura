#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace neura {

struct Span {
    std::string file;
    std::size_t line{1};
    std::size_t column{1};
    std::size_t offset{};
    std::size_t length{};
};

class Diagnostic : public std::runtime_error {
public:
    Diagnostic(Span location, std::string message)
        : std::runtime_error(std::move(message)), span(std::move(location)) {}

    Span span;
};

} // namespace neura
