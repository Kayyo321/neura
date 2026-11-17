#pragma once

// The environment that the runtime's cached readers see (docs/runtime/NVM.md,
// "Controls"; docs/design/Service030.md, "The worker"). Some readers of
// NEURA_TIER*, NEURA_JIT and the tier 3 controls keep their first answer for
// the life of the process. A worker of the runtime service is started before
// it knows its application and is given the caller's environment only then,
// so it holds the environment until it has replaced it; a cached reader that
// runs while it is held would keep the supervisor's value, and aborts the
// process instead. Every other process starts with the environment bound.

namespace neura::nvm {

// Marks the environment as not yet the application's. Called once, first,
// by `neurad --worker`.
void hold_environment();

// Marks the environment as the application's: called after it is replaced.
void bind_environment();

bool environment_bound();

// Called by each reader that keeps its first answer, before it reads.
// Aborts with a message naming `variable` when the environment is held.
void require_bound_environment(const char* variable);

} // namespace neura::nvm
