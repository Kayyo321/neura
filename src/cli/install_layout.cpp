#include "install_layout.hpp"

#include "util/sha256.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <set>
#include <tuple>

namespace neura::lifecycle {

namespace fs = std::filesystem;

namespace {

bool all_digits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

bool parse_number(std::string_view text, std::uint64_t& value) {
    if (!all_digits(text) || (text.size() > 1 && text.front() == '0')) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// Splits `text` into lines. Every line, the last included, ends with '\n';
// a carriage return or a NUL anywhere is rejected.
bool split_lines(std::string_view text, std::vector<std::string_view>& lines, std::string& error) {
    if (text.empty() || text.back() != '\n') {
        error = "the file does not end with a newline";
        return false;
    }
    if (text.find_first_of(std::string_view("\r\0", 2)) != std::string_view::npos) {
        error = "the file holds a carriage return or a NUL byte";
        return false;
    }
    while (!text.empty()) {
        const auto end = text.find('\n');
        lines.push_back(text.substr(0, end));
        text.remove_prefix(end + 1);
    }
    return true;
}

// Splits a line at single spaces. Empty fields (doubled, leading, or
// trailing spaces) and tabs make the line invalid.
bool split_fields(std::string_view line, std::vector<std::string_view>& fields) {
    if (line.find('\t') != std::string_view::npos) return false;
    while (true) {
        const auto space = line.find(' ');
        const auto field = line.substr(0, space);
        if (field.empty()) return false;
        fields.push_back(field);
        if (space == std::string_view::npos) return true;
        line.remove_prefix(space + 1);
    }
}

bool is_hex_digest(std::string_view text) {
    return text.size() == 64 && std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool is_triple(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '-';
    });
}

bool is_asset_file_name(std::string_view text) {
    if (text.empty() || text.front() == '.' || text.front() == '-') return false;
    return std::ranges::all_of(text, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '-' || c == '+';
    });
}

bool is_method_name(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-'; });
}

} // namespace

std::optional<SemVer> parse_semver(std::string_view text) {
    SemVer version;
    const auto dash = text.find('-');
    std::string_view core = text.substr(0, dash);
    std::uint64_t* fields[] = {&version.major, &version.minor, &version.patch};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto dot = core.find('.');
        if (dot == std::string_view::npos ? i != 2 : i == 2) return std::nullopt;
        if (!parse_number(core.substr(0, dot), *fields[i])) return std::nullopt;
        if (dot != std::string_view::npos) core.remove_prefix(dot + 1);
    }
    if (dash == std::string_view::npos) return version;
    std::string_view rest = text.substr(dash + 1);
    while (true) {
        const auto dot = rest.find('.');
        const auto identifier = rest.substr(0, dot);
        const bool valid = !identifier.empty() && std::ranges::all_of(identifier, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
        });
        if (!valid) return std::nullopt;
        if (all_digits(identifier) && identifier.size() > 1 && identifier.front() == '0') return std::nullopt;
        version.prerelease.emplace_back(identifier);
        if (dot == std::string_view::npos) return version;
        rest.remove_prefix(dot + 1);
    }
}

