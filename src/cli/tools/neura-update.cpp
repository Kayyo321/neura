// neura-update: updates an archive install
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/tool.hpp"
#include "cli/lifecycle.hpp"

namespace {

using namespace neura::cli;

int command(const Options& options) {
    switch (options.command) {
    case Command::Update:
        return neura::lifecycle::update(options.executable, options.update);
    default:
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-update"), argc, argv);
    if (start.status) return *start.status;
    return core_options_main(std::move(start.arguments), command);
}
