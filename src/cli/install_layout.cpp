#include "install_layout.hpp"

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

std::optional<Marker> parse_marker(std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    if (!split_lines(text, lines, error)) return std::nullopt;
    if (lines.size() < 4 || lines[0] != "neura-install 1") {
        error = "the first line is not \"neura-install 1\" or the file is incomplete";
        return std::nullopt;
    }
    Marker marker;
    const auto value = [&](std::string_view line, std::string_view key) -> std::optional<std::string> {
        if (!line.starts_with(key) || line.size() <= key.size() + 1 || line[key.size()] != ' ') return std::nullopt;
        std::string_view rest = line.substr(key.size() + 1);
        if (rest.find_first_of(" \t") != std::string_view::npos) return std::nullopt;
        return std::string(rest);
    };
    const auto method = value(lines[1], "method");
    if (!method || !is_method_name(*method)) {
        error = "line 2 is not \"method <name>\"";
        return std::nullopt;
    }
    marker.method = *method;
    // A link path can hold spaces, so it takes the rest of the line.
    if (!lines[2].starts_with("link /") || lines[2].find_first_of("\t") != std::string_view::npos) {
        error = "line 3 is not \"link <absolute path>\"";
        return std::nullopt;
    }
    marker.link = std::string(lines[2].substr(5));
    std::set<std::string> seen;
    for (std::size_t i = 3; i < lines.size(); ++i) {
        const auto version = value(lines[i], "version");
        if (!version || !parse_semver(*version)) {
            error = "line " + std::to_string(i + 1) + " is not \"version <semantic version>\"";
            return std::nullopt;
        }
        if (!seen.insert(*version).second) {
            error = "line " + std::to_string(i + 1) + " repeats version " + *version;
            return std::nullopt;
        }
        marker.versions.push_back(*version);
    }
    return marker;
}

std::string format_marker(const Marker& marker) {
    std::string text = "neura-install 1\nmethod " + marker.method + "\nlink " + marker.link + "\n";
    for (const auto& version : marker.versions) text += "version " + version + "\n";
    return text;
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
    // <root>/versions/<version>/bin/neura
    const auto bin = executable.parent_path();
    const auto version_dir = bin.parent_path();
    const auto versions = version_dir.parent_path();
    if (executable.empty() || bin.filename() != "bin" || versions.filename() != "versions" || !version_dir.has_filename()) {
        return detection;
    }
    const auto root = versions.parent_path();
    const auto name = version_dir.filename().string();
    if (!parse_semver(name)) return detection;
    std::error_code error;
    const auto marker_path = root / marker_name;
    if (!fs::is_regular_file(marker_path, error)) return detection;
    detection.root = root;
    detection.running_version = name;
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