int compare_semver(const SemVer& left, const SemVer& right) {
    const auto three = [](const SemVer& v) { return std::tie(v.major, v.minor, v.patch); };
    if (three(left) != three(right)) return three(left) < three(right) ? -1 : 1;
    // A version without a prerelease is higher than one with.
    if (left.prerelease.empty() || right.prerelease.empty()) {
        return left.prerelease.empty() == right.prerelease.empty() ? 0 : (left.prerelease.empty() ? 1 : -1);
    }
    const auto common = std::min(left.prerelease.size(), right.prerelease.size());
    for (std::size_t i = 0; i < common; ++i) {
        const auto& a = left.prerelease[i];
        const auto& b = right.prerelease[i];
        if (a == b) continue;
        const bool a_numeric = all_digits(a);
        const bool b_numeric = all_digits(b);
        if (a_numeric != b_numeric) return a_numeric ? -1 : 1;
        if (a_numeric) {
            // No leading zeros, so a longer numeral is larger.
            if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
        }
        return a < b ? -1 : 1;
    }
    if (left.prerelease.size() == right.prerelease.size()) return 0;
    return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

bool is_newer(std::string_view latest, std::string_view current) {
    const auto a = parse_semver(latest);
    const auto b = parse_semver(current);
    return a && b && compare_semver(*a, *b) > 0;
}

namespace {

// `<key> <value>` where the value takes the rest of the line and may hold
// spaces, but no tab.
std::optional<std::string> rest_value(std::string_view line, std::string_view key) {
    if (!line.starts_with(key) || line.size() <= key.size() + 1 || line[key.size()] != ' ') return std::nullopt;
    const auto rest = line.substr(key.size() + 1);
    if (rest.find('\t') != std::string_view::npos) return std::nullopt;
    return std::string(rest);
}

// `<key> <word>`: one field without spaces.
std::optional<std::string> word_value(std::string_view line, std::string_view key) {
    auto value = rest_value(line, key);
    if (!value || value->find(' ') != std::string::npos) return std::nullopt;
    return value;
}

bool is_component_name(std::string_view text) {
    return !text.empty() && text.size() <= 64 && text.front() >= 'a' && text.front() <= 'z' &&
           std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == '-'; });
}

bool is_tool_name(std::string_view text) { return is_component_name(text); }

bool is_build_id(std::string_view text) {
    return !text.empty() && text.size() <= 64 &&
           std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'); });
}

bool is_generation_id(std::string_view text) {
    return text.size() == 12 && std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

int component_rank(std::string_view name) {
    if (name == "core") return 0;
    if (name == "sdk") return 1;
    if (name == "service") return 2;
    return 3;
}

std::optional<Marker> parse_marker_1(const std::vector<std::string_view>& lines, std::string& error) {
    if (lines.size() < 4) {
        error = "the file is incomplete";
        return std::nullopt;
    }
    Marker marker;
    marker.schema = 1;
    const auto method = word_value(lines[1], "method");
    if (!method || !is_method_name(*method)) {
        error = "line 2 is not \"method <name>\"";
        return std::nullopt;
    }
    marker.method = *method;
    // A link path can hold spaces, so it takes the rest of the line.
    if (!lines[2].starts_with("link /") || lines[2].find('\t') != std::string_view::npos) {
        error = "line 3 is not \"link <absolute path>\"";
        return std::nullopt;
    }
    marker.link = std::string(lines[2].substr(5));
    const std::filesystem::path link(marker.link);
    marker.bin = link.parent_path().string();
    marker.links = {link.filename().string()};
    std::set<std::string> seen;
    for (std::size_t i = 3; i < lines.size(); ++i) {
        const auto version = word_value(lines[i], "version");
        if (!version || !parse_semver(*version)) {
            error = "line " + std::to_string(i + 1) + " is not \"version <semantic version>\"";
            return std::nullopt;
        }
        if (!seen.insert(*version).second) {
            error = "line " + std::to_string(i + 1) + " repeats version " + *version;
            return std::nullopt;
        }
        marker.generations.push_back(*version);
    }
    return marker;
}

std::optional<Marker> parse_marker_2(const std::vector<std::string_view>& lines, std::string& error) {
    const auto fail = [&](std::size_t line, const std::string& reason) -> std::optional<Marker> {
        error = "line " + std::to_string(line + 1) + ": " + reason;
        return std::nullopt;
    };
    if (lines.size() < 4) {
        error = "the file is incomplete";
        return std::nullopt;
    }
    Marker marker;
    marker.schema = 2;
    const auto method = word_value(lines[1], "method");
    if (!method || !is_method_name(*method)) return fail(1, "expected \"method <name>\"");
    marker.method = *method;
    const auto bin = rest_value(lines[2], "bin");
    if (!bin || !bin->starts_with('/')) return fail(2, "expected \"bin <absolute path>\"");
    marker.bin = *bin;
    {
        std::vector<std::string_view> fields;
        if (!split_fields(lines[3], fields) || fields[0] != "components" || fields.size() < 2) {
            return fail(3, "expected \"components <name>...\"");
        }
        for (std::size_t i = 1; i < fields.size(); ++i) {
            if (!is_component_name(fields[i])) return fail(3, "not a component name: " + std::string(fields[i]));
            if (!marker.components.empty() && !component_before(marker.components.back(), fields[i])) {
                return fail(3, "the components are not in order, or one is repeated");
            }
            marker.components.emplace_back(fields[i]);
        }
    }
    std::size_t i = 4;
    std::set<std::string> generations;
    for (; i < lines.size() && lines[i].starts_with("generation "); ++i) {
        const auto name = word_value(lines[i], "generation");
        if (!name || !parse_generation_name(*name)) return fail(i, "expected \"generation <generation name>\"");
        if (!marker.generations.empty() && !(marker.generations.back() < *name)) return fail(i, "the generations are not sorted, or one is repeated");
        marker.generations.push_back(*name);
    }
    for (; i < lines.size(); ++i) {
        const auto name = word_value(lines[i], "link");
        if (!name || !is_tool_name(*name)) return fail(i, "expected \"link <tool name>\"");
        if (!marker.links.empty() && !(marker.links.back() < *name)) return fail(i, "the links are not sorted, or one is repeated");
        marker.links.push_back(*name);
    }
    return marker;
}

} // namespace

