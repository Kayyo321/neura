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
//
// The same summaries carry the effect and retention facts of
// docs/design/Representation030.md ("Effect and retention summaries"):
// allocation, traps, host requests, the scheduler, class initialization,
// each slot's retention and the ownership of the result. They over-approximate:
// a fact that is absent is guaranteed. A function with a body is summarized
// from its body only. A function without one takes its facts from a contract:
// a kernel from its signature, a host binding from its host.effects attribute.
// A call through a function value, a virtual call without known targets and a
// bodyless function without a contract are unknown in every fact. Task
// transfer stays in TaskAnalysis (task_analysis.hpp) and is not copied here.

#include "dispatch.hpp"
#include "ir.hpp"
#include "module_dispatch.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
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

class HookIndex;

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
    // Why a call of `function` is not render-compatible (Html030.md, "Typing
    // and ownership"), or nothing: as impure(), except that reading a
    // reassignable or mutable binding, the static initialization a read runs
    // and object identity are allowed.
    // Writes to the slots in `ignored` are not reported for `function` itself.
    std::optional<Impure> render_effect(SymbolId function, const std::set<std::size_t>& ignored = {}) const;
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
        // Anything: a program global, or `opaque` for a place the analysis
        // knows nothing about. `unknown` is what purity reads; the ownership
        // of a result tells the two apart.
        bool unknown = false;
        bool opaque = false;
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
    // The operation that causes a fact: a direct operation, or the call whose
    // callee `callee` continues the chain (see origin()).
    struct Cause {
        std::string operation;
        Span span;
        SymbolId callee = no_symbol;
    };
    enum class Allocation : std::uint8_t { Free, May, Unknown };
    enum class Retention : std::uint8_t { NotRetained, ByResult, Elsewhere, Unknown };
    // What a result is. `Fresh` also covers a result that holds no reference
    // (a number, or nothing). `Borrowed` names the slot (0 is the receiver,
    // k is parameter k-1) whose object graph the result lies in.
    enum class Ownership : std::uint8_t { Fresh, Borrowed, Static, Unknown };
    enum class Fact : std::uint8_t { Allocation, Traps, Host, Scheduler, ClassInit, Hooks };
    // The four flags that the optimizer's EffectAnalysis has always assumed
    // for a call: virtual calls resolved only for a module whose target is
    // fixed, no constructor effects at an allocation, and every function
    // without a body unknown. EffectAnalysis reads these so that the images
    // do not change; the facts above are the precise ones.
    struct OptimizerFlags {
        bool allocates = false;
        bool traps = false;
        bool host = false;
        bool scheduler = false;
        bool operator==(const OptimizerFlags&) const = default;
    };
    struct Summary {
        // Present when the fact may hold, with the first operation that
        // causes it.
        std::optional<Cause> allocates;
        // The allocation fact is unknown rather than merely "may allocate".
        bool allocation_unknown = false;
        std::optional<Cause> traps;
        std::optional<Cause> host;
        std::optional<Cause> scheduler;
        std::optional<Cause> class_init;
        // A class hook clause that is not identity may run: the event, or
        // the call that reaches one (docs/design/Hooks030.md, Summaries).
        std::optional<Cause> hooks;
        OptimizerFlags optimizer;
        // What the call stores where no slot names it (a global, a place of
        // unknown identity) and what it hands to code the analysis cannot
        // see; `shared_graph` is what the objects it allocated hold when
        // they escape that way.
        Roots escapes;
        Roots leaks;
        Roots shared_graph;
        // Derived once the fixed point is reached: per slot, and for the
        // result. `retained_at` names the operation that retains a slot.
        std::vector<Retention> retention;
        std::map<std::size_t, Cause> retained_at;
        Ownership ownership = Ownership::Unknown;
        std::size_t borrowed_slot = 0;
        std::optional<Cause> ownership_cause;
        Allocation allocation() const {
            return allocation_unknown ? Allocation::Unknown : allocates ? Allocation::May : Allocation::Free;
        }
        // Impure for a reason other than writing the objects of its slots.
        std::optional<Reason> reason;
        // The first such reason that rendering does not allow.
        std::optional<Reason> render;
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
    // The summary of a function with a body or a contract, or nothing.
    const Summary* summary(SymbolId function) const;
    // The operation at the end of the chain that causes `fact` of `function`:
    // the first cause that is not a call, or the deepest call that can be
    // followed. Nothing when the fact is absent.
    std::optional<Cause> origin(SymbolId function, Fact fact) const;

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

    void finish(SymbolId function, Summary& summary) const;
    std::optional<Impure> chain(SymbolId function, bool rendering, const std::set<std::size_t>& ignored) const;

    // The clause functions a HookRead or HookAdmit may run (class_hooks.hpp).
    std::vector<SymbolId> hook_targets(const IrNode& node) const;

    const IrProgram& program_;
    std::function<bool(SymbolId)> opaque_;
    DispatchTargets dispatch_;
    ModuleDispatch modules_;
    // Built only for a program whose class hook events are elaborated.
    std::shared_ptr<const HookIndex> hooks_;
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

// Checks that every HTML factory of the program (a function with the
// ui.factory attribute) is render-compatible (E1010), reporting at the hole
// with the call chain to the effect. `opaque` and `specialized` are as for
// check_pure_tags; a factory of a prebuilt module was checked when it was
// compiled.
void check_render_factories(const IrProgram& program, const std::function<bool(SymbolId)>& opaque,
                            bool specialized = false);

} // namespace neura
