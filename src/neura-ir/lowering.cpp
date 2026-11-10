#include "aggregate.hpp"
#include "control_flow.hpp"
#include "float.hpp"
#include "host_binding.hpp"
#include "integer.hpp"
#include "ir.hpp"
#include "nominal.hpp"
#include "static_initialization.hpp"
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
const IrType int64_type{TypeKind::Integer, "int64", no_symbol, {}};
const IrType uint8_type{TypeKind::Integer, "uint8", no_symbol, {}};
const IrType uint64_type{TypeKind::Integer, "uint64", no_symbol, {}};
const IrType uint32_type{TypeKind::Integer, "uint32", no_symbol, {}};
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

std::optional<IrOp> intrinsic_value_member(std::string_view name) {
    if (name == "equals") return IrOp::ValueEquals;
    if (name == "hash_code") return IrOp::ValueHash;
    if (name == "to_string") return IrOp::ValueText;
    return std::nullopt;
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

IrNode discard_before(IrNode qualifier, IrNode result) {
    auto region = node(IrOp::ScopedValue, result.span, result.type);
    auto discard = node(IrOp::Discard, qualifier.span);
    discard.operands.push_back(std::move(qualifier));
    region.body.push_back(std::move(discard));
    region.body.push_back(std::move(result));
    return region;
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
        std::vector<SymbolId> promoted_modules{};
        SymbolId static_scope = no_symbol;
        SymbolId singleton_instance = no_symbol;
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
    const std::vector<ParsedFile>* files_ = nullptr;
    SymbolId root_class_ = no_symbol;

    IrSymbol& symbol(SymbolId id) { return program_.symbols[id]; }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    SymbolId add(SymbolKind kind, std::string name, const Span& span, SymbolId owner = no_symbol);
    SymbolId declare(const Declaration& decl, SymbolId owner, SymbolKind kind);
    bool static_member(SymbolId id) const;
    SymbolId static_scope(SymbolId owner);
    SymbolId root_class();
    bool class_reference(const IrType& type) const;
    SymbolId resolve_import(const Expr& expr);
    SymbolId resolve_import_target(const ResolvedImport& import, const Span& span) const;
    void prepare_promotions(const Declaration& decl, SymbolId owner);
    void resolve(SymbolId id);
    void resolve_binding(SymbolId id, const Declaration& decl);
    void resolve_generics(SymbolId id, const Declaration& decl, Context& context);
    void lower_declaration(SymbolId id);
    void check_override(SymbolId id, const Declaration& decl);
    void check_interfaces(SymbolId id);
    void check_constructor(SymbolId id, const Declaration& decl);
    void require_implicit_constructor(SymbolId cls, const Span& span);

    SymbolId own_member(SymbolId owner, std::string_view name) const;
    SymbolId member(SymbolId owner, std::string_view name, bool exported) const;
    SymbolId value_member(const IrType& receiver, std::string_view name, bool exported) const;
    SymbolId find(std::string_view name, const Context& context, const Span& span,
                  SymbolId* promoted_receiver = nullptr);
    SymbolId qualified(const std::string& name, const Span& span, const Context& context, bool nominal_type = false);
    bool inside(SymbolId scope, SymbolId owner) const;
    IrType type(const TypeSyntax& syntax, const Context& context);
    IrType callable_type(std::vector<IrType> signature, const Span& span);
    SymbolId closure_layout(const Span& span, SymbolId scope);
    SymbolId closure_method(SymbolId layout, const IrType& type, const Span& span,
                            const std::vector<std::string>& names);
    IrNode closure_value(const IrType& type, ClosureEnvironment& environment, const Span& span);
    IrNode lambda(const Declaration& declaration, Context& context);
    IrNode bind_function(IrNode callee, Context& context);
    std::optional<IrNode> static_qualifier(IrNode& callee);
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
    Substitution receiver_arguments(const IrType& type, SymbolId owner = no_symbol) const;

    Context context(SymbolId owner);
    IrNode expression(const Expr& expr, Context& context, const IrType* expected = nullptr);
    IrNode value(const Expr& expr, Context& context, const IrType* expected = nullptr);
    IrNode reference(SymbolId id, const Span& span, Context& context, bool implicit_receiver = true);
    IrNode select(const Expr& expr, Context& context);
    IrNode call(const Expr& expr, Context& context);
    std::optional<IrNode> primitive_call(const Expr& expr, Context& context);
    IrNode enum_construct(const IrNode& variant, const Expr& expr, Context& context);
    Substitution enum_arguments(const IrType& type) const;
    IrNode tuple(const Expr& expr, Context& context, const IrType* expected);
    IrNode range(const Expr& expr, Context& context, const IrType* expected);
    IrType parameter_type(const Parameter& parameter, const Context& context);
    IrNode pack_argument(const Expr& expr, const IrType& argument, Context& context);
    IrNode allocate(const Expr& expr, Context& context);
    IrNode binary(const Expr& expr, Context& context, const IrType* expected);
    IrNode comparison_chain(const Expr& expr, Context& context);
    IrNode conditional(const Expr& expr, Context& context, const IrType* expected);
    IrNode update(const Expr& expr, Context& context);
    IrNode number(const Expr& expr, const IrType* expected, bool negative = false);
    void arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                   const Substitution& replacements, Context& context);
    void place(const IrNode& destination) const;
    IrNode save_temporary(IrNode initializer, std::vector<IrNode>& instructions,
                          Context& context, std::string name, bool constant = true);
    IrNode capture_place(const Expr& expr, std::vector<IrNode>& instructions, Context& context);
    IrNode statement(const Statement& stmt, Context& context);
    IrNode discard_binding(const Declaration& decl, Context& context);
    void append_statement(std::vector<IrNode>& instructions, const Statement& stmt, Context& context);
    void destructure(std::vector<IrNode>& instructions, const Statement& stmt, Context& context,
                     const std::vector<SymbolId>* globals = nullptr);
    void resolve_destructure(const Statement& stmt, SymbolId file);
    IrNode lower_match(const Statement& stmt, Context& context, bool produces_value, const IrType* expected = nullptr);
    IrNode do_loop(const Statement& stmt, Context& context);
    IrNode for_loop(const Statement& stmt, Context& context);
    IrNode for_each(const Statement& stmt, Context& context);
    IrNode for_each_iterable(const Statement& stmt, Context& context, IrNode items);
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
    if (decl.name == "_" && kind != SymbolKind::Variant) {
        if (decl.kind != DeclKind::Binding || decl.exported || decl.override_member ||
            decl.static_member || !decl.initializer ||
            (symbol(owner).kind != SymbolKind::File && symbol(owner).kind != SymbolKind::Module))
            throw Diagnostic(decl.span, "'_' requires a value initializer and does not declare a member");
        return no_symbol;
    }
    switch (decl.kind) {
    case DeclKind::Module: kind = SymbolKind::Module; break;
    case DeclKind::Class: kind = SymbolKind::Class; break;
    case DeclKind::Interface: kind = SymbolKind::Interface; break;
    case DeclKind::Function: kind = SymbolKind::Function; break;
    case DeclKind::Enum: kind = SymbolKind::Enum; break;
    case DeclKind::Binding: break;
    }
    const auto lexical = owner;
    if (decl.static_member) {
        if (!class_or_interface(symbol(owner).kind) || decl.override_member || decl.promote_import ||
            decl.kind == DeclKind::Module || decl.name == "init" || decl.name == "deinit")
            throw Diagnostic(decl.span, "static requires a non-override class or interface member");
        owner = static_scope(owner);
        if (kind == SymbolKind::Field) kind = SymbolKind::Global;
    }
    auto id = add(kind, decl.name, decl.span, owner);
    if (!entries_[lexical].names.emplace(decl.name, id).second)
        throw Diagnostic(decl.span, "duplicate declaration of '" + decl.name + "'");
    entries_[id].declaration = &decl;
    entries_[id].resolution = Resolution::Pending;
    symbol(id).constant = decl.constant ||
        (decl.static_member && kind == SymbolKind::Global && symbol(lexical).kind == SymbolKind::Interface);
    symbol(id).exported = decl.exported || symbol(lexical).kind == SymbolKind::Interface;
    symbol(id).has_body = decl.has_body;
    if (kind == SymbolKind::Module)
        symbol(id).type = {TypeKind::Module, decl.name, id, {}};
    if (decl.singleton) {
        if (symbol(owner).kind != SymbolKind::File && symbol(owner).kind != SymbolKind::Module)
            throw Diagnostic(decl.span, "a singleton must be a file, module, or static class member");
        const auto instance = add(SymbolKind::Global, decl.name, decl.span, owner);
        entries_[id].singleton_instance = instance;
        symbol(instance).constant = true;
    }
    auto member_kind = kind == SymbolKind::Class ? SymbolKind::Field
                     : kind == SymbolKind::Enum ? SymbolKind::Variant
                                                : SymbolKind::Global;
    for (const auto& child : decl.members) declare(child, id, member_kind);
    return id;
}

bool Lowering::static_member(SymbolId id) const {
    const auto* decl = entries_[id].declaration;
    return decl && decl->static_member;
}

SymbolId Lowering::static_scope(SymbolId owner) {
    if (entries_[owner].static_scope != no_symbol) return entries_[owner].static_scope;
    const auto scope = add(SymbolKind::Module, static_scope_name, symbol(owner).span, owner);
    symbol(scope).type = {TypeKind::Module, static_scope_name, scope, {}};
    entries_[owner].static_scope = scope;
    return scope;
}

SymbolId Lowering::root_class() {
    if (root_class_ != no_symbol) return root_class_;
    Span span;
    span.file = "<class>";
    root_class_ = add(SymbolKind::Class, "$class", span);
    const IrType root{TypeKind::Object, "class", root_class_, {}};
    symbol(root_class_).type = root;
    const auto make_method = [&](std::string name, const IrType& result, IrOp operation) {
        const auto method = add(SymbolKind::Function, std::move(name), span, root_class_);
        entries_[root_class_].names.emplace(symbol(method).name, method);
        symbol(method).type = result;
        symbol(method).exported = true;
        symbol(method).has_body = true;
        auto value = node(operation, span, result);
        auto self = node(IrOp::Receiver, span, root);
        self.symbol = root_class_;
        value.operands.push_back(std::move(self));
        if (operation == IrOp::Binary) {
            const auto other = add(SymbolKind::Parameter, "other", span, method);
            symbol(other).type = root;
            symbol(method).parameters.push_back(other);
            auto load = node(IrOp::Load, span, root);
            load.symbol = other;
            value.text = "==";
            value.operands.push_back(std::move(load));
        }
        auto ret = node(IrOp::Return, span);
        ret.operands.push_back(std::move(value));
        program_.bodies.push_back({method, {std::move(ret)}});
    };
    make_method("equals", bool_type, IrOp::Binary);
    make_method("hash_code", uint32_type, IrOp::ClassHash);
    make_method("to_string", string_type, IrOp::ClassText);
    return root_class_;
}