bool component_before(std::string_view left, std::string_view right) {
    const auto a = component_rank(left);
    const auto b = component_rank(right);
    if (a != b) return a < b;
    return left < right;
}

bool known_component(std::string_view name) { return component_rank(name) < 3; }

bool valid_component_set(const std::vector<std::string>& components) {
    std::vector<std::string> sorted = components;
    std::ranges::sort(sorted, [](const std::string& a, const std::string& b) { return component_before(a, b); });
    return sorted == std::vector<std::string>{"core", "sdk"} || sorted == std::vector<std::string>{"core", "service"} ||
           sorted == std::vector<std::string>{"core", "sdk", "service"};
}

std::optional<Marker> parse_marker(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    if (lines[0] == "neura-install 1") return parse_marker_1(lines, error);
    if (lines[0] == "neura-install 2") return parse_marker_2(lines, error);
    error = "the first line is not \"neura-install 1\" or \"neura-install 2\"";
    return std::nullopt;
}

std::string format_marker(const Marker& marker) {
    auto components = marker.components;
    std::ranges::sort(components, [](const std::string& a, const std::string& b) { return component_before(a, b); });
    auto generations = marker.generations;
    std::ranges::sort(generations);
    generations.erase(std::unique(generations.begin(), generations.end()), generations.end());
    auto links = marker.links;
    std::ranges::sort(links);
    links.erase(std::unique(links.begin(), links.end()), links.end());
    std::string text = "neura-install 2\nmethod " + marker.method + "\nbin " + marker.bin + "\ncomponents";
    for (const auto& component : components) text += " " + component;
    text += "\n";
    for (const auto& generation : generations) text += "generation " + generation + "\n";
    for (const auto& link : links) text += "link " + link + "\n";
    return text;
}

bool is_safe_relative_path(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/') return false;
    const bool characters = std::ranges::all_of(path, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '+' ||
               c == '-' || c == '/';
    });
    if (!characters) return false;
    while (true) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string_view::npos) return true;
        path.remove_prefix(slash + 1);
    }
}

