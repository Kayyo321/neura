#pragma once

// Neura source is read with LF line endings on every host: a checkout that
// converts them to CR LF (Git for Windows with core.autocrlf) must lex to the
// same tokens and spans, and so build the same image, as an LF one. Every
// reader of source text applies normalize_crlf before lexing.

#include <cstddef>
#include <string>
#include <string_view>

namespace neura::util {

// `text` with each CR LF pair replaced by LF. A CR that no LF follows is
// kept: it is a character of the source, not a line ending.
inline std::string normalize_crlf(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        out.push_back(text[i]);
    }
    return out;
}

// Whether `a` and `b` differ but are equal after normalize_crlf, so that a
// tool comparing a file with the text it would write treats the file as
// unchanged.
inline bool only_line_endings_differ(std::string_view a, std::string_view b) {
    if (a == b) return false;
    std::size_t i = 0;
    std::size_t j = 0;
    const auto skip_cr = [](std::string_view text, std::size_t& at) {
        if (text[at] == '\r' && at + 1 < text.size() && text[at + 1] == '\n') ++at;
    };
    while (i < a.size() && j < b.size()) {
        skip_cr(a, i);
        skip_cr(b, j);
        if (a[i] != b[j]) return false;
        ++i;
        ++j;
    }
    return i == a.size() && j == b.size();
}

}  // namespace neura::util
