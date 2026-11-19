#include "lifetime.hpp"

#include "reference_storage.hpp"
#include "util/diagnostic.hpp"

#include <algorithm>
#include <string>

namespace neura {
namespace {

// Storage that belongs to the frame: an array, a primitive reference, or a
// tuple or enum that holds one (lowering_lifetime.inc keeps the same rule).
bool frame_owned(const IrProgram& program, const IrType& type, unsigned depth = 0) {
    if (depth > 8) return false;
    if (type.kind == TypeKind::Array || is_primitive_reference(type)) return true;
    if (type.kind != TypeKind::Tuple && type.kind != TypeKind::Enum) return false;
    if (std::ranges::any_of(type.arguments, [&](const IrType& item) { return frame_owned(program, item, depth + 1); }))
        return true;
    if (type.kind == TypeKind::Enum && type.symbol < program.symbols.size() && type.arguments.empty())
        for (const auto& item : program.symbols)
            if (item.kind == SymbolKind::Variant && item.owner == type.symbol &&
                std::ranges::any_of(item.payload_types,
                                    [&](const IrType& payload) { return frame_owned(program, payload, depth + 1); }))
                return true;
    return false;
}

std::string type_text(const IrProgram& program, const IrType& type) {
    std::string text;
    switch (type.kind) {
    case TypeKind::Array: return type.arguments.empty() ? "array" : type_text(program, type.arguments.front()) + "[]";
    default: break;
    }
    text = type.symbol < program.symbols.size() && (type.kind == TypeKind::Enum || type.kind == TypeKind::Object)
               ? program.symbols[type.symbol].name
               : type.name;
    if (!type.arguments.empty()) {
        text += "<";
        for (std::size_t i = 0; i < type.arguments.size(); ++i)
            text += (i ? ", " : "") + type_text(program, type.arguments[i]);
        text += ">";
    }
    return text;
}

// The call of close_outcome in a Cleanup's return list, or nothing.
const IrNode* close_outcome_call(const IrProgram& program, const std::vector<IrNode>& nodes) {
    for (const auto& node : nodes) {
        if ((node.op == IrOp::Call || node.op == IrOp::VirtualCall) && node.symbol < program.symbols.size() &&
            program.symbols[node.symbol].name.starts_with("close_outcome"))
            return &node;
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            if (const auto* found = close_outcome_call(program, *region)) return found;
    }
    return nullptr;
}

// The `using` keyword: lowering saves the resource in the token `$using<N>`
// with an Initialize at the keyword, just before the Cleanup.
Span using_keyword(const IrProgram& program, const std::vector<IrNode>& nodes, std::size_t cleanup) {
    for (std::size_t i = cleanup; i-- > 0;) {
        const auto& node = nodes[i];
        if (node.op == IrOp::Initialize && node.symbol < program.symbols.size() &&
            program.symbols[node.symbol].name.starts_with("$using"))
            return node.span;
    }
    return nodes[cleanup].span;
}

void check_cleanups(const IrProgram& program, const IrBody& body, const std::vector<IrNode>& nodes) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto& node = nodes[index];
        if (node.op == IrOp::Cleanup && !node.alternative.empty()) {
            // After specialization `Result<T>` is an instance such as
            // `Result<int32[]>` with no type arguments of its own:
            // frame_owned reads its variants' payloads.
            if (const auto* call = close_outcome_call(program, node.alternative);
                call && frame_owned(program, call->type)) {
                const auto keyword = using_keyword(program, nodes, index);
                auto error = Diagnostic(keyword, "'using' cannot carry the returned '" + type_text(program, call->type) +
                                                     "' through its close")
                                 .with_code("E0473");
                if (body.owner < program.symbols.size()) {
                    // A lambda's body is its `$invoke`: name only a source
                    // function.
                    const auto& owner = program.symbols[body.owner];
                    const auto who = owner.name.starts_with('$') ? std::string("this function")
                                                                 : "'" + owner.name + "'";
                    error.label(owner.span, who + " returns '" + type_text(program, call->type) +
                                                "' with the type arguments it is called with");
                }
                error.note("a return inside 'using' passes its value to the close, which may replace it with the "
                           "close error");
                throw error.help("return a list, which the result can hold, or close the resource before building "
                                 "the array");
            }
        }
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            check_cleanups(program, body, *region);
    }
}

} // namespace

void check_using_results(const IrProgram& program) {
    for (const auto& body : program.bodies)
        if (uses_lifetime_operations(body.instructions)) check_cleanups(program, body, body.instructions);
}

bool uses_lifetime_operations(const std::vector<IrNode>& nodes) {
    for (const auto& node : nodes) {
        if (lifetime_operation(node.op)) return true;
        for (const auto* region : {&node.operands, &node.body, &node.alternative})
            if (uses_lifetime_operations(*region)) return true;
    }
    return false;
}

std::size_t count_yields(const std::vector<IrNode>& nodes) {
    std::size_t count = 0;
    for (const auto& node : nodes) {
        if (node.op == IrOp::Yield) ++count;
        for (const auto* region : {&node.operands, &node.body, &node.alternative}) count += count_yields(*region);
    }
    return count;
}

} // namespace neura