bool Lowering::class_reference(const IrType& type) const {
    return receiver_type(type).kind == TypeKind::Object;
}

SymbolId Lowering::resolve_import(const Expr& expr) {
    if (expr.text.ends_with(".*"))
        throw Diagnostic(expr.span, "wildcard imports require 'let.promote _'");
    auto source = std::ranges::find(*files_, expr.span.file, &ParsedFile::file);
    if (source == files_->end()) throw Diagnostic(expr.span, "cannot locate importing file");
    auto import = std::ranges::find(source->imports, expr.text, &ResolvedImport::name);
    if (import == source->imports.end()) {
        // The built-in SysIo and Test modules were retired in favor of the
        // standard library (Stdlib.md section 4.6); name their replacements.
        if (expr.text == "SysIo")
            throw Diagnostic(expr.span, "cannot resolve import 'SysIo': the built-in SysIo module was removed; "
                                        "use Std.Io.Console and Std.Program");
        if (expr.text == "Test")
            throw Diagnostic(expr.span, "cannot resolve import 'Test': the built-in Test module was removed; "
                                        "use Std.Test");
        throw Diagnostic(expr.span, "cannot resolve import '" + expr.text + "'");
    }
    auto result = resolve_import_target(*import, expr.span);
    resolve(result);
    return result;
}

SymbolId Lowering::resolve_import_target(const ResolvedImport& import, const Span& span) const {
    auto target_file = std::ranges::find(*files_, import.file, &ParsedFile::file);
    if (target_file == files_->end())
        throw Diagnostic(span, "cannot load source file for import '" + import.name + "'");
    auto index = static_cast<std::size_t>(target_file - files_->begin());
    auto name = import.name.ends_with(".*") ? import.target_module
        : import.name.substr(import.name.find_last_of('.') + 1);
    if (name.empty())
        throw Diagnostic(span, "imported file '" + import.file + "' must declare a module matching its name");
    auto result = own_member(file_ids_[index], name);
    if (result == no_symbol || symbol(result).kind != SymbolKind::Module)
        throw Diagnostic(span, "imported file must declare module '" + name + "'");
    return result;
}

