#pragma once

#include "trace.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace neura::nvm {

// The text form of a recorded trace, written by `neura run --record` and
// read by `neura run --replay`. Version 1:
//
//   neura-trace 1
//   request <import> <version> <arguments>
//   ok <value> | error <value> | exit <status>
//   ...
//   outcome completed <status>
//   outcome trap <kind> <reason> <message>
//
// Each request line is followed by the line of its completion, in request
// order. A value is a decimal number (the slot bits of a Boolean, integer, or
// float, as an unsigned 64-bit number), a quoted byte string (text, bytes, or
// a uint8 list), or a bracketed, comma-separated list of values (a tuple or
// another list). Arguments are always a list. In a quoted string, bytes
// outside printable ASCII and the characters `"` and `\` are escaped as \xHH,
// \n, \r, \t, \", or \\. A value with no bits, no bytes, and no elements
// prints as 0; each of the three forms reads back as the same value.
// `exit` is the terminal completion of neura.process.exit. The trap kind is
// one of the names of trace_trap_kind_name, and the message is a quoted
// string, empty unless the trap carries a program failure message. Lines
// starting with `#` and blank lines are ignored. The writer emits one space
// between fields, `, ` between list elements, and LF line ends, so the same
// trace always produces the same bytes.
// Version 2 adds callbacks. Version 3 is written only for an image with
// tasks (docs/runtime/NVM.md, "Traces of images with tasks"):
//
//   neura-trace 3
//   settings quantum <n> max-tasks <n> max-queued-messages <n> max-pending-requests <n>
//   request <import> <version> <arguments> task <id> tick <n> [overlapping | cancellable]
//   ok <value> | error <value> | exit <status> | abandoned
//   ...
//   deliver <tick> <request> [cancelled]
//   clock <tick> <nanoseconds>
//   outcome ...
//
// A request is numbered by its position among the request lines, from
// zero. Its tick is the number of scheduling decisions made when it was
// issued. `abandoned` replaces the completion of an overlapping request that the
// run ended without delivering. The deliver and clock lines follow the last
// request, in the order they happened. A reader accepts 1, 2, and 3.
inline constexpr int trace_callbacks_version = 2;
inline constexpr int trace_tasks_version = 3;
inline constexpr int trace_file_version = trace_tasks_version;

class TraceFormatError : public std::runtime_error {
public:
    TraceFormatError(std::size_t line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message), line(line) {}
    std::size_t line;
};

std::string write_trace(const Trace& trace);
// Throws TraceFormatError for malformed text.
Trace read_trace(std::string_view text);

// The token for a trap kind in a trace file, such as `step-limit`.
std::string_view trace_trap_kind_name(TrapKind kind);

// The text of one value, as it appears in a trace file.
std::string format_trace_value(const HostValue& value);

} // namespace neura::nvm
