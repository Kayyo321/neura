#include "verifier.hpp"

#include "aggregate.hpp"
#include "control_flow.hpp"
#include "integer.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>

namespace neura {
namespace {

std::string_view kind_name(TypeKind kind) {
    switch (kind) {
    case TypeKind::Void: return "void";
    case TypeKind::Bool: return "bool";
    case TypeKind::String: return "string";
    case TypeKind::Integer: return "integer";
    case TypeKind::Float: return "float";
    case TypeKind::Array: return "array";
    case TypeKind::List: return "list";
    case TypeKind::Object: return "object";
    case TypeKind::TypeParameter: return "type parameter";
    case TypeKind::Module: return "module";
    case TypeKind::Enum: return "enum";
    case TypeKind::Callable: return "callable";
    case TypeKind::Tuple: return "tuple";
    }
    return "<invalid>";
}

std::string describe(const IrType& type) {
    if ((type.kind == TypeKind::Array || type.kind == TypeKind::List) && type.arguments.size() == 1) {
        return describe(type.arguments.front()) + (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    std::string result = type.name.empty() ? std::string(kind_name(type.kind)) : type.name;
    if (!type.arguments.empty()) {
        result += '<';
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            result += (i == 0 ? "" : ", ") + describe(type.arguments[i]);
        }
        result += '>';
    }
    return result;
}

bool is_statement(IrOp op) {
    switch (op) {
    case IrOp::Initialize:
    case IrOp::Store:
    case IrOp::Discard:
    case IrOp::Block:
    case IrOp::Branch:
    case IrOp::Loop:
    case IrOp::Return:
    case IrOp::Break:
    case IrOp::Continue:
    case IrOp::Destroy:
    case IrOp::Fail:
        return true;
    default:
        return false;
    }
}

// True when `region` holds a return, break, or continue at any depth. A
// loop latch may hold none of them.
bool transfers_control(const std::vector<IrNode>& region) {
    return std::ranges::any_of(region, [](const IrNode& node) {
        return node.op == IrOp::Return || node.op == IrOp::Break || node.op == IrOp::Continue ||
               transfers_control(node.body) || transfers_control(node.alternative);
    });
}

bool is_collection(const IrType& type) {
    return (type.kind == TypeKind::Array || type.kind == TypeKind::List) && type.arguments.size() == 1;
}

class Verifier {
public:
    explicit Verifier(const IrProgram& program)
        : program_(program), initialized_(program.symbols.size(), false) {}

    std::vector<VerificationIssue> run() {
        check_symbols();
        check_bodies();
        return std::move(issues_);
    }

private:
    struct Context {
        SymbolId owner = no_symbol;
        SymbolId function = no_symbol;
        unsigned loops = 0;
    };

    const IrProgram& program_;
    std::vector<VerificationIssue> issues_;
    std::vector<std::vector<SymbolId>> scopes_;
    std::vector<bool> initialized_;

    bool valid(SymbolId id) const { return id < program_.symbols.size(); }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }

    void report(const Span& span, std::string message) {
        issues_.push_back({span, std::move(message)});
    }

    std::string name(SymbolId id) const {
        return "%" + std::to_string(id) + (valid(id) ? " '" + symbol(id).name + "'" : "");
    }

    void check_type_references(const IrType& type, const Span& span) {
        if (type.symbol != no_symbol && !valid(type.symbol)) {
            report(span, "type " + describe(type) + " refers to missing symbol %" +
                         std::to_string(type.symbol));
        }
        if (type.kind == TypeKind::Tuple &&
            (type.symbol != no_symbol || !type.name.empty() || type.arguments.empty() ||
             std::ranges::any_of(type.arguments, [](const IrType& element) {
                 return element.kind == TypeKind::Void || element.kind == TypeKind::Module ||
                        element.kind == TypeKind::Callable;
             }))) {
            report(span, "a tuple must have one or more value types and no symbol");
        }
        if (type.kind == TypeKind::Enum &&
            (!valid(type.symbol) || symbol(type.symbol).kind != SymbolKind::Enum || !type.arguments.empty())) {
            report(span, "an enum type must name an enum declaration");
        }
        for (const auto& argument : type.arguments) {
            check_type_references(argument, span);
        }
    }

    bool assignable(const IrType& actual, const IrType& expected, unsigned depth = 0) const {
        if (actual == expected) {
            return true;
        }
        if (depth > 8 || !valid(actual.symbol)) {
            return false;
        }
        const auto& bound = symbol(actual.symbol).bound;
        if (actual.kind == TypeKind::TypeParameter && bound) {
            return assignable(*bound, expected, depth + 1);
        }
        if (actual.kind != TypeKind::Object || expected.kind != TypeKind::Object ||
            !expected.arguments.empty()) {
            return false;
        }
        std::size_t steps = 0;
        for (auto base = symbol(actual.symbol).base; valid(base) && steps < program_.symbols.size();
             base = symbol(base).base, ++steps) {
            if (base == expected.symbol) {
                return true;
            }
        }
        return false;
    }

    void require_assignable(const IrType& actual, const IrType& expected, const Span& span,
                            std::string_view role) {
        if (!assignable(actual, expected)) {
            report(span, std::string(role) + " has type " + describe(actual) + ", expected " +
                         describe(expected));
        }
    }

    void check_symbols() {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& entry = symbol(id);
            for (const auto& [label, reference] : {std::pair{"owner", entry.owner},
                                                  std::pair{"base", entry.base},
                                                  std::pair{"overrides", entry.overrides}}) {
                if (reference != no_symbol && !valid(reference)) {
                    report(entry.span, "symbol " + name(id) + " has missing " + label + " %" +
                                       std::to_string(reference));
                }
            }
            for (const auto parameter : entry.parameters) {
                if (!valid(parameter) || symbol(parameter).kind != SymbolKind::Parameter ||
                    symbol(parameter).owner != id) {
                    report(entry.span, "symbol " + name(id) + " lists " + name(parameter) +
                                       " as a parameter it does not own");
                }
            }
            for (const auto generic : entry.generics) {
                if (!valid(generic) || symbol(generic).kind != SymbolKind::TypeParameter) {
                    report(entry.span, "symbol " + name(id) + " lists " + name(generic) +
                                       " as a type parameter");
                }
            }
            check_type_references(entry.type, entry.span);
            if (entry.bound) {
                check_type_references(*entry.bound, entry.span);
            }
            if (entry.kind == SymbolKind::Variant &&
                (!valid(entry.owner) || symbol(entry.owner).kind != SymbolKind::Enum ||
                 entry.type.kind != TypeKind::Enum || entry.type.symbol != entry.owner)) {
                report(entry.span, "a variant must belong to its enum type");
            }
            if (!entry.payload_types.empty() && entry.kind != SymbolKind::Variant) {
                report(entry.span, "only enum variants can declare payload fields");
            }
            for (const auto& payload : entry.payload_types) {
                check_type_references(payload, entry.span);
                if (payload.kind == TypeKind::Void || payload.kind == TypeKind::Module ||
                    payload.kind == TypeKind::Callable) report(entry.span, "an enum payload needs a value type");
            }
        }
    }

