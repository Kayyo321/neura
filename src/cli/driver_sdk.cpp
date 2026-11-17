#include "driver.hpp"

#include "diagnostic_render.hpp"
#include "lifecycle.hpp"
#include "util/diagnostic.hpp"
#include "util/source_map.hpp"

#include <charconv>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace neura::cli {

namespace {

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

} // namespace

int sdk_protocol_main(const std::vector<std::string>& arguments, ProtocolCommand command) {
    try {
        const std::vector<std::string> rest(arguments.begin() + 2, arguments.end());
        return command(rest, executable_path(arguments.front()));
    } catch (const neura::Diagnostic& error) {
        neura::cli::DiagnosticConfig config;
        neura::cli::DiagnosticPrinter print(config, {use_color(config.color), terminal_width()}, neura::SourceMap::global());
        print.print(std::cerr, error);
        for (const auto& next : error.following()) print.print(std::cerr, next);
        print.finish(std::cerr);
        return 1;
    } catch (const std::exception& error) { std::cerr << "neura: " << error.what() << '\n'; return 1; }
}

int sdk_options_main(std::vector<std::string> arguments, OptionsCommand command) {
    auto argv = neura::tool::argv_of(arguments);
    const int argc = static_cast<int>(arguments.size());
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
        const auto options = read_options(argc, argv.data());
        diagnostics = options.diagnostics;
        int status = command(options);
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

} // namespace neura::cli
