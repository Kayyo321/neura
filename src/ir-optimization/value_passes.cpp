// Passes over the values of locals: copy and constant propagation, common
// subexpression elimination, loop-invariant code motion, and dead-store
// elimination. Each works on the tree with the effect summaries of
// effects.hpp; none builds a control-flow graph or SSA form. The
// preconditions are stated in docs/IrOptimization.md.
#include "passes.hpp"

#include "analysis.hpp"
#include "effects.hpp"
#include "locals.hpp"

#include <algorithm>
#include <set>

namespace neura::optimization {
namespace {

const IrSymbol& symbol(const IrProgram& program, SymbolId id) { return program.symbols[id]; }

bool literal(const IrNode& node) {
    return (node.op == IrOp::Integer || node.op == IrOp::Boolean || node.op == IrOp::Float) && node.operands.empty() &&
           node.body.empty() && node.alternative.empty();
}

// Locals bound by a comparison chain's hidden subject: the chain's shape
// requires that Initialize to stay.
std::set<SymbolId> chain_subjects(const std::vector<IrNode>& nodes) {
    std::set<SymbolId> subjects;
    const auto visit = [&](const auto& self, const std::vector<IrNode>& list) -> void {
        for (const auto& node : list) {
            if (node.op == IrOp::ComparisonChain && !node.body.empty() && node.body.front().op == IrOp::Initialize) {
                subjects.insert(node.body.front().symbol);
            }
            self(self, node.operands);
            self(self, node.body);
            self(self, node.alternative);
        }
    };
    visit(visit, nodes);
    return subjects;
}

template <typename Visit>
void each_node(std::vector<IrNode>& nodes, Visit& visit) {
    for (auto& node : nodes) {
        visit(node);
        each_node(node.operands, visit);
        each_node(node.body, visit);
        each_node(node.alternative, visit);
    }
}

template <typename Predicate>
void erase_statements(std::vector<IrNode>& nodes, Predicate& erase) {
    std::erase_if(nodes, erase);
    for (auto& node : nodes) {
        erase_statements(node.operands, erase);
        erase_statements(node.body, erase);
        erase_statements(node.alternative, erase);
    }
}

IrNode load_of(const IrProgram& program, SymbolId local, const IrNode& replaced) {
    IrNode load{IrOp::Load, symbol(program, local).type, replaced.span, local, {}, {}, {}, {}, {}};
    load.inline_site = replaced.inline_site;
    return load;
}

// Expressions whose value depends only on the locals, fields, and globals
// they read, so that two evaluations with nothing written in between give
// the same value or the same trap: literals, loads, unary and binary
// operations and conversions on bool, integer, and float values, field
// reads, element reads, and lengths. String operations that allocate are
// left out.
bool repeatable(const IrProgram& program, const IrNode& node) {
    const auto children = [&] {
        return node.body.empty() && node.alternative.empty() &&
               std::ranges::all_of(node.operands, [&](const IrNode& operand) { return repeatable(program, operand); });
    };
    switch (node.op) {
    case IrOp::Integer:
    case IrOp::Boolean:
    case IrOp::Float:
        return literal(node);
    case IrOp::Load:
        return node.symbol < program.symbols.size() && node.operands.empty() &&
               (is_local(program, node.symbol) || symbol(program, node.symbol).kind == SymbolKind::Global);
    case IrOp::Unary:
    case IrOp::Binary:
    case IrOp::Convert:
        return value_type(node.type) &&
               std::ranges::all_of(node.operands, [](const IrNode& operand) { return value_type(operand.type); }) &&
               children();
    case IrOp::Field:
    case IrOp::Length:
    case IrOp::TextLength:
        return node.operands.size() == 1 && children();
    case IrOp::Index:
    case IrOp::TextByteAt:
        return node.operands.size() == 2 && children();
    default:
        return false;
    }
}

// True when `statement` contains a break or continue that leaves the loop
// enclosing it, after which the local may still be read.
bool leaves_loop(const IrNode& statement) {
    switch (statement.op) {
    case IrOp::Break:
    case IrOp::Continue:
        return true;
    case IrOp::Block:
    case IrOp::Branch:
    case IrOp::TaskScope:
        return std::ranges::any_of(statement.body, leaves_loop) ||
               std::ranges::any_of(statement.alternative, leaves_loop);
    default:
        return false;
    }
}

} // namespace

std::size_t propagate_copies(IrProgram& program) {
    std::size_t count = 0;
    for (auto& body : program.bodies) {
        if (!function_body(program, body)) continue;
        const auto uses = scan_locals(program, body.instructions);
        const auto subjects = chain_subjects(body.instructions);
        // A local qualifies when its only write is its Initialize and its
        // value is a literal or a load of a local or parameter that is never
        // stored to, so every read of it sees the same value as that copy.
        std::map<SymbolId, IrNode> values;
        for (const auto& [local, use] : uses) {
            const auto& item = symbol(program, local);
            if (item.kind != SymbolKind::Local || use.initializes != 1 || use.stores != 0 || use.other ||
                use.initializer == nullptr || use.initializer->operands.size() != 1 || !value_type(item.type) ||
                subjects.contains(local)) {
                continue;
            }
            const auto& value = use.initializer->operands.front();
            if (value.type != item.type) continue;
            if (literal(value)) {
                values.emplace(local, value);
                continue;
            }
            if (value.op != IrOp::Load || !value.operands.empty() || !is_local(program, value.symbol) ||
                value.symbol == local) {
                continue;
            }
            const auto source = uses.find(value.symbol);
            if (source == uses.end() || source->second.stores != 0 || source->second.other) continue;
            const bool parameter = symbol(program, value.symbol).kind == SymbolKind::Parameter;
            if (source->second.initializes != (parameter ? 0U : 1U)) continue;
            values.emplace(local, value);
        }
        if (values.empty()) continue;
        // Follow copies of copies to their source; the sources form a forest
        // because a local is initialized from one declared before it.
        for (auto& [local, value] : values) {
            for (std::size_t steps = 0; value.op == IrOp::Load && steps <= values.size(); ++steps) {
                const auto next = values.find(value.symbol);
                if (next == values.end()) break;
                const auto span = value.span;
                value = next->second;
                value.span = span;
            }
        }
        auto replace = [&](IrNode& node) {
            if (node.op != IrOp::Load) return;
            const auto found = values.find(node.symbol);
            if (found == values.end()) return;
            // The value takes the span of the read it replaces.
            const auto span = node.span;
            const auto site = node.inline_site;
            node = found->second;
            node.span = span;
            node.inline_site = site;
        };
        each_node(body.instructions, replace);
        // The values are pure and total, so dropping the initializations
        // drops no effect.
        auto initialization = [&](const IrNode& node) {
            return node.op == IrOp::Initialize && values.contains(node.symbol);
        };
        erase_statements(body.instructions, initialization);
        count += values.size();
    }
    return count;
}

std::size_t eliminate_common_subexpressions(IrProgram& program) {
    const EffectAnalysis analysis(program);
    std::size_t count = 0;
    for (auto& body : program.bodies) {
        if (!function_body(program, body)) continue;
        const auto uses = scan_locals(program, body.instructions);
        each_statement_region(body.instructions, [&](std::vector<IrNode>& region) {
            std::vector<Effects> effects;
            effects.reserve(region.size());
            for (const auto& statement : region) effects.push_back(analysis.of(statement));
            for (std::size_t i = 0; i < region.size(); ++i) {
                // A definition holds the value of `expression` in `local`
                // once it completes.
                const auto& definition = region[i];
                SymbolId local = no_symbol;
                const IrNode* expression = nullptr;
                if (definition.op == IrOp::Initialize && definition.operands.size() == 1 &&
                    is_local(program, definition.symbol)) {
                    local = definition.symbol;
                    expression = &definition.operands.front();
                } else if (definition.op == IrOp::Store && definition.operands.size() == 2 &&
                           definition.operands.front().op == IrOp::Load &&
                           is_local(program, definition.operands.front().symbol)) {
                    local = definition.operands.front().symbol;
                    expression = &definition.operands.back();
                }
                if (expression == nullptr || node_count(*expression) < 2 || !repeatable(program, *expression) ||
                    expression->type != symbol(program, local).type) {
                    continue;
                }
                if (const auto use = uses.find(local); use != uses.end() && use->second.other) continue;
                const auto reads = analysis.of(*expression);
                if (reads.local_reads.contains(local)) continue;
                const IrNode pattern = *expression;
                // Everything that runs between the definition and a later
                // use: the statements in between and, conservatively, the
                // whole statement that contains the use.
                Effects between;
                for (std::size_t j = i + 1; j < region.size(); ++j) {
                    between.merge(effects[j]);
                    if (between.local_writes.contains(local) || !between.preserves(reads)) break;
                    std::size_t replaced = 0;
                    const auto replace = [&](const auto& self, IrNode& node) -> void {
                        if (same_expression(node, pattern)) {
                            node = load_of(program, local, node);
                            ++replaced;
                            return;
                        }
                        for (std::size_t k = 0; k < node.operands.size(); ++k) {
                            // A store's destination names storage; only
                            // its receiver and index are values.
                            if (node.op == IrOp::Store && k == 0) {
                                for (auto& operand : node.operands[0].operands) self(self, operand);
                                continue;
                            }
                            self(self, node.operands[k]);
                        }
                        for (auto& child : node.body) self(self, child);
                        for (auto& child : node.alternative) self(self, child);
                    };
                    replace(replace, region[j]);
                    count += replaced;
                }
            }
        });
    }
    return count;
}

std::size_t hoist_loop_invariants(IrProgram& program) {
    const EffectAnalysis analysis(program);
    std::size_t count = 0;
    for (auto& body : program.bodies) {
        if (!function_body(program, body)) continue;
        const auto uses = scan_locals(program, body.instructions);
        const auto subjects = chain_subjects(body.instructions);
        each_statement_region(body.instructions, [&](std::vector<IrNode>& region) {
            for (std::size_t k = 0; k < region.size(); ++k) {
                if (region[k].op != IrOp::Loop) continue;
                // Locals the loop writes or binds anywhere: its condition,
                // body, and latch.
                const auto changed = [&] { return analysis.of(region[k]).local_writes; };
                auto written = changed();
                for (std::size_t m = 0; m < region[k].body.size();) {
                    const auto& statement = region[k].body[m];
                    const bool candidate = [&] {
                        if (statement.op != IrOp::Initialize || statement.operands.size() != 1 ||
                            !is_local(program, statement.symbol) || subjects.contains(statement.symbol)) {
                            return false;
                        }
                        const auto use = uses.find(statement.symbol);
                        if (use == uses.end() || use->second.initializes != 1 || use->second.stores != 0 ||
                            use->second.other) {
                            return false;
                        }
                        const auto& value = statement.operands.front();
                        if (!pure_and_total(program, value)) return false;
                        const auto reads = analysis.of(value).local_reads;
                        return std::ranges::none_of(reads, [&](SymbolId read) { return written.contains(read); });
                    }();
                    if (!candidate) {
                        ++m;
                        continue;
                    }
                    // The value is pure and total and reads nothing the loop
                    // changes, so computing it once before the loop gives
                    // every iteration the same value at no extra cost.
                    auto hoisted = std::move(region[k].body[m]);
                    region[k].body.erase(region[k].body.begin() + static_cast<std::ptrdiff_t>(m));
                    region.insert(region.begin() + static_cast<std::ptrdiff_t>(k), std::move(hoisted));
                    ++k;
                    ++count;
                    written = changed();
                }
            }
        });
    }
    return count;
}

std::size_t remove_dead_stores(IrProgram& program) {
    const EffectAnalysis analysis(program);
    std::size_t count = 0;
    for (auto& body : program.bodies) {
        if (!function_body(program, body)) continue;
        const auto uses = scan_locals(program, body.instructions);
        const auto subjects = chain_subjects(body.instructions);
        // Only locals and parameters of a value type: a slot of any other
        // type keeps its heap entry alive until it is overwritten or the
        // frame returns, so removing a store to it could raise the peak heap.
        const auto eligible = [&](SymbolId local) {
            const auto use = uses.find(local);
            return is_local(program, local) && use != uses.end() && !use->second.other &&
                   value_type(symbol(program, local).type) && !subjects.contains(local);
        };
        const auto local_store = [&](const IrNode& node) {
            return node.op == IrOp::Store && node.operands.size() == 2 && node.operands.front().op == IrOp::Load &&
                   is_local(program, node.operands.front().symbol);
        };
        // Replaces a dead store with a discard of its value when evaluating
        // the value may have an effect; returns false when the whole store
        // can go.
        const auto drop = [&](IrNode& store) {
            if (pure_and_total(program, store.operands.back())) return false;
            auto value = std::move(store.operands.back());
            IrNode discard{IrOp::Discard, store.type, store.span, no_symbol, {}, {}, {}, {}, {}};
            discard.inline_site = store.inline_site;
            discard.operands.push_back(std::move(value));
            store = std::move(discard);
            return true;
        };
        // Stores to locals that are never read, and their initializations
        // when the initial value is pure and total.
        std::set<SymbolId> unread;
        for (const auto& [local, use] : uses) {
            if (use.loads == 0 && eligible(local)) unread.insert(local);
        }
        if (!unread.empty()) {
            const auto visit = [&](const auto& self, std::vector<IrNode>& nodes) -> void {
                std::vector<IrNode> kept;
                kept.reserve(nodes.size());
                for (auto& node : nodes) {
                    if (local_store(node) && unread.contains(node.operands.front().symbol)) {
                        ++count;
                        if (!drop(node)) continue;
                    } else if (node.op == IrOp::Initialize && unread.contains(node.symbol) &&
                               node.operands.size() == 1 && pure_and_total(program, node.operands.front())) {
                        ++count;
                        continue;
                    }
                    kept.push_back(std::move(node));
                }
                nodes = std::move(kept);
                for (auto& node : nodes) {
                    self(self, node.operands);
                    self(self, node.body);
                    self(self, node.alternative);
                }
            };
            visit(visit, body.instructions);
        }
        // A store that a later store in the same region overwrites before
        // anything reads the local and before control can leave the region
        // other than by returning, which ends the local's life.
        each_statement_region(body.instructions, [&](std::vector<IrNode>& region) {
            for (std::size_t i = 0; i < region.size(); ++i) {
                if (!local_store(region[i]) || !eligible(region[i].operands.front().symbol)) continue;
                const auto local = region[i].operands.front().symbol;
                for (std::size_t j = i + 1; j < region.size(); ++j) {
                    const auto& next = region[j];
                    if (local_store(next) && next.operands.front().symbol == local &&
                        !analysis.of(next.operands.back()).local_reads.contains(local)) {
                        ++count;
                        if (!drop(region[i])) {
                            region.erase(region.begin() + static_cast<std::ptrdiff_t>(i));
                            --i;
                        }
                        break;
                    }
                    if (analysis.of(next).local_reads.contains(local) || leaves_loop(next)) break;
                }
            }
        });
    }
    return count;
}

} // namespace neura::optimization
