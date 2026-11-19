#pragma once

#include "ir.hpp"

#include <functional>

namespace neura {

// docs/design/Lifetime030.md, `using`: a resource bound by `using` is
// closed when its block is left, so it may not escape the block. It is not
// returned, stored in a field, a global, an element or a binding of an outer
// block, sent on a channel, captured by a function value that escapes, or
// passed to a parameter whose retention summary (purity.hpp) is "retained
// elsewhere" or unknown; a value whose summary says it retains the resource
// is scoped in the same way. A `spawn` whose function value captures it is
// accepted only inside a task scope that is itself inside the block.
//
// The check reads the linked program's NeuraIR: a `using` block is the
// statement list after the initialization of its token `$using<N>`, which
// copies the binding. `opaque` is as for PurityAnalysis. Throws E0475.
void check_scoped_resources(const IrProgram& program, const std::function<bool(SymbolId)>& opaque);

} // namespace neura
