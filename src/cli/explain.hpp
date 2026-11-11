#pragma once

#include <ostream>
#include <string_view>

namespace neura::cli {

// `neura explain <CODE>`: prints the explain page of a diagnostic code, the
// Markdown of docs/diagnostics/<CODE>.md embedded at build time. Accepts the
// code in either case and without its letter (0401 means E0401). Throws
// std::runtime_error for a code that is not registered, naming the closest
// registered code when one is close.
void explain(std::string_view code, std::ostream& out);

// `neura explain --list`: every registered code and its title, one per line.
void explain_list(std::ostream& out);

} // namespace neura::cli
