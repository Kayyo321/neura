#include "manifest.hpp"

#include "toml.hpp"
#include "util/suggest.hpp"

#include <algorithm>
#include <charconv>
#include <initializer_list>
#include <system_error>
#include <utility>
#include <variant>

namespace neura::project {
namespace {

[[noreturn]] void malformed(const Span& at, std::string reason) {
    throw Diagnostic(at, std::move(reason)).with_code("E1020");
}

[[noreturn]] void invalid_target(const Span& at, std::string reason) {
    throw Diagnostic(at, std::move(reason)).with_code("E1023");
}

const TomlEntry* find(const TomlTable& table, std::string_view key) {
    const auto entry = table.entries.find(key);
    return entry == table.entries.end() ? nullptr : &entry->second;
}

const TomlEntry& required(const TomlTable& table, std::string_view key) {
    if (const auto* entry = find(table, key)) return *entry;
    malformed(table.span, "missing required key '" + std::string(key) + "'");
}

void keys(const TomlTable& table, std::initializer_list<std::string_view> allowed) {
    // The map gives stable lookup. Point to the first unknown key in the
    // source when more than one name is misspelled.
    const decltype(table.entries)::value_type* unknown = nullptr;
    for (const auto& entry : table.entries) {
        if (std::ranges::find(allowed, entry.first) != allowed.end()) continue;
        if (!unknown || entry.second.key_span.offset < unknown->second.key_span.offset) unknown = &entry;
    }
    if (!unknown) return;
    const auto& [name, entry] = *unknown;
    auto error = Diagnostic(entry.key_span, "unknown manifest key '" + name + "'").with_code("E1020");
    const std::vector<std::string> candidates(allowed.begin(), allowed.end());
    if (const auto suggest = closest_name(name, candidates)) {
        error.suggest("did you mean '" + *suggest + "'?", entry.key_span, *suggest, Applicability::MaybeIncorrect);
    }
    throw error;
}

[[noreturn]] void wrong_kind(const TomlEntry& entry, std::string_view expected) {
    malformed(entry.key_span, "expected " + std::string(expected) + " for this key");
}

const TomlTable& as_table(const TomlDocument& doc, const TomlEntry& entry) {
    if (const auto* id = std::get_if<TomlTableId>(&entry.item)) return doc.table(*id);
    wrong_kind(entry, "a table");
}

template <typename T>
const TomlValue& as_value(const TomlEntry& entry, std::string_view expected) {
    const auto* value = std::get_if<TomlValue>(&entry.item);
    if (!value || !std::holds_alternative<T>(value->value)) wrong_kind(entry, expected);
    return *value;
}

Text as_text(const TomlEntry& entry) {
    const auto& value = as_value<std::string>(entry, "a string");
    return {std::get<std::string>(value.value), value.span};
}

std::optional<Text> text(const TomlTable& table, std::string_view key) {
    if (const auto* entry = find(table, key)) return as_text(*entry);
    return std::nullopt;
}

std::vector<Text> as_texts(const TomlEntry& entry) {
    const auto& value = as_value<TomlValue::Array>(entry, "an array of strings");
    std::vector<Text> result;
    const auto& items = std::get<TomlValue::Array>(value.value);
    result.reserve(items.size());
    for (const auto& item : items) {
        const auto* str = std::get_if<std::string>(&item.value);
        if (!str) wrong_kind(entry, "an array of strings");
        result.push_back({*str, item.span});
    }
    return result;
}

std::vector<Text> texts(const TomlTable& table, std::string_view key) {
    if (const auto* entry = find(table, key)) return as_texts(*entry);
    return {};
}

bool digit(char c) { return c >= '0' && c <= '9'; }
bool lower_letter(char c) { return c >= 'a' && c <= 'z'; }
bool letter(char c) { return lower_letter(c) || (c >= 'A' && c <= 'Z'); }
bool alnum(char c) { return letter(c) || digit(c); }
bool name_char(char c) { return lower_letter(c) || digit(c) || c == '-'; }

// Remove the first segment, leaving the rest to the caller.
std::string_view segment(std::string_view& remaining, char separator) {
    const auto end = remaining.find(separator);
    const auto part = remaining.substr(0, end);
    remaining = end == std::string_view::npos ? std::string_view{} : remaining.substr(end + 1);
    return part;
}

bool dns_name(std::string_view name, bool lowercase) {
    if (name.empty() || name.ends_with('.') || name.size() > 253) return false;
    while (!name.empty()) {
        const auto label = segment(name, '.');
        if (label.empty() || label.size() > 63 || !alnum(label.front()) || !alnum(label.back())) return false;
        if (!std::ranges::all_of(label, [&](char c) { return lowercase ? name_char(c) : alnum(c) || c == '-'; })) {
            return false;
        }
    }
    return true;
}

bool version_core(std::string_view version) {
    if (std::ranges::count(version, '.') != 2) return false;
    for (int i = 0; i < 3; ++i) {
        const auto part = segment(version, '.');
        if (part.empty() || (part.size() > 1 && part.starts_with('0')) ||
            !std::ranges::all_of(part, digit)) return false;
    }
    return true;
}

ProjectIdentity read_identity(const TomlTable& table) {
    keys(table, {"name", "id", "version", "language"});
    ProjectIdentity info{as_text(required(table, "name")), as_text(required(table, "id")),
                         as_text(required(table, "version")), as_text(required(table, "language"))};
    if (!valid_project_name(info.name.value)) {
        malformed(info.name.span, "project name needs lowercase letters, digits and hyphens");
    }
    if (!valid_project_id(info.id.value)) {
        malformed(info.id.span, "expected a lowercase reverse-domain project identifier");
    }
    if (!valid_project_version(info.version.value)) malformed(info.version.span, "expected a project version of the form MAJOR.MINOR.PATCH");
    if (info.language.value != "0.3") {
        throw Diagnostic(info.language.span, "unsupported project language version; this SDK supports \"0.3\"").with_code("E1021");
    }
    return info;
}

// Why a path component cannot name the same file on every host, or nothing.
// These are the rules of bundle file paths: Windows reads ':' as the start of
// a stream name, opens a device for a reserved name with any extension, and
// drops a trailing dot or space.
std::optional<std::string> unportable(std::string_view part) {
    if (part.find(':') != std::string_view::npos) return "contains ':', which Windows reads as a stream name";
    if (part.ends_with('.') || part.ends_with(' ')) return "ends with a dot or a space, which Windows removes";
    std::string stem(part.substr(0, part.find('.')));
    for (auto& c : stem) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    const bool numbered = stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem.back() >= '1' &&
                          stem.back() <= '9';
    if (numbered || stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return "is a device name that Windows reserves";
    }
    return std::nullopt;
}

// Compare as slash-separated paths independently of the build host's rules,
// and accept only names that every host can create.
Text local_path(Text path, bool outside = false) {
    const std::string_view value = path.value;
    const bool drive = value.size() >= 2 && letter(value[0]) && value[1] == ':';
    if (value.empty() || value.starts_with('/') || drive || value.find("://") != std::string_view::npos ||
        value.find('\\') != std::string_view::npos || value.find('\0') != std::string_view::npos) {
        throw Diagnostic(path.span, "expected a local path relative to the manifest, using '/'").with_code("E1025");
    }
    std::size_t depth = 0;
    bool escapes = false;
    auto rest = value;
    while (!rest.empty()) {
        const auto part = segment(rest, '/');
        if (part == "..") {
            if (depth == 0) escapes = true;
            else --depth;
        } else if (!part.empty() && part != ".") {
            if (const auto reason = unportable(part)) {
                throw Diagnostic(path.span, "path component '" + std::string(part) + "' " + *reason).with_code("E1025");
            }
            ++depth;
        }
    }
    if (escapes != outside) {
        throw Diagnostic(path.span, outside ? "an external dependency directory must be outside the project"
                                            : "this path leaves the project directory").with_code("E1025");
    }
    return path;
}

std::optional<Text> path(const TomlTable& table, std::string_view name) {
    if (auto value = text(table, name)) return local_path(std::move(*value));
    return std::nullopt;
}

std::vector<Text> paths(const TomlTable& table, std::string_view name) {
    auto result = texts(table, name);
    for (auto& value : result) value = local_path(std::move(value));
    return result;
}

Package read_package(const TomlTable& table) {
    keys(table, {"root", "assets", "depends"});
    return {table.span, local_path(as_text(required(table, "root"))), paths(table, "assets"), texts(table, "depends")};
}

ExternalDependency read_dependency(const TomlTable& table) {
    keys(table, {"path", "archive", "digest"});
    const auto* directory = find(table, "path");
    const auto* archive = find(table, "archive");
    const auto* pin = find(table, "digest");
    if (!pin || (directory == nullptr) == (archive == nullptr)) {
        throw Diagnostic(table.span, "an external dependency requires one of 'path' or 'archive' and a digest").with_code("E1032");
    }
    auto digest = as_text(*pin);
    if (digest.value.empty() || !std::ranges::any_of(digest.value, [](char c) {
            return c != ' ' && c != '\t' && c != '\r' && c != '\n';
        })) {
        throw Diagnostic(table.span, "an external dependency requires a digest").with_code("E1032").label(digest.span, "replace this empty pin");
    }
    const auto source = directory ? DependencySource::Directory : DependencySource::Archive;
    return {table.span, source, local_path(as_text(directory ? *directory : *archive), directory != nullptr), std::move(digest)};
}

std::optional<unsigned> unsigned_number(std::string_view input, unsigned max) {
    if (input.empty()) return std::nullopt;
    unsigned result{};
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), result);
    if (error != std::errc{} || end != input.data() + input.size() || result > max) return std::nullopt;
    return result;
}

