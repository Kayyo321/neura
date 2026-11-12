#pragma once

// Pure parsing for the opt-in GitHub prerelease channel. Transport and all
// archive verification remain in lifecycle.cpp.
#include "install_layout.hpp"
#include "tooling/json.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura::lifecycle {

struct PublishedRelease {
    std::string tag;
    std::string version;
};

struct ReleasePage {
    std::size_t count = 0;
    std::vector<PublishedRelease> releases;
};

inline std::optional<ReleasePage> parse_github_release_page(std::string_view text, std::string& error) {
    try {
        const auto json = tooling::Json::parse(text);
        if (!json.is_array()) {
            error = "expected a GitHub releases array";
            return std::nullopt;
        }
        ReleasePage page;
        page.count = json.size();
        for (const auto& entry : json.items()) {
            if (!entry.is_object() || !entry["draft"].is_bool() || !entry["prerelease"].is_bool() ||
                !entry["tag_name"].is_string()) {
                error = "a GitHub release has missing or invalid fields";
                return std::nullopt;
            }
            if (entry["draft"].boolean() || !entry["published_at"].is_string() || entry["published_at"].string().empty()) continue;
            const auto tag = entry["tag_name"].string();
            const std::string version = tag.starts_with('v') ? tag.substr(1) : tag;
            // Strict SemVer also restricts the tag to characters safe in the
            // fixed repository download URL; never use an API-provided URL.
            if (!parse_semver(version)) continue;
            page.releases.push_back({tag, version});
        }
        return page;
    } catch (const std::exception& problem) {
        error = std::string("invalid GitHub releases JSON: ") + problem.what();
        return std::nullopt;
    }
}

inline void select_newest_release(std::optional<PublishedRelease>& selected, const ReleasePage& page) {
    for (const auto& release : page.releases) {
        if (!selected || is_newer(release.version, selected->version)) selected = release;
    }
}

} // namespace neura::lifecycle
