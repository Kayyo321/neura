#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace neura::tooling {

struct FormatOptions {
    unsigned tab_size{4};
    bool insert_spaces{true};
};

// Formats a complete source snapshot. Syntax is checked before and after
// formatting; token spellings and documentation attachment are preserved.
// Throws Diagnostic for invalid source, and invalid_argument for bad options.
std::string format_source(std::string_view source, const std::string& file,
                          FormatOptions options = {});

// Arguments exclude the executable and the `fmt` subcommand. Printing is the
// default; --write is explicit, --check reports whether changes are needed.
int run_formatter(const std::vector<std::string>& arguments);

} // namespace neura::tooling
