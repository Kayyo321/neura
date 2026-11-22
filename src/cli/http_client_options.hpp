#pragma once

#include "nvm/http_fetch_provider.hpp"
#include "nvm/providers.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace neura::cli {

// Shared by the direct runner and the server host and its workers.
// --allow-http-client grants every origin, --allow-http-client=a,b only the
// listed origins (added to a list already in effect), --allow-http-client=
// none, and --deny-http-client revokes the grant and its list.
inline bool parse_http_client_option(std::string_view argument, nvm::Capabilities& capabilities) {
    constexpr std::string_view allow = "--allow-http-client";
    constexpr std::string_view scoped = "--allow-http-client=";
    if (argument == allow || argument == "--deny-http-client") {
        capabilities.http_client = argument == allow;
        capabilities.http_client_origins.reset();
        return true;
    }
    if (!argument.starts_with(scoped)) return false;

    auto list = argument.substr(scoped.size());
    std::vector<std::string> origins;
    if (!list.empty()) {
        for (;;) {
            const auto comma = list.find(',');
            const auto origin = list.substr(0, comma);
            if (origin.empty()) {
                throw std::runtime_error("--allow-http-client= expects a comma-separated list of origins");
            }
            if (!nvm::provider::valid_http_client_origin(origin)) {
                throw std::runtime_error("--allow-http-client: '" + std::string(origin) +
                                         "' is not an http or https origin (scheme, host and optional port)");
            }
            origins.emplace_back(origin);
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
    }
    auto& current = capabilities.http_client_origins;
    if (!capabilities.http_client || !current || origins.empty()) current.emplace();
    for (auto& origin : origins) current->push_back(std::move(origin));
    capabilities.http_client = true;
    return true;
}

inline std::string http_client_option(const std::vector<std::string>* origins) {
    std::string option = "--allow-http-client";
    if (origins) {
        option += '=';
        for (std::size_t i = 0; i < origins->size(); ++i) {
            if (i != 0) option += ',';
            option += (*origins)[i];
        }
    }
    return option;
}

} // namespace neura::cli
