#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura::tooling {

enum class TagStyle { Keep, Dot, Hash };
std::optional<TagStyle> parse_tag_style(std::string_view value);

struct FormatOptions {
    unsigned tab_size{4};
    bool insert_spaces{true};
    TagStyle tag_style{TagStyle::Keep};
};

// Formats a complete source snapshot. Syntax is checked before and after
// formatting. Explicit Dot and Hash modes replace only tag sigils.
// Throws Diagnostic for invalid source, and invalid_argument for bad options.
std::string format_source(std::string_view source, const std::string& file,
                          FormatOptions options = {});

// Arguments exclude the executable and the `fmt` subcommand. Printing is the
// default; --write is explicit, --check reports whether changes are needed.
int run_formatter(const std::vector<std::string>& arguments);

} // namespace neura::tooling
