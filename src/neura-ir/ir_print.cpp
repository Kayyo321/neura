#include "ir.hpp"

#include <ostream>
#include <string_view>

namespace neura {
namespace {

std::string_view spelling(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::File: return "file";
    case SymbolKind::Module: return "module";
    case SymbolKind::Class: return "class";
    case SymbolKind::Alias: return "alias";
    case SymbolKind::Import: return "import";
    case SymbolKind::Field: return "field";
    case SymbolKind::Function: return "function";
    case SymbolKind::Parameter: return "parameter";
    case SymbolKind::Local: return "local";
    case SymbolKind::Global: return "global";
    case SymbolKind::TypeParameter: return "type_parameter";
    case SymbolKind::Enum: return "enum";
    case SymbolKind::Variant: return "variant";
    case SymbolKind::Interface: return "interface";
    }
    return "<invalid>";
}

std::string_view spelling(TypeKind kind) {
    switch (kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::String: return "string";
    case TypeKind::Integer: return "integer";
    case TypeKind::Float: return "float";
    case TypeKind::Array: return "array";
    case TypeKind::List: return "list";
    case TypeKind::Object: return "object";
    case TypeKind::TypeParameter: return "type_parameter";
    case TypeKind::Module: return "module";
    case TypeKind::Enum: return "enum";
    case TypeKind::Callable: return "callable";
    case TypeKind::Tuple: return "tuple";
    }
    return "<invalid>";
}

std::string_view spelling(IrOp op) {
    switch (op) {
    case IrOp::Initialize: return "initialize";
    case IrOp::Store: return "store";
    case IrOp::Load: return "load";
    case IrOp::Reference: return "reference";
    case IrOp::Receiver: return "receiver";
    case IrOp::Field: return "field";
    case IrOp::Index: return "index";
    case IrOp::Collection: return "collection";
    case IrOp::RepeatCollection: return "repeat_collection";
    case IrOp::Integer: return "integer";
    case IrOp::Float: return "float";
    case IrOp::String: return "string";
    case IrOp::Boolean: return "boolean";
    case IrOp::Unary: return "unary";
    case IrOp::Binary: return "binary";
    case IrOp::ShortCircuit: return "short_circuit";
    case IrOp::Call: return "call";
    case IrOp::VirtualCall: return "virtual_call";
    case IrOp::ExternalCall: return "external_call";
    case IrOp::BaseInitialize: return "base_initialize";
    case IrOp::Allocate: return "allocate";
    case IrOp::Length: return "length";
    case IrOp::Append: return "append";
    case IrOp::RemoveAt: return "remove_at";
    case IrOp::Return: return "return";
    case IrOp::Block: return "block";
    case IrOp::Branch: return "branch";
    case IrOp::Loop: return "loop";
    case IrOp::Break: return "break";
    case IrOp::Continue: return "continue";
    case IrOp::Destroy: return "destroy";
    case IrOp::Discard: return "discard";
    case IrOp::Convert: return "convert";
    case IrOp::CollectionVersion: return "collection_version";
    case IrOp::Fail: return "fail";
    case IrOp::Tuple: return "tuple";
    case IrOp::Project: return "project";
    case IrOp::EnumConstruct: return "enum_construct";
    case IrOp::EnumTag: return "enum_tag";
    case IrOp::EnumProject: return "enum_project";
    case IrOp::ComparisonChain: return "comparison_chain";
    case IrOp::ScopedValue: return "scoped_value";
    case IrOp::SelectValue: return "select_value";
    case IrOp::MakeClosure: return "make_closure";
    case IrOp::CallClosure: return "call_closure";
    case IrOp::Nil: return "nil";
    case IrOp::ClassHash: return "class_hash";
    case IrOp::ClassText: return "class_text";
    case IrOp::ClassIs: return "class_is";
    case IrOp::ClassCast: return "class_cast";
    case IrOp::ValueEquals: return "value_equals";
    case IrOp::ValueHash: return "value_hash";
    case IrOp::ValueText: return "value_text";
    case IrOp::TextLength: return "text_length";
    case IrOp::TextByteAt: return "text_byte_at";
    case IrOp::TextSlice: return "text_slice";
    case IrOp::TextFromUtf8: return "text_from_utf8";
    case IrOp::TextEqual: return "text_equal";
    case IrOp::ReinterpretFloat: return "reinterpret_float";
    case IrOp::TrapMessage: return "trap_message";
    }
    return "<invalid>";
}

void print_quoted(std::ostream& output, std::string_view value) {
    constexpr std::string_view hex = "0123456789abcdef";
    output << '"';
    for (const char byte : value) {
        const auto character = static_cast<unsigned char>(byte);
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 32 || character == 127) {
                output << "\\x" << hex[character >> 4] << hex[character & 15];
            } else {
                output << byte;
            }
        }
    }
    output << '"';
}

