#pragma once

// Primitive conformance to static contracts (docs/design/Records030.md,
// Primitive conformance and the contracts), shared by lowering's bound checks
// and the verifier's. A primitive conforms to an interface instance only
// through its companion module, whose functions carry static.implements for
// the primitive: it satisfies a bound when the bound's interface and its
// parents have static requirements only, and each requirement has an
// implementation for the primitive with the signature that the bound's type
// arguments give it.

#include "attributes.hpp"
#include "nominal.hpp"

#include <map>

namespace neura {

inline bool primitive_type(const IrType& type) {
    return type.kind == TypeKind::Bool || type.kind == TypeKind::Integer || type.kind == TypeKind::Float ||
           type.kind == TypeKind::String;
}

inline bool primitive_conforms(const IrProgram& program, const IrType& primitive, const IrType& bound) {
    if (!primitive_type(primitive) || bound.kind != TypeKind::Object || bound.symbol >= program.symbols.size() ||
        program.symbols[bound.symbol].kind != SymbolKind::Interface)
        return false;
    // The implementations for the primitive, by requirement.
    std::map<SymbolId, SymbolId> implementations;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        for (const auto* attribute : find_attributes(program.symbols[id], AttributeId::StaticImplements)) {
            const auto& items = attribute->value.items;
            if (items.size() == 2 && items[1].type == primitive) implementations.emplace(items[0].symbol, id);
        }
    }
    if (implementations.empty()) return false;
    for (const auto& view : nominal_views(program, bound)) {
        if (view.kind != TypeKind::Object || view.symbol >= program.symbols.size()) return false;
        const auto& interface = program.symbols[view.symbol];
        // The umbrella class every interface extends adds no requirement.
        if (interface.kind != SymbolKind::Interface) continue;
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto& member = program.symbols[id];
            if (member.owner == no_symbol || member.owner >= program.symbols.size()) continue;
            const auto& scope = program.symbols[member.owner];
            const bool instance_member = member.owner == view.symbol && member.kind != SymbolKind::TypeParameter &&
                                         !is_static_scope(member);
            const bool requirement = member.kind == SymbolKind::Function && !member.has_body && !member.external &&
                                     is_static_scope(scope) && scope.owner == view.symbol;
            // A primitive conforms only to a static contract.
            if (instance_member) return false;
            if (!requirement) continue;
            const auto found = implementations.find(id);
            if (found == implementations.end()) return false;
            const auto& implementation = program.symbols[found->second];
            const auto expected = [&](const IrType& type) {
                return substitute_type(type, interface.generics, view.arguments);
            };
            if (implementation.type != expected(member.type) ||
                implementation.parameters.size() != member.parameters.size())
                return false;
            for (std::size_t i = 0; i < member.parameters.size(); ++i) {
                if (program.symbols[implementation.parameters[i]].type !=
                    expected(program.symbols[member.parameters[i]].type))
                    return false;
            }
        }
    }
    return true;
}

} // namespace neura
