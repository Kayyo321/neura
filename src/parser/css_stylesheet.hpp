#pragma once

#include "util/diagnostic.hpp"

#include <map>

namespace neura {

struct CssName {
    enum class Kind { Class, Keyframes, Animation };
    Kind kind;
    std::string name;
    Span span;
};

struct StylesheetBody {
    Span span;
    std::vector<CssName> names;
    std::vector<Span> global_markers;
};

// Parse already decoded UTF-8. Origin is the position of the first byte of
// the CSS, in either a Neura island or an external stylesheet.
StylesheetBody parse_stylesheet(std::string_view bytes, Span origin, bool module);

struct RewrittenStylesheet {
    std::string text;
    std::map<std::string, std::string, std::less<>> classes;
    std::map<std::string, std::string, std::less<>> keyframes;
};

// Pass the same bytes that were parsed. The caller supplies the package and
// the inline qualified name or package-relative file path used as the key.
RewrittenStylesheet rewrite_stylesheet(std::string_view bytes, const StylesheetBody& parsed,
                                       std::string_view package, std::string_view key);

} // namespace neura
