#pragma once

#include "image.hpp"

#include <optional>
#include <string>
#include <vector>

namespace neura::nvr {

// The optional NAME section (docs/runtime/NVR.md, Names): source names of
// functions, layouts, fields, globals, and frame slots, for tools. Like the
// source map it is provenance only: runtimes ignore it, and nothing about an
// image's behavior may be inferred from it.
struct Names {
    std::vector<std::string> functions;
    std::vector<std::string> layouts;
    // By layout, then field.
    std::vector<std::vector<std::string>> fields;
    std::vector<std::string> globals;
    // By function, then slot.
    std::vector<std::vector<std::string>> slots;
};

inline constexpr std::array<char, 4> names_tag{'N', 'A', 'M', 'E'};

RawSection encode_names(const Names& names);
// The names of an image that has a well-formed NAME section.
std::optional<Names> decode_names(const Image& image);

} // namespace neura::nvr
