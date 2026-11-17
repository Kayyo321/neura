#pragma once

#include "manifest.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace neura::project {

class Sha256Digest {
public:
    static std::optional<Sha256Digest> parse(std::string_view hex);
    static Sha256Digest from_bytes(const std::array<std::uint8_t, 32>& bytes);
    std::string_view hex() const { return {hex_.data(), hex_.size()}; }
    bool operator==(const Sha256Digest&) const = default;

private:
    Sha256Digest() = default;
    std::array<char, 64> hex_;
};

struct LockedComponent {
    Span span;
    Text version;
    Located<Sha256Digest> digest;
};

struct LockTarget {
    Span span;
    Named<LockedComponent> components;
};

struct ProjectLock {
    Span span;
    Located<std::int64_t> schema;
    Text language;
    Named<Located<Sha256Digest>> dependencies;
    Named<LockTarget> targets;
    std::optional<Located<Sha256Digest>> contracts_digest;
};

struct ResolvedComponent {
    std::string version;
    Sha256Digest digest;
};

// Supplied by the resolver. Dependency and contract digests describe found
// content. Component versions and digests come from installed metadata.
struct ResolvedLockInputs {
    Named<Sha256Digest> dependencies;
    Named<Named<ResolvedComponent>> targets;
    std::optional<Sha256Digest> contracts_digest;
};

Sha256Digest dependency_pin(std::string_view name, const ExternalDependency& dependency);
ProjectLock parse_lock(std::string_view bytes, std::string_view file);
ProjectLock make_lock(const Manifest& manifest, const ResolvedLockInputs& inputs);
// Serialize a lock returned by parse_lock or make_lock.
std::string write_lock(const ProjectLock& lock);

// Passing nullptr reports a missing project lock. This check is entirely
// in memory and verifies the full manifest against the supplied resolution.
void validate_lock(const ProjectLock* lock, const Manifest& manifest, const ResolvedLockInputs& inputs);

} // namespace neura::project
