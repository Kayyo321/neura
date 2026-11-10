#pragma once

#include "nvm/graph_device.hpp"

#include <functional>
#include <memory>

namespace neura::testing {

// Every pair of 8-bit inputs and the boundary and pseudo-random wider inputs
// used by backend-circuits. Each device belongs to one graph's image.
using GraphDeviceFactory = std::function<std::unique_ptr<nvm::GraphDevice>(const nvr::Image&)>;
void check_integer_graph_device(const GraphDeviceFactory& make_device);

} // namespace neura::testing
