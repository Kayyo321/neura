#include "scoped_resources.hpp"

#include "dispatch.hpp"
#include "purity.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neura {
namespace {

using Retention = PurityAnalysis::Retention;

bool is_token(const IrProgram& program, const IrNode& node) {
    return node.op == IrOp::Initialize && node.symbol < program.symbols.size() &&
           program.symbols[node.symbol].kind == SymbolKind::Local &&
           program.symbols[node.symbol].name.starts_with("$using") && node.operands.size() == 1 &&
           node.operands.front().op == IrOp::Load;
}

class ScopeCheck {
public:
    ScopeCheck(const IrProgram& program, const PurityAnalysis& purity)
        : program_(program), purity_(purity), dispatch_(program) {}

    // Finds the `using` blocks of a statement list and of the statement
    // regions nested in it.
    void find(const std::vector<IrNode>& region) {
        for (std::size_t i = 0; i < region.size(); ++i) {
            const auto& node = region[i];
            if (is_token(program_, node)) analyze(region, i);
            if (node.op == IrOp::Block || node.op == IrOp::Branch || node.op == IrOp::Loop ||
                node.op == IrOp::TaskScope || node.op == IrOp::Cleanup) {
                find(node.body);
                find(node.alternative);
            }
        }
    }

private:
    const IrProgram& program_;
    const PurityAnalysis& purity_;
    DispatchTargets dispatch_;
    // The block being checked: its binding's name and `using` keyword, the
    // locals that may hold the resource or a value retaining it, the locals
    // declared inside the block, and the task scopes entered inside it.
    std::string name_;
    Span keyword_;
    std::set<SymbolId> scoped_;
    std::set<SymbolId> inner_;
    unsigned task_scopes_ = 0;
    // The walk repeats until the set of locals that hold the resource stops
    // growing, so that an alias made late in a loop is seen at its top;
    // only the last walk reports.
    bool report_ = true;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool local(SymbolId id) const {
        return id < program_.symbols.size() &&
               (symbol(id).kind == SymbolKind::Local || symbol(id).kind == SymbolKind::Parameter);
    }

    // Whether a value of `type` can hold a reference to the resource.
    static bool can_hold(const IrType& type) {
        switch (type.kind) {
        case TypeKind::Void:
        case TypeKind::Bool:
        case TypeKind::Integer:
        case TypeKind::Float:
        case TypeKind::String:
        case TypeKind::Module:
            return false;
        default:
            return true;
        }
    }

    void escape(const Span& at, const std::string& how) const {
        if (!report_) return;
        throw Diagnostic(at, "'" + name_ + "' is closed at the end of its 'using' block and cannot be " + how)
            .with_code("E0475")
            .label(keyword_, "'" + name_ + "' is bound here, and closed when this block is left")
            .help("use the resource inside the block, or keep what it produced instead of the resource");
    }

    void analyze(const std::vector<IrNode>& region, std::size_t token_index) {
        const auto& token = region[token_index];
        const auto binding = token.operands.front().symbol;
        name_ = symbol(binding).name;
        keyword_ = token.span;
        scoped_ = {binding, token.symbol};
        inner_ = {binding, token.symbol};
        task_scopes_ = 0;
        // The Cleanup after the token is the block's own close.
        auto first = token_index + 1;
        if (first < region.size() && region[first].op == IrOp::Cleanup) ++first;
        report_ = false;
        for (std::size_t before = 0; before != scoped_.size();) {
            before = scoped_.size();
            for (auto i = first; i < region.size(); ++i) statement(region[i]);
        }
        report_ = true;
        for (auto i = first; i < region.size(); ++i) statement(region[i]);
    }

    void statements(const std::vector<IrNode>& region) {
        for (const auto& item : region) statement(item);
    }