bool ipv4(std::string_view ip) {
    if (std::ranges::count(ip, '.') != 3) return false;
    for (int i = 0; i < 4; ++i) {
        const auto part = segment(ip, '.');
        if ((part.size() > 1 && part.starts_with('0')) || !unsigned_number(part, 255)) return false;
    }
    return true;
}

bool ipv6_segments(std::string_view ip, int& seen, bool ipv4_allowed) {
    if (ip.ends_with(':')) return false;
    while (!ip.empty()) {
        const auto part = segment(ip, ':');
        if (ipv4_allowed && ip.empty() && ipv4(part)) {
            seen += 2;
        } else {
            if (part.empty() || part.size() > 4 ||
                !std::ranges::all_of(part, [](char c) {
                    return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                })) return false;
            ++seen;
        }
        if (seen > 8) return false;
    }
    return true;
}

bool ipv6(std::string_view ip) {
    int seen = 0;
    const auto compression = ip.find("::");
    if (compression == std::string_view::npos) return ipv6_segments(ip, seen, true) && seen == 8;
    return ipv6_segments(ip.substr(0, compression), seen, false) &&
           ipv6_segments(ip.substr(compression + 2), seen, true) && seen < 8;
}

bool origin_host(std::string_view host) {
    if (host.ends_with('.')) host.remove_suffix(1);
    if (std::ranges::all_of(host, [](char c) { return digit(c) || c == '.'; })) return ipv4(host);
    return dns_name(host, false);
}

