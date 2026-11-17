#include "commands.hpp"
#include "diagnostic_render.hpp"
#include "lifecycle.hpp"
#include "options.hpp"
#include "util/diagnostic.hpp"
#include "util/source_map.hpp"
#include "tooling/lsp.hpp"
#include "tooling/debug.hpp"
#include "tooling/formatter.hpp"
#include "tooling/json.hpp"
#include "tooling/inspect.hpp"
#include "neura/version.hpp"

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using namespace neura::cli;

// Whether human diagnostics are colored (Tooling.md, Diagnostics).
bool use_color(neura::cli::ColorChoice choice) {
    if (choice != neura::cli::ColorChoice::Auto) return choice == neura::cli::ColorChoice::Always;
    if (const char* no_color = std::getenv("NO_COLOR"); no_color && *no_color) return false;
    if (const char* term = std::getenv("TERM"); term && std::string_view(term) == "dumb") return false;
#ifdef _WIN32
    return false;
#else
    return ::isatty(2) == 1;
#endif
}

// The terminal width: the terminal's own, then COLUMNS, then 100.
std::size_t terminal_width() {
#ifndef _WIN32
    winsize size{};
    if (::ioctl(2, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) return size.ws_col;
#endif
    if (const char* columns = std::getenv("COLUMNS"); columns && *columns) {
        std::size_t value = 0;
        const std::string_view text(columns);
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error == std::errc{} && end == text.data() + text.size() && value > 0) return value;
    }
    return 100;
}

// Commands after which an update notice may appear: the ones that do their
// work and exit. run and test execute user programs, and the lifecycle
// commands manage the install themselves.
bool shows_update_notice(Command command) {
    switch (command) {
    case Command::Check: case Command::Docs: case Command::EmitIr: case Command::Build: case Command::Inspect:
    case Command::Disassemble: case Command::Assemble: case Command::Decompile: case Command::CBindings:
        return true;
    default:
        return false;
    }
}

int dispatch(const Options& options) {
    switch (options.command) {
    case Command::Version:
        std::cout << neura::lifecycle::version_line() << "\n";
        return 0;
    case Command::Help:
        usage(std::cout);
        return 0;
    case Command::Doctor:
        return neura::lifecycle::doctor(options.executable, {options.library});
    case Command::Update:
        return neura::lifecycle::update(options.executable, options.update);
    case Command::Uninstall:
        return neura::lifecycle::uninstall(options.executable, options.uninstall);
    case Command::Tags:
        return tags(options);
    case Command::Explain:
        return explain(options);
    case Command::Check:
        return check(options);
    case Command::Docs:
        return docs(options);
    case Command::EmitIr:
        return emit_ir(options);
    case Command::Build:
        return build(options);
    case Command::Inspect:
        return inspect(options);
    case Command::Run:
        return run(options);
    case Command::Test:
        return run_tests(options);
    case Command::Disassemble:
        return disassemble(options);
    case Command::Assemble:
        return assemble(options);
    case Command::Decompile:
        return decompile(options);
    case Command::CBindings:
        return c_bindings(options);
    }
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(std::cerr);
        return 1;
    }
    // Protocol commands own stdout. Dispatch before ordinary option/diagnostic
    // handling so no terminal notices can corrupt their transport.
    const std::string_view tooling_command(argv[1]);
    if (tooling_command == "lsp" || tooling_command == "fmt" || tooling_command == "debug" || tooling_command == "capabilities" || tooling_command == "inspect-image") {
        try {
            std::vector<std::string> arguments;
            for (int i = 2; i < argc; ++i) arguments.emplace_back(argv[i]);
            if (tooling_command == "lsp") return neura::tooling::run_lsp(arguments);
            if (tooling_command == "fmt") return neura::tooling::run_formatter(arguments);
            if (tooling_command == "inspect-image") return neura::tooling::run_image_inspector(arguments);
            if (tooling_command == "debug") {
                if (arguments != std::vector<std::string>{"--stdio"}) throw std::runtime_error("usage: neura debug --stdio");
                return neura::tooling::run_debug(std::cin, std::cout, fs::absolute(argv[0]));
            }
            if (arguments != std::vector<std::string>{"--json"}) throw std::runtime_error("usage: neura capabilities --json");
            const neura::tooling::Json info = neura::tooling::Json::Object{
                {"schemaVersion", 1}, {"version", neura::version::full},
                {"features", neura::tooling::Json::Object{{"lsp", true}, {"formatter", true}, {"debugger", true},
                    {"testDiscovery", true}, {"testFilter", true}, {"testReporter", true}, {"coverage", true}, {"testCoverage", true}, {"documentation", true}, {"imageInspection", true}}}
            };
            std::cout << info.dump() << '\n'; return 0;
        } catch (const neura::Diagnostic& error) {
            neura::cli::DiagnosticConfig config;
            neura::cli::DiagnosticPrinter print(config, {use_color(config.color), terminal_width()}, neura::SourceMap::global());
            print.print(std::cerr, error);
            for (const auto& next : error.following()) print.print(std::cerr, next);
            print.finish(std::cerr);
            return 1;
        } catch (const std::exception& error) { std::cerr << "neura: " << error.what() << '\n'; return 1; }
    }
    // Warnings collect here while a command runs and print before its error
    // or at its end.
    neura::DiagnosticSink sink;
    const neura::DiagnosticSinkScope sink_scope(sink);
    neura::cli::DiagnosticConfig diagnostics;
    const auto printer = [&] {
        return neura::cli::DiagnosticPrinter(diagnostics, {use_color(diagnostics.color), terminal_width()},
                                             neura::SourceMap::global());
    };
    try {
        const auto options = read_options(argc, argv);
        diagnostics = options.diagnostics;
        int status = dispatch(options);
        if (!sink.entries().empty()) {
            auto print = printer();
            if (print.warnings(std::cerr, sink.entries()) > 0 && diagnostics.warnings_are_errors) status = 1;
        }
        if (status == 0 && shows_update_notice(options.command)) neura::lifecycle::update_notice(options.executable);
        return status;
    } catch (const neura::Diagnostic& error) {
        auto print = printer();
        print.warnings(std::cerr, sink.entries());
        print.print(std::cerr, error);
        for (const auto& next : error.following()) print.print(std::cerr, next);
        print.finish(std::cerr);
    } catch (const std::exception& error) {
        std::cerr << "neura: error: " << error.what() << '\n';
    }
    return 1;
}
