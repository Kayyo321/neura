#include "aggregate.hpp"
#include "control_flow.hpp"
#include "float.hpp"
#include "integer.hpp"
#include "ir.hpp"
#include "parser/syntax.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace neura {
namespace {

using Names = std::map<std::string, SymbolId, std::less<>>;
using Substitution = std::map<SymbolId, IrType>;
using ExprKind = Expr::Kind;
using StmtKind = Statement::Kind;
using DeclKind = Declaration::Kind;

const IrType void_type{TypeKind::Void, "void", no_symbol, {}};
const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};
const IrType string_type{TypeKind::String, "string", no_symbol, {}};
const IrType int32_type{TypeKind::Integer, "int32", no_symbol, {}};
const IrType uint64_type{TypeKind::Integer, "uint64", no_symbol, {}};
const IrType float64_type{TypeKind::Float, "float64", no_symbol, {}};

std::optional<IrType> primitive(std::string_view name) {
    if (name == "void") return void_type;
    if (name == "bool") return bool_type;
    if (name == "string") return string_type;
    // `int` and `byte` are aliases of int32 and uint8 (Types.md). Lowering
    // spells every integer type by its sized name, so the aliases are
    // interchangeable with it everywhere.
    if (name == "int") return int32_type;
    if (name == "byte") return IrType{TypeKind::Integer, "uint8", no_symbol, {}};
    if (name == "float") return IrType{TypeKind::Float, "float32", no_symbol, {}};
    constexpr std::array integers{"int8", "int16", "int32", "int64",
                                  "uint8", "uint16", "uint32", "uint64"};
    constexpr std::array floats{"float16", "float32", "float64", "bfloat16"};
    if (std::ranges::find(integers, name) != integers.end())
        return IrType{TypeKind::Integer, std::string(name), no_symbol, {}};
    if (std::ranges::find(floats, name) != floats.end())
        return IrType{TypeKind::Float, std::string(name), no_symbol, {}};
    return {};
}

bool collection(const IrType& type) {
    return type.kind == TypeKind::Array || type.kind == TypeKind::List;
}

IrType collection_type(TypeKind kind, const IrType& element) {
    return {kind, {}, no_symbol, {element}};
}

std::string describe(const IrType& type) {
    if (collection(type))
        return describe(type.arguments.front()) + (type.kind == TypeKind::Array ? "[]" : "[list]");
    if (type.arguments.empty()) return type.name;
    std::string name = type.name + "<";
    for (const auto& arg : type.arguments) {
        if (name.back() != '<') name += ", ";
        name += describe(arg);
    }
    return name + ">";
}

IrNode node(IrOp op, const Span& span, IrType type = void_type) {
    return {op, std::move(type), span, no_symbol, {}, {}, {}, {}, {}};
}

IrType substitute(IrType type, const Substitution& replacements) {
    if (type.kind == TypeKind::TypeParameter) {
        if (auto it = replacements.find(type.symbol); it != replacements.end()) return it->second;
    }
    for (auto& arg : type.arguments) arg = substitute(std::move(arg), replacements);
    return type;
}

std::vector<std::string> components(std::string_view name) {
    std::vector<std::string> parts;
    for (;;) {
        auto dot = name.find('.');
        parts.emplace_back(name.substr(0, dot));
        if (dot == std::string_view::npos) break;
        name.remove_prefix(dot + 1);
    }
    return parts;
}

struct ClosureEnvironment;

struct Context {
    SymbolId owner;
    SymbolId function = no_symbol;
    SymbolId receiver = no_symbol;
    unsigned loops = 0;
    std::vector<Names> scopes;
    ClosureEnvironment* closure = nullptr;
};

struct ClosureEnvironment {
    SymbolId layout;
    Context& enclosing;
    std::map<SymbolId, SymbolId> fields;
    std::vector<std::pair<SymbolId, IrNode>> inputs;
};

bool contains_type_parameter(const IrType& type);

SymbolId lexical_owner(const Context& context) {
    return context.closure ? lexical_owner(context.closure->enclosing) : context.owner;
}

class Lowering {
public:
    IrProgram run(const std::vector<ParsedFile>& files);

private:
    enum class Resolution { Pending, Resolving, Resolved };
    struct Entry {
        const Declaration* declaration = nullptr;
        Names names;
        Resolution resolution = Resolution::Resolved;
        std::optional<IrNode> initializer;
        const Statement* destructure = nullptr;
    };
    struct GlobalDestructure {
        std::vector<SymbolId> bindings;
        Resolution resolution = Resolution::Pending;
        std::vector<IrNode> instructions;
    };

    IrProgram program_;
    std::vector<Entry> entries_;
    std::vector<SymbolId> file_ids_;
    std::map<const Statement*, GlobalDestructure> destructures_;
    std::map<std::string, SymbolId, std::less<>> builtins_;
    std::vector<std::pair<std::vector<IrType>, SymbolId>> callable_types_;
    const std::vector<ParsedFile>* files_ = nullptr;

    IrSymbol& symbol(SymbolId id) { return program_.symbols[id]; }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    SymbolId add(SymbolKind kind, std::string name, const Span& span, SymbolId owner = no_symbol);
    SymbolId declare(const Declaration& decl, SymbolId owner, SymbolKind kind);
    void add_builtin(std::string_view name, const Span& span);
    void builtin_function(SymbolId owner, std::string name, const Span& span, IrType result,
                          const std::vector<IrType>& parameters);
    SymbolId resolve_import(const Expr& expr);
    void resolve(SymbolId id);
    void resolve_binding(SymbolId id, const Declaration& decl);
    void resolve_generics(SymbolId id, const Declaration& decl, Context& context);
    void lower_declaration(SymbolId id);
    void check_override(SymbolId id, const Declaration& decl);
    void check_constructor(SymbolId id, const Declaration& decl);

    SymbolId own_member(SymbolId owner, std::string_view name) const;
    SymbolId member(SymbolId owner, std::string_view name, bool exported) const;
    SymbolId find(std::string_view name, const Context& context);
    SymbolId qualified(const std::string& name, const Span& span, const Context& context, bool nominal_type = false);
    bool inside(SymbolId scope, SymbolId owner) const;
    IrType type(const TypeSyntax& syntax, const Context& context);
    IrType callable_type(std::vector<IrType> signature, const Span& span);
    bool is_callable(const IrType& type) const;
    SymbolId closure_layout(const IrType& type, const Span& span, SymbolId scope);
    SymbolId closure_method(SymbolId layout, const IrType& type, const Span& span,
                            const std::vector<std::string>& names);
    IrNode closure_value(const IrType& type, ClosureEnvironment& environment, const Span& span);
    IrNode lambda(const Declaration& declaration, Context& context);
    IrNode bind_function(IrNode callee, Context& context);
    IrNode closure_field(SymbolId field, const Span& span);
    IrNode capture(ClosureEnvironment& environment, SymbolId key, IrNode input, const Span& span);
    IrNode receiver(SymbolId id, const Span& span, Context& context);
    Substitution type_arguments(const std::vector<SymbolId>& params,
                                const std::vector<TypeSyntax>& args,
                                const Span& span, const Context& context);
    IrType apply_arguments(IrType type, const Substitution& arguments);
    bool assignable(const IrType& actual, const IrType& expected) const;
    void require_type(const IrType& actual, const IrType& expected, const Span& span) const;
    IrType receiver_type(const IrType& type) const;
    Substitution receiver_arguments(const IrType& type) const;

    Context context(SymbolId owner);
    IrNode expression(const Expr& expr, Context& context, const IrType* expected = nullptr);
    IrNode value(const Expr& expr, Context& context, const IrType* expected = nullptr);
    IrNode reference(SymbolId id, const Span& span, Context& context, bool implicit_receiver = true);
    IrNode select(const Expr& expr, Context& context);
    IrNode call(const Expr& expr, Context& context);
    IrNode enum_construct(SymbolId variant, const Expr& expr, Context& context);
    IrNode tuple(const Expr& expr, Context& context, const IrType* expected);
    IrNode allocate(const Expr& expr, Context& context);
    IrNode binary(const Expr& expr, Context& context, const IrType* expected);
    IrNode comparison_chain(const Expr& expr, Context& context);
    IrNode number(const Expr& expr, const IrType* expected, bool negative = false);
    void arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                   const Substitution& replacements, Context& context);
    void place(const IrNode& destination) const;
    IrNode statement(const Statement& stmt, Context& context);
    void append_statement(std::vector<IrNode>& instructions, const Statement& stmt, Context& context);
    void destructure(std::vector<IrNode>& instructions, const Statement& stmt, Context& context,
                     const std::vector<SymbolId>* globals = nullptr);
    void resolve_destructure(const Statement& stmt, SymbolId file);
    IrNode lower_match(const Statement& stmt, Context& context, bool produces_value, const IrType* expected = nullptr);
    IrNode for_loop(const Statement& stmt, Context& context);
    IrNode for_each(const Statement& stmt, Context& context);
    std::vector<IrNode> body(const std::vector<Statement>& stmts, Context& context);
    SymbolId owner_kind(SymbolId id, SymbolKind kind) const;
};

SymbolId Lowering::add(SymbolKind kind, std::string name, const Span& span, SymbolId owner) {
    auto id = program_.symbols.size();
    IrSymbol entry{};
    entry.kind = kind;
    entry.name = std::move(name);
    entry.span = span;
    entry.owner = owner;
    entry.type = void_type;
    program_.symbols.push_back(std::move(entry));
    entries_.emplace_back();
    return id;
}

SymbolId Lowering::declare(const Declaration& decl, SymbolId owner, SymbolKind kind) {
    switch (decl.kind) {
    case DeclKind::Module: kind = SymbolKind::Module; break;
    case DeclKind::Class: kind = SymbolKind::Class; break;
    case DeclKind::Function: kind = SymbolKind::Function; break;
    case DeclKind::Enum: kind = SymbolKind::Enum; break;
    case DeclKind::Binding: break;
    }
    auto id = add(kind, decl.name, decl.span, owner);
    if (!entries_[owner].names.emplace(decl.name, id).second)
        throw Diagnostic(decl.span, "duplicate declaration of '" + decl.name + "'");
    entries_[id].declaration = &decl;
    entries_[id].resolution = Resolution::Pending;
    symbol(id).constant = decl.constant;
    symbol(id).exported = decl.exported;
    symbol(id).has_body = decl.has_body;
    auto member_kind = kind == SymbolKind::Class ? SymbolKind::Field
                     : kind == SymbolKind::Enum ? SymbolKind::Variant
                                                : SymbolKind::Global;
    for (const auto& child : decl.members) declare(child, id, member_kind);
    return id;
}

void Lowering::builtin_function(SymbolId owner, std::string name, const Span& span, IrType result,
                                const std::vector<IrType>& parameters) {
    auto id = add(SymbolKind::Function, std::move(name), span, owner);
    entries_[owner].names.emplace(symbol(id).name, id);
    symbol(id).type = std::move(result);
    symbol(id).external = true;
    symbol(id).exported = true;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        auto arg = add(SymbolKind::Parameter, "arg" + std::to_string(i), span, id);
        symbol(arg).type = parameters[i];
        symbol(id).parameters.push_back(arg);
    }
}

