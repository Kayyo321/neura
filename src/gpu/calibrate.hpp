#pragma once

#include "device.hpp"
#include "nvm/placement.hpp"

#include <optional>

namespace neura::gpu {

struct Calibration {
    nvm::DeviceModel device_model;
    nvm::CpuModel cpu_model;
};

// Measures a context that has no live graph device. A missing result means
// a probe declined, differed from the reference, or failed a timing check.
std::optional<Calibration> calibrate(Context& context);

// Opening and calibration are deferred until AutoDevice calls the opener.
nvm::Opener make_opener(const nvr::Image& image, ContextOptions options);

} // namespace neura::gpu
