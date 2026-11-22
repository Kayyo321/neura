// neurad: the runtime service of one user and installation
// (docs/design/Service030.md, docs/runtime/Service.md). `neurad --foreground
// --root ROOT` is the supervisor; `neurad --worker` is the private entry of
// its workers. Users reach it through `neura run --runtime=...` and
// `neura runtime ...`, never directly.

#include "cli/tool.hpp"
#include "service/service.hpp"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include "platform/console.hpp"
#include "platform/windows.hpp"
#endif

int main(int argc, char** argv) {
    const std::vector<std::string> arguments(argv + 1, argv + argc);
#ifdef _WIN32
    // What tool::start sets for every tool (W10, W19): no error dialogs and
    // DLLs from the system directories only. The streams are set up below,
    // the worker's once its launch gives it the caller's.
    neura::platform::set_unattended_error_mode();
    ::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (arguments.empty() || arguments.front() != "--worker") {
        neura::platform::set_binary_stdio();
        neura::platform::install_console_streams();
    }
#endif
    // The worker touches no stream before its launch gives it the caller's.
    if (!arguments.empty() && arguments.front() == "--worker") {
        return neura::service::worker_main({arguments.begin() + 1, arguments.end()});
    }
    if (arguments.size() == 1 && arguments.front() == "--version") {
        std::printf("%s\n", neura::tool::version_line().c_str());
        return 0;
    }
    if (arguments.size() == 1 && arguments.front() == "--build") {
        std::printf("%s\n", neura::service::own_build().c_str());
        return 0;
    }
    if (!arguments.empty() && arguments.front() == "--foreground") {
        return neura::service::supervisor_main(arguments, neura::tool::executable_path(argv[0]));
    }
    std::fprintf(stderr,
                 "usage: neurad --foreground --root ROOT\n"
                 "neurad is the runtime service of Neura; use 'neura run --runtime=auto' and 'neura runtime'.\n");
    return 1;
}
