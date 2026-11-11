#pragma once

#include "neura-ir/ir.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <vector>

namespace neura::optimization {

// How a body uses one of its locals or parameters. A local is written only
// by its Initialize, by stores whose destination loads it, and by a task
// scope that binds it; `other` records any other node that names it, such
// as a reference binding, which makes every analysis leave it alone.
struct LocalUse {
    std::size_t initializes = 0;
    std::size_t stores = 0;
    std::size_t loads = 0;
    bool other = false;
    const IrNode* initializer = nullptr;
};

std::map<SymbolId, LocalUse> scan_locals(const IrProgram& program, const std::vector<IrNode>& body);

bool is_local(const IrProgram& program, SymbolId symbol);

// True when `left` and `right` are the same expression: equal operations,
// types, symbols, text, and children, whatever their spans.
bool same_expression(const IrNode& left, const IrNode& right);

std::size_t node_count(const IrNode& node);

// Calls `visit` on each statement region of `body`, innermost first: the
// body itself and the regions of blocks, branches, loops (body and latch),
// and task scopes, including those nested in expressions.
void each_statement_region(std::vector<IrNode>& body, const std::function<void(std::vector<IrNode>&)>& visit);

// True for a function body, as opposed to a file or module initializer.
bool function_body(const IrProgram& program, const IrBody& body);

} // namespace neura::optimization
