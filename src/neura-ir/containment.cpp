#include "containment.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neura {

namespace {

// True for a declaration inside a generic class or function, including the
// generic declaration itself. Specialization keeps these, unreachable, and
// gives each instance its own symbols with no generics.
bool in_template(const IrProgram& program, SymbolId id) {
    for (auto scope = id; scope != no_symbol && scope < program.symbols.size(); scope = program.symbols[scope].owner) {
        const auto& item = program.symbols[scope];
        if (!item.generics.empty() || item.kind == SymbolKind::TypeParameter) return true;
        if (is_static_scope(item)) return false;
    }
    return false;
}

class Graph {
public:
    explicit Graph(const IrProgram& program) : program_(program) {
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].kind == SymbolKind::Enum && !in_template(program, id)) {
                index_.emplace(id, nodes_.size());
                nodes_.push_back(id);
            }
        }
        edges_.resize(nodes_.size());
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto& variant = program.symbols[id];
            if (variant.kind != SymbolKind::Variant) continue;
            const auto owner = index_.find(variant.owner);
            if (owner == index_.end()) continue;
            for (std::size_t payload = 0; payload < variant.payload_types.size(); ++payload) {
                ContainmentStep step{variant.owner, id, payload, {}, no_symbol};
                walk(variant.payload_types[payload], step, owner->second);
            }
        }
    }

    std::vector<ContainmentCycle> cycles() {
        // Tarjan's algorithm over nodes in symbol order.
        order_.assign(nodes_.size(), unvisited);
        low_.assign(nodes_.size(), 0);
        on_stack_.assign(nodes_.size(), false);
        component_.assign(nodes_.size(), unvisited);
        for (std::size_t node = 0; node < nodes_.size(); ++node) {
            if (order_[node] == unvisited) connect(node);
        }
        std::vector<ContainmentCycle> result;
        std::vector<bool> reported(components_, false);
        for (std::size_t node = 0; node < nodes_.size(); ++node) {
            const auto group = component_[node];
            if (reported[group]) continue;
            reported[group] = true;
            if (auto cycle = shortest_cycle(node)) result.push_back(std::move(*cycle));
        }
        return result;
    }

private:
    static constexpr std::size_t unvisited = static_cast<std::size_t>(-1);

    void walk(const IrType& type, ContainmentStep& step, std::size_t from) {
        if (type.kind == TypeKind::Tuple) {
            for (std::size_t element = 0; element < type.arguments.size(); ++element) {
                step.tuple.push_back(element);
                walk(type.arguments[element], step, from);
                step.tuple.pop_back();
            }
            return;
        }
        if (type.kind != TypeKind::Enum) return;
        if (index_.find(type.symbol) == index_.end()) return;
        auto edge = step;
        edge.to = type.symbol;
        edges_[from].push_back(std::move(edge));
    }

    void connect(std::size_t node) {
        order_[node] = low_[node] = next_order_++;
        stack_.push_back(node);
        on_stack_[node] = true;
        for (const auto& edge : edges_[node]) {
            const auto target = index_.at(edge.to);
            if (order_[target] == unvisited) {
                connect(target);
                low_[node] = std::min(low_[node], low_[target]);
            } else if (on_stack_[target]) {
                low_[node] = std::min(low_[node], order_[target]);
            }
        }
        if (low_[node] != order_[node]) return;
        while (true) {
            const auto member = stack_.back();
            stack_.pop_back();
            on_stack_[member] = false;
            component_[member] = components_;
            if (member == node) break;
        }
        ++components_;
    }

    // The shortest path from `start` back to itself inside its component,
    // breadth first over edges in symbol and payload order. None when the
    // component is a single enum that does not contain itself.
    std::optional<ContainmentCycle> shortest_cycle(std::size_t start) const {
        const auto group = component_[start];
        std::vector<const ContainmentStep*> reached_by(nodes_.size(), nullptr);
        std::deque<std::size_t> queue{start};
        std::vector<bool> seen(nodes_.size(), false);
        seen[start] = true;
        while (!queue.empty()) {
            const auto node = queue.front();
            queue.pop_front();
            for (const auto& edge : edges_[node]) {
                const auto target = index_.at(edge.to);
                if (component_[target] != group) continue;
                if (target == start) {
                    ContainmentCycle cycle;
                    cycle.steps.push_back(edge);
                    for (auto at = node; at != start; at = index_.at(reached_by[at]->from)) {
                        cycle.steps.push_back(*reached_by[at]);
                    }
                    std::reverse(cycle.steps.begin(), cycle.steps.end());
                    return cycle;
                }
                if (seen[target]) continue;
                seen[target] = true;
                reached_by[target] = &edge;
                queue.push_back(target);
            }
        }
        return std::nullopt;
    }

    [[maybe_unused]] const IrProgram& program_;
    std::vector<SymbolId> nodes_;
    std::map<SymbolId, std::size_t> index_;
    std::vector<std::vector<ContainmentStep>> edges_;
    std::vector<std::size_t> order_;
    std::vector<std::size_t> low_;
    std::vector<bool> on_stack_;
    std::vector<std::size_t> component_;
    std::vector<std::size_t> stack_;
    std::size_t next_order_ = 0;
    std::size_t components_ = 0;
};

const std::string& name_of(const IrProgram& program, SymbolId id) { return program.symbols.at(id).name; }

} // namespace

std::vector<ContainmentCycle> find_containment_cycles(const IrProgram& program) {
    return Graph(program).cycles();
}

std::string describe_containment_cycle(const IrProgram& program, const ContainmentCycle& cycle) {
    if (cycle.steps.empty()) return {};
    std::string text = name_of(program, cycle.steps.front().from);
    for (const auto& step : cycle.steps) {
        text += " -> " + name_of(program, step.from) + "." + name_of(program, step.variant) + "." +
                std::to_string(step.payload);
        for (const auto element : step.tuple) text += "." + std::to_string(element);
        text += ": " + name_of(program, step.to);
    }
    return text;
}

Diagnostic containment_diagnostic(const IrProgram& program, const ContainmentCycle& cycle) {
    const auto& closing = cycle.steps.back();
    const auto& start = name_of(program, cycle.steps.front().from);
    const auto& variant = program.symbols.at(closing.variant);
    auto error = Diagnostic(variant.span, "enum '" + start + "' contains itself, so its values have no finite size")
                     .with_code("E0448");
    error.label("this payload holds '" + name_of(program, closing.to) + "' inline");
    error.note("the path: " + describe_containment_cycle(program, cycle));
    error.help("hold the link in a class, which is a reference: for example, a class with one `last` field of type '" +
               name_of(program, closing.to) + "'");
    return error;
}

void check_containment(const IrProgram& program) {
    const auto cycles = find_containment_cycles(program);
    if (!cycles.empty()) throw containment_diagnostic(program, cycles.front());
}

} // namespace neura
