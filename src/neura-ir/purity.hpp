#pragma once

// Purity (docs/lang/Tags.md, "Pure"): the one analysis that both the #Pure
// check and the optimizer use, so that they agree on what a pure function is.
//
// A function is pure when no call of it can be observed except through its
// result or a trap: it writes no global and no field or element of an object
// it did not allocate during the call, reads no reassignable global and no
// global of a mutable type, makes no host request (a call of a kernel is
// allowed), uses no scheduler operation and no object identity, and calls only
// pure functions. Calls through function values are not pure. A pure function
// may allocate, may trap, and may loop forever; it may read the fields and
// elements of its arguments, so "the same arguments" means the same values and
// the same state reachable from them.

#include "ir.hpp"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura {

// The immutable types of docs/lang/Types.md: bool, integers, floats, string,
// tuples and enums whose parts are immutable, and classes marked #Immutable.
// A type parameter counts as immutable here; each specialization is checked
// with its concrete type.
bool immutable_type(const IrProgram& program, const IrType& type);

class PurityAnalysis {
public:
    // `opaque`, when given, names functions whose purity is part of their
    // interface: they are pure exactly when they carry #Pure, and their bodies
    // are not looked at. The #Pure check passes the functions of prebuilt
    // .nir modules; the optimizer, which sees the whole linked program,
    // passes nothing.
    explicit PurityAnalysis(const IrProgram& program, std::function<bool(SymbolId)> opaque = {});

    struct Impure {
        // Functions called on the way, outermost first, each with the span of
        // the call that reaches it.
        std::vector<std::pair<SymbolId, Span>> calls;
        std::string operation;
        Span span;
    };
    // Why a call of `function` may be observed, or nothing when it is pure.
    std::optional<Impure> impure(SymbolId function) const;
    bool pure(SymbolId function) const { return !reasons_.contains(function); }
    // Pure, and every call of it returns normally: its body has no loop and no
    // operation that can trap, it takes part in no cycle of calls, and it calls
    // only total functions directly. The frame and step limits are run limits,
    // which an optimized program may meet later than an unoptimized one.
    bool total(SymbolId function) const;
    // Whether some function in the program overrides `function`, so that a
    // virtual call of it may run another body.
    bool overridden(SymbolId function) const { return overriders_.contains(function); }

private:
    struct Reason {
        std::string operation;
        Span span;
        SymbolId callee = no_symbol;
    };

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool valid(SymbolId id) const { return id < program_.symbols.size(); }
    void callees(const IrNode& node, std::vector<std::pair<SymbolId, Span>>& out) const;
    std::optional<Reason> direct(SymbolId owner, const IrNode& node, const std::set<SymbolId>& fresh) const;
    bool fresh_object(SymbolId owner, const IrNode& object, const std::set<SymbolId>& fresh) const;
    std::set<SymbolId> fresh_locals(const IrBody& body) const;
    bool function_value_call(const IrNode& node) const;
    bool cannot_trap(const IrNode& node) const;

    const IrProgram& program_;
    std::function<bool(SymbolId)> opaque_;
    std::map<SymbolId, const IrBody*> bodies_;
    std::map<SymbolId, std::vector<SymbolId>> overriders_;
    std::map<SymbolId, Reason> reasons_;
    mutable std::map<SymbolId, bool> total_;
};

// Checks every function and method of the program marked #Pure (E0917),
// reporting at the tag with the call chain to the first operation that makes
// it impure. `opaque` is as for PurityAnalysis.
void check_pure_tags(const IrProgram& program, const std::function<bool(SymbolId)>& opaque);

} // namespace neura