    void statement(const IrNode& node) {
        switch (node.op) {
        case IrOp::Initialize: {
            if (node.operands.empty()) {
                inner_.insert(node.symbol);
                return;
            }
            const bool held = expression(node.operands.front());
            if (!local(node.symbol)) {
                if (held) escape(node.operands.front().span, "stored outside it");
                return;
            }
            inner_.insert(node.symbol);
            if (held) scoped_.insert(node.symbol);
            return;
        }
        case IrOp::Store: {
            const auto& destination = node.operands.front();
            for (const auto& part : destination.operands) expression(part);
            const bool held = expression(node.operands.back());
            if (!held) return;
            if (destination.op == IrOp::Pending) {
                escape(node.operands.back().span, "returned");
                return;
            }
            if (destination.op == IrOp::Load && local(destination.symbol)) {
                if (!inner_.contains(destination.symbol))
                    escape(node.operands.back().span, "stored in a binding of an outer block");
                scoped_.insert(destination.symbol);
                return;
            }
            escape(node.operands.back().span, destination.op == IrOp::Field ? "stored in a field"
                                              : destination.op == IrOp::Index ? "stored in an element"
                                                                              : "stored in a global");
            return;
        }
        case IrOp::Return:
            if (!node.operands.empty() && expression(node.operands.front()))
                escape(node.operands.front().span, "returned");
            return;
        case IrOp::TaskScope:
            if (node.symbol != no_symbol) inner_.insert(node.symbol);
            ++task_scopes_;
            statements(node.body);
            --task_scopes_;
            return;
        case IrOp::Block:
        case IrOp::Branch:
        case IrOp::Loop:
        case IrOp::Cleanup:
            for (const auto& operand : node.operands) expression(operand);
            statements(node.body);
            statements(node.alternative);
            return;
        default:
            for (const auto& operand : node.operands) expression(operand);
            return;
        }
    }

    // The worst retention of `slot` over the functions the call may run. A
    // declaration without code (an interface's or abstract method) runs
    // nothing itself; a call with no code to run at all is unknown.
    Retention retention(const IrNode& call, std::size_t slot, bool dispatched) const {
        const auto callee = call.symbol;
        auto worst = Retention::NotRetained;
        bool code = false;
        for (const auto target : dispatched ? dispatch_.of(call) : std::vector<SymbolId>{callee}) {
            if (target >= program_.symbols.size()) return Retention::Unknown;
            const auto& item = symbol(target);
            if (dispatched && !item.has_body && !item.external) continue;
            code = true;
            const auto* summary = purity_.summary(target);
            if (!summary || slot >= summary->retention.size()) return Retention::Unknown;
            worst = std::max(worst, summary->retention[slot]);
        }
        return code ? worst : Retention::Unknown;
    }

    // At an allocation, the constructor's receiver (slot 0) is the new
    // object, which is the result: a parameter that only becomes reachable
    // from it is retained by the result.
    Retention constructed(SymbolId init, std::size_t slot) const {
        IrNode call;
        call.symbol = init;
        const auto kind = retention(call, slot, false);
        if (kind != Retention::Elsewhere) return kind;
        const auto* summary = purity_.summary(init);
        const auto has = [&](const PurityAnalysis::Roots& roots) {
            return roots.fresh || roots.unknown || roots.slots.contains(slot) || roots.inside.contains(slot);
        };
        if (has(summary->escapes) || has(summary->leaks)) return kind;
        for (const auto& [from, linked] : summary->links)
            if (from != 0 && has(linked)) return kind;
        return Retention::ByResult;
    }

    // A call: each argument that may hold the resource is checked against
    // its slot's retention. The result holds it when a slot is retained by
    // the result.
    bool call(const IrNode& node, std::size_t first_slot, bool dispatched) {
        bool result = false;
        for (std::size_t i = 0; i < node.operands.size(); ++i) {
            const auto& operand = node.operands[i];
            if (!expression(operand)) continue;
            const auto kind = node.op == IrOp::Allocate ? constructed(node.symbol, first_slot + i)
                                                        : retention(node, first_slot + i, dispatched);
            if (kind == Retention::NotRetained) continue;
            if (kind == Retention::ByResult) {
                result = true;
                continue;
            }
            const auto& callee = node.symbol < program_.symbols.size() ? symbol(node.symbol).name : std::string("?");
            if (callee == "send" || callee == "try_send") escape(operand.span, "sent on a channel");
            if (operand.op == IrOp::MakeClosure)
                escape(operand.span, "captured by a function value that '" + callee + "' retains");
            escape(operand.span, "retained by '" + callee + "'");
        }
        return result;
    }

