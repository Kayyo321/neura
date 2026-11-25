#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace neura::cli {

struct Options;

// Compiles the C wrappers and links their Win64 static inputs into a PE DLL.
std::vector<std::uint8_t> link_windows_c_import(const Options& options, std::string_view shim,
                                                const std::vector<std::string>& libraries,
                                                const std::set<std::string>& include_dirs,
                                                const std::set<std::string>& symbols);

} // namespace neura::cli
