#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace neura::tooling {
int run_lsp(const std::vector<std::string> &arguments);
// `neura rename [-I <dir>]... <file.nra> <line>:<column> <new-name>`.
int run_rename(const std::vector<std::string> &arguments, const std::filesystem::path &library);
}
