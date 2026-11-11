#include "verifier.hpp"

#include "aggregate.hpp"
#include "control_flow.hpp"
#include "float.hpp"
#include "host_binding.hpp"
#include "scheduler_ops.hpp"
#include "nominal.hpp"
#include "integer.hpp"
#include "reference_storage.hpp"

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
    if (is_function_value(type)) {
        std::string result = "func(";
        for (std::size_t i = 0; i + 1 < type.arguments.size(); ++i) {
            result += (i == 0 ? "" : ", ") + describe(type.arguments[i]);
        }
        return result + ") -> " + describe(type.arguments.back());
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
    case IrOp::TrapMessage:
    case IrOp::TaskScope:
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

bool prepared_constant(const IrNode& node) {
    const bool comparison = node.text == "==" || node.text == "!=" || node.text == "<" ||
        node.text == "<=" || node.text == ">" || node.text == ">=";
    switch (node.op) {
    case IrOp::Boolean:
    case IrOp::Integer:
    case IrOp::Float:
    case IrOp::String:
    case IrOp::ShortCircuit:
    case IrOp::SelectValue:
    case IrOp::TextEqual:
        break;
    case IrOp::Unary:
        if (node.text != "!" && node.text != "~" && node.type.kind != TypeKind::Float) return false;
        break;
    case IrOp::Binary:
        if (!comparison && node.type.kind != TypeKind::Float) return false;
        break;
    case IrOp::Convert:
        if (node.type.kind != TypeKind::Float) return false;
        break;
    default:
        return false;
    }
    return std::ranges::all_of(node.operands, prepared_constant) &&
        std::ranges::all_of(node.body, prepared_constant) &&
        std::ranges::all_of(node.alternative, prepared_constant);
}

class Verifier {
public:
    explicit Verifier(const IrProgram& program)
        : program_(program), initialized_(program.symbols.size(), false) {}

    std::vector<VerificationIssue> run() {
        check_symbols();
        check_bodies();
        check_static_constants();
        return std::move(issues_);
    }

private:
    struct Context {
        SymbolId owner = no_symbol;
        SymbolId function = no_symbol;
        unsigned loops = 0;
        bool scoped_value = false;
        // Enclosing TaskScope statements of this body.
        unsigned task_scopes = 0;
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
        if (is_primitive_reference(type) &&
            (!valid(type.symbol) || symbol(type.symbol).kind != SymbolKind::Class ||
             symbol(type.symbol).name != "$reference" || symbol(type.symbol).type != type ||
             !valid(symbol(type.symbol).owner) || symbol(symbol(type.symbol).owner).kind != SymbolKind::File ||
             symbol(symbol(type.symbol).owner).name != "<reference:" + type.name.substr(1) + ">")) {
            report(span, "a primitive reference must name its compiler-provided layout");
        }
        if (type.kind == TypeKind::Tuple &&
            (type.symbol != no_symbol || !type.name.empty() || type.arguments.empty() ||
             std::ranges::any_of(type.arguments, [](const IrType& element) {
                 return element.kind == TypeKind::Void || element.kind == TypeKind::Module ||
                        (element.kind == TypeKind::Callable && !is_function_value(element));
             }))) {
            report(span, "a tuple must have one or more value types and no symbol");
        }
        if (type.kind == TypeKind::Callable && (!type.arguments.empty() || type.name == "func") &&
            (!is_function_value(type) || type.name != "func" ||
             std::ranges::any_of(type.arguments.begin(), type.arguments.end() - 1,
                                 [](const IrType& part) { return part.kind == TypeKind::Void; }))) {
            report(span, "a function type requires a result and non-void parameters");
        }
        if (type.kind == TypeKind::Enum &&
            (!valid(type.symbol) || symbol(type.symbol).kind != SymbolKind::Enum ||
             type.arguments.size() != symbol(type.symbol).generics.size())) {
            report(span, "an enum type must name an enum declaration with its type arguments");
        }
        for (const auto& argument : type.arguments) {
            check_type_references(argument, span);
        }
    }

    bool assignable(const IrType& actual, const IrType& expected) const {
        return nominal_assignable(program_, actual, expected);
    }

    bool class_reference(const IrType& type, unsigned depth = 0) const {
        return (type.kind == TypeKind::Object && !is_primitive_reference(type) && valid(type.symbol) &&
                class_or_interface(symbol(type.symbol).kind)) ||
            (type.kind == TypeKind::TypeParameter && depth < 8 && valid(type.symbol) &&
             symbol(type.symbol).bound && class_reference(*symbol(type.symbol).bound, depth + 1));
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
            if (is_static_scope(entry) &&
                (!valid(entry.owner) || !class_or_interface(symbol(entry.owner).kind) ||
                 entry.type.kind != TypeKind::Module || entry.type.symbol != id || !entry.generics.empty())) {
                report(entry.span, "a static scope must be a non-generic module owned by a class or interface");
            }
            if (is_static_initializer(entry) &&
                (!valid(entry.owner) || !is_static_scope(symbol(entry.owner)) || entry.external ||
                 !entry.has_body || entry.exported || entry.type.kind != TypeKind::Void ||
                 !entry.generics.empty() || !entry.parameters.empty())) {
                report(entry.span, "a class initializer must be a private receiver-free void function");
            }
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
            if (entry.variadic != VariadicKind::None &&
                (entry.kind != SymbolKind::Function || entry.parameters.empty() ||
                 !valid(entry.parameters.back()) || symbol(entry.parameters.back()).type.kind != TypeKind::Array ||
                 !is_collection(symbol(entry.parameters.back()).type))) {
                report(entry.span, "a variadic function needs a packed array parameter");
            }
            if (entry.overload_group != no_symbol &&
                (entry.kind != SymbolKind::Function || entry.overload_group == id ||
                 !valid(entry.overload_group) || symbol(entry.overload_group).kind != SymbolKind::Function ||
                 symbol(entry.overload_group).owner != entry.owner ||
                 symbol(entry.overload_group).overload_group != no_symbol)) {
                report(entry.span, "invalid overload group reference");
            }
            if (entry.user_cast &&
                (entry.kind != SymbolKind::Function || !entry.has_body || entry.external ||
                 !entry.generics.empty() || !entry.parameters.empty() || entry.type.kind == TypeKind::Void ||
                 !valid(entry.owner) || symbol(entry.owner).kind != SymbolKind::Class)) {
                report(entry.span, "invalid user-defined cast");
            }
            if (entry.singleton_instance != no_symbol &&
                (entry.kind != SymbolKind::Class || !valid(entry.singleton_instance) ||
                 symbol(entry.singleton_instance).kind != SymbolKind::Global ||
                 symbol(entry.singleton_instance).owner != entry.owner ||
                 !symbol(entry.singleton_instance).constant ||
                 symbol(entry.singleton_instance).type != entry.type)) {
                report(entry.span, "invalid singleton instance reference");
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
            if (!entry.additional_bounds.empty() &&
                (entry.kind != SymbolKind::TypeParameter || !entry.bound)) {
                report(entry.span, "additional bounds require a bounded type parameter");
            }
            for (const auto& bound : entry.additional_bounds) {
                check_type_references(bound, entry.span);
                if (bound.kind != TypeKind::Object) report(entry.span, "a bound requires a class or interface");
            }
            if (!entry.interfaces.empty() && !class_or_interface(entry.kind)) {
                report(entry.span, "only classes and interfaces can have interface bases");
            }
            for (const auto& interface : entry.interfaces) {
                check_type_references(interface, entry.span);
                if (interface.kind != TypeKind::Object || !valid(interface.symbol) ||
                    symbol(interface.symbol).kind != SymbolKind::Interface ||
                    symbol(interface.symbol).generics.size() != interface.arguments.size()) {
                    report(entry.span, "interface base must refer to an interface");
                    continue;
                }
                const auto views = nominal_views(program_, interface);
                if (std::ranges::find(views, id, &IrType::symbol) != views.end()) {
                    report(entry.span, "cyclic interface inheritance");
                }
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
                    (payload.kind == TypeKind::Callable && !is_function_value(payload))) {
                    report(entry.span, "an enum payload needs a value type");
                }
            }
            if (!entry.library_path.empty() &&
                (entry.kind != SymbolKind::File || !entry.library_path.starts_with("std/") ||
                 !entry.library_path.ends_with(".nra") || entry.library_path.find("/../") != std::string::npos ||
                 entry.library_path.find("/./") != std::string::npos ||
                 entry.library_path.find("//") != std::string::npos)) {
                report(entry.span, "invalid standard library file identity");
            }
            if (host_callback(entry)) {
                const auto file = source_file(program_, id);
                if (entry.kind != SymbolKind::Function || entry.external || entry.overrides != no_symbol ||
                    !entry.generics.empty() || !valid(entry.owner) || symbol(entry.owner).kind != SymbolKind::Module ||
                    !valid(file) || symbol(file).library_path.empty() || entry.host_binding->version == 0 ||
                    !host_service_name(entry.host_binding->name) ||
                    (entry.type.kind != TypeKind::Void && !host_data_type(entry.type))) {
                    report(entry.span, "invalid standard library host callback");
                }
                for (auto parameter : entry.parameters) {
                    if (valid(parameter) && !host_data_type(symbol(parameter).type))
                        report(entry.span, "this parameter cannot cross the host boundary");
                }
            } else if (entry.host_binding) {
                const auto file = source_file(program_, id);
                if (entry.kind != SymbolKind::Function || !entry.external || entry.has_body ||
                    entry.overrides != no_symbol || !entry.generics.empty() ||
                    !valid(entry.owner) || symbol(entry.owner).kind != SymbolKind::Module ||
                    !valid(file) || symbol(file).library_path.empty() ||
                    entry.host_binding->version == 0 || !host_service_name(entry.host_binding->name) ||
                    !host_variants(program_, entry.type)) {
                    report(entry.span, "invalid standard library host binding");
                }
                for (auto parameter : entry.parameters) {
                    if (valid(parameter) && !host_data_type(symbol(parameter).type))
                        report(entry.span, "this parameter cannot cross the host boundary");
                }
            }
        }
    }

    void check_static_constants() {
        std::set<SymbolId> fields;
        for (const auto& constant : program_.static_constants) {
            const auto& value = constant.value;
            if (!valid(constant.field) || !fields.insert(constant.field).second ||
                symbol(constant.field).kind != SymbolKind::Global || !symbol(constant.field).constant ||
                !valid(symbol(constant.field).owner) || !is_static_scope(symbol(symbol(constant.field).owner)) ||
                !prepared_constant(value)) {
                report(value.span, "invalid prepared static constant");
                continue;
            }
            const auto& field = symbol(constant.field);
            Context context{field.owner};
            check_value(value, context);
            require_assignable(value.type, field.type, value.span, "static constant");
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
            case SymbolKind::Interface:
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
        if (node.poll && node.op != IrOp::Loop && node.op != IrOp::Call && node.op != IrOp::VirtualCall &&
            node.op != IrOp::CallClosure) {
            report(node.span, "a poll marks an operation that is neither a loop nor a call");
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
        if (node.op != IrOp::Block && node.op != IrOp::Branch && node.op != IrOp::Loop &&
            node.op != IrOp::TaskScope) {
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
        case IrOp::TaskScope:
            expect_operands(node, 0, "task scope");
            if (!node.alternative.empty()) {
                report(node.span, "a task scope has an else region");
            }
            if (context.function == no_symbol) {
                report(node.span, "a task scope appears outside a function body");
            }
            // With a symbol, the constant uint64 local that receives the
            // scope's identity for its handle.
            if (node.symbol != no_symbol &&
                (!valid(node.symbol) || symbol(node.symbol).kind != SymbolKind::Local ||
                 symbol(node.symbol).type != IrType{TypeKind::Integer, "uint64", no_symbol, {}})) {
                report(node.span, "a task scope's identity is a uint64 local");
            } else if (node.symbol != no_symbol) {
                if (initialized_[node.symbol]) report(node.span, "a task scope's identity is initialized twice");
                initialized_[node.symbol] = true;
            }
            ++context.task_scopes;
            // The identity is set on entry and visible in the body only.
            scopes_.emplace_back();
            if (valid(node.symbol)) scopes_.back().push_back(node.symbol);
            check_region(node.body, context);
            scopes_.pop_back();
            --context.task_scopes;
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
        case IrOp::TrapMessage:
            if (expect_operands(node, 1, "trap with a message")) {
                check_value(node.operands.front(), context);
                expect_type(node.operands.front(), TypeKind::String, "trap message");
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

    // A spawn starts a function value of no arguments. Specialization turns
    // that value into the allocation of its capture layout, whose static
    // type is the signature layout it derives from.
    void check_spawn(const IrNode& node, const Context& context) {
        if (node.type != IrType{TypeKind::Integer, "uint64", no_symbol, {}}) {
            report(node.span, "spawn result must be uint64");
        }
        if (context.task_scopes == 0) {
            report(node.span, "spawn appears outside a task scope of its function body");
        }
        if (!expect_operands(node, 1, "spawn")) return;
        const auto& body = node.operands.front();
        if (body.op != IrOp::MakeClosure && body.op != IrOp::Allocate) {
            report(node.span, "spawn requires the function value it starts as its operand");
            return;
        }
        if (body.op == IrOp::MakeClosure &&
            (!is_function_value(body.type) || body.type.arguments.size() != 1)) {
            report(node.span, "spawn requires a function value that takes no arguments");
        }
        if (body.op == IrOp::MakeClosure && body.type.arguments.size() == 1 &&
            body.type.arguments.front().kind == TypeKind::Void) {
            report(node.span, "a spawned function value returns a value");
        }
    }

    void check_scheduler_call(const IrNode& node) {
        if (node.symbol != no_symbol) {
            report(node.span, "a scheduler call names its operation in text only");
        }
        const IrType uint64{TypeKind::Integer, "uint64", no_symbol, {}};
        const IrType uint32{TypeKind::Integer, "uint32", no_symbol, {}};
        const auto value = [](const IrType& type) {
            return type.kind != TypeKind::Void && type.kind != TypeKind::Module;
        };
        if (node.text == select_operation || node.text == select_with_default) {
            // Arms in source order: a uint32 kind literal and its operands.
            if (!node.type_arguments.empty()) report(node.span, "a select takes no type arguments");
            if (node.type != uint64) report(node.span, "a select result must be uint64");
            std::size_t index = 0;
            std::size_t arms = 0;
            while (index < node.operands.size()) {
                const auto& kind = node.operands[index];
                if (kind.op != IrOp::Integer || kind.type != uint32 ||
                    (kind.text != "0" && kind.text != "1" && kind.text != "2" && kind.text != "3")) {
                    report(kind.span, "a select arm begins with its kind, a uint32 literal from 0 to 3");
                    return;
                }
                const std::size_t operands = kind.text == "1" ? 2 : 1;
                if (index + operands >= node.operands.size()) {
                    report(kind.span, "a select arm lacks its operands");
                    return;
                }
                const auto& target = node.operands[index + 1];
                if (kind.text == "2" ? target.type != uint64 : target.type.kind != TypeKind::Object) {
                    report(target.span, "a select arm waits on an object, or on a uint64 task for a join");
                }
                if (operands == 2 && !value(node.operands[index + 2].type)) {
                    report(node.operands[index + 2].span, "a select send arm needs a value");
                }
                index += 1 + operands;
                ++arms;
            }
            if (arms == 0) report(node.span, "a select needs at least one arm");
            return;
        }
        const auto operation = scheduler_operation(node.text);
        if (!operation) {
            report(node.span, "scheduler call names unknown operation '" + node.text + "'");
            return;
        }
        if (node.type_arguments.size() != (operation->typed ? 1U : 0U)) {
            report(node.span, "scheduler operation '" + node.text + "' has the wrong number of type arguments");
        }
        const auto matches = [&](char letter, const IrType& type) {
            switch (letter) {
            // A function value of no arguments, or after specialization the
            // object of its signature layout.
            case 'f': return type.kind == TypeKind::Object || (is_function_value(type) && type.arguments.size() == 1);
            case 'l': return type.kind == TypeKind::List && type.arguments.size() == 1 && type.arguments.front() == uint64;
            case 'a': return type.kind == TypeKind::Array && type.arguments.size() == 1;
            case 'u': return type == uint64;
            case 'i': return type == IrType{TypeKind::Integer, "int64", no_symbol, {}};
            case 'c': return type == uint32;
            case 'b': return type.kind == TypeKind::Bool;
            case 'o': return type.kind == TypeKind::Object;
            case 'v': return value(type);
            case '0': return type.kind == TypeKind::Void;
            default: return false;
            }
        };
        if (!expect_operands(node, operation->arguments.size(), "scheduler call")) return;
        for (std::size_t i = 0; i < node.operands.size(); ++i) {
            if (!matches(operation->arguments[i], node.operands[i].type)) {
                report(node.operands[i].span, "scheduler operation '" + node.text + "' argument " +
                                               std::to_string(i + 1) + " has type " + describe(node.operands[i].type));
            }
        }
        if (!matches(operation->result, node.type)) {
            report(node.span, "scheduler operation '" + node.text + "' has result type " + describe(node.type));
        }
        // The function an isolated item runs is fixed where it is written.
        if (operation->arguments.starts_with('f') && node.operands.front().op != IrOp::MakeClosure &&
            node.operands.front().op != IrOp::Allocate) {
            report(node.operands.front().span, "scheduler operation '" + node.text +
                                               "' takes a function value written at the call");
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
        if (context.scoped_value && target.kind != SymbolKind::Local)
            report(node.span, "a scoped value can initialize only local bindings");
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
            const auto& target = symbol(destination.symbol);
            if (target.kind != SymbolKind::Global || !valid(context.function) ||
                !is_static_initializer(symbol(context.function)) || target.owner != symbol(context.function).owner)
                report(destination.span, "store writes to const " + name(destination.symbol));
        }
        require_assignable(source.type, destination.type, node.span, "stored value");
    }

    void check_return(const IrNode& node, Context& context) {
        if (context.scoped_value) {
            report(node.span, "return cannot leave a scoped value");
            return;
        }
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
        case IrOp::ReferencePlace: {
            if (!expect_operands(node, 1, "reference binding") ||
                !check_symbol_kind(node, {SymbolKind::Local, SymbolKind::Parameter, SymbolKind::Global,
                                          SymbolKind::Field}, "reference binding")) break;
            const auto* place = &node.operands.front();
            while (place->op == IrOp::ScopedValue && !place->body.empty()) place = &place->body.back();
            const auto& target = symbol(node.symbol);
            if (!is_primitive_reference(node.type) || node.type.name != "&" + target.type.name ||
                target.constant || target.name.starts_with("$capture") ||
                (place->op != IrOp::Load && place->op != IrOp::Field) || place->symbol != node.symbol ||
                place->type != target.type ||
                (target.type.kind != TypeKind::Bool && target.type.kind != TypeKind::Integer &&
                 target.type.kind != TypeKind::Float && target.type.kind != TypeKind::String)) {
                report(node.span, "a primitive reference must bind to matching mutable storage");
            }
            break;
        }
        case IrOp::ValueEquals:
        case IrOp::ValueHash:
        case IrOp::ValueText: {
            const bool equality = node.op == IrOp::ValueEquals;
            if (expect_operands(node, equality ? 2 : 1, "value member") && equality)
                require_assignable(node.operands[1].type, node.operands[0].type, node.span, "equals argument");
            const auto expected = equality ? IrType{TypeKind::Bool, "bool", no_symbol, {}} :
                node.op == IrOp::ValueHash ? IrType{TypeKind::Integer, "uint32", no_symbol, {}} :
                IrType{TypeKind::String, "string", no_symbol, {}};
            if (node.type != expected) report(node.span, "value member has an invalid result type");
            break;
        }
        case IrOp::Nil:
            expect_operands(node, 0, "nil");
            if (!class_reference(node.type)) report(node.span, "nil must have a class reference type");
            break;
        case IrOp::ClassHash:
        case IrOp::ClassText:
        case IrOp::ClassCast:
        case IrOp::ClassIs: {
            if (expect_operands(node, 1, "class operation") && !class_reference(node.operands.front().type))
                report(node.span, "class operation requires a class reference");
            if (node.op == IrOp::ClassCast && !class_reference(node.type))
                report(node.span, "class cast must produce a class reference");
            if (node.op == IrOp::ClassIs) {
                expect_type(node, TypeKind::Bool, "class test");
                if (node.type_arguments.size() != 1 || !class_reference(node.type_arguments.front()))
                    report(node.span, "class test must name a class reference type");
            } else if (node.op == IrOp::ClassHash && node.type != IrType{TypeKind::Integer, "uint32", no_symbol, {}}) {
                report(node.span, "class hash must produce uint32");
            } else if (node.op == IrOp::ClassText) {
                expect_type(node, TypeKind::String, "class text");
            }
            break;
        }
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
                                     SymbolKind::Class, SymbolKind::Interface, SymbolKind::Alias, SymbolKind::Enum},
                              "reference");
            if (valid(node.symbol) && symbol(node.symbol).kind == SymbolKind::Variant &&
                (node.type.kind != TypeKind::Enum || node.type.symbol != symbol(node.symbol).owner ||
                 has_enum_payload(program_, symbol(node.symbol).owner)))
                report(node.span, "a payload enum value requires a variant constructor");
            break;
        case IrOp::Receiver:
            expect_operands(node, 0, "receiver");
            check_symbol_kind(node, {SymbolKind::Class, SymbolKind::Interface, SymbolKind::Module}, "receiver");
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
        case IrOp::RepeatCollection:
            if (!is_collection(node.type)) {
                report(node.span, "repeated collection requires an array or list result");
                break;
            }
            if (expect_operands(node, 2, "repeated collection")) {
                const IrType length{TypeKind::Integer, "uint64", no_symbol, {}};
                require_assignable(node.operands[0].type, length, node.operands[0].span, "length");
                require_assignable(node.operands[1].type, node.type.arguments.front(),
                                   node.operands[1].span, "element");
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
        case IrOp::TextLength:
        case IrOp::TextByteAt:
        case IrOp::TextSlice:
        case IrOp::TextFromUtf8:
        case IrOp::TextEqual:
            check_text_operation(node);
            break;
        case IrOp::ReinterpretFloat:
            if (expect_operands(node, 1, "float bit reinterpretation")) {
                const auto& source = node.operands.front().type;
                if (float_bits_type(source) != std::optional{node.type} &&
                    float_bits_type(node.type) != std::optional{source})
                    report(node.span, "float bit reinterpretation requires an unsigned integer of the same width");
            }
            break;
        case IrOp::ScopedValue:
            check_scoped_value(node, context, allow_void);
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
        case IrOp::MakeClosure:
            check_make_closure(node);
            break;
        case IrOp::Spawn:
            check_spawn(node, context);
            break;
        case IrOp::SchedulerCall:
            check_scheduler_call(node);
            break;
        case IrOp::CallClosure:
            check_call_closure(node);
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
            expect_type(node, TypeKind::Void, "base initialization");
            if (node.operands.empty() || node.operands.front().op != IrOp::Receiver) {
                report(node.span, "base initialization needs a receiver operand");
            }
            if (check_constructor(node, "base initialization")) {
                check_arity(node, 1);
                if (valid(node.symbol)) {
                    const auto receiver = node.operands.empty() ? no_symbol : node.operands.front().symbol;
                    if (!valid(context.function) || !valid(receiver) || symbol(receiver).kind != SymbolKind::Class ||
                        symbol(context.function).owner != receiver || symbol(context.function).name != "init" ||
                        symbol(receiver).base != symbol(node.symbol).owner) {
                        report(node.span, "base initialization requires a constructor of the direct base");
                    }
                }
            }
            break;
        case IrOp::Allocate:
            expect_type(node, TypeKind::Object, "allocation");
            if (check_constructor(node, "allocation")) {
                check_arity(node, 0);
                if (valid(node.symbol)) {
                    const auto owner = symbol(node.symbol).owner;
                    if (owner != node.type.symbol && !assignable(symbol(owner).type, node.type)) {
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

    bool check_constructor(const IrNode& node, std::string_view operation) {
        if (node.symbol == no_symbol) return true;
        if (!check_symbol_kind(node, {SymbolKind::Function}, operation)) return false;
        const auto& constructor = symbol(node.symbol);
        if (constructor.name != "init" || !constructor.has_body || constructor.external ||
            !constructor.generics.empty() || constructor.type.kind != TypeKind::Void ||
            !valid(constructor.owner) || symbol(constructor.owner).kind != SymbolKind::Class) {
            report(node.span, std::string(operation) + " must name a class constructor");
            return false;
        }
        return true;
    }

    void check_make_closure(const IrNode& node) {
        if (!is_function_value(node.type)) {
            report(node.span, "closure construction requires a function type");
            return;
        }
        if (!check_symbol_kind(node, {SymbolKind::Function}, "closure constructor")) return;
        const auto& constructor = symbol(node.symbol);
        if (constructor.name != "init" || !constructor.has_body || !valid(constructor.owner) ||
            symbol(constructor.owner).kind != SymbolKind::Class ||
            !symbol(constructor.owner).name.starts_with("$closure")) {
            report(node.span, "closure constructor must initialize a closure environment");
            return;
        }
        const auto& layout = symbol(constructor.owner);
        if (node.type_arguments.size() != layout.generics.size()) {
            report(node.span, "closure environment has the wrong number of type arguments");
            return;
        }
        const auto instantiate = [&](IrType type) {
            return substitute_type(std::move(type), layout.generics, node.type_arguments);
        };
        check_arity(node, 0);
        for (std::size_t i = 0; i < std::min(node.operands.size(), constructor.parameters.size()); ++i) {
            if (valid(constructor.parameters[i])) {
                require_assignable(node.operands[i].type, instantiate(symbol(constructor.parameters[i]).type),
                                   node.operands[i].span, "captured value");
            }
        }
        const auto method = std::ranges::find_if(program_.symbols, [&](const IrSymbol& item) {
            return item.owner == constructor.owner && item.name == "$invoke" && item.kind == SymbolKind::Function;
        });
        const auto& signature = node.type.arguments;
        if (method == program_.symbols.end() || !method->has_body || method->parameters.size() + 1 != signature.size() ||
            instantiate(method->type) != signature.back()) {
            report(node.span, "closure body does not have its declared function type");
            return;
        }
        for (std::size_t i = 0; i < method->parameters.size(); ++i) {
            if (!valid(method->parameters[i]) || instantiate(symbol(method->parameters[i]).type) != signature[i]) {
                report(node.span, "closure parameter does not have its declared type");
            }
        }
    }

    void check_call_closure(const IrNode& node) {
        if (node.symbol != no_symbol || !node.type_arguments.empty() || node.operands.empty() ||
            !is_function_value(node.operands.front().type)) {
            report(node.span, "a function-value call needs a callable operand and no type arguments");
            return;
        }
        const auto& signature = node.operands.front().type.arguments;
        if (node.operands.size() != signature.size() || node.type != signature.back()) {
            report(node.span, "function-value call does not match its signature");
            return;
        }
        for (std::size_t i = 1; i < node.operands.size(); ++i) {
            require_assignable(node.operands[i].type, signature[i - 1], node.operands[i].span, "function argument");
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
            if (!valid(generics[i])) continue;
            const auto& parameter = symbol(generics[i]);
            const auto check = [&](const IrType& declared) {
                const auto bound = substitute_type(declared, generics, node.type_arguments);
                if (!assignable(node.type_arguments[i], bound)) {
                    report(node.span, "type argument " + describe(node.type_arguments[i]) + " does not satisfy the bound " +
                                      describe(bound) + " of " + name(generics[i]));
                }
            };
            if (parameter.bound) check(*parameter.bound);
            for (const auto& bound : parameter.additional_bounds) check(bound);
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
        if (target.kind == SymbolKind::Global && !node.operands.empty() &&
            node.operands.front().type.kind != TypeKind::Module &&
            (!valid(target.owner) || !is_static_scope(symbol(target.owner)) ||
             !class_reference(node.operands.front().type))) {
            report(node.span, "only a static field can have a class reference qualifier");
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

    void check_scoped_value(const IrNode& node, Context& context, bool allow_void) {
        expect_operands(node, 0, "scoped value");
        if (node.body.empty()) {
            report(node.span, "a scoped value needs a result expression");
            return;
        }
        scopes_.emplace_back();
        auto nested = context;
        nested.loops = 0;
        nested.scoped_value = true;
        for (std::size_t i = 0; i + 1 < node.body.size(); ++i)
            check_statement(node.body[i], nested);
        check_expression(node.body.back(), nested, allow_void);
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

    void check_text_operation(const IrNode& node) {
        const IrType integer{TypeKind::Integer, "int32", no_symbol, {}};
        const IrType byte{TypeKind::Integer, "uint8", no_symbol, {}};
        const IrType boolean{TypeKind::Bool, "bool", no_symbol, {}};
        const IrType text{TypeKind::String, "string", no_symbol, {}};
        if (node.op == IrOp::TextFromUtf8) {
            if (expect_operands(node, 1, "string.from_utf8")) {
                const auto& source = node.operands.front().type;
                if (!is_collection(source) || source.arguments.front() != byte) {
                    report(node.span, "string.from_utf8 requires an array or list of bytes");
                }
            }
            if (node.type != IrType{TypeKind::Tuple, {}, no_symbol, {boolean, text}}) {
                report(node.span, "string.from_utf8 must return [bool, string]");
            }
            return;
        }
        const std::size_t arity = node.op == IrOp::TextLength ? 1 : node.op == IrOp::TextSlice ? 3 : 2;
        if (expect_operands(node, arity, "text operation")) {
            require_assignable(node.operands.front().type, text, node.span, "text receiver");
            const auto& argument_type = node.op == IrOp::TextEqual ? text : integer;
            for (std::size_t i = 1; i < arity; ++i)
                require_assignable(node.operands[i].type, argument_type, node.operands[i].span, "text argument");
        }
        const auto& result_type = node.op == IrOp::TextLength ? integer : node.op == IrOp::TextByteAt ? byte
                                : node.op == IrOp::TextSlice ? text : boolean;
        if (node.type != result_type) report(node.span, "text operation has an invalid result type");
        if (node.op == IrOp::TextEqual && node.text != "==" && node.text != "!=") {
            report(node.span, "text comparison requires == or !=");
        }
    }

    void check_enum_construct(const IrNode& node) {
        if (!check_symbol_kind(node, {SymbolKind::Variant}, "enum construction")) return;
        const auto& variant = symbol(node.symbol);
        if (node.type.kind != TypeKind::Enum || node.type.symbol != variant.owner ||
            !has_enum_payload(program_, variant.owner))
            report(node.span, "enum construction result must be its variant's enum with payloads");
        if (!expect_operands(node, variant.payload_types.size(), "enum construction")) return;
        const auto order = enum_field_order(node.text, variant.payload_types.size());
        if (!order) {
            report(node.span, "enum construction has an invalid payload field order");
            return;
        }
        for (std::size_t i = 0; i < node.operands.size(); ++i)
            require_assignable(node.operands[i].type, enum_payload_type(node.type, variant.payload_types[(*order)[i]]),
                               node.operands[i].span, "enum payload");
    }

    IrType enum_payload_type(const IrType& type, IrType field) const {
        if (type.kind == TypeKind::Enum && valid(type.symbol)) {
            const auto& generics = symbol(type.symbol).generics;
            for (std::size_t i = 0; i < std::min(generics.size(), type.arguments.size()); ++i)
                field = substitute(std::move(field), generics[i], type.arguments[i]);
        }
        return field;
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
            if (source.kind != TypeKind::Enum || source.symbol != variant.owner) {
                report(node.span, "enum projection requires a value of the variant's enum type");
                return;
            }
            fields = &variant.payload_types;
        }
        const auto index = aggregate_index(node.text);
        if (!index || *index >= fields->size() ||
            node.type != (node.op == IrOp::Project ? (*fields)[*index] : enum_payload_type(source, (*fields)[*index])))
            report(node.span, "projection index or result type does not match the aggregate field");
    }

    void check_binary(const IrNode& node) {
        if (!expect_operands(node, 2, "binary operation")) {
            return;
        }
        const auto& left = node.operands[0].type;
        const auto& right = node.operands[1].type;
        const auto& op = node.text;
        if ((op == "==" || op == "!=") && class_reference(left) && class_reference(right)) {
            expect_type(node, TypeKind::Bool, "class comparison");
            return;
        }
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
        throw Diagnostic(std::move(first.span), std::move(message)).with_code("E0899");
    }
}

} // namespace neura
