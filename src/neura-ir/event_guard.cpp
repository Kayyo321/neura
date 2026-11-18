#include "event_guard.hpp"

#include "attributes.hpp"
#include "dispatch.hpp"
#include "tags.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace neura {
namespace {

// The member `name` of kind `kind` owned by `owner`, or nothing.
SymbolId owned(const IrProgram& program, SymbolId owner, SymbolKind kind, const std::string& name) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.owner == owner && item.kind == kind && item.name == name) return id;
    }
    return no_symbol;
}

// `File/Module/Class.function` of the trusted library file at `path`.
SymbolId library_function(const IrProgram& program, const std::string& path, const std::string& module,
                          const std::string& cls, const std::string& function) {
    for (SymbolId file = 0; file < program.symbols.size(); ++file) {
        const auto& item = program.symbols[file];
        if (item.kind != SymbolKind::File || item.library_path != path) continue;
        const auto found_module = owned(program, file, SymbolKind::Module, module);
        if (found_module == no_symbol) return no_symbol;
        const auto found_class = owned(program, found_module, SymbolKind::Class, cls);
        if (found_class == no_symbol) return no_symbol;
        return owned(program, found_class, SymbolKind::Function, function);
    }
    return no_symbol;
}

class GuardCheck {
public:
    GuardCheck(const IrProgram& program, SymbolId emit) : program_(program), dispatch_(program), emit_(emit) {
        for (const auto& body : program.bodies) bodies_[body.owner].push_back(&body);
    }

    void run(SymbolId with) {
        // Which functions reach `emit` through resolved calls, to a fixed
        // point over the call graph.
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& [function, bodies] : bodies_) {
                if (reaches_.contains(function)) continue;
                for (const auto* body : bodies)
                    if (first_reaching(body->instructions)) {
                        reaches_.insert(function);
                        changed = true;
                        break;
                    }
            }
        }
        // #Synchronized methods.
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.kind != SymbolKind::Function || !has_attribute(item, AttributeId::Synchronized)) continue;
            const auto* call = in_body(id);
            if (!call) continue;
            auto span = tag_span(item, TagId::Synchronized);
            if (!span && item.owner < program_.symbols.size()) span = tag_span(program_.symbols[item.owner], TagId::Synchronized);
            const auto owner = item.owner < program_.symbols.size() ? program_.symbols[item.owner].name : item.name;
            report(*call, "handlers would run while '" + owner + "' is locked",
                   span.value_or(item.span), "this method runs under its instance's guard");
        }
        // Lambdas passed directly to Guarded.with.
        if (with == no_symbol) return;
        for (const auto& body : program_.bodies) guarded_calls(body.instructions, with);
    }

private:
    const IrProgram& program_;
    DispatchTargets dispatch_;
    SymbolId emit_;
    std::map<SymbolId, std::vector<const IrBody*>> bodies_;
    std::set<SymbolId> reaches_;

    bool calls_emit(const IrNode& node) const {
        if (node.op != IrOp::Call && node.op != IrOp::VirtualCall) return false;
        if (node.symbol == emit_) return true;
        if (node.op == IrOp::VirtualCall)
            for (const auto target : dispatch_.of(node))
                if (target == emit_) return true;
        return false;
    }

    // The functions a resolved call runs.
    std::vector<SymbolId> callees(const IrNode& node) const {
        switch (node.op) {
        case IrOp::Call:
        case IrOp::Allocate:
        case IrOp::BaseInitialize:
            return node.symbol == no_symbol ? std::vector<SymbolId>{} : std::vector<SymbolId>{node.symbol};
        case IrOp::VirtualCall:
            return dispatch_.of(node);
        default:
            return {};
        }
    }

    // The first call in `nodes`, in evaluation order, that calls `emit` or a
    // function that reaches it. Function values are not followed.
    const IrNode* first_reaching(const std::vector<IrNode>& nodes) const {
        for (const auto& node : nodes) {
            for (const auto* region : {&node.operands, &node.body, &node.alternative})
                if (const auto* found = first_reaching(*region)) return found;
            if (calls_emit(node)) return &node;
            for (const auto callee : callees(node))
                if (reaches_.contains(callee)) return &node;
        }
        return nullptr;
    }

    const IrNode* in_body(SymbolId function) const {
        const auto found = bodies_.find(function);
        if (found == bodies_.end()) return nullptr;
        for (const auto* body : found->second)
            if (const auto* call = first_reaching(body->instructions)) return call;
        return nullptr;
    }

    void guarded_calls(const std::vector<IrNode>& nodes, SymbolId with) const {
        for (const auto& node : nodes) {
            for (const auto* region : {&node.operands, &node.body, &node.alternative}) guarded_calls(*region, with);
            if ((node.op != IrOp::VirtualCall && node.op != IrOp::Call) || node.symbol != with) continue;
            for (const auto& operand : node.operands) {
                if (operand.op != IrOp::MakeClosure || operand.symbol >= program_.symbols.size()) continue;
                const auto layout = program_.symbols[operand.symbol].owner;
                const auto invoke = owned(program_, layout, SymbolKind::Function, "$invoke");
                if (invoke == no_symbol) continue;
                if (const auto* call = in_body(invoke))
                    report(*call, "handlers would run while the guard is held", node.span,
                           "the guard is held while this lambda runs");
            }
        }
    }

    [[noreturn]] void report(const IrNode& call, const std::string& message, const Span& guard,
                             const std::string& why) const {
        auto error = Diagnostic(call.span, message).with_code("E0550");
        error.label(calls_emit(call) ? "the event is emitted here" : "this call emits an event");
        error.label(guard, why);
        error.help("commit the change, leave the guard, then emit");
        throw error;
    }
};

} // namespace

void check_event_guards(const IrProgram& program) {
    const auto emit = library_function(program, "std/events/event.nra", "Event", "Event", "emit");
    if (emit == no_symbol) return;
    const auto with = library_function(program, "std/concurrent/guarded.nra", "Guarded", "Guarded", "with");
    GuardCheck(program, emit).run(with);
}

} // namespace neura
