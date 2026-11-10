#pragma once

#include "ir.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace neura {

// The isolation rules of docs/lang/Concurrency.md section 5, applied to a
// specialized program: every type is concrete, a function value is an object
// of its signature layout, and a closure is an allocation of a capture layout
// that derives from one.
class TaskAnalysis {
public:
    explicit TaskAnalysis(const IrProgram& program);

    // Section 5.1. A recursive type is transferable when its parts are.
    bool transferable(const IrType& type) const;
    // Whether objects of exactly this class may be given to another task.
    // The converter marks the layouts of these classes in the image.
    bool transferable_class(SymbolId cls) const;

    // Section 5.2. The operation that makes `function` unsafe off the root
    // task, with the calls that reach it, or nothing when it is task-safe.
    struct Unsafe {
        // Functions called on the way, outermost first, each with the span
        // of the call that leaves it.
        std::vector<std::pair<SymbolId, Span>> calls;
        std::string operation;
        Span span;
    };
    std::optional<Unsafe> unsafe(SymbolId function) const;
    // Why `function` cannot be the worker of a parallel map beyond being
    // task-safe: it issues a host request, uses the scheduler, or uses an
    // object's identity (section 12). Nothing when it can.
    std::optional<Unsafe> not_isolated(SymbolId function) const;

    // Checks every spawn of the program and decision C15. Throws a
    // Diagnostic at the first violation: at the argument for a value that is
    // not transferable, and at the spawn for a call that is not task-safe.
    void check() const;

private:
    struct Reason {
        // Set for a direct operation; otherwise `callee` continues the chain.
        std::string operation;
        Span span;
        SymbolId callee = no_symbol;
    };

    const IrProgram& program_;
    std::map<SymbolId, const IrBody*> bodies_;
    std::map<SymbolId, std::vector<SymbolId>> overriders_;
    std::map<SymbolId, std::vector<SymbolId>> fields_;
    std::map<SymbolId, std::vector<SymbolId>> derived_;
    std::map<SymbolId, std::vector<SymbolId>> variants_;
    std::map<SymbolId, std::vector<SymbolId>> destructors_;
    std::map<SymbolId, Reason> unsafe_;
    // Why a function cannot run as an isolated item of a parallel map
    // (section 12): it issues a host request, uses the scheduler, or performs
    // an identity operation, itself or through a call.
    std::map<SymbolId, Reason> impure_;
    std::optional<Reason> isolated_reason(const IrNode& node) const;
    std::optional<Unsafe> chain(const std::map<SymbolId, Reason>& reasons, SymbolId function) const;
    // For each body owner, the owners whose bodies can run it and where.
    std::map<SymbolId, std::vector<std::pair<SymbolId, Span>>> callers_;
    bool library_code(SymbolId owner) const;
    Span user_site(SymbolId owner, const Span& fallback) const;
    mutable std::map<SymbolId, bool> class_cache_;
    mutable std::map<SymbolId, bool> enum_cache_;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool valid(SymbolId id) const { return id < program_.symbols.size(); }
    bool signature_layout(SymbolId cls) const;
    bool capture_layout(SymbolId cls) const;
    bool concurrent_handle(SymbolId cls) const;
    bool fields_transferable(SymbolId cls) const;
    std::vector<SymbolId> all_fields(SymbolId cls) const;
    void callees(const IrNode& node, std::vector<std::pair<SymbolId, Span>>& out) const;
    std::optional<Reason> direct(const IrNode& node) const;
    void summarize();
};

} // namespace neura
