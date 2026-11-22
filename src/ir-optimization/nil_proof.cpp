#include "nil_proof.hpp"

#include "neura-ir/dispatch.hpp"
#include "neura-ir/module_dispatch.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/tags.hpp"

#include <optional>
#include <set>
#include <unordered_map>
#include <utility>

namespace neura::optimization {
namespace {

struct Flow {
    std::set<SymbolId> non_nil;
    bool reachable = true;
};

Flow join(Flow first, Flow second) {
    if (!first.reachable) return second;
    if (!second.reachable) return first;
    std::erase_if(first.non_nil, [&](SymbolId id) { return !second.non_nil.contains(id); });
    return first;
}

struct Condition {
    Flow yes;
    Flow no;
};

struct Body {
    SymbolId owner;
    bool record_proofs;
    bool saw_return = false;
    bool all_returns_proved = true;
};

bool class_reference(const IrType& type) {
    return type.kind == TypeKind::Object && !is_primitive_reference(type);
}

class Analysis {
public:
    Analysis(const IrProgram& program, std::unordered_set<const IrNode*>& proved)
        : program_(program), proved_(proved) {
        for (const auto& body : program_.bodies) bodies_.emplace(body.owner, &body);
    }

    void run(const IrBody& body) {
        Body scan{body.owner, true};
        Flow flow;
        region(body.instructions, flow, scan);
    }

private:
    const IrProgram& program_;
    std::optional<ModuleDispatch> modules_;
    std::optional<DispatchTargets> dispatch_;
    std::unordered_set<const IrNode*>& proved_;
    std::unordered_map<SymbolId, const IrBody*> bodies_;
    std::unordered_map<SymbolId, bool> results_;
    std::size_t summary_depth_ = 0;

    bool valid(SymbolId id) const { return id < program_.symbols.size(); }

    bool frame_slot(SymbolId id, const Body& scan) const {
        if (!valid(id)) return false;
        const auto& item = program_.symbols[id];
        return item.owner == scan.owner &&
            (item.kind == SymbolKind::Local || item.kind == SymbolKind::Parameter);
    }

    SymbolId frame_load(const IrNode& node, const Body& scan) const {
        if (node.op != IrOp::Load || !node.operands.empty() || !node.body.empty() ||
            !node.alternative.empty() || !frame_slot(node.symbol, scan) ||
            node.type != program_.symbols[node.symbol].type) return no_symbol;
        return node.symbol;
    }

    SymbolId reference_load(const IrNode& node, const Body& scan) const {
        return class_reference(node.type) ? frame_load(node, scan) : no_symbol;
    }

    SymbolId target(const IrNode& node) {
        if (node.op == IrOp::Call) return node.symbol;
        if (node.op != IrOp::VirtualCall || node.operands.empty() || !valid(node.symbol)) return no_symbol;
        const auto& receiver = node.operands.front().type;
        if (receiver.kind == TypeKind::Module) {
            if (!modules_) modules_.emplace(program_);
            return modules_->target(node);
        }
        if (!class_reference(receiver) || !valid(receiver.symbol)) return no_symbol;
        const auto& method = program_.symbols[node.symbol];
        const auto& cls = program_.symbols[receiver.symbol];
        const bool final_class = cls.kind == SymbolKind::Class && has_tag(cls, TagId::Final);
        const bool final_method = valid(method.owner) && program_.symbols[method.owner].kind == SymbolKind::Class &&
            has_tag(method, TagId::Final);
        if (!final_class && !final_method) return no_symbol;
        if (!dispatch_) dispatch_.emplace(program_);
        const auto candidates = dispatch_->of(node);
        return candidates.size() == 1 && candidates.front() == node.symbol ? node.symbol : no_symbol;
    }