    void check_bodies() {
        std::set<SymbolId> owners;
        for (const auto& body : program_.bodies) {
            Span span;
            if (!valid(body.owner)) {
                report(span, "body owner %" + std::to_string(body.owner) + " does not exist");
                continue;
            }
            const auto& owner = symbol(body.owner);
            span = owner.span;
            if (!owners.insert(body.owner).second) {
                report(span, "symbol " + name(body.owner) + " has more than one body");
            }
            Context context{body.owner, no_symbol, 0};
            switch (owner.kind) {
            case SymbolKind::Function:
                if (!owner.has_body || owner.external) {
                    report(span, "function " + name(body.owner) + " has a body but is not defined here");
                }
                context.function = body.owner;
                break;
            case SymbolKind::File:
            case SymbolKind::Module:
            case SymbolKind::Class:
                break;
            default:
                report(span, "symbol " + name(body.owner) + " cannot own a body");
                continue;
            }
            check_region(body.instructions, context);
            if (context.function != no_symbol && owner.type.kind != TypeKind::Void &&
                !always_returns(body.instructions)) {
                report(span, "function " + name(body.owner) + " can finish without returning " +
                             describe(owner.type));
            }
        }
    }

    void check_region(const std::vector<IrNode>& region, Context& context) {
        scopes_.emplace_back();
        for (const auto& node : region) {
            check_statement(node, context);
        }
        scopes_.pop_back();
    }

    bool visible(SymbolId local) const {
        return std::ranges::any_of(scopes_, [local](const auto& scope) {
            return std::ranges::find(scope, local) != scope.end();
        });
    }

