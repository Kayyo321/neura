#include "lock.hpp"

#include "toml.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

namespace neura::project {

std::optional<Sha256Digest> Sha256Digest::parse(std::string_view hex) {
    Sha256Digest result;
    if (hex.size() != result.hex_.size() || !std::ranges::all_of(hex, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        })) {
        return std::nullopt;
    }
    std::ranges::copy(hex, result.hex_.begin());
    return result;
}

namespace {

constexpr std::int64_t lock_schema = 1;
constexpr std::string_view project_language = "0.3";

[[noreturn]] void malformed_lock(const Span& at, std::string message) {
    throw Diagnostic(at, std::move(message)).with_code("E1020");
}

[[noreturn]] void stale(const Span& at, std::string message) {
    throw Diagnostic(at, std::move(message)).with_code("E1022").help("run 'neura build --update-lock' to update the lock deliberately");
}

const TomlEntry* find(const TomlTable& table, std::string_view key) {
    const auto found = table.entries.find(key);
    return found == table.entries.end() ? nullptr : &found->second;
}

const TomlEntry& require(const TomlTable& table, std::string_view key) {
    if (const auto* entry = find(table, key)) return *entry;
    malformed_lock(table.span, "missing lock-file key '" + std::string(key) + "'");
}

void check_keys(const TomlTable& table, std::initializer_list<std::string_view> allowed) {
    const decltype(table.entries)::value_type* unknown = nullptr;
    for (const auto& item : table.entries) {
        if (std::ranges::find(allowed, item.first) != allowed.end()) continue;
        if (!unknown || item.second.key_span.offset < unknown->second.key_span.offset) unknown = &item;
    }
    if (unknown) malformed_lock(unknown->second.key_span, "unknown lock-file key '" + unknown->first + "'");
}

const TomlTable& table(const TomlDocument& doc, const TomlEntry& entry) {
    if (const auto* id = std::get_if<TomlTableId>(&entry.item)) return doc.table(*id);
    malformed_lock(entry.key_span, "expected a lock-file table");
}

template <typename T>
Located<T> value(const TomlEntry& entry, std::string_view type) {
    if (const auto* item = std::get_if<TomlValue>(&entry.item)) {
        if (const auto* typed = std::get_if<T>(&item->value)) return {*typed, item->span};
    }
    malformed_lock(entry.key_span, "expected " + std::string(type) + " in the lock file");
}

Located<Sha256Digest> locked_digest(const TomlEntry& entry) {
    const auto text = value<std::string>(entry, "a digest string");
    if (const auto digest = Sha256Digest::parse(text.value)) return {*digest, text.span};
    malformed_lock(text.span, "a SHA-256 digest requires 64 lowercase hexadecimal digits");
}

bool alnum(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool version_token(std::string_view version) {
    return !version.empty() && alnum(version.front()) && std::ranges::all_of(version, [](char c) {
        return alnum(c) || c == '.' || c == '_' || c == '+' || c == '-';
    });
}

LockTarget read_target(const TomlDocument& doc, const TomlTable& target) {
    LockTarget result{target.span, {}};
    for (const auto& [name, entry] : target.entries) {
        const auto& component = table(doc, entry);
        check_keys(component, {"version", "digest"});
        auto version = value<std::string>(require(component, "version"), "a component version string");
        if (!version_token(version.value)) malformed_lock(version.span, "expected a component version token");
        result.components.emplace(name, LockedComponent{component.span, std::move(version),
                                                        locked_digest(require(component, "digest"))});
    }
    return result;
}

std::string described_component(std::string_view target, std::string_view name) {
    return "component '" + std::string(name) + "' for target '" + std::string(target) + "'";
}

const Span& contracts_span(const Manifest& manifest) {
    return manifest.contracts_manifest ? manifest.contracts_manifest->span : manifest.project.language.span;
}

ProjectLock expected_lock(const Manifest& manifest, const ResolvedLockInputs& inputs) {
    const auto& language = manifest.project.language;
    ProjectLock expected{manifest.schema.span, {lock_schema, manifest.schema.span}, language, {}, {}, std::nullopt};
    for (const auto& [name, external] : manifest.dependencies) {
        const auto pin = Sha256Digest::parse(external.digest.value);
        if (!pin) {
            throw Diagnostic(external.span, "dependency '" + name + "' requires a SHA-256 pin").with_code("E1032")
                .label(external.digest.span, "expected 64 lowercase hexadecimal digits here");
        }
        if (!inputs.dependencies.contains(name)) {
            throw Diagnostic(external.span, "dependency '" + name + "' was not resolved").with_code("E1032");
        }
        expected.dependencies.emplace(name, Located<Sha256Digest>{*pin, external.digest.span});
    }
    for (const auto& [name, digest] : inputs.dependencies) {
        if (!expected.dependencies.contains(name)) stale(language.span, "resolution contains an undeclared dependency '" + name + "'");
    }
    for (const auto& [name, target] : manifest.targets) {
        const auto found = inputs.targets.find(name);
        if (found == inputs.targets.end()) stale(target.span, "component resolution is missing for target '" + name + "'");
        LockTarget locked{target.span, {}};
        for (const auto& [key, component] : found->second) {
            if (!version_token(component.version)) {
                stale(target.span, "installed " + described_component(name, key) + " has an invalid component version token");
            }
            locked.components.emplace(key, LockedComponent{target.span, {component.version, target.span},
                                                            {component.digest, target.span}});
        }
        expected.targets.emplace(name, std::move(locked));
    }
    for (const auto& [name, target] : inputs.targets) {
        if (!expected.targets.contains(name)) stale(language.span, "component resolution contains an undeclared target '" + name + "'");
    }
    if (inputs.contracts_digest) {
        expected.contracts_digest = Located<Sha256Digest>{*inputs.contracts_digest, contracts_span(manifest)};
    }
    return expected;
}

[[noreturn]] void wrong_content(const Located<Sha256Digest>& expected, const Sha256Digest& found, std::string role) {
    const std::string wanted(expected.value.hex());
    const std::string actual(found.hex());
    throw Diagnostic(expected.span, role + " has SHA-256 " + actual + "; expected " + wanted).with_code("E1026")
        .arg("expected_digest", wanted).arg("found_digest", actual);
}

void compare_dependency_content(const ProjectLock& lock, const ResolvedLockInputs& inputs) {
    for (const auto& [name, pin] : lock.dependencies) {
        const auto& found = inputs.dependencies.at(name);
        if (pin.value != found) wrong_content(pin, found, "dependency '" + name + "'");
    }
}

void compare_dependencies(const ProjectLock& expected, const ProjectLock& lock) {
    for (const auto& [name, pin] : expected.dependencies) {
        const auto found = lock.dependencies.find(name);
        if (found == lock.dependencies.end()) stale(pin.span, "lock is missing dependency '" + name + "'");
        if (found->second.value != pin.value) {
            stale(found->second.span, "lock pins dependency '" + name + "' to " +
                  std::string(found->second.value.hex()) + "; manifest pins " + std::string(pin.value.hex()));
        }
    }
    for (const auto& [name, pin] : lock.dependencies) {
        if (!expected.dependencies.contains(name)) stale(pin.span, "lock contains an undeclared dependency '" + name + "'");
    }
}

void compare_components(std::string_view target, const LockTarget& expected, const LockTarget& locked) {
    for (const auto& [name, component] : expected.components) {
        const auto found = locked.components.find(name);
        const auto role = described_component(target, name);
        if (found == locked.components.end()) stale(component.span, "lock is missing " + role);
        const auto& pin = found->second;
        if (pin.version.value != component.version.value) {
            stale(pin.version.span, "lock records version '" + pin.version.value + "' for " + role +
                  "; installed version is '" + component.version.value + "'");
        }
        if (pin.digest.value != component.digest.value) {
            stale(pin.digest.span, "lock records SHA-256 " + std::string(pin.digest.value.hex()) + " for " + role +
                  "; installed SHA-256 is " + std::string(component.digest.value.hex()));
        }
    }
    for (const auto& [name, component] : locked.components) {
        if (!expected.components.contains(name)) stale(component.span, "lock contains an unused " + described_component(target, name));
    }
}

void compare_targets(const ProjectLock& expected, const ProjectLock& lock) {
    for (const auto& [name, target] : expected.targets) {
        const auto found = lock.targets.find(name);
        if (found == lock.targets.end()) stale(target.span, "lock is missing target '" + name + "'");
        compare_components(name, target, found->second);
    }
    for (const auto& [name, target] : lock.targets) {
        if (!expected.targets.contains(name)) stale(target.span, "lock contains an undeclared target '" + name + "'");
    }
}

void write_digest(std::string& out, std::string_view name, const Sha256Digest& digest) {
    out += name;
    out += " = \"";
    out += digest.hex();
    out += "\"\n";
}

} // namespace

ProjectLock parse_lock(std::string_view bytes, std::string_view file) {
    const auto doc = parse_toml(bytes, file);
    const auto& root = doc.root();
    check_keys(root, {"schema", "language", "contracts_digest", "dependencies", "components"});
    auto schema = value<std::int64_t>(require(root, "schema"), "a decimal integer schema");
    if (schema.value != lock_schema) {
        stale(schema.span, "unsupported lock schema " + std::to_string(schema.value) + "; expected schema 1");
    }
    auto language = value<std::string>(require(root, "language"), "a language version string");
    if (language.value != project_language) {
        stale(language.span, "unsupported lock language version '" + language.value + "'; expected '0.3'");
    }
    ProjectLock lock{root.span, std::move(schema), std::move(language), {}, {}, std::nullopt};
    if (const auto* digest = find(root, "contracts_digest")) lock.contracts_digest = locked_digest(*digest);
    if (const auto* dependencies = find(root, "dependencies")) {
        for (const auto& [name, entry] : table(doc, *dependencies).entries) {
            lock.dependencies.emplace(name, locked_digest(entry));
        }
    }
    if (const auto* components = find(root, "components")) {
        for (const auto& [name, entry] : table(doc, *components).entries) {
            lock.targets.emplace(name, read_target(doc, table(doc, entry)));
        }
    }
    return lock;
}

ProjectLock make_lock(const Manifest& manifest, const ResolvedLockInputs& inputs) {
    auto lock = expected_lock(manifest, inputs);
    compare_dependency_content(lock, inputs);
    return lock;
}

std::string write_lock(const ProjectLock& lock) {
    std::string out = "schema = " + std::to_string(lock.schema.value) + "\nlanguage = " +
                      quote_toml_string(lock.language.value) + "\n";
    if (lock.contracts_digest) write_digest(out, "contracts_digest", lock.contracts_digest->value);
    if (!lock.dependencies.empty()) {
        out += "\n[dependencies]\n";
        for (const auto& [name, pin] : lock.dependencies) write_digest(out, quote_toml_string(name), pin.value);
    }
    for (const auto& [name, target] : lock.targets) {
        const auto prefix = "\n[components." + quote_toml_string(name);
        if (target.components.empty()) out += prefix + "]\n";
        for (const auto& [key, component] : target.components) {
            out += prefix + "." + quote_toml_string(key) + "]\n";
            out += "version = " + quote_toml_string(component.version.value) + "\n";
            write_digest(out, "digest", component.digest.value);
        }
    }
    return out;
}

void validate_lock(const ProjectLock* lock, const Manifest& manifest, const ResolvedLockInputs& inputs) {
    if (!lock) stale(manifest.project.language.span, "neura.lock is missing");
    const auto expected = expected_lock(manifest, inputs);
    if (lock->schema.value != lock_schema) stale(lock->schema.span, "expected lock schema 1");
    if (lock->language.value != expected.language.value) {
        stale(lock->language.span, "lock language version disagrees with the manifest");
    }
    compare_dependencies(expected, *lock);
    compare_targets(expected, *lock);
    if (expected.contracts_digest && !lock->contracts_digest) {
        stale(expected.contracts_digest->span, "lock is missing the contracts.lock digest");
    }
    if (!expected.contracts_digest && lock->contracts_digest) {
        stale(lock->contracts_digest->span, "lock has a digest for an absent contracts.lock");
    }
    compare_dependency_content(*lock, inputs);
    if (lock->contracts_digest && lock->contracts_digest->value != expected.contracts_digest->value) {
        wrong_content(*lock->contracts_digest, expected.contracts_digest->value, "contracts.lock");
    }
}

} // namespace neura::project
