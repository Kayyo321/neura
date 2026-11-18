// neura-debug: the debug adapter, which runs its debuggee at tier 0 with the JIT off
// (docs/design/Products030.md, "Binary map").

#include "cli/driver.hpp"
#include "cli/tool.hpp"
#include "tooling/debug.hpp"

#include <iostream>
#include <stdexcept>

namespace {

int command(const std::vector<std::string>& arguments, const std::filesystem::path& self) {
    if (arguments != std::vector<std::string>{"--stdio"}) throw std::runtime_error("usage: neura debug --stdio");
    return neura::tooling::run_debug(std::cin, std::cout, self);
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-debug"), argc, argv);
    if (start.status) return *start.status;
    return neura::cli::sdk_protocol_main(start.arguments, command);
}
