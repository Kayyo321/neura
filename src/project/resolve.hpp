#pragma once

#include "lock.hpp"

#include <filesystem>
#include <optional>

namespace neura::project {

using ResolvedPath = Located<std::filesystem::path>;

struct ResolvedPackagePaths {
    ResolvedPath root;
    std::vector<ResolvedPath> assets;
};

struct ResolvedTargetPaths {
    std::vector<ResolvedPath> styles;
    std::optional<ResolvedPath> icon;
    Named<ResolvedPath> migrations;
};

struct ResolvedProject {
    std::filesystem::path root;
    std::filesystem::path manifest;
    Named<ResolvedPackagePaths> packages;
    Named<ResolvedPath> dependencies;
    Named<ResolvedTargetPaths> targets;
    std::optional<ResolvedPath> contracts_manifest;
    std::optional<ResolvedPath> contracts_lock;
    // Fill targets from installed metadata before make_lock/validate_lock.
    ResolvedLockInputs lock_inputs;
};

// Resolve the paths of a parsed manifest and read the found digest inputs.
// contracts_lock, when supplied, is the caller's project-relative location
// for the generated lock. A missing generated lock contributes no digest.
ResolvedProject resolve_project(const Manifest& manifest, const std::filesystem::path& manifest_path,
                                const std::optional<std::filesystem::path>& contracts_lock = std::nullopt);

} // namespace neura::project
