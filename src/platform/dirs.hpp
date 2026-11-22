#pragma once

// Well-known directories of the current user.

#include <filesystem>
#include <system_error>

namespace neura::platform {

// %LOCALAPPDATA% as the shell knows it (SHGetKnownFolderPath
// FOLDERID_LocalAppData). Not supported on POSIX (error set, empty path).
std::filesystem::path local_app_data(std::error_code& error);

// The user's home directory: FOLDERID_Profile on Windows; $HOME, else the
// password database, on POSIX.
std::filesystem::path home_directory(std::error_code& error);

// The temporary directory without a trailing separator: GetTempPath2W where
// the system has it (a per-user directory even for SYSTEM), else GetTempPathW;
// std::filesystem::temp_directory_path() on POSIX.
std::filesystem::path temp_directory(std::error_code& error);

} // namespace neura::platform
