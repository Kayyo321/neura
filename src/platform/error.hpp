#pragma once

// Operating-system error codes and their messages as UTF-8.

#include <string>
#include <string_view>
#include <system_error>

namespace neura::platform {

// GetLastError() on Windows, errno elsewhere.
int last_error() noexcept;

// last_error() in std::system_category(). On Windows the value is a Win32
// error code (Winsock codes are Win32 codes too); elsewhere it is an errno
// value.
std::error_code last_error_code() noexcept;

// A code of this platform's error space in std::system_category().
std::error_code system_error_code(int code) noexcept;

// The system's message for an error code, as UTF-8 without a trailing
// newline or period: FormatMessageW on Windows, strerror elsewhere.
std::string error_message(int code);

// The message of any std::error_code: error_message() for system-category
// codes, code.message() for the others.
std::string error_message(const std::error_code& code);

// Throws std::system_error(last_error_code(), what). Its what() carries the
// standard library's message, which MSVC builds in the ANSI code page; use
// error_message(e.code()) for text that reaches a user.
[[noreturn]] void throw_last_error(std::string_view what);

} // namespace neura::platform
