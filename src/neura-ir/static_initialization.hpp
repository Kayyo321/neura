#pragma once

#include "ir.hpp"

#include <set>

namespace neura {

// Make first active use explicit in the ordinary IR call and state operations.
void lower_static_initializers(IrProgram& program, const std::set<SymbolId>& singletons);

} // namespace neura
