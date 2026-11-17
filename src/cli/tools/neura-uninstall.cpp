// neura-uninstall: removes an archive install
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/tool.hpp"
#include "cli/lifecycle.hpp"

namespace {

using namespace neura::cli;

int command(const Options& options) {
    switch (options.command) {
    case Command::Uninstall:
        return neura::lifecycle::uninstall(options.executable, options.uninstall);
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
