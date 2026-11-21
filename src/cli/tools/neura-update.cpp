// neura-update: updates an archive install
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/lifecycle.hpp"
#include "cli/tool.hpp"
#if !defined(_WIN32) || defined(NEURA_HAS_SERVICE)
#include "service/service.hpp"
#endif

#include <filesystem>
#include <system_error>

namespace {

using namespace neura::cli;

// The lifecycle commands manage the installation that `neura` belongs to,
// and name it by the dispatcher next to this tool, as 0.2 named its single
// executable; without one, by this tool itself.
std::filesystem::path installation(const std::filesystem::path& self) {
    std::error_code error;
    const auto neura = self.parent_path() / neura::tool::executable_file("neura");
    return !self.empty() && std::filesystem::is_regular_file(neura, error) ? neura : self;
}

int command(const Options& options) {
    switch (options.command) {
    case Command::Update:
        return neura::lifecycle::update(installation(options.executable), options.update);
    default:
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-update"), argc, argv);
    if (start.status) return *start.status;
#if !defined(_WIN32) || defined(NEURA_HAS_SERVICE)
    // The service's control client, for the applications that run and for
    // Retire (docs/design/Lifecycle030.md, decision 17).
    neura::service::install_lifecycle_hooks();
#endif
    return core_options_main(std::move(start.arguments), command);
}
