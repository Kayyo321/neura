#pragma once

#include "ir.hpp"

namespace neura {

// docs/design/Expressions030.md, typeof. Replaces each TypeOf whose types
// are concrete with existing operations: the constant parts of its static
// type, or for a class or interface reference and an `any` a selection on
// the value's class tests and tag. Run by specialize() once generic code
// is specialized; a TypeOf left in a generic declaration is never converted.
void expand_type_of(IrProgram& program);

} // namespace neura
