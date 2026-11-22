#pragma once
#include "provider_internal.hpp"
#include <string_view>
namespace neura::nvm::provider {
void add_http_fetch_services(const OverlapAdder& add, const Capabilities& capabilities);
// Whether text is an origin an http_client grant can list: http or https,
// a DNS name or IP literal, an optional port, and no path beyond "/".
bool valid_http_client_origin(std::string_view text);
}