void print_span(std::ostream& output, const Span& span) {
    if (!span.file.empty()) {
        output << " at ";
        print_quoted(output, span.file);
        output << ':' << span.line << ':' << span.column;
    }
}

void print_type(std::ostream& output, const IrType& type);

void print_type_arguments(std::ostream& output, const std::vector<IrType>& arguments) {
    if (arguments.empty()) {
        return;
    }
    output << '<';
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        print_type(output, arguments[index]);
    }
    output << '>';
}

void print_type(std::ostream& output, const IrType& type) {
    if (type.name.empty()) {
        output << spelling(type.kind);
    } else {
        output << type.name;
    }
    if (type.symbol != no_symbol) {
        output << '#' << type.symbol;
    }
    print_type_arguments(output, type.arguments);
}

void print_reference(std::ostream& output, std::string_view label, SymbolId symbol) {
    if (symbol != no_symbol) {
        output << ' ' << label << '%' << symbol;
    }
}

void print_references(std::ostream& output, std::string_view label, const std::vector<SymbolId>& symbols) {
    if (symbols.empty()) {
        return;
    }
    output << ' ' << label << '[';
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << '%' << symbols[index];
    }
    output << ']';
}

void print_symbol(std::ostream& output, SymbolId id, const IrSymbol& symbol) {
    output << "  %" << id << ' ' << spelling(symbol.kind) << ' ';
    print_quoted(output, symbol.name);
    output << " : ";
    print_type(output, symbol.type);
    print_reference(output, "owner=", symbol.owner);
    print_reference(output, "base=", symbol.base);
    print_reference(output, "overrides=", symbol.overrides);
    print_references(output, "parameters=", symbol.parameters);
    print_references(output, "generics=", symbol.generics);
    if (!symbol.payload_types.empty()) {
        output << " payload=";
        print_type_arguments(output, symbol.payload_types);
    }
    if (!symbol.library_path.empty()) {
        output << " library=";
        print_quoted(output, symbol.library_path);
    }
    if (symbol.host_binding) {
        output << " host=";
        print_quoted(output, symbol.host_binding->name);
        output << " version=" << symbol.host_binding->version;
    }
    if (symbol.bound) {
        output << " bound=";
        print_type(output, *symbol.bound);
    }
    if (!symbol.additional_bounds.empty()) {
        output << " additional_bounds=";
        print_type_arguments(output, symbol.additional_bounds);
    }
    if (!symbol.interfaces.empty()) {
        output << " interfaces=";
        print_type_arguments(output, symbol.interfaces);
    }
    if (symbol.exported) {
        output << " export";
    }
    if (symbol.constant) {
        output << " const";
    }
    if (symbol.external) {
        output << " external";
    }
    if (symbol.has_body) {
        output << " defined";
    }
    print_span(output, symbol.span);
    output << '\n';
}

void print_nodes(std::ostream& output, const std::vector<IrNode>& nodes, std::size_t depth);

void print_region(std::ostream& output, std::string_view name, const std::vector<IrNode>& nodes, std::size_t depth) {
    if (nodes.empty()) {
        return;
    }
    const std::string indent(depth * 2, ' ');
    output << indent << name << " {\n";
    print_nodes(output, nodes, depth + 1);
    output << indent << "}\n";
}

void print_nodes(std::ostream& output, const std::vector<IrNode>& nodes, std::size_t depth) {
    const std::string indent(depth * 2, ' ');
    for (const IrNode& node : nodes) {
        output << indent << spelling(node.op);
        print_reference(output, "", node.symbol);
        if (!node.type_arguments.empty()) {
            output << " type_arguments=";
            print_type_arguments(output, node.type_arguments);
        }
        if (!node.text.empty() || node.op == IrOp::String) {
            output << ' ';
            print_quoted(output, node.text);
        }
        output << " : ";
        print_type(output, node.type);
        print_span(output, node.span);
        output << '\n';
        print_region(output, "operands", node.operands, depth + 1);
        print_region(output, "body", node.body, depth + 1);
        print_region(output, node.op == IrOp::Loop ? "latch" : "else", node.alternative, depth + 1);
    }
}

} // namespace

void print_ir(std::ostream& output, const IrProgram& program) {
    output << "neura.ir\nsymbols {\n";
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        print_symbol(output, id, program.symbols[id]);
    }
    output << "}\n";
    for (const IrBody& body : program.bodies) {
        output << "body %" << body.owner << " {\n";
        print_nodes(output, body.instructions, 1);
        output << "}\n";
    }
}

} // namespace neura