    // Whether the value of `node` may be the resource or hold it.
    bool expression(const IrNode& node) {
        switch (node.op) {
        case IrOp::Load:
            for (const auto& qualifier : node.operands) expression(qualifier);
            return local(node.symbol) && scoped_.contains(node.symbol);
        case IrOp::Call:
        case IrOp::ExternalCall:
        case IrOp::Allocate:
            return call(node, 1, false);
        case IrOp::VirtualCall:
            return call(node, 0, true);
        case IrOp::CallClosure: {
            for (std::size_t i = 1; i < node.operands.size(); ++i)
                if (expression(node.operands[i]))
                    escape(node.operands[i].span, "passed to a function value, which may retain it");
            // A function value that captured the resource may return it.
            const bool held = !node.operands.empty() && expression(node.operands.front());
            return held && can_hold(node.type);
        }
        // A part of a value that holds the resource may be the resource.
        case IrOp::Field:
        case IrOp::Index:
        case IrOp::Project:
        case IrOp::EnumProject:
        case IrOp::AnyUnpack: {
            bool held = false;
            for (const auto& operand : node.operands) held = expression(operand) || held;
            return held && can_hold(node.type);
        }
        case IrOp::Spawn: {
            const bool held = !node.operands.empty() && expression(node.operands.front());
            if (held && task_scopes_ == 0)
                escape(node.span, "captured by a task that is not joined inside the block: put the 'scope' "
                                  "inside the 'using' block");
            return false;
        }
        case IrOp::Append: {
            bool held = false;
            for (std::size_t i = 0; i < node.operands.size(); ++i) {
                const bool value = expression(node.operands[i]);
                if (i > 0) held = held || value;
            }
            if (held) escape(node.operands.back().span, "stored in a list");
            return false;
        }
        case IrOp::ScopedValue:
        case IrOp::SelectValue: {
            for (const auto& operand : node.operands) expression(operand);
            bool held = false;
            for (const auto* region : {&node.body, &node.alternative}) {
                for (std::size_t i = 0; i < region->size(); ++i) {
                    if (i + 1 == region->size() && (*region)[i].type.kind != TypeKind::Void) {
                        held = expression((*region)[i]) || held;
                    } else {
                        statement((*region)[i]);
                    }
                }
            }
            return held;
        }
        case IrOp::MakeClosure:
        case IrOp::EnumConstruct:
        case IrOp::Tuple:
        case IrOp::Collection:
        case IrOp::RepeatCollection:
        case IrOp::AnyPack:
        case IrOp::ClassCast: {
            bool held = false;
            for (const auto& operand : node.operands) held = expression(operand) || held;
            return held;
        }
        default:
            for (const auto& operand : node.operands) expression(operand);
            for (const auto& item : node.body) expression(item);
            return false;
        }
    }
};

bool uses_using(const std::vector<IrNode>& nodes, const IrProgram& program) {
    return std::ranges::any_of(nodes, [&](const IrNode& node) {
        return is_token(program, node) || uses_using(node.body, program) || uses_using(node.alternative, program);
    });
}

} // namespace

void check_scoped_resources(const IrProgram& program, const std::function<bool(SymbolId)>& opaque) {
    if (std::ranges::none_of(program.bodies, [&](const IrBody& body) { return uses_using(body.instructions, program); }))
        return;
    const PurityAnalysis purity(program, opaque);
    ScopeCheck check(program, purity);
    for (const auto& body : program.bodies) check.find(body.instructions);
}

} // namespace neura
