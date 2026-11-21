#pragma once

// The running process: its identifier and its executable.

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>

namespace neura::platform {

#ifdef _WIN32
using process_id = std::uint32_t; // DWORD
#else
using process_id = int; // pid_t
#endif

process_id current_process_id() noexcept;

// The real path of the running executable, with links resolved: on Windows
// GetModuleFileNameW, then GetFinalPathNameByHandleW so junctions, symbolic
// links and 8.3 names do not leak into the result (the \\?\ prefix is
// removed when the path does not need it); /proc/self/exe on Linux;
// _NSGetExecutablePath on macOS. Returns an empty path and sets `error` when
// the platform cannot tell (other POSIX systems: not_supported).
std::filesystem::path executable_path(std::error_code& error);
// Throws std::system_error on failure.
std::filesystem::path executable_path();

// ".exe" on Windows, "" elsewhere.
std::string_view executable_suffix() noexcept;

} // namespace neura::platform
