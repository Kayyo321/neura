#include "initialization_order.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace neura {
namespace {

SymbolId file_of(const IrProgram& program, SymbolId id) {
    for (auto scope = id; scope != no_symbol; scope = program.symbols[scope].owner) {
        if (program.symbols[scope].kind == SymbolKind::File) return scope;
    }
    return no_symbol;
}

} // namespace

std::vector<SymbolId> file_initialization_order(const IrProgram& program) {
    std::vector<SymbolId> files;
    std::map<SymbolId, std::set<SymbolId>> imports;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind == SymbolKind::File) {
            files.push_back(id);
        } else if (item.kind == SymbolKind::Import && item.type.symbol != no_symbol) {
            const auto from = file_of(program, id);
            const auto to = file_of(program, item.type.symbol);
            if (from != no_symbol && to != no_symbol && from != to) {
                imports[from].insert(to);
            }
        }
    }
    // Files that import each other, directly or through other files, form a
    // cycle. A cycle runs once every file it imports from outside the cycle
    // has run, and its files run in the given order. Without cycles this is
    // the plain dependency order with ties in the given order.
    std::map<SymbolId, std::set<SymbolId>> reachable;
    for (const auto file : files) {
        auto& seen = reachable[file];
        std::vector<SymbolId> pending(imports[file].begin(), imports[file].end());
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            if (seen.insert(next).second) {
                pending.insert(pending.end(), imports[next].begin(), imports[next].end());
            }
        }
    }
    const auto same_cycle = [&](SymbolId a, SymbolId b) {
        return a == b || (reachable[a].contains(b) && reachable[b].contains(a));
    };
    std::vector<SymbolId> order;
    std::set<SymbolId> placed;
    while (order.size() < files.size()) {
        bool progressed = false;
        for (const auto file : files) {
            if (placed.contains(file)) continue;
            std::vector<SymbolId> cycle;
            for (const auto member : files) {
                if (!placed.contains(member) && same_cycle(file, member)) cycle.push_back(member);
            }
            const bool ready = std::ranges::all_of(cycle, [&](SymbolId member) {
                return std::ranges::all_of(imports[member], [&](SymbolId dependency) {
                    return placed.contains(dependency) || same_cycle(file, dependency);
                });
            });
            if (!ready) continue;
            for (const auto member : cycle) {
                placed.insert(member);
                order.push_back(member);
            }
            progressed = true;
            break;
        }
        if (!progressed) {
            throw std::logic_error("file initialization order has no ready file");
        }
    }
    return order;
}

} // namespace neura
