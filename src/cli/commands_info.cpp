#include "commands.hpp"

#include "explain.hpp"
#include "neura-ir/tags.hpp"
#include "util/suggest.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace neura::cli {

int tags(const Options& options) {
    if (options.explain_code.empty()) {
        neura::print_tag_catalogue(std::cout);
        return 0;
    }
    if (const auto* tag = neura::find_tag(options.explain_code)) {
        neura::print_tag(std::cout, *tag);
        return 0;
    }
    std::vector<std::string> names;
    for (const auto& tag : neura::tag_registry()) names.emplace_back(tag.name);
    std::string message = "'" + options.explain_code + "' is not a tag";
    if (const auto close = neura::closest_name(options.explain_code, names))
        message += "; did you mean " + *close + "?";
    else message += "; `neura tags` lists the tags";
    throw std::runtime_error(message);
}

int explain(const Options& options) {
    if (options.explain_list) neura::cli::explain_list(std::cout);
    else neura::cli::explain(options.explain_code, std::cout);
    return 0;
}

} // namespace neura::cli
