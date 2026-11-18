// neura-run: direct execution of an image, with no compiler
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/serve.hpp"
#include "cli/tool.hpp"
#ifndef _WIN32
#include "service/service.hpp"
#endif

#include <iostream>

namespace {

using namespace neura::cli;

int command(const Options& options) {
    switch (options.command) {
    case Command::Run:
        return run(options);
    default:
        return 1;
    }
}

#ifndef _WIN32
// `neura runtime ...` (docs/design/Service030.md): the service's commands.
int runtime(const std::vector<std::string>& arguments, const std::filesystem::path& self) {
    try {
        return neura::service::runtime_command(arguments, self);
    } catch (const RuntimeError& error) {
        std::cerr << "neura: runtime: " << error.what() << '\n';
        return error.status();
    }
}
#endif

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-run"), argc, argv);
    if (start.status) return *start.status;
#ifndef _WIN32
    if (start.arguments.size() > 1 && start.arguments[1] == "runtime") {
        return core_protocol_main(start.arguments, runtime);
    }
#endif
    // `neura serve` (docs/runtime/ServerHost.md) builds through `neura` and
    // replaces this process with neura-server-host.
    if (start.arguments.size() > 1 && start.arguments[1] == "serve") {
        auto argv = neura::tool::argv_of(start.arguments);
        return neura::cli::run_serve(static_cast<int>(start.arguments.size()), argv.data());
    }
    return core_options_main(std::move(start.arguments), command);
}