    bool proved_result(SymbolId id) {
        if (!valid(id) || summary_depth_ >= 64) return false;
        if (const auto cached = results_.find(id); cached != results_.end()) return cached->second;
        const auto& function = program_.symbols[id];
        const auto body = bodies_.find(id);
        if (function.kind != SymbolKind::Function || !function.has_body || function.external ||
            !class_reference(function.type) || body == bodies_.end()) return false;

        // Seed recursive calls with no proof. Analyze the actual executable
        // body with all incoming parameters unknown.
        results_.emplace(id, false);
        ++summary_depth_;
        Body scan{id, false};
        Flow flow;
        region(body->second->instructions, flow, scan);
        --summary_depth_;
        return results_.at(id) = scan.saw_return && scan.all_returns_proved && !flow.reachable;
    }

    bool region(const std::vector<IrNode>& nodes, Flow& flow, Body& scan) {
        bool proved = false;
        bool cleanup = false;
        for (const auto& node : nodes) {
            if (!flow.reachable) break;
            cleanup = cleanup || node.op == IrOp::Cleanup;
            proved = visit(node, flow, scan);
        }
        // Deferred work runs at scope exit. Its body is analyzed by neither
        // the current flow nor the result prover.
        if (cleanup) {
            flow.non_nil.clear();
            proved = false;
        }
        return proved;
    }

    Condition condition(const IrNode& test, Flow flow, Body& scan) {
        if (test.op == IrOp::Boolean) {
            if (test.text == "true") return {std::move(flow), {{}, false}};
            if (test.text == "false") return {{{}, false}, std::move(flow)};
        }
        if (test.op == IrOp::Unary && test.text == "!" && test.operands.size() == 1) {
            auto inner = condition(test.operands.front(), std::move(flow), scan);
            return {std::move(inner.no), std::move(inner.yes)};
        }
        if (test.op == IrOp::ShortCircuit) {
            if (test.operands.size() == 1 && test.body.size() == 1 && test.text == "&&") {
                auto left = condition(test.operands.front(), std::move(flow), scan);
                auto right = condition(test.body.front(), std::move(left.yes), scan);
                return {std::move(right.yes), join(std::move(left.no), std::move(right.no))};
            }
            if (test.operands.size() == 1 && test.body.size() == 1 && test.text == "||") {
                auto left = condition(test.operands.front(), std::move(flow), scan);
                auto right = condition(test.body.front(), std::move(left.no), scan);
                return {join(std::move(left.yes), std::move(right.yes)), std::move(right.no)};
            }
            flow.non_nil.clear();
            return {flow, std::move(flow)};
        }

        visit(test, flow, scan);
        Condition edges{flow, std::move(flow)};
        if (test.op == IrOp::ClassIs && test.operands.size() == 1) {
            if (const auto subject = reference_load(test.operands.front(), scan); subject != no_symbol)
                edges.yes.non_nil.insert(subject);
        } else if (test.op == IrOp::Binary && (test.text == "==" || test.text == "!=") &&
                   test.operands.size() == 2) {
            const auto& left = test.operands.front();
            const auto& right = test.operands.back();
            const auto subject = left.op == IrOp::Nil ? reference_load(right, scan)
                               : right.op == IrOp::Nil ? reference_load(left, scan) : no_symbol;
            if (subject != no_symbol) {
                auto& present = test.text == "!=" ? edges.yes : edges.no;
                auto& absent = test.text == "!=" ? edges.no : edges.yes;
                present.non_nil.insert(subject);
                absent.non_nil.erase(subject);
            }
        }
        return edges;
    }

    bool visit(const IrNode& node, Flow& flow, Body& scan) {
        if (!flow.reachable) return false;
        const auto proved = transfer(node, flow, scan) && flow.reachable && class_reference(node.type);
        if (proved && scan.record_proofs) proved_.insert(&node);
        return proved;
    }