std::optional<Origin> origin_parts(std::string_view raw) {
    const auto boundary = raw.find("://");
    if (boundary == std::string_view::npos) return std::nullopt;
    const auto scheme = raw.substr(0, boundary);
    if (scheme.empty() || !letter(scheme.front()) || !std::ranges::all_of(scheme, [](char c) {
            return alnum(c) || c == '+' || c == '-' || c == '.';
        })) return std::nullopt;

    auto authority = raw.substr(boundary + 3);
    std::string_view host;
    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string_view::npos || !ipv6(authority.substr(1, close - 1))) return std::nullopt;
        host = authority.substr(0, close + 1);
        authority.remove_prefix(close + 1);
    } else {
        host = authority.substr(0, authority.find(':'));
        if (!origin_host(host)) return std::nullopt;
        authority.remove_prefix(host.size());
    }

    std::optional<std::uint16_t> port;
    if (!authority.empty()) {
        if (!authority.starts_with(':')) return std::nullopt;
        const auto number = unsigned_number(authority.substr(1), 65535);
        if (!number) return std::nullopt;
        port = static_cast<std::uint16_t>(*number);
    }
    return Origin{std::string(raw), std::string(scheme), std::string(host), port};
}

std::vector<Located<Origin>> origin_list(const TomlTable& table, std::string_view key) {
    std::vector<Located<Origin>> result;
    for (auto& value : texts(table, key)) {
        auto parsed = parse_origin(value.value);
        if (!parsed) malformed(value.span, "expected an absolute origin with a scheme, host and optional port");
        result.push_back({std::move(*parsed), std::move(value.span)});
    }
    return result;
}

