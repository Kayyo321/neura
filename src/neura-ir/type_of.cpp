#include "type_of.hpp"

#include "any.hpp"
#include "attributes.hpp"
#include "dispatch.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace neura {
namespace {

// The ordinals of Std.Reflect.Type.TypeKind, in declaration order.
enum class Kind : int {
    Nil, Bool, Integer, Float, String, Class, Interface, Record, Enum, Tuple, Array, List, Function, Any,
    TypeParameter, Other
};

struct Parts {
    Kind kind = Kind::Other;
    std::string name;
    std::string qualified;
};

const IrType int_type{TypeKind::Integer, "int32", no_symbol, {}};
const IrType uint32_type{TypeKind::Integer, "uint32", no_symbol, {}};
const IrType text_type{TypeKind::String, "string", no_symbol, {}};
const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};
const IrType parts_type{TypeKind::Tuple, {}, no_symbol, {int_type, text_type, text_type}};

IrNode make(IrOp op, const Span& span, IrType type) {
    IrNode result{};
    result.op = op;
    result.span = span;
    result.type = std::move(type);
    result.symbol = no_symbol;
    return result;
}

bool concrete(const IrType& type) {
    return type.kind != TypeKind::TypeParameter && std::ranges::all_of(type.arguments, concrete);
}

class Expander {
public:
    explicit Expander(IrProgram& program) : program_(program), dispatch_(program) {
        for (const auto& body : program_.bodies) collect(body.instructions);
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = program_.symbols[id];
            if (item.kind != SymbolKind::Class) continue;
            if (item.name == "$class") root_ = id;
            if (item.generics.empty() && !item.name.starts_with('$') && !item.name.starts_with('&') &&
                allocated_.contains(id))
                classes_.push_back(id);
        }
        // Deeper classes first, so that the first test that holds names the
        // object's own class; ties in symbol order, for a stable image.
        std::ranges::stable_sort(classes_, [&](SymbolId a, SymbolId b) { return depth(a) > depth(b); });
    }

    void run() {
        for (auto& body : program_.bodies) walk(body.instructions);
        for (auto& constant : program_.static_constants) walk_one(constant.value);
    }

