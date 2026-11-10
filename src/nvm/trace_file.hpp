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
inline constexpr int trace_file_version = 1;

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
