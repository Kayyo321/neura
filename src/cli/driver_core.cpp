#include "driver.hpp"

#include "lifecycle.hpp"

#include <exception>
#include <iostream>

namespace neura::cli {

// Commands after which an update notice may appear: the ones that do their
// work and exit. run and test execute user programs, and the lifecycle
// commands manage the install themselves.
bool shows_update_notice(Command command) {
    switch (command) {
    case Command::Check: case Command::Docs: case Command::EmitIr: case Command::Build: case Command::Inspect:
    case Command::Disassemble: case Command::Assemble: case Command::Decompile: case Command::CBindings:
        return true;
    default:
        return false;
    }
}

int core_options_main(std::vector<std::string> arguments, OptionsCommand command) {
    auto argv = neura::tool::argv_of(arguments);
    try {
        const auto options = read_options(static_cast<int>(arguments.size()), argv.data());
        const int status = command(options);
        if (status == 0 && shows_update_notice(options.command)) neura::lifecycle::update_notice(options.executable);
        return status;
    } catch (const RuntimeError& error) {
        std::cerr << "neura: runtime: " << error.what() << '\n';
        return error.status();
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}

int core_protocol_main(const std::vector<std::string>& arguments, ProtocolCommand command) {
    try {
        const std::vector<std::string> rest(arguments.begin() + 2, arguments.end());
        return command(rest, executable_path(arguments.front()));
    } catch (const std::exception& error) {
        std::cerr << "neura: " << error.what() << '\n';
        return 1;
    }
}

} // namespace neura::cli
