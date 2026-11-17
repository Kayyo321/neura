#include "effects.hpp"

#include "neura-ir/integer.hpp"
#include "neura-ir/kernel_signature.hpp"

#include <algorithm>
#include <functional>

namespace neura::optimization {
namespace {

bool comparison(const std::string& op) {
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

template <typename Set>
void insert_all(Set& into, const Set& from) {
    into.insert(from.begin(), from.end());
}

template <typename Set>
bool intersects(const Set& left, const Set& right) {
    return std::ranges::any_of(left, [&](const auto& item) { return right.contains(item); });
}

void each_node(const std::vector<IrNode>& nodes, const std::function<void(const IrNode&)>& visit) {
    for (const auto& node : nodes) {
        visit(node);
        each_node(node.operands, visit);
        each_node(node.body, visit);
        each_node(node.alternative, visit);
    }
}

} // namespace

void Effects::merge(const Effects& other) {
    insert_all(local_reads, other.local_reads);
    insert_all(local_writes, other.local_writes);
    insert_all(field_reads, other.field_reads);
    insert_all(field_writes, other.field_writes);
    insert_all(global_reads, other.global_reads);
    insert_all(global_writes, other.global_writes);
    any_reads = any_reads || other.any_reads;
    any_writes = any_writes || other.any_writes;
    element_reads = element_reads || other.element_reads;
    element_writes = element_writes || other.element_writes;
    allocates = allocates || other.allocates;
    destroys = destroys || other.destroys;
    may_trap = may_trap || other.may_trap;
    direct_calls = direct_calls || other.direct_calls;
    virtual_calls = virtual_calls || other.virtual_calls;
    closure_calls = closure_calls || other.closure_calls;
    host = host || other.host;
    scheduler = scheduler || other.scheduler;
}

bool Effects::preserves(const Effects& reader) const {
    if (intersects(local_writes, reader.local_reads)) return false;
    const bool reads_heap = !reader.field_reads.empty() || !reader.global_reads.empty() || reader.element_reads ||
                            reader.any_reads;
    if (!reads_heap) return true;
    if (any_writes || destroys) return false;
    if (intersects(field_writes, reader.field_reads) || intersects(global_writes, reader.global_reads)) return false;
    if (reader.any_reads && (!field_writes.empty() || !global_writes.empty() || element_writes)) return false;
    return !(element_writes && reader.element_reads);
}

Effects unknown_effects() {
    Effects effects;
    effects.any_reads = true;
    effects.any_writes = true;
    effects.element_reads = true;
    effects.element_writes = true;
    effects.allocates = true;
    effects.destroys = true;
    effects.may_trap = true;
    effects.direct_calls = true;
    effects.virtual_calls = true;
    effects.closure_calls = true;
    effects.host = true;
    effects.scheduler = true;
    return effects;
}

bool value_type(const IrType& type) {
    return type.kind == TypeKind::Bool || type.kind == TypeKind::Integer || type.kind == TypeKind::Float;
}

SymbolId EffectAnalysis::callee(const IrNode& node) const {
    if (node.op == IrOp::Call) return node.symbol;
    if (node.op == IrOp::VirtualCall) return dispatch_.target(node);
    return no_symbol;
}

EffectAnalysis::EffectAnalysis(const IrProgram& program)
    : program_(program), owned_(std::make_unique<PurityAnalysis>(program)), purity_(*owned_), dispatch_(program),
      unknown_(unknown_effects()) {
    build();
}

EffectAnalysis::EffectAnalysis(const IrProgram& program, const PurityAnalysis& purity)
    : program_(program), purity_(purity), dispatch_(program), unknown_(unknown_effects()) {
    build();
}

void EffectAnalysis::build() {
    const auto& program = program_;
    std::map<SymbolId, const IrBody*> bodies;
    for (const auto& body : program.bodies) {
        if (body.owner < program.symbols.size() && program.symbols[body.owner].kind == SymbolKind::Function &&
            !bodies.contains(body.owner)) {
            bodies.emplace(body.owner, &body);
            auto& callees = calls_[body.owner];
            each_node(body.instructions, [&](const IrNode& node) {
                if (const auto target = callee(node); target != no_symbol) callees.insert(target);
            });
        }
    }
    // Tarjan's algorithm yields the strongly connected components of the
    // direct call graph callees first, so each component is summarized
    // after everything it calls. Iterative to bound host recursion.
    std::map<SymbolId, std::size_t> index;
    std::map<SymbolId, std::size_t> low;
    std::set<SymbolId> on_stack;
    std::vector<SymbolId> stack;
    std::vector<std::vector<SymbolId>> components;
    std::size_t counter = 0;
    struct Visit {
        SymbolId function;
        std::set<SymbolId>::const_iterator next;
    };
    for (const auto& [root, root_body] : bodies) {
        if (index.contains(root)) continue;
        std::vector<Visit> visits;
        const auto start = [&](SymbolId function) {
            index[function] = low[function] = counter++;
            stack.push_back(function);
            on_stack.insert(function);
            visits.push_back({function, calls_[function].cbegin()});
        };
        start(root);
        while (!visits.empty()) {
            auto& visit = visits.back();
            const auto& callees = calls_[visit.function];
            if (visit.next != callees.cend()) {
                const auto callee = *visit.next++;
                if (!bodies.contains(callee)) continue;
                if (!index.contains(callee)) {
                    start(callee);
                } else if (on_stack.contains(callee)) {
                    low[visit.function] = std::min(low[visit.function], index[callee]);
                }
                continue;
            }
            const auto function = visit.function;
            visits.pop_back();
            if (!visits.empty()) {
                low[visits.back().function] = std::min(low[visits.back().function], low[function]);
            }
            if (low[function] == index[function]) {
                std::vector<SymbolId> component;
                SymbolId member;
                do {
                    member = stack.back();
                    stack.pop_back();
                    on_stack.erase(member);
                    component.push_back(member);
                } while (member != function);
                std::ranges::sort(component);
                components.push_back(std::move(component));
            }
        }
    }
    for (const auto& component : components) {
        if (component.size() > 1 || calls_[component.front()].contains(component.front())) {
            recursive_.insert(component.begin(), component.end());
        }
        for (const auto function : component) functions_[function] = Effects{};
        // Summaries only grow, and each is bounded by the program's symbols
        // and flags, so this reaches a fixed point.
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto function : component) {
                auto summary = of(bodies.at(function)->instructions);
                summary.local_reads.clear();
                summary.local_writes.clear();
                // Allocation, traps, host requests and the scheduler are the
                // purity analysis's facts, not a second computation.
                if (const auto* facts = purity_.summary(function)) {
                    summary.allocates = facts->optimizer.allocates;
                    summary.may_trap = facts->optimizer.traps;
                    summary.host = facts->optimizer.host;
                    summary.scheduler = facts->optimizer.scheduler;
                }
                if (summary != functions_[function]) {
                    functions_[function] = std::move(summary);
                    changed = true;
                }
            }
        }
    }
}

