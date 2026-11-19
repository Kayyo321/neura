// neura-lsp: the compiler-backed language server, and `neura rename`, which
// applies its rename to the files on disk (docs/design/Products030.md,
// "Binary map").

#include "cli/driver.hpp"
#include "cli/options.hpp"
#include "cli/tool.hpp"
#include "tooling/lsp.hpp"

namespace {

int serve(const std::vector<std::string>& arguments, const std::filesystem::path& self) {
    (void)self;
    return neura::tooling::run_lsp(arguments);
}

int rename_files(const std::vector<std::string>& arguments, const std::filesystem::path& self) {
    return neura::tooling::run_rename(arguments, neura::cli::standard_library(self));
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-lsp"), argc, argv);
    if (start.status) return *start.status;
    const bool renaming = start.arguments.size() > 1 && start.arguments[1] == "rename";
    return neura::cli::sdk_protocol_main(start.arguments, renaming ? rename_files : serve);
}
