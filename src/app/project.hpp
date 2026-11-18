#pragma once

// The project tools' shared layer (docs/design/Projects030.md): loading a
// project, checking or writing its lock, building a target into a bundle, and
// starting the host of a built bundle. `neura build --project`, `neura run
// <bundle>`, `neura new`, `neura dev` and `neura package` are thin mains over
// it. Every function reports a project problem as a Diagnostic carrying its
// E10xx code; print_diagnostic renders one.

#include "project/bundle.hpp"
#include "project/resolve.hpp"

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace neura::app {

namespace fs = std::filesystem;

struct Project {
    project::Manifest manifest;
    project::ResolvedProject resolved;
};

// Reads, validates and resolves `neura.toml`.
Project load_project(const fs::path& manifest_path);

// The manifest a command uses: --project, else `neura.toml` of the current
// directory. Refuses (status 1, message on err) when there is none.
std::optional<fs::path> find_manifest(const std::optional<std::string>& project, std::ostream& err);

// The components each target resolves to, as the lock records them.
void fill_components(Project& project);

// Compares `neura.lock` with the project (E1022, E1026), or writes it when
// `update` is set or, for `allow_missing`, when there is none yet.
void check_lock(Project& project, bool update, bool allow_missing = false);

struct BuildOptions {
    std::string target;
    std::optional<fs::path> out;
    bool update_lock = false;
    bool source_map = false;
    bool no_optimize = false;
    bool no_lift_hints = false;
    bool dev = false;
    // The `neura` executable that compiles images.
    fs::path neura;
    // Where nvm.wasm, nvm.mjs and the page modules are: --web-runtime, else
    // NEURA_WEB_RUNTIME, else <prefix>/share/neura/web.
    std::optional<fs::path> web_runtime;
    // The development reload script and endpoint (`neura dev` only).
    std::optional<std::string> dev_reload;
};

struct BuildResult {
    fs::path directory;
    project::Sha256Digest digest;
};

// Builds one target into its bundle directory (dist/<target> unless --out)
// and verifies the written bundle. A desktop target builds its web target
// first, into dist/<web target>, and embeds it unchanged.
BuildResult build_target(Project& project, const BuildOptions& options);

// Refuses an unknown target (E1023) or a provider this SDK does not have
// (E1031), before the lock is compared.
void check_target(const Project& project, const std::string& name);

// The versions of every axis this SDK's hosts support (bundle.json
// `requires`).
project::Named<std::uint32_t> supported_requirements();

// The installation prefix of the running tool (the parent of bin/).
fs::path install_prefix();

// The directory of the running executable.
fs::path tool_directory();

void print_diagnostic(std::ostream& err, const Diagnostic& diagnostic);

// Runs a program and waits for it. Returns its exit status, 128 + signal.
int spawn_and_wait(const std::vector<std::string>& arguments, const std::optional<fs::path>& directory = std::nullopt);

// `neura run <bundle directory> [-- arguments]`: validate, then run a server
// bundle with neura-server-host or a desktop bundle with the desktop host.
int run_bundle(const fs::path& directory, const std::vector<std::string>& arguments, std::ostream& out,
               std::ostream& err);

bool is_bundle_directory(const fs::path& path);

// SHA-256 of bytes as a digest.
project::Sha256Digest sha256_of(std::string_view bytes);
std::string read_file(const fs::path& path);
void write_file(const fs::path& path, std::string_view bytes);

} // namespace neura::app

namespace neura::app {

// `neura build` names a project when it has --project, or --target without
// --native (whose --target is a native triple).
bool is_project_build(const std::vector<std::string>& arguments);

// `neura build --project <manifest> --target <name> [--out <directory>]
// [--update-lock] [--no-optimize] [--no-lift-hints] [--web-runtime <dir>]`.
// arguments[1] is "build".
int project_build_main(const std::vector<std::string>& arguments);

} // namespace neura::app