    void check_node(const IrNode& node) {
        if (node.span.file.empty()) {
            report(node.span, "operation has no source span");
        }
        check_type_references(node.type, node.span);
        for (const auto& argument : node.type_arguments) {
            check_type_references(argument, node.span);
        }
    }

    bool expect_operands(const IrNode& node, std::size_t count, std::string_view operation) {
        if (node.operands.size() != count) {
            report(node.span, std::string(operation) + " expects " + std::to_string(count) +
                              " operand(s), found " + std::to_string(node.operands.size()));
            return false;
        }
        return true;
    }

    void expect_no_regions(const IrNode& node, std::string_view operation) {
        if (!node.body.empty() || !node.alternative.empty()) {
            report(node.span, std::string(operation) + " cannot have nested regions");
        }
    }

    void expect_type(const IrNode& node, TypeKind kind, std::string_view role) {
        if (node.type.kind != kind) {
            report(node.span, std::string(role) + " has type " + describe(node.type) + ", expected " +
                              std::string(kind_name(kind)));
        }
    }

    void check_statement(const IrNode& node, Context& context) {
        check_node(node);
        if (!is_statement(node.op)) {
            report(node.span, "an expression operation appears in a statement region");
            check_expression(node, context, true);
            return;
        }
        if (node.op != IrOp::Block && node.op != IrOp::Branch && node.op != IrOp::Loop) {
            expect_no_regions(node, "a simple statement");
        }
        if (node.type.kind != TypeKind::Void) {
            report(node.span, "a statement has type " + describe(node.type));
        }
        switch (node.op) {
        case IrOp::Initialize: check_initialize(node, context); break;
        case IrOp::Store: check_store(node, context); break;
        case IrOp::Discard:
            if (expect_operands(node, 1, "discard")) {
                check_expression(node.operands.front(), context, true);
            }
            break;
        case IrOp::Block:
            expect_operands(node, 0, "block");
            if (!node.alternative.empty()) {
                report(node.span, "a block has an else region");
            }
            check_region(node.body, context);
            break;
        case IrOp::Branch:
            if (expect_operands(node, 1, "branch")) {
                check_value(node.operands.front(), context);
                expect_type(node.operands.front(), TypeKind::Bool, "branch condition");
            }
            check_region(node.body, context);
            check_region(node.alternative, context);
            break;
        case IrOp::Loop:
            if (expect_operands(node, 1, "loop")) {
                check_value(node.operands.front(), context);
                expect_type(node.operands.front(), TypeKind::Bool, "loop condition");
            }
            ++context.loops;
            check_region(node.body, context);
            --context.loops;
            if (transfers_control(node.alternative)) {
                report(node.span, "a loop latch contains return, break, or continue");
            }
            check_region(node.alternative, context);
            break;
        case IrOp::Return: check_return(node, context); break;
        case IrOp::Break:
        case IrOp::Continue:
            expect_operands(node, 0, "loop control");
            if (context.loops == 0) {
                report(node.span, "loop control appears outside a loop");
            }
            break;
        case IrOp::Fail:
            expect_operands(node, 0, "fail");
            if (node.text != "iteration") {
                report(node.span, "fail names unknown failure '" + node.text + "'");
            }
            break;
        case IrOp::Destroy:
            if (expect_operands(node, 1, "destroy")) {
                const auto& object = node.operands.front();
                check_value(object, context);
                if (object.type.kind != TypeKind::Object || object.type.symbol != node.symbol) {
                    report(node.span, "destroy names a class other than its operand's type " +
                                      describe(object.type));
                }
            }
            break;
        default:
            break;
        }
    }

    void check_initialize(const IrNode& node, Context& context) {
        if (!expect_operands(node, 1, "initialize")) {
            return;
        }
        check_value(node.operands.front(), context);
        if (!valid(node.symbol)) {
            report(node.span, "initialize refers to missing symbol " + name(node.symbol));
            return;
        }
        const auto& target = symbol(node.symbol);
        if (target.kind != SymbolKind::Local && target.kind != SymbolKind::Global &&
            target.kind != SymbolKind::Field) {
            report(node.span, "initialize targets " + name(node.symbol) + ", which is not a binding");
            return;
        }
        if (target.owner != context.owner) {
            report(node.span, "initialize of " + name(node.symbol) + " appears outside its owner's body");
        }
        if (initialized_[node.symbol]) {
            report(node.span, "binding " + name(node.symbol) + " is initialized more than once");
        }
        initialized_[node.symbol] = true;
        require_assignable(node.operands.front().type, target.type, node.span, "initializer");
        if (target.kind == SymbolKind::Local) {
            scopes_.back().push_back(node.symbol);
        }
    }

