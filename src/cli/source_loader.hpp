#pragma once

#include "parser/syntax.hpp"

#include <filesystem>
#include <vector>

namespace neura {

// Lexes and parses `files` and the project modules they import. An import is
// searched for in the importing file's directory and then in each of `roots`.
// The built-in SysIo and Test modules are resolved during lowering.
std::vector<ParsedFile> read_sources(const std::vector<std::filesystem::path>& files,
                                     const std::vector<std::filesystem::path>& roots);

} // namespace neura