std::optional<std::vector<Located<Origin>>> read_connect_origins(const TomlTable& table) {
    if (!find(table, "connect")) return std::nullopt;
    return origin_list(table, "connect");
}

Origins read_origins(const TomlDocument& doc, const TomlEntry* entry) {
    if (!entry) return {};
    const auto& table = as_table(doc, *entry);
    keys(table, {"connect", "load", "style", "external"});
    return {read_connect_origins(table), origin_list(table, "load"),
            origin_list(table, "style"), origin_list(table, "external")};
}

bool capability_allowed(Capability value, TargetKind kind) {
    switch (value) {
    case Capability::Ui:
    case Capability::Bridge: return kind == TargetKind::Web;
    case Capability::HttpServer: return kind == TargetKind::Server;
    case Capability::Desktop: return kind == TargetKind::Desktop;
    case Capability::HttpClient: return true;
    default: return kind != TargetKind::Web;
    }
}

constexpr std::pair<std::string_view, Capability> capability_names[] = {
    {"ui", Capability::Ui}, {"http_client", Capability::HttpClient}, {"bridge", Capability::Bridge},
    {"http_server", Capability::HttpServer}, {"sql", Capability::Sql}, {"app", Capability::App},
    {"desktop", Capability::Desktop}, {"fs", Capability::Fs}, {"net", Capability::Net}, {"env", Capability::Env},
};

std::vector<Located<Capability>> read_capabilities(const TomlEntry& entry, TargetKind kind) {
    std::vector<Located<Capability>> result;
    for (auto& item : as_texts(entry)) {
        const auto capability = target_capability(kind, item.value);
        if (!capability) {
            malformed(item.span, "capability '" + item.value + "' cannot be listed for this target kind");
        }
        result.push_back({*capability, std::move(item.span)});
    }
    return result;
}

struct LimitRule {
    std::string_view name;
    std::uint32_t maximum;
    std::uint32_t second = 0;
};

// Maxima and units from Apps030.md, Finite limits. Composite entries set
// both bounds in the order they appear there.
constexpr std::uint32_t kib = 1024, mib = 1024 * kib, gib = 1024 * mib;
constexpr LimitRule limit_rules[] = {
    {"U-1", 65535}, {"U-2", 255}, {"U-3", 65535}, {"U-4", 1000000}, {"U-5", 4000000},
    {"U-6", 16 * mib}, {"U-7", 65536}, {"U-8", 4096}, {"U-9", 256}, {"U-10", 1024},
    {"W-1", gib}, {"W-2", 50}, {"W-3", 256 * mib}, {"W-4", 256},
    {"C-1", 4096}, {"C-2", 64 * mib}, {"C-3", 1000000}, {"C-4", 128},
    {"S-1", 1024}, {"S-2", 64}, {"S-3", 65536}, {"S-4", 1000000}, {"S-5", mib},
    {"S-6", gib}, {"S-7", gib}, {"S-8", 3600}, {"S-9", 1024}, {"S-10", 65536, 600},
    {"S-11", 1000000}, {"S-12", 3600, 60}, {"S-13", mib, 1000000},
    {"D-1", 64}, {"D-2", 1024}, {"D-3", 64 * mib}, {"D-4", 3600},
};

bool limit_allowed(std::string_view name, TargetKind kind) {
    switch (name.front()) {
    case 'U':
    case 'W': return kind == TargetKind::Web;
    case 'S': return kind == TargetKind::Server;
    case 'D': return kind == TargetKind::Desktop;
    default: return true;
    }
}