    void check_store(const IrNode& node, Context& context) {
        if (!expect_operands(node, 2, "store")) {
            return;
        }
        const auto& destination = node.operands[0];
        const auto& source = node.operands[1];
        if (destination.op != IrOp::Load && destination.op != IrOp::Field && destination.op != IrOp::Index) {
            report(destination.span, "store destination is not a binding, field, or element");
        }
        check_value(destination, context);
        check_value(source, context);
        if ((destination.op == IrOp::Load || destination.op == IrOp::Field) && valid(destination.symbol) &&
            symbol(destination.symbol).constant) {
            report(destination.span, "store writes to const " + name(destination.symbol));
        }
        require_assignable(source.type, destination.type, node.span, "stored value");
    }

    void check_return(const IrNode& node, Context& context) {
        if (context.function == no_symbol) {
            report(node.span, "return appears outside a function body");
            return;
        }
        const auto& expected = symbol(context.function).type;
        if (expected.kind == TypeKind::Void) {
            expect_operands(node, 0, "return from a void function");
            return;
        }
        if (expect_operands(node, 1, "return")) {
            check_value(node.operands.front(), context);
            require_assignable(node.operands.front().type, expected, node.span, "returned value");
        }
    }

    void check_value(const IrNode& node, Context& context) { check_expression(node, context, false); }