    bool transfer(const IrNode& node, Flow& flow, Body& scan) {
        switch (node.op) {
        case IrOp::Block:
        case IrOp::ScopedValue:
        case IrOp::ComparisonChain:
            return region(node.body, flow, scan);
        case IrOp::TaskScope:
            flow.non_nil.clear();
            region(node.body, flow, scan);
            flow.non_nil.clear();
            return false;
        case IrOp::Branch:
        case IrOp::SelectValue: {
            auto edges = condition(node.operands.front(), std::move(flow), scan);
            const bool yes = region(node.body, edges.yes, scan) || !edges.yes.reachable;
            const bool no = region(node.alternative, edges.no, scan) || !edges.no.reachable;
            flow = join(std::move(edges.yes), std::move(edges.no));
            return node.op == IrOp::SelectValue && yes && no;
        }
        case IrOp::ShortCircuit: {
            auto edges = condition(node, std::move(flow), scan);
            flow = join(std::move(edges.yes), std::move(edges.no));
            return false;
        }
        case IrOp::Loop: {
            // Forget the incoming state at the loop head. This covers the
            // first entry, every back edge, and writes along break edges.
            auto edges = condition(node.operands.front(), Flow{}, scan);
            if (edges.yes.reachable) {
                region(node.body, edges.yes, scan);
                Flow latch;
                region(node.alternative, latch, scan);
            }
            flow.non_nil.clear();
            return false;
        }
        case IrOp::Cleanup:
            scan.all_returns_proved = false;
            return false;
        default:
            break;
        }

        if (!node.body.empty() || !node.alternative.empty()) {
            // An unfamiliar region may exit or resume the enclosing body.
            scan.all_returns_proved = false;
            flow.non_nil.clear();
            return false;
        }

        bool value = false;
        for (std::size_t i = 0; i < node.operands.size() && flow.reachable; ++i) {
            const auto& operand = node.operands[i];
            if (node.op == IrOp::Store && i == 0 && frame_load(operand, scan) != no_symbol) continue;
            value = visit(operand, flow, scan);
        }
        if (!flow.reachable) return false;

        switch (node.op) {
        case IrOp::Initialize:
        case IrOp::Store: {
            const auto binding = node.op == IrOp::Initialize ? node.symbol : frame_load(node.operands.front(), scan);
            if (!frame_slot(binding, scan)) {
                flow.non_nil.clear();
            } else if (value && class_reference(program_.symbols[binding].type)) {
                flow.non_nil.insert(binding);
            } else {
                flow.non_nil.erase(binding);
            }
            return false;
        }
        case IrOp::Return:
            scan.saw_return = true;
            scan.all_returns_proved &= value;
            flow.reachable = false;
            return false;
        case IrOp::Break:
        case IrOp::Continue:
        case IrOp::TrapMessage:
        case IrOp::Fail:
            flow.reachable = false;
            return false;
        case IrOp::Load:
            return flow.non_nil.contains(reference_load(node, scan));
        case IrOp::Allocate:
            flow.non_nil.clear();
            return class_reference(node.type);
        case IrOp::Call:
        case IrOp::VirtualCall:
            flow.non_nil.clear();
            return class_reference(node.type) && proved_result(target(node));
        case IrOp::Field:
        case IrOp::Index:
        case IrOp::Project:
        case IrOp::EnumProject:
        case IrOp::Integer:
        case IrOp::Float:
        case IrOp::String:
        case IrOp::Boolean:
        case IrOp::Nil:
        case IrOp::Unary:
        case IrOp::Binary:
        case IrOp::ClassIs:
        case IrOp::ClassCast:
        case IrOp::Convert:
        case IrOp::Reference:
        case IrOp::Receiver:
        case IrOp::Discard:
            // These may inspect a value or trap, but cannot write to a slot.
            return false;
        case IrOp::Yield:
            scan.all_returns_proved = false;
            flow.non_nil.clear();
            return false;
        default:
            // This includes hook results, globals by indirection, field
            // mutation, destruction, scheduler operations and unknown calls.
            flow.non_nil.clear();
            return false;
        }
    }
};

} // namespace

NilProof::NilProof(const IrProgram& program) {
    Analysis analysis(program, proved_);
    for (const auto& body : program.bodies) analysis.run(body);
}

} // namespace neura::optimization
