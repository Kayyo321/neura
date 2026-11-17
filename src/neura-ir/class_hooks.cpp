#include "class_hooks.hpp"

#include "attributes.hpp"

#include <algorithm>

namespace neura {

SymbolId declared_hook(const IrSymbol& cls, HookKind kind) {
    if (cls.kind != SymbolKind::Class) return no_symbol;
    const auto* link = find_attribute(cls, kind == HookKind::Get ? AttributeId::HookGet : AttributeId::HookSet);
    return link ? link->value.symbol : no_symbol;
}

SymbolId effective_hook(const IrProgram& program, SymbolId cls, HookKind kind) {
    // A base chain is finite and acyclic in a verified program; the bound
    // keeps a malformed one from looping.
    for (std::size_t hops = 0; cls != no_symbol && cls < program.symbols.size() && hops <= program.symbols.size();
         ++hops) {
        const auto& item = program.symbols[cls];
        if (const auto function = declared_hook(item, kind); function != no_symbol) return function;
        cls = item.base;
    }
    return no_symbol;
}

SymbolId hook_owner(const IrSymbol& function) {
    const auto* of = find_attribute(function, AttributeId::HookOf);
    return of && !of->value.items.empty() ? of->value.items.front().symbol : no_symbol;
}

bool has_class_hooks(const IrProgram& program) {
    return std::ranges::any_of(program.symbols, [](const IrSymbol& item) {
        return has_attribute(item, AttributeId::HookGet) || has_attribute(item, AttributeId::HookSet);
    });
}

void require_executable_hooks(const IrProgram& program) {
    for (const auto& item : program.symbols) {
        for (const auto kind : {HookKind::Get, HookKind::Set}) {
            const auto function = declared_hook(item, kind);
            if (function == no_symbol) continue;
            throw Diagnostic(program.symbols[function].span, "class hooks are not executable yet").with_code("E0801")
                .label("class '" + item.name + "' declares a '" + hook_word(kind) + "' hook")
                .note("this compiler checks class hooks but cannot build an image that runs them");
        }
    }
}

} // namespace neura