    void check_expression(const IrNode& node, Context& context, bool allow_void) {
        check_node(node);
        if (is_statement(node.op)) {
            report(node.span, "a statement operation appears as an expression");
            return;
        }
        if (node.type.kind == TypeKind::Void && !allow_void) {
            report(node.span, "a void expression is used as a value");
        }
        const bool allows_body = node.op == IrOp::ShortCircuit || node.op == IrOp::ComparisonChain ||
                                 node.op == IrOp::ScopedValue || node.op == IrOp::SelectValue;
        if ((node.op != IrOp::SelectValue && !node.alternative.empty()) || (!allows_body && !node.body.empty())) {
            report(node.span, "expression has a region it does not use");
        }
        for (const auto& operand : node.operands) {
            check_value(operand, context);
        }
        switch (node.op) {
        case IrOp::Load: check_load(node, context); break;
        case IrOp::Field:
            check_symbol_kind(node, {SymbolKind::Field}, "field");
            if (node.operands.size() > 1) {
                report(node.span, "field access has more than one receiver");
            }
            break;
        case IrOp::Index:
            if (expect_operands(node, 2, "index")) {
                check_element_access(node, node.operands[0], node.operands[1]);
                if (is_collection(node.operands[0].type) && node.type != node.operands[0].type.arguments.front()) {
                    report(node.span, "index result has type " + describe(node.type) +
                                      ", expected the element type");
                }
            }
            break;
        case IrOp::Reference:
            expect_operands(node, 0, "reference");
            check_symbol_kind(node, {SymbolKind::Variant, SymbolKind::Module, SymbolKind::Import,
                                     SymbolKind::Class, SymbolKind::Alias, SymbolKind::Enum},
                              "reference");
            if (valid(node.symbol) && symbol(node.symbol).kind == SymbolKind::Variant &&
                (node.type != symbol(node.symbol).type || has_enum_payload(program_, symbol(node.symbol).owner)))
                report(node.span, "a payload enum value requires a variant constructor");
            break;
        case IrOp::Receiver:
            expect_operands(node, 0, "receiver");
            check_symbol_kind(node, {SymbolKind::Class, SymbolKind::Module}, "receiver");
            break;
        case IrOp::Collection:
            if (!is_collection(node.type)) {
                report(node.span, "collection literal has type " + describe(node.type));
                break;
            }
            for (const auto& element : node.operands) {
                require_assignable(element.type, node.type.arguments.front(), element.span, "element");
            }
            break;
        case IrOp::Tuple:
            if (node.type.kind != TypeKind::Tuple ||
                !expect_operands(node, node.type.arguments.size(), "tuple construction")) {
                if (node.type.kind != TypeKind::Tuple) report(node.span, "tuple construction needs a tuple type");
                break;
            }
            for (std::size_t i = 0; i < node.operands.size(); ++i)
                require_assignable(node.operands[i].type, node.type.arguments[i], node.operands[i].span, "tuple element");
            break;
        case IrOp::Project:
        case IrOp::EnumProject:
            check_projection(node);
            break;
        case IrOp::EnumConstruct:
            check_enum_construct(node);
            break;
        case IrOp::EnumTag:
            if (expect_operands(node, 1, "enum tag")) {
                const auto& type = node.operands.front().type;
                if (type.kind != TypeKind::Enum || !has_enum_payload(program_, type.symbol))
                    report(node.span, "enum tag requires an enum with payloads");
            }
            if (node.type != IrType{TypeKind::Integer, "uint32", no_symbol, {}})
                report(node.span, "enum tag result must be uint32");
            break;
        case IrOp::Integer: check_integer_literal(node); break;
        case IrOp::Float:
            expect_operands(node, 0, "float literal");
            expect_type(node, TypeKind::Float, "float literal");
            break;
        case IrOp::String:
            expect_operands(node, 0, "string literal");
            expect_type(node, TypeKind::String, "string literal");
            break;
        case IrOp::Boolean:
            expect_operands(node, 0, "boolean literal");
            expect_type(node, TypeKind::Bool, "boolean literal");
            if (node.text != "true" && node.text != "false") {
                report(node.span, "boolean literal is spelled '" + node.text + "'");
            }
            break;
        case IrOp::Unary: check_unary(node); break;
        case IrOp::Binary: check_binary(node); break;
        case IrOp::ShortCircuit: check_short_circuit(node, context); break;
        case IrOp::ComparisonChain: check_comparison_chain(node, context); break;
        case IrOp::ScopedValue:
            check_scoped_value(node, context);
            break;
        case IrOp::SelectValue:
            check_select_value(node, context);
            break;
        case IrOp::Call:
            if (check_symbol_kind(node, {SymbolKind::Function}, "call") &&
                (symbol(node.symbol).external || !symbol(node.symbol).has_body)) {
                report(node.span, "direct call targets " + name(node.symbol) + ", which has no body");
            }
            check_arity(node, 0);
            check_type_arguments(node);
            break;
        case IrOp::VirtualCall:
            if (check_symbol_kind(node, {SymbolKind::Function}, "virtual call") &&
                symbol(node.symbol).external) {
                report(node.span, "virtual call targets external function " + name(node.symbol));
            }
            check_arity(node, 1);
            check_type_arguments(node);
            break;
        case IrOp::ExternalCall:
            if (check_symbol_kind(node, {SymbolKind::Function}, "external call") &&
                !symbol(node.symbol).external) {
                report(node.span, "external call targets non-external function " + name(node.symbol));
            }
            check_arity(node, 0);
            break;
        case IrOp::BaseInitialize:
            if (node.operands.empty() || node.operands.front().op != IrOp::Receiver) {
                report(node.span, "base initialization needs a receiver operand");
            }
            if (node.symbol == no_symbol || check_symbol_kind(node, {SymbolKind::Function}, "base initialization")) {
                check_arity(node, 1);
            }
            break;
        case IrOp::Allocate:
            expect_type(node, TypeKind::Object, "allocation");
            if (node.symbol == no_symbol || check_symbol_kind(node, {SymbolKind::Function}, "allocation")) {
                check_arity(node, 0);
                if (valid(node.symbol)) {
                    const auto& constructor = symbol(node.symbol);
                    if (constructor.name != "init" || !valid(constructor.owner) ||
                        symbol(constructor.owner).kind != SymbolKind::Class ||
                        (constructor.owner != node.type.symbol &&
                         !assignable(symbol(constructor.owner).type, node.type))) {
                        report(node.span, "allocation has a constructor for an incompatible class");
                    }
                }
            }
            break;
        case IrOp::Length:
            if (expect_operands(node, 1, "length") && !is_collection(node.operands.front().type)) {
                report(node.span, "length requires an array or list");
            }
            expect_type(node, TypeKind::Integer, "length");
            break;
        case IrOp::Append:
            if (expect_operands(node, 2, "append")) {
                const auto& list = node.operands[0].type;
                if (list.kind != TypeKind::List || list.arguments.size() != 1) {
                    report(node.span, "append requires a list");
                } else {
                    require_assignable(node.operands[1].type, list.arguments.front(), node.span, "appended value");
                }
            }
            break;
        case IrOp::Convert:
            if (expect_operands(node, 1, "conversion")) {
                const auto& from = node.operands.front().type;
                const auto numeric = [](const IrType& t) {
                    return t.kind == TypeKind::Integer || t.kind == TypeKind::Float;
                };
                if (!numeric(node.type) || !(numeric(from) || (from.kind == TypeKind::Enum &&
                                                                !has_enum_payload(program_, from.symbol) &&
                                                                node.type.kind == TypeKind::Integer))) {
                    report(node.span, "cannot convert " + describe(from) + " to " + describe(node.type));
                }
            }
            break;
        case IrOp::CollectionVersion:
            if (expect_operands(node, 1, "collection version") &&
                node.operands.front().type.kind != TypeKind::List) {
                report(node.span, "collection version requires a list");
            }
            if (node.type != IrType{TypeKind::Integer, "uint64", no_symbol, {}}) {
                report(node.span, "collection version has type " + describe(node.type) + ", expected uint64");
            }
            break;
        case IrOp::RemoveAt:
            if (expect_operands(node, 2, "remove_at")) {
                if (node.operands[0].type.kind != TypeKind::List) {
                    report(node.span, "remove_at requires a list");
                }
                expect_type(node.operands[1], TypeKind::Integer, "list index");
            }
            break;
        default:
            break;
        }
    }

