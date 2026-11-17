#pragma once

#include "util/diagnostic.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace neura::project {

template <typename T>
struct Located {
    T value;
    Span span;
};

using Text = Located<std::string>;

template <typename T>
using Named = std::map<std::string, T, std::less<>>;

struct ProjectIdentity {
    Text name;
    Text id;
    Text version;
    Text language;
};

struct Package {
    Span span;
    Text root;
    std::vector<Text> assets;
    std::vector<Text> depends;
};

enum class DependencySource { Directory, Archive };
struct ExternalDependency {
    Span span;
    DependencySource source;
    Text path;
    Text digest;
};

enum class TargetKind { Web, Server, Desktop };
enum class Capability { Ui, HttpClient, Bridge, HttpServer, Sql, App, Desktop, Fs, Net, Env };

using Limit = Located<std::uint32_t>;
using LimitOverride = std::variant<Limit, std::array<Limit, 2>>;

struct Origin {
    std::string text;
    std::string scheme;
    std::string host;
    std::optional<std::uint16_t> port;
};

struct Origins {
    std::vector<Located<Origin>> connect, load, style, external;
};

enum class Routing { Hash, History };
struct WebSettings {
    std::vector<Text> styles;
    std::optional<Located<Routing>> routing;
    Origins origins;
};

struct HttpSettings {
    std::optional<Text> listen, tls_listen, admin_listen;
    std::vector<Text> trusted_proxies;
};

enum class MigrationMode { Manual, OnStart };
struct SqlPool {
    Text provider;
    std::optional<Text> migrations;
    std::optional<Located<MigrationMode>> migrate;
};

struct ConfigSettings {
    std::optional<Text> prefix;
    std::vector<Text> expose;
};

struct ServerSettings {
    HttpSettings http;
    Named<SqlPool> sql;
    ConfigSettings config;
    std::vector<Text> providers;
};

struct DesktopPackaging {
    std::optional<Text> icon, sign_identity;
    std::vector<Text> formats;
};

struct DesktopSettings {
    Text ui;
    std::vector<Text> operations;
    Origins origins;
    DesktopPackaging packaging;
    Named<SqlPool> sql;
    std::vector<Text> providers;
};

struct Target {
    Span span;
    Located<TargetKind> kind;
    Text package;
    Text entry;
    std::vector<Located<Capability>> capabilities;
    Named<LimitOverride> limits;
    std::variant<WebSettings, ServerSettings, DesktopSettings> settings;
};

struct Manifest {
    Located<std::int64_t> schema;
    ProjectIdentity project;
    Named<Package> packages;
    Named<ExternalDependency> dependencies;
    Named<Target> targets;
    std::optional<Text> contracts_manifest;
};

// Parse and validate the file-local project model. Paths retain their spelling.
// The caller resolves paths and verifies the filesystem and installed components.
Manifest parse_manifest(std::string_view bytes, std::string_view file);

} // namespace neura::project
