#pragma once

#include "ir.hpp"

#include <map>
#include <string_view>

namespace neura {

struct LinkedImports {
    IrProgram program;
    std::map<std::pair<std::string, std::string>, SymbolId> modules;
};

// Merge a lowered compilation unit into a single symbol space. Files with
// identical identities must have compatible definitions after remapping.
// The result maps every symbol in incoming to its symbol in destination.
std::vector<SymbolId> link_ir(IrProgram& destination, IrProgram incoming, const Span& import_site,
                              bool legacy_signatures = false);

// Older artifacts encode an extra overload branch's group in its name.
SymbolId legacy_overload_group(std::string_view name);

} // namespace neura