    bool check_symbol_kind(const IrNode& node, std::initializer_list<SymbolKind> kinds,
                           std::string_view operation) {
        if (!valid(node.symbol)) {
            report(node.span, std::string(operation) + " refers to missing symbol " + name(node.symbol));
            return false;
        }
        if (std::ranges::find(kinds, symbol(node.symbol).kind) == kinds.end()) {
            report(node.span, std::string(operation) + " refers to " + name(node.symbol) +
                              ", which has the wrong kind");
            return false;
        }
        return true;
    }

    void check_arity(const IrNode& node, std::size_t leading) {
        const auto parameters = valid(node.symbol) ? symbol(node.symbol).parameters.size() : 0;
        if (node.operands.size() != leading + parameters) {
            report(node.span, "call passes " + std::to_string(node.operands.size() - std::min(leading, node.operands.size())) +
                              " argument(s) to " + name(node.symbol) + ", which takes " +
                              std::to_string(parameters));
        }
    }

    // A generic call supplies one type argument per type parameter, and each
    // satisfies its parameter's bound. Lowering checks bounds too, but a .nir
    // file reaches the backend without lowering.
    void check_type_arguments(const IrNode& node) {
        if (!valid(node.symbol)) {
            return;
        }
        const auto& generics = symbol(node.symbol).generics;
        if (node.type_arguments.size() != generics.size()) {
            report(node.span, "call passes " + std::to_string(node.type_arguments.size()) + " type argument(s) to " +
                              name(node.symbol) + ", which takes " + std::to_string(generics.size()));
            return;
        }
        for (std::size_t i = 0; i < generics.size(); ++i) {
            if (!valid(generics[i]) || !symbol(generics[i]).bound) {
                continue;
            }
            auto bound = *symbol(generics[i]).bound;
            for (std::size_t j = 0; j < generics.size(); ++j) {
                bound = substitute(std::move(bound), generics[j], node.type_arguments[j]);
            }
            if (!assignable(node.type_arguments[i], bound)) {
                report(node.span, "type argument " + describe(node.type_arguments[i]) + " does not satisfy the bound " +
                                  describe(bound) + " of " + name(generics[i]));
            }
        }
    }

    static IrType substitute(IrType type, SymbolId parameter, const IrType& argument) {
        if (type.kind == TypeKind::TypeParameter && type.symbol == parameter) {
            return argument;
        }
        for (auto& nested : type.arguments) {
            nested = substitute(std::move(nested), parameter, argument);
        }
        return type;
    }

    void check_load(const IrNode& node, const Context& context) {
        if (!check_symbol_kind(node, {SymbolKind::Local, SymbolKind::Parameter, SymbolKind::Global}, "load")) {
            return;
        }
        const auto& target = symbol(node.symbol);
        if (target.kind != SymbolKind::Global && !node.operands.empty()) {
            report(node.span, "load of " + name(node.symbol) + " has a receiver");
        }
        if (node.operands.size() > 1) {
            report(node.span, "load has more than one receiver");
        }
        if (target.kind == SymbolKind::Local && (target.owner != context.owner || !visible(node.symbol))) {
            report(node.span, "load of local " + name(node.symbol) +
                              " is not preceded by its initialization in an enclosing scope");
        }
        if (target.kind == SymbolKind::Parameter && target.owner != context.function) {
            report(node.span, "load of parameter " + name(node.symbol) + " outside its function");
        }
        if (target.kind != SymbolKind::Global && node.type != target.type) {
            report(node.span, "load has type " + describe(node.type) + ", but " + name(node.symbol) +
                              " has type " + describe(target.type));
        }
    }

