#pragma once

// The process environment as UTF-8 (W18). Windows reads and writes the
// UTF-16 environment (GetEnvironmentVariableW), so names match
// case-insensitively, and keeps the C runtime's copy in step for code that
// still calls getenv.

#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace neura::platform {

// The value of `name`, nullopt when it is not set. A name or value that is
// not valid text (invalid UTF-8 in `name`, an unpaired surrogate in a Windows
// value) returns nullopt with `error` set to invalid_text_error().
std::optional<std::string> get_env(std::string_view name, std::error_code& error);
// Throws std::system_error for text that cannot be converted.
std::optional<std::string> get_env(std::string_view name);

// Sets or removes a variable in this process (and for children started
// later). Windows: SetEnvironmentVariableW and _wputenv_s. The C runtime
// cannot hold an empty value, so after set_env(name, "") getenv(name) is
// NULL there while get_env(name) returns "".
bool set_env(std::string_view name, std::string_view value, std::error_code& error);
bool unset_env(std::string_view name, std::error_code& error);

// Every variable, in the order the system keeps them (GetEnvironmentStringsW,
// environ). Windows' per-drive "=C:" entries are left out. Throws
// std::system_error for an entry that is not valid text.
std::vector<std::pair<std::string, std::string>> environment();

} // namespace neura::platform
