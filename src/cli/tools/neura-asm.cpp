// neura-asm: an image from its assembly text
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/tool.hpp"

namespace {

using namespace neura::cli;

int command(const Options& options) {
    switch (options.command) {
    case Command::Assemble:
        return assemble(options);
    default:
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-asm"), argc, argv);
    if (start.status) return *start.status;
    return sdk_options_main(std::move(start.arguments), command);
}
