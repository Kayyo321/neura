#pragma once

// Class hooks (docs/design/Hooks030.md). A class that declares a hook clause
// carries the attribute hook.get or hook.set, whose payload is the clause's
// hidden function `$class_get(instance, $policy)` or
// `$class_set(value, instance, $policy)`, a static function of the class
// marked hook.of. A class without a clause of a kind inherits the nearest
// base's clause of that kind; with none its events of that kind are identity.

#include "ir.hpp"

namespace neura {

enum class HookKind : std::uint8_t { Get = 0, Set = 1 };

inline const char* hook_word(HookKind kind) { return kind == HookKind::Get ? "get" : "set"; }

// The hidden function of the clause of `kind` that `cls` declares, or none.
SymbolId declared_hook(const IrSymbol& cls, HookKind kind);
// The clause of `kind` that applies to `cls`: its own, or the nearest base's.
SymbolId effective_hook(const IrProgram& program, SymbolId cls, HookKind kind);
// The class whose clause `function` is, when it is a hidden hook function.
SymbolId hook_owner(const IrSymbol& function);
// True when some class of the program declares a clause.
bool has_class_hooks(const IrProgram& program);

// Step 3 of the milestone: a program with a clause is checked but cannot be
// converted yet. Throws for such a program and does nothing otherwise.
void require_executable_hooks(const IrProgram& program);

} // namespace neura
