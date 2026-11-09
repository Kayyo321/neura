#pragma once

#include "neura-ir/ir.hpp"

#include <optional>
#include <vector>

namespace neura::optimization {

// The value of a Boolean literal, or nothing for any other node.
std::optional<bool> boolean_literal(const IrNode& node);

// True when evaluating `node` cannot change state, request an effect, fail,
// or diverge, so that removing the evaluation preserves behavior. The
// predicate is deliberately narrow: Boolean and integer literals, loads of
// locals and parameters, `!`, comparisons of bool or integer operands, and
// short-circuit operations built only from such operands. Everything else,
// including arithmetic that may overflow, calls, field and element access,
// allocation, and loads of globals, is treated as effectful.
bool pure_and_total(const IrProgram& program, const IrNode& node);

// True when control cannot continue to the statement after `statement` in
// the same region: return, break, continue, a block whose region never
// completes, a branch whose arms both never complete, or `while (true)`
// without a break that leaves it.
bool never_completes(const IrNode& statement);

} // namespace neura::optimization