    void check_element_access(const IrNode& node, const IrNode& collection, const IrNode& index) {
        if (!is_collection(collection.type)) {
            report(node.span, "indexing requires an array or list, found " + describe(collection.type));
        }
        expect_type(index, TypeKind::Integer, "collection index");
    }

    void check_integer_literal(const IrNode& node) {
        expect_operands(node, 0, "integer literal");
        expect_type(node, TypeKind::Integer, "integer literal");
        const auto value = parse_integer_literal(node.text);
        if (!value) {
            report(node.span, "integer literal '" + node.text + "' is not a decimal integer");
            return;
        }
        const auto format = specified_integer_format(node.type);
        if (format && !representable(*value, *format)) {
            report(node.span, "integer literal " + node.text + " is outside the range of " + describe(node.type));
        }
    }

    void check_unary(const IrNode& node) {
        if (!expect_operands(node, 1, "unary operation")) {
            return;
        }
        const auto& operand = node.operands.front().type;
        if (node.text == "!") {
            if (operand.kind != TypeKind::Bool || node.type.kind != TypeKind::Bool) {
                report(node.span, "'!' requires and produces bool");
            }
        } else if (node.text == "-") {
            if ((operand.kind != TypeKind::Integer && operand.kind != TypeKind::Float) || node.type != operand) {
                report(node.span, "unary '-' requires a numeric operand of its result type");
            }
        } else if (node.text == "~") {
            if (operand.kind != TypeKind::Integer || node.type != operand) {
                report(node.span, "'~' requires an integer operand of its result type");
            }
        } else {
            report(node.span, "unknown unary operator '" + node.text + "'");
        }
    }

    void check_scoped_value(const IrNode& node, Context& context) {
        expect_operands(node, 0, "scoped value");
        if (node.body.empty()) {
            report(node.span, "a scoped value needs a result expression");
            return;
        }
        scopes_.emplace_back();
        for (std::size_t i = 0; i + 1 < node.body.size(); ++i) {
            const auto& init = node.body[i];
            if (init.op != IrOp::Initialize || !check_symbol_kind(init, {SymbolKind::Local}, "scoped value binding")) {
                report(init.span, "a scoped value can only declare local bindings before its result");
            } else {
                check_statement(init, context);
            }
        }
        check_value(node.body.back(), context);
        require_assignable(node.body.back().type, node.type, node.span, "scoped value result");
        scopes_.pop_back();
    }

    void check_select_value(const IrNode& node, Context& context) {
        if (expect_operands(node, 1, "select value"))
            expect_type(node.operands.front(), TypeKind::Bool, "selection condition");
        for (const auto* region : {&node.body, &node.alternative}) {
            if (region->size() != 1) {
                report(node.span, "each selection arm needs one result expression");
                continue;
            }
            check_value(region->front(), context);
            require_assignable(region->front().type, node.type, node.span, "selection arm");
        }
    }

    void check_enum_construct(const IrNode& node) {
        if (!check_symbol_kind(node, {SymbolKind::Variant}, "enum construction")) return;
        const auto& variant = symbol(node.symbol);
        if (node.type != variant.type || !has_enum_payload(program_, variant.owner))
            report(node.span, "enum construction result must be its variant's enum with payloads");
        if (!expect_operands(node, variant.payload_types.size(), "enum construction")) return;
        const auto order = enum_field_order(node.text, variant.payload_types.size());
        if (!order) {
            report(node.span, "enum construction has an invalid payload field order");
            return;
        }
        for (std::size_t i = 0; i < node.operands.size(); ++i)
            require_assignable(node.operands[i].type, variant.payload_types[(*order)[i]],
                               node.operands[i].span, "enum payload");
    }