void Lowering::add_builtin(std::string_view name, const Span& span) {
    auto id = add(SymbolKind::Module, std::string(name), span);
    builtins_.emplace(name, id);
    symbol(id).type = {TypeKind::Module, std::string(name), id, {}};
    if (name == "Test") {
        builtin_function(id, "assert", span, void_type, {bool_type});
        builtin_function(id, "assert_equal_int32", span, void_type, {int32_type, int32_type});
        return;
    }
    builtin_function(id, "println", span, void_type, {string_type});
    auto entry = add(SymbolKind::Module, "Entry", span, id);
    entries_[id].names.emplace("Entry", entry);
    symbol(entry).type = {TypeKind::Module, "Entry", entry, {}};
    symbol(entry).exported = true;
    auto code = add(SymbolKind::Enum, "ExitCode", span, entry);
    symbol(code).type = {TypeKind::Enum, "Entry.ExitCode", code, {}};
    symbol(code).exported = true;
    entries_[entry].names.emplace("ExitCode", code);
    auto ok = add(SymbolKind::Variant, "Ok", span, code);
    symbol(ok).type = symbol(code).type;
    symbol(ok).exported = true;
    entries_[code].names.emplace("Ok", ok);
    builtin_function(entry, "main", span, symbol(code).type,
                     {collection_type(TypeKind::Array, string_type)});
    symbol(own_member(entry, "main")).external = false;
}

SymbolId Lowering::resolve_import(const Expr& expr) {
    if (expr.text == "SysIo" || expr.text == "Test") {
        if (!builtins_.contains(expr.text)) add_builtin(expr.text, expr.span);
        return builtins_.at(expr.text);
    }
    auto source = std::ranges::find(*files_, expr.span.file, &ParsedFile::file);
    if (source == files_->end()) throw Diagnostic(expr.span, "cannot locate importing file");
    auto import = std::ranges::find(source->imports, expr.text, &ResolvedImport::name);
    if (import == source->imports.end())
        throw Diagnostic(expr.span, "cannot resolve import '" + expr.text + "'");
    auto target_file = std::ranges::find(*files_, import->file, &ParsedFile::file);
    if (target_file == files_->end())
        throw Diagnostic(expr.span, "cannot load source file for import '" + expr.text + "'");
    auto index = static_cast<std::size_t>(target_file - files_->begin());
    auto name = expr.text.substr(expr.text.find_last_of('.') + 1);
    auto result = own_member(file_ids_[index], name);
    if (result == no_symbol || symbol(result).kind != SymbolKind::Module)
        throw Diagnostic(expr.span, "imported file must declare module '" + name + "'");
    resolve(result);
    return result;
}

SymbolId Lowering::own_member(SymbolId owner, std::string_view name) const {
    auto it = entries_[owner].names.find(name);
    return it == entries_[owner].names.end() ? no_symbol : it->second;
}

SymbolId Lowering::member(SymbolId owner, std::string_view name, bool exported) const {
    for (auto scope = owner; scope != no_symbol; scope = symbol(scope).base) {
        auto id = own_member(scope, name);
        if (id != no_symbol && (!exported || symbol(id).exported)) return id;
        exported = true;
    }
    return no_symbol;
}

SymbolId Lowering::find(std::string_view name, const Context& context) {
    for (auto it = context.scopes.rbegin(); it != context.scopes.rend(); ++it) {
        if (auto local = it->find(name); local != it->end()) return local->second;
    }
    if (context.closure) return find(name, context.closure->enclosing);
    for (auto scope = context.owner; scope != no_symbol; scope = symbol(scope).owner) {
        if (auto id = member(scope, name, false); id != no_symbol) return id;
        for (auto base = symbol(scope).base; base != no_symbol; base = symbol(base).base) {
            if (symbol(base).name == name) return base;
        }
    }
    return no_symbol;
}

bool Lowering::inside(SymbolId scope, SymbolId owner) const {
    for (; scope != no_symbol; scope = symbol(scope).owner) {
        if (scope == owner) return true;
    }
    return false;
}

SymbolId Lowering::qualified(const std::string& name, const Span& span, const Context& context, bool nominal_type) {
    const auto parts = components(name);
    auto id = find(parts.front(), context);
    if (id == no_symbol) throw Diagnostic(span, "unknown name '" + parts.front() + "'");
    for (std::size_t i = 1; i < parts.size(); ++i) {
        resolve(id);
        auto kind = symbol(id).kind;
        if (kind == SymbolKind::Import) {
            id = symbol(id).type.symbol;
        } else if (kind != SymbolKind::Module && kind != SymbolKind::Class &&
                   kind != SymbolKind::Enum) {
            throw Diagnostic(span, "'" + parts[i - 1] + "' is not a namespace");
        }
        id = member(id, parts[i], !inside(lexical_owner(context), id));
        if (id == no_symbol) throw Diagnostic(span, "unknown or inaccessible name '" + name + "'");
    }
    if (!(nominal_type && symbol(id).kind == SymbolKind::Enum &&
          entries_[id].resolution == Resolution::Resolving)) resolve(id);
    return id;
}

IrType Lowering::apply_arguments(IrType type, const Substitution& arguments) {
    return substitute(std::move(type), arguments);
}

Substitution Lowering::type_arguments(const std::vector<SymbolId>& params,
                                     const std::vector<TypeSyntax>& args,
                                     const Span& span, const Context& context) {
    if (params.size() != args.size())
        throw Diagnostic(span, "expected " + std::to_string(params.size()) + " type argument(s)");
    Substitution result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto actual = type(args[i], context);
        if (actual.kind == TypeKind::Void) throw Diagnostic(args[i].span, "void is not a type argument");
        if (symbol(params[i]).bound) {
            auto bound = substitute(*symbol(params[i]).bound, result);
            if (!assignable(actual, bound))
                throw Diagnostic(args[i].span, "type argument " + describe(actual) + " does not satisfy the bound " +
                                               describe(bound) + " of type parameter '" + symbol(params[i]).name + "'");
        }
        result.emplace(params[i], std::move(actual));
    }
    return result;
}

IrType Lowering::type(const TypeSyntax& syntax, const Context& context) {
    IrType result;
    if (syntax.name == "[tuple]") {
        result = {TypeKind::Tuple, {}, no_symbol, {}};
        for (const auto& part : syntax.arguments) {
            auto element = type(part, context);
            if (element.kind == TypeKind::Void)
                throw Diagnostic(part.span, "a tuple element cannot have type void");
            result.arguments.push_back(std::move(element));
        }
    } else if (syntax.name == "[function]") {
        std::vector<IrType> signature;
        for (const auto& part : syntax.arguments) signature.push_back(type(part, context));
        result = callable_type(std::move(signature), syntax.span);
    } else if (auto builtin = primitive(syntax.name)) {
        if (!syntax.arguments.empty()) throw Diagnostic(syntax.span, "builtin type takes no arguments");
        result = *builtin;
    } else {
        auto id = qualified(syntax.name, syntax.span, context, true);
        auto kind = symbol(id).kind;
        if (kind != SymbolKind::Class && kind != SymbolKind::Alias &&
            kind != SymbolKind::Enum && kind != SymbolKind::TypeParameter) {
            throw Diagnostic(syntax.span, "'" + syntax.name + "' is not a type");
        }
        const auto params = symbol(id).generics;
        auto replacements = type_arguments(params, syntax.arguments, syntax.span, context);
        result = apply_arguments(symbol(id).type, replacements);
    }
    for (auto suffix : syntax.collections) {
        if (result.kind == TypeKind::Void)
            throw Diagnostic(syntax.span, "void is not a collection element type");
        result = collection_type(suffix == CollectionKind::Array ? TypeKind::Array : TypeKind::List,
                                 result);
    }
    if (context.closure && contains_type_parameter(result)) {
        throw Diagnostic(syntax.span, "a closure body must use concrete types");
    }
    return result;
}

bool Lowering::assignable(const IrType& actual, const IrType& expected) const {
    if (actual == expected) return true;
    if (actual.kind == TypeKind::TypeParameter && symbol(actual.symbol).bound)
        return assignable(*symbol(actual.symbol).bound, expected);
    if (actual.kind != TypeKind::Object || expected.kind != TypeKind::Object) return false;
    for (auto base = symbol(actual.symbol).base; base != no_symbol; base = symbol(base).base) {
        if (base == expected.symbol && expected.arguments.empty()) return true;
    }
    return false;
}

void Lowering::require_type(const IrType& actual, const IrType& expected, const Span& span) const {
    if (!assignable(actual, expected))
        throw Diagnostic(span, "expected " + describe(expected) + ", got " + describe(actual));
}

IrType Lowering::receiver_type(const IrType& type) const {
    if (type.kind == TypeKind::TypeParameter && symbol(type.symbol).bound)
        return receiver_type(*symbol(type.symbol).bound);
    return type;
}

Substitution Lowering::receiver_arguments(const IrType& type) const {
    auto nominal = receiver_type(type);
    Substitution result;
    if (nominal.kind == TypeKind::Object) {
        const auto& parameters = symbol(nominal.symbol).generics;
        for (std::size_t i = 0; i < parameters.size(); ++i)
            result.emplace(parameters[i], nominal.arguments[i]);
    }
    return result;
}

SymbolId Lowering::owner_kind(SymbolId id, SymbolKind kind) const {
    for (; id != no_symbol; id = symbol(id).owner) {
        if (symbol(id).kind == kind) return id;
    }
    return no_symbol;
}

Context Lowering::context(SymbolId owner) {
    Context result{owner, no_symbol, no_symbol, 0, {}};
    for (auto id = owner; id != no_symbol; id = symbol(id).owner) {
        if (symbol(id).kind == SymbolKind::Class || symbol(id).kind == SymbolKind::Module) {
            result.receiver = id;
            break;
        }
    }
    return result;
}

bool contains_type_parameter(const IrType& type) {
    return type.kind == TypeKind::TypeParameter ||
        std::ranges::any_of(type.arguments, contains_type_parameter);
}

IrType Lowering::callable_type(std::vector<IrType> signature, const Span& span) {
    if (std::ranges::any_of(signature, contains_type_parameter)) {
        throw Diagnostic(span, "a function value's signature must have concrete types");
    }
    if (const auto found = std::ranges::find(callable_types_, signature, &decltype(callable_types_)::value_type::first);
        found != callable_types_.end()) {
        return symbol(found->second).type;
    }

    std::string name = "func(";
    for (std::size_t i = 0; i + 1 < signature.size(); ++i) {
        if (signature[i].kind == TypeKind::Void) {
            throw Diagnostic(span, "a function parameter cannot have type void");
        }
        if (i != 0) name += ", ";
        name += describe(signature[i]);
    }
    name += ") -> " + describe(signature.back());
    const auto layout = add(SymbolKind::Class, "$signature" + std::to_string(callable_types_.size()), span);
    symbol(layout).type = {TypeKind::Object, std::move(name), layout, {}};
    const auto method = add(SymbolKind::Function, "$invoke", span, layout);
    entries_[layout].names.emplace("$invoke", method);
    symbol(method).type = signature.back();
    for (std::size_t i = 0; i + 1 < signature.size(); ++i) {
        const auto parameter = add(SymbolKind::Parameter, "$arg" + std::to_string(i), span, method);
        symbol(parameter).type = signature[i];
        symbol(method).parameters.push_back(parameter);
    }
    callable_types_.emplace_back(std::move(signature), layout);
    return symbol(layout).type;
}

