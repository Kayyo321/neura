#pragma once

// Purity (docs/lang/Tags.md, "Pure"): the one analysis that both the #Pure
// check and the optimizer use, so that they agree on what a pure function is.
//
// A function is pure when no call of it can be observed except through its
// result or a trap: it writes no global and no field or element of an object
// it did not allocate during the call, reads no reassignable global and no
// global of a mutable type, makes no host request, uses no scheduler
// operation and no object identity, and every function it can call, through
// dispatch included, is pure for the arguments it is given. Calls through
// function values are not pure. A pure function may allocate, may trap, and
// may loop forever; it may read the fields and elements of its arguments, so
// "the same arguments" means the same values and the same state reachable
// from them.
//
// The analysis is a summary per function, solved callees first to a fixed
// point. A summary says which of the function's slots (0 is the receiver,
// k is parameter k-1) it may write the object graph of, what it may make
// reachable from each slot's graph, what its result may reach, and whether
// it is impure for any other reason. A caller that passes an object it
// allocated itself for a written slot stays pure: the object graph reachable
// from such an argument holds only objects the caller allocated, unless
// something the caller did not allocate was stored into one of them, which
// the analysis tracks for all of the caller's fresh objects together.
// A kernel writes the lists it receives for its writable-view and state
// parameters and nothing else (kernel_signature.hpp).

#include "dispatch.hpp"
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
    bool pure(SymbolId function) const;
    // Pure, and every call of it returns normally: its body has no loop and no
    // operation that can trap, it takes part in no cycle of calls, and it calls
    // only total functions directly. The frame and step limits are run limits,
    // which an optimized program may meet later than an unoptimized one.
    bool total(SymbolId function) const;
    // The bodies a VirtualCall can run (dispatch.hpp); empty for a call
    // through a function value or on a type parameter.
    std::vector<SymbolId> targets(const IrNode& call) const;

    // Where an object may come from: the call allocated it, it is the object
    // a slot holds (`slots`), it is reachable from that object (`inside`),
    // or anything else.
    struct Roots {
        bool fresh = false;
        bool unknown = false;
        std::set<std::size_t> slots;
        std::set<std::size_t> inside;
        bool empty() const { return !fresh && !unknown && slots.empty() && inside.empty(); }
        bool join(const Roots& other);
        bool operator==(const Roots&) const = default;
    };
    struct Reason {
        std::string operation;
        Span span;
        // Set when a call of `callee` is the reason; `slot` then names the
        // callee's slot whose write it is, or nothing for the callee's own
        // reason.
        SymbolId callee = no_symbol;
        std::optional<std::size_t> slot{};
    };
    struct Summary {
        // Impure for a reason other than writing the objects of its slots.
        std::optional<Reason> reason;
        // The slots whose object it may write, each with the first write;
        // `deep` holds those where the write may reach past the slot's own
        // object into what it refers to.
        std::map<std::size_t, Reason> writes;
        std::set<std::size_t> deep;
        // What may become reachable from each slot's object graph.
        std::map<std::size_t, Roots> links;
        // What the result may be, and what the objects the call allocated
        // may refer to.
        Roots result;
        Roots fresh_graph;
        bool same(const Summary& other) const;
    };
    // The summary of a function with a body, or nothing.
    const Summary* summary(SymbolId function) const;

private:
    friend class PurityWalker;
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool valid(SymbolId id) const { return id < program_.symbols.size(); }
    bool function_value_call(const IrNode& node) const;
    bool cannot_trap(const IrNode& node) const;
    bool immutable(const IrType& type) const;
    // The functions whose summaries a node applies, for the caller graph.
    void callees(const IrNode& node, std::vector<SymbolId>& out) const;
    // The constructor chain an allocation of `cls` runs besides `init`.
    std::vector<SymbolId> construction(SymbolId cls, SymbolId init) const;

    const IrProgram& program_;
    std::function<bool(SymbolId)> opaque_;
    DispatchTargets dispatch_;
    std::map<SymbolId, std::vector<const IrBody*>> bodies_;
    std::map<SymbolId, Summary> summaries_;
    mutable std::map<std::string, bool> immutable_;
    mutable std::map<SymbolId, bool> total_;
};

// Checks every function and method of the program marked #Pure (E0917),
// reporting at the tag with the call chain to the first operation that makes
// it impure. `opaque` is as for PurityAnalysis. Before specialization a call
// through a type parameter counts as pure, because its target is known only
// in a specialization; with `specialized`, the check runs over the program
// specialize() produced, where every such call has its concrete targets, and
// it checks each specialization of a generic function marked #Pure.
void check_pure_tags(const IrProgram& program, const std::function<bool(SymbolId)>& opaque,
                     bool specialized = false);

} // namespace neura
