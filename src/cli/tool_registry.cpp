#include "tool.hpp"

#include "neura/version.hpp"

#include <array>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The parts of the tool registry that the dispatcher links: data, the
// version line and the usage text, with no stream or file system code, so
// that `neura` stays small when the C++ runtime is linked into it. (The
// Windows lease takes a std::filesystem::path: the Windows dispatcher uses
// std::filesystem anyway.)

namespace neura::tool {

namespace {


constexpr std::array<std::string_view, 3> compiler_words{"build", "check", "emit-ir"};
constexpr std::array<std::string_view, 3> run_words{"run", "runtime", "serve"};
constexpr std::array<std::string_view, 1> test_words{"test"};
constexpr std::array<std::string_view, 2> inspect_words{"inspect", "inspect-image"};
constexpr std::array<std::string_view, 1> doc_words{"docs"};
constexpr std::array<std::string_view, 1> cbind_words{"c-bindings"};
constexpr std::array<std::string_view, 1> decompile_words{"decompile"};
constexpr std::array<std::string_view, 1> asm_words{"assemble"};
constexpr std::array<std::string_view, 1> disasm_words{"disassemble"};
constexpr std::array<std::string_view, 2> explain_words{"explain", "--explain"};
constexpr std::array<std::string_view, 1> tags_words{"tags"};
constexpr std::array<std::string_view, 1> debug_words{"debug"};
constexpr std::array<std::string_view, 2> lsp_words{"lsp", "rename"};
constexpr std::array<std::string_view, 1> fmt_words{"fmt"};
constexpr std::array<std::string_view, 1> new_words{"new"};
constexpr std::array<std::string_view, 1> package_words{"package"};
constexpr std::array<std::string_view, 1> dev_words{"dev"};
constexpr std::array<std::string_view, 1> doctor_words{"doctor"};
constexpr std::array<std::string_view, 1> update_words{"update"};
constexpr std::array<std::string_view, 1> uninstall_words{"uninstall"};
// The server host serves no `neura` command: `neura serve` (neura-run) starts it.
constexpr std::array<std::string_view, 0> server_host_words{};

// The executables next to `neura`, core first (Products030.md, "Binary map").
constexpr std::array<Tool, 21> tool_table{{
    {"neura-run", Component::Core, run_words},
    {"neura-inspect", Component::Core, inspect_words},
    {"neura-doctor", Component::Core, doctor_words},
    {"neura-update", Component::Core, update_words},
    {"neura-uninstall", Component::Core, uninstall_words},
    {"neura-server-host", Component::Core, server_host_words},
    {"neurac", Component::Sdk, compiler_words},
    {"neura-test", Component::Sdk, test_words},
    {"neura-doc", Component::Sdk, doc_words},
    {"neura-cbind", Component::Sdk, cbind_words},
    {"neura-decompile", Component::Sdk, decompile_words},
    {"neura-asm", Component::Sdk, asm_words},
    {"neura-disasm", Component::Sdk, disasm_words},
    {"neura-explain", Component::Sdk, explain_words},
    {"neura-tags", Component::Sdk, tags_words},
    {"neura-debug", Component::Sdk, debug_words},
    {"neura-lsp", Component::Sdk, lsp_words},
    {"neura-fmt", Component::Sdk, fmt_words},
    {"neura-new", Component::Sdk, new_words},
    {"neura-package", Component::Sdk, package_words},
    {"neura-dev", Component::Sdk, dev_words},
}};

// Every `neura` command that a tool serves. version, help and capabilities
// are built into `neura` itself.
constexpr std::array<Command, 27> command_table{{
    {"check", "neurac", "check", Component::Sdk},
    {"docs", "neura-doc", "docs", Component::Sdk},
    {"emit-ir", "neurac", "emit-ir", Component::Sdk},
    {"build", "neurac", "build", Component::Sdk},
    {"inspect", "neura-inspect", "inspect", Component::Core},
    {"run", "neura-run", "run", Component::Core},
    {"serve", "neura-run", "serve", Component::Core},
    {"test", "neura-test", "test", Component::Sdk},
    {"disassemble", "neura-disasm", "disassemble", Component::Sdk},
    {"assemble", "neura-asm", "assemble", Component::Sdk},
    {"decompile", "neura-decompile", "decompile", Component::Sdk},
    {"c-bindings", "neura-cbind", "c-bindings", Component::Sdk},
    {"doctor", "neura-doctor", "doctor", Component::Core},
    {"update", "neura-update", "update", Component::Core},
    {"uninstall", "neura-uninstall", "uninstall", Component::Core},
    {"explain", "neura-explain", "explain", Component::Sdk},
    {"--explain", "neura-explain", "--explain", Component::Sdk},
    {"tags", "neura-tags", "tags", Component::Sdk},
    {"lsp", "neura-lsp", "lsp", Component::Sdk},
    {"rename", "neura-lsp", "rename", Component::Sdk},
    {"fmt", "neura-fmt", "fmt", Component::Sdk},
    {"new", "neura-new", "new", Component::Sdk},
    {"package", "neura-package", "package", Component::Sdk},
    {"dev", "neura-dev", "dev", Component::Sdk},
    {"debug", "neura-debug", "debug", Component::Sdk},
    {"inspect-image", "neura-inspect", "inspect-image", Component::Core},
    // The seam of the service component: forwarded once neurad is installed.
    {"runtime", "neura-run", "runtime", Component::Service},
}};


} // namespace

std::string_view component_name(Component component) {
    switch (component) {
    case Component::Core: return "Neura core";
    case Component::Sdk: return "Neura-sdk";
    case Component::Service: return "Neura-runtime service";
    }
    return "";
}

std::span<const Command> commands() { return command_table; }
std::span<const Tool> tools() { return tool_table; }

const Command* find_command(std::string_view name) {
    for (const auto& command : commands())
        if (command.command == name) return &command;
    return nullptr;
}

const Tool* find_tool(std::string_view name) {
    for (const auto& tool : tool_table)
        if (tool.name == name) return &tool;
    return nullptr;
}

std::string version_line() {
    return std::string("neura ") + version::full + " (" + version::triple + ", commit " + version::commit + ")";
}

std::string dispatch_token() {
#ifdef NEURA_DISPATCH_VERSION_OVERRIDE
    // Test builds only: a dispatcher that claims another build.
    return std::string(dispatch_prefix) + NEURA_DISPATCH_VERSION_OVERRIDE;
#else
    return std::string(dispatch_prefix) + version_line();
#endif
}

#ifdef _WIN32
namespace {

// Whether the marker at `root` is schema 2, where a missing lease file
// means that the generation was removed.
bool schema_2_root(const std::filesystem::path& root) {
    const HANDLE file = ::CreateFileW((root / L"neura-install.txt").c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char head[15] = {};
    DWORD count = 0;
    const BOOL read = ::ReadFile(file, head, sizeof head, &count, nullptr);
    ::CloseHandle(file);
    return read && count == sizeof head && std::memcmp(head, "neura-install 2", sizeof head) == 0;
}

} // namespace

LeaseHandle take_lease(const std::filesystem::path& executable, bool& removed) {
    removed = false;
    // <root>/versions/<generation>/bin/<tool>, with either separator.
    const auto bin = executable.parent_path();
    const auto generation = bin.parent_path();
    const auto versions = generation.parent_path();
    if (!executable.has_filename() || bin.filename() != L"bin" || !generation.has_filename() ||
        versions.filename() != L"versions" || !versions.has_parent_path()) {
        return no_lease;
    }
    const auto root = versions.parent_path();
    // Not inheritable, so a child (which takes its own lease) does not keep
    // this one; shared for delete, so a remover can delete the name.
    const HANDLE file = ::CreateFileW((root / L"leases" / generation.filename()).c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return no_lease;
        // No lease file: a root that a 0.2 client made runs without one; on
        // a schema 2 root the generation was removed.
        removed = schema_2_root(root);
        return no_lease;
    }
    // The whole file, as every lock of the lifecycle takes it.
    OVERLAPPED whole{};
    if (!::LockFileEx(file, LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD, MAXDWORD, &whole)) {
        if (::GetLastError() != ERROR_LOCK_VIOLATION) {
            // A file system that cannot lock: run without a lease.
            ::CloseHandle(file);
            return no_lease;
        }
        // Held exclusively: a command is removing the generation, or only
        // probing whether it is in use. Wait, then tell by the file's state.
        whole = OVERLAPPED{};
        if (!::LockFileEx(file, 0, 0, MAXDWORD, MAXDWORD, &whole)) {
            ::CloseHandle(file);
            return no_lease;
        }
    }
    FILE_STANDARD_INFO info{};
    if (!::GetFileInformationByHandleEx(file, FileStandardInfo, &info, sizeof info) || info.DeletePending ||
        info.NumberOfLinks == 0) {
        removed = true;
        ::CloseHandle(file);
        return no_lease;
    }
    return reinterpret_cast<LeaseHandle>(file);
}
#else
LeaseHandle take_lease(const std::filesystem::path& executable, bool& removed) {
    return take_lease(executable.c_str(), removed);
}

int take_lease(const char* executable, bool& removed) {
    removed = false;
    // <root>/versions/<generation>/bin/<tool>
    const std::string_view path(executable ? executable : "");
    const auto tool_slash = path.rfind('/');
    if (tool_slash == std::string_view::npos || tool_slash < 4) return -1;
    const auto bin_slash = path.rfind('/', tool_slash - 1);
    if (bin_slash == std::string_view::npos || path.substr(bin_slash + 1, tool_slash - bin_slash - 1) != "bin") return -1;
    const auto generation_slash = path.rfind('/', bin_slash - 1);
    if (generation_slash == std::string_view::npos || generation_slash == 0) return -1;
    const auto versions_slash = path.rfind('/', generation_slash - 1);
    if (versions_slash == std::string_view::npos || path.substr(versions_slash + 1, generation_slash - versions_slash - 1) != "versions") {
        return -1;
    }
    const auto root = path.substr(0, versions_slash);
    const auto generation = path.substr(generation_slash + 1, bin_slash - generation_slash - 1);
    std::string lease(root);
    lease += "/leases/";
    lease += generation;
    const int descriptor = ::open(lease.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        if (errno != ENOENT) return -1;
        // No lease file: a root that a 0.2 client made runs without one; on
        // a schema 2 root the generation was removed.
        std::string marker(root);
        marker += "/neura-install.txt";
        const int file = ::open(marker.c_str(), O_RDONLY | O_CLOEXEC);
        if (file < 0) return -1;
        char head[16] = {};
        const auto count = ::read(file, head, sizeof head - 1);
        ::close(file);
        removed = count >= 16 - 1 && std::memcmp(head, "neura-install 2", 15) == 0;
        return -1;
    }
    if (::flock(descriptor, LOCK_SH | LOCK_NB) != 0) {
        if (errno != EWOULDBLOCK) {
            // A file system that cannot lock: run without a lease.
            ::close(descriptor);
            return -1;
        }
        // Held exclusively: a command is removing the generation, or only
        // probing whether it is in use. Wait, then tell by the link count.
        while (::flock(descriptor, LOCK_SH) != 0) {
            if (errno != EINTR) {
                ::close(descriptor);
                return -1;
            }
        }
    }
    struct stat info {};
    if (::fstat(descriptor, &info) != 0 || info.st_nlink == 0) {
        removed = true;
        ::close(descriptor);
        return -1;
    }
    return descriptor;
}
#endif

std::string executable_file(std::string_view name) {
#ifdef _WIN32
    return std::string(name) + ".exe";
#else
    return std::string(name);
#endif
}

const char* usage_text() {
    return "usage: neura lsp --stdio\n"
           "       neura debug --stdio\n"
           "       neura fmt [--check | --write] [--tag-style=dot|hash|keep] [--stdin --stdin-filepath <file>] <file.nra>...\n"
           "       neura capabilities --json\n"
           "       neura rename [-I source-root] <file.nra> <line>:<column> <new-name>\n"
           "       neura check [-I source-root] [--task-safety | --fix | --show-derived[=Name]] <file.nra>...\n"
           "       neura docs <file.nra>... [-o <file.json>]\n"
           "       neura docs --doxygen <file.nra>\n"
           "       neura emit-ir [-I source-root] [--no-optimize] <file.nra>... [-o <file.nir>]\n"
           "       neura emit-ir [--no-optimize] <file.nir>\n"
           "       neura build [-I source-root] [--no-optimize] [--no-source-map] [--no-lift-hints] <file.nra>... -o <file.nvr>\n"
           "       neura build [--no-optimize] [--no-source-map] [--no-lift-hints] <file.nir> -o <file.nvr>\n"
           "       neura build --project <neura.toml> --target <name> [--out <directory>] [--update-lock]\n"
           "       neura dev --project <neura.toml> --target <name> [--port <n>] [-- host arguments...]\n"
           "       neura package --project <neura.toml> --target <name> [--format tar.gz|deb|zip] [-o <file>]\n"
           "       neura new <directory> --template fullstack|web|server|desktop [--name <name>] [--id <id>]\n"
           "       neura build --native [policy] [--static] [--target <triple>] [--launcher <file>]\n"
           "                  <file.nra>... | <file.nir> | <file.nvr> -o <file>\n"
           "       neura inspect <file> [-o <file.nvr>]\n"
           "       neura run [--max-steps N] [--max-frames N] [--max-heap-cells N] [--max-stack-cells N] [--stats] [--profile] [--tier=0|1|2] "
           "[--jit=off|on|auto] [--tier-stats] [capabilities] "
           "[--record <file> | --replay <file>] [--each <lists>] [--gpu=auto|always|off] [--no-gpu] "
           "[--require-gpu] [--gpu-software] [--gpu-verify] <file.nvr> [-- args...]\n"
           "       neura run <bundle directory> [-- host arguments...]\n"
           "       neura run --runtime=off|auto|required [--detach --stdout=<file> --stderr=<file> [--stdin=<file>]]\n"
           "                 [--runtime-report=<file>] [run options] <file.nvr> [-- args...]\n"
           "       neura runtime start | status [--json] | ps [--json] [--all] | inspect <id> [--json]\n"
           "                 | stop <id> [--grace=<seconds>] | shutdown [--terminate] [--timeout=<seconds>]\n"
           "                 | enable | disable | cache clear\n"
           "       neura test [-I source-root] [--no-optimize] [--max-steps N] [--max-frames N]\n"
           "                  [--list --format=json] [--filter <exact-id>] [--reporter=json]\n"
           "                  [--workspace-root <directory>]\n"
           "                  [--max-heap-cells N] [--max-stack-cells N] [--stats] [--tier=0|1|2] [--jit=off|on|auto]\n"
           "                  [--tier-stats] [capabilities]\n"
           "                  [--gpu=auto|always|off] [--no-gpu]\n"
           "                  [--require-gpu] [--gpu-software] [--gpu-verify] <file.nra>...\n"
           "       neura serve [-I source-root] <file.nra>... | <file.nvr> [neura-server-host options]\n"
           "       neura disassemble <file.nvr> [-o <file.nvra>]\n"
           "       neura assemble <file.nvra> -o <file.nvr>\n"
           "       neura decompile [--no-provenance] <file.nvr> [-o <file>]\n"
           "       neura c-bindings [--linked] [c-options] <header.h>\n"
           "       neura explain <code> | --explain <code> | explain --list\n"
           "       neura tags [<Name>]\n"
           "       neura --version | version\n"
           "       neura --help | help\n"
           "       neura doctor\n"
           "       neura update [--check] [--nightly] [--source <https-url-or-directory>] [--require-attestation]\n"
           "       neura update --components sdk|runtime|sdk,service [--terminate] [--source <https-url-or-directory>]\n"
           "       neura update --rollback | --repair | --migrate-install\n"
           "       neura uninstall [--dry-run] [--yes] [--component sdk|service] [--terminate]\n"
           "\n"
           "doctor, update, and uninstall manage an installed neura and are described in docs/Install.md.\n"
           "update and uninstall exit with status 3 for an install that neura did not make, and update --check\n"
           "exits with status 4 when a newer version is available.\n"
           "\n"
           "capabilities: run and test, and the policy that build --native records, grant files, network,\n"
           "environment, clock, entropy, and native code by default. Raw IP sockets and HTTP clients need explicit grants. A denied\n"
           "service fails with error 14 (CapabilityDenied). Options apply in command-line order.\n"
           "  --deny-fs                files and directories\n"
           "  --deny-net               network connections and listeners\n"
           "  --allow-raw, --deny-raw  grant or deny raw IP sockets, which also need network access\n"
           "  --allow-http-client, --deny-http-client  grant or deny outgoing HTTP requests\n"
           "  --deny-env               environment variables\n"
           "  --deny-clock             wall and monotonic clocks and sleeping\n"
           "  --deny-entropy           operating-system entropy\n"
           "  --deny-native            native libraries, memory, and calls\n"
           "  --deny-all               every group above\n"
           "  --allow-fs[=prefix,...]  grant files again: every path, or only below the prefixes\n"
           "  --allow-net, --allow-env, --allow-clock, --allow-entropy, --allow-native\n"
           "                           grant that group again after a --deny option (native code is unsafe)\n"
           "  Run an image you do not trust with --deny-all, plus the --allow options it needs.\n"
           "  --sql-pool <name>=<url>  (run, test) an SQL pool for Std.Data.Sql: sqlite:<path>, sqlite::memory:,\n"
           "                           or postgresql://...; <name>.migrations=<dir> applies a migration directory,\n"
           "                           <name>.password_env=<variable> reads the password\n"
           "  --record <file>          (run) write every host request and response to a trace\n"
           "  --profile                (run) print the steps each function took\n"
           "  --tier=0|1|2             the highest execution tier: 0 evaluates every graph by reference, 1 also\n"
           "                           runs proven lifted blocks natively, 2 (the default) also runs regions of\n"
           "                           them without returning to the dispatcher; the result is the same\n"
           "  --jit=off|on|auto        tier 2 runs its regions as native code: on fails the run when this platform\n"
           "                           cannot, auto (the default) uses it where it is enabled by default, off\n"
           "                           interprets them (and, with it, runs no native tier 3 programs); the result\n"
           "                           is the same (--deny-native does not apply)\n"
           "  --tier-stats             print the blocks lifted, the regions formed, the steps each tier ran, and\n"
           "                           where the wall time went\n"
           "  --replay <file>          (run) answer host requests from a trace instead of services; a trace of a\n"
           "                           concurrent run carries its own quantum and limits, which replace the options\n"
           "\n"
           "graph execution: run and test choose between the CPU and a GPU by predicted speed.\n"
           "  --gpu=auto               decide automatically (default)\n"
           "  --gpu=always             evaluate every eligible graph on a GPU when one is usable\n"
           "  --gpu=off, --no-gpu      evaluate graphs on the CPU\n"
           "  --require-gpu            --gpu=always, and fail unless a GPU device is available\n"
           "  --gpu-software           also accept software Vulkan implementations\n"
           "  --gpu-verify             check every GPU evaluation against the CPU\n"
           "  --each <lists>           (run) run the image once per line of <lists>; each line is one argument list\n"
           "  --max-tasks N            bound the number of live tasks in a concurrent image\n"
           "  --task-quantum N         polls per turn for a concurrent image\n"
           "  --max-queued-messages N  bound the values buffered in all channels\n"
           "  --max-pending-requests N bound the overlapping host requests in flight\n"
           "  --parallel-workers N     threads that evaluate the items of a parallel map (default 1)\n"
           "  --task-dump[=text|json]  print every task when one traps; SIGQUIT prints a dump at any time\n"
           "  --virtual-time           timers, deadlines, and the clock follow a clock that moves only when\n"
           "                           every task is parked, so a timeout takes no real time\n"
           "  --schedules N            (test) run each test under up to N legal task schedules\n"
           "  --schedule-seed S        (test) the seed of the explored schedules (default 0)\n"
           "  --save-schedule FILE     (test) write the first failing schedule to FILE\n"
           "  --schedule FILE          (test) run the test of a saved schedule under exactly that schedule\n"
           "  --trace-events FILE      write one JSON line per scheduler event (run only)\n"
           "  --trace-wall-time        add wall-clock times to --trace-events\n"
           "\n"
           "c-options: check, emit-ir, build, test, and c-bindings accept these for import:c(...)\n"
           "  --c-include <dir>        search a directory for C headers\n"
           "  --c-define <name[=value]> define a macro while reading C headers\n"
           "  --libclang <file>        the libclang library to load (default: NEURA_LIBCLANG, then a search)\n"
           "  --cc <compiler>          the C compiler that links the native library (default: $CC, then cc)\n"
           "  --c-link-arg <arg>       pass an argument to that link\n"
           "\n"
           "diagnostics: every command accepts the first two; the others apply to check, emit-ir, build, and test\n"
           "  --error-format=human|short|json\n"
           "                           human (default) shows source snippets; short is one stable line\n"
           "                           per diagnostic, file:line:column: error[CODE]: message; json is one\n"
           "                           object per line\n"
           "  --color=auto|always|never\n"
           "                           color human output; auto colors a terminal unless NO_COLOR is set\n"
           "                           or TERM is dumb\n"
           "  --max-errors N           stop after N syntax errors (default 20); the lexer and the parser\n"
           "                           recover and report several, and any of them stops compilation\n"
           "  -W<name>, -Wno-<name>    enable or disable the warning <name>\n"
           "  -Werror                  report every warning as an error\n"
           "  --warn-library           also report warnings in the standard library\n"
           "  --fix                    (check) apply machine-applicable suggestions to the source files\n";
}

} // namespace neura::tool
