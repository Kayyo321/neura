#include "ir.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
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
const IrType int_type{TypeKind::Integer, "int", no_symbol, {}};
const IrType int32_type{TypeKind::Integer, "int32", no_symbol, {}};
const IrType float64_type{TypeKind::Float, "float64", no_symbol, {}};

std::optional<IrType> primitive(std::string_view name) {
    if (name == "void") return void_type;
    if (name == "bool") return bool_type;
    if (name == "string") return string_type;
    constexpr std::array integers{"byte", "int", "int8", "int16", "int32", "int64",
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

struct Context {
    SymbolId owner;
    SymbolId function = no_symbol;
    SymbolId receiver = no_symbol;
    unsigned loops = 0;
    std::vector<Names> scopes;
};

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
    };

    IrProgram program_;
    std::vector<Entry> entries_;
    std::vector<SymbolId> file_ids_;
    std::map<std::string, SymbolId, std::less<>> builtins_;
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
    SymbolId qualified(const std::string& name, const Span& span, const Context& context);
    bool inside(SymbolId scope, SymbolId owner) const;
    IrType type(const TypeSyntax& syntax, const Context& context);
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
    IrNode reference(SymbolId id, const Span& span, Context& context);
    IrNode select(const Expr& expr, Context& context);
    IrNode call(const Expr& expr, Context& context);
    IrNode allocate(const Expr& expr, Context& context);
    IrNode binary(const Expr& expr, Context& context, const IrType* expected);
    IrNode number(const Expr& expr, const IrType* expected, bool negative = false);
    void arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                   const Substitution& replacements, Context& context);
    void place(const IrNode& destination) const;
    IrNode statement(const Statement& stmt, Context& context);
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
    auto member_kind = kind == SymbolKind::Class ? SymbolKind::Field : SymbolKind::Global;
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

SymbolId Lowering::qualified(const std::string& name, const Span& span, const Context& context) {
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
        id = member(id, parts[i], !inside(context.owner, id));
        if (id == no_symbol) throw Diagnostic(span, "unknown or inaccessible name '" + name + "'");
    }
    resolve(id);
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
            require_type(actual, bound, args[i].span);
        }
        result.emplace(params[i], std::move(actual));
    }
    return result;
}

