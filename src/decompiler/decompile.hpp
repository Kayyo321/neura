#pragma once

#include "lifter.hpp"
#include "nvr/image.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace neura::decompiler {

struct DecompileOptions {
    LiftOptions lift;
    // Use the NAME section and source map for labels and comments.
    bool provenance = true;
};

struct Coverage {
    std::size_t blocks = 0;
    std::size_t decoded = 0;     // nothing to lift
    std::size_t structural = 0;  // lifted, regenerated identically
    std::size_t exhaustive = 0;  // lifted, equal on every input
    std::size_t circuit = 0;     // shown as a circuit
    std::size_t loops = 0;       // reconstructed loop headers
};

struct Decompilation {
    std::string text;
    Coverage coverage;
    // The lifting result of every block, by function and block.
    std::vector<std::vector<LiftedBlock>> blocks;
};

// The typed reconstruction of a loaded image (docs/Decompiler.md,
// Reconstruction form). Behavior comes only from the executable tables and
// graphs; names and source locations are labeled as provenance.
Decompilation decompile(const nvr::Image& image, const DecompileOptions& options = {});

// An image in which every structurally lifted block's graph is replaced by
// the graph regenerated from its lifted operations. Behavior must be equal to
// the original's; the decompiler tests run both.
nvr::Image resynthesize(const nvr::Image& image, const Decompilation& decompilation);

} // namespace neura::decompiler