bool Lowering::is_callable(const IrType& type) const {
    return type.kind == TypeKind::Object && type.symbol < program_.symbols.size() &&
        symbol(type.symbol).name.starts_with("$signature");
}

SymbolId Lowering::closure_layout(const IrType& type, const Span& span, SymbolId scope) {
    const auto file = owner_kind(scope, SymbolKind::File);
    const auto layout = add(SymbolKind::Class, "$closure" + std::to_string(program_.symbols.size()), span, file);
    symbol(layout).type = {TypeKind::Object, symbol(layout).name, layout, {}};
    symbol(layout).base = type.symbol;
    return layout;
}

SymbolId Lowering::closure_method(SymbolId layout, const IrType& type, const Span& span,
                                   const std::vector<std::string>& names) {
    const auto base = own_member(type.symbol, "$invoke");
    const auto parameters = symbol(base).parameters;
    const auto method = add(SymbolKind::Function, "$invoke", span, layout);
    entries_[layout].names.emplace("$invoke", method);
    symbol(method).overrides = base;
    symbol(method).type = symbol(base).type;
    symbol(method).has_body = true;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        const auto parameter = add(SymbolKind::Parameter, names[i], span, method);
        if (!entries_[method].names.emplace(names[i], parameter).second) {
            throw Diagnostic(span, "duplicate parameter '" + names[i] + "'");
        }
        symbol(parameter).type = symbol(parameters[i]).type;
        symbol(method).parameters.push_back(parameter);
    }
    return method;
}

IrNode Lowering::closure_field(SymbolId field, const Span& span) {
    const auto layout = symbol(field).owner;
    auto input = node(IrOp::Receiver, span, symbol(layout).type);
    input.symbol = layout;
    auto result = node(IrOp::Field, span, symbol(field).type);
    result.symbol = field;
    result.operands.push_back(std::move(input));
    return result;
}

IrNode Lowering::capture(ClosureEnvironment& environment, SymbolId key, IrNode input, const Span& span) {
    if (const auto found = environment.fields.find(key); found != environment.fields.end()) {
        return closure_field(found->second, span);
    }
    if (contains_type_parameter(input.type)) {
        throw Diagnostic(span, "a closure can only capture values of concrete types");
    }
    const auto field = add(SymbolKind::Field, "$capture" + std::to_string(environment.inputs.size()),
                           span, environment.layout);
    symbol(field).type = input.type;
    environment.fields.emplace(key, field);
    environment.inputs.emplace_back(field, std::move(input));
    return closure_field(field, span);
}

IrNode Lowering::receiver(SymbolId id, const Span& span, Context& context) {
    if (context.closure && symbol(id).kind == SymbolKind::Module) {
        // Modules have a static receiver and no runtime handle.
        auto result = node(IrOp::Reference, span, symbol(id).type);
        result.symbol = id;
        return result;
    }
    if (context.closure) {
        return capture(*context.closure, id, receiver(id, span, context.closure->enclosing), span);
    }
    auto result = node(IrOp::Receiver, span, symbol(id).type);
    result.symbol = id;
    return result;
}

IrNode Lowering::closure_value(const IrType& type, ClosureEnvironment& environment, const Span& span) {
    const auto layout = environment.layout;
    const auto constructor = add(SymbolKind::Function, "init", span, layout);
    entries_[layout].names.emplace("init", constructor);
    symbol(constructor).has_body = true;
    std::vector<IrNode> instructions;
    auto super = node(IrOp::BaseInitialize, span);
    auto base = node(IrOp::Receiver, span, type);
    base.symbol = layout;
    super.operands.push_back(std::move(base));
    auto initialize_base = node(IrOp::Discard, span);
    initialize_base.operands.push_back(std::move(super));
    instructions.push_back(std::move(initialize_base));

    auto result = node(IrOp::Allocate, span, type);
    result.symbol = constructor;
    for (auto& [field, input] : environment.inputs) {
        const auto parameter = add(SymbolKind::Parameter, symbol(field).name, span, constructor);
        symbol(parameter).type = symbol(field).type;
        symbol(constructor).parameters.push_back(parameter);
        auto load = node(IrOp::Load, span, symbol(parameter).type);
        load.symbol = parameter;
        auto store = node(IrOp::Store, span);
        store.operands = {closure_field(field, span), std::move(load)};
        instructions.push_back(std::move(store));
        result.operands.push_back(std::move(input));
    }
    program_.bodies.push_back({constructor, std::move(instructions)});
    return result;
}

IrNode Lowering::lambda(const Declaration& declaration, Context& context) {
    const auto& span = declaration.span;
    std::vector<IrType> signature;
    std::vector<std::string> names;
    for (const auto& parameter : declaration.parameters) {
        signature.push_back(type(parameter.type, context));
        names.push_back(parameter.name);
    }
    signature.push_back(type(*declaration.type, context));
    const auto callable = callable_type(std::move(signature), span);
    ClosureEnvironment environment{closure_layout(callable, span, context.owner), context, {}, {}};
    const auto method = closure_method(environment.layout, callable, span, names);
    Context inner{method, method, context.receiver, 0, {entries_[method].names}, &environment};
    auto instructions = body(declaration.body, inner);
    if (symbol(method).type.kind != TypeKind::Void && !always_returns(instructions)) {
        throw Diagnostic(span, "function may exit without returning " + describe(symbol(method).type));
    }
    program_.bodies.push_back({method, std::move(instructions)});
    return closure_value(callable, environment, span);
}

IrNode Lowering::bind_function(IrNode callee, Context& context) {
    const auto target = callee.symbol;
    const auto& span = callee.span;
    if (callee.type.name == "super.init") {
        throw Diagnostic(span, "super.init must be called directly in a constructor");
    }
    if (!symbol(target).generics.empty()) {
        throw Diagnostic(span, "a generic function must be called with explicit type arguments");
    }
    if (callee.operands.empty() && !symbol(target).external && !symbol(target).has_body) {
        throw Diagnostic(span, "function requires an implementation");
    }
    auto replacements = callee.operands.empty() ? Substitution{} : receiver_arguments(callee.operands.front().type);
    std::vector<IrType> signature;
    std::vector<std::string> names;
    for (const auto parameter : symbol(target).parameters) {
        signature.push_back(substitute(symbol(parameter).type, replacements));
        names.push_back(symbol(parameter).name);
    }
    signature.push_back(substitute(symbol(target).type, replacements));
    const auto callable = callable_type(std::move(signature), span);
    ClosureEnvironment environment{closure_layout(callable, span, context.owner), context, {}, {}};
    const auto method = closure_method(environment.layout, callable, span, names);
    auto invocation = node(IrOp::Call, span, symbol(method).type);
    invocation.symbol = target;
    if (symbol(target).external) {
        invocation.op = IrOp::ExternalCall;
    } else if (!callee.operands.empty()) {
        invocation.op = IrOp::VirtualCall;
        auto input = std::move(callee.operands.front());
        if (input.type.kind == TypeKind::Module) {
            if (input.op == IrOp::Receiver) {
                input.op = IrOp::Reference;
            }
            invocation.operands.push_back(std::move(input));
        } else {
            invocation.operands.push_back(capture(environment, target, std::move(input), span));
        }
    }
    for (const auto parameter : symbol(method).parameters) {
        auto argument = node(IrOp::Load, span, symbol(parameter).type);
        argument.symbol = parameter;
        invocation.operands.push_back(std::move(argument));
    }
    auto result = node(invocation.type.kind == TypeKind::Void ? IrOp::Discard : IrOp::Return, span);
    result.operands.push_back(std::move(invocation));
    program_.bodies.push_back({method, {std::move(result)}});
    return closure_value(callable, environment, span);
}

void Lowering::resolve_generics(SymbolId id, const Declaration& decl, Context& context) {
    for (const auto& parameter : decl.generics) {
        auto generic = add(SymbolKind::TypeParameter, parameter.name, parameter.span, id);
        if (!entries_[id].names.emplace(parameter.name, generic).second)
            throw Diagnostic(parameter.span, "duplicate type parameter '" + parameter.name + "'");
        symbol(generic).type = {TypeKind::TypeParameter, parameter.name, generic, {}};
        symbol(id).generics.push_back(generic);
        if (parameter.bound) {
            auto bound = type(*parameter.bound, context);
            if (bound.kind != TypeKind::Object)
                throw Diagnostic(parameter.span, "a type parameter bound must be a class");
            symbol(generic).bound = std::move(bound);
        }
    }
}

void Lowering::resolve_binding(SymbolId id, const Declaration& decl) {
    auto ctx = context(symbol(id).owner);
    if (decl.initializer && decl.initializer->kind == ExprKind::Import) {
        if (decl.type) throw Diagnostic(decl.span, "import aliases cannot have a type annotation");
        auto target = resolve_import(*decl.initializer);
        symbol(id).kind = SymbolKind::Import;
        symbol(id).type = symbol(target).type;
        return;
    }
    if (!decl.type && decl.initializer) {
        const auto& init = *decl.initializer;
        if (init.kind == ExprKind::Name && primitive(init.text)) {
            auto builtin = *primitive(init.text);
            symbol(id).kind = SymbolKind::Alias;
            symbol(id).type = std::move(builtin);
            return;
        }
        auto initializer = expression(init, ctx);
        if (initializer.op == IrOp::Reference && initializer.symbol != no_symbol) {
            auto kind = symbol(initializer.symbol).kind;
            if (kind == SymbolKind::Alias || kind == SymbolKind::Class || kind == SymbolKind::Enum) {
                if (!symbol(initializer.symbol).generics.empty())
                    throw Diagnostic(init.span, "generic type overlays are not supported");
                symbol(id).kind = SymbolKind::Alias;
                symbol(id).type = symbol(initializer.symbol).type;
                return;
            }
        }
        if (initializer.type.kind == TypeKind::Callable && initializer.symbol != no_symbol &&
            symbol(initializer.symbol).kind == SymbolKind::Function) {
            initializer = bind_function(std::move(initializer), ctx);
        }
        if (initializer.type.kind == TypeKind::Void || initializer.type.kind == TypeKind::Module ||
            initializer.type.kind == TypeKind::Callable || initializer.op == IrOp::Reference) {
            if (initializer.symbol == no_symbol ||
                symbol(initializer.symbol).kind != SymbolKind::Variant)
                throw Diagnostic(init.span, "expected a value initializer");
        }
        symbol(id).type = initializer.type;
        entries_[id].initializer = std::move(initializer);
        return;
    }
    if (!decl.type) throw Diagnostic(decl.span, "declaration needs a type or initializer");
    auto declared = type(*decl.type, ctx);
    if (declared.kind == TypeKind::Void) throw Diagnostic(decl.span, "a binding cannot have type void");
    symbol(id).type = declared;
    if (decl.initializer) {
        entries_[id].initializer = value(*decl.initializer, ctx, &declared);
    } else if (symbol(id).kind != SymbolKind::Field || decl.constant) {
        throw Diagnostic(decl.span, "binding requires an initializer");
    }
}

