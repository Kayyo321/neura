// neurac: the compiler. `neurac app.nra -o app.nvr` is `neura build`
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/tool.hpp"

namespace {

using namespace neura::cli;

int command(const Options& options) {
    switch (options.command) {
    case Command::Build:
        return build(options);
    case Command::Check:
        return check(options);
    case Command::EmitIr:
        return emit_ir(options);
    default:
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neurac"), argc, argv);
    if (start.status) return *start.status;
    return sdk_options_main(std::move(start.arguments), command);
}
