#pragma once

#include "ir.hpp"

namespace neura {

// docs/design/Lifetime030.md, Events: `Std.Events.Event.emit` runs handlers
// synchronously, so it is not called under a monitor guard. A call of
// `emit` in a `#Synchronized` method, in a lambda passed directly to
// `Guarded.with`, or in a function either of them reaches through calls the
// compiler resolves is E0550. A call through a function value is not
// followed, so this is a check of the known call graph and not a proof.
// Runs on the linked program, and does nothing when it does not contain
// the event module.
void check_event_guards(const IrProgram& program);

} // namespace neura