Limit checked_limit(const TomlEntry& entry, const TomlValue& value, std::string_view name, std::uint32_t maximum) {
    const auto* number = std::get_if<std::int64_t>(&value.value);
    if (!number) wrong_kind(entry, "a decimal integer limit");
    if (*number < 1 || *number > maximum) {
        throw Diagnostic(value.span, "limit '" + std::string(name) + "' must be between 1 and " +
                         std::to_string(maximum)).with_code("E1028");
    }
    return {static_cast<std::uint32_t>(*number), value.span};
}

Named<LimitOverride> read_limits(const TomlDocument& doc, const TomlEntry* entry, TargetKind kind) {
    Named<LimitOverride> result;
    if (!entry) return result;
    for (const auto& [name, limit] : as_table(doc, *entry).entries) {
        const auto rule = std::ranges::find(limit_rules, name, &LimitRule::name);
        if (rule == std::end(limit_rules)) malformed(limit.key_span, "unknown limit '" + name + "'");
        if (!limit_allowed(name, kind)) {
            const auto* value = std::get_if<TomlValue>(&limit.item);
            throw Diagnostic(value ? value->span : limit.key_span,
                             "limit '" + name + "' is for another target kind").with_code("E1028");
        }
        if (rule->second == 0) {
            const auto& value = as_value<std::int64_t>(limit, "a decimal integer limit");
            result.emplace(name, checked_limit(limit, value, name, rule->maximum));
        } else {
            const auto& value = as_value<TomlValue::Array>(limit, "a pair of decimal integer limits");
            const auto& pair = std::get<TomlValue::Array>(value.value);
            if (pair.size() != 2) wrong_kind(limit, "a pair of decimal integer limits");
            result.emplace(name, std::array{checked_limit(limit, pair[0], name, rule->maximum),
                                            checked_limit(limit, pair[1], name, rule->second)});
        }
    }
    return result;
}

WebSettings read_web(const TomlDocument& doc, const TomlTable& table) {
    WebSettings web;
    web.styles = paths(table, "styles");
    if (const auto value = text(table, "routing")) {
        if (value->value == "hash") web.routing = Located<Routing>{Routing::Hash, value->span};
        else if (value->value == "history") web.routing = Located<Routing>{Routing::History, value->span};
        else malformed(value->span, "routing must be 'hash' or 'history'");
    }
    web.origins = read_origins(doc, find(table, "origins"));
    return web;
}

HttpSettings read_http(const TomlDocument& doc, const TomlEntry* entry) {
    if (!entry) return {};
    const auto& table = as_table(doc, *entry);
    keys(table, {"listen", "tls_listen", "admin_listen", "trusted_proxies"});
    return {text(table, "listen"), text(table, "tls_listen"), text(table, "admin_listen"), texts(table, "trusted_proxies")};
}

Named<SqlPool> read_sql(const TomlDocument& doc, const TomlEntry* entry) {
    Named<SqlPool> result;
    if (!entry) return result;
    for (const auto& [name, item] : as_table(doc, *entry).entries) {
        const auto& table = as_table(doc, item);
        keys(table, {"provider", "migrations", "migrate"});
        SqlPool pool{as_text(required(table, "provider")), path(table, "migrations"), std::nullopt};
        if (const auto mode = text(table, "migrate")) {
            if (mode->value == "manual") pool.migrate = Located<MigrationMode>{MigrationMode::Manual, mode->span};
            else if (mode->value == "on_start") pool.migrate = Located<MigrationMode>{MigrationMode::OnStart, mode->span};
            else malformed(mode->span, "migration mode must be 'manual' or 'on_start'");
        }
        result.emplace(name, std::move(pool));
    }
    return result;
}

ConfigSettings read_config(const TomlDocument& doc, const TomlEntry* entry) {
    if (!entry) return {};
    const auto& table = as_table(doc, *entry);
    keys(table, {"prefix", "expose"});
    return {text(table, "prefix"), texts(table, "expose")};
}

ServerSettings read_server(const TomlDocument& doc, const TomlTable& table) {
    std::optional<std::vector<Located<Origin>>> connect;
    if (const auto* entry = find(table, "origins")) {
        const auto& origins = as_table(doc, *entry);
        keys(origins, {"connect"});
        connect = read_connect_origins(origins);
    }
    return {read_http(doc, find(table, "http")), std::move(connect), read_sql(doc, find(table, "sql")),
            read_config(doc, find(table, "config")), texts(table, "providers")};
}

