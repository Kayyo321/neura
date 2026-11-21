#pragma once

// Standard streams, terminals and console events.
//
// The byte contract of stdin, stdout and stderr is the same on every host
// (W17): no CRLF translation. On Windows a real console is written as UTF-16
// through WriteConsoleW and its code page is never changed; pipes and files
// get the bytes unchanged.

#include <optional>

namespace neura::platform {

// Puts the C runtime's descriptors 1 and 2, and 0 unless it is a console, in
// binary mode (_setmode _O_BINARY), so "\n" stays one byte. Console stdin
// stays in text mode, which turns the console's "\r\n" line ending into "\n"
// as a POSIX terminal delivers it. A no-op on POSIX.
void set_binary_stdio() noexcept;

// Whether descriptor `fd` is an interactive terminal: GetConsoleMode on its
// handle on Windows (not _isatty, which is also true for NUL), isatty
// elsewhere.
bool is_terminal(int fd) noexcept;

// Turns on ANSI escape processing for a console (ENABLE_VIRTUAL_TERMINAL_
// PROCESSING) and reports whether colors can be written to `fd`. On POSIX it
// is is_terminal(fd).
bool enable_terminal_colors(int fd) noexcept;

// The width of the terminal at `fd` in columns, if it is one.
std::optional<int> terminal_columns(int fd) noexcept;

// When stdout or stderr is a real Windows console, makes std::cout, std::cerr
// and std::clog write through a buffer that converts complete UTF-8 sequences
// to UTF-16 for WriteConsoleW (a sequence split across writes is held until
// it completes; invalid bytes are shown as U+FFFD). Streams that are pipes or
// files keep writing bytes. Call it once, early, before writing. A no-op on
// POSIX.
void install_console_streams();

// Makes crashes and failures end the process instead of waiting on a dialog:
// SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
// SEM_NOOPENFILEERRORBOX), _set_abort_behavior(0, _CALL_REPORTFAULT) and, in
// debug CRT builds, CRT reports sent to stderr. A no-op on POSIX.
void set_unattended_error_mode() noexcept;

// Console events (W10). On POSIX they correspond to SIGINT (ctrl_c), SIGQUIT
// (ctrl_break, the counterpart of Ctrl+\) and SIGHUP (close, logoff,
// shutdown).
enum class ConsoleEvent { ctrl_c, ctrl_break, close, logoff, shutdown };

#ifdef _WIN32
// Handles console events with `handler`, which runs on a thread the system
// creates and returns true when it handled the event (false passes it to the
// next handler, by default ExitProcess). For close, logoff and shutdown the
// system ends the process when the handler returns. Replaces the previous
// handler of this function; nullptr removes it. POSIX code uses sigaction.
using ConsoleHandler = bool (*)(ConsoleEvent event);
void on_console_event(ConsoleHandler handler) noexcept;

// Handles every console event by doing nothing, for a process that waits for
// a child that receives the same events. Unlike SetConsoleCtrlHandler(nullptr,
// TRUE), children do not inherit it. For close, logoff and shutdown the
// handler never returns, so this process stays until its child ends or the
// system's timeout ends both, and the child's handler gets its time.
void ignore_console_events() noexcept;
#endif

} // namespace neura::platform
