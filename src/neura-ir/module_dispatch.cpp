#include "module_dispatch.hpp"

namespace neura {

ModuleDispatch::ModuleDispatch(const IrProgram& program) : program_(program) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind == SymbolKind::Module) {
            // Every module up the chain has a derived module.
            std::set<SymbolId> seen;
            for (auto base = item.base; base < program.symbols.size() && seen.insert(base).second;
                 base = program.symbols[base].base) {
                derived_.insert(base);
            }
        }
        if (item.kind == SymbolKind::Function && item.owner != no_symbol) {
            members_[{item.owner, item.name}].push_back(id);
        }
    }
}

SymbolId ModuleDispatch::module(const IrNode& call) const {
    if (call.op != IrOp::VirtualCall || call.operands.empty()) return no_symbol;
    const auto& receiver = call.operands.front();
    if (receiver.type.kind != TypeKind::Module || receiver.symbol >= program_.symbols.size()) return no_symbol;
    const auto& item = program_.symbols[receiver.symbol];
    if (receiver.op == IrOp::Reference && item.kind == SymbolKind::Module) return receiver.symbol;
    if (receiver.op == IrOp::Reference && item.kind == SymbolKind::Import &&
        item.type.symbol < program_.symbols.size() && program_.symbols[item.type.symbol].kind == SymbolKind::Module) {
        return item.type.symbol;
    }
    if (receiver.op == IrOp::Receiver && item.kind == SymbolKind::Module && !derived(receiver.symbol)) {
        return receiver.symbol;
    }
    return no_symbol;
}

SymbolId ModuleDispatch::target(const IrNode& call) const {
    const auto module = this->module(call);
    if (module == no_symbol || call.symbol >= program_.symbols.size() || !call.type_arguments.empty()) {
        return no_symbol;
    }
    // The first module up the chain that declares a function of that name
    // supplies it; it must be the declared function or override it.
    std::set<SymbolId> seen;
    for (auto scope = module; scope < program_.symbols.size() && seen.insert(scope).second;
         scope = program_.symbols[scope].base) {
        const auto found = members_.find({scope, program_.symbols[call.symbol].name});
        if (found == members_.end()) continue;
        if (found->second.size() != 1) return no_symbol;
        const auto candidate = found->second.front();
        std::set<SymbolId> chain;
        for (auto current = candidate; current < program_.symbols.size() && chain.insert(current).second;
             current = program_.symbols[current].overrides) {
            if (current == call.symbol) {
                const auto& function = program_.symbols[candidate];
                return function.has_body && function.generics.empty() ? candidate : no_symbol;
            }
        }
        return no_symbol;
    }
    return no_symbol;
}

} // namespace neura