void Lowering::resolve(SymbolId id) {
    auto state = entries_[id].resolution;
    if (state == Resolution::Resolved) return;
    if (state == Resolution::Resolving)
        throw Diagnostic(symbol(id).span, "cyclic declaration of '" + symbol(id).name + "'");
    if (const auto* destructure = entries_[id].destructure) {
        resolve_destructure(*destructure, symbol(id).owner);
        return;
    }
    entries_[id].resolution = Resolution::Resolving;
    const auto& decl = *entries_[id].declaration;
    auto ctx = context(id);
    if (decl.kind == DeclKind::Class || decl.kind == DeclKind::Module) {
        auto kind = decl.kind == DeclKind::Class ? TypeKind::Object : TypeKind::Module;
        symbol(id).type = {kind, decl.name, id, {}};
        resolve_generics(id, decl, ctx);
        for (auto generic : symbol(id).generics)
            symbol(id).type.arguments.push_back(symbol(generic).type);
        if (decl.base) {
            auto base = qualified(decl.base->name, decl.base->span, context(symbol(id).owner));
            if (symbol(base).kind == SymbolKind::Import) base = symbol(base).type.symbol;
            if (symbol(base).kind == SymbolKind::Alias && symbol(base).type.kind == TypeKind::Object)
                base = symbol(base).type.symbol;
            if (symbol(base).kind != symbol(id).kind)
                throw Diagnostic(decl.base->span, "base must have the same declaration kind");
            if (symbol(base).kind == SymbolKind::Module && symbol(base).constant)
                throw Diagnostic(decl.base->span, "cannot inherit a constant module");
            if (!symbol(base).generics.empty() || !decl.base->arguments.empty() ||
                !decl.base->collections.empty())
                throw Diagnostic(decl.base->span, "generic inheritance is not supported");
            symbol(id).base = base;
        }
    } else if (decl.kind == DeclKind::Enum) {
        // Variants are values of the enum, visible wherever the enum is.
        symbol(id).type = {TypeKind::Enum, decl.name, id, {}};
        for (const auto& member : decl.members) {
            auto variant = own_member(id, member.name);
            symbol(variant).type = symbol(id).type;
            symbol(variant).exported = true;
            symbol(variant).constant = true;
            entries_[variant].resolution = Resolution::Resolved;
        }
        for (const auto& member : decl.members) {
            const auto variant = own_member(id, member.name);
            std::set<std::string> names;
            for (const auto& field : member.parameters) {
                if (member.named_payload && !names.insert(field.name).second)
                    throw Diagnostic(field.span, "duplicate enum payload field '" + field.name + "'");
                auto payload = type(field.type, ctx);
                if (payload.kind == TypeKind::Void)
                    throw Diagnostic(field.span, "an enum payload cannot have type void");
                symbol(variant).payload_types.push_back(std::move(payload));
            }
        }
    } else if (decl.kind == DeclKind::Function) {
        resolve_generics(id, decl, ctx);
        symbol(id).type = type(*decl.type, ctx);
        for (const auto& parameter : decl.parameters) {
            auto parameter_type = type(parameter.type, ctx);
            if (parameter_type.kind == TypeKind::Void)
                throw Diagnostic(parameter.span, "a parameter cannot have type void");
            auto arg = add(SymbolKind::Parameter, parameter.name, parameter.span, id);
            if (!entries_[id].names.emplace(parameter.name, arg).second)
                throw Diagnostic(parameter.span, "duplicate parameter '" + parameter.name + "'");
            symbol(arg).type = std::move(parameter_type);
            symbol(id).parameters.push_back(arg);
        }
        if (!decl.has_body && symbol(symbol(id).owner).kind != SymbolKind::Module &&
            symbol(symbol(id).owner).kind != SymbolKind::Class)
            throw Diagnostic(decl.span, "only class or module members may omit a function body");
    } else {
        resolve_binding(id, decl);
    }
    entries_[id].resolution = Resolution::Resolved;
}

void Lowering::check_override(SymbolId id, const Declaration& decl) {
    auto owner = symbol(id).owner;
    auto base = symbol(owner).base;
    auto inherited = base == no_symbol ? no_symbol : member(base, decl.name, true);
    if (inherited != no_symbol && symbol(owner).kind == SymbolKind::Class &&
        (decl.name == "init" || decl.name == "deinit")) inherited = no_symbol;
    if (decl.override_member && inherited == no_symbol)
        throw Diagnostic(decl.span, "no inherited member named '" + decl.name + "'");
    if (inherited == no_symbol) return;
    if (!decl.override_member)
        throw Diagnostic(decl.span, "inherited member '" + decl.name + "' requires .override");
    if (!decl.exported) throw Diagnostic(decl.span, "an override must be exported");
    if (symbol(owner).kind == SymbolKind::Class && symbol(id).kind != SymbolKind::Function)
        throw Diagnostic(decl.span, "class field overrides are not supported");
    resolve(inherited);
    bool match = symbol(id).kind == symbol(inherited).kind &&
                 symbol(id).type == symbol(inherited).type &&
                 symbol(id).generics.empty() && symbol(inherited).generics.empty();
    const auto& ours = symbol(id).parameters;
    const auto& theirs = symbol(inherited).parameters;
    match = match && std::ranges::equal(ours, theirs, [this](auto a, auto b) {
        return symbol(a).type == symbol(b).type;
    });
    if (!match) throw Diagnostic(decl.span, "override does not match inherited member '" + decl.name + "'");
    symbol(id).overrides = inherited;
}

IrNode Lowering::reference(SymbolId id, const Span& span, Context& context, bool implicit_receiver) {
    resolve(id);
    const auto& target = symbol(id);
    if (context.closure && target.owner != context.owner &&
        (target.kind == SymbolKind::Local || target.kind == SymbolKind::Parameter)) {
        return capture(*context.closure, id, reference(id, span, context.closure->enclosing), span);
    }
    if (context.function == no_symbol && target.owner == context.owner &&
        (target.kind == SymbolKind::Global || target.kind == SymbolKind::Field) &&
        target.span.file == span.file && target.span.offset > span.offset) {
        throw Diagnostic(span, "'" + target.name + "' is used before its declaration");
    }
    auto result = node(IrOp::Reference, span, target.type);
    result.symbol = id;
    if (target.kind == SymbolKind::Function)
        result.type = {TypeKind::Callable, target.name, id, {}};
    if (target.kind == SymbolKind::Field || target.kind == SymbolKind::Global ||
        target.kind == SymbolKind::Local || target.kind == SymbolKind::Parameter) {
        result.op = target.kind == SymbolKind::Field ? IrOp::Field : IrOp::Load;
    }
    auto owner = target.owner;
    if (implicit_receiver && owner != no_symbol && context.receiver != no_symbol) {
        bool own_receiver = false;
        for (auto current = context.receiver; current != no_symbol; current = symbol(current).base)
            own_receiver |= current == owner;
        if (own_receiver && (target.kind == SymbolKind::Field || target.kind == SymbolKind::Function ||
                             target.kind == SymbolKind::Global)) {
            result.operands.push_back(receiver(context.receiver, span, context));
        }
    }
    return result;
}

IrNode Lowering::select(const Expr& expr, Context& context) {
    const auto& operand = expr.operands.front();
    if (operand.kind == ExprKind::Name && operand.text == "super") {
        auto receiver = context.receiver;
        if (receiver == no_symbol || context.function == no_symbol || context.closure ||
            symbol(receiver).kind != SymbolKind::Class || symbol(receiver).base == no_symbol ||
            symbol(context.function).name != "init" || expr.text != "init")
            throw Diagnostic(expr.span, "super.init is only available in a derived constructor");
        auto base = symbol(receiver).base;
        auto input = node(IrOp::Receiver, operand.span, symbol(base).type);
        input.symbol = receiver;
        auto target = own_member(base, "init");
        if (target != no_symbol) resolve(target);
        auto result = node(IrOp::Reference, expr.span, {TypeKind::Callable, "super.init", target, {}});
        result.symbol = target;
        result.operands.push_back(std::move(input));
        return result;
    }
    auto receiver = expression(operand, context);
    auto nominal = receiver_type(receiver.type);
    if (collection(nominal)) {
        if (expr.text == "length") {
            auto result = node(IrOp::Length, expr.span, int32_type);
            result.operands.push_back(std::move(receiver));
            return result;
        }
        if (nominal.kind == TypeKind::List && (expr.text == "append" || expr.text == "remove_at")) {
            auto result = node(IrOp::Reference, expr.span, {TypeKind::Callable, expr.text, no_symbol, {}});
            result.operands.push_back(std::move(receiver));
            return result;
        }
        throw Diagnostic(expr.span, "type " + describe(nominal) + " has no member '" + expr.text + "'");
    }
    if (nominal.kind != TypeKind::Object && nominal.kind != TypeKind::Module &&
        nominal.kind != TypeKind::Enum) {
        throw Diagnostic(expr.span, "type " + describe(nominal) + " has no members");
    }
    if (nominal.kind == TypeKind::Enum &&
        (receiver.op != IrOp::Reference || receiver.symbol == no_symbol ||
         (symbol(receiver.symbol).kind != SymbolKind::Enum && symbol(receiver.symbol).kind != SymbolKind::Alias))) {
        throw Diagnostic(expr.span, "qualify a variant with its enum type");
    }
    bool type_reference = receiver.op == IrOp::Reference && receiver.symbol != no_symbol &&
        (symbol(receiver.symbol).kind == SymbolKind::Class ||
         symbol(receiver.symbol).kind == SymbolKind::Alias);
    if (nominal.kind == TypeKind::Object && (expr.text == "init" || expr.text == "deinit"))
        throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy");
    auto target = member(nominal.symbol, expr.text, !inside(lexical_owner(context), nominal.symbol));
    if (target == no_symbol)
        throw Diagnostic(expr.span, "unknown or inaccessible member '" + expr.text + "' of " +
                                    describe(nominal));
    auto result = reference(target, expr.span, context, false);
    const auto kind = symbol(target).kind;
    if (kind == SymbolKind::Field || kind == SymbolKind::Function || kind == SymbolKind::Global) {
        if (type_reference)
            throw Diagnostic(expr.span, "member '" + expr.text + "' requires an instance");
        result.type = substitute(std::move(result.type), receiver_arguments(nominal));
        result.operands.clear();
        result.operands.push_back(std::move(receiver));
    }
    return result;
}

void Lowering::arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                         const Substitution& replacements, Context& context) {
    auto params = target == no_symbol ? std::vector<SymbolId>{} : symbol(target).parameters;
    if (expr.operands.size() - first != params.size())
        throw Diagnostic(expr.span, "expected " + std::to_string(params.size()) + " argument(s)");
    for (std::size_t i = 0; i < params.size(); ++i) {
        auto expected = substitute(symbol(params[i]).type, replacements);
        call.operands.push_back(value(expr.operands[i + first], context, &expected));
    }
}

IrNode Lowering::enum_construct(SymbolId variant, const Expr& expr, Context& context) {
    const auto* payload = entries_[variant].declaration;
    const bool named = expr.kind == ExprKind::NamedVariant;
    const auto fields = symbol(variant).payload_types;
    if (!payload || fields.empty() || !expr.type_arguments.empty() || named != payload->named_payload)
        throw Diagnostic(expr.span, "use the variant's declared payload constructor");
    if (fields.size() != expr.operands.size() - 1)
        throw Diagnostic(expr.span, "expected " + std::to_string(fields.size()) + " enum payload field(s)");
    auto result = node(IrOp::EnumConstruct, expr.span, symbol(variant).type);
    result.symbol = variant;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        auto index = i;
        if (named) {
            const auto& name = expr.field_names[i];
            const auto field = std::ranges::find(payload->parameters, name, &Parameter::name);
            if (field == payload->parameters.end() || !seen.insert(name).second)
                throw Diagnostic(expr.span, "unknown or repeated enum payload field '" + name + "'");
            index = static_cast<std::size_t>(field - payload->parameters.begin());
            if (i != 0) result.text += ',';
            result.text += std::to_string(index);
        }
        result.operands.push_back(value(expr.operands[i + 1], context, &fields[index]));
    }
    return result;
}

