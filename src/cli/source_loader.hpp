#pragma once

#include "parser/syntax.hpp"

#include <filesystem>
#include <vector>

namespace neura {

// Lexes and parses `files` and the project modules they import. An import is
// searched for in the importing file's directory and then in each of `roots`.
// Std imports resolve only within the trusted standard library root.
std::vector<ParsedFile> read_sources(const std::vector<std::filesystem::path>& files,
                                     const std::vector<std::filesystem::path>& roots,
                                     const std::filesystem::path& library = {});

} // namespace neura