IrType Lowering::type(const TypeSyntax& syntax, const Context& context) {
    IrType result;
    if (auto builtin = primitive(syntax.name)) {
        if (!syntax.arguments.empty()) throw Diagnostic(syntax.span, "builtin type takes no arguments");
        result = *builtin;
    } else {
        auto id = qualified(syntax.name, syntax.span, context);
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

IrNode Lowering::reference(SymbolId id, const Span& span, Context& context) {
    resolve(id);
    const auto& target = symbol(id);
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
    if (owner != no_symbol && context.receiver != no_symbol) {
        auto receiver = symbol(context.receiver).type;
        bool own_receiver = false;
        for (auto current = context.receiver; current != no_symbol; current = symbol(current).base)
            own_receiver |= current == owner;
        if (own_receiver && (target.kind == SymbolKind::Field || target.kind == SymbolKind::Function ||
                             target.kind == SymbolKind::Global)) {
            auto input = node(IrOp::Receiver, span, receiver);
            input.symbol = context.receiver;
            result.operands.push_back(std::move(input));
        }
    }
    return result;
}

IrNode Lowering::select(const Expr& expr, Context& context) {
    const auto& operand = expr.operands.front();
    if (operand.kind == ExprKind::Name && operand.text == "super") {
        auto receiver = context.receiver;
        if (receiver == no_symbol || context.function == no_symbol ||
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
            auto result = node(IrOp::Length, expr.span, int_type);
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
    bool type_reference = receiver.op == IrOp::Reference && receiver.symbol != no_symbol &&
        (symbol(receiver.symbol).kind == SymbolKind::Class ||
         symbol(receiver.symbol).kind == SymbolKind::Alias);
    if (nominal.kind == TypeKind::Object && (expr.text == "init" || expr.text == "deinit"))
        throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy");
    auto target = member(nominal.symbol, expr.text, !inside(context.owner, nominal.symbol));
    if (target == no_symbol)
        throw Diagnostic(expr.span, "unknown or inaccessible member '" + expr.text + "' of " +
                                    describe(nominal));
    auto result = reference(target, expr.span, context);
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

IrNode Lowering::call(const Expr& expr, Context& context) {
    auto callee = expression(expr.operands.front(), context);
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
        if (!symbol(target).exported && !inside(context.owner, id))
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
    if (floating) return result;
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
    if (numeric_literal(lhs) && !numeric_literal(rhs)) {
        right = value(rhs, context, comparison ? nullptr : expected);
        left = value(lhs, context, &right.type);
    } else {
        left = value(lhs, context, comparison ? nullptr : expected);
        right = value(rhs, context, &left.type);
    }
    bool numeric = left.type.kind == TypeKind::Integer || left.type.kind == TypeKind::Float;
    bool concatenate = left.type.kind == TypeKind::String && op == "+";
    bool equatable = equality && (left.type.kind == TypeKind::Bool || left.type.kind == TypeKind::Enum);
    if (!(numeric || concatenate || equatable))
        throw Diagnostic(expr.span, "operator '" + op + "' is not defined for " + describe(left.type));
    auto result = node(IrOp::Binary, expr.span, comparison ? bool_type : left.type);
    result.text = op;
    result.operands.push_back(std::move(left));
    result.operands.push_back(std::move(right));
    return result;
}

IrNode Lowering::expression(const Expr& expr, Context& context, const IrType* expected) {
    switch (expr.kind) {
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
        if (!logical && input.type.kind != TypeKind::Integer && input.type.kind != TypeKind::Float)
            throw Diagnostic(expr.span, "unary minus requires a numeric operand");
        auto result = node(IrOp::Unary, expr.span, input.type);
        result.text = expr.text;
        result.operands.push_back(std::move(input));
        return result;
    }
    case ExprKind::Binary:
        return binary(expr, context, expected);
    case ExprKind::Member:
        return select(expr, context);
    case ExprKind::Call:
        return call(expr, context);
    case ExprKind::Index: {
        auto receiver = value(expr.operands[0], context);
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
    case ExprKind::Import:
        throw Diagnostic(expr.span, "import must initialize a module-level alias");
    }
    throw Diagnostic(expr.span, "unsupported expression");
}

IrNode Lowering::value(const Expr& expr, Context& context, const IrType* expected) {
    auto result = expression(expr, context, expected);
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
        auto& scope = context.scopes.back();
        if (scope.contains(declaration.name)) {
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
        scope.emplace(declaration.name, id);

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
    case StmtKind::While: {
        auto result = node(IrOp::Loop, stmt.span);
        result.operands.push_back(value(stmt.expressions.front(), context, &bool_type));
        ++context.loops;
        result.body = body(stmt.body, context);
        --context.loops;
        return result;
    }
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
        if (object.type.kind != TypeKind::Object) {
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

std::vector<IrNode> Lowering::body(const std::vector<Statement>& stmts, Context& context) {
    context.scopes.emplace_back();
    std::vector<IrNode> instructions;
    instructions.reserve(stmts.size());
    for (const auto& stmt : stmts) {
        instructions.push_back(statement(stmt, context));
    }
    context.scopes.pop_back();
    return instructions;
}

bool terminates(const std::vector<IrNode>& instructions);

bool exits_loop(const std::vector<IrNode>& instructions) {
    for (const auto& instruction : instructions) {
        if (instruction.op == IrOp::Break) {
            return true;
        }
        if (instruction.op == IrOp::Block && exits_loop(instruction.body)) {
            return true;
        }
        if (instruction.op == IrOp::Branch &&
            (exits_loop(instruction.body) || exits_loop(instruction.alternative))) {
            return true;
        }
    }
    return false;
}

bool terminates(const std::vector<IrNode>& instructions) {
    for (const auto& instruction : instructions) {
        if (instruction.op == IrOp::Return) {
            return true;
        }
        if (instruction.op == IrOp::Block && terminates(instruction.body)) {
            return true;
        }
        if (instruction.op == IrOp::Branch &&
            terminates(instruction.body) && terminates(instruction.alternative)) {
            return true;
        }
        if (instruction.op == IrOp::Loop && instruction.operands.front().op == IrOp::Boolean &&
            instruction.operands.front().text == "true" && !exits_loop(instruction.body)) {
            return true;
        }
    }
    return false;
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
        if (symbol(id).type.kind != TypeKind::Void && !terminates(instructions))
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
