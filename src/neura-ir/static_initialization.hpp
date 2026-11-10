#pragma once

#include "ir.hpp"

#include <set>

namespace neura {

// Make first active use explicit in the ordinary IR call and state
// operations. Linked IR already has guards and initialization regions.
void lower_static_initializers(IrProgram& program, const std::set<SymbolId>& singletons,
                               std::size_t linked_symbols = 0, std::size_t linked_bodies = 0);

} // namespace neura
