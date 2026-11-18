#include "commands.hpp"

#include "explain.hpp"

#include <iostream>

namespace neura::cli {

int explain(const Options& options) {
    if (options.explain_list) neura::cli::explain_list(std::cout);
    else neura::cli::explain(options.explain_code, std::cout);
    return 0;
}

} // namespace neura::cli