std::vector<Text> read_bridge(const TomlDocument& doc, const TomlEntry* entry) {
    if (!entry) return {};
    const auto& table = as_table(doc, *entry);
    keys(table, {"operations"});
    return texts(table, "operations");
}

DesktopPackaging read_desktop_packaging(const TomlDocument& doc, const TomlEntry* entry) {
    if (!entry) return {};
    const auto& table = as_table(doc, *entry);
    keys(table, {"icon", "formats", "sign_identity"});
    return {path(table, "icon"), text(table, "sign_identity"), texts(table, "formats")};
}

DesktopSettings read_desktop(const TomlDocument& doc, const TomlTable& table, const Span& target_key) {
    const auto* ui = find(table, "ui");
    if (!ui) invalid_target(target_key, "a desktop target requires 'ui' naming its web target");
    return {as_text(*ui), read_bridge(doc, find(table, "bridge")), read_origins(doc, find(table, "origins")),
            read_desktop_packaging(doc, find(table, "packaging")), read_sql(doc, find(table, "sql")), texts(table, "providers")};
}

Located<TargetKind> read_kind(const TomlTable& table) {
    const auto kind = as_text(required(table, "kind"));
    if (kind.value == "web") return {TargetKind::Web, kind.span};
    if (kind.value == "server") return {TargetKind::Server, kind.span};
    if (kind.value == "desktop") return {TargetKind::Desktop, kind.span};
    invalid_target(kind.span, "expected a target kind of 'web', 'server' or 'desktop'");
}

Target read_target(const TomlDocument& doc, const TomlEntry& entry, const Named<Package>& packages) {
    const auto& table = as_table(doc, entry);
    auto kind = read_kind(table);
    switch (kind.value) {
    case TargetKind::Web:
        keys(table, {"kind", "package", "entry", "capabilities", "styles", "routing", "origins", "limits"});
        break;
    case TargetKind::Server:
        keys(table, {"kind", "package", "entry", "capabilities", "http", "origins", "sql", "config", "providers", "limits"});
        break;
    case TargetKind::Desktop:
        keys(table, {"kind", "package", "entry", "capabilities", "ui", "bridge", "origins", "packaging",
                     "sql", "providers", "limits"});
        break;
    }
    const auto* package_key = find(table, "package");
    if (!package_key) invalid_target(entry.key_span, "a target requires 'package' naming a project package");
    auto package = as_text(*package_key);
    if (!packages.contains(package.value)) invalid_target(package.span, "unknown target package '" + package.value + "'");
    auto module = as_text(required(table, "entry"));
    if (module.value.empty()) malformed(module.span, "entry requires a module name");
    auto capabilities = read_capabilities(required(table, "capabilities"), kind.value);
    auto limits = read_limits(doc, find(table, "limits"), kind.value);

    std::variant<WebSettings, ServerSettings, DesktopSettings> settings;
    switch (kind.value) {
    case TargetKind::Web: settings = read_web(doc, table); break;
    case TargetKind::Server: settings = read_server(doc, table); break;
    case TargetKind::Desktop: settings = read_desktop(doc, table, entry.key_span); break;
    }
    return {table.span, std::move(kind), std::move(package), std::move(module), std::move(capabilities),
            std::move(limits), std::move(settings)};
}

void check_package_graph(const Named<Package>& packages, const Named<ExternalDependency>& external) {
    struct Visit {
        Named<Package>::const_iterator package;
        std::size_t edge = 0;
    };
    enum class State { Active, Complete };
    Named<State> states;
    std::vector<Visit> pending;
    for (auto start = packages.begin(); start != packages.end(); ++start) {
        if (!states.emplace(start->first, State::Active).second) continue;
        pending.push_back({start});
        while (!pending.empty()) {
            auto& visit = pending.back();
            const auto& depends = visit.package->second.depends;
            if (visit.edge == depends.size()) {
                states.at(visit.package->first) = State::Complete;
                pending.pop_back();
                continue;
            }
            const auto& dependency = depends[visit.edge++];
            const auto target = packages.find(dependency.value);
            if (target == packages.end()) {
                if (external.contains(dependency.value)) continue;
                malformed(dependency.span, "unknown package dependency '" + dependency.value + "'");
            }
            const auto [state, inserted] = states.emplace(dependency.value, State::Active);
            if (inserted) {
                pending.push_back({target});
            } else if (state->second == State::Active) {
                const auto first = std::ranges::find(pending, target, &Visit::package);
                std::string cycle;
                for (auto step = first; step != pending.end(); ++step) cycle += step->package->first + " -> ";
                malformed(dependency.span, "package dependency cycle " + cycle + dependency.value);
            }
        }
    }
}

