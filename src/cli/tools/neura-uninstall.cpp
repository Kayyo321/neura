// neura-uninstall: removes an archive install
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/lifecycle.hpp"
#include "cli/tool.hpp"

#include <filesystem>
#include <system_error>

namespace {

using namespace neura::cli;

// The lifecycle commands manage the installation that `neura` belongs to,
// and name it by the dispatcher next to this tool, as 0.2 named its single
// executable; without one, by this tool itself.
std::filesystem::path installation(const std::filesystem::path& self) {
    std::error_code error;
    const auto neura = self.parent_path() / neura::tool::executable_file("neura");
    return !self.empty() && std::filesystem::is_regular_file(neura, error) ? neura : self;
}

int command(const Options& options) {
    switch (options.command) {
    case Command::Uninstall:
        return neura::lifecycle::uninstall(installation(options.executable), options.uninstall);
    default:
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-uninstall"), argc, argv);
    if (start.status) return *start.status;
    return core_options_main(std::move(start.arguments), command);
}
