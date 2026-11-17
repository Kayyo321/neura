#pragma once

#include "ir.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura {

// Calls on module receivers. A module function is reached by a virtual call
// whose receiver names the module (`console.print_line`) or is the enclosing
// module's own receiver; the converter resolves it for the concrete module
// (Backend.md). The target is the same in every run when the receiver names
// the module, or when it is a module receiver and no module derives from
// that module.
class ModuleDispatch {
public:
    explicit ModuleDispatch(const IrProgram& program);

    // The module a virtual call's receiver is in every run, or no_symbol.
    SymbolId module(const IrNode& call) const;
    // The function with a body that a virtual call reaches in every run, or
    // no_symbol when it depends on the run or cannot be resolved.
    SymbolId target(const IrNode& call) const;
    // True when another module derives from `module`.
    bool derived(SymbolId module) const { return derived_.contains(module); }

private:
    const IrProgram& program_;
    std::set<SymbolId> derived_;
    std::map<std::pair<SymbolId, std::string>, std::vector<SymbolId>> members_;
};

} // namespace neura
