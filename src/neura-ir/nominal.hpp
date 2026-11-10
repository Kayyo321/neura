#pragma once

#include "ir.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace neura {

inline bool class_or_interface(SymbolKind kind) {
    return kind == SymbolKind::Class || kind == SymbolKind::Interface;
}

inline IrType substitute_type(IrType type, const std::vector<SymbolId>& parameters,
                              const std::vector<IrType>& arguments) {
    if (type.kind == TypeKind::TypeParameter) {
        for (std::size_t i = 0; i < std::min(parameters.size(), arguments.size()); ++i) {
            if (type.symbol == parameters[i]) return arguments[i];
        }
    }
    for (auto& part : type.arguments) {
        part = substitute_type(std::move(part), parameters, arguments);
    }
    return type;
}

// Every statically known view of a receiver, with substituted arguments at
// each interface edge. Visit a concrete class and its base before interfaces.
inline std::vector<IrType> nominal_views(const IrProgram& program, const IrType& actual) {
    std::vector<IrType> result;
    struct Visit {
        IrType view;
        SymbolId leave = no_symbol;
    };
    std::vector<Visit> pending{{actual}};
    std::set<SymbolId> active;
    std::map<SymbolId, std::vector<std::size_t>> seen;
    while (!pending.empty()) {
        auto visit = std::move(pending.back());
        pending.pop_back();
        if (visit.leave != no_symbol) {
            active.erase(visit.leave);
            continue;
        }
        const auto& type = visit.view;
        if (type.symbol >= program.symbols.size() || active.contains(type.symbol)) continue;
        auto& prior = seen[type.symbol];
        if (std::ranges::any_of(prior, [&](std::size_t index) { return result[index] == type; })) continue;
        prior.push_back(result.size());
        const auto& declaration = program.symbols[type.symbol];
        active.insert(type.symbol);
        pending.push_back({{}, type.symbol});
        if (type.kind == TypeKind::TypeParameter) {
            for (auto bound = declaration.additional_bounds.rbegin(); bound != declaration.additional_bounds.rend(); ++bound) {
                pending.push_back({*bound});
            }
            if (declaration.bound) pending.push_back({*declaration.bound});
        } else if (type.kind == TypeKind::Object) {
            for (auto interface = declaration.interfaces.rbegin(); interface != declaration.interfaces.rend(); ++interface) {
                pending.push_back({substitute_type(*interface, declaration.generics, type.arguments)});
            }
            if (declaration.base < program.symbols.size()) {
                pending.push_back({program.symbols[declaration.base].type});
            }
        }
        result.push_back(std::move(visit.view));
    }
    return result;
}

inline bool nominal_assignable(const IrProgram& program, const IrType& actual, const IrType& expected) {
    if (actual == expected) return true;
    if (expected.kind != TypeKind::Object ||
        (actual.kind != TypeKind::Object && actual.kind != TypeKind::TypeParameter)) return false;
    const auto views = nominal_views(program, actual);
    return std::ranges::find(views, expected) != views.end();
}

} // namespace neura
