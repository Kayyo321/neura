#pragma once

// Class hooks (docs/design/Hooks030.md). A class that declares a hook clause
// carries the attribute hook.get or hook.set, whose payload is the clause's
// hidden function `$class_get(instance, $policy)` or
// `$class_set(value, instance, $policy)`, a static function of the class
// marked hook.of. A class without a clause of a kind inherits the nearest
// base's clause of that kind; with none its events of that kind are identity.

#include "ir.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

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

// The clauses of a linked, specialized program, by the types they apply to.
// A class, interface or `class` type is policy-carrying when some class
// assignable to it has an effective clause.
class HookIndex {
public:
    explicit HookIndex(const IrProgram& program);
    bool any() const { return !views_.empty(); }
    bool carrying(const IrType& type) const;
    // The distinct clause functions of `kind` that a reference of `type` may
    // run, in ascending symbol order. Empty when the event is identity.
    std::vector<SymbolId> targets(const IrType& type, HookKind kind) const;

private:
    struct View {
        std::vector<SymbolId> get, set;
    };
    std::map<SymbolId, View> views_;
};

// Derives the events of the linked, specialized program (docs/design/
// Hooks030.md, Derivation) and wraps each operand in IrOp::HookRead or
// IrOp::HookAdmit, then reports E0544 and E0546. Does nothing when the
// program declares no clause, so that its bytes are unchanged.
void elaborate_class_hooks(IrProgram& program);
// The derivation alone, for the verifier: the program with its hook nodes
// removed and derived again, compared with `program`. The message of the
// first difference, or nothing when every event is wrapped exactly once.
std::optional<std::string> check_class_hook_events(const IrProgram& program);
// A copy of `node` without HookRead and HookAdmit nodes.
IrNode strip_class_hooks(const IrNode& node);

} // namespace neura