std::optional<ComponentManifest> parse_manifest(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    const auto fail = [&](std::size_t line, const std::string& reason) -> std::optional<ComponentManifest> {
        error = "line " + std::to_string(line + 1) + ": " + reason;
        return std::nullopt;
    };
    if (lines[0] != "neura-component 1") return fail(0, "expected \"neura-component 1\"");
    if (lines.size() < 5) return fail(lines.size() - 1, "the manifest is incomplete");
    ComponentManifest manifest;
    const auto name = word_value(lines[1], "name");
    if (!name || !is_component_name(*name)) return fail(1, "expected \"name <component>\"");
    manifest.name = *name;
    const auto version = word_value(lines[2], "version");
    if (!version || !parse_semver(*version)) return fail(2, "expected \"version <semantic version>\"");
    manifest.version = *version;
    const auto build = word_value(lines[3], "build");
    if (!build || !is_build_id(*build)) return fail(3, "expected \"build <commit>\"");
    manifest.build = *build;
    const auto target = word_value(lines[4], "target");
    if (!target || !is_triple(*target)) return fail(4, "expected \"target <triple>\"");
    manifest.target = *target;
    std::size_t i = 5;
    for (; i < lines.size() && lines[i].starts_with("requires "); ++i) {
        const auto required = word_value(lines[i], "requires");
        if (!required || !is_component_name(*required)) return fail(i, "expected \"requires <component>\"");
        if (std::ranges::find(manifest.requirements, *required) != manifest.requirements.end()) return fail(i, "a repeated requires line");
        manifest.requirements.push_back(*required);
    }
    if (i < lines.size() && lines[i].starts_with("protocol ")) {
        const auto protocol = rest_value(lines[i], "protocol");
        std::vector<std::string_view> fields;
        if (!protocol || !split_fields(*protocol, fields) || fields.size() != 2) return fail(i, "expected \"protocol <name> <version>\"");
        manifest.protocol = *protocol;
        ++i;
    }
    for (; i < lines.size() && lines[i].starts_with("tool "); ++i) {
        const auto tool = word_value(lines[i], "tool");
        if (!tool || !is_tool_name(*tool)) return fail(i, "expected \"tool <name>\"");
        if (!manifest.tools.empty() && !(manifest.tools.back() < *tool)) return fail(i, "the tools are not sorted, or one is repeated");
        manifest.tools.push_back(*tool);
    }
    for (; i < lines.size(); ++i) {
        std::vector<std::string_view> fields;
        if (!split_fields(lines[i], fields) || fields[0] != "file") return fail(i, "unknown line kind");
        if (fields.size() != 5) return fail(i, "expected \"file <mode> <sha256> <size> <path>\"");
        ManifestFile file;
        if (fields[1] == "755") file.mode = 0755;
        else if (fields[1] == "644") file.mode = 0644;
        else return fail(i, "the mode is not 755 or 644");
        if (!is_hex_digest(fields[2])) return fail(i, "the SHA-256 is not 64 lowercase hexadecimal digits");
        file.sha256 = std::string(fields[2]);
        if (!parse_number(fields[3], file.size) || file.size > max_asset_size * 4) return fail(i, "the size is not a number of bytes");
        if (!is_safe_relative_path(fields[4])) return fail(i, "the path is not a safe relative path");
        file.path = std::string(fields[4]);
        if (file.path.starts_with(std::string(components_directory) + "/")) return fail(i, "a manifest does not list the manifests");
        if (!manifest.files.empty() && !(manifest.files.back().path < file.path)) return fail(i, "the files are not sorted by path, or one is repeated");
        manifest.files.push_back(std::move(file));
    }
    for (const auto& tool : manifest.tools) {
        const auto path = "bin/" + tool;
        const auto found = std::ranges::find_if(manifest.files, [&](const ManifestFile& f) { return f.path == path; });
        if (found == manifest.files.end() || found->mode != 0755) {
            error = "the tool " + tool + " has no file bin/" + tool + " with mode 755";
            return std::nullopt;
        }
    }
    return manifest;
}

std::string format_manifest(const ComponentManifest& manifest) {
    std::string text = "neura-component 1\nname " + manifest.name + "\nversion " + manifest.version + "\nbuild " + manifest.build +
                       "\ntarget " + manifest.target + "\n";
    for (const auto& required : manifest.requirements) text += "requires " + required + "\n";
    if (!manifest.protocol.empty()) text += "protocol " + manifest.protocol + "\n";
    auto tools = manifest.tools;
    std::ranges::sort(tools);
    for (const auto& tool : tools) text += "tool " + tool + "\n";
    auto files = manifest.files;
    std::ranges::sort(files, [](const ManifestFile& a, const ManifestFile& b) { return a.path < b.path; });
    for (const auto& file : files) {
        text += std::string("file ") + (file.mode == 0755 ? "755 " : "644 ") + file.sha256 + " " + std::to_string(file.size) + " " +
                file.path + "\n";
    }
    return text;
}

