#pragma once

#include "device.hpp"
#include "nvm/placement.hpp"

#include <optional>
#include <string>

namespace neura::gpu {

struct Calibration {
    nvm::DeviceModel device_model;
    nvm::CpuModel cpu_model;
};

// Measures a context that has no live graph device. A missing result means
// a probe declined, differed from the reference, or failed a timing check,
// or that memory ran out. In the last case, and only then, `reason` (when
// given) is set to "cannot allocate device memory". Allocation failure never
// throws out of this function.
std::optional<Calibration> calibrate(Context& context, std::string* reason = nullptr);

// Opening and calibration are deferred until AutoDevice calls the opener.
nvm::Opener make_opener(const nvr::Image& image, ContextOptions options);

} // namespace neura::gpu
