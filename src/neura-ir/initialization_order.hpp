#pragma once

#include "ir.hpp"

#include <vector>

namespace neura {

// The order in which a program's files initialize (Modules.md,
// Initialization): files that a file imports first, ties in symbol order,
// and the files of an import cycle together once everything the cycle
// imports from outside it is placed.
std::vector<SymbolId> file_initialization_order(const IrProgram& program);

} // namespace neura