std::string check_known_manifest(const ComponentManifest& manifest) {
    if (!known_component(manifest.name)) return "the component " + manifest.name + " is not one this neura knows";
    const std::vector<std::string> requirements =
        manifest.name == "core" ? std::vector<std::string>{} : std::vector<std::string>{"core"};
    if (manifest.requirements != requirements) {
        return manifest.name == "core" ? "the core component requires nothing" : "the " + manifest.name + " component requires core alone";
    }
    if (manifest.name == "service" ? manifest.protocol != "service 1.0" : !manifest.protocol.empty()) {
        return manifest.name == "service" ? "the service component has no \"protocol service 1.0\" line"
                                          : "only the service component has a protocol line";
    }
    return {};
}

const ComponentArchive* ComponentsMetadata::find(std::string_view product, std::string_view triple) const {
    for (const auto& archive : archives) {
        if (archive.product == product && archive.triple == triple) return &archive;
    }
    return nullptr;
}

std::string product_archive_name(std::string_view product, std::string_view version, std::string_view triple) {
    return "neura-" + std::string(product) + "-" + std::string(version) + "-" + std::string(triple) + ".tar.gz";
}

std::optional<ComponentsMetadata> parse_components_metadata(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    const auto fail = [&](std::size_t line, const std::string& reason) -> std::optional<ComponentsMetadata> {
        error = "line " + std::to_string(line + 1) + ": " + reason;
        return std::nullopt;
    };
    if (lines[0] != "neura-components 1") return fail(0, "expected \"neura-components 1\"");
    if (lines.size() < 3) return fail(lines.size() - 1, "the file is incomplete");
    ComponentsMetadata metadata;
    const auto version = word_value(lines[1], "version");
    if (!version || !parse_semver(*version)) return fail(1, "expected \"version <semantic version>\"");
    metadata.version = *version;
    const auto build = word_value(lines[2], "build");
    if (!build || !is_build_id(*build)) return fail(2, "expected \"build <commit>\"");
    metadata.build = *build;
    std::string previous;
    for (std::size_t i = 3; i < lines.size(); ++i) {
        std::vector<std::string_view> fields;
        if (!split_fields(lines[i], fields) || fields[0] != "archive") return fail(i, "unknown line kind");
        if (fields.size() != 6) return fail(i, "expected \"archive <product> <triple> <file> <sha256> <size>\"");
        ComponentArchive archive;
        if (fields[1] != "sdk" && fields[1] != "runtime") return fail(i, "unknown product \"" + std::string(fields[1]) + "\"");
        archive.product = std::string(fields[1]);
        if (!is_triple(fields[2])) return fail(i, "the target triple is not valid");
        archive.triple = std::string(fields[2]);
        if (fields[3] != product_archive_name(archive.product, metadata.version, archive.triple)) {
            return fail(i, "the file is not " + product_archive_name(archive.product, metadata.version, archive.triple));
        }
        archive.file = std::string(fields[3]);
        if (!is_hex_digest(fields[4])) return fail(i, "the SHA-256 is not 64 lowercase hexadecimal digits");
        archive.sha256 = std::string(fields[4]);
        if (!parse_number(fields[5], archive.size) || archive.size == 0 || archive.size > max_asset_size) {
            return fail(i, "the size is not between 1 byte and 1 GiB");
        }
        if (metadata.find(archive.product, archive.triple)) return fail(i, "a second archive for " + archive.product + " on " + archive.triple);
        const std::string key = std::string(fields[1]) + " " + std::string(fields[2]) + " " + std::string(fields[3]);
        if (!previous.empty() && !(previous < key)) return fail(i, "the archive lines are not sorted");
        previous = key;
        metadata.archives.push_back(std::move(archive));
    }
    return metadata;
}

