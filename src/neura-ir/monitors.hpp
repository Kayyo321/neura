#pragma once

// Monitors and the checked class tags (docs/lang/Concurrency.md, "Monitors";
// docs/lang/Tags.md): #Synchronized methods, the declaration rules of a
// monitor class that lowering records as class.shareable, the #Immutable
// rules, and the lowering of synchronized methods onto the scheduler's guard
// operations in a program that spawns tasks.

#include "ir.hpp"

#include <functional>
#include <optional>
#include <string>

namespace neura {

// The method carries the NeuraIR `synchronized` attribute.
bool synchronized_method(const IrSymbol& symbol);
// The class or one of its bases declares a synchronized method.
bool has_synchronized_methods(const IrProgram& program, SymbolId cls);
// The umbrella class `$class`, every class's implicit root.
bool umbrella_class(const IrProgram& program, SymbolId cls);

// A rule a declaration breaks, with the member that breaks it.
struct MemberProblem {
    std::string text;
    Span span;
};

// The first declaration rule of a monitor (Concurrency.md section 8.1) that
// `cls` breaks: a base that is not a monitor, an exported method that is not
// synchronized, or an exported field that can be assigned or has a mutable
// type and no accessors. Nothing when the class is a monitor by its
// declaration. Whether its instances may be shared is decided over the
// linked program (TaskAnalysis::shareable).
std::optional<MemberProblem> monitor_problem(const IrProgram& program, SymbolId cls);

// The checks of the declaration-level tags, run by lowering once every symbol
// is resolved, on the symbols from `first` on (the ones this compilation
// declared). Sets the `synchronized` attribute on #Synchronized methods, on
// the exported methods of a #Synchronized class, and on the accessors of a
// class with synchronized methods; rejects a misplaced #Synchronized (E0921)
// and an override that drops it (E0922); checks #Immutable (E0918-E0920);
// and records class.shareable on each monitor. `name_span` gives the span of
// a declaration's name.
void declare_checked_tags(IrProgram& program, SymbolId first, const std::function<Span(SymbolId)>& name_span);

// In a program that spawns a task, makes each synchronized method take its
// receiver's guard: the method's body moves to a hidden method named
// `$locked.<name>`, and the method enters the guard, calls it, and exits the
// guard. A call on the receiver itself from a locked body calls the locked
// method, so a synchronized method may call its siblings. The base-most class
// with synchronized methods opens the guard among its field initializers. In
// a program without a spawn this does nothing, so the methods stay plain.
void lower_synchronized(IrProgram& program);

} // namespace neura
