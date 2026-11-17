#pragma once

// The install lifecycle commands (docs/Install.md): `neura --version`,
// `doctor`, `update`, `uninstall`, and the update notice. The data formats
// they read and write are in install_layout.hpp.

#include "c-import/c_import.hpp"

#include <filesystem>
#include <cstddef>
#include <functional>
#include <optional>
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
    // Loads libclang to report it. The SDK's C import stage supplies it, so
    // that the doctor of the execution core links no compiler code; without
    // it the libclang lines say they were not checked.
    std::function<LibclangStatus()> probe_libclang;
};

struct UpdateOptions {
    bool check = false;
    bool nightly = false;
    std::string source = default_release_source;
    // Whether --source was given; adding a component reads the installed
    // version's own release otherwise.
    bool source_given = false;
    bool require_attestation = false;
    // --install-from <dir>, which install.sh runs, and --add <dir>.
    std::string install_from;
    std::string add;
    bool force = false;
    // --components sdk|runtime|sdk,service.
    std::string components;
    bool migrate = false;
    bool rollback = false;
    bool repair = false;
    bool terminate = false;
};

struct UninstallOptions {
    bool dry_run = false;
    bool yes = false;
    // --component sdk|service: remove one component, by a new generation.
    std::string component;
    bool terminate = false;
};

// The service's part in a change (docs/design/Lifecycle030.md, "A change,
// step by step", and docs/design/Service030.md). The service milestone
// supplies its control client through these hooks; without one, the
// lifecycle finds supervisors by the lock they hold, removes the login
// registration and state by their paths, and asks the installation's own
// `neura runtime shutdown --terminate` to stop applications.
struct ServiceHooks {
    // The applications of the installation at `root` that are not terminal,
    // or nothing when that cannot be told without a control client.
    std::function<std::optional<std::size_t>(const std::filesystem::path& root)> applications;
    // Stops every application and supervisor of the installation. Returns an
    // empty string on success, else the reason.
    std::function<std::string(const std::filesystem::path& root)> terminate;
    // Tells the supervisor of `generation`, if one runs, that it is retired,
    // waiting at most 250 ms.
    std::function<void(const std::filesystem::path& root, const std::string& generation)> retire;
    // Stops the supervisors of the installation when they run no
    // application, terminating nothing. Returns an empty string on success,
    // else the reason. Only the control client supplies it; without it an
    // idle supervisor holds its generation's lease like any other process.
    std::function<std::string(const std::filesystem::path& root)> stop_idle;
};
void set_service_hooks(ServiceHooks hooks);

// `executable` is the real path of the running executable.
int doctor(const std::filesystem::path& executable, const DoctorOptions& options);
int update(const std::filesystem::path& executable, const UpdateOptions& options);
int uninstall(const std::filesystem::path& executable, const UninstallOptions& options);

// Prints the update notice and refreshes its state when the conditions in
// docs/Install.md hold. It never throws and never changes the exit status.
void update_notice(const std::filesystem::path& executable);

} // namespace neura::lifecycle
