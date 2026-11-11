#pragma once

#include "neura-ir/ir.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura::optimization {

// What evaluating a node, a region, or a call may read, write, or do
// (docs/IrOptimization.md, "Effect summaries"). A summary over-approximates:
// a flag that is set means the effect may happen, a flag that is clear means
// it cannot. Locals and parameters are named by symbol. Fields and globals
// are named by symbol unless an unknown call may touch any of them.
struct Effects {
    // Locals and parameters of the body being summarized.
    std::set<SymbolId> local_reads;
    std::set<SymbolId> local_writes;
    std::set<SymbolId> field_reads;
    std::set<SymbolId> field_writes;
    std::set<SymbolId> global_reads;
    std::set<SymbolId> global_writes;
    // A virtual or closure call, or a call of a function without a body:
    // any field or global may be read or written.
    bool any_reads = false;
    bool any_writes = false;
    // Array and list elements, lengths, and versions, and text contents.
    bool element_reads = false;
    bool element_writes = false;
    bool allocates = false;
    bool destroys = false;
    // A specified trap or a runtime fault, including the frame-depth and
    // heap limits a call or an allocation can reach.
    bool may_trap = false;
    bool direct_calls = false;
    bool virtual_calls = false;
    bool closure_calls = false;
    // External calls: requests to the host.
    bool host = false;
    // Task scopes, spawns, and scheduler calls.
    bool scheduler = false;

    void merge(const Effects& other);
    // True when nothing a node with these effects does can change what
    // `reader` reads: no write to a local, field, global, element, or object
    // lifetime that `reader` reads.
    bool preserves(const Effects& reader) const;
    // Any call, host request, or scheduler operation.
    bool calls() const { return direct_calls || virtual_calls || closure_calls || host || scheduler; }
    bool operator==(const Effects&) const = default;
};

// Calls on module receivers. A module function is reached by a virtual call
// whose receiver names the module (`console.print_line`) or is the enclosing
// module's own receiver; the converter resolves it for the concrete module
// (Backend.md). The target is the same in every run when the receiver names
// the module, or when it is a module receiver and no module derives from
// that module.
class ModuleDispatch {
public:
    explicit ModuleDispatch(const IrProgram& program);

    // The module a virtual call's receiver is in every run, or no_symbol.
    SymbolId module(const IrNode& call) const;
    // The function with a body that a virtual call reaches in every run, or
    // no_symbol when it depends on the run or cannot be resolved.
    SymbolId target(const IrNode& call) const;
    // True when another module derives from `module`.
    bool derived(SymbolId module) const { return derived_.contains(module); }

private:
    const IrProgram& program_;
    std::set<SymbolId> derived_;
    std::map<std::pair<SymbolId, std::string>, std::vector<SymbolId>> members_;
};

// Effect summaries for one program. A function's summary covers what a call
// of it may do, without its own locals: the union of its body's effects and
// those of the functions it calls directly, computed to a fixed point over
// the call graph, so recursion is handled. A virtual call, a closure call, a
// call of a function without a body, and a host request are summarized
// conservatively. Summaries are computed on the tree; no control-flow graph
// is built.
class EffectAnalysis {
public:
    explicit EffectAnalysis(const IrProgram& program);

    // The effects of evaluating `node` and everything below it, including
    // the lazily evaluated parts of short-circuit operations and every arm.
    Effects of(const IrNode& node) const;
    Effects of(const std::vector<IrNode>& nodes) const;
    // What a call of `function` may do. Unknown functions get the
    // conservative summary.
    const Effects& function(SymbolId function) const;
    // True when `function` takes part in a cycle of direct calls, counting
    // module calls with a fixed target as direct.
    bool recursive(SymbolId function) const;
    const ModuleDispatch& dispatch() const { return dispatch_; }

private:
    void add(const IrNode& node, Effects& effects) const;
    // A function that a call reaches in every run: a direct call's target,
    // or a module call's fixed target.
    SymbolId callee(const IrNode& node) const;

    const IrProgram& program_;
    ModuleDispatch dispatch_;
    std::map<SymbolId, Effects> functions_;
    std::map<SymbolId, std::set<SymbolId>> calls_;
    std::set<SymbolId> recursive_;
    Effects unknown_;
};

// The conservative summary: anything may happen.
Effects unknown_effects();

// True for the value types that a frame slot holds without a heap
// reference: bool, integers, and floats. Slots of any other type can keep a
// heap entry alive (NVM.md, garbage collection).
bool value_type(const IrType& type);

} // namespace neura::optimization
