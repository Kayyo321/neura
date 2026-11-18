#pragma once

// The common start of every Neura tool after its command word is known
// (docs/design/Products030.md): read the options or the protocol
// arguments, run the command, and report an error with the `neura:` prefix
// of 0.2, so a command prints the same bytes through `neura` and directly.
//
// The SDK forms render source diagnostics with snippets; the core forms,
// which the execution core links, have no diagnostic renderer and print a
// failure's message, since no core command produces source diagnostics.

#include "options.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace neura::cli {

// `arguments[1]` is the command word; the rest are the command's options.
using OptionsCommand = int (*)(const Options& options);
int sdk_options_main(std::vector<std::string> arguments, OptionsCommand command);
int core_options_main(std::vector<std::string> arguments, OptionsCommand command);

// Protocol commands own standard output, so nothing else is printed there.
// The command receives the arguments after the command word and the real
// path of the running executable.
using ProtocolCommand = int (*)(const std::vector<std::string>& arguments, const std::filesystem::path& self);
int sdk_protocol_main(const std::vector<std::string>& arguments, ProtocolCommand command);
int core_protocol_main(const std::vector<std::string>& arguments, ProtocolCommand command);

// Commands after which an update notice may appear.
bool shows_update_notice(Command command);

} // namespace neura::cli
