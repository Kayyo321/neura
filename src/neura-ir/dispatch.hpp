#pragma once

// The bodies a dynamically dispatched call can run, shared by the analyses
// that take the union over them: purity (purity.hpp), task safety
// (task_analysis.hpp), and the optimizer's choice of a fixed callee.
//
// The runtime picks a method by name for the receiver's class, walking the
// class's base chain and then its interface defaults (IRtoNeuronConversion,
// Converter::implementation). An implementation of an interface requirement
// needs no `.override`, so the `overrides` link alone does not find every
// target; this follows the runtime's rule over every class, module, or
// interface that derives from the receiver's static type.

#include "ir.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura {

class DispatchTargets {
public:
    explicit DispatchTargets(const IrProgram& program);

    // Every function with a body, or bound outside the program, that a call
    // of the method named `name` on a receiver of static type `owner` (a
    // class, interface, or module symbol) can run, in symbol order: for each
    // class or module that is `owner` or derives from it, the implementation
    // the runtime selects, together with every interface default it could
    // select.
    const std::vector<SymbolId>& members(SymbolId owner, const std::string& name) const;

    // The targets of a VirtualCall: the declared function, if it can run,
    // and the members of that name for the receiver's static type. A call
    // through a function value or on a type parameter has none here; callers
    // treat those separately.
    std::vector<SymbolId> of(const IrNode& call) const;

    // The value members (equals, hash_code, to_string) that a ValueEquals,
    // ValueHash, or ValueText on a value of `type` runs: for each class or
    // interface type in it, directly or as an element, a part, or a payload,
    // the members of that name. `deferred` is set when the type contains a
    // type parameter, whose members are known only in a specialization.
    std::vector<SymbolId> value_members(const IrType& type, IrOp op, bool& deferred) const;

    // The functions named `name` that `owner` itself declares.
    const std::vector<SymbolId>& declared(SymbolId owner, const std::string& name) const;

    // Whether `type` is `ancestor` or derives from it through bases and
    // interfaces.
    bool derives(SymbolId type, SymbolId ancestor) const;

private:
    const IrProgram& program_;
    std::map<std::pair<SymbolId, std::string>, std::vector<SymbolId>> by_name_;
    std::map<SymbolId, std::set<SymbolId>> ancestors_;
    std::vector<SymbolId> types_;
    std::map<SymbolId, std::vector<SymbolId>> variants_;
    mutable std::map<std::pair<SymbolId, std::string>, std::vector<SymbolId>> cache_;
    bool valid(SymbolId id) const { return id < program_.symbols.size(); }
};

} // namespace neura
