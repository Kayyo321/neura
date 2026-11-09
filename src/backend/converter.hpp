#pragma once

#include "neura-ir/ir.hpp"
#include "nvr/image.hpp"

#include <cstdint>
#include <vector>

namespace neura::backend {

struct ConversionOptions {
    // Emit the optional SMAP section. Paths are relative to the deepest
    // directory that contains every source file of the program, so the image
    // carries no build-machine directories.
    bool source_map = true;
    // Functions to convert in addition to those reachable from the entry
    // point, for embedders and tests that call them directly. A module
    // function is specialized for the module that declares it.
    std::vector<SymbolId> extra_functions;
    // When not empty, the image has one start function per listed function
    // instead of one for the program's entry point: each runs the program's
    // initialization, calls its function, and returns 0. `neura test` uses
    // this with the test functions, which take no arguments and return void.
    // The first start function is the image's entry.
    std::vector<SymbolId> entries;
};

// What an image function implements.
struct FunctionOrigin {
    enum class Role {
        // An IR function. Module functions are specialized per concrete
        // module receiver; class methods take the receiver as parameter 0.
        Function,
        // Allocation-time initialization of a class: base initialization,
        // field initializers, and the declared constructor body.
        Constructor,
        // Cleanup of a class and its bases, most derived first.
        Destructor,
        // Text concatenation for string `+`.
        Concatenate,
        // Program start: runs top-level statements and module initializers
        // that are not literals, then calls the entry function `symbol`.
        Start,
    };

    Role role = Role::Function;
    SymbolId symbol = no_symbol;
    SymbolId receiver = no_symbol;

    auto operator<=>(const FunctionOrigin&) const = default;
};

struct Conversion {
    nvr::Image image;
    // One entry per image function, in function table order.
    std::vector<FunctionOrigin> functions;
};

// IRtoNeuronConversion. Converts verified, optimized NeuraIR into an NVR
// image whose neuron graphs compute the program's arithmetic, comparisons,
// and branch decisions. Throws a Diagnostic at the first operation whose
// semantics are not defined or not supported; see docs/Backend.md.
Conversion convert(const IrProgram& program, const ConversionOptions& options = {});

struct BuildOptions {
    bool optimize = true;
    bool source_map = true;
};

// The pipeline shared by `neura build` from source and from .nir: verify the
// IR, optimize it unless disabled, and convert it.
Conversion build(IrProgram program, const BuildOptions& options = {});

} // namespace neura::backend
