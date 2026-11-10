#pragma once

#include "nvr/image.hpp"

#include <cstdint>
#include <string>

namespace neura::nvm {

// Formats a validated Boolean, integer, or float from its NVR encoding.
std::string scalar_text(const nvr::Type& type, std::uint64_t bits);

} // namespace neura::nvm
