// neura-doctor: checks the installation and the components next to it
// (docs/design/Products030.md, "Binary map").

#include "cli/commands.hpp"
#include "cli/driver.hpp"
#include "cli/tool.hpp"
#include "cli/lifecycle.hpp"

namespace {

using namespace neura::cli;

int command(const Options& options) {
    switch (options.command) {
    case Command::Doctor: {
        neura::lifecycle::DoctorOptions doctor{options.library, {}};
#ifdef NEURA_DOCTOR_PROBES_LIBCLANG
        doctor.probe_libclang = [] { return neura::probe_libclang(neura::CImportOptions{}); };
#endif
        return neura::lifecycle::doctor(options.executable, doctor);
    }
    default:
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    auto start = neura::tool::start(*neura::tool::find_tool("neura-doctor"), argc, argv);
    if (start.status) return *start.status;
    return core_options_main(std::move(start.arguments), command);
}