IrNode Lowering::tuple(const Expr& expr, Context& context, const IrType* expected) {
    if (expr.operands.empty() || (expected && expr.operands.size() != expected->arguments.size()))
        throw Diagnostic(expr.span, "tuple literal has the wrong number of elements");
    auto result = node(IrOp::Tuple, expr.span, expected ? *expected : IrType{TypeKind::Tuple, {}, no_symbol, {}});
    for (std::size_t i = 0; i < expr.operands.size(); ++i) {
        auto element = value(expr.operands[i], context, expected ? &expected->arguments[i] : nullptr);
        if (!expected) result.type.arguments.push_back(element.type);
        result.operands.push_back(std::move(element));
    }
    return result;
}

IrNode Lowering::call(const Expr& expr, Context& context) {
    auto callee = expression(expr.operands.front(), context);
    if (callee.op == IrOp::Reference && callee.symbol != no_symbol &&
        symbol(callee.symbol).kind == SymbolKind::Variant) {
        return enum_construct(callee.symbol, expr, context);
    }
    if (is_callable(callee.type)) {
        const auto target = own_member(callee.type.symbol, "$invoke");
        if (!expr.type_arguments.empty()) throw Diagnostic(expr.span, "function values take no type arguments");
        auto result = node(IrOp::VirtualCall, expr.span, symbol(target).type);
        result.symbol = target;
        result.operands.push_back(std::move(callee));
        arguments(result, expr, 1, target, {}, context);
        return result;
    }
    if (callee.type.kind != TypeKind::Callable)
        throw Diagnostic(expr.operands.front().span, "expression is not callable");
    auto target = callee.symbol;
    auto result = node(IrOp::Call, expr.span);
    result.symbol = target;
    if (callee.type.name == "super.init") {
        const auto& first = entries_[context.function].declaration->body.front();
        if (expr.span.offset != first.expressions.front().span.offset) {
            throw Diagnostic(expr.span, "super.init must be the first statement of the constructor");
        }
        if (!expr.type_arguments.empty())
            throw Diagnostic(expr.span, "super.init takes no type arguments");
        result.op = IrOp::BaseInitialize;
        result.operands = std::move(callee.operands);
        arguments(result, expr, 1, target, {}, context);
        return result;
    }
    if (target == no_symbol) {
        if (!expr.type_arguments.empty() || expr.operands.size() != 2)
            throw Diagnostic(expr.span, "expected one argument and no type arguments");
        auto receiver = std::move(callee.operands.front());
        if (callee.type.name == "append") {
            result.op = IrOp::Append;
            auto element = value(expr.operands[1], context, &receiver.type.arguments.front());
            result.operands = {std::move(receiver), std::move(element)};
        } else {
            result.op = IrOp::RemoveAt;
            auto index = value(expr.operands[1], context);
            if (index.type.kind != TypeKind::Integer)
                throw Diagnostic(index.span, "collection index must be an integer");
            result.operands = {std::move(receiver), std::move(index)};
        }
        return result;
    }
    if (symbol(target).kind != SymbolKind::Function)
        throw Diagnostic(expr.span, "expression is not callable");
    auto replacements = callee.operands.empty() ? Substitution{}
        : receiver_arguments(callee.operands.front().type);
    auto generics = symbol(target).generics;
    auto instantiated = type_arguments(generics, expr.type_arguments, expr.span, context);
    for (auto generic : generics) result.type_arguments.push_back(instantiated.at(generic));
    replacements.merge(std::move(instantiated));
    result.type = substitute(symbol(target).type, replacements);
    if (symbol(target).external) {
        result.op = IrOp::ExternalCall;
    } else if (!callee.operands.empty()) {
        result.op = IrOp::VirtualCall;
    } else if (!symbol(target).has_body) {
        throw Diagnostic(expr.span, "function requires an implementation");
    }
    if (result.op != IrOp::ExternalCall) result.operands = std::move(callee.operands);
    arguments(result, expr, 1, target, replacements, context);
    return result;
}

IrNode Lowering::allocate(const Expr& expr, Context& context) {
    auto object = type(*expr.type, context);
    if (object.kind != TypeKind::Object)
        throw Diagnostic(expr.span, "new requires a class type");
    const auto id = object.symbol;
    Names effective;
    for (auto scope = id; scope != no_symbol; scope = symbol(scope).base) {
        for (const auto& [name, member_id] : entries_[scope].names) {
            if (name != "init" && name != "deinit") effective.emplace(name, member_id);
        }
    }
    for (const auto& [name, member_id] : effective) {
        if (symbol(member_id).kind == SymbolKind::Function && !symbol(member_id).has_body)
            throw Diagnostic(expr.span, "cannot instantiate abstract class " + describe(object) +
                                        " with unimplemented method '" + name + "'");
    }
    auto target = own_member(id, "init");
    if (target != no_symbol) {
        resolve(target);
        if (!symbol(target).exported && !inside(lexical_owner(context), id))
            throw Diagnostic(expr.span, "constructor is not exported");
    } else {
        for (auto base = symbol(id).base; base != no_symbol; base = symbol(base).base) {
            auto inherited = own_member(base, "init");
            if (inherited == no_symbol) continue;
            resolve(inherited);
            if (!symbol(inherited).parameters.empty())
                throw Diagnostic(expr.span, "class needs a constructor to initialize its base");
            break;
        }
    }
    auto result = node(IrOp::Allocate, expr.span, object);
    result.symbol = target;
    arguments(result, expr, 0, target, receiver_arguments(object), context);
    return result;
}

IrNode Lowering::number(const Expr& expr, const IrType* expected, bool negative) {
    const bool floating = expr.kind == ExprKind::Float;
    auto kind = floating ? TypeKind::Float : TypeKind::Integer;
    auto result = node(floating ? IrOp::Float : IrOp::Integer, expr.span,
                       expected && expected->kind == kind ? *expected
                                                         : (floating ? float64_type : int32_type));
    result.text = negative ? "-" + expr.text : expr.text;
    if (floating) {
        // Literals convert directly from decimal to the target format, so a
        // literal that does not fit is a compile-time error (Numbers.md).
        const auto format = float_format(result.type);
        const auto parsed = parse_float_literal(result.text, *format);
        if (parsed.error == FloatLiteralError::Overflow)
            throw Diagnostic(expr.span, "float literal is outside the range of " + result.type.name);
        if (parsed.error == FloatLiteralError::Underflow)
            throw Diagnostic(expr.span, "nonzero float literal rounds to zero in " + result.type.name);
        if (parsed.error)
            throw Diagnostic(expr.span, "malformed float literal");
        return result;
    }
    const auto& name = result.type.name;
    const bool signed_integer = name.starts_with("int");
    unsigned bits = 32;
    if (name == "byte") {
        bits = 8;
    } else if (name != "int") {
        auto digits = std::string_view(name).substr(signed_integer ? 3 : 4);
        std::from_chars(digits.data(), digits.data() + digits.size(), bits);
    }
    auto width = bits - (signed_integer ? 1U : 0U);
    auto limit = width == 64 ? std::numeric_limits<std::uint64_t>::max()
                             : (std::uint64_t{1} << width) - 1;
    if (negative && signed_integer) ++limit;
    std::uint64_t magnitude = 0;
    auto [end, error] = std::from_chars(expr.text.data(), expr.text.data() + expr.text.size(), magnitude);
    if (error != std::errc{} || end != expr.text.data() + expr.text.size() ||
        magnitude > limit || (negative && !signed_integer))
        throw Diagnostic(expr.span, "integer literal is outside the range of " + name);
    return result;
}

bool numeric_literal(const Expr& expr) {
    if (expr.kind == ExprKind::Integer || expr.kind == ExprKind::Float) return true;
    return expr.kind == ExprKind::Unary && expr.text == "-" &&
           (expr.operands.front().kind == ExprKind::Integer ||
            expr.operands.front().kind == ExprKind::Float);
}

bool comparable(const IrProgram& program, const IrType& type, std::string_view op) {
    const bool equality = op == "==" || op == "!=";
    return type.kind == TypeKind::Integer || type.kind == TypeKind::Float ||
           (equality && (type.kind == TypeKind::Bool ||
                         (type.kind == TypeKind::Enum && !has_enum_payload(program, type.symbol))));
}

IrNode Lowering::binary(const Expr& expr, Context& context, const IrType* expected) {
    const auto& op = expr.text;
    if (op == "&&" || op == "||") {
        auto result = node(IrOp::ShortCircuit, expr.span, bool_type);
        result.text = op;
        result.operands.push_back(value(expr.operands[0], context, &bool_type));
        result.body.push_back(value(expr.operands[1], context, &bool_type));
        return result;
    }
    const bool equality = op == "==" || op == "!=";
    const bool comparison = equality || op == "<" || op == "<=" || op == ">" || op == ">=";
    const auto& lhs = expr.operands[0];
    const auto& rhs = expr.operands[1];
    IrNode left = node(IrOp::Binary, lhs.span);
    IrNode right = node(IrOp::Binary, rhs.span);
    if (is_shift(op)) {
        left = value(lhs, context, expected);
        right = value(rhs, context);
        if (right.type.kind != TypeKind::Integer)
            throw Diagnostic(rhs.span, "a shift count must be an integer");
    } else if (numeric_literal(lhs) && !numeric_literal(rhs)) {
        right = value(rhs, context, comparison ? nullptr : expected);
        left = value(lhs, context, &right.type);
    } else {
        left = value(lhs, context, comparison ? nullptr : expected);
        right = value(rhs, context, &left.type);
    }
    const bool valid = comparison ? comparable(program_, left.type, op)
        : op == "%" || is_bitwise_binary(op) ? left.type.kind == TypeKind::Integer
        : left.type.kind == TypeKind::Integer || left.type.kind == TypeKind::Float ||
          (left.type.kind == TypeKind::String && op == "+");
    if (!valid)
        throw Diagnostic(expr.span, "operator '" + op + "' is not defined for " + describe(left.type));
    auto result = node(IrOp::Binary, expr.span, comparison ? bool_type : left.type);
    result.text = op;
    result.operands.push_back(std::move(left));
    result.operands.push_back(std::move(right));
    return result;
}