const Effects& EffectAnalysis::function(SymbolId function) const {
    const auto found = functions_.find(function);
    return found == functions_.end() ? unknown_ : found->second;
}

bool EffectAnalysis::recursive(SymbolId function) const { return recursive_.contains(function); }

Effects EffectAnalysis::of(const IrNode& node) const {
    Effects effects;
    add(node, effects);
    return effects;
}

Effects EffectAnalysis::of(const std::vector<IrNode>& nodes) const {
    Effects effects;
    for (const auto& node : nodes) add(node, effects);
    return effects;
}

void EffectAnalysis::add(const IrNode& node, Effects& effects) const {
    const auto kind = [&](SymbolId id) {
        return id < program_.symbols.size() ? program_.symbols[id].kind : SymbolKind::File;
    };
    const auto local = [&](SymbolId id) { return kind(id) == SymbolKind::Local || kind(id) == SymbolKind::Parameter; };
    // A module global is resolved by name for the concrete module, so in a
    // chain of modules one access can reach another module's global of the
    // same name; such accesses count as touching every global.
    const auto inherited = [&](SymbolId global) {
        if (global >= program_.symbols.size()) return true;
        const auto owner = program_.symbols[global].owner;
        return owner >= program_.symbols.size() || dispatch_.derived(owner) ||
               (kind(owner) == SymbolKind::Module && program_.symbols[owner].base != no_symbol);
    };
    const auto children = [&](const IrNode& parent) {
        for (const auto& child : parent.operands) add(child, effects);
        for (const auto& child : parent.body) add(child, effects);
        for (const auto& child : parent.alternative) add(child, effects);
    };
    switch (node.op) {
    case IrOp::Boolean:
    case IrOp::Integer:
    case IrOp::Float:
    case IrOp::Nil:
    case IrOp::Reference:
    case IrOp::Receiver:
    case IrOp::ReinterpretFloat:
    case IrOp::AnyPack:
    case IrOp::AnyTag:
    case IrOp::AnyUnpack:
    case IrOp::ShortCircuit:
    case IrOp::ComparisonChain:
    case IrOp::ScopedValue:
    case IrOp::SelectValue:
    case IrOp::Block:
    case IrOp::Branch:
    case IrOp::Loop:
    case IrOp::Break:
    case IrOp::Continue:
    case IrOp::Return:
    case IrOp::Discard:
        break;
    case IrOp::Load:
        if (local(node.symbol)) {
            effects.local_reads.insert(node.symbol);
        } else {
            // A global that has not been initialized faults when read.
            effects.global_reads.insert(node.symbol);
            if (inherited(node.symbol)) effects.any_reads = true;
            effects.may_trap = true;
        }
        break;
    case IrOp::Store: {
        if (node.operands.size() != 2) {
            effects.merge(unknown_);
            break;
        }
        const auto& destination = node.operands.front();
        if (destination.op == IrOp::Load) {
            if (local(destination.symbol)) {
                effects.local_writes.insert(destination.symbol);
            } else {
                effects.global_writes.insert(destination.symbol);
                if (inherited(destination.symbol)) effects.any_writes = true;
            }
            // A static field's qualifier is evaluated, then discarded.
            for (const auto& qualifier : destination.operands) add(qualifier, effects);
        } else {
            if (destination.op == IrOp::Field) {
                effects.field_writes.insert(destination.symbol);
            } else {
                effects.element_writes = true;
            }
            effects.may_trap = true;
            for (const auto& operand : destination.operands) add(operand, effects);
        }
        add(node.operands.back(), effects);
        return;
    }
    case IrOp::Initialize:
        if (local(node.symbol)) {
            effects.local_writes.insert(node.symbol);
        } else if (kind(node.symbol) == SymbolKind::Global) {
            effects.global_writes.insert(node.symbol);
        } else {
            effects.field_writes.insert(node.symbol);
            effects.may_trap = true;
        }
        break;
    case IrOp::Unary:
        if (node.text != "!" && node.text != "~") effects.may_trap = true;
        break;
    case IrOp::Binary: {
        const auto operand = node.operands.empty() ? TypeKind::Void : node.operands.front().type.kind;
        const bool scalar = operand == TypeKind::Bool || operand == TypeKind::Integer;
        const bool safe = scalar && (comparison(node.text) || (is_bitwise_binary(node.text) && !is_shift(node.text)));
        if (!safe) effects.may_trap = true;
        if (operand == TypeKind::String) effects.allocates = true;
        break;
    }
    case IrOp::Convert:
    case IrOp::Fail:
    case IrOp::TrapMessage:
        effects.may_trap = true;
        break;
    case IrOp::Field:
        effects.field_reads.insert(node.symbol);
        effects.may_trap = true;
        break;
    case IrOp::Index:
    case IrOp::Length:
    case IrOp::CollectionVersion:
    case IrOp::TextLength:
    case IrOp::TextByteAt:
    case IrOp::TextEqual:
    case IrOp::Project:
    case IrOp::EnumTag:
    case IrOp::EnumProject:
        effects.element_reads = true;
        effects.may_trap = true;
        break;
    case IrOp::TextSlice:
    case IrOp::TextFromUtf8:
        effects.element_reads = true;
        effects.allocates = true;
        effects.may_trap = true;
        break;
    case IrOp::Append:
    case IrOp::RemoveAt:
        effects.element_reads = true;
        effects.element_writes = true;
        effects.allocates = true;
        effects.may_trap = true;
        break;
    case IrOp::String:
    case IrOp::Allocate:
    case IrOp::Collection:
    case IrOp::RepeatCollection:
    case IrOp::Tuple:
    case IrOp::EnumConstruct:
    case IrOp::MakeClosure:
        effects.allocates = true;
        effects.may_trap = true;
        break;
    case IrOp::Call:
        effects.direct_calls = true;
        // Every call can reach the frame-depth limit.
        effects.may_trap = true;
        effects.merge(function(node.symbol));
        break;
    case IrOp::VirtualCall:
        if (const auto target = callee(node); target != no_symbol) {
            // A module call whose target is fixed is summarized as a direct
            // call of that target.
            effects.direct_calls = true;
            effects.may_trap = true;
            effects.merge(function(target));
            break;
        }
        effects.merge(unknown_);
        effects.virtual_calls = true;
        break;
    case IrOp::CallClosure:
        effects.merge(unknown_);
        effects.closure_calls = true;
        break;
    case IrOp::ExternalCall:
        if (node.symbol < program_.symbols.size() && kernel_bound(program_.symbols[node.symbol])) {
            // A kernel runs inside the NVM: it reads the elements of its
            // arguments, writes the elements of the lists it receives for
            // writable-view and state parameters (neura-ir/kernel_signature.hpp,
            // the same rule the purity analysis applies), allocates its
            // result, and may trap. It calls nothing back.
            effects.element_reads = true;
            const auto writes = kernel_writes(program_.symbols[node.symbol]);
            if (!writes || std::ranges::any_of(*writes, [](bool written) { return written; })) {
                effects.element_writes = true;
            }
            effects.allocates = true;
            effects.may_trap = true;
            break;
        }
        // A host service may call back into the program.
        effects.merge(unknown_);
        effects.host = true;
        break;
    case IrOp::TaskScope:
        if (node.symbol != no_symbol) effects.local_writes.insert(node.symbol);
        effects.scheduler = true;
        effects.may_trap = true;
        break;
    case IrOp::Spawn:
    case IrOp::SchedulerCall:
        effects.merge(unknown_);
        effects.scheduler = true;
        break;
    case IrOp::ReferencePlace:
        // The local escapes as a mutable reference.
        effects.local_reads.insert(node.symbol);
        effects.local_writes.insert(node.symbol);
        effects.merge(unknown_);
        break;
    default:
        // Destroy and BaseInitialize run class code; class and value
        // members may dispatch. Anything not listed is treated as unknown.
        effects.merge(unknown_);
        break;
    }
    children(node);
}

} // namespace neura::optimization