std::optional<Journal> parse_journal(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    const auto fail = [&](std::size_t line, const std::string& reason) -> std::optional<Journal> {
        error = "line " + std::to_string(line + 1) + ": " + reason;
        return std::nullopt;
    };
    if (lines[0] != "neura-journal 1") return fail(0, "expected \"neura-journal 1\"");
    if (lines.size() < 5) return fail(lines.size() - 1, "the journal is incomplete");
    Journal journal;
    const auto operation = word_value(lines[1], "operation");
    if (!operation || !is_method_name(*operation)) return fail(1, "expected \"operation <name>\"");
    journal.operation = *operation;
    if (lines[2] == "old -") {
        journal.old_generation.clear();
    } else if (const auto old = word_value(lines[2], "old"); old && parse_generation_name(*old)) {
        journal.old_generation = *old;
    } else {
        return fail(2, "expected \"old <generation>\" or \"old -\"");
    }
    const auto next = word_value(lines[3], "new");
    if (!next || !parse_generation_name(*next)) return fail(3, "expected \"new <generation>\"");
    journal.new_generation = *next;
    if (lines[4] == "placed yes") journal.placed = true;
    else if (lines[4] != "placed no") return fail(4, "expected \"placed yes\" or \"placed no\"");
    for (std::size_t i = 5; i < lines.size(); ++i) {
        if (auto path = rest_value(lines[i], "remove"); path && path->starts_with('/')) {
            journal.remove.push_back(*path);
        } else if (auto added = rest_value(lines[i], "add"); added && added->starts_with('/')) {
            journal.add.push_back(*added);
        } else if (lines[i].starts_with("old-marker ")) {
            journal.old_marker += std::string(lines[i].substr(11)) + "\n";
        } else if (lines[i].starts_with("new-marker ")) {
            journal.new_marker += std::string(lines[i].substr(11)) + "\n";
        } else {
            return fail(i, "unknown line kind");
        }
    }
    if (journal.new_marker.empty()) return fail(lines.size() - 1, "the journal has no new marker");
    return journal;
}

std::string format_journal(const Journal& journal) {
    std::string text = "neura-journal 1\noperation " + journal.operation + "\nold " +
                       (journal.old_generation.empty() ? std::string("-") : journal.old_generation) + "\nnew " + journal.new_generation +
                       "\nplaced " + (journal.placed ? "yes" : "no") + "\n";
    for (const auto& path : journal.remove) text += "remove " + path + "\n";
    for (const auto& path : journal.add) text += "add " + path + "\n";
    const auto lines = [&](std::string_view marker, std::string_view key) {
        while (!marker.empty()) {
            const auto end = marker.find('\n');
            text += std::string(key) + " " + std::string(marker.substr(0, end)) + "\n";
            if (end == std::string_view::npos) break;
            marker.remove_prefix(end + 1);
        }
    };
    lines(journal.old_marker, "old-marker");
    lines(journal.new_marker, "new-marker");
    return text;
}

std::optional<GenerationName> parse_generation_name(std::string_view name) {
    const auto plus = name.find('+');
    GenerationName result;
    result.version = std::string(name.substr(0, plus));
    if (!parse_semver(result.version)) return std::nullopt;
    if (plus != std::string_view::npos) {
        result.id = std::string(name.substr(plus + 1));
        if (!is_generation_id(result.id)) return std::nullopt;
    }
    return result;
}

std::string generation_name(std::string_view version, std::vector<std::pair<std::string, std::string>> manifests) {
    std::ranges::sort(manifests, [](const auto& a, const auto& b) { return component_before(a.first, b.first); });
    util::Sha256 hash;
    for (const auto& [name, bytes] : manifests) hash.update(bytes);
    const auto digest = hash.finish();
    std::string hex;
    for (std::size_t i = 0; i < 6; ++i) {
        hex += "0123456789abcdef"[digest[i] >> 4];
        hex += "0123456789abcdef"[digest[i] & 15];
    }
    return std::string(version) + "+" + hex;
}

const ReleaseAsset* ReleaseMetadata::find(std::string_view triple) const {
    for (const auto& asset : assets) {
        if (asset.triple == triple) return &asset;
    }
    return nullptr;
}