IrNode Lowering::comparison_chain(const Expr& expr, Context& context) {
    const auto& subject_expr = expr.operands.front();
    const auto& first_expr = expr.operands[1];
    std::optional<IrNode> first;
    if (numeric_literal(subject_expr) && !numeric_literal(first_expr)) {
        first = value(first_expr, context);
    }
    auto subject = value(subject_expr, context, first ? &first->type : nullptr);
    const auto subject_type = subject.type;
    if (!comparable(program_, subject_type, expr.text)) {
        throw Diagnostic(subject_expr.span, "operator '" + expr.text + "' is not defined for " + describe(subject_type));
    }

    auto owner = context.owner;
    if (symbol(owner).kind == SymbolKind::Global || symbol(owner).kind == SymbolKind::Field) {
        owner = symbol(owner).owner;
    }
    const auto local = add(SymbolKind::Local, "$comparison", subject_expr.span, owner);
    symbol(local).type = subject_type;
    symbol(local).constant = true;
    auto init = node(IrOp::Initialize, subject_expr.span);
    init.symbol = local;
    init.operands.push_back(std::move(subject));

    auto compare = [&](std::size_t index) {
        const auto& input = expr.operands[index];
        auto result = node(IrOp::Binary, input.span, bool_type);
        result.text = expr.text;
        auto load = node(IrOp::Load, subject_expr.span, subject_type);
        load.symbol = local;
        result.operands.push_back(std::move(load));
        result.operands.push_back(index == 1 && first ? std::move(*first) : value(input, context, &subject_type));
        return result;
    };

    auto comparisons = compare(1);
    for (std::size_t i = 2; i < expr.operands.size(); ++i) {
        auto combined = node(IrOp::ShortCircuit, expr.span, bool_type);
        combined.text = expr.chain_conjunction ? "&&" : "||";
        combined.operands.push_back(std::move(comparisons));
        combined.body.push_back(compare(i));
        comparisons = std::move(combined);
    }
    auto result = node(IrOp::ComparisonChain, expr.span, bool_type);
    result.body.push_back(std::move(init));
    result.body.push_back(std::move(comparisons));
    return result;
}

IrNode Lowering::expression(const Expr& expr, Context& context, const IrType* expected) {
    switch (expr.kind) {
    case ExprKind::Lambda:
        return lambda(*expr.lambda, context);
    case ExprKind::Name: {
        auto id = find(expr.text, context);
        if (id == no_symbol) throw Diagnostic(expr.span, "unknown name '" + expr.text + "'");
        if (symbol(id).kind == SymbolKind::Function &&
            owner_kind(symbol(id).owner, SymbolKind::Class) != no_symbol &&
            (expr.text == "init" || expr.text == "deinit")) {
            throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy");
        }
        return reference(id, expr.span, context);
    }
    case ExprKind::Integer:
    case ExprKind::Float:
        return number(expr, expected);
    case ExprKind::String: {
        auto result = node(IrOp::String, expr.span, string_type);
        auto text = std::string_view(expr.text).substr(1, expr.text.size() - 2);
        for (std::size_t i = 0; i < text.size(); ++i) {
            char ch = text[i];
            if (ch == '\\') {
                ch = text[++i];
                if (ch == 'n') ch = '\n';
                else if (ch == 'r') ch = '\r';
                else if (ch == 't') ch = '\t';
            }
            result.text += ch;
        }
        return result;
    }
    case ExprKind::Boolean: {
        auto result = node(IrOp::Boolean, expr.span, bool_type);
        result.text = expr.text;
        return result;
    }
    case ExprKind::Unary: {
        const auto& child = expr.operands.front();
        if (expr.text == "-" && (child.kind == ExprKind::Integer || child.kind == ExprKind::Float)) {
            auto literal = number(child, expected, true);
            literal.span = expr.span;
            return literal;
        }
        bool logical = expr.text == "!";
        auto input = value(child, context, logical ? &bool_type : expected);
        if (expr.text == "~" && input.type.kind != TypeKind::Integer)
            throw Diagnostic(expr.span, "unary '~' requires an integer operand");
        if (expr.text == "-" && input.type.kind != TypeKind::Integer && input.type.kind != TypeKind::Float)
            throw Diagnostic(expr.span, "unary minus requires a numeric operand");
        auto result = node(IrOp::Unary, expr.span, input.type);
        result.text = expr.text;
        result.operands.push_back(std::move(input));
        return result;
    }
    case ExprKind::Binary:
        return binary(expr, context, expected);
    case ExprKind::ComparisonChain:
        return comparison_chain(expr, context);
    case ExprKind::Member:
        return select(expr, context);
    case ExprKind::Call:
        return call(expr, context);
    case ExprKind::Index: {
        auto receiver = value(expr.operands[0], context);
        if (receiver.type.kind == TypeKind::Tuple) {
            const auto& index = expr.operands[1];
            const auto field = index.kind == ExprKind::Integer ? aggregate_index(index.text) : std::nullopt;
            if (!field || *field >= receiver.type.arguments.size())
                throw Diagnostic(index.span, "tuple indexing requires a nonnegative literal in range");
            auto result = node(IrOp::Project, expr.span, receiver.type.arguments[*field]);
            result.text = std::to_string(*field);
            result.operands.push_back(std::move(receiver));
            return result;
        }
        if (!collection(receiver.type))
            throw Diagnostic(expr.span, "indexing requires an array or list");
        auto index = value(expr.operands[1], context);
        if (index.type.kind != TypeKind::Integer)
            throw Diagnostic(index.span, "collection index must be an integer");
        auto result = node(IrOp::Index, expr.span, receiver.type.arguments.front());
        result.operands.push_back(std::move(receiver));
        result.operands.push_back(std::move(index));
        return result;
    }
    case ExprKind::Collection: {
        if (expected && expected->kind == TypeKind::Tuple) return tuple(expr, context, expected);
        auto result = node(IrOp::Collection, expr.span);
        if (expected && collection(*expected)) {
            result.type = *expected;
        } else {
            if (expr.operands.empty())
                throw Diagnostic(expr.span, "empty collection literal needs an expected type");
            auto first = value(expr.operands.front(), context);
            result.type = collection_type(TypeKind::Array, first.type);
            result.operands.push_back(std::move(first));
        }
        const auto& element = result.type.arguments.front();
        for (std::size_t i = result.operands.size(); i < expr.operands.size(); ++i)
            result.operands.push_back(value(expr.operands[i], context, &element));
        return result;
    }
    case ExprKind::New:
        return allocate(expr, context);
    case ExprKind::NamedVariant: {
        const auto target = expression(expr.operands.front(), context);
        if (target.op != IrOp::Reference || target.symbol == no_symbol ||
            symbol(target.symbol).kind != SymbolKind::Variant) {
            throw Diagnostic(expr.span, "expected a named enum variant");
        }
        return enum_construct(target.symbol, expr, context);
    }
    case ExprKind::Match:
        return lower_match(*expr.match, context, true, expected);
    case ExprKind::Cast: {
        auto target = type(*expr.type, context);
        auto source = value(expr.operands.front(), context);
        const auto numeric = [](const IrType& t) {
            return t.kind == TypeKind::Integer || t.kind == TypeKind::Float;
        };
        const bool allowed = numeric(target) &&
            (numeric(source.type) || (source.type.kind == TypeKind::Enum && target.kind == TypeKind::Integer &&
                                      !has_enum_payload(program_, source.type.symbol)));
        if (!allowed)
            throw Diagnostic(expr.span, "cannot convert " + describe(source.type) + " to " + describe(target));
        auto result = node(IrOp::Convert, expr.span, std::move(target));
        result.operands.push_back(std::move(source));
        return result;
    }
    case ExprKind::Import:
        throw Diagnostic(expr.span, "import must initialize a module-level alias");
    }
    throw Diagnostic(expr.span, "unsupported expression");
}

IrNode Lowering::value(const Expr& expr, Context& context, const IrType* expected) {
    auto result = expression(expr, context, expected);
    if (result.op == IrOp::Reference && result.symbol != no_symbol &&
        symbol(result.symbol).kind == SymbolKind::Variant) {
        const auto& variant = symbol(result.symbol);
        if (!variant.payload_types.empty())
            throw Diagnostic(expr.span, "enum variant '" + variant.name + "' requires a payload");
        if (has_enum_payload(program_, variant.owner)) result.op = IrOp::EnumConstruct;
    }
    if (result.type.kind == TypeKind::Callable && result.symbol != no_symbol &&
        symbol(result.symbol).kind == SymbolKind::Function) {
        result = bind_function(std::move(result), context);
    }
    if (result.type.kind == TypeKind::Void || result.type.kind == TypeKind::Module ||
        result.type.kind == TypeKind::Callable ||
        (result.op == IrOp::Reference &&
         (result.symbol == no_symbol || symbol(result.symbol).kind != SymbolKind::Variant))) {
        throw Diagnostic(expr.span, "expected a value");
    }
    if (expected) require_type(result.type, *expected, expr.span);
    return result;
}

void Lowering::place(const IrNode& destination) const {
    if (destination.op == IrOp::Index) return;
    if (destination.op != IrOp::Load && destination.op != IrOp::Field)
        throw Diagnostic(destination.span, "assignment requires a mutable binding, field, or element");
    if (symbol(destination.symbol).name.starts_with("$capture"))
        throw Diagnostic(destination.span, "a captured binding cannot be reassigned");
    if (symbol(destination.symbol).constant)
        throw Diagnostic(destination.span, "cannot assign to const '" + symbol(destination.symbol).name + "'");
}

IrNode Lowering::statement(const Statement& stmt, Context& context) {
    switch (stmt.kind) {
    case StmtKind::Binding: {
        const auto& declaration = *stmt.declaration;
        if (declaration.kind != DeclKind::Binding || declaration.exported ||
            declaration.override_member) {
            throw Diagnostic(declaration.span, "expected a local value binding");
        }
        if (context.scopes.back().contains(declaration.name)) {
            throw Diagnostic(declaration.span, "duplicate local binding '" + declaration.name + "'");
        }
        if (!declaration.initializer) {
            throw Diagnostic(declaration.span, "a local binding requires an initializer");
        }

        std::optional<IrType> declared;
        if (declaration.type) {
            declared = type(*declaration.type, context);
            if (declared->kind == TypeKind::Void) {
                throw Diagnostic(declaration.type->span, "a binding cannot have type void");
            }
        }
        auto initializer = value(*declaration.initializer, context, declared ? &*declared : nullptr);
        auto id = add(SymbolKind::Local, declaration.name, declaration.span, context.owner);
        symbol(id).constant = declaration.constant;
        symbol(id).type = declared.value_or(initializer.type);
        context.scopes.back().emplace(declaration.name, id);

        auto result = node(IrOp::Initialize, stmt.span);
        result.symbol = id;
        result.operands.push_back(std::move(initializer));
        return result;
    }
    case StmtKind::Expression: {
        auto result = node(IrOp::Discard, stmt.span);
        // Expression statements can call functions that return void.
        auto operand = expression(stmt.expressions.front(), context);
        if (operand.op == IrOp::Reference &&
            (operand.symbol == no_symbol || symbol(operand.symbol).kind != SymbolKind::Variant)) {
            throw Diagnostic(operand.span, "expected an executable expression");
        }
        if (operand.op == IrOp::Reference && has_enum_payload(program_, operand.type.symbol)) {
            if (!symbol(operand.symbol).payload_types.empty())
                throw Diagnostic(operand.span, "enum variant requires a payload");
            operand.op = IrOp::EnumConstruct;
        }
        result.operands.push_back(std::move(operand));
        return result;
    }
    case StmtKind::Assignment: {
        auto destination = expression(stmt.expressions[0], context);
        place(destination);
        auto source = value(stmt.expressions[1], context, &destination.type);
        auto result = node(IrOp::Store, stmt.span);
        result.operands.push_back(std::move(destination));
        result.operands.push_back(std::move(source));
        return result;
    }
    case StmtKind::Block: {
        auto result = node(IrOp::Block, stmt.span);
        result.body = body(stmt.body, context);
        return result;
    }
    case StmtKind::If: {
        auto result = node(IrOp::Branch, stmt.span);
        result.operands.push_back(value(stmt.expressions.front(), context, &bool_type));
        result.body = body(stmt.body, context);
        result.alternative = body(stmt.alternative, context);
        return result;
    }
    case StmtKind::Match:
        return lower_match(stmt, context, false);
    case StmtKind::Destructure:
        throw Diagnostic(stmt.span, "destructuring requires a statement list");
    case StmtKind::While: {
        auto result = node(IrOp::Loop, stmt.span);
        result.operands.push_back(value(stmt.expressions.front(), context, &bool_type));
        ++context.loops;
        result.body = body(stmt.body, context);
        --context.loops;
        return result;
    }
    case StmtKind::For:
        return for_loop(stmt, context);
    case StmtKind::ForEach:
        return for_each(stmt, context);
    case StmtKind::Return: {
        if (context.function == no_symbol) {
            throw Diagnostic(stmt.span, "return is only allowed inside a function");
        }
        const auto expected = symbol(context.function).type;
        auto result = node(IrOp::Return, stmt.span);
        if (stmt.expressions.empty()) {
            if (expected.kind != TypeKind::Void) {
                throw Diagnostic(stmt.span, "return requires a value of type " + describe(expected));
            }
        } else {
            if (expected.kind == TypeKind::Void) {
                throw Diagnostic(stmt.span, "a void function cannot return a value");
            }
            result.operands.push_back(value(stmt.expressions.front(), context, &expected));
        }
        return result;
    }
    case StmtKind::Break:
    case StmtKind::Continue:
        if (context.loops == 0) {
            throw Diagnostic(stmt.span, "loop control is only allowed inside a loop");
        }
        return node(stmt.kind == StmtKind::Break ? IrOp::Break : IrOp::Continue, stmt.span);
    case StmtKind::Destroy: {
        auto object = value(stmt.expressions.front(), context);
        if (object.type.kind != TypeKind::Object || is_callable(object.type)) {
            throw Diagnostic(object.span, "destroy requires an object");
        }
        auto result = node(IrOp::Destroy, stmt.span);
        result.symbol = object.type.symbol;
        result.operands.push_back(std::move(object));
        return result;
    }
    }
    throw std::logic_error("invalid statement kind");
}

