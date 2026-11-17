#pragma once

#include "util/diagnostic.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace neura {

// Translate byte ranges in a source island into the original file's
// absolute offsets and UTF-8 columns, with the lexer's CRLF convention.
class IslandPositions {
public:
    IslandPositions(std::string_view source, Span origin) : origin_(std::move(origin)), lines_{0} {
        for (std::size_t i = 0; i < source.size(); ++i) {
            const auto c = static_cast<unsigned char>(source[i]);
            if (c == '\r' && i + 1 < source.size() && source[i + 1] == '\n') ++i;
            if (source[i] == '\r' || source[i] == '\n') lines_.push_back(i + 1);
            else if ((c & 0xc0) == 0x80) continuations_.push_back(i);
        }
    }

    Span span(std::size_t begin, std::size_t end) const {
        const auto line = std::prev(std::ranges::upper_bound(lines_, begin));
        const auto skipped = std::ranges::lower_bound(continuations_, begin) -
                             std::ranges::lower_bound(continuations_, *line);
        const auto column = line == lines_.begin() ? origin_.column : 1;
        return {origin_.file, origin_.line + static_cast<std::size_t>(line - lines_.begin()),
                column + begin - *line - static_cast<std::size_t>(skipped), origin_.offset + begin, end - begin};
    }

private:
    Span origin_;
    std::vector<std::size_t> lines_;
    std::vector<std::size_t> continuations_;
};

} // namespace neura
