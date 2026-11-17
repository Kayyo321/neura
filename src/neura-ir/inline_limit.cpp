#include "inline_limit.hpp"

#include <algorithm>
#include <vector>

namespace neura {

namespace {

bool in_template(const IrProgram& program, SymbolId id) {
    for (auto scope = id; scope != no_symbol && scope < program.symbols.size(); scope = program.symbols[scope].owner) {
        const auto& item = program.symbols[scope];
        if (!item.generics.empty() || item.kind == SymbolKind::TypeParameter) return true;
        if (is_static_scope(item)) return false;
    }
    return false;
}

// The first tuple, in a pre-order walk of `type`, whose own word count is over
// the limit. A tuple that holds an oversized tuple is not itself reported: the
// inner one names the shape that is too large.
const IrType* oversized(const IrProgram& program, const IrType& type) {
    for (const auto& part : type.arguments) {
        if (const auto* inner = oversized(program, part)) return inner;
    }
    if (inline_value(program, type) && value_words(program, type) > max_inline_words) return &type;
    return nullptr;
}

void check_type(const IrProgram& program, const IrType& type, const Span& span) {
    if (type.kind == TypeKind::Void) return;
    if (const auto* big = oversized(program, type)) throw inline_limit_diagnostic(program, *big, span);
}

void check_node(const IrProgram& program, const IrNode& node) {
    check_type(program, node.type, node.span);
    for (const auto& argument : node.type_arguments) check_type(program, argument, node.span);
    for (const auto* region : {&node.operands, &node.body, &node.alternative}) {
        for (const auto& child : *region) check_node(program, child);
    }
}

} // namespace

std::string describe_shape(const IrProgram& program, const IrType& type) {
    switch (type.kind) {
    case TypeKind::Tuple: {
        std::vector<std::string> parts;
        for (const auto& element : type.arguments) parts.push_back(describe_shape(program, element));
        std::string text = "[";
        for (std::size_t i = 0; i < parts.size();) {
            auto run = i;
            while (run < parts.size() && parts[run] == parts[i]) ++run;
            if (i != 0) text += ", ";
            text += parts[i];
            if (run - i > 1) text += " x " + std::to_string(run - i);
            i = run;
        }
        return text + "]";
    }
    case TypeKind::Array:
    case TypeKind::List:
        if (type.arguments.size() == 1)
            return describe_shape(program, type.arguments.front()) + (type.kind == TypeKind::Array ? "[]" : "[list]");
        break;
    case TypeKind::Object:
    case TypeKind::Enum:
        if (type.symbol < program.symbols.size()) return program.symbols[type.symbol].name;
        break;
    default:
        break;
    }
    return type.name.empty() ? "value" : type.name;
}

Diagnostic inline_limit_diagnostic(const IrProgram& program, const IrType& type, const Span& span) {
    const auto shape = describe_shape(program, type);
    const auto words = value_words(program, type);
    auto error = Diagnostic(span, "the tuple " + shape + " has " + std::to_string(words) +
                                      " value words, more than the inline limit of " + std::to_string(max_inline_words))
                     .with_code("E0449");
    error.label("this type is stored inline");
    error.help("hold the elements in a class, which is a reference, or group them in smaller tuples held by a class");
    return error;
}

void check_inline_limit(const IrProgram& program) {
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        switch (item.kind) {
        case SymbolKind::Field:
        case SymbolKind::Parameter:
        case SymbolKind::Local:
        case SymbolKind::Global:
        case SymbolKind::Function:
        case SymbolKind::Variant:
            break;
        default:
            continue;
        }
        if (in_template(program, id)) continue;
        check_type(program, item.type, item.span);
        for (const auto& payload : item.payload_types) check_type(program, payload, item.span);
    }
    for (const auto& body : program.bodies) {
        if (in_template(program, body.owner)) continue;
        for (const auto& node : body.instructions) check_node(program, node);
    }
}

} // namespace neura