IrNode Lowering::for_loop(const Statement& stmt, Context& context) {
    // for (init; condition; step) body lowers to a block that holds the
    // initializer and a loop whose latch is the step. The initializer's
    // binding is scoped to the loop; `continue` runs the step and then tests
    // the condition (CoreLanguage.md, Loops).
    context.scopes.emplace_back();
    auto result = node(IrOp::Block, stmt.span);
    for (const auto& init : stmt.initializer) {
        if (init.kind != StmtKind::Binding && init.kind != StmtKind::Assignment &&
            init.kind != StmtKind::Expression)
            throw Diagnostic(init.span, "a for loop initializer must be a binding, an assignment, or an expression");
        result.body.push_back(statement(init, context));
    }
    auto loop = node(IrOp::Loop, stmt.span);
    if (stmt.expressions.empty()) {
        auto always = node(IrOp::Boolean, stmt.span, bool_type);
        always.text = "true";
        loop.operands.push_back(std::move(always));
    } else {
        loop.operands.push_back(value(stmt.expressions.front(), context, &bool_type));
    }
    ++context.loops;
    loop.body = body(stmt.body, context);
    --context.loops;
    for (const auto& step : stmt.step) {
        if (step.kind != StmtKind::Assignment && step.kind != StmtKind::Expression)
            throw Diagnostic(step.span, "a for loop step must be an assignment or an expression");
        loop.alternative.push_back(statement(step, context));
    }
    context.scopes.pop_back();
    result.body.push_back(std::move(loop));
    return result;
}

IrNode Lowering::for_each(const Statement& stmt, Context& context) {
    // The collection is evaluated once. A list's structural version is
    // captured and checked before every advance, including the one that ends
    // the loop, so an append or removal through any alias fails at the loop
    // (CoreLanguage.md, Loops). `continue` re-enters the loop head, which
    // checks and advances; `break` and `return` leave without advancing.
    const auto& declaration = *stmt.declaration;
    const auto& source = stmt.expressions.front();
    auto result = node(IrOp::Block, stmt.span);
    auto items = value(source, context);
    if (!collection(items.type))
        throw Diagnostic(source.span, "for-each requires an array or list, got " + describe(items.type));
    const auto element = items.type.arguments.front();
    const bool list = items.type.kind == TypeKind::List;
    auto hidden = [&](std::string name, IrType hidden_type, IrNode initializer, bool constant) {
        auto id = add(SymbolKind::Local, std::move(name), stmt.span, context.owner);
        symbol(id).type = std::move(hidden_type);
        symbol(id).constant = constant;
        auto init = node(IrOp::Initialize, stmt.span);
        init.symbol = id;
        init.operands.push_back(std::move(initializer));
        result.body.push_back(std::move(init));
        return id;
    };
    auto load = [&](SymbolId id) {
        auto loaded = node(IrOp::Load, stmt.span, symbol(id).type);
        loaded.symbol = id;
        return loaded;
    };
    auto version_of = [&](SymbolId id) {
        auto version = node(IrOp::CollectionVersion, stmt.span, uint64_type);
        version.operands.push_back(load(id));
        return version;
    };
    auto binary_node = [&](std::string op, IrType type, IrNode left, IrNode right) {
        auto combined = node(IrOp::Binary, stmt.span, std::move(type));
        combined.text = std::move(op);
        combined.operands.push_back(std::move(left));
        combined.operands.push_back(std::move(right));
        return combined;
    };
    auto integer = [&](std::string text) {
        auto literal = node(IrOp::Integer, stmt.span, int32_type);
        literal.text = std::move(text);
        return literal;
    };

    const auto items_type = items.type;
    const auto items_id = hidden("$items", items_type, std::move(items), true);
    const auto version_id = list ? hidden("$version", uint64_type, version_of(items_id), true) : no_symbol;
    const auto index_id = hidden("$index", int32_type, integer("0"), false);

    auto loop = node(IrOp::Loop, stmt.span);
    auto always = node(IrOp::Boolean, stmt.span, bool_type);
    always.text = "true";
    loop.operands.push_back(std::move(always));
    if (list) {
        auto check = node(IrOp::Branch, stmt.span);
        check.operands.push_back(binary_node("!=", bool_type, version_of(items_id), load(version_id)));
        auto failure = node(IrOp::Fail, stmt.span);
        failure.text = "iteration";
        check.body.push_back(std::move(failure));
        loop.body.push_back(std::move(check));
    }
    {
        auto length = node(IrOp::Length, stmt.span, int32_type);
        length.operands.push_back(load(items_id));
        auto done = node(IrOp::Branch, stmt.span);
        done.operands.push_back(binary_node(">=", bool_type, load(index_id), std::move(length)));
        done.body.push_back(node(IrOp::Break, stmt.span));
        loop.body.push_back(std::move(done));
    }

    context.scopes.emplace_back();
    IrType declared = element;
    if (declaration.type) {
        declared = type(*declaration.type, context);
        require_type(element, declared, declaration.type->span);
    }
    auto access = node(IrOp::Index, stmt.span, element);
    access.operands.push_back(load(items_id));
    access.operands.push_back(load(index_id));
    auto element_id = add(SymbolKind::Local, declaration.name, declaration.span, context.owner);
    symbol(element_id).type = declared;
    symbol(element_id).constant = declaration.constant;
    context.scopes.back().emplace(declaration.name, element_id);
    auto bind = node(IrOp::Initialize, stmt.span);
    bind.symbol = element_id;
    bind.operands.push_back(std::move(access));
    loop.body.push_back(std::move(bind));
    auto advance = node(IrOp::Store, stmt.span);
    advance.operands.push_back(load(index_id));
    advance.operands.push_back(binary_node("+", int32_type, load(index_id), integer("1")));
    loop.body.push_back(std::move(advance));
    ++context.loops;
    for (const auto& item : stmt.body) {
        append_statement(loop.body, item, context);
    }
    --context.loops;
    context.scopes.pop_back();
    result.body.push_back(std::move(loop));
    return result;
}

void Lowering::append_statement(std::vector<IrNode>& instructions, const Statement& stmt, Context& context) {
    if (stmt.kind == StmtKind::Destructure) {
        destructure(instructions, stmt, context);
    } else {
        instructions.push_back(statement(stmt, context));
    }
}

void Lowering::destructure(std::vector<IrNode>& instructions, const Statement& stmt, Context& context,
                           const std::vector<SymbolId>* globals) {
    const auto& initializer = stmt.expressions.front();
    auto source = initializer.kind == ExprKind::Collection
        ? tuple(initializer, context, nullptr) : value(initializer, context);
    const auto tuple_type = source.type;
    if (tuple_type.kind != TypeKind::Tuple || tuple_type.arguments.size() != stmt.bindings.size())
        throw Diagnostic(stmt.span, "destructuring requires a tuple with one element per binding");
    const auto temporary = add(SymbolKind::Local, "$tuple", stmt.span, context.owner);
    symbol(temporary).type = tuple_type;
    symbol(temporary).constant = true;
    auto save = node(IrOp::Initialize, stmt.span);
    save.symbol = temporary;
    save.operands.push_back(std::move(source));
    instructions.push_back(std::move(save));
    for (std::size_t index = 0; index < stmt.bindings.size(); ++index) {
        const auto& binding = stmt.bindings[index];
        if (binding.text == "_") continue;
        SymbolId id;
        if (globals) {
            id = (*globals)[index];
        } else {
            auto& names = context.scopes.back();
            if (names.contains(binding.text))
                throw Diagnostic(binding.span, "duplicate local binding '" + binding.text + "'");
            id = add(SymbolKind::Local, binding.text, binding.span, context.owner);
            names.emplace(binding.text, id);
        }
        symbol(id).type = tuple_type.arguments[index];
        symbol(id).constant = stmt.constant_bindings;
        auto load = node(IrOp::Load, binding.span, tuple_type);
        load.symbol = temporary;
        auto element = node(IrOp::Project, binding.span, symbol(id).type);
        element.text = std::to_string(index);
        element.operands.push_back(std::move(load));
        auto init = node(IrOp::Initialize, binding.span);
        init.symbol = id;
        init.operands.push_back(std::move(element));
        instructions.push_back(std::move(init));
    }
}

void Lowering::resolve_destructure(const Statement& stmt, SymbolId file) {
    auto& group = destructures_.at(&stmt);
    if (group.resolution == Resolution::Resolved) return;
    if (group.resolution == Resolution::Resolving)
        throw Diagnostic(stmt.span, "cyclic destructuring declaration");
    group.resolution = Resolution::Resolving;
    for (const auto id : group.bindings) {
        if (id != no_symbol) entries_[id].resolution = Resolution::Resolving;
    }
    auto scope = context(file);
    destructure(group.instructions, stmt, scope, &group.bindings);
    for (const auto id : group.bindings) {
        if (id != no_symbol) entries_[id].resolution = Resolution::Resolved;
    }
    group.resolution = Resolution::Resolved;
}