    void check_projection(const IrNode& node) {
        if (!expect_operands(node, 1, "projection")) return;
        const auto& source = node.operands.front().type;
        const auto* fields = &source.arguments;
        if (node.op == IrOp::Project) {
            if (source.kind != TypeKind::Tuple) {
                report(node.span, "tuple projection requires a tuple");
                return;
            }
        } else {
            if (!check_symbol_kind(node, {SymbolKind::Variant}, "enum projection")) return;
            const auto& variant = symbol(node.symbol);
            if (source != variant.type) {
                report(node.span, "enum projection requires a value of the variant's enum type");
                return;
            }
            fields = &variant.payload_types;
        }
        const auto index = aggregate_index(node.text);
        if (!index || *index >= fields->size() || node.type != (*fields)[*index])
            report(node.span, "projection index or result type does not match the aggregate field");
    }

    void check_binary(const IrNode& node) {
        if (!expect_operands(node, 2, "binary operation")) {
            return;
        }
        const auto& left = node.operands[0].type;
        const auto& right = node.operands[1].type;
        const auto& op = node.text;
        if (is_shift(op)) {
            if (left.kind != TypeKind::Integer || right.kind != TypeKind::Integer || node.type != left) {
                report(node.span, "a shift takes two integers and produces the left operand's type");
            }
            return;
        }
        if (left != right) {
            report(node.span, "binary operands have different types " + describe(left) + " and " +
                              describe(right));
            return;
        }
        const bool numeric = left.kind == TypeKind::Integer || left.kind == TypeKind::Float;
        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%" || is_bitwise_binary(op)) {
            if (((op == "%" || is_bitwise_binary(op)) && left.kind != TypeKind::Integer) ||
                (!numeric && !(op == "+" && left.kind == TypeKind::String))) {
                report(node.span, "operator '" + op + "' is not defined for " + describe(left));
            } else if (node.type != left) {
                report(node.span, "operator '" + op + "' produces " + describe(node.type) +
                                  " from operands of type " + describe(left));
            }
        } else if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=") {
            const bool equality = op == "==" || op == "!=";
            const bool scalar_enum = left.kind == TypeKind::Enum && !has_enum_payload(program_, left.symbol);
            if (!numeric && !(equality && (left.kind == TypeKind::Bool || scalar_enum))) {
                report(node.span, "operator '" + op + "' is not defined for " + describe(left));
            }
            expect_type(node, TypeKind::Bool, "comparison");
        } else {
            report(node.span, "unknown binary operator '" + op + "'");
        }
    }

    void check_short_circuit(const IrNode& node, Context& context) {
        if (node.text != "&&" && node.text != "||") {
            report(node.span, "unknown short-circuit operator '" + node.text + "'");
        }
        expect_type(node, TypeKind::Bool, "short-circuit result");
        if (expect_operands(node, 1, "short-circuit operation")) {
            expect_type(node.operands.front(), TypeKind::Bool, "short-circuit left operand");
        }
        if (node.body.size() != 1) {
            report(node.span, "short-circuit operation needs exactly one lazily evaluated operand");
            return;
        }
        check_value(node.body.front(), context);
        expect_type(node.body.front(), TypeKind::Bool, "short-circuit right operand");
    }

    void check_comparison_chain(const IrNode& node, Context& context) {
        expect_type(node, TypeKind::Bool, "comparison chain result");
        expect_operands(node, 0, "comparison chain");
        if (node.body.size() != 2 || node.body.front().op != IrOp::Initialize ||
            !check_symbol_kind(node.body.front(), {SymbolKind::Local}, "comparison chain subject")) {
            report(node.span, "a comparison chain requires a subject binding followed by a boolean expression");
            return;
        }
        if (!symbol(node.body.front().symbol).constant) {
            report(node.span, "a comparison chain subject must be constant");
        }
        scopes_.emplace_back();
        check_initialize(node.body.front(), context);
        check_value(node.body.back(), context);
        expect_type(node.body.back(), TypeKind::Bool, "comparison chain body");
        scopes_.pop_back();
    }
};

} // namespace

std::vector<VerificationIssue> verify_ir(const IrProgram& program) {
    return Verifier(program).run();
}

void require_valid_ir(const IrProgram& program, std::string_view stage) {
    auto issues = verify_ir(program);
    if (!issues.empty()) {
        auto& first = issues.front();
        auto message = "NeuraIR verification failed " + std::string(stage) + ": " + first.message;
        if (issues.size() > 1) {
            message += " (and " + std::to_string(issues.size() - 1) + " more issue(s))";
        }
        throw Diagnostic(std::move(first.span), std::move(message));
    }
}

} // namespace neura
