#pragma once

#include <filesystem>
#include <iosfwd>

namespace neura::tooling {

// Debug Adapter Protocol on Content-Length framed streams. Only this
// protocol writes to output; debuggee console output is emitted as events.
int run_debug(std::istream& input, std::ostream& output,
              const std::filesystem::path& executable);

} // namespace neura::tooling
