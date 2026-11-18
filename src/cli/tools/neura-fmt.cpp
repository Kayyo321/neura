// neura-fmt: the source formatter
// (docs/design/Products030.md, "Binary map").

#include "cli/driver.hpp"
#include "cli/tool.hpp"
#include "tooling/formatter.hpp"

namespace {

int command(const std::vector<std::string>& arguments, const std::filesystem::path& self) {
    (void)self;
    return neura::tooling::run_formatter(arguments);
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-fmt"), argc, argv);
    if (start.status) return *start.status;
    return neura::cli::sdk_protocol_main(start.arguments, command);
}
