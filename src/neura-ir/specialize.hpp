#pragma once

#include "ir.hpp"

#include <cstddef>

namespace neura {

struct SpecializationLimits {
    // Nesting of generic instances and collections in one type argument, as
    // in Box<Box<int32>[]>. A specialization that keeps nesting deeper, such
    // as a function that calls itself with Box<T>, stops here.
    std::size_t max_depth = 32;
    // Class and function instances in one program.
    std::size_t max_instances = 4096;
};

// Generic specialization (monomorphization). Every generic class and
// function that non-generic code instantiates gets a concrete copy per
// declaration and distinct type arguments, and every use is rewritten to the
// copy. Copies are appended to the symbol table and keep the declaration's
// spans; their names spell the arguments, as in `Box<int32>`, which no source
// declaration can use. Instances are cached, so equal arguments share one copy
// and the result does not depend on discovery order beyond symbol numbering.
//
// Afterward no reachable body or non-generic symbol mentions a type
// parameter, and no call carries type arguments. The generic declarations
// themselves stay in the program, unreachable, so that diagnostics and
// listings can still name them.
//
// The program must be verified first. Throws a Diagnostic at the
// instantiating site when the nesting or instance limit is exceeded.
void specialize(IrProgram& program, const SpecializationLimits& limits = {});

} // namespace neura