std::optional<ReleaseMetadata> parse_release_metadata(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    const auto fail = [&](std::size_t line, const std::string& reason) -> std::optional<ReleaseMetadata> {
        error = "line " + std::to_string(line + 1) + ": " + reason;
        return std::nullopt;
    };
    if (lines.empty() || lines[0] != "neura-release 1") return fail(0, "expected \"neura-release 1\"");
    ReleaseMetadata metadata;
    std::set<std::string> triples;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::vector<std::string_view> fields;
        if (!split_fields(lines[i], fields)) return fail(i, "fields must be separated by single spaces");
        if (fields[0] == "version") {
            if (fields.size() != 2 || !parse_semver(fields[1])) return fail(i, "expected \"version <semantic version>\"");
            if (!metadata.version.empty()) return fail(i, "the version is given twice");
            metadata.version = std::string(fields[1]);
        } else if (fields[0] == "asset") {
            if (metadata.version.empty()) return fail(i, "an asset precedes the version");
            if (fields.size() != 5) return fail(i, "expected \"asset <triple> <file> <sha256> <size>\"");
            ReleaseAsset asset;
            if (!is_triple(fields[1])) return fail(i, "the target triple is not valid");
            if (!is_asset_file_name(fields[2])) return fail(i, "the file name is not valid");
            if (!is_hex_digest(fields[3])) return fail(i, "the SHA-256 is not 64 lowercase hexadecimal digits");
            if (!parse_number(fields[4], asset.size) || asset.size == 0 || asset.size > max_asset_size) {
                return fail(i, "the size is not between 1 byte and 1 GiB");
            }
            asset.triple = std::string(fields[1]);
            asset.file = std::string(fields[2]);
            asset.sha256 = std::string(fields[3]);
            if (!triples.insert(asset.triple).second) return fail(i, "a second asset for " + asset.triple);
            metadata.assets.push_back(std::move(asset));
        } else {
            return fail(i, "unknown line kind \"" + std::string(fields[0]) + "\"");
        }
    }
    if (metadata.version.empty()) return fail(lines.size() - 1, "no version line");
    return metadata;
}

std::optional<UpdateCheck> parse_update_check(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    if (lines.size() != 3 || lines[0] != "neura-update-check 1" || !lines[1].starts_with("checked ") ||
        !lines[2].starts_with("latest ")) {
        error = "not a neura-update-check 1 file";
        return std::nullopt;
    }
    UpdateCheck state;
    std::uint64_t seconds = 0;
    if (!parse_number(lines[1].substr(8), seconds) || seconds > (std::uint64_t{1} << 40)) {
        error = "the check time is not a number of seconds";
        return std::nullopt;
    }
    state.checked = static_cast<std::int64_t>(seconds);
    state.latest = std::string(lines[2].substr(7));
    if (!parse_semver(state.latest)) {
        error = "the latest version is not a semantic version";
        return std::nullopt;
    }
    return state;
}

std::string format_update_check(const UpdateCheck& state) {
    return "neura-update-check 1\nchecked " + std::to_string(state.checked) + "\nlatest " + state.latest + "\n";
}

bool update_check_due(const std::optional<UpdateCheck>& state, std::int64_t now) {
    if (!state) return true;
    // A recorded time in the future (a clock that moved back) counts as due.
    return state->checked > now || now - state->checked >= update_check_interval_seconds;
}

Detection detect_install(const fs::path& executable) {
    Detection detection;
    // <root>/versions/<generation>/bin/<tool>
    const auto bin = executable.parent_path();
    const auto version_dir = bin.parent_path();
    const auto versions = version_dir.parent_path();
    if (executable.empty() || bin.filename() != "bin" || versions.filename() != "versions" || !version_dir.has_filename()) {
        return detection;
    }
    const auto root = versions.parent_path();
    const auto name = version_dir.filename().string();
    if (!parse_generation_name(name)) return detection;
    std::error_code error;
    const auto marker_path = root / marker_name;
    if (!fs::is_regular_file(marker_path, error)) return detection;
    detection.root = root;
    detection.running_generation = name;
    std::string text;
    {
        std::FILE* file = std::fopen(marker_path.c_str(), "rb");
        if (file == nullptr) {
            detection.kind = Detection::Kind::Broken;
            detection.problem = "cannot read " + marker_path.string();
            return detection;
        }
        char buffer[4096];
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof buffer, file)) > 0 && text.size() < (1u << 20)) text.append(buffer, count);
        std::fclose(file);
    }
    std::string problem;
    if (auto marker = parse_marker(text, problem)) {
        detection.kind = Detection::Kind::Installed;
        detection.marker = std::move(*marker);
    } else {
        detection.kind = Detection::Kind::Broken;
        detection.problem = marker_path.string() + ": " + problem;
    }
    return detection;
}

} // namespace neura::lifecycle
