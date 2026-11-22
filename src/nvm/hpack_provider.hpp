#pragma once

#include "provider_internal.hpp"

namespace neura::nvm::provider {

// Independent RFC 7541 field sections; no network state or capability.
void add_hpack_services(const Adder& add);

} // namespace neura::nvm::provider