IrNode Lowering::lower_match(const Statement& stmt, Context& context, bool produces_value, const IrType* expected) {
    auto subject = value(stmt.expressions.front(), context);
    const auto enum_type = subject.type;
    if (enum_type.kind != TypeKind::Enum)
        throw Diagnostic(subject.span, "match requires an enum value");
    auto owner = context.owner;
    if (symbol(owner).kind == SymbolKind::Global || symbol(owner).kind == SymbolKind::Field)
        owner = symbol(owner).owner;
    const auto temporary = add(SymbolKind::Local, "$match", subject.span, owner);
    symbol(temporary).type = enum_type;
    symbol(temporary).constant = true;
    auto load_subject = [&](const Span& span) {
        auto load = node(IrOp::Load, span, enum_type);
        load.symbol = temporary;
        return load;
    };
    auto result = node(produces_value ? IrOp::ScopedValue : IrOp::Block, stmt.span);
    std::optional<IrType> match_type;
    if (expected) match_type = *expected;
    auto save = node(IrOp::Initialize, subject.span);
    save.symbol = temporary;
    save.operands.push_back(std::move(subject));
    result.body.push_back(std::move(save));

    std::set<SymbolId> seen;
    bool wildcard = false;
    std::vector<std::pair<SymbolId, IrNode>> arms;
    for (const auto& arm : stmt.arms) {
        if (wildcard)
            throw Diagnostic(arm.span, "a wildcard match arm must be last");
        wildcard = arm.variant == "_";
        const auto variant = wildcard ? no_symbol : qualified(arm.variant, arm.span, context);
        if (!wildcard && (symbol(variant).kind != SymbolKind::Variant || symbol(variant).owner != enum_type.symbol))
            throw Diagnostic(arm.span, "match arm must name a variant of " + describe(enum_type));
        if (!wildcard && !seen.insert(variant).second)
            throw Diagnostic(arm.span, "duplicate match arm for '" + arm.variant + "'");
        const auto fields = wildcard ? std::vector<IrType>{} : symbol(variant).payload_types;
        const auto* declaration = wildcard ? nullptr : entries_[variant].declaration;
        if (arm.bindings.size() != fields.size() || arm.named != (declaration && declaration->named_payload))
            throw Diagnostic(arm.span, "match pattern must bind each payload field, using _ to ignore a field");

        auto region = node(produces_value ? IrOp::ScopedValue : IrOp::Block, arm.span);
        context.scopes.emplace_back();
        std::set<std::string> seen_fields;
        for (std::size_t i = 0; i < arm.bindings.size(); ++i) {
            auto field = i;
            if (arm.named) {
                const auto& name = arm.fields[i];
                const auto found = std::ranges::find(declaration->parameters, name, &Parameter::name);
                if (found == declaration->parameters.end() || !seen_fields.insert(name).second)
                    throw Diagnostic(arm.span, "unknown or repeated enum payload field '" + name + "'");
                field = static_cast<std::size_t>(found - declaration->parameters.begin());
            }
            const auto& binding = arm.bindings[i];
            if (binding.text == "_") continue;
            auto& names = context.scopes.back();
            if (names.contains(binding.text))
                throw Diagnostic(binding.span, "duplicate match binding '" + binding.text + "'");
            const auto local = add(SymbolKind::Local, binding.text, binding.span, owner);
            names.emplace(binding.text, local);
            symbol(local).type = fields[field];
            symbol(local).constant = true;
            auto project = node(IrOp::EnumProject, binding.span, fields[field]);
            project.symbol = variant;
            project.text = std::to_string(field);
            project.operands.push_back(load_subject(binding.span));
            auto init = node(IrOp::Initialize, binding.span);
            init.symbol = local;
            init.operands.push_back(std::move(project));
            region.body.push_back(std::move(init));
        }
        if (produces_value) {
            auto returned = value(*arm.value, context, match_type ? &*match_type : nullptr);
            if (!match_type) match_type = returned.type;
            region.type = *match_type;
            region.body.push_back(std::move(returned));
        } else {
            for (const auto& child : arm.body) append_statement(region.body, child, context);
        }
        context.scopes.pop_back();
        arms.emplace_back(variant, std::move(region));
    }
    if (!wildcard) {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.kind == SymbolKind::Variant && item.owner == enum_type.symbol && !seen.contains(id))
                throw Diagnostic(stmt.span, "non-exhaustive match, missing variant '" + item.name + "'");
        }
    }
    if (arms.empty()) throw Diagnostic(stmt.span, "match requires at least one arm");
    if (match_type) result.type = *match_type;
    const auto tag_type = has_enum_payload(program_, enum_type.symbol)
        ? IrType{TypeKind::Integer, "uint32", no_symbol, {}} : enum_type;
    auto tag_slot = temporary;
    if (tag_type.kind == TypeKind::Integer && arms.size() > 1) {
        tag_slot = add(SymbolKind::Local, "$tag", stmt.span, owner);
        symbol(tag_slot).type = tag_type;
        symbol(tag_slot).constant = true;
        auto tag = node(IrOp::EnumTag, stmt.span, tag_type);
        tag.operands.push_back(load_subject(stmt.span));
        auto save_tag = node(IrOp::Initialize, stmt.span);
        save_tag.symbol = tag_slot;
        save_tag.operands.push_back(std::move(tag));
        result.body.push_back(std::move(save_tag));
    }
    auto remaining = std::move(arms.back().second);
    arms.pop_back();
    for (auto it = arms.rbegin(); it != arms.rend(); ++it) {
        const auto& span = it->second.span;
        auto comparison = node(IrOp::Binary, span, bool_type);
        comparison.text = "==";
        auto left = node(IrOp::Load, span, tag_type);
        left.symbol = tag_slot;
        auto right = node(IrOp::Reference, span, enum_type);
        if (tag_type.kind == TypeKind::Integer) {
            right = node(IrOp::Integer, span, tag_type);
            right.text = std::to_string(enum_ordinal(program_, it->first));
        } else {
            right.symbol = it->first;
        }
        comparison.operands.push_back(std::move(left));
        comparison.operands.push_back(std::move(right));
        auto branch = node(produces_value ? IrOp::SelectValue : IrOp::Branch, span, result.type);
        branch.operands.push_back(std::move(comparison));
        branch.body.push_back(std::move(it->second));
        branch.alternative.push_back(std::move(remaining));
        remaining = std::move(branch);
    }
    result.body.push_back(std::move(remaining));
    return result;
}

std::vector<IrNode> Lowering::body(const std::vector<Statement>& stmts, Context& context) {
    context.scopes.emplace_back();
    std::vector<IrNode> instructions;
    instructions.reserve(stmts.size());
    for (const auto& stmt : stmts) {
        append_statement(instructions, stmt, context);
    }
    context.scopes.pop_back();
    return instructions;
}

void Lowering::check_constructor(SymbolId id, const Declaration& decl) {
    auto owner = symbol(id).owner;
    if (symbol(owner).kind != SymbolKind::Class || (decl.name != "init" && decl.name != "deinit"))
        return;
    if (decl.kind != DeclKind::Function)
        throw Diagnostic(decl.span, "init and deinit are reserved lifecycle functions");
    if (!decl.has_body || !decl.generics.empty() || symbol(id).type.kind != TypeKind::Void)
        throw Diagnostic(decl.span, "a lifecycle function needs a body, no type parameters, and void return");
    if (decl.name == "deinit" && !decl.parameters.empty())
        throw Diagnostic(decl.span, "deinit takes no parameters");
    if (decl.name != "init" || symbol(owner).base == no_symbol) return;
    if (!decl.body.empty()) {
        const auto& first = decl.body.front();
        if (first.kind == StmtKind::Expression) {
            const auto& expr = first.expressions.front();
            if (expr.kind == ExprKind::Call && expr.operands.front().kind == ExprKind::Member) {
                const auto& member = expr.operands.front();
                const auto& receiver = member.operands.front();
                if (member.text == "init" && receiver.kind == ExprKind::Name && receiver.text == "super")
                    return;
            }
        }
    }
    throw Diagnostic(decl.span, "derived constructor must start with super.init");
}

void Lowering::lower_declaration(SymbolId id) {
    resolve(id);
    const auto& declaration = *entries_[id].declaration;
    check_override(id, declaration);
    check_constructor(id, declaration);
    if (declaration.kind == DeclKind::Function) {
        if (!declaration.has_body) return;
        auto ctx = context(id);
        ctx.function = id;
        auto instructions = body(declaration.body, ctx);
        if (symbol(id).type.kind != TypeKind::Void && !always_returns(instructions))
            throw Diagnostic(declaration.span, "function may exit without returning " +
                                               describe(symbol(id).type));
        program_.bodies.push_back({id, std::move(instructions)});
    } else if (declaration.kind == DeclKind::Module || declaration.kind == DeclKind::Class) {
        std::vector<IrNode> initializers;
        for (const auto& member : declaration.members) {
            auto child = own_member(id, member.name);
            lower_declaration(child);
            if (auto& initializer = entries_[child].initializer) {
                auto instruction = node(IrOp::Initialize, member.span);
                instruction.symbol = child;
                instruction.operands.push_back(std::move(*initializer));
                initializers.push_back(std::move(instruction));
            }
        }
        if (!initializers.empty()) program_.bodies.push_back({id, std::move(initializers)});
    }
}

IrProgram Lowering::run(const std::vector<ParsedFile>& files) {
    files_ = &files;
    for (const auto& file : files) {
        Span span;
        span.file = file.file;
        auto file_id = add(SymbolKind::File, file.file, span);
        file_ids_.push_back(file_id);
        for (const auto& stmt : file.statements) {
            if (stmt.kind == StmtKind::Binding)
                declare(*stmt.declaration, file_id, SymbolKind::Global);
            if (stmt.kind == StmtKind::Destructure) {
                auto& group = destructures_[&stmt];
                for (const auto& binding : stmt.bindings) {
                    if (binding.text == "_") {
                        group.bindings.push_back(no_symbol);
                        continue;
                    }
                    const auto id = add(SymbolKind::Global, binding.text, binding.span, file_id);
                    if (!entries_[file_id].names.emplace(binding.text, id).second)
                        throw Diagnostic(binding.span, "duplicate declaration of '" + binding.text + "'");
                    entries_[id].resolution = Resolution::Pending;
                    entries_[id].destructure = &stmt;
                    group.bindings.push_back(id);
                }
            }
        }
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto id = file_ids_[i];
        auto ctx = context(id);
        std::vector<IrNode> instructions;
        for (const auto& stmt : files[i].statements) {
            if (stmt.kind == StmtKind::Binding) {
                auto target = own_member(id, stmt.declaration->name);
                lower_declaration(target);
                if (auto& initializer = entries_[target].initializer) {
                    auto instruction = node(IrOp::Initialize, stmt.span);
                    instruction.symbol = target;
                    instruction.operands.push_back(std::move(*initializer));
                    instructions.push_back(std::move(instruction));
                }
            } else if (stmt.kind == StmtKind::Destructure) {
                resolve_destructure(stmt, id);
                for (auto& instruction : destructures_.at(&stmt).instructions)
                    instructions.push_back(std::move(instruction));
            } else {
                instructions.push_back(statement(stmt, ctx));
            }
        }
        if (!instructions.empty()) program_.bodies.push_back({id, std::move(instructions)});
    }
    return std::move(program_);
}

} // namespace

IrProgram lower(const std::vector<ParsedFile>& files) {
    return Lowering{}.run(files);
}

} // namespace neura