void Lowering::prepare_promotions(const Declaration& decl, SymbolId owner) {
    if (decl.promote_import || (decl.name == "_" && decl.initializer && decl.initializer->kind == ExprKind::Import)) {
        if (symbol(owner).kind != SymbolKind::Module && symbol(owner).kind != SymbolKind::File)
            throw Diagnostic(decl.span, "promoted and discarded imports require a file or module binding");
        if (decl.type || decl.exported || !decl.initializer || decl.initializer->kind != ExprKind::Import)
            throw Diagnostic(decl.span, "let.promote requires an unannotated import alias");
        const auto& expr = *decl.initializer;
        if (expr.text.ends_with(".*") && (!decl.promote_import || decl.name != "_"))
            throw Diagnostic(expr.span, "wildcard imports require 'let.promote _'");
        const auto source = std::ranges::find(*files_, expr.span.file, &ParsedFile::file);
        if (source == files_->end()) throw Diagnostic(expr.span, "cannot locate importing file");
        bool found = false;
        for (const auto& import : source->imports) {
            if (import.name != expr.text) continue;
            found = true;
            const auto target = resolve_import_target(import, expr.span);
            if (decl.promote_import) entries_[owner].promoted_modules.push_back(target);
            if (decl.name == "_") {
                const auto dependency = add(SymbolKind::Import, "$import", decl.span, owner);
                symbol(dependency).type = symbol(target).type;
            }
        }
        if (!found) throw Diagnostic(expr.span, "cannot resolve import '" + expr.text + "'");
    }
    if (!decl.members.empty()) {
        const auto id = own_member(owner, decl.name);
        for (const auto& child : decl.members) prepare_promotions(child, id);
    }
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

SymbolId Lowering::value_member(const IrType& receiver, std::string_view name, bool exported) const {
    const auto views = nominal_views(program_, receiver);
    for (const auto& view : views) {
        if (view.kind == TypeKind::Object && symbol(view.symbol).kind == SymbolKind::Class) {
            if (const auto id = member(view.symbol, name, exported); id != no_symbol) return id;
        }
    }
    for (const auto& view : views) {
        if (view.kind == TypeKind::Object && symbol(view.symbol).kind == SymbolKind::Interface) {
            const auto id = own_member(view.symbol, name);
            if (id != no_symbol && !(static_member(id) && symbol(id).kind == SymbolKind::Function))
                return id;
        }
    }
    return no_symbol;
}

SymbolId Lowering::find(std::string_view name, const Context& context, const Span& span,
                        SymbolId* promoted_receiver) {
    for (auto it = context.scopes.rbegin(); it != context.scopes.rend(); ++it) {
        if (auto local = it->find(name); local != it->end()) return local->second;
    }
    if (context.closure) return find(name, context.closure->enclosing, span, promoted_receiver);
    bool static_context = false;
    for (auto scope = context.owner; scope != no_symbol; scope = symbol(scope).owner) {
        static_context |= is_static_scope(symbol(scope));
        if (auto id = member(scope, name, false); id != no_symbol) {
            if (static_context && symbol(id).kind == SymbolKind::TypeParameter && class_or_interface(symbol(scope).kind))
                throw Diagnostic(span, "static member cannot use an enclosing class type parameter");
            return id;
        }
        if (class_or_interface(symbol(scope).kind)) {
            if (auto id = value_member(symbol(scope).type, name, true); id != no_symbol) return id;
        }
        for (auto base = symbol(scope).base; base != no_symbol; base = symbol(base).base) {
            if (symbol(base).name == name) return base;
        }
    }
    for (auto scope = context.owner; scope != no_symbol; scope = symbol(scope).owner) {
        SymbolId result = no_symbol;
        SymbolId source = no_symbol;
        // Resolving a module can grow the symbol table and invalidate the entries.
        const auto promoted = entries_[scope].promoted_modules;
        for (auto module : promoted) {
            if (entries_[module].resolution == Resolution::Pending && entries_[module].declaration->base)
                resolve(module);
            const auto candidate = member(module, name, true);
            if (candidate == no_symbol || module == source) continue;
            if (result != no_symbol)
                throw Diagnostic(span, "ambiguous promoted name '" + std::string(name) + "'");
            result = candidate;
            source = module;
        }
        if (result != no_symbol) {
            if (promoted_receiver) *promoted_receiver = source;
            return result;
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
    auto id = find(parts.front(), context, span);
    if (id == no_symbol) throw Diagnostic(span, "unknown name '" + parts.front() + "'");
    for (std::size_t i = 1; i < parts.size(); ++i) {
        resolve(id);
        auto kind = symbol(id).kind;
        if (kind == SymbolKind::Import) {
            id = symbol(id).type.symbol;
        } else if (kind == SymbolKind::Alias && symbol(id).type.kind == TypeKind::Enum) {
            id = symbol(id).type.symbol;
        } else if (kind != SymbolKind::Module && !class_or_interface(kind) &&
                   kind != SymbolKind::Enum) {
            throw Diagnostic(span, "'" + parts[i - 1] + "' is not a namespace");
        }
        id = member(id, parts[i], !inside(lexical_owner(context), id));
        if (id == no_symbol) throw Diagnostic(span, "unknown or inaccessible name '" + name + "'");
    }
    if (!(nominal_type && (symbol(id).kind == SymbolKind::Enum || class_or_interface(symbol(id).kind)) &&
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
        result.emplace(params[i], std::move(actual));
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& parameter = symbol(params[i]);
        const auto check = [&](const IrType& declared) {
            const auto bound = substitute(declared, result);
            const auto& actual = result.at(params[i]);
            if (!assignable(actual, bound)) {
                throw Diagnostic(args[i].span, "type argument " + describe(actual) + " does not satisfy the bound " +
                                               describe(bound) + " of type parameter '" + parameter.name + "'");
            }
        };
        if (parameter.bound) check(*parameter.bound);
        for (const auto& bound : parameter.additional_bounds) check(bound);
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
    } else if (syntax.name == "class") {
        result = symbol(root_class()).type;
    } else if (auto builtin = primitive(syntax.name)) {
        if (!syntax.arguments.empty()) throw Diagnostic(syntax.span, "builtin type takes no arguments");
        result = *builtin;
    } else {
        auto id = qualified(syntax.name, syntax.span, context, true);
        auto kind = symbol(id).kind;
        if (!class_or_interface(kind) && kind != SymbolKind::Alias &&
            kind != SymbolKind::Enum && kind != SymbolKind::TypeParameter) {
            throw Diagnostic(syntax.span, "'" + syntax.name + "' is not a type");
        }
        const auto params = symbol(id).generics;
        if (entries_[id].resolution == Resolution::Resolving && kind == SymbolKind::Enum &&
            params.size() != entries_[id].declaration->generics.size())
            throw Diagnostic(syntax.span, "recursive reference in an enum's type parameter bounds");
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
    return nominal_assignable(program_, actual, expected);
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

Substitution Lowering::receiver_arguments(const IrType& type, SymbolId owner) const {
    auto nominal = receiver_type(type);
    Substitution result;
    if (owner != no_symbol && nominal.symbol != owner) {
        const auto views = nominal_views(program_, type);
        const auto view = std::ranges::find(views, owner, &IrType::symbol);
        if (view == views.end()) return result;
        nominal = *view;
    }
    if (nominal.kind == TypeKind::Object) {
        const auto& parameters = symbol(nominal.symbol).generics;
        for (std::size_t i = 0; i < std::min(parameters.size(), nominal.arguments.size()); ++i)
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
        if (class_or_interface(symbol(id).kind) || symbol(id).kind == SymbolKind::Module) {
            result.receiver = id;
            break;
        }
    }
    return result;
}

IrType Lowering::callable_type(std::vector<IrType> signature, const Span& span) {
    for (std::size_t i = 0; i + 1 < signature.size(); ++i) {
        if (signature[i].kind == TypeKind::Void) {
            throw Diagnostic(span, "a function parameter cannot have type void");
        }
    }
    return {TypeKind::Callable, "func", no_symbol, std::move(signature)};
}

SymbolId Lowering::closure_layout(const Span& span, SymbolId scope) {
    const auto file = owner_kind(scope, SymbolKind::File);
    const auto layout = add(SymbolKind::Class, "$closure" + std::to_string(program_.symbols.size()), span, file);
    symbol(layout).type = {TypeKind::Object, symbol(layout).name, layout, {}};
    // A closure body may use any type parameter in lexical scope, including
    // one that appears only in a local annotation or another generic call.
    for (auto owner = scope; owner != no_symbol; owner = symbol(owner).owner) {
        if (is_static_scope(symbol(owner))) break;
        for (const auto parameter : symbol(owner).generics) {
            symbol(layout).generics.push_back(parameter);
            symbol(layout).type.arguments.push_back(symbol(parameter).type);
        }
    }
    return layout;
}

SymbolId Lowering::closure_method(SymbolId layout, const IrType& type, const Span& span,
                                   const std::vector<std::string>& names) {
    const auto method = add(SymbolKind::Function, "$invoke", span, layout);
    entries_[layout].names.emplace("$invoke", method);
    symbol(method).type = type.arguments.back();
    symbol(method).has_body = true;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto parameter = add(SymbolKind::Parameter, names[i], span, method);
        if (!entries_[method].names.emplace(names[i], parameter).second) {
            throw Diagnostic(span, "duplicate parameter '" + names[i] + "'");
        }
        symbol(parameter).type = type.arguments[i];
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

    auto result = node(IrOp::MakeClosure, span, type);
    result.symbol = constructor;
    result.type_arguments = symbol(layout).type.arguments;
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

IrType Lowering::parameter_type(const Parameter& parameter, const Context& context) {
    if (!parameter.variadic) return type(parameter.type, context);
    if (parameter.type.name == "[variadic argument]") {
        const Expr dependency{ExprKind::Import, parameter.type.span,
                              "Std.Core.Variadic", {}, {}, std::nullopt};
        const auto module = resolve_import(dependency);
        const auto argument = own_member(module, "Argument");
        if (argument == no_symbol || symbol(argument).kind != SymbolKind::Enum)
            throw Diagnostic(parameter.span, "Std.Core.Variadic must export its Argument enum");
        resolve(argument);
        return collection_type(TypeKind::Array, symbol(argument).type);
    }
    auto element = type(parameter.type, context);
    if (element.kind == TypeKind::Void)
        throw Diagnostic(parameter.span, "a variadic parameter cannot have elements of type void");
    return collection_type(TypeKind::Array, element);
}

IrNode Lowering::lambda(const Declaration& declaration, Context& context) {
    const auto& span = declaration.span;
    std::vector<IrType> signature;
    std::vector<std::string> names;
    for (const auto& parameter : declaration.parameters) {
        if (parameter.variadic)
            throw Diagnostic(parameter.span, "a variadic parameter requires a named function");
        signature.push_back(parameter_type(parameter, context));
        names.push_back(parameter.name);
    }
    signature.push_back(type(*declaration.type, context));
    const auto callable = callable_type(std::move(signature), span);
    ClosureEnvironment environment{closure_layout(span, lexical_owner(context)), context, {}, {}};
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
    auto qualifier = static_qualifier(callee);
    auto replacements = callee.operands.empty() ? Substitution{}
        : receiver_arguments(callee.operands.front().type, symbol(target).owner);
    std::vector<IrType> signature;
    std::vector<std::string> names;
    for (const auto parameter : symbol(target).parameters) {
        signature.push_back(substitute(symbol(parameter).type, replacements));
        names.push_back(symbol(parameter).name);
    }
    signature.push_back(substitute(symbol(target).type, replacements));
    const auto callable = callable_type(std::move(signature), span);
    ClosureEnvironment environment{closure_layout(span, lexical_owner(context)), context, {}, {}};
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
    auto closure = closure_value(callable, environment, span);
    return qualifier ? discard_before(std::move(*qualifier), std::move(closure)) : std::move(closure);
}

std::optional<IrNode> Lowering::static_qualifier(IrNode& callee) {
    if (callee.operands.empty() || !static_member(callee.symbol)) return std::nullopt;
    auto qualifier = std::move(callee.operands.front());
    callee.operands.clear();
    return qualifier;
}

void Lowering::resolve_generics(SymbolId id, const Declaration& decl, Context& context) {
    for (const auto& parameter : decl.generics) {
        auto generic = add(SymbolKind::TypeParameter, parameter.name, parameter.span, id);
        if (!entries_[id].names.emplace(parameter.name, generic).second)
            throw Diagnostic(parameter.span, "duplicate type parameter '" + parameter.name + "'");
        symbol(generic).type = {TypeKind::TypeParameter, parameter.name, generic, {}};
        symbol(id).generics.push_back(generic);
    }
    for (std::size_t i = 0; i < decl.generics.size(); ++i) {
        const auto generic = symbol(id).generics[i];
        for (const auto& syntax : decl.generics[i].bounds) {
            auto bound = type(syntax, context);
            if (bound.kind != TypeKind::Object) {
                throw Diagnostic(syntax.span, "a type parameter bound must be a class or interface");
            }
            auto& parameter = symbol(generic);
            if (!parameter.bound) {
                parameter.bound = std::move(bound);
            } else {
                if (*parameter.bound == bound ||
                    std::ranges::find(parameter.additional_bounds, bound) != parameter.additional_bounds.end()) {
                    throw Diagnostic(syntax.span, "duplicate type parameter bound");
                }
                parameter.additional_bounds.push_back(std::move(bound));
            }
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
            if (kind == SymbolKind::Alias || class_or_interface(kind) || kind == SymbolKind::Enum) {
                if (!symbol(initializer.symbol).generics.empty() && init.kind != ExprKind::TypeApplication)
                    throw Diagnostic(init.span, "generic type overlays are not supported");
                symbol(id).kind = SymbolKind::Alias;
                symbol(id).type = initializer.type;
                return;
            }
            if (kind == SymbolKind::Variant) {
                if (!symbol(initializer.symbol).payload_types.empty())
                    throw Diagnostic(init.span, "enum variant requires a payload");
                if (has_enum_payload(program_, initializer.type.symbol)) initializer.op = IrOp::EnumConstruct;
            }
        }
        if (initializer.op == IrOp::Reference && initializer.type.kind == TypeKind::Callable &&
            initializer.symbol != no_symbol &&
            symbol(initializer.symbol).kind == SymbolKind::Function) {
            initializer = bind_function(std::move(initializer), ctx);
        }
        if (initializer.type.kind == TypeKind::Void || initializer.type.kind == TypeKind::Module ||
            (initializer.type.kind == TypeKind::Callable && !is_function_value(initializer.type)) ||
            initializer.op == IrOp::Reference) {
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
    } else if (decl.static_member && !symbol(id).constant &&
               (declared.kind == TypeKind::Integer || declared.kind == TypeKind::Float ||
                declared.kind == TypeKind::Bool || declared.kind == TypeKind::Object)) {
        const auto op = declared.kind == TypeKind::Integer ? IrOp::Integer
                      : declared.kind == TypeKind::Float ? IrOp::Float
                      : declared.kind == TypeKind::Bool ? IrOp::Boolean : IrOp::Nil;
        auto initializer = node(op, decl.span, declared);
        if (op != IrOp::Nil) initializer.text = op == IrOp::Boolean ? "false" : "0";
        entries_[id].initializer = std::move(initializer);
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
    if (class_or_interface(symbol(id).kind) || decl.kind == DeclKind::Module) {
        auto kind = decl.kind == DeclKind::Module ? TypeKind::Module : TypeKind::Object;
        symbol(id).type = {kind, decl.name, id, {}};
        if (const auto instance = entries_[id].singleton_instance; instance != no_symbol)
            symbol(instance).type = symbol(id).type;
        resolve_generics(id, decl, ctx);
        for (auto generic : symbol(id).generics)
            symbol(id).type.arguments.push_back(symbol(generic).type);
        if (decl.base && decl.base->name != "class") {
            auto base = qualified(decl.base->name, decl.base->span, context(symbol(id).owner));
            if (symbol(base).kind == SymbolKind::Import) base = symbol(base).type.symbol;
            if (symbol(base).kind == SymbolKind::Alias && symbol(base).type.kind == TypeKind::Object)
                base = symbol(base).type.symbol;
            if (symbol(base).kind != symbol(id).kind)
                throw Diagnostic(decl.base->span, "base must have the same declaration kind");
            if (entries_[base].singleton_instance != no_symbol)
                throw Diagnostic(decl.base->span, "a singleton cannot be extended");
            if (symbol(base).kind == SymbolKind::Module && symbol(base).constant)
                throw Diagnostic(decl.base->span, "cannot inherit a constant module");
            if (!symbol(base).generics.empty() || !decl.base->arguments.empty() ||
                !decl.base->collections.empty())
                throw Diagnostic(decl.base->span, "generic inheritance is not supported");
            symbol(id).base = base;
        } else if (class_or_interface(symbol(id).kind)) {
            if (decl.base && !decl.base->collections.empty())
                throw Diagnostic(decl.base->span, "a base must be a class type");
            symbol(id).base = root_class();
        } else if (decl.base) {
            throw Diagnostic(decl.base->span, "a module cannot inherit from class");
        }
        for (const auto& syntax : decl.interfaces) {
            auto interface = type(syntax, ctx);
            if (interface.kind != TypeKind::Object || symbol(interface.symbol).kind != SymbolKind::Interface) {
                throw Diagnostic(syntax.span, "an interface is required here");
            }
            const auto views = nominal_views(program_, interface);
            if (std::ranges::find(views, id, &IrType::symbol) != views.end()) {
                throw Diagnostic(syntax.span, "cyclic interface inheritance");
            }
            if (std::ranges::find(symbol(id).interfaces, interface.symbol, &IrType::symbol) !=
                symbol(id).interfaces.end()) {
                throw Diagnostic(syntax.span, "interface is listed more than once");
            }
            symbol(id).interfaces.push_back(std::move(interface));
        }
    } else if (decl.kind == DeclKind::Enum) {
        // Variants are values of the enum, visible wherever the enum is.
        symbol(id).type = {TypeKind::Enum, decl.name, id, {}};
        resolve_generics(id, decl, ctx);
        for (const auto generic : symbol(id).generics)
            symbol(id).type.arguments.push_back(symbol(generic).type);
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
            auto argument_type = parameter_type(parameter, ctx);
            if (argument_type.kind == TypeKind::Void)
                throw Diagnostic(parameter.span, "a parameter cannot have type void");
            auto arg = add(SymbolKind::Parameter, parameter.name, parameter.span, id);
            if (!entries_[id].names.emplace(parameter.name, arg).second)
                throw Diagnostic(parameter.span, "duplicate parameter '" + parameter.name + "'");
            symbol(arg).type = std::move(argument_type);
            symbol(id).parameters.push_back(arg);
        }
        if (!decl.has_body && symbol(symbol(id).owner).kind != SymbolKind::Module &&
            !class_or_interface(symbol(symbol(id).owner).kind))
            throw Diagnostic(decl.span, "only class, interface or module members may omit a function body");
        if (!decl.has_body && decl.static_member)
            throw Diagnostic(decl.span, "a static function requires a body");
        if (decl.host_binding) {
            const auto file = source_file(program_, id);
            if (file == no_symbol || symbol(file).library_path.empty() ||
                symbol(symbol(id).owner).kind != SymbolKind::Module)
                throw Diagnostic(decl.span, "host bindings are only allowed in standard library modules");
            if (!decl.generics.empty() || decl.override_member ||
                std::ranges::any_of(decl.parameters, &Parameter::variadic) ||
                !host_service_name(decl.host_binding->first))
                throw Diagnostic(decl.span, "a host binding needs a non-generic function and a qualified service name");
            for (auto parameter : symbol(id).parameters) {
                if (!host_data_type(symbol(parameter).type))
                    throw Diagnostic(symbol(parameter).span, "this parameter cannot cross the host boundary");
            }
            if (!host_variants(program_, symbol(id).type))
                throw Diagnostic(decl.span, "a host result needs exactly Ok or Ok(T), and Error(uint32)");
            symbol(id).external = true;
            symbol(id).host_binding = IrHostBinding{decl.host_binding->first, decl.host_binding->second};
        }
    } else {
        resolve_binding(id, decl);
    }
    entries_[id].resolution = Resolution::Resolved;
}

void Lowering::check_override(SymbolId id, const Declaration& decl) {
    auto owner = decl.static_member ? symbol(symbol(id).owner).owner : symbol(id).owner;
    auto base = symbol(owner).base;
    auto inherited = base == no_symbol ? no_symbol : member(base, decl.name, true);
    if (inherited != no_symbol && (decl.static_member || static_member(inherited))) {
        if (decl.override_member)
            throw Diagnostic(decl.span, "a static member is hidden without .override");
        if (symbol(id).kind == SymbolKind::Function || symbol(inherited).kind == SymbolKind::Function) {
            resolve(inherited);
            const auto same_parameter = [&](SymbolId a, SymbolId b) { return symbol(a).type == symbol(b).type; };
            if (decl.static_member != static_member(inherited) ||
                symbol(inherited).kind != SymbolKind::Function || symbol(id).kind != SymbolKind::Function ||
                !symbol(id).generics.empty() || !symbol(inherited).generics.empty() ||
                !assignable(symbol(id).type, symbol(inherited).type) ||
                !std::ranges::equal(symbol(id).parameters, symbol(inherited).parameters, same_parameter))
                throw Diagnostic(decl.span, "static function does not match inherited member '" + decl.name + "'");
        }
        return;
    }
    if (inherited != no_symbol && symbol(owner).kind == SymbolKind::Class &&
        (decl.name == "init" || decl.name == "deinit")) inherited = no_symbol;
    if (decl.override_member && inherited == no_symbol) {
        for (const auto& view : nominal_views(program_, symbol(owner).type)) {
            if (view.symbol != owner && symbol(view.symbol).kind == SymbolKind::Interface) {
                const auto requirement = own_member(view.symbol, decl.name);
                if (requirement != no_symbol && !static_member(requirement)) {
                    if (!symbol(id).exported) throw Diagnostic(decl.span, "an override must be exported");
                    symbol(id).overrides = requirement;
                    return;
                }
            }
        }
        throw Diagnostic(decl.span, "no inherited member named '" + decl.name + "'");
    }
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

void Lowering::check_interfaces(SymbolId id) {
    const auto nominal = symbol(id).type;
    std::map<SymbolId, IrType> seen;
    struct Requirement {
        IrType interface;
        SymbolId method;
    };
    std::map<std::string, std::vector<Requirement>> requirements;
    for (const auto& view : nominal_views(program_, nominal)) {
        if (symbol(view.symbol).kind != SymbolKind::Interface) continue;
        const auto [previous, added] = seen.emplace(view.symbol, view);
        if (!added && previous->second != view) {
            throw Diagnostic(symbol(id).span, "cannot inherit different instances of interface '" + view.name + "'");
        }
        // resolve() can add entries and reallocate entries_, which would move
        // the map being iterated, so iterate a copy of the names.
        const auto names = entries_[view.symbol].names;
        for (const auto& [name, method] : names) {
            if (symbol(method).kind != SymbolKind::Function || static_member(method)) continue;
            resolve(method);
            requirements[name].push_back({view, method});
        }
    }
    const bool is_class = symbol(id).kind == SymbolKind::Class;
    const auto signature = [&](SymbolId method, const IrType& view) {
        const auto replacements = receiver_arguments(view, symbol(method).owner);
        std::vector<IrType> types;
        for (const auto parameter : symbol(method).parameters) {
            types.push_back(substitute(symbol(parameter).type, replacements));
        }
        types.push_back(substitute(symbol(method).type, replacements));
        return types;
    };
    for (const auto& [name, methods] : requirements) {
        const auto required = signature(methods.front().method, methods.front().interface);
        for (const auto& method : methods) {
            if (signature(method.method, method.interface) != required) {
                throw Diagnostic(symbol(id).span, "incompatible interface requirements for '" + name + "'");
            }
        }
        const auto implementation = is_class ? member(id, name, false) : own_member(id, name);
        if (implementation != no_symbol) {
            resolve(implementation);
            const auto& candidate = symbol(implementation);
            if (candidate.kind != SymbolKind::Function || static_member(implementation) ||
                !candidate.exported || !candidate.generics.empty() ||
                signature(implementation, nominal) != required) {
                throw Diagnostic(candidate.span, "member '" + name + "' does not match the interface requirement");
            }
            continue;
        }
        std::size_t defaults = 0;
        for (const auto& method : methods) {
            if (!symbol(method.method).has_body) continue;
            const bool shadowed = std::ranges::any_of(methods, [&](const Requirement& other) {
                return other.interface != method.interface && assignable(other.interface, method.interface);
            });
            if (!shadowed) ++defaults;
        }
        if (defaults > 1) {
            throw Diagnostic(symbol(id).span, "multiple interface defaults for '" + name + "' require an override");
        }
        if (defaults == 0 && is_class) {
            throw Diagnostic(symbol(id).span, "class must declare interface method '" + name + "'");
        }
    }
}

IrNode Lowering::reference(SymbolId id, const Span& span, Context& context, bool implicit_receiver) {
    resolve(id);
    if (entries_[id].singleton_instance != no_symbol) id = entries_[id].singleton_instance;
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
    if (implicit_receiver && owner != no_symbol && context.receiver != no_symbol && !static_member(id)) {
        bool own_receiver = false;
        for (auto current = context.receiver; current != no_symbol; current = symbol(current).base)
            own_receiver |= current == owner;
        if (!own_receiver && class_or_interface(symbol(context.receiver).kind)) {
            const auto views = nominal_views(program_, symbol(context.receiver).type);
            own_receiver = std::ranges::find(views, owner, &IrType::symbol) != views.end();
        }
        if (own_receiver && (target.kind == SymbolKind::Field || target.kind == SymbolKind::Function ||
                             target.kind == SymbolKind::Global)) {
            result.operands.push_back(receiver(context.receiver, span, context));
        }
    }
    if (implicit_receiver && result.operands.empty() && owner != no_symbol &&
        class_or_interface(symbol(owner).kind) &&
        (target.kind == SymbolKind::Field || target.kind == SymbolKind::Function))
        throw Diagnostic(span, "instance member '" + target.name + "' requires a receiver");
    return result;
}

IrNode Lowering::select(const Expr& expr, Context& context) {
    const auto& operand = expr.operands.front();
    if (operand.kind == ExprKind::Name && operand.text == "super") {
        auto receiver = context.receiver;
        if (receiver == no_symbol || context.function == no_symbol || context.closure ||
            symbol(receiver).kind != SymbolKind::Class || symbol(receiver).base == no_symbol ||
            symbol(receiver).base == root_class_ ||
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
    if (nominal.kind != TypeKind::Object &&
        (receiver.op != IrOp::Reference ||
         (receiver.symbol != no_symbol && symbol(receiver.symbol).kind == SymbolKind::Variant)) &&
        intrinsic_value_member(expr.text) && supports_value_members(program_, nominal)) {
        // Ensure the root method contracts are available when a generic
        // instantiation or a composite operation encounters a class value.
        root_class();
        auto result = node(IrOp::Reference, expr.span, {TypeKind::Callable, expr.text, no_symbol, {}});
        result.operands.push_back(std::move(receiver));
        return result;
    }
    if (nominal.kind == TypeKind::String && expr.text == "byte_length") {
        auto result = node(IrOp::TextLength, expr.span, int32_type);
        result.operands.push_back(std::move(receiver));
        return result;
    }
    if ((nominal.kind == TypeKind::String && (expr.text == "byte_at" || expr.text == "slice")) ||
        (nominal.kind == TypeKind::Float && expr.text == "to_bits")) {
        auto result = node(IrOp::Reference, expr.span, {TypeKind::Callable, expr.text, no_symbol, {}});
        result.operands.push_back(std::move(receiver));
        return result;
    }
    if (collection(nominal)) {
        if (expr.text == "length") {
            auto result = node(IrOp::Length, expr.span, int32_type);
            result.operands.push_back(std::move(receiver));
            return result;
        }
        if (nominal.kind == TypeKind::List && expr.text == "version") {
            auto result = node(IrOp::CollectionVersion, expr.span, uint64_type);
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
        (class_or_interface(symbol(receiver.symbol).kind) ||
         symbol(receiver.symbol).kind == SymbolKind::Alias);
    if (nominal.kind == TypeKind::Object && (expr.text == "init" || expr.text == "deinit"))
        throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy");
    const bool exported = !inside(lexical_owner(context), nominal.symbol);
    auto target = type_reference || nominal.kind != TypeKind::Object
        ? member(nominal.symbol, expr.text, exported) : no_symbol;
    if (target == no_symbol && nominal.kind == TypeKind::Object)
        target = value_member(receiver.type, expr.text, exported);
    if (target == no_symbol)
        throw Diagnostic(expr.span, "unknown or inaccessible member '" + expr.text + "' of " +
                                    describe(nominal));
    auto result = reference(target, expr.span, context, false);
    if (static_member(target)) {
        if (!type_reference) {
            const auto kind = symbol(target).kind;
            const auto declaring_type = symbol(symbol(target).owner).owner;
            if ((kind != SymbolKind::Global && kind != SymbolKind::Function) ||
                (kind == SymbolKind::Function && symbol(declaring_type).kind == SymbolKind::Interface)) {
                throw Diagnostic(expr.span, "static member '" + expr.text + "' requires a class or interface name");
            }
            result.operands.push_back(std::move(receiver));
        }
        return result;
    }
    const auto kind = symbol(target).kind;
    if (kind == SymbolKind::Variant) {
        if (!symbol(nominal.symbol).generics.empty() &&
            symbol(receiver.symbol).kind != SymbolKind::Alias &&
            operand.kind != ExprKind::TypeApplication)
            throw Diagnostic(expr.span, "qualify a generic enum variant with explicit type arguments");
        result.type = nominal;
    }
    if (kind == SymbolKind::Field || kind == SymbolKind::Function || kind == SymbolKind::Global) {
        if (type_reference)
            throw Diagnostic(expr.span, "member '" + expr.text + "' requires an instance");
        result.type = substitute(std::move(result.type), receiver_arguments(receiver.type, symbol(target).owner));
        result.operands.clear();
        result.operands.push_back(std::move(receiver));
    }
    return result;
}

void Lowering::arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                         const Substitution& replacements, Context& context) {
    auto params = target == no_symbol ? std::vector<SymbolId>{} : symbol(target).parameters;
    const auto* declaration = target == no_symbol ? nullptr : entries_[target].declaration;
    const auto* rest = declaration && !declaration->parameters.empty() &&
        declaration->parameters.back().variadic ? &declaration->parameters.back() : nullptr;
    const auto fixed = params.size() - (rest ? 1 : 0);
    const auto count = expr.operands.size() - first;
    if (count < fixed || (!rest && count != fixed))
        throw Diagnostic(expr.span, "expected " + std::string(rest ? "at least " : "") +
                                    std::to_string(fixed) + " argument(s)");
    for (std::size_t i = 0; i < fixed; ++i) {
        auto expected = substitute(symbol(params[i]).type, replacements);
        call.operands.push_back(value(expr.operands[i + first], context, &expected));
    }
    if (!rest) return;
    auto pack = node(IrOp::Collection, expr.span, substitute(symbol(params.back()).type, replacements));
    const auto& element = pack.type.arguments.front();
    for (std::size_t i = fixed; i < count; ++i) {
        const auto& input = expr.operands[i + first];
        pack.operands.push_back(rest->type.name == "[variadic argument]"
            ? pack_argument(input, element, context) : value(input, context, &element));
    }
    call.operands.push_back(std::move(pack));
}

IrNode Lowering::pack_argument(const Expr& expr, const IrType& argument, Context& context) {
    auto input = value(expr, context);
    if (input.type == argument) return input;
    auto variant_name = "Text";
    const IrType* numeric_type = nullptr;
    if (auto integer = specified_integer_format(input.type)) {
        variant_name = integer->is_signed ? "Signed" : "Unsigned";
        numeric_type = integer->is_signed ? &int64_type : &uint64_type;
    } else if (input.type.kind == TypeKind::Float) {
        if (input.type.name == "float16") variant_name = "Floating16";
        else if (input.type.name == "bfloat16") variant_name = "BFloating16";
        else if (input.type.name == "float32") variant_name = "Floating32";
        else variant_name = "Floating";
    } else if (input.type.kind == TypeKind::Bool) {
        variant_name = "Boolean";
    } else if (class_reference(input.type)) {
        variant_name = "Object";
    } else if (input.type != string_type) {
        if (!supports_value_members(program_, input.type))
            throw Diagnostic(expr.span, "cannot pack " + describe(input.type) +
                                        " as a variadic argument; pass its text representation explicitly");
        auto text = node(IrOp::ValueText, expr.span, string_type);
        text.operands.push_back(std::move(input));
        input = std::move(text);
    }
    if (numeric_type && *numeric_type != input.type) {
        auto widened = node(IrOp::Convert, expr.span, *numeric_type);
        widened.operands.push_back(std::move(input));
        input = std::move(widened);
    }
    auto packed = node(IrOp::EnumConstruct, expr.span, argument);
    packed.symbol = own_member(argument.symbol, variant_name);
    packed.operands.push_back(std::move(input));
    return packed;
}

Substitution Lowering::enum_arguments(const IrType& type) const {
    Substitution result;
    const auto& generics = symbol(type.symbol).generics;
    for (std::size_t i = 0; i < generics.size(); ++i)
        result.emplace(generics[i], type.arguments[i]);
    return result;
}

IrNode Lowering::enum_construct(const IrNode& target, const Expr& expr, Context& context) {
    const auto variant = target.symbol;
    const auto* payload = entries_[variant].declaration;
    const bool named = expr.kind == ExprKind::NamedVariant;
    const auto fields = symbol(variant).payload_types;
    if (!payload || fields.empty() || !expr.type_arguments.empty() || named != payload->named_payload)
        throw Diagnostic(expr.span, "use the variant's declared payload constructor");
    if (fields.size() != expr.operands.size() - 1)
        throw Diagnostic(expr.span, "expected " + std::to_string(fields.size()) + " enum payload field(s)");
    auto result = node(IrOp::EnumConstruct, expr.span, target.type);
    result.symbol = variant;
    const auto replacements = enum_arguments(target.type);
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
        const auto field = substitute(fields[index], replacements);
        result.operands.push_back(value(expr.operands[i + 1], context, &field));
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

std::optional<IrNode> Lowering::primitive_call(const Expr& expr, Context& context) {
    const auto& callee = expr.operands.front();
    if (callee.kind != ExprKind::Member || callee.operands.front().kind != ExprKind::Name) return std::nullopt;
    const auto& name = callee.operands.front().text;
    if (find(name, context, expr.span) != no_symbol) return std::nullopt;
    const auto target = primitive(name);
    if (!target) return std::nullopt;
    const bool from_utf8 = target->kind == TypeKind::String && callee.text == "from_utf8";
    const auto bits_type = callee.text == "from_bits" ? float_bits_type(*target) : std::nullopt;
    if (!from_utf8 && !bits_type) return std::nullopt;
    if (!expr.type_arguments.empty() || expr.operands.size() != 2)
        throw Diagnostic(expr.span, "expected one argument and no type arguments");
    const auto& input = expr.operands[1];
    if (bits_type) {
        auto result = node(IrOp::ReinterpretFloat, expr.span, *target);
        result.operands.push_back(value(input, context, &*bits_type));
        return result;
    }
    const auto byte_array = collection_type(TypeKind::Array, uint8_type);
    auto bytes = value(input, context, input.kind == ExprKind::Collection ? &byte_array : nullptr);
    if (!collection(bytes.type) || bytes.type.arguments.front() != uint8_type)
        throw Diagnostic(input.span, "string.from_utf8 requires an array or list of bytes");
    auto result = node(IrOp::TextFromUtf8, expr.span, {TypeKind::Tuple, {}, no_symbol, {bool_type, string_type}});
    result.operands.push_back(std::move(bytes));
    return result;
}

IrNode Lowering::call(const Expr& expr, Context& context) {
    if (auto result = primitive_call(expr, context)) return std::move(*result);
    auto callee = expression(expr.operands.front(), context);
    if (callee.op == IrOp::Reference && callee.symbol != no_symbol &&
        symbol(callee.symbol).kind == SymbolKind::Variant) {
        return enum_construct(callee, expr, context);
    }
    if (is_function_value(callee.type)) {
        if (!expr.type_arguments.empty()) throw Diagnostic(expr.span, "function values take no type arguments");
        const auto signature = callee.type.arguments;
        if (expr.operands.size() != signature.size()) {
            throw Diagnostic(expr.span, "expected " + std::to_string(signature.size() - 1) + " argument(s)");
        }
        auto result = node(IrOp::CallClosure, expr.span, signature.back());
        result.operands.push_back(std::move(callee));
        for (std::size_t i = 1; i < expr.operands.size(); ++i) {
            result.operands.push_back(value(expr.operands[i], context, &signature[i - 1]));
        }
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
        if (target == no_symbol) require_implicit_constructor(callee.operands.front().type.symbol, expr.span);
        result.op = IrOp::BaseInitialize;
        result.operands = std::move(callee.operands);
        arguments(result, expr, 1, target, {}, context);
        return result;
    }
    if (target == no_symbol) {
        if (const auto operation = intrinsic_value_member(callee.type.name)) {
            const bool equality = *operation == IrOp::ValueEquals;
            if (!expr.type_arguments.empty() || expr.operands.size() != (equality ? 2U : 1U))
                throw Diagnostic(expr.span, equality ? "equals requires one argument" :
                                                      "hash_code and to_string take no arguments");
            auto input = std::move(callee.operands.front());
            const auto input_type = input.type;
            result.op = *operation;
            result.type = equality ? bool_type : *operation == IrOp::ValueHash ? uint32_type : string_type;
            result.operands.push_back(std::move(input));
            if (equality) result.operands.push_back(value(expr.operands[1], context, &input_type));
            return result;
        }
        if (callee.type.name == "to_bits" || callee.type.name == "byte_at" || callee.type.name == "slice") {
            const auto& method = callee.type.name;
            const std::size_t count = method == "to_bits" ? 0 : method == "byte_at" ? 1 : 2;
            if (!expr.type_arguments.empty() || expr.operands.size() != count + 1)
                throw Diagnostic(expr.span, "expected " + std::to_string(count) + " argument(s) and no type arguments");
            auto input = std::move(callee.operands.front());
            if (method == "to_bits") {
                result.op = IrOp::ReinterpretFloat;
                result.type = float_bits_type(input.type).value();
            } else if (method == "byte_at") {
                result.op = IrOp::TextByteAt;
                result.type = uint8_type;
            } else {
                result.op = IrOp::TextSlice;
                result.type = string_type;
            }
            result.operands.push_back(std::move(input));
            for (std::size_t index = 1; index <= count; ++index)
                result.operands.push_back(value(expr.operands[index], context, &int32_type));
            return result;
        }
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
    auto qualifier = static_qualifier(callee);
    auto replacements = callee.operands.empty() ? Substitution{}
        : receiver_arguments(callee.operands.front().type, symbol(target).owner);
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
    return qualifier ? discard_before(std::move(*qualifier), std::move(result)) : std::move(result);
}

IrNode Lowering::allocate(const Expr& expr, Context& context) {
    auto object = type(*expr.type, context);
    if (object.kind != TypeKind::Object || symbol(object.symbol).kind != SymbolKind::Class)
        throw Diagnostic(expr.span, "new requires a class type");
    const auto id = object.symbol;
    if (entries_[id].singleton_instance != no_symbol)
        throw Diagnostic(expr.span, "a singleton cannot be constructed with new");
    if (id == root_class_)
        throw Diagnostic(expr.span, "class is an umbrella type and cannot be constructed");
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
        require_implicit_constructor(id, expr.span);
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
           (equality && (type.kind == TypeKind::Bool || type.kind == TypeKind::Object ||
                         type.kind == TypeKind::String ||
                         (type.kind == TypeKind::Enum && !has_enum_payload(program, type.symbol))));
}

IrNode arithmetic(std::string op, const Span& span, IrNode left, IrNode right) {
    const auto kind = left.type.kind;
    const bool valid = op == "%" || is_bitwise_binary(op) ? kind == TypeKind::Integer
        : kind == TypeKind::Integer || kind == TypeKind::Float ||
          (kind == TypeKind::String && op == "+");
    if (!valid)
        throw Diagnostic(span, "operator '" + op + "' is not defined for " + describe(left.type));
    if (is_shift(op) && right.type.kind != TypeKind::Integer)
        throw Diagnostic(right.span, "a shift count must be an integer");
    auto result = node(IrOp::Binary, span, left.type);
    result.text = std::move(op);
    result.operands.push_back(std::move(left));
    result.operands.push_back(std::move(right));
    return result;
}

IrNode Lowering::binary(const Expr& expr, Context& context, const IrType* expected) {
    const auto& op = expr.text;
    if (op == "===") {
        // Use the same lookup, dispatch, and argument checks as an explicit call.
        Expr member{ExprKind::Member, expr.span, "equals", {expr.operands[0]}, {}, {}};
        Expr invocation{ExprKind::Call, expr.span, {}, {std::move(member), expr.operands[1]}, {}, {}};
        auto result = call(invocation, context);
        require_type(result.type, bool_type, expr.span);
        return result;
    }
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
    } else if (numeric_literal(lhs) && !numeric_literal(rhs)) {
        right = value(rhs, context, comparison ? nullptr : expected);
        left = value(lhs, context, &right.type);
    } else {
        left = value(lhs, context, comparison ? nullptr : expected);
        if (equality && class_reference(left.type)) {
            const auto root = symbol(root_class()).type;
            right = value(rhs, context, &root);
        } else {
            right = value(rhs, context, &left.type);
        }
    }
    if (!comparison)
        return arithmetic(op, expr.span, std::move(left), std::move(right));
    if (!comparable(program_, receiver_type(left.type), op))
        throw Diagnostic(expr.span, "operator '" + op + "' is not defined for " + describe(left.type));
    const auto op_kind = equality && left.type.kind == TypeKind::String ? IrOp::TextEqual : IrOp::Binary;
    auto result = node(op_kind, expr.span, bool_type);
    result.text = op;
    result.operands.push_back(std::move(left));
    result.operands.push_back(std::move(right));
    return result;
}

IrNode Lowering::conditional(const Expr& expr, Context& context, const IrType* expected) {
    auto condition = value(expr.operands[0], context, &bool_type);
    const auto& yes = expr.operands[1];
    const auto& no = expr.operands[2];
    auto result = node(IrOp::SelectValue, expr.span);
    result.operands.push_back(std::move(condition));
    if (!expected && ((numeric_literal(yes) && !numeric_literal(no)) || yes.kind == ExprKind::Nil)) {
        auto when_false = value(no, context);
        result.type = when_false.type;
        result.body.push_back(value(yes, context, &result.type));
        result.alternative.push_back(std::move(when_false));
    } else {
        auto when_true = value(yes, context, expected);
        result.type = expected ? *expected : when_true.type;
        result.body.push_back(std::move(when_true));
        result.alternative.push_back(value(no, context, &result.type));
    }
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
    if (!comparable(program_, receiver_type(subject_type), expr.text)) {
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
    const auto comparison_type = class_reference(subject_type) ? symbol(root_class()).type : subject_type;

    auto compare = [&](std::size_t index) {
        const auto& input = expr.operands[index];
        auto result = node(subject_type.kind == TypeKind::String ? IrOp::TextEqual : IrOp::Binary,
                           input.span, bool_type);
        result.text = expr.text;
        auto load = node(IrOp::Load, subject_expr.span, subject_type);
        load.symbol = local;
        result.operands.push_back(std::move(load));
        result.operands.push_back(index == 1 && first ? std::move(*first) : value(input, context, &comparison_type));
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

IrNode Lowering::range(const Expr& expr, Context& context, const IrType* expected) {
    const auto& span = expr.span;
    const auto* expected_element = expected && collection(*expected) ? &expected->arguments.front() : nullptr;
    auto start = value(expr.operands[0], context, expected_element);
    const auto element = start.type;
    const auto format = specified_integer_format(element);
    if (!format) throw Diagnostic(start.span, "a range requires integer bounds and step");
    auto result = node(IrOp::ScopedValue, span,
                       expected_element ? *expected : collection_type(TypeKind::Array, element));
    const auto integer = [&](const IrType& type, std::string digits) {
        auto literal = node(IrOp::Integer, span, type);
        literal.text = std::move(digits);
        return literal;
    };
    const auto binary = [&](std::string op, const IrType& type, IrNode left, IrNode right) {
        auto operation = node(IrOp::Binary, span, type);
        operation.text = std::move(op);
        operation.operands = {std::move(left), std::move(right)};
        return operation;
    };
    const auto convert = [&](const IrType& type, IrNode operand) {
        if (operand.type == type) return operand;
        auto operation = node(IrOp::Convert, span, type);
        operation.operands.push_back(std::move(operand));
        return operation;
    };
    const auto select = [&](IrNode condition, IrNode yes, IrNode no) {
        auto selection = node(IrOp::SelectValue, span, yes.type);
        selection.operands.push_back(std::move(condition));
        selection.body.push_back(std::move(yes));
        selection.alternative.push_back(std::move(no));
        return selection;
    };
    const auto save = [&](IrNode initializer, std::string name, bool constant = true) {
        return save_temporary(std::move(initializer), result.body, context, std::move(name), constant);
    };
    const auto store = [&](IrNode place, IrNode value) {
        auto operation = node(IrOp::Store, span);
        operation.operands = {std::move(place), std::move(value)};
        return operation;
    };

    const auto current = save(std::move(start), "$range_current", false);
    const auto stop = save(value(expr.operands[1], context, &element), "$range_stop");
    const auto step = save(expr.operands.size() == 3 ? value(expr.operands[2], context, &element)
                                                   : integer(element, "1"), "$range_step");
    const auto& distance_type = format->bits <= 32 ? uint32_type : uint64_type;
    const auto zero = integer(distance_type, "0");
    const auto one = integer(distance_type, "1");

    // Signed ranks preserve order and let even the full int64 distance fit in
    // uint64. Negate the stride one short of its magnitude to handle INT64_MIN.
    const auto rank = [&](IrNode bound) {
        if (!format->is_signed) return convert(distance_type, std::move(bound));
        const auto signed_bound = convert(int64_type, std::move(bound));
        if (format->bits <= 32) {
            const auto bias = std::to_string(std::uint64_t{1} << (format->bits - 1));
            return convert(distance_type, binary("+", int64_type, signed_bound, integer(int64_type, bias)));
        }
        return select(binary("<", bool_type, signed_bound, integer(int64_type, "0")),
                      convert(uint64_type, binary("-", int64_type, signed_bound,
                                                 integer(int64_type, "-9223372036854775808"))),
                      binary("+", uint64_type, convert(uint64_type, signed_bound),
                             integer(uint64_type, "9223372036854775808")));
    };
    const auto from_rank = save(rank(current), "$range_from_rank");
    const auto to_rank = save(rank(stop), "$range_to_rank");
    auto low = from_rank;
    auto high = to_rank;
    auto stride = convert(distance_type, step);
    if (format->is_signed) {
        const auto descending = save(binary("<", bool_type, step, integer(element, "0")), "$range_descending");
        low = save(select(descending, to_rank, from_rank), "$range_low");
        high = save(select(descending, from_rank, to_rank), "$range_high");
        const auto signed_step = convert(int64_type, step);
        auto magnitude = convert(distance_type, binary("-", int64_type,
            integer(int64_type, format->bits <= 32 ? "0" : "-1"), signed_step));
        if (format->bits == 64) magnitude = binary("+", distance_type, std::move(magnitude), one);
        stride = select(descending, std::move(magnitude), std::move(stride));
    }
    const auto has_items = save(binary("<", bool_type, low, high), "$range_nonempty");
    auto last_offset = select(has_items, binary("-", distance_type,
                             binary("-", distance_type, high, low), one), zero);
    // The division also checks a zero step for an empty range.
    auto count = binary("+", distance_type,
                        binary("/", distance_type, std::move(last_offset), std::move(stride)),
                        select(has_items, one, zero));
    const auto length = save(convert(int32_type, std::move(count)), "$range_length");
    auto allocation = node(IrOp::RepeatCollection, span, result.type);
    allocation.operands = {convert(uint64_type, length), current};
    const auto items = save(std::move(allocation), "$range_items");
    const auto index = save(integer(int32_type, "0"), "$range_index", false);

    auto loop = node(IrOp::Loop, span);
    const auto in_range = binary("<", bool_type, index, length);
    loop.operands.push_back(in_range);
    auto target = node(IrOp::Index, span, element);
    target.operands = {items, index};
    loop.body.push_back(store(std::move(target), current));
    loop.body.push_back(store(index, binary("+", int32_type, index, integer(int32_type, "1"))));
    auto advance = node(IrOp::Branch, span);
    advance.operands.push_back(in_range);
    advance.body.push_back(store(current, binary("+", element, current, step)));
    loop.body.push_back(std::move(advance));
    result.body.push_back(std::move(loop));
    result.body.push_back(items);
    return result;
}

IrNode Lowering::expression(const Expr& expr, Context& context, const IrType* expected) {
    switch (expr.kind) {
    case ExprKind::TypeApplication: {
        auto target = expression(expr.operands.front(), context);
        if (target.op != IrOp::Reference || target.symbol == no_symbol ||
            symbol(target.symbol).kind != SymbolKind::Enum)
            throw Diagnostic(expr.span, "a parameterized type reference requires a generic enum");
        target.type = apply_arguments(target.type,
            type_arguments(symbol(target.symbol).generics, expr.type_arguments, expr.span, context));
        target.span = expr.span;
        return target;
    }
    case ExprKind::Lambda:
        return lambda(*expr.lambda, context);
    case ExprKind::Name: {
        SymbolId promoted_receiver = no_symbol;
        auto id = find(expr.text, context, expr.span, &promoted_receiver);
        if (id == no_symbol) throw Diagnostic(expr.span, "unknown name '" + expr.text + "'");
        if (symbol(id).kind == SymbolKind::Function &&
            owner_kind(symbol(id).owner, SymbolKind::Class) != no_symbol &&
            (expr.text == "init" || expr.text == "deinit")) {
            throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy");
        }
        auto result = reference(id, expr.span, context, promoted_receiver == no_symbol);
        if (promoted_receiver != no_symbol &&
            (symbol(id).kind == SymbolKind::Function || symbol(id).kind == SymbolKind::Global)) {
            auto module = node(IrOp::Reference, expr.span, symbol(promoted_receiver).type);
            module.symbol = promoted_receiver;
            result.operands.push_back(std::move(module));
        }
        return result;
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
    case ExprKind::Nil: {
        if (expected && !class_reference(*expected))
            throw Diagnostic(expr.span, "nil requires a class reference type");
        const auto target = expected ? *expected : symbol(root_class()).type;
        return node(IrOp::Nil, expr.span, target);
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
    case ExprKind::Conditional:
        return conditional(expr, context, expected);
    case ExprKind::PrefixUpdate:
    case ExprKind::PostfixUpdate:
        return update(expr, context);
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
    case ExprKind::Range:
        return range(expr, context, expected);
    case ExprKind::New:
        return allocate(expr, context);
    case ExprKind::NamedVariant: {
        const auto target = expression(expr.operands.front(), context);
        if (target.op != IrOp::Reference || target.symbol == no_symbol ||
            symbol(target.symbol).kind != SymbolKind::Variant) {
            throw Diagnostic(expr.span, "expected a named enum variant");
        }
        return enum_construct(target, expr, context);
    }
    case ExprKind::Match:
        return lower_match(*expr.match, context, true, expected);
    case ExprKind::TypeTest:
    case ExprKind::Cast: {
        auto target = type(*expr.type, context);
        auto source = value(expr.operands.front(), context);
        if (class_reference(source.type) && class_reference(target)) {
            const auto test = expr.kind == ExprKind::TypeTest;
            auto result = node(test ? IrOp::ClassIs : IrOp::ClassCast, expr.span, test ? bool_type : target);
            if (test) result.type_arguments.push_back(target);
            result.operands.push_back(std::move(source));
            return result;
        }
        if (expr.kind == ExprKind::TypeTest)
            throw Diagnostic(expr.span, "is requires class reference types");
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
    if (result.op == IrOp::Reference && result.type.kind == TypeKind::Callable && result.symbol != no_symbol &&
        symbol(result.symbol).kind == SymbolKind::Function) {
        result = bind_function(std::move(result), context);
    }
    if (result.type.kind == TypeKind::Void || result.type.kind == TypeKind::Module ||
        (result.type.kind == TypeKind::Callable && !is_function_value(result.type)) ||
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

IrNode Lowering::discard_binding(const Declaration& decl, Context& context) {
    if (!decl.initializer)
        throw Diagnostic(decl.span, "'_' requires a value initializer");
    std::optional<IrType> expected;
    if (decl.type) expected = type(*decl.type, context);
    auto result = node(IrOp::Discard, decl.span);
    result.operands.push_back(value(*decl.initializer, context, expected ? &*expected : nullptr));
    return result;
}

IrNode Lowering::save_temporary(IrNode initializer, std::vector<IrNode>& instructions,
                                Context& context, std::string name, bool constant) {
    auto owner = context.owner;
    if (symbol(owner).kind == SymbolKind::Global || symbol(owner).kind == SymbolKind::Field)
        owner = symbol(owner).owner;
    const auto id = add(SymbolKind::Local, std::move(name), initializer.span, owner);
    symbol(id).type = initializer.type;
    symbol(id).constant = constant;
    auto load = node(IrOp::Load, initializer.span, initializer.type);
    load.symbol = id;
    auto init = node(IrOp::Initialize, initializer.span);
    init.symbol = id;
    init.operands.push_back(std::move(initializer));
    instructions.push_back(std::move(init));
    return load;
}

IrNode Lowering::capture_place(const Expr& expr, std::vector<IrNode>& instructions, Context& context) {
    auto destination = expression(expr, context);
    place(destination);
    if (destination.op == IrOp::Load && !destination.operands.empty() &&
        destination.operands.front().type.kind != TypeKind::Module) {
        auto discard = node(IrOp::Discard, destination.operands.front().span);
        discard.operands.push_back(std::move(destination.operands.front()));
        instructions.push_back(std::move(discard));
        destination.operands.clear();
    } else if (destination.op == IrOp::Index || destination.op == IrOp::Field) {
        for (auto& operand : destination.operands)
            operand = save_temporary(std::move(operand), instructions, context, "$update_place");
    }
    return destination;
}

IrNode Lowering::update(const Expr& expr, Context& context) {
    auto result = node(IrOp::ScopedValue, expr.span);
    auto destination = capture_place(expr.operands.front(), result.body, context);
    const auto kind = destination.type.kind;
    if (kind != TypeKind::Integer && kind != TypeKind::Float)
        throw Diagnostic(expr.span, "'" + expr.text + "' requires a mutable numeric operand");
    result.type = destination.type;

    const bool postfix = expr.kind == ExprKind::PostfixUpdate;
    auto previous = postfix ? save_temporary(destination, result.body, context, "$update_old") : destination;
    auto one = node(kind == TypeKind::Integer ? IrOp::Integer : IrOp::Float, expr.span, result.type);
    one.text = "1";
    auto updated = arithmetic(expr.text == "++" ? "+" : "-", expr.span, previous, std::move(one));
    if (!postfix)
        updated = save_temporary(std::move(updated), result.body, context, "$update_new");
    auto store = node(IrOp::Store, expr.span);
    store.operands.push_back(std::move(destination));
    store.operands.push_back(updated);
    result.body.push_back(std::move(store));
    result.body.push_back(postfix ? std::move(previous) : std::move(updated));
    return result;
}

IrNode Lowering::statement(const Statement& stmt, Context& context) {
    switch (stmt.kind) {
    case StmtKind::Binding: {
        const auto& declaration = *stmt.declaration;
        if (declaration.kind != DeclKind::Binding || declaration.exported ||
            declaration.override_member || declaration.promote_import || declaration.static_member) {
            throw Diagnostic(declaration.span, "expected a local value binding");
        }
        if (declaration.name == "_") return discard_binding(declaration, context);
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
        const auto& syntax = stmt.expressions.front();
        if (syntax.kind == ExprKind::Call && syntax.operands.front().kind == ExprKind::Name &&
            syntax.operands.front().text == "trap_with_message" &&
            find("trap_with_message", context, syntax.span) == no_symbol) {
            if (!syntax.type_arguments.empty() || syntax.operands.size() != 2)
                throw Diagnostic(syntax.span, "trap_with_message requires one string argument and no type arguments");
            auto result = node(IrOp::TrapMessage, syntax.span);
            result.operands.push_back(value(syntax.operands[1], context, &string_type));
            return result;
        }
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
        if (stmt.assignment_operator != "=") {
            auto result = node(IrOp::Block, stmt.span);
            auto destination = capture_place(stmt.expressions[0], result.body, context);
            const auto op = stmt.assignment_operator.substr(0, stmt.assignment_operator.size() - 1);
            auto right = value(stmt.expressions[1], context, is_shift(op) ? nullptr : &destination.type);
            auto source = arithmetic(op, stmt.span, destination, std::move(right));
            auto store = node(IrOp::Store, stmt.span);
            store.operands.push_back(std::move(destination));
            store.operands.push_back(std::move(source));
            result.body.push_back(std::move(store));
            return result;
        }
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
    case StmtKind::Do:
        return do_loop(stmt, context);
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
        if (object.type.kind != TypeKind::Object) {
            throw Diagnostic(object.span, "destroy requires an object");
        }
        if (entries_[object.type.symbol].singleton_instance != no_symbol)
            throw Diagnostic(object.span, "a singleton cannot be destroyed");
        auto result = node(IrOp::Destroy, stmt.span);
        result.symbol = object.type.symbol;
        result.operands.push_back(std::move(object));
        return result;
    }
    }
    throw std::logic_error("invalid statement kind");
}

bool has_loop_transfer(const std::vector<IrNode>& region) {
    return std::ranges::any_of(region, [](const IrNode& item) {
        return item.op == IrOp::Break || item.op == IrOp::Continue ||
            ((item.op == IrOp::Block || item.op == IrOp::Branch) &&
             (has_loop_transfer(item.body) || has_loop_transfer(item.alternative)));
    });
}

IrNode Lowering::do_loop(const Statement& stmt, Context& context) {
    std::optional<IrNode> condition;
    if (!stmt.expressions.empty())
        condition = value(stmt.expressions.front(), context, &bool_type);
    ++context.loops;
    auto instructions = body(stmt.body, context);
    --context.loops;
    auto always = node(IrOp::Boolean, stmt.span, bool_type);
    always.text = "true";
    auto loop = node(IrOp::Loop, stmt.span);
    if (!condition || (condition->op == IrOp::Boolean && condition->text == "true") ||
        (always_returns(instructions) && !has_loop_transfer(instructions))) {
        // The last case cannot reach the test, but it was still type-checked.
        loop.operands.push_back(std::move(always));
        loop.body = std::move(instructions);
        return loop;
    }

    // The first visit to the head bypasses the condition. Normal fallthrough
    // and continue re-enter the head after the body has cleared the flag.
    auto result = node(IrOp::Block, stmt.span);
    auto first = save_temporary(std::move(always), result.body, context, "$do_first", false);
    auto test = node(IrOp::ShortCircuit, condition->span, bool_type);
    test.text = "||";
    test.operands.push_back(first);
    test.body.push_back(std::move(*condition));
    loop.operands.push_back(std::move(test));
    auto no = node(IrOp::Boolean, stmt.span, bool_type);
    no.text = "false";
    auto clear = node(IrOp::Store, stmt.span);
    clear.operands.push_back(std::move(first));
    clear.operands.push_back(std::move(no));
    loop.body.push_back(std::move(clear));
    for (auto& instruction : instructions)
        loop.body.push_back(std::move(instruction));
    result.body.push_back(std::move(loop));
    return result;
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
        return for_each_iterable(stmt, context, std::move(items));
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

IrNode Lowering::for_each_iterable(const Statement& stmt, Context& context, IrNode items) {
    const auto views = nominal_views(program_, items.type);
    const auto iterable = std::ranges::find_if(views, [&](const IrType& view) {
        return view.kind == TypeKind::Object && symbol(view.symbol).kind == SymbolKind::Interface &&
               symbol(view.symbol).name == "Iterable" && view.arguments.size() == 1;
    });
    if (iterable == views.end()) {
        throw Diagnostic(items.span, "for-each requires an array or list or Iterable<T>, got " +
                                     describe(items.type));
    }
    const auto invoke = [&](const IrType& view, std::string_view name, IrNode receiver) {
        const auto method = value_member(view, name, true);
        if (method == no_symbol) {
            throw Diagnostic(stmt.span, describe(view) + " must provide '" + std::string(name) + "()'");
        }
        resolve(method);
        const auto& declaration = symbol(method);
        if (declaration.kind != SymbolKind::Function || !declaration.generics.empty() ||
            !declaration.parameters.empty()) {
            throw Diagnostic(stmt.span, std::string(name) + "() must take no arguments");
        }
        auto call = node(IrOp::VirtualCall, stmt.span, substitute(declaration.type,
                         receiver_arguments(view, declaration.owner)));
        call.symbol = method;
        call.operands.push_back(std::move(receiver));
        return call;
    };

    auto iterator = invoke(*iterable, "iterator", std::move(items));
    const auto iterator_type = iterator.type;
    const auto iterator_id = add(SymbolKind::Local, "$iterator", stmt.span, context.owner);
    symbol(iterator_id).type = iterator_type;
    symbol(iterator_id).constant = true;
    auto result = node(IrOp::Block, stmt.span);
    auto initialize = node(IrOp::Initialize, stmt.span);
    initialize.symbol = iterator_id;
    initialize.operands.push_back(std::move(iterator));
    result.body.push_back(std::move(initialize));
    auto load = node(IrOp::Load, stmt.span, iterator_type);
    load.symbol = iterator_id;
    auto condition = invoke(iterator_type, "has_next", load);
    require_type(condition.type, bool_type, stmt.span);
    auto next = invoke(iterator_type, "next", std::move(load));
    const auto element = iterable->arguments.front();
    require_type(next.type, element, stmt.span);

    const auto& declaration = *stmt.declaration;
    auto declared = declaration.type ? type(*declaration.type, context) : element;
    require_type(element, declared, declaration.span);
    const auto element_id = add(SymbolKind::Local, declaration.name, declaration.span, context.owner);
    symbol(element_id).type = declared;
    symbol(element_id).constant = declaration.constant;
    auto bind = node(IrOp::Initialize, stmt.span);
    bind.symbol = element_id;
    bind.operands.push_back(std::move(next));
    auto loop = node(IrOp::Loop, stmt.span);
    loop.operands.push_back(std::move(condition));
    loop.body.push_back(std::move(bind));
    context.scopes.emplace_back();
    context.scopes.back().emplace(declaration.name, element_id);
    ++context.loops;
    for (const auto& statement : stmt.body) {
        append_statement(loop.body, statement, context);
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
    const auto replacements = enum_arguments(enum_type);
    for (const auto& arm : stmt.arms) {
        if (wildcard)
            throw Diagnostic(arm.span, "a wildcard match arm must be last");
        wildcard = arm.variant == "_";
        const auto variant = wildcard ? no_symbol : qualified(arm.variant, arm.span, context);
        if (!wildcard && (symbol(variant).kind != SymbolKind::Variant || symbol(variant).owner != enum_type.symbol))
            throw Diagnostic(arm.span, "match arm must name a variant of " + describe(enum_type));
        if (const auto separator = arm.variant.rfind('.'); !wildcard && separator != std::string::npos) {
            const auto qualifier = qualified(arm.variant.substr(0, separator), arm.span, context);
            if (symbol(qualifier).kind == SymbolKind::Alias && symbol(qualifier).type != enum_type)
                throw Diagnostic(arm.span, "match arm must use an overlay of " + describe(enum_type));
        }
        if (!wildcard && !seen.insert(variant).second)
            throw Diagnostic(arm.span, "duplicate match arm for '" + arm.variant + "'");
        auto fields = wildcard ? std::vector<IrType>{} : symbol(variant).payload_types;
        for (auto& field : fields) field = substitute(std::move(field), replacements);
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
    if (entries_[owner].singleton_instance != no_symbol &&
        (decl.name == "deinit" || !decl.parameters.empty()))
        throw Diagnostic(decl.span, "a singleton can declare only a zero-argument init lifecycle function");
    if (!decl.has_body || !decl.generics.empty() || symbol(id).type.kind != TypeKind::Void)
        throw Diagnostic(decl.span, "a lifecycle function needs a body, no type parameters, and void return");
    if (decl.name == "deinit" && !decl.parameters.empty())
        throw Diagnostic(decl.span, "deinit takes no parameters");
    if (decl.name != "init" || symbol(owner).base == no_symbol || symbol(owner).base == root_class_) return;
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

void Lowering::require_implicit_constructor(SymbolId cls, const Span& span) {
    for (auto base = symbol(cls).base; base != no_symbol; base = symbol(base).base) {
        const auto inherited = own_member(base, "init");
        if (inherited == no_symbol) continue;
        resolve(inherited);
        if (!symbol(inherited).parameters.empty())
            throw Diagnostic(span, "class '" + symbol(cls).name + "' needs a constructor to initialize its base");
        return;
    }
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
    } else if (declaration.kind == DeclKind::Module || class_or_interface(symbol(id).kind)) {
        if (symbol(id).kind == SymbolKind::Interface) {
            for (const auto& member : declaration.members) {
                if (member.static_member) continue;
                if (member.kind != DeclKind::Function || !member.generics.empty() ||
                    member.name == "init" || member.name == "deinit") {
                    throw Diagnostic(member.span, "interfaces may declare only non-generic methods");
                }
            }
        }
        if (class_or_interface(symbol(id).kind)) check_interfaces(id);
        std::vector<IrNode> initializers;
        std::vector<IrNode> static_initializers;
        for (const auto& member : declaration.members) {
            if (member.name == "_") {
                if (member.initializer->kind != ExprKind::Import) {
                    auto ctx = context(id);
                    initializers.push_back(discard_binding(member, ctx));
                }
                continue;
            }
            auto child = own_member(id, member.name);
            lower_declaration(child);
            if (auto& initializer = entries_[child].initializer) {
                auto instruction = node(IrOp::Initialize, member.span);
                const auto instance = entries_[child].singleton_instance;
                instruction.symbol = instance == no_symbol ? child : instance;
                instruction.operands.push_back(std::move(*initializer));
                auto& region = member.static_member ? static_initializers : initializers;
                region.push_back(std::move(instruction));
            }
        }
        if (!initializers.empty()) program_.bodies.push_back({id, std::move(initializers)});
        if (!static_initializers.empty())
            program_.bodies.push_back({entries_[id].static_scope, std::move(static_initializers)});
        if (entries_[id].singleton_instance != no_symbol) {
            for (const auto& member : declaration.members) {
                if (member.kind == DeclKind::Function && !member.has_body)
                    throw Diagnostic(member.span, "a singleton cannot have an abstract method");
            }
            auto instance = node(IrOp::Allocate, declaration.span, symbol(id).type);
            instance.symbol = own_member(id, "init");
            entries_[id].initializer = std::move(instance);
            // An umbrella or interface reference must not permit destroying
            // the singleton after its concrete type has been erased.
            const auto cleanup = add(SymbolKind::Function, "deinit", declaration.span, id);
            symbol(cleanup).has_body = true;
            auto trap = node(IrOp::TrapMessage, declaration.span);
            auto message = node(IrOp::String, declaration.span, string_type);
            message.text = "a singleton cannot be destroyed";
            trap.operands.push_back(std::move(message));
            program_.bodies.push_back({cleanup, {std::move(trap)}});
        }
    }
}

IrProgram Lowering::run(const std::vector<ParsedFile>& files) {
    files_ = &files;
    for (const auto& file : files) {
        Span span;
        span.file = file.file;
        auto file_id = add(SymbolKind::File, file.file, span);
        symbol(file_id).library_path = file.library_path;
        file_ids_.push_back(file_id);
        for (const auto& stmt : file.statements) {
            if (stmt.kind == StmtKind::Binding) {
                if (file.library_path.empty() && stmt.declaration->kind == DeclKind::Module &&
                    stmt.declaration->name == "Std")
                    throw Diagnostic(stmt.declaration->span, "the Std module namespace is reserved");
                declare(*stmt.declaration, file_id, SymbolKind::Global);
            }
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
        for (const auto& stmt : files[i].statements) {
            if (stmt.kind == StmtKind::Binding) prepare_promotions(*stmt.declaration, file_ids_[i]);
        }
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto id = file_ids_[i];
        auto ctx = context(id);
        std::vector<IrNode> instructions;
        for (const auto& stmt : files[i].statements) {
            if (stmt.kind == StmtKind::Binding) {
                if (stmt.declaration->name == "_") {
                    if (stmt.declaration->initializer->kind != ExprKind::Import)
                        instructions.push_back(discard_binding(*stmt.declaration, ctx));
                    continue;
                }
                auto target = own_member(id, stmt.declaration->name);
                lower_declaration(target);
                if (auto& initializer = entries_[target].initializer) {
                    auto instruction = node(IrOp::Initialize, stmt.span);
                    const auto instance = entries_[target].singleton_instance;
                    instruction.symbol = instance == no_symbol ? target : instance;
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
    std::set<SymbolId> singletons;
    for (const auto& entry : entries_) {
        if (entry.singleton_instance != no_symbol) singletons.insert(entry.singleton_instance);
    }
    lower_static_initializers(program_, singletons);
    return std::move(program_);
}

} // namespace

IrProgram lower(const std::vector<ParsedFile>& files) {
    return Lowering{}.run(files);
}

} // namespace neura
