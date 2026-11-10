#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace neura::cli {

struct ArgumentList {
    std::size_t line;
    std::vector<std::string> arguments;
};

// Reads and validates every list before any execution starts. The line number
// includes skipped comments so that reports identify the original input.
std::vector<ArgumentList> read_argument_lists(const std::filesystem::path& path);

} // namespace neura::cli
