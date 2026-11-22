#pragma once

#include "ir.hpp"

#include <cstddef>

namespace neura {

// Infers return states, checks reference accesses and argument contracts,
// and records narrowed reads in source bodies added by lowering.
void analyze_nil_references(IrProgram& program, std::size_t first_body);

} // namespace neura
