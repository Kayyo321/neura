#include "dispatch.hpp"

#include <algorithm>

namespace neura {

DispatchTargets::DispatchTargets(const IrProgram& program) : program_(program) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind == SymbolKind::Function && valid(item.owner)) by_name_[{item.owner, item.name}].push_back(id);
        if (item.kind == SymbolKind::Class || item.kind == SymbolKind::Module) types_.push_back(id);
        if (item.kind == SymbolKind::Variant && valid(item.owner)) variants_[item.owner].push_back(id);
    }
    // Each type with everything it derives from, itself included.
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto kind = program.symbols[id].kind;
        if (kind != SymbolKind::Class && kind != SymbolKind::Module && kind != SymbolKind::Interface) continue;
        auto& into = ancestors_[id];
        std::vector<SymbolId> pending{id};
        while (!pending.empty()) {
            const auto scope = pending.back();
            pending.pop_back();
            if (!valid(scope) || !into.insert(scope).second) continue;
            const auto& item = program.symbols[scope];
            if (valid(item.base)) pending.push_back(item.base);
            for (const auto& interface : item.interfaces) pending.push_back(interface.symbol);
        }
    }
}

const std::vector<SymbolId>& DispatchTargets::declared(SymbolId owner, const std::string& name) const {
    static const std::vector<SymbolId> none;
    const auto found = by_name_.find({owner, name});
    return found == by_name_.end() ? none : found->second;
}

bool DispatchTargets::derives(SymbolId type, SymbolId ancestor) const {
    const auto found = ancestors_.find(type);
    return found != ancestors_.end() && found->second.contains(ancestor);
}

const std::vector<SymbolId>& DispatchTargets::members(SymbolId owner, const std::string& name) const {
    const auto key = std::pair{owner, name};
    if (const auto found = cache_.find(key); found != cache_.end()) return found->second;
    std::set<SymbolId> result;
    const auto with_body = [&](SymbolId scope, std::vector<SymbolId>& out) {
        const auto found = by_name_.find({scope, name});
        if (found == by_name_.end()) return;
        for (const auto id : found->second) {
            if (program_.symbols[id].has_body || program_.symbols[id].external) out.push_back(id);
        }
    };
    for (const auto type : types_) {
        if (!derives(type, owner)) continue;
        // The class chain first, as the runtime does.
        std::vector<SymbolId> chosen;
        std::set<SymbolId> seen;
        for (auto scope = type; valid(scope) && seen.insert(scope).second && chosen.empty();
             scope = program_.symbols[scope].base) {
            with_body(scope, chosen);
        }
        if (chosen.empty()) {
            // Otherwise an interface default; every one the class inherits
            // counts, which includes the most specific one the runtime picks.
            for (const auto ancestor : ancestors_.at(type)) {
                if (program_.symbols[ancestor].kind == SymbolKind::Interface) with_body(ancestor, chosen);
            }
        }
        result.insert(chosen.begin(), chosen.end());
    }
    return cache_.emplace(key, std::vector<SymbolId>(result.begin(), result.end())).first->second;
}

std::vector<SymbolId> DispatchTargets::of(const IrNode& call) const {
    std::set<SymbolId> result;
    if (valid(call.symbol) && (program_.symbols[call.symbol].has_body || program_.symbols[call.symbol].external)) {
        result.insert(call.symbol);
    }
    if (!call.operands.empty() && valid(call.symbol)) {
        const auto& receiver = call.operands.front().type;
        if ((receiver.kind == TypeKind::Object || receiver.kind == TypeKind::Module) && valid(receiver.symbol)) {
            const auto& found = members(receiver.symbol, program_.symbols[call.symbol].name);
            result.insert(found.begin(), found.end());
        }
    }
    return {result.begin(), result.end()};
}

std::vector<SymbolId> DispatchTargets::value_members(const IrType& type, IrOp op, bool& deferred) const {
    const std::string name = op == IrOp::ValueEquals ? "equals" : op == IrOp::ValueHash ? "hash_code" : "to_string";
    std::set<SymbolId> result;
    std::set<SymbolId> enums;
    const auto visit = [&](const auto& self, const IrType& item) -> void {
        switch (item.kind) {
        case TypeKind::TypeParameter:
            deferred = true;
            break;
        case TypeKind::Object:
            if (valid(item.symbol)) {
                const auto& found = members(item.symbol, name);
                result.insert(found.begin(), found.end());
            }
            break;
        case TypeKind::Enum:
            if (!valid(item.symbol) || !enums.insert(item.symbol).second) break;
            if (const auto found = variants_.find(item.symbol); found != variants_.end()) {
                for (const auto variant : found->second) {
                    for (const auto& payload : program_.symbols[variant].payload_types) self(self, payload);
                }
            }
            break;
        default:
            for (const auto& argument : item.arguments) self(self, argument);
            break;
        }
    };
    visit(visit, type);
    return {result.begin(), result.end()};
}

} // namespace neura
