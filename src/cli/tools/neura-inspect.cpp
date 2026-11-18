// neura-inspect: a native executable's container (`inspect`) and the
// structured image inspector (`inspect-image`), with no compiler
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/tool.hpp"
#include "tooling/inspect.hpp"

#include <string_view>

namespace {

int image(const std::vector<std::string>& arguments, const std::filesystem::path&) {
    return neura::tooling::run_image_inspector(arguments);
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-inspect"), argc, argv);
    if (start.status) return *start.status;
    if (start.arguments[1] == "inspect-image") return neura::cli::core_protocol_main(start.arguments, image);
    return neura::cli::core_options_main(std::move(start.arguments), neura::cli::inspect);
}
