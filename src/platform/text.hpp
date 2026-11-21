#pragma once

// Text that crosses an operating-system boundary. UTF-8 is the only narrow
// encoding Neura uses (W07): on Windows, text is converted to UTF-16 for the
// W functions strictly, so an invalid UTF-8 sequence or an unpaired surrogate
// is an error instead of a replacement character. On POSIX the bytes pass
// through unchanged.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace neura::platform {

// The error that a failed strict conversion reports: ERROR_NO_UNICODE_TRANSLATION
// on Windows, EILSEQ elsewhere.
std::error_code invalid_text_error() noexcept;

// Whether `text` is well-formed UTF-8 (no overlong forms, no surrogates, no
// code point above U+10FFFF).
bool is_valid_utf8(std::string_view text) noexcept;

#ifdef _WIN32
// Strict UTF-8 -> UTF-16. Throws std::system_error(invalid_text_error()) for
// text that is not valid UTF-8.
std::wstring to_utf16(std::string_view utf8);
std::optional<std::wstring> try_to_utf16(std::string_view utf8);

// Strict UTF-16 -> UTF-8. Throws std::system_error(invalid_text_error()) for
// an unpaired surrogate.
std::string to_utf8(std::wstring_view utf16);
std::optional<std::string> try_to_utf8(std::wstring_view utf16);
#endif

// A path from UTF-8 text. Throws std::system_error(invalid_text_error()) on
// Windows when the text is not valid UTF-8.
std::filesystem::path path_from_utf8(std::string_view utf8);

// The path as UTF-8 in generic form ('/' separators on every host, the drive
// letter as given). Use it for text that reaches a user, an artifact or
// another process instead of path::string() or generic_string(), which use
// the ANSI code page on Windows. Throws std::system_error(invalid_text_error())
// for a Windows name that holds an unpaired surrogate.
std::string utf8_path(const std::filesystem::path& path);

// The path as UTF-8 in native form ('\\' separators on Windows), for a C API
// or a command line that wants the platform's own spelling.
std::string native_utf8_path(const std::filesystem::path& path);

} // namespace neura::platform