private:
    IrProgram& program_;
    DispatchTargets dispatch_;
    std::set<SymbolId> allocated_;
    std::vector<SymbolId> classes_;
    SymbolId root_ = no_symbol;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool valid(SymbolId id) const { return id < program_.symbols.size(); }

    void collect(const std::vector<IrNode>& nodes) {
        for (const auto& node : nodes) {
            if (node.op == IrOp::Allocate && valid(node.type.symbol)) allocated_.insert(node.type.symbol);
            if (node.op == IrOp::Allocate && valid(node.symbol)) allocated_.insert(node.symbol);
            collect(node.operands);
            collect(node.body);
            collect(node.alternative);
        }
    }

    std::size_t depth(SymbolId id) const {
        std::size_t result = 0;
        for (auto base = symbol(id).base; valid(base) && result < program_.symbols.size(); base = symbol(base).base)
            ++result;
        return result;
    }

    // The name the image uses for class text: the owners' names, outside
    // static scopes, up to the file (IRtoNeuronConversion, qualified()).
    std::string qualified(SymbolId id) const {
        std::string name = symbol(id).name;
        for (auto owner = symbol(id).owner; valid(owner) && symbol(owner).kind != SymbolKind::File;
             owner = symbol(owner).owner) {
            if (!is_static_scope(symbol(owner))) name = symbol(owner).name + "." + name;
        }
        return name;
    }

    // A function value's signature after specialization gave it a
    // `$signature` layout: the parameters and result of its `$invoke`.
    std::optional<Parts> signature(SymbolId layout) const {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.owner != layout || item.kind != SymbolKind::Function || item.name != "$invoke") continue;
            Parts parts{Kind::Function, "func(", "func("};
            for (std::size_t i = 0; i < item.parameters.size(); ++i) {
                const auto described = describe(symbol(item.parameters[i]).type);
                parts.name += (i ? ", " : "") + described.name;
                parts.qualified += (i ? ", " : "") + described.qualified;
            }
            const auto result = describe(item.type);
            parts.name += ") -> " + result.name;
            parts.qualified += ") -> " + result.qualified;
            return parts;
        }
        return std::nullopt;
    }

    Parts describe(const IrType& type) const {
        switch (type.kind) {
        case TypeKind::Bool: return {Kind::Bool, "bool", "bool"};
        case TypeKind::Integer: return {Kind::Integer, type.name, type.name};
        case TypeKind::Float: return {Kind::Float, type.name, type.name};
        case TypeKind::String: return {Kind::String, "string", "string"};
        case TypeKind::Any: return {Kind::Any, "any", "any"};
        case TypeKind::TypeParameter: return {Kind::TypeParameter, type.name, type.name};
        case TypeKind::Array:
        case TypeKind::List: {
            const auto suffix = type.kind == TypeKind::Array ? "[]" : "[list]";
            const auto element = type.arguments.empty() ? Parts{} : describe(type.arguments.front());
            return {type.kind == TypeKind::Array ? Kind::Array : Kind::List, element.name + suffix,
                    element.qualified + suffix};
        }
        case TypeKind::Tuple: {
            Parts parts{Kind::Tuple, "[", "["};
            for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                const auto element = describe(type.arguments[i]);
                parts.name += (i ? ", " : "") + element.name;
                parts.qualified += (i ? ", " : "") + element.qualified;
            }
            parts.name += "]";
            parts.qualified += "]";
            return parts;
        }
        case TypeKind::Callable: {
            if (!is_function_value(type)) return {Kind::Function, "func", "func"};
            Parts parts{Kind::Function, "func(", "func("};
            for (std::size_t i = 0; i + 1 < type.arguments.size(); ++i) {
                const auto parameter = describe(type.arguments[i]);
                parts.name += (i ? ", " : "") + parameter.name;
                parts.qualified += (i ? ", " : "") + parameter.qualified;
            }
            const auto result = describe(type.arguments.back());
            parts.name += ") -> " + result.name;
            parts.qualified += ") -> " + result.qualified;
            return parts;
        }
        case TypeKind::Enum:
            if (!valid(type.symbol)) return {Kind::Enum, type.name, type.name};
            return {Kind::Enum, symbol(type.symbol).name, qualified(type.symbol)};
        case TypeKind::Object: {
            if (!valid(type.symbol) || type.name.starts_with('&')) return {Kind::Other, type.name, type.name};
            const auto& item = symbol(type.symbol);
            if (type.symbol == root_ || item.name == "$class") return {Kind::Class, "class", "class"};
            if (item.name.starts_with("$signature"))
                if (auto parts = signature(type.symbol)) return *parts;
            if (item.kind == SymbolKind::Interface) return {Kind::Interface, item.name, qualified(type.symbol)};
            const auto kind = has_attribute(item, AttributeId::ClassRecord) ? Kind::Record : Kind::Class;
            return {kind, item.name, qualified(type.symbol)};
        }
        case TypeKind::Void: return {Kind::Other, "void", "void"};
        default: return {Kind::Other, type.name, type.name};
        }
    }

    IrNode tuple(const Parts& parts, const Span& span) const {
        auto result = make(IrOp::Tuple, span, parts_type);
        auto kind = make(IrOp::Integer, span, int_type);
        kind.text = std::to_string(static_cast<int>(parts.kind));
        auto name = make(IrOp::String, span, text_type);
        name.text = parts.name;
        auto qualified_name = make(IrOp::String, span, text_type);
        qualified_name.text = parts.qualified;
        result.operands.push_back(std::move(kind));
        result.operands.push_back(std::move(name));
        result.operands.push_back(std::move(qualified_name));
        return result;
    }

    IrNode select(IrNode condition, IrNode chosen, IrNode otherwise, const Span& span) const {
        auto result = make(IrOp::SelectValue, span, parts_type);
        result.operands.push_back(std::move(condition));
        result.body.push_back(std::move(chosen));
        result.alternative.push_back(std::move(otherwise));
        return result;
    }

    // The classes a reference of static type `type` can hold, most-derived
    // first, each tested with ClassIs; nil (no test holds) is kind Nil.
    IrNode class_chain(const IrNode& value, const IrType& type, const Span& span) const {
        auto result = tuple({Kind::Nil, "nil", "nil"}, span);
        const bool any_class = type.symbol == root_ || !valid(type.symbol);
        for (auto it = classes_.rbegin(); it != classes_.rend(); ++it) {
            if (!any_class && !dispatch_.derives(*it, type.symbol)) continue;
            auto test = make(IrOp::ClassIs, span, bool_type);
            test.type_arguments.push_back(symbol(*it).type);
            test.operands.push_back(value);
            result = select(std::move(test), tuple(describe(symbol(*it).type), span), std::move(result), span);
        }
        return result;
    }

    // An `any`: its tag selects a scalar or string type; a reference is
    // tested as a class (any.hpp, the fixed tags).
    IrNode any_chain(const IrNode& load, const Span& span) const {
        const auto tag_is = [&](std::uint32_t tag) {
            auto read = make(IrOp::AnyTag, span, uint32_type);
            read.operands.push_back(load);
            auto constant = make(IrOp::Integer, span, uint32_type);
            constant.text = std::to_string(tag);
            auto result = make(IrOp::Binary, span, bool_type);
            result.text = "==";
            result.operands.push_back(std::move(read));
            result.operands.push_back(std::move(constant));
            return result;
        };
        IrNode result = tuple({Kind::Nil, "nil", "nil"}, span);
        if (valid(root_)) {
            auto unpack = make(IrOp::AnyUnpack, span, symbol(root_).type);
            unpack.operands.push_back(load);
            result = select(tag_is(any_reference_tag), class_chain(unpack, symbol(root_).type, span), std::move(result), span);
        }
        constexpr const char* scalars[]{"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32",
                                        "uint64", "float16", "bfloat16", "float32", "float64", "string"};
        for (std::uint32_t tag = 14; tag >= 1; --tag) {
            const std::string name = scalars[tag - 1];
            const auto kind = tag == 1 ? Kind::Bool : tag <= 9 ? Kind::Integer : tag <= 13 ? Kind::Float : Kind::String;
            result = select(tag_is(tag), tuple({kind, name, name}, span), std::move(result), span);
        }
        return result;
    }

    bool expandable(const IrNode& node) const {
        if (node.operands.empty()) return node.type_arguments.size() == 1 && concrete(node.type_arguments.front());
        return concrete(node.operands.front().type);
    }

    IrNode expand(const IrNode& node) const {
        if (node.operands.empty()) return tuple(describe(node.type_arguments.front()), node.span);
        const auto& value = node.operands.front();
        const auto& type = value.type;
        if (type.kind == TypeKind::Any) return any_chain(value, node.span);
        if (type.kind == TypeKind::Object && valid(type.symbol) && !type.name.starts_with('&') &&
            !symbol(type.symbol).name.starts_with("$signature"))
            return class_chain(value, type, node.span);
        return tuple(describe(type), node.span);
    }

    void walk_one(IrNode& node) {
        walk(node.operands);
        walk(node.body);
        walk(node.alternative);
        if (node.op == IrOp::TypeOf && expandable(node)) node = expand(node);
    }

    void walk(std::vector<IrNode>& nodes) {
        for (auto& node : nodes) walk_one(node);
    }
};

} // namespace

void expand_type_of(IrProgram& program) {
    Expander(program).run();
}

} // namespace neura
