#pragma once

// The install lifecycle commands (docs/Install.md): `neura --version`,
// `doctor`, `update`, `uninstall`, and the update notice. The data formats
// they read and write are in install_layout.hpp.

#include <filesystem>
#include <string>

namespace neura::lifecycle {

// Exit statuses of `update` and `uninstall`.
inline constexpr int exit_ok = 0;
inline constexpr int exit_failure = 1;
// The install is not managed by `neura update`.
inline constexpr int exit_unmanaged = 3;
// `update --check` found a newer version.
inline constexpr int exit_update_available = 4;

inline constexpr const char* default_release_source = "https://github.com/Kayyo321/neura/releases/latest/download";

// The line `neura --version` prints, without the newline.
std::string version_line();

struct DoctorOptions {
    // The standard library directory the tool resolved; empty when none.
    std::filesystem::path library;
};

struct UpdateOptions {
    bool check = false;
    std::string source = default_release_source;
    bool require_attestation = false;
};

struct UninstallOptions {
    bool dry_run = false;
    bool yes = false;
};

// `executable` is the real path of the running executable.
int doctor(const std::filesystem::path& executable, const DoctorOptions& options);
int update(const std::filesystem::path& executable, const UpdateOptions& options);
int uninstall(const std::filesystem::path& executable, const UninstallOptions& options);

// Prints the update notice and refreshes its state when the conditions in
// docs/Install.md hold. It never throws and never changes the exit status.
void update_notice(const std::filesystem::path& executable);

} // namespace neura::lifecycle