void check_desktop_targets(const Named<Target>& targets) {
    for (const auto& [name, target] : targets) {
        const auto* desktop = std::get_if<DesktopSettings>(&target.settings);
        if (!desktop) continue;
        const auto& ui = desktop->ui;
        const auto web = targets.find(ui.value);
        if (web == targets.end() || web->second.kind.value != TargetKind::Web) {
            invalid_target(ui.span, "desktop target '" + name + "' needs a web target for 'ui'");
        }
    }
}

} // namespace

bool valid_project_name(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, name_char);
}

bool valid_project_id(std::string_view id) {
    return id.find('.') != std::string_view::npos && dns_name(id, true);
}

bool valid_project_version(std::string_view version) { return version_core(version); }

std::optional<Origin> parse_origin(std::string_view text) { return origin_parts(text); }

std::optional<Capability> target_capability(TargetKind kind, std::string_view name) {
    const auto found = std::ranges::find(capability_names, name, &std::pair<std::string_view, Capability>::first);
    if (found == std::end(capability_names) || !capability_allowed(found->second, kind)) return std::nullopt;
    return found->second;
}

std::string_view capability_name(Capability capability) {
    const auto found = std::ranges::find(capability_names, capability, &std::pair<std::string_view, Capability>::second);
    return found == std::end(capability_names) ? std::string_view{} : found->first;
}

std::optional<LimitBounds> target_limit_bounds(TargetKind kind, std::string_view name) {
    const auto found = std::ranges::find(limit_rules, name, &LimitRule::name);
    if (found == std::end(limit_rules) || !limit_allowed(name, kind)) return std::nullopt;
    return LimitBounds{found->maximum, found->second};
}

Manifest parse_manifest(std::string_view bytes, std::string_view file) {
    const auto doc = parse_toml(bytes, file);
    const auto& top = doc.root();
    keys(top, {"schema", "project", "packages", "dependencies", "targets", "contracts"});
    const auto& schema = as_value<std::int64_t>(required(top, "schema"), "a decimal integer");
    if (std::get<std::int64_t>(schema.value) != 1) {
        throw Diagnostic(schema.span, "unsupported manifest schema; this SDK supports schema 1").with_code("E1021");
    }
    Manifest project;
    project.schema = {std::get<std::int64_t>(schema.value), schema.span};
    project.project = read_identity(as_table(doc, required(top, "project")));

    const auto& packages = as_table(doc, required(top, "packages"));
    if (packages.entries.empty()) malformed(packages.span, "declare at least one project package");
    for (const auto& [name, package] : packages.entries) {
        project.packages.emplace(name, read_package(as_table(doc, package)));
    }
    if (const auto* item = find(top, "dependencies")) {
        for (const auto& [name, dependency] : as_table(doc, *item).entries) {
            if (project.packages.contains(name)) {
                throw Diagnostic(dependency.key_span, "external dependency '" + name + "' has the same name as a project package")
                    .with_code("E1020").label(project.packages.at(name).span, "project package declared here");
            }
            project.dependencies.emplace(name, read_dependency(as_table(doc, dependency)));
        }
    }
    check_package_graph(project.packages, project.dependencies);
    if (const auto* item = find(top, "contracts")) {
        const auto& table = as_table(doc, *item);
        keys(table, {"manifest"});
        project.contracts_manifest = local_path(as_text(required(table, "manifest")));
    }
    if (const auto* item = find(top, "targets")) {
        for (const auto& [name, target] : as_table(doc, *item).entries) {
            project.targets.emplace(name, read_target(doc, target, project.packages));
        }
        check_desktop_targets(project.targets);
    }
    return project;
}

} // namespace neura::project
