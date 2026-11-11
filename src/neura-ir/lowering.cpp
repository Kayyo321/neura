#include "aggregate.hpp"
#include "attributes.hpp"
#include "control_flow.hpp"
#include "definite_assignment.hpp"
#include "float.hpp"
#include "host_binding.hpp"
#include "kernel_signature.hpp"
#include "scheduler_ops.hpp"
#include "integer.hpp"
#include "ir.hpp"
#include "linker.hpp"
#include "nominal.hpp"
#include "reference_initialization.hpp"
#include "reference_storage.hpp"
#include "static_initialization.hpp"
#include "tags.hpp"
#include "parser/syntax.hpp"
#include "util/source_map.hpp"
#include "util/suggest.hpp"

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

struct PrimitiveWrapper {
    std::string_view module;
    std::string_view path;
};

std::optional<PrimitiveWrapper> primitive_wrapper(const IrType& type) {
    static constexpr std::pair<std::string_view, PrimitiveWrapper> wrappers[] = {
        {"bool", {"Bool", "std/core/bool.nra"}},
        {"string", {"String", "std/core/string.nra"}},
        {"int8", {"Int8", "std/core/int8.nra"}},
        {"int16", {"Int16", "std/core/int16.nra"}},
        {"int32", {"Int32", "std/core/int32.nra"}},
        {"int64", {"Int64", "std/core/int64.nra"}},
        {"uint8", {"UInt8", "std/core/u-int8.nra"}},
        {"uint16", {"UInt16", "std/core/u-int16.nra"}},
        {"uint32", {"UInt32", "std/core/u-int32.nra"}},
        {"uint64", {"UInt64", "std/core/u-int64.nra"}},
        {"float16", {"Float16", "std/core/float16.nra"}},
        {"bfloat16", {"BFloat16", "std/core/b-float16.nra"}},
        {"float32", {"Float32", "std/core/float32.nra"}},
        {"float64", {"Float64", "std/core/float64.nra"}},
    };
    if (type.kind != TypeKind::Bool && type.kind != TypeKind::String &&
        type.kind != TypeKind::Integer && type.kind != TypeKind::Float) return std::nullopt;
    const auto found = std::ranges::find_if(wrappers, [&](const auto& entry) { return entry.first == type.name; });
    return found == std::end(wrappers) ? std::nullopt : std::optional{found->second};
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

std::vector<ReferenceFieldInitialization> reference_fields(const Declaration& declaration) {
    std::vector<ReferenceFieldInitialization> result;
    for (const auto& member : declaration.members) {
        if (member.kind == DeclKind::Binding && !member.static_member && member.type && member.type->reference)
            result.push_back({member.name, member.initializer.has_value()});
    }
    return result;
}

struct ClosureEnvironment;

struct Context {
    SymbolId owner;
    SymbolId function = no_symbol;
    SymbolId receiver = no_symbol;
    unsigned loops = 0;
    std::vector<Names> scopes;
    ClosureEnvironment* closure = nullptr;
    // Enclosing `scope` statements of this function body (Concurrency.md 4.1).
    unsigned task_scopes = 0;
    // The index in `scopes` of the innermost task scope's block. A local
    // found at or above it was declared inside that scope. Without a task
    // scope every local of the function body qualifies; parameters never do.
    std::size_t task_scope_base = 0;
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
    IrProgram run(const std::vector<ParsedFile>& files, LinkedImports linked);
    IrProgram builtins() {
        root_class();
        return std::move(program_);
    }
    IrProgram reference_builtins(const std::string& name, const Span& span) {
        const auto referent = primitive(name);
        if (!referent || referent->name != name)
            throw Diagnostic(span, "invalid primitive reference layout '&" + name + "'").with_code("E0428");
        primitive_reference(*referent, span);
        return std::move(program_);
    }

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
        std::vector<SymbolId> overloads{};
        std::vector<SymbolId> casts{};
    };
    struct GlobalDestructure {
        std::vector<SymbolId> bindings;
        Resolution resolution = Resolution::Pending;
        std::vector<IrNode> instructions;
    };

    IrProgram program_;
    std::vector<Entry> entries_;
    std::vector<SymbolId> file_ids_;
    Names linked_files_;
    Names library_files_;
    std::map<std::string, SymbolId, std::less<>> reference_layouts_;
    std::set<const Statement*> reference_field_bindings_;
    // The single assignments of deferred `last` bindings (definite_assignment.hpp).
    std::set<const Statement*> deferred_assignments_;
    std::map<std::pair<std::string, std::string>, SymbolId> linked_modules_;
    std::map<const Statement*, GlobalDestructure> destructures_;
    std::set<std::pair<SymbolId, SymbolId>> wrapper_imports_;
    const std::vector<ParsedFile>* files_ = nullptr;
    SymbolId root_class_ = no_symbol;
    // Trusted library functions bound to a scheduler operation, by name.
    std::map<SymbolId, std::string> scheduler_bindings_;

    // Source positions that only diagnostics use: the name of a declared
    // symbol, the `const` keyword of a constant binding, and the declared
    // result type of a function.
    std::map<SymbolId, Span> name_spans_;
    std::map<SymbolId, Span> const_spans_;
    std::map<SymbolId, Span> result_spans_;
    // Where the expected type of the next value() comes from, so that a
    // mismatch can label it. value() takes it on entry; nested calls do not
    // see it.
    struct ExpectedOrigin {
        Span span;
        std::string text;
        // The written annotation that gives the expected type, when known,
        // and whether the value initializes the binding it annotates. A
        // target-typed `new` of a fields-only anonymous class takes its
        // field initializers from such a binding annotation.
        const TypeSyntax* syntax = nullptr;
        bool initializes = false;
    };
    std::optional<ExpectedOrigin> expected_origin_;
    // The origin that value() received for a `new` expression it lowers.
    std::optional<std::pair<const Expr*, ExpectedOrigin>> new_origin_;

    // Anonymous classes (Types.md, Anonymous classes). A fields-only
    // class is interned by its fields; any other is one class per written
    // occurrence. Bodies written as a binding's own annotation may give
    // field initializers to a fields-only class.
    std::map<std::string, SymbolId, std::less<>> structural_classes_;
    std::map<const Declaration*, SymbolId> occurrence_classes_;
    std::set<const Declaration*> binding_bodies_;
    std::vector<SymbolId> pending_anonymous_;
    void mark_binding_annotation(const TypeSyntax& syntax);
    IrType anonymous_class(const TypeSyntax& syntax, const Context& context);
    std::string structural_key(SymbolId cls) const;
    std::string canonical_type(const IrType& type) const;
    bool structural_class(const IrType& type) const;
    IrNode construct(const Expr& expr, Context& context, const IrType* expected);
    IrNode construct_structural(const Expr& expr, Context& context, const IrType& object,
                                const Declaration* body);
    IrNode destructure_source(const Statement& stmt, Context& context);

    Span name_span(SymbolId id) const;
    void record_names(SymbolId id, const Declaration& decl);
    std::vector<std::string> visible_names(const Context& context) const;
    std::vector<std::string> member_names(const IrType& receiver, bool exported) const;
    void suggest_name(Diagnostic& error, const Span& at, std::string_view name,
                      const std::vector<std::string>& candidates, std::string_view what) const;
    void not_exported(Diagnostic& error, SymbolId hidden) const;
    Diagnostic duplicate_local(const Span& at, std::string message, SymbolId earlier) const;
    Diagnostic missing_return(const Span& at, SymbolId function, const Declaration& declaration) const;
    Diagnostic non_exhaustive(const Statement& stmt, const std::vector<SymbolId>& missing, bool produces_value) const;

    IrSymbol& symbol(SymbolId id) { return program_.symbols[id]; }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    SymbolId add(SymbolKind kind, std::string name, const Span& span, SymbolId owner = no_symbol);
    void seed(LinkedImports linked);
    SymbolId declare(const Declaration& decl, SymbolId owner, SymbolKind kind);
    SymbolId function_branch(const Declaration& decl, SymbolId owner, std::string name);
    void check_overloads(SymbolId id);
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
    IrType primitive_reference(const IrType& referent, const Span& span);
    SymbolId reference_field(const IrType& type) const;
    IrNode read_reference(IrNode handle);
    bool can_bind_reference(const IrNode& place, const IrType& type) const;
    IrNode bind_reference(IrNode place, const IrType& type);
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
    void require_type(const IrType& actual, const IrType& expected, const Span& span,
                      const ExpectedOrigin* origin = nullptr) const;
    IrType receiver_type(const IrType& type) const;
    Substitution receiver_arguments(const IrType& type, SymbolId owner = no_symbol) const;

    Context context(SymbolId owner);
    IrNode expression(const Expr& expr, Context& context, const IrType* expected = nullptr,
                      bool call_argument = false);
    IrNode expression_node(const Expr& expr, Context& context, const IrType* expected,
                           bool call_argument);
    IrNode value(const Expr& expr, Context& context, const IrType* expected = nullptr,
                 bool call_argument = false);
    IrNode reference(SymbolId id, const Span& span, Context& context, bool implicit_receiver = true);
    IrNode select(const Expr& expr, Context& context, bool for_call = false);
    IrNode wrapped_member(const Expr& expr, const IrType& type, Context& context,
                          std::optional<IrNode> receiver = std::nullopt);
    IrNode call(const Expr& expr, Context& context);
    std::optional<IrNode> primitive_call(const Expr& expr, Context& context);
    IrNode enum_construct(const IrNode& variant, const Expr& expr, Context& context);
    Substitution enum_arguments(const IrType& type) const;
    IrNode tuple(const Expr& expr, Context& context, const IrType* expected);
    IrNode range(const Expr& expr, Context& context, const IrType* expected);
    IrType parameter_type(const Parameter& parameter, const Context& context);
    IrNode pack_argument(const Expr& expr, const IrType& argument, Context& context);
    IrNode allocate(const Expr& expr, Context& context, const IrType* target = nullptr);
    IrNode binary(const Expr& expr, Context& context, const IrType* expected);
    IrNode comparison_chain(const Expr& expr, Context& context);
    IrNode conditional(const Expr& expr, Context& context, const IrType* expected, bool call_argument);
    IrNode update(const Expr& expr, Context& context);
    IrNode number(const Expr& expr, const IrType* expected, bool negative = false);
    SymbolId cast_method(const IrType& source, const IrType& target, const Context& context,
                         bool diagnose_ambiguity = true);
    IrNode cast_call(SymbolId target, IrNode source, const IrType& type, const Span& span);
    SymbolId select_overload(SymbolId group, const Expr& expr, std::size_t first,
                             const Substitution& replacements, Context& context, IrNode& call,
                             const IrType* wrapped_receiver = nullptr);
    void arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                   const Substitution& replacements, Context& context, std::size_t leading = 0);
    void place(const IrNode& destination) const;
    // `last` bindings (definite_assignment.hpp).
    void mark_last_member(SymbolId id, const Declaration& decl, SymbolKind kind);
    [[noreturn]] void assign_last(const IrNode& destination) const;
    void check_definite_assignment(const Declaration& function, SymbolId id);
    IrNode save_temporary(IrNode initializer, std::vector<IrNode>& instructions,
                          Context& context, std::string name, bool constant = true);
    IrNode capture_place(const Expr& expr, std::vector<IrNode>& instructions, Context& context);
    IrNode statement(const Statement& stmt, Context& context);
    IrNode discard_binding(const Declaration& decl, Context& context);
    void append_statement(std::vector<IrNode>& instructions, const Statement& stmt, Context& context);
    void destructure(std::vector<IrNode>& instructions, const Statement& stmt, Context& context,
                     const std::vector<SymbolId>* globals = nullptr);
    void resolve_destructure(const Statement& stmt, SymbolId file);
    IrNode lower_match(const Statement& stmt, Context& context, bool produces_value,
                       const IrType* expected = nullptr, bool call_argument = false);
    IrNode do_loop(const Statement& stmt, Context& context);
    IrNode for_loop(const Statement& stmt, Context& context);
    IrNode for_each(const Statement& stmt, Context& context);
    IrNode for_each_iterable(const Statement& stmt, Context& context, IrNode items);
    std::vector<IrNode> body(const std::vector<Statement>& stmts, Context& context);
    SymbolId owner_kind(SymbolId id, SymbolKind kind) const;

    // Scoped tasks (Concurrency.md sections 4 and 5).
    bool scope_bound_class(SymbolId id) const;
    bool scope_bound(const IrType& type) const;
    bool declared_in_task_scope(const IrNode& place, const Context& context) const;
    void require_scope_local(const IrNode& place, const IrType& stored, const Span& span,
                             const Context& context) const;
    SymbolId library_module(std::string_view path, std::string_view module, const Span& span,
                            const Context& context, const std::string& message);
    IrNode task_scope(const Statement& stmt, Context& context);
    IrNode select_statement(const Statement& stmt, Context& context);
    // With `handle`, the Task<T> for the new task; without, the bare spawn
    // of a statement that discards it.
    IrNode spawn(const Expr& expr, Context& context, bool handle = true);

    // Accessors of exported instance fields (Types.md, Accessors). The
    // implementation is in lowering_accessors.inc.
    std::map<SymbolId, const AccessorClause*> accessor_clauses_;
    // The expression that the statement being lowered assigns to, so that a
    // field reached through accessors is kept as a place rather than read.
    const Expr* place_expr_ = nullptr;
    void check_accessor_member(const Declaration& decl, SymbolId owner) const;
    void declare_accessors(const Declaration& decl, SymbolId field);
    void lower_accessors(SymbolId field);
    SymbolId accessor(SymbolId field, AccessorKind kind) const;
    IrNode route_field(IrNode field, const Expr& expr, Context& context);
    IrNode place_expression(const Expr& expr, Context& context);
    IrNode read_place(IrNode place, Context& context);
    IrNode store_place(IrNode place, IrNode value, const Span& span);
    [[noreturn]] void accessor_reference(const IrNode& place, const IrType& type) const;
};

static std::string plural(std::size_t count, std::string_view noun) {
    return std::to_string(count) + " " + std::string(noun) + (count == 1 ? "" : "s");
}

Span Lowering::name_span(SymbolId id) const {
    const auto found = name_spans_.find(id);
    return found != name_spans_.end() ? found->second : symbol(id).span;
}

void Lowering::record_names(SymbolId id, const Declaration& decl) {
    if (decl.name_span && !decl.name_span->file.empty()) name_spans_.insert_or_assign(id, *decl.name_span);
    // A constant binding that is neither exported nor static starts with
    // its `const` keyword.
    if (decl.constant && !decl.exported && !decl.static_member && decl.kind == DeclKind::Binding &&
        !decl.span.file.empty())
        const_spans_.insert_or_assign(id, Span{decl.span.file, decl.span.line, decl.span.column, decl.span.offset,
                                               decl.last ? 4u : 5u});
}

// The names a reference at `context` could mean: locals from the innermost
// scope out, then the members of each enclosing declaration, then promoted
// module members. Compiler-generated names (with '$') are left out.
std::vector<std::string> Lowering::visible_names(const Context& context) const {
    std::vector<std::string> result;
    std::set<std::string, std::less<>> seen;
    const auto add_name = [&](const std::string& name) {
        if (name.find('$') == std::string::npos && seen.insert(name).second) result.push_back(name);
    };
    for (const auto* current = &context; current; current = current->closure ? &current->closure->enclosing : nullptr) {
        for (auto it = current->scopes.rbegin(); it != current->scopes.rend(); ++it)
            for (const auto& [name, id] : *it) add_name(name);
        for (auto scope = current->owner; scope != no_symbol; scope = symbol(scope).owner) {
            for (const auto& [name, id] : entries_[scope].names) add_name(name);
            for (auto base = symbol(scope).base; base != no_symbol; base = symbol(base).base) {
                for (const auto& [name, id] : entries_[base].names)
                    if (symbol(id).exported) add_name(name);
            }
            for (const auto module : entries_[scope].promoted_modules) {
                for (const auto& [name, id] : entries_[module].names)
                    if (symbol(id).exported) add_name(name);
            }
        }
    }
    return result;
}

// The members of `receiver` that a reference may name: its own (exported
// ones only when `exported`), then those of its bases and interfaces.
std::vector<std::string> Lowering::member_names(const IrType& receiver, bool exported) const {
    std::vector<std::string> result;
    std::set<std::string, std::less<>> seen;
    for (const auto& view : nominal_views(program_, receiver)) {
        if (view.symbol == no_symbol || view.symbol >= entries_.size()) continue;
        bool own = true;
        for (auto scope = view.symbol; scope != no_symbol; scope = symbol(scope).base) {
            for (const auto& [name, id] : entries_[scope].names) {
                if ((exported || !own) && !symbol(id).exported) continue;
                if (name.find('$') == std::string::npos && seen.insert(name).second) result.push_back(name);
            }
            own = false;
        }
    }
    return result;
}

// A "did you mean" suggestion that replaces the text of `name` at the start
// of `at` (or at its end, for a member) with the closest candidate.
void Lowering::suggest_name(Diagnostic& error, const Span& at, std::string_view name,
                            const std::vector<std::string>& candidates, std::string_view what) const {
    const auto close = closest_name(name, candidates);
    if (!close) return;
    error.arg("suggestion", *close);
    for (const auto offset : {at.offset, at.offset + at.length - name.size()}) {
        if (at.length < name.size()) break;
        const auto span = source_span(at.file, offset, name.size());
        if (span && source_text(*span) == name) {
            error.suggest("there is " + std::string(what) + " with a similar name: '" + *close + "'", *span, *close,
                          Applicability::MaybeIncorrect);
            return;
        }
    }
    error.help("there is " + std::string(what) + " with a similar name: '" + *close + "'");
}

// Labels a member that exists but is not exported, and suggests exporting it
// when its declaration is in the program's own source.
void Lowering::not_exported(Diagnostic& error, SymbolId hidden) const {
    const auto& item = symbol(hidden);
    error.label("'" + item.name + "' is not exported");
    error.label(name_span(hidden), "'" + item.name + "' is declared here without 'export'");
    const auto* declaration = hidden < entries_.size() ? entries_[hidden].declaration : nullptr;
    if (declaration && !declaration->span.file.empty() && user_source(declaration->span.file)) {
        if (const auto start = source_span(declaration->span.file, declaration->span.offset, 0)) {
            error.suggest("consider exporting it", *start, "export ", Applicability::MaybeIncorrect);
            return;
        }
    }
    error.note("only exported members are visible outside the declaration that contains them");
}

Diagnostic Lowering::duplicate_local(const Span& at, std::string message, SymbolId earlier) const {
    auto error = Diagnostic(at, std::move(message)).with_code("E0301").label("bound again here");
    const auto& first = symbol(earlier);
    if (!first.span.file.empty()) error.label(name_span(earlier), "first bound here");
    return error;
}

// A body that can reach its end without a return. The error points at the
// declared result type, which is what the body fails to produce.
Diagnostic Lowering::missing_return(const Span& at, SymbolId function, const Declaration& declaration) const {
    const auto type = describe(symbol(function).type);
    const bool typed = declaration.type && !declaration.type->span.file.empty();
    auto error = Diagnostic(typed ? declaration.type->span : at, "function may exit without returning " + type)
        .with_code("E0405").label("the function must return " + type + " on every path");
    if (name_spans_.contains(function)) error.label(name_span(function), "in this function");
    error.note("a path through the body ends without 'return' or a trap");
    return error;
}

// A match on an enum without a wildcard arm that leaves variants out. The
// suggestion adds an arm per variant before the closing brace when that
// brace is on a line of its own after a complete arm.
Diagnostic Lowering::non_exhaustive(const Statement& stmt, const std::vector<SymbolId>& missing,
                                    bool produces_value) const {
    std::string names;
    for (std::size_t i = 0; i < missing.size(); ++i)
        names += (i == 0 ? "'" : i + 1 == missing.size() ? " and '" : ", '") + symbol(missing[i]).name + "'";
    auto error = Diagnostic(Span{stmt.span.file, stmt.span.line, stmt.span.column, stmt.span.offset, 5},
                            "non-exhaustive match, missing variant '" + symbol(missing.front()).name + "'")
        .with_code("E0410").label(names + (missing.size() == 1 ? " is" : " are") + " not covered");
    error.note("a match without a '_' arm needs an arm for every variant");
    std::string qualifier;
    for (const auto& arm : stmt.arms) {
        if (const auto dot = arm.variant.rfind('.'); arm.variant != "_" && dot != std::string::npos) {
            qualifier = arm.variant.substr(0, dot + 1);
            break;
        }
    }
    const auto close = source_span(stmt.span.file, stmt.span.offset + stmt.span.length - 1, 1);
    const auto file = SourceMap::global().find(stmt.span.file);
    if (!close || !file || !user_source(stmt.span.file) || source_text(*close) != "}" || stmt.arms.empty())
        return error;
    const auto& text = file->text();
    const auto line_start = file->line_start(close->line - 1);
    const auto indent = text.substr(line_start, close->offset - line_start);
    if (indent.find_first_not_of(" \t") != std::string::npos) return error;
    auto before = line_start;
    while (before > 0 && std::string_view(" \t\r\n").find(text[before - 1]) != std::string_view::npos) --before;
    if (before == 0 || (text[before - 1] != ',' && text[before - 1] != '{')) return error;
    const auto arm_indent = std::string(stmt.arms.front().span.column - 1, ' ');
    std::string arms;
    for (const auto variant : missing) {
        std::string pattern = qualifier + symbol(variant).name;
        const auto* declaration = variant < entries_.size() ? entries_[variant].declaration : nullptr;
        if (declaration && !declaration->parameters.empty()) {
            std::string fields;
            for (const auto& field : declaration->parameters)
                fields += (fields.empty() ? "" : ", ") + (declaration->named_payload ? field.name : std::string("_"));
            pattern += declaration->named_payload ? " { " + fields + " }" : "(" + fields + ")";
        }
        arms += arm_indent + pattern + (produces_value ? " => <value>,\n" : " => {},\n");
    }
    error.suggest(missing.size() == 1 ? "add an arm for it" : "add an arm for each missing variant",
                  *source_span(stmt.span.file, line_start, 0),
                  arms, Applicability::HasPlaceholders);
    return error;
}

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

SymbolId Lowering::function_branch(const Declaration& decl, SymbolId owner, std::string name) {
    const auto id = add(SymbolKind::Function, std::move(name), decl.span, owner);
    record_names(id, decl);
    entries_[id].declaration = &decl;
    entries_[id].resolution = Resolution::Pending;
    symbol(id).constant = decl.constant;
    symbol(id).exported = decl.exported || symbol(owner).kind == SymbolKind::Interface;
    symbol(id).has_body = decl.has_body;
    symbol(id).user_cast = decl.cast_target.has_value();
    return id;
}

SymbolId Lowering::declare(const Declaration& decl, SymbolId owner, SymbolKind kind) {
    if (!decl.accessors.empty()) check_accessor_member(decl, owner);
    if (decl.name == "_" && kind != SymbolKind::Variant) {
        if (decl.kind != DeclKind::Binding || decl.exported || decl.override_member ||
            decl.static_member || !decl.initializer ||
            (symbol(owner).kind != SymbolKind::File && symbol(owner).kind != SymbolKind::Module))
            throw Diagnostic(decl.span, "'_' requires a value initializer and does not declare a member")
                .with_code("E0310");
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
    if (decl.cast_target) {
        if (symbol(owner).kind != SymbolKind::Class || decl.static_member || decl.override_member ||
            !decl.has_body || !decl.generics.empty() || !decl.parameters.empty()) {
            throw Diagnostic(decl.span, "a cast requires a class instance method with a body and no parameters")
                .with_code("E0512");
        }
        const auto id = function_branch(decl, owner, "$as$" + std::to_string(entries_.size()));
        entries_[owner].casts.push_back(id);
        attach_tags(program_, id, TagTarget::Method, decl.tags, decl.name_span.value_or(decl.span));
        return id;
    }
    const auto lexical = owner;
    if (decl.static_member) {
        if (!class_or_interface(symbol(owner).kind) || decl.override_member || decl.promote_import ||
            decl.kind == DeclKind::Module || decl.name == "init" || decl.name == "deinit")
            throw Diagnostic(decl.span, "static requires a non-override class or interface member").with_code("E0501");
        owner = static_scope(owner);
        if (kind == SymbolKind::Field) kind = SymbolKind::Global;
    }
    auto id = add(kind, decl.name, decl.span, owner);
    record_names(id, decl);
    if (const auto [earlier, added] = entries_[lexical].names.emplace(decl.name, id); !added) {
        auto error = Diagnostic(decl.name_span.value_or(decl.span), "duplicate declaration of '" + decl.name + "'")
            .with_code("E0302")
            .label("'" + decl.name + "' is declared again here")
            .label(name_span(earlier->second), "'" + decl.name + "' is first declared here");
        if (decl.kind == DeclKind::Function && symbol(earlier->second).kind == SymbolKind::Function)
            error.help("to overload '" + decl.name + "', write the alternatives in one declaration, "
                       "joined with 'or func'");
        else
            error.help("rename one of them");
        throw error;
    }
    entries_[id].declaration = &decl;
    entries_[id].resolution = Resolution::Pending;
    symbol(id).constant = decl.constant ||
        (decl.static_member && kind == SymbolKind::Global && symbol(lexical).kind == SymbolKind::Interface);
    symbol(id).exported = decl.exported || symbol(lexical).kind == SymbolKind::Interface;
    symbol(id).has_body = decl.has_body;
    if (decl.last && decl.kind == DeclKind::Binding) mark_last_member(id, decl, kind);
    if (!decl.overloads.empty()) {
        const auto valid_branch = [](const Declaration& branch) {
            return branch.has_body && !branch.host_binding && branch.generics.empty() &&
                std::ranges::none_of(branch.parameters, &Parameter::variadic);
        };
        if (decl.override_member || symbol(lexical).kind == SymbolKind::Interface ||
            !valid_branch(decl)) {
            throw Diagnostic(decl.span, "overloads require implemented, non-generic, fixed-arity functions without .override")
                .with_code("E0433");
        }
        for (const auto& branch : decl.overloads) {
            if (!valid_branch(branch))
                throw Diagnostic(branch.span, "overloads require implemented, non-generic, fixed-arity functions")
                    .with_code("E0433");
            auto name = decl.name == "init" && symbol(lexical).kind == SymbolKind::Class ? decl.name
                : decl.name + "$" + std::to_string(id) + "$" +
                    std::to_string(entries_[id].overloads.size() + 1);
            const auto branch_id = function_branch(branch, owner, std::move(name));
            symbol(branch_id).overload_group = id;
            entries_[id].overloads.push_back(branch_id);
        }
    }
    if (kind == SymbolKind::Module)
        symbol(id).type = {TypeKind::Module, decl.name, id, {}};
    if (decl.singleton) {
        if (symbol(owner).kind != SymbolKind::File && symbol(owner).kind != SymbolKind::Module)
            throw Diagnostic(decl.span, "a singleton must be a file, module, or static class member")
                .with_code("E0511");
        const auto instance = add(SymbolKind::Global, decl.name, decl.span, owner);
        entries_[id].singleton_instance = instance;
        symbol(id).singleton_instance = instance;
        symbol(instance).constant = true;
    }
    if (!decl.accessors.empty()) declare_accessors(decl, id);
    attach_tags(program_, id, tag_target(program_, id), decl.tags, decl.name_span.value_or(decl.span),
                entries_[id].overloads);
    auto member_kind = kind == SymbolKind::Class ? SymbolKind::Field
                     : kind == SymbolKind::Enum ? SymbolKind::Variant
                                                : SymbolKind::Global;
    for (const auto& child : decl.members) declare(child, id, member_kind);
    return id;
}

bool Lowering::static_member(SymbolId id) const {
    const auto owner = symbol(id).owner;
    return owner != no_symbol && is_static_scope(symbol(owner));
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
    return receiver_type(type).kind == TypeKind::Object && !is_primitive_reference(receiver_type(type));
}

SymbolId Lowering::resolve_import(const Expr& expr) {
    if (expr.text.ends_with(".*"))
        throw Diagnostic(expr.span, "wildcard imports require 'let.promote _'").with_code("E0704");
    auto source = std::ranges::find(*files_, expr.span.file, &ParsedFile::file);
    if (source == files_->end()) throw Diagnostic(expr.span, "cannot locate importing file").with_code("E0899");
    auto import = std::ranges::find_if(source->imports, [&](const ResolvedImport& import) {
        return import.name == expr.text && import.artifact_path == expr.import_path.value_or("");
    });
    if (import == source->imports.end()) {
        // The built-in SysIo and Test modules were retired in favor of the
        // standard library (Stdlib.md section 4.6); name their replacements.
        if (expr.text == "SysIo")
            throw Diagnostic(expr.span, "cannot resolve import 'SysIo': the built-in SysIo module was removed; "
                                        "use Std.Io.Console and Std.Program").with_code("E0701");
        if (expr.text == "Test")
            throw Diagnostic(expr.span, "cannot resolve import 'Test': the built-in Test module was removed; "
                                        "use Std.Test").with_code("E0701");
        throw Diagnostic(expr.span, "cannot resolve import '" + expr.text + "'").with_code("E0701");
    }
    auto result = resolve_import_target(*import, expr.span);
    resolve(result);
    check_tagged_use(program_, result, expr.span, file_ids_[static_cast<std::size_t>(source - files_->begin())]);
    return result;
}

SymbolId Lowering::resolve_import_target(const ResolvedImport& import, const Span& span) const {
    if (import.artifact) {
        const auto linked = linked_modules_.find({import.file, import.target_module});
        if (linked != linked_modules_.end()) return linked->second;
        throw Diagnostic(span, "cannot link module from '" + import.file + "'").with_code("E0899");
    }
    auto target_file = std::ranges::find(*files_, import.file, &ParsedFile::file);
    SymbolId file;
    if (target_file != files_->end()) {
        file = file_ids_[static_cast<std::size_t>(target_file - files_->begin())];
    } else if (const auto linked = linked_files_.find(import.file); linked != linked_files_.end()) {
        file = linked->second;
    } else {
        throw Diagnostic(span, "cannot load source file for import '" + import.name + "'").with_code("E0899");
    }
    auto name = import.name.ends_with(".*") ? import.target_module
        : import.name.substr(import.name.find_last_of('.') + 1);
    if (name.empty())
        throw Diagnostic(span, "imported file '" + import.file + "' must declare a module matching its name")
            .with_code("E0705");
    auto result = own_member(file, name);
    if (result == no_symbol || symbol(result).kind != SymbolKind::Module)
        throw Diagnostic(span, "imported file must declare module '" + name + "'").with_code("E0705");
    return result;
}

void Lowering::prepare_promotions(const Declaration& decl, SymbolId owner) {
    if (decl.promote_import || (decl.name == "_" && decl.initializer && decl.initializer->kind == ExprKind::Import)) {
        if (symbol(owner).kind != SymbolKind::Module && symbol(owner).kind != SymbolKind::File)
            throw Diagnostic(decl.span, "promoted and discarded imports require a file or module binding")
                .with_code("E0704");
        if (decl.type || decl.exported || !decl.initializer || decl.initializer->kind != ExprKind::Import)
            throw Diagnostic(decl.span, "let.promote requires an unannotated import alias").with_code("E0704");
        const auto& expr = *decl.initializer;
        if (expr.text.ends_with(".*") && (!decl.promote_import || decl.name != "_"))
            throw Diagnostic(expr.span, "wildcard imports require 'let.promote _'").with_code("E0704");
        const auto source = std::ranges::find(*files_, expr.span.file, &ParsedFile::file);
        if (source == files_->end()) throw Diagnostic(expr.span, "cannot locate importing file").with_code("E0899");
        bool found = false;
        for (const auto& import : source->imports) {
            if (import.name != expr.text || import.artifact_path != expr.import_path.value_or("")) continue;
            found = true;
            const auto target = resolve_import_target(import, expr.span);
            check_tagged_use(program_, target, expr.span, owner);
            if (decl.promote_import) entries_[owner].promoted_modules.push_back(target);
            if (decl.name == "_") {
                const auto dependency = add(SymbolKind::Import, "$import", decl.span, owner);
                symbol(dependency).type = symbol(target).type;
            }
        }
        if (!found) throw Diagnostic(expr.span, "cannot resolve import '" + expr.text + "'").with_code("E0701");
    }
    if (!decl.members.empty()) {
        const auto id = own_member(owner, decl.name);
        for (const auto& child : decl.members) {
            if (!child.cast_target) prepare_promotions(child, id);
        }
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
                throw Diagnostic(span, "static member cannot use an enclosing class type parameter").with_code("E0501");
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
                throw Diagnostic(span, "ambiguous promoted name '" + std::string(name) + "'").with_code("E0307");
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
    if (id == no_symbol) {
        auto error = Diagnostic(span, "unknown name '" + parts.front() + "'").with_code("E0304")
            .label("not found in this scope");
        suggest_name(error, Span{span.file, span.line, span.column, span.offset, parts.front().size()}, parts.front(),
                     visible_names(context), "a binding");
        throw error;
    }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        resolve(id);
        auto kind = symbol(id).kind;
        if (kind == SymbolKind::Import) {
            id = symbol(id).type.symbol;
        } else if (kind == SymbolKind::Alias && symbol(id).type.kind == TypeKind::Enum) {
            id = symbol(id).type.symbol;
        } else if (kind != SymbolKind::Module && !class_or_interface(kind) &&
                   kind != SymbolKind::Enum) {
            throw Diagnostic(span, "'" + parts[i - 1] + "' is not a namespace").with_code("E0308");
        }
        const auto owner = id;
        const bool outside = !inside(lexical_owner(context), owner);
        id = member(owner, parts[i], outside);
        if (id == no_symbol) {
            auto error = Diagnostic(span, "unknown or inaccessible name '" + name + "'").with_code("E0304")
                .arg("kind", "name").arg("name", parts[i]).arg("owner", symbol(owner).name);
            if (const auto hidden = member(owner, parts[i], false); hidden != no_symbol) {
                not_exported(error, hidden);
            } else {
                error.label("'" + symbol(owner).name + "' has no member '" + parts[i] + "'");
                std::size_t prefix = 0;
                for (std::size_t j = 0; j < i; ++j) prefix += parts[j].size() + 1;
                if (span.length >= prefix + parts[i].size())
                    suggest_name(error, Span{span.file, span.line, span.column, span.offset + prefix, parts[i].size()},
                                 parts[i], member_names(symbol(owner).type, outside), "a member");
            }
            throw error;
        }
    }
    if (!(nominal_type && (symbol(id).kind == SymbolKind::Enum || class_or_interface(symbol(id).kind)) &&
          entries_[id].resolution == Resolution::Resolving)) resolve(id);
    check_tagged_use(program_, id, span, lexical_owner(context), [&](std::string_view other) {
        SpeculativeDiagnostics lookup;
        try { return find(other, context, span); } catch (const Diagnostic&) { return no_symbol; }
    });
    return id;
}

IrType Lowering::apply_arguments(IrType type, const Substitution& arguments) {
    return substitute(std::move(type), arguments);
}

Substitution Lowering::type_arguments(const std::vector<SymbolId>& params,
                                     const std::vector<TypeSyntax>& args,
                                     const Span& span, const Context& context) {
    if (params.size() != args.size())
        throw Diagnostic(span, "expected " + std::to_string(params.size()) + " type argument(s)").with_code("E0417");
    Substitution result;
    for (std::size_t i = 0; i < args.size(); ++i) {
        auto actual = type(args[i], context);
        if (actual.kind == TypeKind::Void) throw Diagnostic(args[i].span, "void is not a type argument")
            .with_code("E0416");
        if (is_primitive_reference(actual))
            throw Diagnostic(args[i].span, "a primitive reference cannot be a type argument").with_code("E0428");
        if (scope_bound(actual))
            throw Diagnostic(args[i].span, "a value of type " + describe(actual) +
                                           " cannot leave its scope: it cannot be a type argument").with_code("E0605");
        result.emplace(params[i], std::move(actual));
    }
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& parameter = symbol(params[i]);
        const auto check = [&](const IrType& declared) {
            const auto bound = substitute(declared, result);
            const auto& actual = result.at(params[i]);
            if (!assignable(actual, bound)) {
                throw Diagnostic(args[i].span, "type argument " + describe(actual) + " does not satisfy the bound " +
                                               describe(bound) + " of type parameter '" + parameter.name + "'")
                    .with_code("E0418");
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
                throw Diagnostic(part.span, "a tuple element cannot have type void").with_code("E0416");
            result.arguments.push_back(std::move(element));
        }
    } else if (syntax.name == "[function]") {
        std::vector<IrType> signature;
        for (const auto& part : syntax.arguments) signature.push_back(type(part, context));
        result = callable_type(std::move(signature), syntax.span);
    } else if (syntax.anonymous) {
        result = anonymous_class(syntax, context);
    } else if (syntax.name == "class") {
        result = symbol(root_class()).type;
    } else if (auto builtin = primitive(syntax.name)) {
        if (!syntax.arguments.empty()) throw Diagnostic(syntax.span, "builtin type takes no arguments")
            .with_code("E0417");
        result = *builtin;
    } else {
        auto id = qualified(syntax.name, syntax.span, context, true);
        auto kind = symbol(id).kind;
        if (!class_or_interface(kind) && kind != SymbolKind::Alias &&
            kind != SymbolKind::Enum && kind != SymbolKind::TypeParameter) {
            throw Diagnostic(syntax.span, "'" + syntax.name + "' is not a type").with_code("E0421");
        }
        const auto params = symbol(id).generics;
        if (entries_[id].resolution == Resolution::Resolving && kind == SymbolKind::Enum &&
            params.size() != entries_[id].declaration->generics.size())
            throw Diagnostic(syntax.span, "recursive reference in an enum's type parameter bounds").with_code("E0421");
        auto replacements = type_arguments(params, syntax.arguments, syntax.span, context);
        result = apply_arguments(symbol(id).type, replacements);
    }
    for (auto suffix : syntax.collections) {
        if (result.kind == TypeKind::Void)
            throw Diagnostic(syntax.span, "void is not a collection element type").with_code("E0416");
        result = collection_type(suffix == CollectionKind::Array ? TypeKind::Array : TypeKind::List,
                                 result);
    }
    return syntax.reference ? primitive_reference(result, syntax.span) : result;
}

IrType Lowering::primitive_reference(const IrType& referent, const Span& span) {
    if (referent.kind != TypeKind::Integer && referent.kind != TypeKind::Float &&
        referent.kind != TypeKind::Bool && referent.kind != TypeKind::String)
        throw Diagnostic(span, "a primitive reference needs a Boolean, numeric, or string type").with_code("E0428");
    if (const auto found = reference_layouts_.find(referent.name); found != reference_layouts_.end())
        return symbol(found->second).type;

    const Span built_in{"<reference:" + referent.name + ">"};
    const auto base = root_class();
    const auto file = add(SymbolKind::File, built_in.file, built_in);
    const auto layout = add(SymbolKind::Class, "$reference", built_in, file);
    const IrType type{TypeKind::Object, "&" + referent.name, layout, {}};
    symbol(layout).type = type;
    symbol(layout).base = base;
    reference_layouts_.emplace(referent.name, layout);

    const auto field = add(SymbolKind::Field, "$value", built_in, layout);
    symbol(field).type = referent;
    entries_[layout].names.emplace("$value", field);
    const auto initialize = add(SymbolKind::Function, "init", built_in, layout);
    symbol(initialize).has_body = true;
    const auto parameter = add(SymbolKind::Parameter, "$initial", built_in, initialize);
    symbol(parameter).type = referent;
    symbol(initialize).parameters.push_back(parameter);
    auto load = node(IrOp::Load, built_in, referent);
    load.symbol = parameter;
    auto receiver = node(IrOp::Receiver, built_in, type);
    receiver.symbol = layout;
    auto store = node(IrOp::Store, built_in);
    store.operands = {read_reference(std::move(receiver)), std::move(load)};
    program_.bodies.push_back({initialize, {std::move(store)}});

    const auto empty = add(SymbolKind::Function, "init", built_in, layout);
    symbol(empty).has_body = true;
    symbol(empty).overload_group = initialize;
    program_.bodies.push_back({empty, {}});
    return type;
}

SymbolId Lowering::reference_field(const IrType& type) const {
    return own_member(type.symbol, "$value");
}

IrNode Lowering::read_reference(IrNode handle) {
    const auto field = reference_field(handle.type);
    auto result = node(IrOp::Field, handle.span, symbol(field).type);
    result.symbol = field;
    result.operands.push_back(std::move(handle));
    return result;
}

bool Lowering::can_bind_reference(const IrNode& place, const IrType& type) const {
    return place.type == symbol(reference_field(type)).type &&
        (place.op == IrOp::Load || place.op == IrOp::Field) && place.symbol != no_symbol &&
        !symbol(place.symbol).constant && !symbol(place.symbol).name.starts_with("$capture");
}

IrNode Lowering::bind_reference(IrNode place, const IrType& type) {
    if (place.type == type) return place;
    if (!can_bind_reference(place, type) && place.op == IrOp::VirtualCall && place.symbol != no_symbol &&
        has_attribute(symbol(place.symbol), AttributeId::AccessorOf))
        accessor_reference(place, type);
    if (!can_bind_reference(place, type))
        throw Diagnostic(place.span, describe(type) + " requires mutable storage of type " +
                                     describe(symbol(reference_field(type)).type)).with_code("E0428");
    if (place.op == IrOp::Field && place.symbol == reference_field(type))
        return std::move(place.operands.front());
    auto result = node(IrOp::ReferencePlace, place.span, type);
    result.symbol = place.symbol;
    result.operands.push_back(std::move(place));
    return result;
}

bool Lowering::assignable(const IrType& actual, const IrType& expected) const {
    return nominal_assignable(program_, actual, expected);
}

void Lowering::require_type(const IrType& actual, const IrType& expected, const Span& span,
                            const ExpectedOrigin* origin) const {
    if (assignable(actual, expected)) return;
    auto error = Diagnostic(span, "expected " + describe(expected) + ", got " + describe(actual)).with_code("E0403")
        .label("expected " + describe(expected) + ", found " + describe(actual));
    if (origin && !origin->span.file.empty()) error.label(origin->span, origin->text);
    const auto distinct = [&](const IrType& type) {
        return type.kind == TypeKind::Object && type.symbol < program_.symbols.size() && !structural_class(type) &&
               has_attribute(symbol(type.symbol), AttributeId::ClassAnonymous);
    };
    if (distinct(actual) || distinct(expected)) {
        error.note("an anonymous class with methods, a constructor, accessors, const or last fields, tagged "
                   "members, a base, or interfaces is a type of its own at each place it is written")
            .help("declare a named class and use it in both places");
    }
    throw error;
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
            throw Diagnostic(span, "a function parameter cannot have type void").with_code("E0416");
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
            throw Diagnostic(span, "duplicate parameter '" + names[i] + "'").with_code("E0303");
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
    if (scope_bound(input.type))
        throw Diagnostic(span, "a value of type " + describe(input.type) +
                               " cannot leave its scope: a lambda cannot capture it").with_code("E0605");
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
            throw Diagnostic(parameter.span, "Std.Core.Variadic must export its Argument enum").with_code("E0899");
        resolve(argument);
        return collection_type(TypeKind::Array, symbol(argument).type);
    }
    auto element = type(parameter.type, context);
    if (element.kind == TypeKind::Void)
        throw Diagnostic(parameter.span, "a variadic parameter cannot have elements of type void").with_code("E0416");
    return collection_type(TypeKind::Array, element);
}

IrNode Lowering::lambda(const Declaration& declaration, Context& context) {
    const auto& span = declaration.span;
    std::vector<IrType> signature;
    std::vector<std::string> names;
    for (const auto& parameter : declaration.parameters) {
        if (parameter.variadic)
            throw Diagnostic(parameter.span, "a variadic parameter requires a named function").with_code("E0207");
        signature.push_back(parameter_type(parameter, context));
        names.push_back(parameter.name);
        check_tags(parameter.tags, TagTarget::Parameter, parameter.span);
    }
    signature.push_back(type(*declaration.type, context));
    const auto callable = callable_type(std::move(signature), span);
    ClosureEnvironment environment{closure_layout(span, lexical_owner(context)), context, {}, {}};
    const auto method = closure_method(environment.layout, callable, span, names);
    if (declaration.type && symbol(method).type.kind != TypeKind::Void) result_spans_.emplace(method, declaration.type->span);
    Context inner{method, method, context.receiver, 0, {entries_[method].names}, &environment};
    check_definite_assignment(declaration, no_symbol);
    auto instructions = body(declaration.body, inner);
    if (symbol(method).type.kind != TypeKind::Void && !always_returns(instructions))
        throw missing_return(span, method, declaration);
    program_.bodies.push_back({method, std::move(instructions)});
    return closure_value(callable, environment, span);
}

IrNode Lowering::bind_function(IrNode callee, Context& context) {
    const auto target = callee.symbol;
    if (!entries_[target].overloads.empty())
        throw Diagnostic(callee.span, "an overloaded function must be called directly").with_code("E0433");
    const auto& span = callee.span;
    if (callee.type.name == "super.init") {
        throw Diagnostic(span, "super.init must be called directly in a constructor").with_code("E0506");
    }
    if (!symbol(target).generics.empty()) {
        throw Diagnostic(span, "a generic function must be called with explicit type arguments").with_code("E0419");
    }
    if (callee.operands.empty() && !symbol(target).external && !symbol(target).has_body) {
        throw Diagnostic(span, "function requires an implementation").with_code("E0434");
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
            throw Diagnostic(parameter.span, "duplicate type parameter '" + parameter.name + "'").with_code("E0303");
        symbol(generic).type = {TypeKind::TypeParameter, parameter.name, generic, {}};
        symbol(id).generics.push_back(generic);
    }
    for (std::size_t i = 0; i < decl.generics.size(); ++i) {
        const auto generic = symbol(id).generics[i];
        for (const auto& syntax : decl.generics[i].bounds) {
            auto bound = type(syntax, context);
            if (bound.kind != TypeKind::Object) {
                throw Diagnostic(syntax.span, "a type parameter bound must be a class or interface").with_code("E0421");
            }
            auto& parameter = symbol(generic);
            if (!parameter.bound) {
                parameter.bound = std::move(bound);
            } else {
                if (*parameter.bound == bound ||
                    std::ranges::find(parameter.additional_bounds, bound) != parameter.additional_bounds.end()) {
                    throw Diagnostic(syntax.span, "duplicate type parameter bound").with_code("E0303");
                }
                parameter.additional_bounds.push_back(std::move(bound));
            }
        }
    }
}

void Lowering::resolve_binding(SymbolId id, const Declaration& decl) {
    auto ctx = context(symbol(id).owner);
    if (decl.initializer && decl.initializer->kind == ExprKind::Import) {
        if (decl.type) throw Diagnostic(decl.span, "import aliases cannot have a type annotation").with_code("E0704");
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
                    throw Diagnostic(init.span, "generic type overlays are not supported").with_code("E0421");
                symbol(id).kind = SymbolKind::Alias;
                symbol(id).type = initializer.type;
                return;
            }
            if (kind == SymbolKind::Variant) {
                if (!symbol(initializer.symbol).payload_types.empty())
                    throw Diagnostic(init.span, "enum variant requires a payload").with_code("E0427");
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
                throw Diagnostic(init.span, "expected a value initializer").with_code("E0422");
        }
        if (scope_bound(initializer.type))
            throw Diagnostic(init.span, "a value of type " + describe(initializer.type) +
                                        " cannot leave its scope: it cannot be stored in a field or global")
                .with_code("E0605");
        symbol(id).type = initializer.type;
        entries_[id].initializer = std::move(initializer);
        return;
    }
    if (!decl.type) throw Diagnostic(decl.span, "declaration needs a type or initializer").with_code("E0205");
    if (decl.initializer) mark_binding_annotation(*decl.type);
    auto declared = type(*decl.type, ctx);
    if (declared.kind == TypeKind::Void) throw Diagnostic(decl.span, "a binding cannot have type void")
        .with_code("E0416");
    if (scope_bound(declared))
        throw Diagnostic(decl.type->span, "a value of type " + describe(declared) +
                                          " cannot leave its scope: it cannot be stored in a field or global")
            .with_code("E0605");
    if (is_primitive_reference(declared)) {
        if (decl.static_member)
            throw Diagnostic(decl.span, "a static binding cannot have a primitive reference type").with_code("E0428");
        if (symbol(id).kind == SymbolKind::Field)
            symbol(id).constant = false; // A constructor can perform the first binding.
    }
    symbol(id).type = declared;
    if (decl.initializer) {
        expected_origin_ = ExpectedOrigin{decl.type->span, "expected because of this type", &*decl.type, true};
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
    } else if (symbol(id).kind != SymbolKind::Field ||
               (decl.constant && !decl.last && !is_primitive_reference(declared))) {
        throw Diagnostic(decl.span, "binding requires an initializer").with_code("E0404");
    }
}

void Lowering::resolve(SymbolId id) {
    auto state = entries_[id].resolution;
    if (state == Resolution::Resolved) return;
    if (state == Resolution::Resolving)
        throw Diagnostic(symbol(id).span, "cyclic declaration of '" + symbol(id).name + "'").with_code("E0309");
    if (const auto* destructure = entries_[id].destructure) {
        resolve_destructure(*destructure, symbol(id).owner);
        return;
    }
    entries_[id].resolution = Resolution::Resolving;
    const TagScope tag_scope(id);
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
                throw Diagnostic(decl.base->span, "base must have the same declaration kind").with_code("E0503");
            if (entries_[base].singleton_instance != no_symbol)
                throw Diagnostic(decl.base->span, "a singleton cannot be extended").with_code("E0503");
            if (symbol(base).kind == SymbolKind::Module && symbol(base).constant)
                throw Diagnostic(decl.base->span, "cannot inherit a constant module").with_code("E0503");
            if (!symbol(base).generics.empty() || !decl.base->arguments.empty() ||
                !decl.base->collections.empty())
                throw Diagnostic(decl.base->span, "generic inheritance is not supported").with_code("E0503");
            symbol(id).base = base;
        } else if (class_or_interface(symbol(id).kind)) {
            if (decl.base && !decl.base->collections.empty())
                throw Diagnostic(decl.base->span, "a base must be a class type").with_code("E0421");
            symbol(id).base = root_class();
        } else if (decl.base) {
            throw Diagnostic(decl.base->span, "a module cannot inherit from class").with_code("E0503");
        }
        for (const auto& syntax : decl.interfaces) {
            auto interface = type(syntax, ctx);
            if (interface.kind != TypeKind::Object || symbol(interface.symbol).kind != SymbolKind::Interface) {
                throw Diagnostic(syntax.span, "an interface is required here").with_code("E0421");
            }
            const auto views = nominal_views(program_, interface);
            if (std::ranges::find(views, id, &IrType::symbol) != views.end()) {
                throw Diagnostic(syntax.span, "cyclic interface inheritance").with_code("E0503");
            }
            if (std::ranges::find(symbol(id).interfaces, interface.symbol, &IrType::symbol) !=
                symbol(id).interfaces.end()) {
                throw Diagnostic(syntax.span, "interface is listed more than once").with_code("E0503");
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
                    throw Diagnostic(field.span, "duplicate enum payload field '" + field.name + "'")
                        .with_code("E0303");
                auto payload = type(field.type, ctx);
                if (payload.kind == TypeKind::Void)
                    throw Diagnostic(field.span, "an enum payload cannot have type void").with_code("E0416");
                if (scope_bound(payload))
                    throw Diagnostic(field.span, "a value of type " + describe(payload) +
                                                 " cannot leave its scope: it cannot be an enum payload")
                        .with_code("E0605");
                symbol(variant).payload_types.push_back(std::move(payload));
            }
        }
    } else if (decl.kind == DeclKind::Function) {
        resolve_generics(id, decl, ctx);
        symbol(id).type = type(*decl.type, ctx);
        if (decl.cast_target) {
            const auto target = type(*decl.cast_target, ctx);
            if (is_primitive_reference(target))
                throw Diagnostic(decl.cast_target->span, "a cast cannot create a primitive reference")
                    .with_code("E0428");
            if (target.kind == TypeKind::Void || target != symbol(id).type)
                throw Diagnostic(decl.type->span, "cast must return its declared target type " + describe(target))
                    .with_code("E0512");
        }
        for (const auto& parameter : decl.parameters) {
            auto argument_type = parameter_type(parameter, ctx);
            if (argument_type.kind == TypeKind::Void)
                throw Diagnostic(parameter.span, "a parameter cannot have type void").with_code("E0416");
            auto arg = add(SymbolKind::Parameter, parameter.name, parameter.span, id);
            attach_tags(program_, arg, TagTarget::Parameter, parameter.tags, parameter.span);
            if (const auto [earlier, added] = entries_[id].names.emplace(parameter.name, arg); !added) {
                throw Diagnostic(parameter.span, "duplicate parameter '" + parameter.name + "'").with_code("E0303")
                    .label("'" + parameter.name + "' is declared again here")
                    .label(symbol(earlier->second).span, "'" + parameter.name + "' is first declared here");
            }
            symbol(arg).type = std::move(argument_type);
            symbol(id).parameters.push_back(arg);
            if (parameter.variadic) {
                symbol(id).variadic = parameter.type.name == "[variadic argument]"
                    ? VariadicKind::Untyped : VariadicKind::Typed;
            }
        }
        if (!decl.has_body && symbol(symbol(id).owner).kind != SymbolKind::Module &&
            !class_or_interface(symbol(symbol(id).owner).kind))
            throw Diagnostic(decl.span, "only class, interface or module members may omit a function body")
                .with_code("E0434");
        if (!decl.has_body && decl.static_member)
            throw Diagnostic(decl.span, "a static function requires a body").with_code("E0501");
        if (decl.host_binding) {
            const auto file = source_file(program_, id);
            if (file == no_symbol || symbol(file).library_path.empty() ||
                symbol(symbol(id).owner).kind != SymbolKind::Module)
                throw Diagnostic(decl.span, "host bindings are only allowed in standard library modules")
                    .with_code("E0710");
            if (!decl.generics.empty() || decl.override_member ||
                std::ranges::any_of(decl.parameters, &Parameter::variadic) ||
                !host_service_name(decl.host_binding->first))
                throw Diagnostic(decl.span, "a host binding needs a non-generic function and a qualified service name")
                    .with_code("E0710");
            for (auto parameter : symbol(id).parameters) {
                if (!host_data_type(symbol(parameter).type))
                    throw Diagnostic(symbol(parameter).span, "this parameter cannot cross the host boundary")
                        .with_code("E0710");
            }
            if (!host_variants(program_, symbol(id).type))
                throw Diagnostic(decl.span, "a host result needs exactly Ok or Ok(T), and Error(uint32)")
                    .with_code("E0710");
            symbol(id).external = true;
            symbol(id).host_binding = IrHostBinding{decl.host_binding->first, decl.host_binding->second};
        }
        if (decl.kernel_binding) {
            // A built-in C kernel implements the function (Kernels.md,
            // "Binding a kernel"). Like a host binding it is trusted library
            // code only, but a call is a runtime state operation, not a host
            // request: it needs no import, no capability, and no trace entry.
            const auto file = source_file(program_, id);
            if (file == no_symbol || symbol(file).library_path.empty() ||
                symbol(symbol(id).owner).kind != SymbolKind::Module)
                throw Diagnostic(decl.span, "kernel bindings are only allowed in standard library modules")
                    .with_code("E0780");
            if (!decl.generics.empty() || decl.override_member ||
                std::ranges::any_of(decl.parameters, &Parameter::variadic) ||
                !host_service_name(decl.kernel_binding->first))
                throw Diagnostic(decl.span, "a kernel binding needs a non-generic function and a qualified kernel name")
                    .with_code("E0781");
            const auto& [kernel, version] = *decl.kernel_binding;
            if (const auto problem = kernel_signature_problem(program_, symbol(id), kernel, version)) {
                if (kernel_known(kernel, version)) throw Diagnostic(decl.span, *problem).with_code("E0781");
                throw Diagnostic(decl.span, *problem).with_code("E0782");
            }
            symbol(id).external = true;
            set_attribute(symbol(id), AttributeId::KernelBinding, kernel_binding_payload(kernel, version));
        }
        if (decl.scheduler_binding) {
            // The NVM scheduler implements the function (Concurrency.md
            // section 10.1). It is not a host service: it needs no import
            // and no capability, and a call lowers to a SchedulerCall.
            const auto file = source_file(program_, id);
            if (file == no_symbol || symbol(file).library_path.empty() ||
                symbol(symbol(id).owner).kind != SymbolKind::Module)
                throw Diagnostic(decl.span, "scheduler bindings are only allowed in standard library modules")
                    .with_code("E0710");
            const auto operation = scheduler_operation(*decl.scheduler_binding);
            if (!operation)
                throw Diagnostic(decl.span, "unknown scheduler operation '" + *decl.scheduler_binding + "'")
                    .with_code("E0710");
            if (decl.exported || decl.override_member || decl.static_member ||
                std::ranges::any_of(decl.parameters, &Parameter::variadic) ||
                decl.parameters.size() != operation->arguments.size() ||
                (operation->typed && decl.generics.size() != 1))
                throw Diagnostic(decl.span, "a scheduler binding needs a private function with the operation's arguments")
                    .with_code("E0710");
            scheduler_bindings_.emplace(id, *decl.scheduler_binding);
        }
        if (decl.host_callback) {
            // A callback: a function with a body that a host service may
            // call while it serves a request (NVR CALB). Its signature uses
            // host data like a binding's, and its result is host data or
            // void.
            const auto file = source_file(program_, id);
            if (file == no_symbol || symbol(file).library_path.empty() ||
                symbol(symbol(id).owner).kind != SymbolKind::Module)
                throw Diagnostic(decl.span, "host callbacks are only allowed in standard library modules")
                    .with_code("E0710");
            if (!decl.generics.empty() || decl.override_member ||
                std::ranges::any_of(decl.parameters, &Parameter::variadic) ||
                !host_service_name(decl.host_callback->first))
                throw Diagnostic(decl.span, "a host callback needs a non-generic function and a qualified service name")
                    .with_code("E0710");
            for (auto parameter : symbol(id).parameters) {
                if (!host_data_type(symbol(parameter).type))
                    throw Diagnostic(symbol(parameter).span, "this parameter cannot cross the host boundary")
                        .with_code("E0710");
            }
            if (symbol(id).type.kind != TypeKind::Void && !host_data_type(symbol(id).type))
                throw Diagnostic(decl.span, "a host callback result must be void or cross the host boundary")
                    .with_code("E0710");
            symbol(id).host_binding = IrHostBinding{decl.host_callback->first, decl.host_callback->second};
        }
    } else {
        resolve_binding(id, decl);
    }
    entries_[id].resolution = Resolution::Resolved;
}

void Lowering::check_overloads(SymbolId id) {
    const auto branches = entries_[id].overloads;
    if (branches.empty()) return;
    std::vector<SymbolId> seen{id};
    for (const auto branch : branches) {
        resolve(branch);
        const auto equal_type = [&](SymbolId a, SymbolId b) { return symbol(a).type == symbol(b).type; };
        for (const auto previous : seen) {
            if (std::ranges::equal(symbol(previous).parameters, symbol(branch).parameters, equal_type))
                throw Diagnostic(symbol(branch).span, "duplicate overload parameter signature for '" +
                                                       symbol(id).name + "'").with_code("E0433");
        }
        seen.push_back(branch);
    }
}

void Lowering::check_override(SymbolId id, const Declaration& decl) {
    if (decl.cast_target) return;
    auto owner = decl.static_member ? symbol(symbol(id).owner).owner : symbol(id).owner;
    auto base = symbol(owner).base;
    auto inherited = base == no_symbol ? no_symbol : member(base, decl.name, true);
    if (symbol(owner).kind != SymbolKind::Class || decl.name != "init") {
        for (auto scope = base; scope != no_symbol; scope = symbol(scope).base) {
            const auto ancestor = own_member(scope, decl.name);
            if (ancestor != no_symbol &&
                (!decl.overloads.empty() || !entries_[ancestor].overloads.empty()))
                throw Diagnostic(decl.span, "inherited overload groups cannot be redeclared or overridden")
                    .with_code("E0502");
        }
    }
    if (inherited != no_symbol && (decl.static_member || static_member(inherited))) {
        if (decl.override_member)
            throw Diagnostic(decl.span, "a static member is hidden without .override").with_code("E0501");
        if (symbol(id).kind == SymbolKind::Function || symbol(inherited).kind == SymbolKind::Function) {
            resolve(inherited);
            const auto same_parameter = [&](SymbolId a, SymbolId b) { return symbol(a).type == symbol(b).type; };
            if (decl.static_member != static_member(inherited) ||
                symbol(inherited).kind != SymbolKind::Function || symbol(id).kind != SymbolKind::Function ||
                !symbol(id).generics.empty() || !symbol(inherited).generics.empty() ||
                !assignable(symbol(id).type, symbol(inherited).type) ||
                !std::ranges::equal(symbol(id).parameters, symbol(inherited).parameters, same_parameter))
                throw Diagnostic(decl.span, "static function does not match inherited member '" + decl.name + "'")
                    .with_code("E0501");
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
                    if (!symbol(id).exported) throw Diagnostic(decl.span, "an override must be exported")
                        .with_code("E0502");
                    symbol(id).overrides = requirement;
                    return;
                }
            }
        }
        throw Diagnostic(decl.span, "no inherited member named '" + decl.name + "'").with_code("E0502");
    }
    if (inherited == no_symbol) return;
    if (!decl.override_member) {
        auto error = Diagnostic(decl.span, "inherited member '" + decl.name + "' requires .override").with_code("E0502")
            .label("replaces the inherited '" + decl.name + "'")
            .label(name_span(inherited), "'" + decl.name + "' is inherited from here");
        // The keyword of the declaration, `let` or `const`, takes `.override`.
        if (const auto text = source_text(decl.span); text && user_source(decl.span.file)) {
            const auto keyword = decl.constant ? std::string("const") : std::string("let");
            for (std::size_t at = 0; (at = text->find(keyword, at)) != std::string::npos; at += keyword.size()) {
                const bool word = (at == 0 || text->at(at - 1) == ' ') && at + keyword.size() < text->size() &&
                                  text->at(at + keyword.size()) == ' ';
                if (!word) continue;
                if (const auto after = source_span(decl.span.file, decl.span.offset + at + keyword.size(), 0))
                    error.suggest("declare it as an override", *after, ".override", Applicability::MaybeIncorrect);
                break;
            }
        }
        throw error;
    }
    if (!decl.exported) throw Diagnostic(decl.span, "an override must be exported").with_code("E0502");
    if (symbol(owner).kind == SymbolKind::Class && symbol(id).kind != SymbolKind::Function)
        throw Diagnostic(decl.span, "class field overrides are not supported").with_code("E0502");
    resolve(inherited);
    bool match = symbol(id).kind == symbol(inherited).kind &&
                 symbol(id).type == symbol(inherited).type &&
                 symbol(id).generics.empty() && symbol(inherited).generics.empty();
    const auto& ours = symbol(id).parameters;
    const auto& theirs = symbol(inherited).parameters;
    match = match && std::ranges::equal(ours, theirs, [this](auto a, auto b) {
        return symbol(a).type == symbol(b).type;
    });
    if (!match) throw Diagnostic(decl.span, "override does not match inherited member '" + decl.name + "'")
        .with_code("E0502");
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
            throw Diagnostic(symbol(id).span, "cannot inherit different instances of interface '" + view.name + "'")
                .with_code("E0503");
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
                throw Diagnostic(symbol(id).span, "incompatible interface requirements for '" + name + "'")
                    .with_code("E0504");
            }
        }
        const auto implementation = is_class ? member(id, name, false) : own_member(id, name);
        if (implementation != no_symbol) {
            resolve(implementation);
            const auto& candidate = symbol(implementation);
            if (candidate.kind != SymbolKind::Function || static_member(implementation) ||
                !entries_[implementation].overloads.empty() ||
                !candidate.exported || !candidate.generics.empty() ||
                signature(implementation, nominal) != required) {
                throw Diagnostic(candidate.span, "member '" + name + "' does not match the interface requirement")
                    .with_code("E0504");
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
            throw Diagnostic(symbol(id).span, "multiple interface defaults for '" + name + "' require an override")
                .with_code("E0504");
        }
        if (defaults == 0 && is_class) {
            throw Diagnostic(symbol(id).span, "class must declare interface method '" + name + "'").with_code("E0504");
        }
    }
}

IrNode Lowering::reference(SymbolId id, const Span& span, Context& context, bool implicit_receiver) {
    resolve(id);
    check_tagged_use(program_, id, span, lexical_owner(context), [&](std::string_view name) {
        SpeculativeDiagnostics lookup;
        try { return find(name, context, span); } catch (const Diagnostic&) { return no_symbol; }
    });
    if (entries_[id].singleton_instance != no_symbol) id = entries_[id].singleton_instance;
    const auto& target = symbol(id);
    if (context.closure && target.owner != context.owner &&
        (target.kind == SymbolKind::Local || target.kind == SymbolKind::Parameter)) {
        return capture(*context.closure, id, reference(id, span, context.closure->enclosing), span);
    }
    if (context.function == no_symbol && target.owner == context.owner &&
        (target.kind == SymbolKind::Global || target.kind == SymbolKind::Field) &&
        target.span.file == span.file && target.span.offset > span.offset) {
        throw Diagnostic(span, "'" + target.name + "' is used before its declaration").with_code("E0306")
            .label("used here")
            .label(name_span(id), "'" + target.name + "' is declared here, later in the file")
            .help("move the declaration of '" + target.name + "' above its first use");
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
        throw Diagnostic(span, "instance member '" + target.name + "' requires a receiver").with_code("E0508");
    return result;
}

IrNode Lowering::wrapped_member(const Expr& expr, const IrType& type, Context& context,
                               std::optional<IrNode> receiver) {
    const auto wrapper = primitive_wrapper(type);
    const auto message = "type " + describe(type) + " has no member '" + expr.text + "'";
    if (!wrapper) throw Diagnostic(expr.span, message).with_code("E0305");
    const auto file = library_files_.find(wrapper->path);
    if (file == library_files_.end()) {
        MissingPrimitiveWrapper missing(expr.span, message, std::string(wrapper->path));
        missing.with_code("E0305");
        throw missing;
    }
    const auto module = own_member(file->second, wrapper->module);
    if (module == no_symbol || symbol(module).kind != SymbolKind::Module)
        throw Diagnostic(expr.span, "invalid primitive wrapper for " + describe(type)).with_code("E0899");
    resolve(module);
    const auto method = member(module, expr.text, true);
    if (method == no_symbol || symbol(method).kind != SymbolKind::Function)
        throw Diagnostic(expr.span, message).with_code("E0305");
    const auto from = source_file(program_, lexical_owner(context));
    if (from != no_symbol && from != file->second && wrapper_imports_.emplace(from, module).second) {
        const auto dependency = add(SymbolKind::Import, "$primitive", expr.span, from);
        symbol(dependency).type = symbol(module).type;
    }
    auto result = reference(method, expr.span, context, false);
    result.operands.push_back(reference(module, expr.span, context, false));
    if (receiver) result.operands.push_back(std::move(*receiver));
    return result;
}

IrNode Lowering::select(const Expr& expr, Context& context, bool for_call) {
    const auto& operand = expr.operands.front();
    if (operand.kind == ExprKind::Name && operand.text == "super") {
        auto receiver = context.receiver;
        if (receiver == no_symbol || context.function == no_symbol || context.closure ||
            symbol(receiver).kind != SymbolKind::Class || symbol(receiver).base == no_symbol ||
            symbol(receiver).base == root_class_ ||
            symbol(context.function).name != "init" || expr.text != "init")
            throw Diagnostic(expr.span, "super.init is only available in a derived constructor").with_code("E0506");
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
    if (operand.kind == ExprKind::Name) {
        const auto builtin = primitive(operand.text);
        if (builtin && primitive_wrapper(*builtin) && find(operand.text, context, operand.span) == no_symbol)
            return wrapped_member(expr, *builtin, context);
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
        throw Diagnostic(expr.span, "type " + describe(nominal) + " has no member '" + expr.text + "'")
            .with_code("E0305");
    }
    if (nominal.kind != TypeKind::Object && nominal.kind != TypeKind::Module &&
        nominal.kind != TypeKind::Enum) {
        if (for_call && primitive_wrapper(nominal) && receiver.op != IrOp::Reference)
            return wrapped_member(expr, nominal, context, std::move(receiver));
        if (receiver.op == IrOp::Reference && receiver.symbol != no_symbol &&
            symbol(receiver.symbol).kind == SymbolKind::Alias && primitive_wrapper(nominal))
            return wrapped_member(expr, nominal, context);
        throw Diagnostic(expr.span, "type " + describe(nominal) + " has no members").with_code("E0305");
    }
    if (nominal.kind == TypeKind::Enum &&
        (receiver.op != IrOp::Reference || receiver.symbol == no_symbol ||
         (symbol(receiver.symbol).kind != SymbolKind::Enum && symbol(receiver.symbol).kind != SymbolKind::Alias))) {
        throw Diagnostic(expr.span, "qualify a variant with its enum type").with_code("E0427");
    }
    bool type_reference = receiver.op == IrOp::Reference && receiver.symbol != no_symbol &&
        (class_or_interface(symbol(receiver.symbol).kind) ||
         symbol(receiver.symbol).kind == SymbolKind::Alias);
    if (nominal.kind == TypeKind::Object && (expr.text == "init" || expr.text == "deinit"))
        throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy").with_code("E0509");
    const bool exported = !inside(lexical_owner(context), nominal.symbol);
    auto target = type_reference || nominal.kind != TypeKind::Object
        ? member(nominal.symbol, expr.text, exported) : no_symbol;
    if (target == no_symbol && nominal.kind == TypeKind::Object)
        target = value_member(receiver.type, expr.text, exported);
    if (target == no_symbol) {
        auto error = Diagnostic(expr.span, "unknown or inaccessible member '" + expr.text + "' of " +
                                           describe(nominal)).with_code("E0305")
            .arg("kind", "member").arg("name", expr.text).arg("owner", describe(nominal));
        auto hidden = type_reference || nominal.kind != TypeKind::Object
            ? member(nominal.symbol, expr.text, false) : no_symbol;
        if (hidden == no_symbol && nominal.kind == TypeKind::Object) hidden = value_member(receiver.type, expr.text, false);
        if (hidden != no_symbol && exported) {
            not_exported(error, hidden);
        } else {
            error.label(describe(nominal) + " has no member '" + expr.text + "'");
            suggest_name(error, expr.span, expr.text,
                         member_names(type_reference || nominal.kind != TypeKind::Object ? nominal : receiver.type, exported),
                         "a member");
        }
        throw error;
    }
    auto result = reference(target, expr.span, context, false);
    if (static_member(target)) {
        if (!type_reference) {
            const auto kind = symbol(target).kind;
            const auto declaring_type = symbol(symbol(target).owner).owner;
            if ((kind != SymbolKind::Global && kind != SymbolKind::Function) ||
                (kind == SymbolKind::Function && symbol(declaring_type).kind == SymbolKind::Interface)) {
                throw Diagnostic(expr.span, "static member '" + expr.text + "' requires a class or interface name")
                    .with_code("E0501");
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
            throw Diagnostic(expr.span, "qualify a generic enum variant with explicit type arguments")
                .with_code("E0419");
        result.type = nominal;
    }
    if (kind == SymbolKind::Field || kind == SymbolKind::Function || kind == SymbolKind::Global) {
        if (type_reference)
            throw Diagnostic(expr.span, "member '" + expr.text + "' requires an instance").with_code("E0508");
        result.type = substitute(std::move(result.type), receiver_arguments(receiver.type, symbol(target).owner));
        result.operands.clear();
        result.operands.push_back(std::move(receiver));
        if (kind == SymbolKind::Field) return route_field(std::move(result), expr, context);
    }
    return result;
}

void Lowering::arguments(IrNode& call, const Expr& expr, std::size_t first, SymbolId target,
                         const Substitution& replacements, Context& context, std::size_t leading) {
    auto params = target == no_symbol ? std::vector<SymbolId>{} : symbol(target).parameters;
    const auto variadic = target == no_symbol ? VariadicKind::None : symbol(target).variadic;
    const bool rest = variadic != VariadicKind::None;
    const auto fixed = params.size() - leading - (rest ? 1 : 0);
    const auto count = expr.operands.size() - first;
    if (count < fixed || (!rest && count != fixed)) {
        auto error = Diagnostic(expr.span, "expected " + std::string(rest ? "at least " : "") +
                                           std::to_string(fixed) + " argument(s)").with_code("E0407")
            .label("expected " + std::string(rest ? "at least " : "") + plural(fixed, "argument") + ", found " +
                   std::to_string(count));
        if (target != no_symbol && !symbol(target).span.file.empty())
            error.label(name_span(target), "'" + symbol(target).name + "' takes " +
                                               std::string(rest ? "at least " : "") + plural(fixed, "parameter"));
        throw error;
    }
    for (std::size_t i = 0; i < fixed; ++i) {
        auto expected = substitute(symbol(params[i + leading]).type, replacements);
        const auto& parameter = symbol(params[i + leading]);
        if (!parameter.span.file.empty()) {
            const auto* function = entries_[target].declaration;
            const auto index = i + leading;
            expected_origin_ = ExpectedOrigin{parameter.span, "parameter '" + parameter.name + "' is declared here",
                function && index < function->parameters.size() ? &function->parameters[index].type : nullptr};
        }
        call.operands.push_back(value(expr.operands[i + first], context, &expected, true));
    }
    if (!rest) return;
    auto pack = node(IrOp::Collection, expr.span, substitute(symbol(params.back()).type, replacements));
    const auto& element = pack.type.arguments.front();
    for (std::size_t i = fixed; i < count; ++i) {
        const auto& input = expr.operands[i + first];
        pack.operands.push_back(variadic == VariadicKind::Untyped
            ? pack_argument(input, element, context) : value(input, context, &element, true));
    }
    call.operands.push_back(std::move(pack));
}

SymbolId Lowering::cast_method(const IrType& source, const IrType& target, const Context& context,
                               bool diagnose_ambiguity) {
    const auto view = receiver_type(source);
    if (view.kind != TypeKind::Object || symbol(view.symbol).kind != SymbolKind::Class)
        return no_symbol;
    for (auto scope = view.symbol; scope != no_symbol; scope = symbol(scope).base) {
        const auto casts = entries_[scope].casts;
        const auto replacements = receiver_arguments(source, scope);
        auto found = no_symbol;
        for (const auto method : casts) {
            if (!symbol(method).exported && !inside(lexical_owner(context), scope)) continue;
            resolve(method);
            if (substitute(symbol(method).type, replacements) != target) continue;
            if (found != no_symbol) {
                if (diagnose_ambiguity)
                    throw Diagnostic(symbol(method).span, "ambiguous cast to " + describe(target)).with_code("E0409");
                return no_symbol;
            }
            found = method;
        }
        if (found != no_symbol) return found;
    }
    return no_symbol;
}

IrNode Lowering::cast_call(SymbolId target, IrNode source, const IrType& type, const Span& span) {
    auto result = node(IrOp::VirtualCall, span, type);
    result.symbol = target;
    result.operands.push_back(std::move(source));
    return result;
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
                                        " as a variadic argument; pass its text representation explicitly")
                .with_code("E0403");
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
        throw Diagnostic(expr.span, "use the variant's declared payload constructor").with_code("E0427");
    if (fields.size() != expr.operands.size() - 1)
        throw Diagnostic(expr.span, "expected " + std::to_string(fields.size()) + " enum payload field(s)")
            .with_code("E0427");
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
                throw Diagnostic(expr.span, "unknown or repeated enum payload field '" + name + "'").with_code("E0427");
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
        throw Diagnostic(expr.span, "tuple literal has the wrong number of elements").with_code("E0426");
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
        throw Diagnostic(expr.span, "expected one argument and no type arguments").with_code("E0407");
    const auto& input = expr.operands[1];
    if (bits_type) {
        auto result = node(IrOp::ReinterpretFloat, expr.span, *target);
        result.operands.push_back(value(input, context, &*bits_type));
        return result;
    }
    const auto byte_array = collection_type(TypeKind::Array, uint8_type);
    auto bytes = value(input, context, input.kind == ExprKind::Collection ? &byte_array : nullptr);
    if (!collection(bytes.type) || bytes.type.arguments.front() != uint8_type)
        throw Diagnostic(input.span, "string.from_utf8 requires an array or list of bytes").with_code("E0403");
    auto result = node(IrOp::TextFromUtf8, expr.span, {TypeKind::Tuple, {}, no_symbol, {bool_type, string_type}});
    result.operands.push_back(std::move(bytes));
    return result;
}

IrNode Lowering::call(const Expr& expr, Context& context) {
    if (auto result = primitive_call(expr, context)) return std::move(*result);
    const auto& callee_expr = expr.operands.front();
    // `spawn (f)(x)` parses as a call of a function named spawn. Without one,
    // say what the form requires (Concurrency.md section 4.4).
    if (callee_expr.kind == ExprKind::Name && callee_expr.text == "spawn" && expr.open &&
        find("spawn", context, callee_expr.span) == no_symbol)
        throw Diagnostic(*expr.open, "a spawn operand begins with a name; bind the callee to a name first")
            .with_code("E0603");
    auto callee = callee_expr.kind == ExprKind::Member
        ? select(callee_expr, context, true) : expression(callee_expr, context);
    if (callee.op == IrOp::Reference && callee.symbol != no_symbol &&
        symbol(callee.symbol).kind == SymbolKind::Variant) {
        return enum_construct(callee, expr, context);
    }
    if (is_function_value(callee.type)) {
        if (!expr.type_arguments.empty()) throw Diagnostic(expr.span, "function values take no type arguments")
            .with_code("E0417");
        const auto signature = callee.type.arguments;
        if (expr.operands.size() != signature.size()) {
            throw Diagnostic(expr.span, "expected " + std::to_string(signature.size() - 1) + " argument(s)")
                .with_code("E0407");
        }
        auto result = node(IrOp::CallClosure, expr.span, signature.back());
        result.operands.push_back(std::move(callee));
        for (std::size_t i = 1; i < expr.operands.size(); ++i) {
            result.operands.push_back(value(expr.operands[i], context, &signature[i - 1], true));
        }
        return result;
    }
    if (callee.type.kind != TypeKind::Callable)
        throw Diagnostic(expr.operands.front().span, "expression is not callable").with_code("E0423");
    auto target = callee.symbol;
    auto result = node(IrOp::Call, expr.span);
    result.symbol = target;
    if (callee.type.name == "super.init") {
        const auto& first = entries_[context.function].declaration->body.front();
        if (expr.span.offset != first.expressions.front().span.offset) {
            throw Diagnostic(expr.span, "super.init must be the first statement of the constructor").with_code("E0506");
        }
        if (!expr.type_arguments.empty())
            throw Diagnostic(expr.span, "super.init takes no type arguments").with_code("E0417");
        if (target == no_symbol) require_implicit_constructor(callee.operands.front().type.symbol, expr.span);
        result.op = IrOp::BaseInitialize;
        result.operands = std::move(callee.operands);
        result.symbol = select_overload(target, expr, 1, {}, context, result);
        return result;
    }
    if (target == no_symbol) {
        if (const auto operation = intrinsic_value_member(callee.type.name)) {
            const bool equality = *operation == IrOp::ValueEquals;
            if (!expr.type_arguments.empty() || expr.operands.size() != (equality ? 2U : 1U))
                throw Diagnostic(expr.span, equality ? "equals requires one argument" :
                                                      "hash_code and to_string take no arguments").with_code("E0407");
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
                throw Diagnostic(expr.span, "expected " + std::to_string(count) + " argument(s) and no type arguments")
                    .with_code("E0407");
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
            throw Diagnostic(expr.span, "expected one argument and no type arguments").with_code("E0407");
        auto receiver = std::move(callee.operands.front());
        if (callee.type.name == "append") {
            result.op = IrOp::Append;
            auto element = value(expr.operands[1], context, &receiver.type.arguments.front());
            require_scope_local(receiver, element.type, element.span, context);
            result.operands = {std::move(receiver), std::move(element)};
        } else {
            result.op = IrOp::RemoveAt;
            auto index = value(expr.operands[1], context);
            if (index.type.kind != TypeKind::Integer)
                throw Diagnostic(index.span, "collection index must be an integer").with_code("E0414");
            result.operands = {std::move(receiver), std::move(index)};
        }
        return result;
    }
    if (symbol(target).kind != SymbolKind::Function)
        throw Diagnostic(expr.span, "expression is not callable").with_code("E0423");
    resolve(target);
    if (const auto bound = scheduler_bindings_.find(target); bound != scheduler_bindings_.end()) {
        auto replacements = type_arguments(symbol(target).generics, expr.type_arguments, expr.span, context);
        arguments(result, expr, 1, target, replacements, context);
        result.op = IrOp::SchedulerCall;
        result.symbol = no_symbol;
        result.text = bound->second;
        result.type = substitute(symbol(target).type, replacements);
        // A typed operation carries its type argument to the scheduler.
        if (scheduler_operation(bound->second)->typed) {
            result.type_arguments.push_back(replacements.at(symbol(target).generics.front()));
        }
        return result;
    }
    auto qualifier = static_qualifier(callee);
    auto replacements = callee.operands.empty() ? Substitution{}
        : receiver_arguments(callee.operands.front().type, symbol(target).owner);
    auto generics = symbol(target).generics;
    auto instantiated = type_arguments(generics, expr.type_arguments, expr.span, context);
    for (auto generic : generics) result.type_arguments.push_back(instantiated.at(generic));
    replacements.merge(std::move(instantiated));
    if (symbol(target).external) {
        result.op = IrOp::ExternalCall;
    } else if (!callee.operands.empty()) {
        result.op = IrOp::VirtualCall;
    } else if (!symbol(target).has_body) {
        throw Diagnostic(expr.span, "function requires an implementation").with_code("E0434");
    }
    std::optional<IrType> wrapped_receiver;
    if (callee.operands.size() == 2) wrapped_receiver = callee.operands[1].type;
    if (result.op != IrOp::ExternalCall) {
        result.operands = std::move(callee.operands);
    } else if (wrapped_receiver) {
        result.operands.push_back(std::move(callee.operands[1]));
    }
    // Only the wrapper module is a dispatch qualifier. The primitive itself
    // is the first ordinary argument and evaluates before the other arguments.
    result.symbol = select_overload(target, expr, 1, replacements, context, result,
                                    wrapped_receiver ? &*wrapped_receiver : nullptr);
    result.type = substitute(symbol(result.symbol).type, replacements);
    return qualifier ? discard_before(std::move(*qualifier), std::move(result)) : std::move(result);
}

IrNode Lowering::allocate(const Expr& expr, Context& context, const IrType* target_type) {
    auto object = target_type ? *target_type : type(*expr.type, context);
    if (object.kind != TypeKind::Object || symbol(object.symbol).kind != SymbolKind::Class)
        throw Diagnostic(expr.span, "new requires a class type").with_code("E0505");
    const auto id = object.symbol;
    if (entries_[id].singleton_instance != no_symbol)
        throw Diagnostic(expr.span, "a singleton cannot be constructed with new").with_code("E0505");
    if (id == root_class_)
        throw Diagnostic(expr.span, "class is an umbrella type and cannot be constructed").with_code("E0505");
    Names effective;
    for (auto scope = id; scope != no_symbol; scope = symbol(scope).base) {
        for (const auto& [name, member_id] : entries_[scope].names) {
            if (name != "init" && name != "deinit") effective.emplace(name, member_id);
        }
    }
    for (const auto& [name, member_id] : effective) {
        if (symbol(member_id).kind == SymbolKind::Function && !symbol(member_id).has_body)
            throw Diagnostic(expr.span, "cannot instantiate abstract class " + describe(object) +
                                        " with unimplemented method '" + name + "'").with_code("E0505");
    }
    auto target = own_member(id, "init");
    if (target != no_symbol) {
        resolve(target);
        if (!symbol(target).exported && !inside(lexical_owner(context), id))
            throw Diagnostic(expr.span, "constructor is not exported").with_code("E0507");
    } else {
        require_implicit_constructor(id, expr.span);
    }
    auto result = node(IrOp::Allocate, expr.span, object);
    result.symbol = select_overload(target, expr, 0, receiver_arguments(object), context, result);
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
            throw Diagnostic(expr.span, "float literal is outside the range of " + result.type.name).with_code("E0424");
        if (parsed.error == FloatLiteralError::Underflow)
            throw Diagnostic(expr.span, "nonzero float literal rounds to zero in " + result.type.name)
                .with_code("E0424");
        if (parsed.error)
            throw Diagnostic(expr.span, "malformed float literal").with_code("E0105");
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
        throw Diagnostic(expr.span, "integer literal is outside the range of " + name).with_code("E0424");
    return result;
}

bool numeric_literal(const Expr& expr) {
    if (expr.kind == ExprKind::Integer || expr.kind == ExprKind::Float) return true;
    return expr.kind == ExprKind::Unary && expr.text == "-" &&
           (expr.operands.front().kind == ExprKind::Integer ||
            expr.operands.front().kind == ExprKind::Float);
}

bool literal_tree(const Expr& expr) {
    switch (expr.kind) {
    case ExprKind::String:
    case ExprKind::Boolean:
    case ExprKind::Nil:
        return true;
    case ExprKind::Collection:
        return std::ranges::all_of(expr.operands, literal_tree);
    default:
        return numeric_literal(expr);
    }
}

SymbolId Lowering::select_overload(SymbolId group, const Expr& expr, std::size_t first,
                                  const Substitution& replacements, Context& context, IrNode& call,
                                  const IrType* wrapped_receiver) {
    const std::size_t leading = wrapped_receiver ? 1 : 0;
    const auto count = expr.operands.size() - first;
    const auto usable_receiver = [&](SymbolId method) {
        return !wrapped_receiver || (!symbol(method).parameters.empty() &&
            symbol(symbol(method).parameters.front()).type == *wrapped_receiver);
    };
    if (group == no_symbol || entries_[group].overloads.empty()) {
        if (!usable_receiver(group))
            throw Diagnostic(expr.span, "primitive method '" + symbol(group).name +
                                        "' requires a matching receiver parameter").with_code("E0899");
        arguments(call, expr, first, group, replacements, context, leading);
        return group;
    }
    if (!expr.type_arguments.empty())
        throw Diagnostic(expr.span, "overloaded functions take no type arguments").with_code("E0417");
    auto branches = entries_[group].overloads;
    branches.insert(branches.begin(), group);
    std::vector<SymbolId> candidates;
    for (const auto branch : branches) {
        resolve(branch);
        if (symbol(branch).parameters.size() == count + leading && usable_receiver(branch))
            candidates.push_back(branch);
    }
    if (candidates.size() == 1) {
        arguments(call, expr, first, candidates.front(), replacements, context, leading);
        return candidates.front();
    }
    const auto no_match = [&] {
        auto error = Diagnostic(expr.span, "no matching overload of '" + symbol(group).name + "'").with_code("E0408")
            .label("no alternative of '" + symbol(group).name + "' accepts " +
                   (count == 0 ? std::string("no arguments") : count == 1 ? std::string("this argument")
                                                                           : std::string("these arguments")));
        constexpr std::size_t shown = 3;
        for (std::size_t i = 0; i < branches.size() && i < shown; ++i) {
            std::string signature;
            for (std::size_t p = leading; p < symbol(branches[i]).parameters.size(); ++p)
                signature += (signature.empty() ? "" : ", ") + describe(symbol(symbol(branches[i]).parameters[p]).type);
            error.label(name_span(branches[i]), "this alternative takes (" + signature + ")");
        }
        if (branches.size() > shown) error.note("and " + plural(branches.size() - shown, "more alternative"));
        return error;
    };
    if (candidates.empty()) throw no_match();

    // Lower ordinary expressions once. Contextual literal trees can be
    // checked against each signature without creating bindings or calls.
    // A target-typed `new` is contextual too: it fits a parameter whose type
    // is a class it can construct, and it is lowered once, for the chosen one.
    const auto target_typed = [](const Expr& input) { return input.kind == ExprKind::New && !input.type; };
    const auto contextual = [&](const Expr& input) {
        return numeric_literal(input) || input.kind == ExprKind::Nil ||
            (input.kind == ExprKind::Collection && literal_tree(input)) || target_typed(input);
    };
    std::vector<std::optional<IrNode>> inputs;
    std::vector<std::optional<IrType>> defaults;
    for (std::size_t i = first; i < expr.operands.size(); ++i) {
        const auto& input = expr.operands[i];
        inputs.emplace_back();
        defaults.emplace_back();
        if (!contextual(input)) {
            inputs.back() = value(input, context);
        } else if (input.kind != ExprKind::Nil && !target_typed(input)) {
            // Lowered again below with the chosen parameter type.
            SpeculativeDiagnostics speculative;
            try {
                defaults.back() = value(input, context).type;
            } catch (const Diagnostic&) {
                // For example, an empty array needs a contextual element type.
            }
        }
    }
    enum class Rank { Exact, Assignable, ReferenceBinding, Contextual, Cast };
    struct Match {
        SymbolId branch;
        std::vector<Rank> ranks;
        std::vector<IrType> types;
        std::vector<SymbolId> casts;
    };
    std::vector<Match> matches;
    for (const auto branch : candidates) {
        Match match{branch, {}, {}, {}};
        for (std::size_t i = 0; i < count; ++i) {
            const auto& input = expr.operands[i + first];
            auto expected = substitute(symbol(symbol(branch).parameters[i + leading]).type, replacements);
            auto conversion = no_symbol;
            Rank rank;
            if (inputs[i]) {
                const auto& argument = *inputs[i];
                const bool read_alias = argument.op == IrOp::Field && !argument.operands.empty() &&
                    is_primitive_reference(argument.operands.front().type);
                if (is_primitive_reference(expected)) {
                    if (!can_bind_reference(argument, expected)) break;
                    rank = read_alias ? Rank::Exact : Rank::ReferenceBinding;
                } else if (argument.type == expected) {
                    rank = read_alias ? Rank::Assignable : Rank::Exact;
                } else if (assignable(argument.type, expected)) {
                    rank = Rank::Assignable;
                } else if ((conversion = cast_method(argument.type, expected, context, false)) != no_symbol) {
                    rank = Rank::Cast;
                } else {
                    break;
                }
            } else if (target_typed(input)) {
                const auto& object = expected;
                if (object.kind != TypeKind::Object || symbol(object.symbol).kind != SymbolKind::Class ||
                    object.symbol == root_class_ || entries_[object.symbol].singleton_instance != no_symbol)
                    break;
                rank = Rank::Contextual;
            } else {
                SpeculativeDiagnostics speculative;
                try {
                    value(input, context, &expected);
                } catch (const Diagnostic&) {
                    break;
                }
                rank = defaults[i] == expected ? Rank::Exact : Rank::Contextual;
            }
            match.ranks.push_back(rank);
            match.types.push_back(std::move(expected));
            match.casts.push_back(conversion);
        }
        if (match.ranks.size() == count) matches.push_back(std::move(match));
    }
    const auto better = [&](const Match& a, const Match& b) {
        bool strictly_better = false;
        for (std::size_t i = 0; i < count; ++i) {
            if (a.ranks[i] > b.ranks[i]) return false;
            if (a.ranks[i] < b.ranks[i]) strictly_better = true;
            else if (a.ranks[i] == Rank::Assignable || expr.operands[i + first].kind == ExprKind::Nil) {
                const bool narrower = assignable(a.types[i], b.types[i]);
                const bool wider = assignable(b.types[i], a.types[i]);
                if (wider && !narrower) return false;
                if (narrower && !wider) strictly_better = true;
            }
        }
        return strictly_better;
    };
    const Match* selected = nullptr;
    for (const auto& match : matches) {
        if (std::ranges::any_of(matches, [&](const Match& other) { return better(other, match); }))
            continue;
        if (selected) throw Diagnostic(expr.span, "ambiguous overload of '" + symbol(group).name + "'")
            .with_code("E0409");
        selected = &match;
    }
    if (!selected) throw no_match();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& input = expr.operands[i + first];
        auto argument = inputs[i] ? std::move(*inputs[i]) : value(input, context, &selected->types[i]);
        if (is_primitive_reference(selected->types[i]))
            argument = bind_reference(std::move(argument), selected->types[i]);
        else if (selected->casts[i] != no_symbol)
            argument = cast_call(selected->casts[i], std::move(argument), selected->types[i], input.span);
        call.operands.push_back(std::move(argument));
    }
    return selected->branch;
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
        throw Diagnostic(span, "operator '" + op + "' is not defined for " + describe(left.type)).with_code("E0413");
    if (is_shift(op) && right.type.kind != TypeKind::Integer)
        throw Diagnostic(right.span, "a shift count must be an integer").with_code("E0413");
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
        throw Diagnostic(expr.span, "operator '" + op + "' is not defined for " + describe(left.type))
            .with_code("E0413");
    const auto op_kind = equality && left.type.kind == TypeKind::String ? IrOp::TextEqual : IrOp::Binary;
    auto result = node(op_kind, expr.span, bool_type);
    result.text = op;
    result.operands.push_back(std::move(left));
    result.operands.push_back(std::move(right));
    return result;
}

IrNode Lowering::conditional(const Expr& expr, Context& context, const IrType* expected, bool call_argument) {
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
        auto when_true = value(yes, context, expected, call_argument);
        result.type = expected ? *expected : when_true.type;
        result.body.push_back(std::move(when_true));
        result.alternative.push_back(value(no, context, &result.type, call_argument && expected));
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
        throw Diagnostic(subject_expr.span, "operator '" + expr.text + "' is not defined for " + describe(subject_type))
            .with_code("E0413");
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
    if (!format) throw Diagnostic(start.span, "a range requires integer bounds and step").with_code("E0429");
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

IrNode Lowering::expression(const Expr& expr, Context& context, const IrType* expected, bool call_argument) {
    auto result = expression_node(expr, context, expected, call_argument);
    if (is_primitive_reference(result.type) && (!expected || result.type != *expected))
        return read_reference(std::move(result));
    return result;
}

IrNode Lowering::expression_node(const Expr& expr, Context& context, const IrType* expected, bool call_argument) {
    switch (expr.kind) {
    case ExprKind::TypeApplication: {
        auto target = expression(expr.operands.front(), context);
        if (target.op != IrOp::Reference || target.symbol == no_symbol ||
            symbol(target.symbol).kind != SymbolKind::Enum)
            throw Diagnostic(expr.span, "a parameterized type reference requires a generic enum").with_code("E0417");
        target.type = apply_arguments(target.type,
            type_arguments(symbol(target.symbol).generics, expr.type_arguments, expr.span, context));
        target.span = expr.span;
        return target;
    }
    case ExprKind::Lambda:
        return lambda(*expr.lambda, context);
    case ExprKind::Spawn:
        return spawn(expr, context);
    case ExprKind::Name: {
        SymbolId promoted_receiver = no_symbol;
        auto id = find(expr.text, context, expr.span, &promoted_receiver);
        if (id == no_symbol && expr.text == "instance" && context.receiver != no_symbol &&
            class_or_interface(symbol(context.receiver).kind)) {
            return receiver(context.receiver, expr.span, context);
        }
        if (id == no_symbol) {
            auto error = Diagnostic(expr.span, "unknown name '" + expr.text + "'").with_code("E0304")
                .label("not found in this scope");
            suggest_name(error, expr.span, expr.text, visible_names(context), "a binding");
            throw error;
        }
        if (symbol(id).kind == SymbolKind::Function &&
            owner_kind(symbol(id).owner, SymbolKind::Class) != no_symbol &&
            (expr.text == "init" || expr.text == "deinit")) {
            throw Diagnostic(expr.span, "lifecycle functions require new, super.init, or destroy").with_code("E0509");
        }
        auto result = reference(id, expr.span, context, promoted_receiver == no_symbol);
        if (symbol(id).kind == SymbolKind::Field) result = route_field(std::move(result), expr, context);
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
        result.text = decode_string_literal(expr.text);
        return result;
    }
    case ExprKind::Nil: {
        if (expected && !class_reference(*expected))
            throw Diagnostic(expr.span, "nil requires a class reference type").with_code("E0425");
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
            throw Diagnostic(expr.span, "unary '~' requires an integer operand").with_code("E0413");
        if (expr.text == "-" && input.type.kind != TypeKind::Integer && input.type.kind != TypeKind::Float)
            throw Diagnostic(expr.span, "unary minus requires a numeric operand").with_code("E0413");
        auto result = node(IrOp::Unary, expr.span, input.type);
        result.text = expr.text;
        result.operands.push_back(std::move(input));
        return result;
    }
    case ExprKind::Binary:
        return binary(expr, context, expected);
    case ExprKind::Conditional:
        return conditional(expr, context, expected, call_argument);
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
                throw Diagnostic(index.span, "tuple indexing requires a nonnegative literal in range")
                    .with_code("E0414");
            auto result = node(IrOp::Project, expr.span, receiver.type.arguments[*field]);
            result.text = std::to_string(*field);
            result.operands.push_back(std::move(receiver));
            return result;
        }
        if (!collection(receiver.type))
            throw Diagnostic(expr.span, "indexing requires an array or list").with_code("E0414");
        auto index = value(expr.operands[1], context);
        if (index.type.kind != TypeKind::Integer)
            throw Diagnostic(index.span, "collection index must be an integer").with_code("E0414");
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
                throw Diagnostic(expr.span, "empty collection literal needs an expected type").with_code("E0425");
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
        return construct(expr, context, expected);
    case ExprKind::NamedVariant: {
        const auto target = expression(expr.operands.front(), context);
        if (target.op != IrOp::Reference || target.symbol == no_symbol ||
            symbol(target.symbol).kind != SymbolKind::Variant) {
            throw Diagnostic(expr.span, "expected a named enum variant").with_code("E0427");
        }
        return enum_construct(target, expr, context);
    }
    case ExprKind::Match:
        return lower_match(*expr.match, context, true, expected, call_argument);
    case ExprKind::TypeTest:
    case ExprKind::Cast: {
        auto target = type(*expr.type, context);
        if (is_primitive_reference(target))
            throw Diagnostic(expr.type->span, "a primitive reference must be bound to storage").with_code("E0428");
        auto source = value(expr.operands.front(), context);
        if (expr.kind == ExprKind::Cast) {
            if (const auto method = cast_method(source.type, target, context); method != no_symbol)
                return cast_call(method, std::move(source), target, expr.span);
        }
        if (class_reference(source.type) && class_reference(target)) {
            const auto test = expr.kind == ExprKind::TypeTest;
            auto result = node(test ? IrOp::ClassIs : IrOp::ClassCast, expr.span, test ? bool_type : target);
            if (test) result.type_arguments.push_back(target);
            result.operands.push_back(std::move(source));
            return result;
        }
        if (expr.kind == ExprKind::TypeTest)
            throw Diagnostic(expr.span, "is requires class reference types").with_code("E0415");
        const auto numeric = [](const IrType& t) {
            return t.kind == TypeKind::Integer || t.kind == TypeKind::Float;
        };
        const bool allowed = numeric(target) &&
            (numeric(source.type) || (source.type.kind == TypeKind::Enum && target.kind == TypeKind::Integer &&
                                      !has_enum_payload(program_, source.type.symbol)));
        if (!allowed)
            throw Diagnostic(expr.span, "cannot convert " + describe(source.type) + " to " + describe(target))
                .with_code("E0415");
        auto result = node(IrOp::Convert, expr.span, std::move(target));
        result.operands.push_back(std::move(source));
        return result;
    }
    case ExprKind::Import:
        throw Diagnostic(expr.span, "import must initialize a module-level alias").with_code("E0704");
    }
    throw Diagnostic(expr.span, "unsupported expression").with_code("E0899");
}

IrNode Lowering::value(const Expr& expr, Context& context, const IrType* expected, bool call_argument) {
    const auto origin = std::exchange(expected_origin_, std::nullopt);
    if (expr.kind == ExprKind::New && origin) new_origin_.emplace(&expr, *origin);
    auto result = expression(expr, context, expected, call_argument);
    if (expected && is_primitive_reference(*expected))
        return bind_reference(std::move(result), *expected);
    if (result.op == IrOp::Reference && result.symbol != no_symbol &&
        symbol(result.symbol).kind == SymbolKind::Variant) {
        const auto& variant = symbol(result.symbol);
        if (!variant.payload_types.empty())
            throw Diagnostic(expr.span, "enum variant '" + variant.name + "' requires a payload").with_code("E0427");
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
        throw Diagnostic(expr.span, "expected a value").with_code("E0422");
    }
    if (expected && call_argument && !assignable(result.type, *expected)) {
        if (const auto method = cast_method(result.type, *expected, context); method != no_symbol)
            return cast_call(method, std::move(result), *expected, expr.span);
    }
    if (expected) require_type(result.type, *expected, expr.span, origin ? &*origin : nullptr);
    return result;
}

void Lowering::place(const IrNode& destination) const {
    if (destination.op == IrOp::Index) return;
    if (destination.op != IrOp::Load && destination.op != IrOp::Field)
        throw Diagnostic(destination.span, "assignment requires a mutable binding, field, or element")
            .with_code("E0402");
    if (symbol(destination.symbol).name.starts_with("$capture"))
        throw Diagnostic(destination.span, "a captured binding cannot be reassigned").with_code("E0402");
    if (const auto& target = symbol(destination.symbol); target.constant) {
        if (last_binding(target)) assign_last(destination);
        auto error = Diagnostic(destination.span, "cannot assign to const '" + target.name + "'")
            .with_code("E0401").label("cannot assign to a constant binding");
        if (!target.span.file.empty()) error.label(name_span(destination.symbol), "declared constant here");
        if (const auto keyword = const_spans_.find(destination.symbol);
            keyword != const_spans_.end() && user_source(keyword->second.file) &&
            source_text(keyword->second) == "const")
            error.suggest("consider declaring it with 'let'", keyword->second, "let", Applicability::MaybeIncorrect);
        throw error;
    }
}

// A `last` member: with a value it is a constant; without one it is a
// deferred instance field that each constructor assigns exactly once.
void Lowering::mark_last_member(SymbolId id, const Declaration& decl, SymbolKind kind) {
    if (!decl.initializer && kind != SymbolKind::Field) {
        const auto at = decl.name_span.value_or(decl.span);
        throw Diagnostic(at, "a deferred 'last' binding must be a local or an instance field").with_code("E0447")
            .label("'" + decl.name + "' is declared without a value")
            .help(decl.static_member ? "give the static field its value where it is declared"
                                     : "give it its value where it is declared, or declare it inside a function body");
    }
    if (kind == SymbolKind::Field) set_attribute(symbol(id), AttributeId::BindingLast, attribute_bool(!decl.initializer));
}

// An assignment to a `last` binding that the definite-assignment analysis did
// not accept as its single assignment.
void Lowering::assign_last(const IrNode& destination) const {
    const auto& target = symbol(destination.symbol);
    const auto quoted = "'" + target.name + "'";
    if (deferred_binding(target) && target.kind == SymbolKind::Field) {
        auto error = Diagnostic(destination.span, "cannot assign to 'last' field " + quoted + " here")
            .with_code("E0445").label("only an 'init' of its class can assign it, once");
        if (!target.span.file.empty()) error.label(name_span(destination.symbol), "declared here without a value");
        error.help("assign it in each 'init' as '" + target.name + " = value' or 'instance." + target.name +
                   " = value', outside any closure");
        throw error;
    }
    auto error = deferred_binding(target)
        ? Diagnostic(destination.span, "cannot assign twice to " + quoted).with_code("E0440")
        : Diagnostic(destination.span, "cannot assign twice to " + quoted).with_code("E0401");
    error.label("second assignment");
    if (!target.span.file.empty())
        error.label(name_span(destination.symbol), deferred_binding(target) ? "declared here" : "first assigned here");
    if (const auto keyword = const_spans_.find(destination.symbol);
        !deferred_binding(target) && keyword != const_spans_.end() && user_source(keyword->second.file) &&
        source_text(keyword->second) == "last")
        error.suggest("consider declaring it with 'let' if it should change", keyword->second, "let",
                      Applicability::MaybeIncorrect);
    else if (!deferred_binding(target))
        error.help("consider declaring it with 'let' if it should change");
    throw error;
}

// Runs the definite-assignment analysis on a function or lambda body before
// lowering it. In a constructor it also covers the class's deferred fields.
void Lowering::check_definite_assignment(const Declaration& function, SymbolId id) {
    std::vector<DeferredField> fields;
    DefiniteAssignmentOptions options;
    const auto owner = id == no_symbol ? no_symbol : symbol(id).owner;
    if (owner != no_symbol && symbol(owner).kind == SymbolKind::Class && function.name == "init" &&
        entries_[owner].declaration) {
        for (const auto& member : entries_[owner].declaration->members) {
            if (member.kind == DeclKind::Binding && member.last && !member.initializer && !member.static_member)
                fields.push_back({member.name, member.name_span.value_or(member.span)});
        }
        auto ctx = context(id);
        options.instance_is_receiver = find("instance", ctx, function.span) == no_symbol;
        options.trap_is_builtin = find("trap_with_message", ctx, function.span) == no_symbol;
    }
    const auto constructor = function.name_span.value_or(function.span);
    if (!fields.empty()) options.constructor = &constructor;
    auto accepted = neura::check_definite_assignment(function.parameters, function.body, fields, options);
    deferred_assignments_.insert(accepted.begin(), accepted.end());
}

IrNode Lowering::discard_binding(const Declaration& decl, Context& context) {
    if (!decl.initializer)
        throw Diagnostic(decl.span, "'_' requires a value initializer").with_code("E0310");
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
    auto destination = place_expression(expr, context);
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
        throw Diagnostic(expr.span, "'" + expr.text + "' requires a mutable numeric operand").with_code("E0413");
    result.type = destination.type;

    const bool postfix = expr.kind == ExprKind::PostfixUpdate;
    auto previous = postfix ? save_temporary(read_place(destination, context), result.body, context, "$update_old")
                            : read_place(destination, context);
    auto one = node(kind == TypeKind::Integer ? IrOp::Integer : IrOp::Float, expr.span, result.type);
    one.text = "1";
    auto updated = arithmetic(expr.text == "++" ? "+" : "-", expr.span, previous, std::move(one));
    if (!postfix)
        updated = save_temporary(std::move(updated), result.body, context, "$update_new");
    result.body.push_back(store_place(std::move(destination), updated, expr.span));
    result.body.push_back(postfix ? std::move(previous) : std::move(updated));
    return result;
}

// Task<T> and Scope are the scope-bound classes: the trusted library
// declares each in its own module file.
bool Lowering::scope_bound_class(SymbolId id) const {
    if (id >= program_.symbols.size() || symbol(id).kind != SymbolKind::Class) return false;
    const auto file = source_file(program_, id);
    if (file == no_symbol) return false;
    const auto& path = symbol(file).library_path;
    return (symbol(id).name == "Task" && path == "std/concurrent/task.nra") ||
           (symbol(id).name == "Scope" && path == "std/concurrent/scope.nra");
}

// A type is scope-bound if it is Task<T>, Scope, or an array, list, or
// tuple with a scope-bound element (Concurrency.md section 4.3).
bool Lowering::scope_bound(const IrType& type) const {
    switch (type.kind) {
    case TypeKind::Object:
        return scope_bound_class(type.symbol);
    case TypeKind::Array:
    case TypeKind::List:
    case TypeKind::Tuple:
        return std::ranges::any_of(type.arguments, [&](const IrType& part) { return scope_bound(part); });
    default:
        return false;
    }
}

// True when `place` is, or is an element of, a local declared inside the
// innermost enclosing task scope. A parameter or an outer local is not.
bool Lowering::declared_in_task_scope(const IrNode& place, const Context& context) const {
    const auto* root = &place;
    while ((root->op == IrOp::Index || root->op == IrOp::Project) && !root->operands.empty())
        root = &root->operands.front();
    if (root->op != IrOp::Load || root->symbol == no_symbol || symbol(root->symbol).kind != SymbolKind::Local)
        return false;
    for (std::size_t i = context.task_scope_base; i < context.scopes.size(); ++i) {
        for (const auto& [name, id] : context.scopes[i]) {
            if (id == root->symbol) return true;
        }
    }
    return false;
}

void Lowering::require_scope_local(const IrNode& place, const IrType& stored, const Span& span,
                                   const Context& context) const {
    if (!scope_bound(stored) || declared_in_task_scope(place, context)) return;
    throw Diagnostic(span, "a value of type " + describe(stored) +
                           " cannot leave its scope: it can be stored only in a local declared inside that scope")
        .with_code("E0605");
}

// The module `module` of trusted library file `path`. The source loader
// supplies the file and retries when the program has not loaded it. The
// file of `context` gains a dependency so that the library file initializes
// first, as a primitive wrapper's does.
SymbolId Lowering::library_module(std::string_view path, std::string_view module, const Span& span,
                                  const Context& context, const std::string& message) {
    const auto file = library_files_.find(path);
    if (file == library_files_.end()) {
        MissingPrimitiveWrapper missing(span, message, std::string(path));
        missing.with_code("E0701");
        throw missing;
    }
    const auto result = own_member(file->second, module);
    if (result == no_symbol || symbol(result).kind != SymbolKind::Module)
        throw Diagnostic(span, "invalid standard library module " + std::string(path)).with_code("E0899");
    resolve(result);
    const auto from = source_file(program_, lexical_owner(context));
    if (from != no_symbol && from != file->second && wrapper_imports_.emplace(from, result).second) {
        const auto dependency = add(SymbolKind::Import, "$concurrent", span, from);
        symbol(dependency).type = symbol(result).type;
    }
    return result;
}

IrNode Lowering::task_scope(const Statement& stmt, Context& context) {
    // Open decision "scope in initialization" (Concurrency.md section 18):
    // decided no. Globals are still being initialized there.
    if (context.function == no_symbol)
        throw Diagnostic(stmt.keyword, "a scope block is only allowed inside a function body").with_code("E0607");
    auto result = node(IrOp::TaskScope, stmt.span);
    const auto base = context.task_scope_base;
    ++context.task_scopes;
    context.task_scope_base = context.scopes.size();
    context.scopes.emplace_back();
    if (!stmt.bindings.empty()) {
        // `scope (name)`: a constant bound to the scope's handle. The scope's
        // identity arrives in a hidden local, and the handle wraps it.
        const auto& name = stmt.bindings.front();
        const auto scope_module = library_module("std/concurrent/scope.nra", "Scope", name.span, context,
                                                 "a scope handle requires Std.Concurrent.Scope");
        const auto scope_class = own_member(scope_module, "Scope");
        if (scope_class == no_symbol || !scope_bound_class(scope_class))
            throw Diagnostic(name.span, "Std.Concurrent.Scope must declare the class Scope").with_code("E0899");
        resolve(scope_class);
        const auto scope_init = own_member(scope_class, "init");
        if (scope_init == no_symbol) throw Diagnostic(name.span, "Std.Concurrent.Scope.Scope needs a constructor")
            .with_code("E0899");
        resolve(scope_init);
        const auto identity = add(SymbolKind::Local, "$scope", name.span, context.owner);
        symbol(identity).type = uint64_type;
        symbol(identity).constant = true;
        result.symbol = identity;
        auto load = node(IrOp::Load, name.span, uint64_type);
        load.symbol = identity;
        auto handle = node(IrOp::Allocate, name.span, symbol(scope_class).type);
        handle.symbol = scope_init;
        handle.operands.push_back(std::move(load));
        const auto binding = add(SymbolKind::Local, name.text, name.span, context.owner);
        symbol(binding).type = symbol(scope_class).type;
        symbol(binding).constant = true;
        context.scopes.back().emplace(name.text, binding);
        auto initialize = node(IrOp::Initialize, name.span);
        initialize.symbol = binding;
        initialize.operands.push_back(std::move(handle));
        result.body.push_back(std::move(initialize));
    }
    for (const auto& inner : stmt.body) append_statement(result.body, inner, context);
    context.scopes.pop_back();
    context.task_scope_base = base;
    --context.task_scopes;
    return result;
}

// select { arms }. Every arm's receiver and arguments are evaluated once, in
// source order, before anything is attempted; then one scheduler operation
// commits exactly one arm, and only that arm's block runs. The operation of
// an arm is a call of a selectable member of a Std.Concurrent handle:
// receive, send, join, or wait. Each such class provides select_handle(),
// what the scheduler waits on, and <member>_selected(code), which turns the
// committed outcome into what the member would have returned on its own.
IrNode Lowering::select_statement(const Statement& stmt, Context& context) {
    if (context.function == no_symbol)
        throw Diagnostic(stmt.keyword, "select is only allowed inside a function body").with_code("E0606");
    struct Arm {
        const SelectArm* syntax = nullptr;
        std::string member;
        std::string receiver_name;
        IrType receiver_type;
    };
    std::vector<Arm> arms;
    const SelectArm* fallback = nullptr;
    auto result = node(IrOp::Block, stmt.span);
    auto call = node(IrOp::SchedulerCall, stmt.span, uint64_type);
    context.scopes.emplace_back();
    const auto synthetic = [](Expr::Kind kind, const Span& span, std::string text) {
        return Expr{kind, span, std::move(text), {}, {}, std::nullopt};
    };
    const auto integer = [&](const IrType& type, std::string digits, const Span& span) {
        auto literal = node(IrOp::Integer, span, type);
        literal.text = std::move(digits);
        return literal;
    };
    for (const auto& arm : stmt.select_arms) {
        if (!arm.operation) {
            if (fallback) throw Diagnostic(arm.span, "a select has at most one '_' arm").with_code("E0606");
            fallback = &arm;
            continue;
        }
        const auto& operation = *arm.operation;
        if (operation.kind != ExprKind::Call || operation.operands.front().kind != ExprKind::Member)
            throw Diagnostic(operation.span, "a select arm is a call of send, receive, join, or wait")
                .with_code("E0606");
        const auto& callee = operation.operands.front();
        const auto kind = callee.text == "receive" ? select_receive : callee.text == "send" ? select_send
                        : callee.text == "join" ? select_join : callee.text == "wait" ? select_timer : 4U;
        const std::size_t expected = kind == select_send ? 1 : 0;
        if (kind == 4U || operation.operands.size() != 1 + expected || !operation.type_arguments.empty())
            throw Diagnostic(operation.span, "a select arm is a call of send, receive, join, or wait")
                .with_code("E0606");
        // The receiver is evaluated once and kept for the committed arm.
        auto receiver = value(callee.operands.front(), context);
        const auto index = std::to_string(arms.size());
        const auto receiver_span = callee.operands.front().span;
        if (receiver.type.kind != TypeKind::Object || value_member(receiver.type, "select_handle", true) == no_symbol ||
            value_member(receiver.type, callee.text + "_selected", true) == no_symbol)
            throw Diagnostic(receiver_span, "type " + describe(receiver.type) + " has no selectable member '" +
                                            callee.text + "'").with_code("E0305");
        Arm lowered{&arm, callee.text, "$select" + index, receiver.type};
        const auto saved = save_temporary(std::move(receiver), result.body, context, lowered.receiver_name);
        context.scopes.back().emplace(lowered.receiver_name, saved.symbol);
        // kind, then select_handle() of the receiver, then a send's value.
        call.operands.push_back(integer(uint32_type, std::to_string(kind), operation.span));
        auto handle_callee = synthetic(ExprKind::Member, receiver_span, "select_handle");
        handle_callee.operands.push_back(synthetic(ExprKind::Name, receiver_span, lowered.receiver_name));
        auto handle_call = synthetic(ExprKind::Call, receiver_span, {});
        handle_call.operands.push_back(std::move(handle_callee));
        auto handle = save_temporary(value(handle_call, context), result.body, context, "$handle" + index);
        if (kind == select_send) {
            // Type the value as the member's parameter, as the call would.
            // A member has no parameters until it is resolved.
            const auto member = value_member(lowered.receiver_type, "send", true);
            if (member != no_symbol) resolve(member);
            if (member == no_symbol || symbol(member).parameters.size() != 1)
                throw Diagnostic(operation.span, "send takes the value to send").with_code("E0407");
            const auto parameter = substitute(symbol(symbol(member).parameters.front()).type,
                                              receiver_arguments(lowered.receiver_type, symbol(member).owner));
            auto sent = save_temporary(value(operation.operands[1], context, &parameter, true), result.body, context,
                                       "$sent" + index);
            call.operands.push_back(std::move(handle));
            call.operands.push_back(std::move(sent));
        } else {
            call.operands.push_back(std::move(handle));
        }
        arms.push_back(std::move(lowered));
    }
    if (arms.empty()) throw Diagnostic(stmt.span, "a select needs at least one arm that waits").with_code("E0606");
    call.text = std::string(fallback ? select_with_default : select_operation);
    const auto committed = save_temporary(std::move(call), result.body, context, "$selected");
    // The low half of the result is the committed arm, the high half its outcome.
    const auto arm_of = [&](const Span& span) {
        auto mask = node(IrOp::Binary, span, uint64_type);
        mask.text = "&";
        auto selected = committed;
        mask.operands.push_back(std::move(selected));
        mask.operands.push_back(integer(uint64_type, "4294967295", span));
        return mask;
    };
    const auto outcome_of = [&](const Span& span) {
        auto shift = node(IrOp::Binary, span, uint64_type);
        shift.text = ">>";
        auto selected = committed;
        shift.operands.push_back(std::move(selected));
        shift.operands.push_back(integer(uint32_type, "32", span));
        auto narrowed = node(IrOp::Convert, span, uint32_type);
        narrowed.operands.push_back(std::move(shift));
        return narrowed;
    };
    const auto arm_block = [&](const SelectArm& arm, const Arm* waits) {
        auto block = node(IrOp::Block, arm.span);
        context.scopes.emplace_back();
        if (waits) {
            auto outcome_name = synthetic(ExprKind::Name, arm.span, "$outcome");
            auto finisher = synthetic(ExprKind::Member, arm.operation->span, waits->member + "_selected");
            finisher.operands.push_back(synthetic(ExprKind::Name, arm.operation->span, waits->receiver_name));
            auto finish = synthetic(ExprKind::Call, arm.operation->span, {});
            finish.operands.push_back(std::move(finisher));
            finish.operands.push_back(std::move(outcome_name));
            const auto outcome = save_temporary(outcome_of(arm.span), block.body, context, "$outcome");
            context.scopes.back().emplace("$outcome", outcome.symbol);
            auto produced = expression(finish, context);
            if (arm.binding) {
                if (produced.type.kind == TypeKind::Void)
                    throw Diagnostic(arm.binding->span, "this operation has no result to bind").with_code("E0606");
                if (arm.binding->text != "_" && context.scopes.back().contains(arm.binding->text))
                    throw duplicate_local(arm.binding->span, "duplicate local binding '" + arm.binding->text + "'",
                                          context.scopes.back().find(arm.binding->text)->second)
                        .with_code("E0301");
                const auto binding = add(SymbolKind::Local, arm.binding->text, arm.binding->span, context.owner);
                symbol(binding).type = produced.type;
                symbol(binding).constant = arm.constant_binding;
                if (arm.binding->text != "_") context.scopes.back().emplace(arm.binding->text, binding);
                auto initialize = node(IrOp::Initialize, arm.binding->span);
                initialize.symbol = binding;
                initialize.operands.push_back(std::move(produced));
                block.body.push_back(std::move(initialize));
            } else {
                auto discard = node(IrOp::Discard, arm.operation->span);
                discard.operands.push_back(std::move(produced));
                block.body.push_back(std::move(discard));
            }
        }
        for (const auto& inner : arm.body) append_statement(block.body, inner, context);
        context.scopes.pop_back();
        return block;
    };
    // The last listed arm, or the default arm, needs no test: it is what
    // remains when no earlier index matched.
    std::optional<IrNode> chain;
    if (fallback) chain = arm_block(*fallback, nullptr);
    for (std::size_t position = arms.size(); position-- > 0;) {
        auto block = arm_block(*arms[position].syntax, &arms[position]);
        if (!chain) {
            chain = std::move(block);
            continue;
        }
        const auto& span = arms[position].syntax->span;
        auto test = node(IrOp::Binary, span, bool_type);
        test.text = "==";
        test.operands.push_back(arm_of(span));
        test.operands.push_back(integer(uint64_type, std::to_string(position), span));
        auto branch = node(IrOp::Branch, span);
        branch.operands.push_back(std::move(test));
        branch.body.push_back(std::move(block));
        branch.alternative.push_back(std::move(*chain));
        chain = std::move(branch);
    }
    result.body.push_back(std::move(*chain));
    context.scopes.pop_back();
    return result;
}

// `spawn call`. The parent evaluates the callee, a receiver, and the
// arguments exactly as the call would, and the task starts from those
// values: they become the captures of a function value that takes no
// arguments and makes the call. A void call yields Unit, so every task has
// a result to join.
IrNode Lowering::spawn(const Expr& expr, Context& context, bool handle) {
    const auto& operand = expr.operands.front();
    if (context.function == no_symbol || context.task_scopes == 0)
        throw Diagnostic(expr.span, "spawn requires an enclosing scope block in the same function body")
            .with_code("E0603");
    const std::string missing = "spawn requires Std.Concurrent.Task";
    const auto task_module = library_module("std/concurrent/task.nra", "Task", expr.span, context, missing);
    const auto task_class = own_member(task_module, "Task");
    if (task_class == no_symbol || !scope_bound_class(task_class))
        throw Diagnostic(expr.span, "Std.Concurrent.Task must declare the class Task").with_code("E0899");
    resolve(task_class);
    const auto task_init = own_member(task_class, "init");
    if (task_init == no_symbol || symbol(task_class).generics.size() != 1)
        throw Diagnostic(expr.span, "Std.Concurrent.Task.Task must be Task<T> with a constructor").with_code("E0899");
    resolve(task_init);

    auto invocation = call(operand, context);
    std::optional<IrNode> qualifier;
    if (invocation.op == IrOp::ScopedValue && invocation.body.size() == 2 &&
        invocation.body.front().op == IrOp::Discard) {
        // A static member's qualifier is evaluated, then dropped (see call).
        qualifier = std::move(invocation.body.front().operands.front());
        auto inner = std::move(invocation.body.back());
        invocation = std::move(inner);
    }
    if (invocation.op != IrOp::Call && invocation.op != IrOp::VirtualCall && invocation.op != IrOp::CallClosure)
        throw Diagnostic(operand.span, "spawn requires a call of a function, a method, or a function value")
            .with_code("E0603");

    ClosureEnvironment environment{closure_layout(expr.span, lexical_owner(context)), context, {}, {}};
    for (std::size_t i = 0; i < invocation.operands.size(); ++i) {
        auto& input = invocation.operands[i];
        if (i == 0 && invocation.op == IrOp::VirtualCall && input.type.kind == TypeKind::Module) {
            // A module receiver is static and has no runtime value.
            if (input.op == IrOp::Receiver) input.op = IrOp::Reference;
            continue;
        }
        if (scope_bound(input.type))
            throw Diagnostic(input.span, "a value of type " + describe(input.type) +
                                         " cannot leave its scope: it cannot be passed to spawn").with_code("E0605");
        const auto span = input.span;
        const auto field = add(SymbolKind::Field, "$capture" + std::to_string(environment.inputs.size()),
                               span, environment.layout);
        symbol(field).type = input.type;
        environment.fields.emplace(field, field);
        environment.inputs.emplace_back(field, std::move(input));
        input = closure_field(field, span);
    }

    IrType result_type = invocation.type;
    std::vector<IrNode> instructions;
    if (invocation.type.kind == TypeKind::Void) {
        const auto unit_module = library_module("std/core/unit.nra", "Unit", expr.span, context, missing);
        const auto unit = own_member(unit_module, "Unit");
        if (unit == no_symbol || symbol(unit).kind != SymbolKind::Enum)
            throw Diagnostic(expr.span, "Std.Core.Unit must export its Unit enum").with_code("E0899");
        resolve(unit);
        const auto unit_value = own_member(unit, "Value");
        if (unit_value == no_symbol || symbol(unit_value).kind != SymbolKind::Variant)
            throw Diagnostic(expr.span, "Std.Core.Unit.Unit must declare the variant Value").with_code("E0899");
        result_type = symbol(unit).type;
        auto discard = node(IrOp::Discard, operand.span);
        discard.operands.push_back(std::move(invocation));
        instructions.push_back(std::move(discard));
        auto value = node(IrOp::Reference, operand.span, result_type);
        value.symbol = unit_value;
        auto finish = node(IrOp::Return, operand.span);
        finish.operands.push_back(std::move(value));
        instructions.push_back(std::move(finish));
    } else {
        if (scope_bound(result_type))
            throw Diagnostic(operand.span, "a value of type " + describe(result_type) +
                                           " cannot leave its scope: a task cannot return it").with_code("E0605");
        auto finish = node(IrOp::Return, operand.span);
        finish.operands.push_back(std::move(invocation));
        instructions.push_back(std::move(finish));
    }

    const auto callable = callable_type({result_type}, expr.span);
    const auto method = closure_method(environment.layout, callable, expr.span, {});
    program_.bodies.push_back({method, std::move(instructions)});
    auto started = node(IrOp::Spawn, expr.span, uint64_type);
    started.operands.push_back(closure_value(callable, environment, expr.span));

    if (!handle) {
        // Nothing can join this task, so the runtime need not keep its
        // result until the scope closes.
        auto dropped = node(IrOp::Discard, expr.span);
        dropped.operands.push_back(std::move(started));
        if (!qualifier) return dropped;
        auto both = node(IrOp::Block, expr.span);
        auto first = node(IrOp::Discard, qualifier->span);
        first.operands.push_back(std::move(*qualifier));
        both.body.push_back(std::move(first));
        both.body.push_back(std::move(dropped));
        return both;
    }
    Substitution arguments{{symbol(task_class).generics.front(), result_type}};
    auto task = node(IrOp::Allocate, expr.span, apply_arguments(symbol(task_class).type, arguments));
    task.symbol = task_init;
    task.operands.push_back(std::move(started));
    return qualifier ? discard_before(std::move(*qualifier), std::move(task)) : std::move(task);
}

IrNode Lowering::statement(const Statement& stmt, Context& context) {
    switch (stmt.kind) {
    case StmtKind::Binding: {
        const auto& declaration = *stmt.declaration;
        if (declaration.kind != DeclKind::Binding || declaration.exported ||
            declaration.override_member || declaration.promote_import || declaration.static_member) {
            throw Diagnostic(declaration.span, "expected a local value binding").with_code("E0431");
        }
        if (declaration.name == "_") return discard_binding(declaration, context);
        if (const auto earlier = context.scopes.back().find(declaration.name);
            earlier != context.scopes.back().end()) {
            auto error = Diagnostic(declaration.name_span.value_or(declaration.span),
                                    "duplicate local binding '" + declaration.name + "'")
                .with_code("E0301").label("'" + declaration.name + "' is bound again here");
            const auto& first = symbol(earlier->second);
            if (!first.span.file.empty()) error.label(name_span(earlier->second), "first bound here");
            error.help(first.constant ? "give the second binding another name"
                                      : "give the second binding another name, or assign to the first one");
            throw error;
        }
        if (!declaration.initializer && declaration.last && declaration.type) {
            // `last x: T;`: declared now, assigned exactly once later.
            auto declared = type(*declaration.type, context);
            if (declared.kind == TypeKind::Void)
                throw Diagnostic(declaration.type->span, "a binding cannot have type void").with_code("E0416");
            if (is_primitive_reference(declared))
                throw Diagnostic(declaration.type->span, "a deferred 'last' binding cannot have a primitive reference type")
                    .with_code("E0428");
            auto id = add(SymbolKind::Local, declaration.name, declaration.span, context.owner);
            record_names(id, declaration);
            symbol(id).constant = true;
            symbol(id).type = declared;
            set_attribute(symbol(id), AttributeId::BindingLast, attribute_bool(true));
            context.scopes.back().emplace(declaration.name, id);
            auto result = node(IrOp::Initialize, stmt.span);
            result.symbol = id;
            return result;
        }
        if (!declaration.initializer) {
            auto error = Diagnostic(declaration.span, "a local binding requires an initializer").with_code("E0404")
                .label("'" + declaration.name + "' is declared without a value");
            const auto after = declaration.type ? std::optional<Span>(declaration.type->span) : declaration.name_span;
            if (const auto at = after ? source_span(after->file, after->offset + after->length, 0) : std::nullopt)
                error.suggest("give it an initial value", *at, " = <value>", Applicability::HasPlaceholders);
            if (const auto keyword = Span{declaration.span.file, declaration.span.line, declaration.span.column,
                                          declaration.span.offset, 3};
                declaration.type && user_source(keyword.file) && source_text(keyword) == "let")
                error.suggest("or, to assign it exactly once later, declare it with 'last'", keyword, "last",
                              Applicability::MaybeIncorrect);
            throw error;
        }

        std::optional<IrType> declared;
        if (declaration.type) {
            mark_binding_annotation(*declaration.type);
            declared = type(*declaration.type, context);
            if (declared->kind == TypeKind::Void) {
                throw Diagnostic(declaration.type->span, "a binding cannot have type void").with_code("E0416");
            }
        }
        if (declared)
            expected_origin_ = ExpectedOrigin{declaration.type->span, "expected because of this type",
                                              &*declaration.type, true};
        auto initializer = value(*declaration.initializer, context, declared ? &*declared : nullptr);
        auto id = add(SymbolKind::Local, declaration.name, declaration.span, context.owner);
        record_names(id, declaration);
        symbol(id).constant = declaration.constant;
        if (declaration.last) set_attribute(symbol(id), AttributeId::BindingLast, attribute_bool(false));
        symbol(id).type = declared.value_or(initializer.type);
        context.scopes.back().emplace(declaration.name, id);
        attach_tags(program_, id, TagTarget::Local, declaration.tags, declaration.name_span.value_or(declaration.span));

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
                throw Diagnostic(syntax.span, "trap_with_message requires one string argument and no type arguments")
                    .with_code("E0407");
            auto result = node(IrOp::TrapMessage, syntax.span);
            result.operands.push_back(value(syntax.operands[1], context, &string_type));
            return result;
        }
        if (syntax.kind == ExprKind::Spawn) return spawn(syntax, context, false);
        auto result = node(IrOp::Discard, stmt.span);
        // Expression statements can call functions that return void.
        auto operand = expression(stmt.expressions.front(), context);
        if (operand.op == IrOp::Reference &&
            (operand.symbol == no_symbol || symbol(operand.symbol).kind != SymbolKind::Variant)) {
            throw Diagnostic(operand.span, "expected an executable expression").with_code("E0422");
        }
        if (operand.op == IrOp::Reference && has_enum_payload(program_, operand.type.symbol)) {
            if (!symbol(operand.symbol).payload_types.empty())
                throw Diagnostic(operand.span, "enum variant requires a payload").with_code("E0427");
            operand.op = IrOp::EnumConstruct;
        }
        check_discarded_result(program_, operand, stmt.span, lexical_owner(context));
        result.operands.push_back(std::move(operand));
        return result;
    }
    case StmtKind::Assignment: {
        if (reference_field_bindings_.contains(&stmt)) {
            auto destination = expression_node(stmt.expressions[0], context, nullptr, false);
            auto result = node(IrOp::Store, stmt.span);
            auto source = value(stmt.expressions[1], context, &destination.type);
            result.operands = {std::move(destination), std::move(source)};
            return result;
        }
        if (stmt.assignment_operator != "=") {
            auto result = node(IrOp::Block, stmt.span);
            auto destination = capture_place(stmt.expressions[0], result.body, context);
            const auto op = stmt.assignment_operator.substr(0, stmt.assignment_operator.size() - 1);
            auto right = value(stmt.expressions[1], context, is_shift(op) ? nullptr : &destination.type);
            auto source = arithmetic(op, stmt.span, read_place(destination, context), std::move(right));
            result.body.push_back(store_place(std::move(destination), std::move(source), stmt.span));
            return result;
        }
        auto destination = place_expression(stmt.expressions[0], context);
        if (!deferred_assignments_.contains(&stmt)) place(destination);
        if ((destination.op == IrOp::Load || destination.op == IrOp::Field) && destination.symbol != no_symbol)
            expected_origin_ = ExpectedOrigin{name_span(destination.symbol),
                                              "'" + symbol(destination.symbol).name + "' is declared here with type " +
                                                  describe(destination.type)};
        auto source = value(stmt.expressions[1], context, &destination.type);
        require_scope_local(destination, source.type, source.span, context);
        return store_place(std::move(destination), std::move(source), stmt.span);
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
        throw Diagnostic(stmt.span, "destructuring requires a statement list").with_code("E0899");
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
            throw Diagnostic(stmt.span, "return is only allowed inside a function").with_code("E0431");
        }
        const auto expected = symbol(context.function).type;
        const auto result_span = result_spans_.find(context.function);
        auto result = node(IrOp::Return, stmt.span);
        if (stmt.expressions.empty()) {
            if (expected.kind != TypeKind::Void) {
                auto error = Diagnostic(stmt.span, "return requires a value of type " + describe(expected))
                    .with_code("E0406").label("return without a value");
                if (result_span != result_spans_.end())
                    error.label(result_span->second, "the function returns " + describe(expected));
                throw error;
            }
        } else {
            if (expected.kind == TypeKind::Void) {
                auto error = Diagnostic(stmt.span, "a void function cannot return a value").with_code("E0406")
                    .label("returns a value");
                if (name_spans_.contains(context.function))
                    error.label(name_span(context.function), "this function declares no result type");
                error.help("declare the result type with '-> T' after the parameters, or return without a value");
                throw error;
            }
            if (result_span != result_spans_.end()) {
                const auto* function = entries_[context.function].declaration;
                expected_origin_ = ExpectedOrigin{result_span->second, "expected because of this result type",
                                                  function && function->type ? &*function->type : nullptr};
            }
            result.operands.push_back(value(stmt.expressions.front(), context, &expected));
            if (scope_bound(result.operands.back().type))
                throw Diagnostic(result.operands.back().span,
                                 "a value of type " + describe(result.operands.back().type) +
                                 " cannot leave its scope: it cannot be returned from a function").with_code("E0605");
        }
        return result;
    }
    case StmtKind::Scope:
        return task_scope(stmt, context);
    case StmtKind::Select:
        return select_statement(stmt, context);
    case StmtKind::Break:
    case StmtKind::Continue:
        if (context.loops == 0) {
            throw Diagnostic(stmt.span, "loop control is only allowed inside a loop").with_code("E0431");
        }
        return node(stmt.kind == StmtKind::Break ? IrOp::Break : IrOp::Continue, stmt.span);
    case StmtKind::Destroy: {
        auto object = value(stmt.expressions.front(), context);
        if (object.type.kind != TypeKind::Object) {
            throw Diagnostic(object.span, "destroy requires an object").with_code("E0514");
        }
        if (entries_[object.type.symbol].singleton_instance != no_symbol)
            throw Diagnostic(object.span, "a singleton cannot be destroyed").with_code("E0511");
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
            ((item.op == IrOp::Block || item.op == IrOp::Branch || item.op == IrOp::TaskScope) &&
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
            throw Diagnostic(init.span, "a for loop initializer must be a binding, an assignment, or an expression")
                .with_code("E0432");
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
            throw Diagnostic(step.span, "a for loop step must be an assignment or an expression").with_code("E0432");
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
                                     describe(items.type)).with_code("E0430");
    }
    const auto invoke = [&](const IrType& view, std::string_view name, IrNode receiver) {
        const auto method = value_member(view, name, true);
        if (method == no_symbol) {
            throw Diagnostic(stmt.span, describe(view) + " must provide '" + std::string(name) + "()'")
                .with_code("E0430");
        }
        resolve(method);
        const auto& declaration = symbol(method);
        if (declaration.kind != SymbolKind::Function || !declaration.generics.empty() ||
            !declaration.parameters.empty()) {
            throw Diagnostic(stmt.span, std::string(name) + "() must take no arguments").with_code("E0430");
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
    require_type(element, declared, declaration.type ? declaration.type->span : declaration.span);
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
    auto source = destructure_source(stmt, context);
    const auto tuple_type = source.type;
    if (tuple_type.kind != TypeKind::Tuple || tuple_type.arguments.size() != stmt.bindings.size())
        throw Diagnostic(stmt.span, "destructuring requires a tuple with one element per binding").with_code("E0426");
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
                throw duplicate_local(binding.span, "duplicate local binding '" + binding.text + "'",
                                      names.find(binding.text)->second);
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
        throw Diagnostic(stmt.span, "cyclic destructuring declaration").with_code("E0309");
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

IrNode Lowering::lower_match(const Statement& stmt, Context& context, bool produces_value,
                             const IrType* expected, bool call_argument) {
    auto subject = value(stmt.expressions.front(), context);
    const auto enum_type = subject.type;
    if (enum_type.kind != TypeKind::Enum)
        throw Diagnostic(subject.span, "match requires an enum value").with_code("E0411");
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
            throw Diagnostic(arm.span, "a wildcard match arm must be last").with_code("E0412");
        wildcard = arm.variant == "_";
        const auto variant = wildcard ? no_symbol : qualified(arm.variant, arm.span, context);
        if (!wildcard && (symbol(variant).kind != SymbolKind::Variant || symbol(variant).owner != enum_type.symbol))
            throw Diagnostic(arm.span, "match arm must name a variant of " + describe(enum_type)).with_code("E0412");
        if (const auto separator = arm.variant.rfind('.'); !wildcard && separator != std::string::npos) {
            const auto qualifier = qualified(arm.variant.substr(0, separator), arm.span, context);
            if (symbol(qualifier).kind == SymbolKind::Alias && symbol(qualifier).type != enum_type)
                throw Diagnostic(arm.span, "match arm must use an overlay of " + describe(enum_type))
                    .with_code("E0412");
        }
        if (!wildcard && !seen.insert(variant).second)
            throw Diagnostic(arm.span, "duplicate match arm for '" + arm.variant + "'").with_code("E0412");
        auto fields = wildcard ? std::vector<IrType>{} : symbol(variant).payload_types;
        for (auto& field : fields) field = substitute(std::move(field), replacements);
        const auto* declaration = wildcard ? nullptr : entries_[variant].declaration;
        if (arm.bindings.size() != fields.size() || arm.named != (declaration && declaration->named_payload))
            throw Diagnostic(arm.span, "match pattern must bind each payload field, using _ to ignore a field")
                .with_code("E0412");

        auto region = node(produces_value ? IrOp::ScopedValue : IrOp::Block, arm.span);
        context.scopes.emplace_back();
        std::set<std::string> seen_fields;
        for (std::size_t i = 0; i < arm.bindings.size(); ++i) {
            auto field = i;
            if (arm.named) {
                const auto& name = arm.fields[i];
                const auto found = std::ranges::find(declaration->parameters, name, &Parameter::name);
                if (found == declaration->parameters.end() || !seen_fields.insert(name).second)
                    throw Diagnostic(arm.span, "unknown or repeated enum payload field '" + name + "'")
                        .with_code("E0427");
                field = static_cast<std::size_t>(found - declaration->parameters.begin());
            }
            const auto& binding = arm.bindings[i];
            if (binding.text == "_") continue;
            auto& names = context.scopes.back();
            if (names.contains(binding.text))
                throw duplicate_local(binding.span, "duplicate match binding '" + binding.text + "'",
                                      names.find(binding.text)->second);
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
            auto returned = value(*arm.value, context, match_type ? &*match_type : nullptr,
                                  call_argument && expected);
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
        std::vector<SymbolId> missing;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.kind == SymbolKind::Variant && item.owner == enum_type.symbol && !seen.contains(id))
                missing.push_back(id);
        }
        if (!missing.empty()) throw non_exhaustive(stmt, missing, produces_value);
    }
    if (arms.empty()) throw Diagnostic(stmt.span, "match requires at least one arm").with_code("E0411");
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
        throw Diagnostic(decl.span, "init and deinit are reserved lifecycle functions").with_code("E0509");
    if (entries_[owner].singleton_instance != no_symbol &&
        (decl.name == "deinit" || !decl.parameters.empty()))
        throw Diagnostic(decl.span, "a singleton can declare only a zero-argument init lifecycle function")
            .with_code("E0511");
    if (!decl.has_body || !decl.generics.empty() || symbol(id).type.kind != TypeKind::Void)
        throw Diagnostic(decl.span, "a lifecycle function needs a body, no type parameters, and void return")
            .with_code("E0509");
    if (decl.name == "deinit" && !decl.parameters.empty())
        throw Diagnostic(decl.span, "deinit takes no parameters").with_code("E0509");
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
    throw Diagnostic(decl.span, "derived constructor must start with super.init").with_code("E0506");
}

void Lowering::require_implicit_constructor(SymbolId cls, const Span& span) {
    for (auto base = symbol(cls).base; base != no_symbol; base = symbol(base).base) {
        const auto inherited = own_member(base, "init");
        if (inherited == no_symbol) continue;
        resolve(inherited);
        const auto branches = entries_[inherited].overloads;
        const auto accepts_zero = [&](SymbolId candidate) {
            resolve(candidate);
            return symbol(candidate).parameters.empty();
        };
        if (!accepts_zero(inherited) && std::ranges::none_of(branches, accepts_zero))
            throw Diagnostic(span, "class '" + symbol(cls).name + "' needs a constructor to initialize its base")
                .with_code("E0506");
        return;
    }
}

void Lowering::lower_declaration(SymbolId id) {
    const TagScope tag_scope(id);
    resolve(id);
    const auto& declaration = *entries_[id].declaration;
    check_override(id, declaration);
    check_constructor(id, declaration);
    if (declaration.kind == DeclKind::Function) {
        check_overloads(id);
        if (!declaration.has_body) return;
        auto ctx = context(id);
        ctx.function = id;
        const auto owner = symbol(id).owner;
        if (symbol(owner).kind == SymbolKind::Class && declaration.name == "init") {
            const auto fields = reference_fields(*entries_[owner].declaration);
            if (!fields.empty()) {
                const auto base = symbol(owner).base;
                const ReferenceConstructorOptions options{
                    find("instance", ctx, declaration.span) == no_symbol,
                    find("trap_with_message", ctx, declaration.span) == no_symbol,
                    base != no_symbol && base != root_class_};
                const auto bindings = check_reference_constructor(declaration, fields, options);
                reference_field_bindings_.insert(bindings.begin(), bindings.end());
            }
        }
        if (declaration.type && symbol(id).type.kind != TypeKind::Void) result_spans_.emplace(id, declaration.type->span);
        check_definite_assignment(declaration, id);
        auto instructions = body(declaration.body, ctx);
        if (symbol(id).type.kind != TypeKind::Void && !always_returns(instructions))
            throw missing_return(declaration.span, id, declaration);
        program_.bodies.push_back({id, std::move(instructions)});
        const auto branches = entries_[id].overloads;
        for (const auto branch : branches) lower_declaration(branch);
    } else if (declaration.kind == DeclKind::Module || class_or_interface(symbol(id).kind)) {
        if (symbol(id).kind == SymbolKind::Class) {
            const auto fields = reference_fields(declaration);
            if (!fields.empty()) {
                auto ctx = context(id);
                check_reference_field_initializers(declaration, fields,
                    find("instance", ctx, declaration.span) == no_symbol);
                if (own_member(id, "init") == no_symbol) {
                    for (const auto& field : fields) {
                        if (!field.has_initializer) {
                            const auto member = own_member(id, field.name);
                            throw Diagnostic(symbol(member).span, "reference field '" + field.name +
                                                                  "' needs an initializer or constructor")
                                .with_code("E0513");
                        }
                    }
                }
            }
        }
        if (symbol(id).kind == SymbolKind::Class && own_member(id, "init") == no_symbol) {
            for (const auto& member : declaration.members) {
                if (member.kind != DeclKind::Binding || !member.last || member.initializer || member.static_member)
                    continue;
                throw Diagnostic(member.name_span.value_or(member.span), "class '" + symbol(id).name +
                                 "' has no 'init' to assign 'last' field '" + member.name + "'")
                    .with_code("E0446").label("declared here without a value")
                    .help("give it its value here, or add an 'init' that assigns it exactly once");
            }
        }
        if (symbol(id).kind == SymbolKind::Interface) {
            for (const auto& member : declaration.members) {
                if (member.static_member) continue;
                if (member.kind != DeclKind::Function || !member.generics.empty() ||
                    member.name == "init" || member.name == "deinit") {
                    throw Diagnostic(member.span, "interfaces may declare only non-generic methods").with_code("E0504");
                }
            }
        }
        if (class_or_interface(symbol(id).kind)) check_interfaces(id);
        const auto casts = entries_[id].casts;
        std::vector<IrType> cast_types;
        for (const auto cast : casts) {
            resolve(cast);
            const auto target = symbol(cast).type;
            if (std::ranges::find(cast_types, target) != cast_types.end())
                throw Diagnostic(symbol(cast).span, "duplicate cast to " + describe(target)).with_code("E0512");
            cast_types.push_back(target);
            lower_declaration(cast);
        }
        std::vector<IrNode> initializers;
        std::vector<IrNode> static_initializers;
        for (const auto& member : declaration.members) {
            if (member.cast_target) continue;
            if (member.name == "_") {
                if (member.initializer->kind != ExprKind::Import) {
                    auto ctx = context(id);
                    initializers.push_back(discard_binding(member, ctx));
                }
                continue;
            }
            auto child = own_member(id, member.name);
            lower_declaration(child);
            if (!member.accessors.empty()) lower_accessors(child);
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
                    throw Diagnostic(member.span, "a singleton cannot have an abstract method").with_code("E0511");
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

void Lowering::seed(LinkedImports linked) {
    program_ = std::move(linked.program);
    linked_modules_ = std::move(linked.modules);
    entries_.resize(program_.symbols.size());

    // A singleton's class and value share their source name. Older files
    // identify them by the value's constant self type and declaration span.
    std::set<SymbolId> singleton_values;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind == SymbolKind::Class && item.singleton_instance != no_symbol) {
            entries_[id].singleton_instance = item.singleton_instance;
            singleton_values.insert(item.singleton_instance);
        }
        if (item.kind != SymbolKind::Global || !item.constant || item.type.kind != TypeKind::Object ||
            item.type.symbol == no_symbol) continue;
        const auto& cls = symbol(item.type.symbol);
        if (cls.kind == SymbolKind::Class && cls.owner == item.owner && cls.name == item.name &&
            cls.span.file == item.span.file && cls.span.offset == item.span.offset) {
            entries_[item.type.symbol].singleton_instance = id;
            singleton_values.insert(id);
        }
    }
    std::vector<SymbolId> legacy_overloads;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& item = symbol(id);
        if (item.kind == SymbolKind::File) {
            linked_files_.emplace(item.name, id);
            if (!item.library_path.empty()) library_files_.emplace(item.library_path, id);
        }
        if (item.kind == SymbolKind::Class && item.owner == no_symbol && item.name == "$class")
            root_class_ = id;
        if (item.kind == SymbolKind::Class && is_primitive_reference(item.type))
            reference_layouts_.emplace(item.type.name.substr(1), id);
        if (item.owner == no_symbol || singleton_values.contains(id)) continue;
        if (item.kind == SymbolKind::Field && item.name == "$value" &&
            is_primitive_reference(symbol(item.owner).type)) {
            entries_[item.owner].names.emplace(item.name, id);
            continue;
        }
        if (is_static_scope(item)) {
            entries_[item.owner].static_scope = id;
            continue;
        }
        if (item.overload_group != no_symbol) {
            entries_[item.overload_group].overloads.push_back(id);
            continue;
        }
        if (item.user_cast || (item.kind == SymbolKind::Function && item.name.starts_with("$as$"))) {
            entries_[item.owner].casts.push_back(id);
            continue;
        }
        if (item.name.starts_with('$') || item.kind == SymbolKind::Local ||
            item.kind == SymbolKind::Parameter || item.kind == SymbolKind::TypeParameter) continue;
        auto owner = item.owner;
        if (is_static_scope(symbol(owner))) owner = symbol(owner).owner;
        if (item.kind == SymbolKind::Function && item.name.find('$') != std::string::npos) {
            legacy_overloads.push_back(id);
            continue;
        }
        const auto [member, added] = entries_[owner].names.emplace(item.name, id);
        if (!added && item.kind == SymbolKind::Function && item.name == "init" &&
            symbol(owner).kind == SymbolKind::Class)
            entries_[member->second].overloads.push_back(id);
    }
    for (const auto id : legacy_overloads) {
        const auto& item = symbol(id);
        auto owner = item.owner;
        if (is_static_scope(symbol(owner))) owner = symbol(owner).owner;
        const auto name = std::string_view(item.name).substr(0, item.name.find('$'));
        const auto group = own_member(owner, name);
        if (group != no_symbol && symbol(group).kind == SymbolKind::Function)
            entries_[group].overloads.push_back(id);
    }
    // A fields-only anonymous class from a linked module is the class that
    // the same fields written here denote.
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        if (structural_class(symbol(id).type) && symbol(id).type.symbol == id)
            structural_classes_.emplace(structural_key(id), id);
    }
}

IrProgram Lowering::run(const std::vector<ParsedFile>& files, LinkedImports linked) {
    files_ = &files;
    const auto* warnings = DiagnosticSink::current();
    const auto warning_mark = warnings ? warnings->size() : 0;
    seed(std::move(linked));
    const auto linked_symbols = program_.symbols.size();
    const auto linked_bodies = program_.bodies.size();
    for (const auto& file : files) {
        Span span;
        span.file = file.file;
        auto file_id = add(SymbolKind::File, file.file, span);
        symbol(file_id).library_path = file.library_path;
        if (!file.library_path.empty()) library_files_.emplace(file.library_path, file_id);
        file_ids_.push_back(file_id);
        for (const auto& stmt : file.statements) {
            if (stmt.kind == StmtKind::Binding) {
                if (file.library_path.empty() && stmt.declaration->kind == DeclKind::Module &&
                    stmt.declaration->name == "Std")
                    throw Diagnostic(stmt.declaration->span, "the Std module namespace is reserved").with_code("E0311");
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
                    if (const auto [earlier, added] = entries_[file_id].names.emplace(binding.text, id); !added) {
                        throw Diagnostic(binding.span, "duplicate declaration of '" + binding.text + "'")
                            .with_code("E0302").label("'" + binding.text + "' is declared again here")
                            .label(name_span(earlier->second), "'" + binding.text + "' is first declared here");
                    }
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
                const auto accepted = neura::check_definite_assignment({}, std::span(&stmt, 1));
                deferred_assignments_.insert(accepted.begin(), accepted.end());
                instructions.push_back(statement(stmt, ctx));
            }
        }
        if (!instructions.empty()) program_.bodies.push_back({id, std::move(instructions)});
    }
    // Classes written in type positions, including those their own bodies
    // write.
    for (std::size_t i = 0; i < pending_anonymous_.size(); ++i) lower_declaration(pending_anonymous_[i]);
    finish_tags(program_, linked_symbols, warning_mark, [&](SymbolId id) -> std::optional<TagDeclarationSpans> {
        if (id >= entries_.size() || !entries_[id].declaration) return std::nullopt;
        const auto& decl = *entries_[id].declaration;
        TagDeclarationSpans spans{decl.base ? std::optional<Span>(decl.base->span) : std::nullopt, {}, name_span(id)};
        for (const auto& item : decl.interfaces) spans.interfaces.push_back(item.span);
        return spans;
    });
    std::set<SymbolId> singletons;
    for (const auto& entry : entries_) {
        if (entry.singleton_instance != no_symbol) singletons.insert(entry.singleton_instance);
    }
    lower_static_initializers(program_, singletons, linked_symbols, linked_bodies);
    return std::move(program_);
}

#include "lowering_accessors.inc"

// Anonymous classes and target-typed `new` (Types.md, Anonymous classes;
// ObjectLifecycle.md, Target-typed new).

bool plain_anonymous(const TypeSyntax& syntax) {
    return syntax.anonymous && syntax.collections.empty() && !syntax.reference;
}

// A body whose members are all plain `let` instance fields denotes a
// structural class. A method, lifecycle function, static member, cast, base,
// or interface, and also a `const` or `last` field, an accessor clause, or a
// tag on a member (Tags.md), gives the class behaviour or attributes of its
// own, so such a body is a class of its own at each place it is written. A
// structural class has no tags, so two bodies with the same fields never
// merge two different sets of tags.
bool fields_only(const Declaration& body) {
    if (body.base || !body.interfaces.empty()) return false;
    return std::ranges::all_of(body.members, [](const Declaration& member) {
        return member.kind == DeclKind::Binding && !member.static_member && !member.override_member &&
               !member.constant && member.accessors.empty() && member.tags.empty() &&
               !member.promote_import && !member.cast_target && member.overloads.empty() &&
               member.name != "_" && member.name != "init" && member.name != "deinit" &&
               !(member.initializer && member.initializer->kind == ExprKind::Import);
    });
}

bool has_type_parameter(const IrType& type) {
    return type.kind == TypeKind::TypeParameter || std::ranges::any_of(type.arguments, has_type_parameter);
}

// The identity of a type in a structural key: its symbol, not its name.
std::string identity_key(const IrType& type) {
    std::string result = std::to_string(static_cast<int>(type.kind)) + ":" + type.name + "#" +
                         (type.symbol == no_symbol ? std::string("-") : std::to_string(type.symbol));
    if (!type.arguments.empty()) {
        result += "<";
        for (const auto& argument : type.arguments) result += identity_key(argument) + ",";
        result += ">";
    }
    return result;
}

std::string field_key(bool constant, const std::string& name, const IrType& type) {
    return (constant ? "const " : "let ") + name + ":" + identity_key(type) + ";";
}

Span join_spans(const Span& first, const Span& last) {
    auto result = first;
    result.length = last.offset + last.length - first.offset;
    return result;
}

void Lowering::mark_binding_annotation(const TypeSyntax& syntax) {
    if (plain_anonymous(syntax)) binding_bodies_.insert(syntax.anonymous.get());
}

bool Lowering::structural_class(const IrType& type) const {
    if (type.kind != TypeKind::Object || type.symbol >= program_.symbols.size()) return false;
    const auto* found = find_attribute(symbol(type.symbol), AttributeId::ClassAnonymous);
    return found && found->value.kind == AttributeValueKind::List && !found->value.items.empty() &&
           found->value.items.front().kind == AttributeValueKind::Bool && found->value.items.front().integer != 0;
}

std::string Lowering::canonical_type(const IrType& type) const {
    // A type as every compilation spells it: a declared type by the identity
    // of its file and its qualified name, never by a symbol number.
    std::string result = std::to_string(static_cast<int>(type.kind)) + ":";
    if (type.symbol != no_symbol && type.symbol < program_.symbols.size() &&
        (type.kind == TypeKind::Object || type.kind == TypeKind::Enum)) {
        std::string qualified = symbol(type.symbol).name;
        auto owner = symbol(type.symbol).owner;
        for (; owner != no_symbol && symbol(owner).kind != SymbolKind::File; owner = symbol(owner).owner)
            qualified = symbol(owner).name + "." + qualified;
        if (owner != no_symbol) {
            const auto& file = symbol(owner);
            result += (file.library_path.empty() ? "file " + file.name : "library " + file.library_path) + "::";
        }
        result += qualified;
    } else {
        result += type.name;
    }
    if (!type.arguments.empty()) {
        result += "<";
        for (const auto& argument : type.arguments) result += canonical_type(argument) + ",";
        result += ">";
    }
    return result;
}

std::string Lowering::structural_key(SymbolId cls) const {
    std::string result;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& field = symbol(id);
        if (field.owner == cls && field.kind == SymbolKind::Field)
            result += field_key(field.constant, field.name, field.type);
    }
    return result;
}

IrType Lowering::anonymous_class(const TypeSyntax& syntax, const Context& context) {
    auto& body = *syntax.anonymous;
    if (const auto found = occurrence_classes_.find(&body); found != occurrence_classes_.end())
        return symbol(found->second).type;
    // The body sees the names of the module or file that encloses it.
    auto owner = lexical_owner(context);
    while (owner != no_symbol && symbol(owner).kind != SymbolKind::File &&
           (symbol(owner).kind != SymbolKind::Module || is_static_scope(symbol(owner))))
        owner = symbol(owner).owner;
    const bool structural = fields_only(body);
    const bool binding = binding_bodies_.contains(&body);
    std::string fields;
    std::string key;
    std::vector<std::pair<const Declaration*, IrType>> resolved;
    for (const auto& member : body.members) {
        if (member.kind != DeclKind::Binding || member.static_member || member.cast_target) continue;
        const auto at = member.name_span.value_or(member.span);
        if (structural) {
            if (!member.type)
                throw Diagnostic(at, "field '" + member.name + "' of a fields-only anonymous class needs a type")
                    .with_code("E0534")
                    .label("'" + member.name + "' has no type annotation")
                    .note("the types of its fields are what identify a fields-only anonymous class")
                    .help("write the type after the name, as in '" + member.name + ": int'");
            if (!member.exported)
                throw Diagnostic(at, "field '" + member.name + "' of a fields-only anonymous class is not exported")
                    .with_code("E0532")
                    .label("nothing can read or write '" + member.name + "'")
                    .note("only the methods of a class can use its unexported fields, and this class has none")
                    .suggest("export the field",
                             Span{member.span.file, member.span.line, member.span.column, member.span.offset, 0},
                             "export ", Applicability::MachineApplicable);
            if (member.initializer && !binding)
                throw Diagnostic(member.initializer->span,
                                 "this field initializer of a fields-only anonymous class never runs")
                    .with_code("E0530")
                    .label("never evaluated")
                    .label(syntax.span, "this class is not the annotation of a binding that 'new' initializes")
                    .note("a fields-only anonymous class takes its field initializers from the annotation of the "
                          "'let', 'const', or 'last' binding that a 'new' initializes; written anywhere else, they "
                          "would never run")
                    .help("remove the initializer, and write it where a binding declared with this class is "
                          "initialized with 'new'");
        }
        if (member.type && member.initializer) mark_binding_annotation(*member.type);
        std::optional<IrType> field;
        if (member.type) {
            field = type(*member.type, context);
            if (has_type_parameter(*field))
                throw Diagnostic(member.type->span,
                                 "an anonymous class cannot use a type parameter of an enclosing declaration")
                    .with_code("E0531")
                    .label("the type of '" + member.name + "' uses a type parameter")
                    .note("an anonymous class is not generic, and it is not part of the generic declaration "
                          "that encloses it")
                    .help("declare a generic class, as in 'let Box = class<T> ( ... );', and use 'Box<T>' here");
            if (field->kind == TypeKind::Void)
                throw Diagnostic(member.type->span, "a field cannot have type void").with_code("E0416");
        }
        const std::string keyword = member.last ? "last " : member.constant ? "const " : "";
        fields += (fields.empty() ? "" : ", ") + keyword + member.name +
                  (field ? ": " + describe(*field) : std::string());
        if (structural) {
            key += field_key(member.constant, member.name, *field);
            resolved.emplace_back(&member, *field);
        }
    }
    auto name = "class(" + fields + ")";
    if (structural) {
        if (const auto found = structural_classes_.find(key); found != structural_classes_.end()) {
            occurrence_classes_.emplace(&body, found->second);
            return symbol(found->second).type;
        }
        // The class belongs to a file of its own, named after a hash of its
        // fields, with every span at the start of that file. Every program
        // and prebuilt module that writes these fields then declares the
        // same file, and linking coalesces it like any shared file.
        std::string canonical;
        for (const auto& [member, field_type] : resolved)
            canonical += "let " + member->name + ":" + canonical_type(field_type) + ";";
        std::uint64_t hash = 14695981039346656037ull;
        for (const auto byte : canonical) {
            hash ^= static_cast<unsigned char>(byte);
            hash *= 1099511628211ull;
        }
        constexpr std::string_view digits = "0123456789abcdef";
        std::string file_name = "<anonymous class ";
        for (int shift = 60; shift >= 0; shift -= 4) file_name += digits[(hash >> shift) & 15];
        file_name += ">";
        const Span at{file_name, 1, 1, 0, 0};
        const auto file = add(SymbolKind::File, file_name, at);
        const auto cls = add(SymbolKind::Class, name, at, file);
        symbol(cls).type = {TypeKind::Object, name, cls, {}};
        symbol(cls).base = root_class();
        name_spans_.emplace(cls, body.name_span.value_or(body.span));
        for (const auto& [member, field_type] : resolved) {
            const auto field = add(SymbolKind::Field, member->name, at, cls);
            symbol(field).type = field_type;
            symbol(field).constant = member->constant;
            symbol(field).exported = true;
            name_spans_.emplace(field, member->name_span.value_or(member->span));
            if (const auto [earlier, added] = entries_[cls].names.emplace(member->name, field); !added)
                throw Diagnostic(member->name_span.value_or(member->span),
                                 "duplicate declaration of '" + member->name + "'")
                    .with_code("E0302")
                    .label("'" + member->name + "' is declared again here")
                    .label(name_span(earlier->second), "'" + member->name + "' is first declared here")
                    .help("rename one of them");
        }
        add_attribute(symbol(cls), AttributeId::ClassAnonymous, anonymous_class_payload(true, name, at));
        structural_classes_.emplace(std::move(key), cls);
        occurrence_classes_.emplace(&body, cls);
        return symbol(cls).type;
    }
    // Any other body is a class of its own, named after where it is written.
    auto file_name = std::string_view(body.span.file);
    if (const auto slash = file_name.find_last_of("/\\"); slash != std::string_view::npos)
        file_name.remove_prefix(slash + 1);
    name += "@" + std::string(file_name) + ":" + std::to_string(body.span.line) + ":" +
            std::to_string(body.span.column);
    body.name = name;
    const auto id = declare(body, owner, SymbolKind::Class);
    entries_[owner].names.erase(name);
    occurrence_classes_.emplace(&body, id);
    add_attribute(symbol(id), AttributeId::ClassAnonymous, anonymous_class_payload(false, name, body.span));
    for (const auto& member : body.members) {
        if (!member.cast_target) prepare_promotions(member, id);
    }
    resolve(id);
    pending_anonymous_.push_back(id);
    return symbol(id).type;
}

IrNode Lowering::construct(const Expr& expr, Context& context, const IrType* expected) {
    std::optional<ExpectedOrigin> origin;
    if (new_origin_ && new_origin_->first == &expr) origin = std::move(new_origin_->second);
    new_origin_.reset();
    if (expr.type) {
        // `new class ( ... )()`: the body written here is constructed.
        mark_binding_annotation(*expr.type);
        auto object = type(*expr.type, context);
        if (structural_class(object))
            return construct_structural(expr, context, object,
                                        plain_anonymous(*expr.type) ? expr.type->anonymous.get() : nullptr);
        return allocate(expr, context, &object);
    }
    const auto problem = [&]() -> std::string {
        if (!expected) return {};
        if (expected->kind == TypeKind::TypeParameter) return "a type parameter";
        if (expected->kind != TypeKind::Object) return "not a class";
        if (expected->symbol == root_class_) return "the umbrella type 'class'";
        if (symbol(expected->symbol).kind == SymbolKind::Interface) return "an interface";
        if (symbol(expected->symbol).kind != SymbolKind::Class) return "not a class";
        if (entries_[expected->symbol].singleton_instance != no_symbol) return "a singleton";
        return {};
    }();
    if (!expected || !problem.empty()) {
        auto error = Diagnostic(expr.span, "target-typed 'new' needs a concrete class type here").with_code("E0420");
        if (!expected) {
            error.label("nothing here gives 'new' a type to construct")
                .note("'new' without a class takes the type that its value must have: the annotation of the "
                      "binding or field it initializes, the place it is assigned to, the declared result type "
                      "of a 'return', the parameter of a call argument, or the element type of a typed "
                      "collection");
        } else {
            error.label("this would construct " + describe(*expected) + ", which is " + problem);
            if (origin && !origin->span.file.empty()) error.label(origin->span, origin->text);
        }
        error.help("name the class to construct, as in 'new T(...)'");
        throw error;
    }
    if (structural_class(*expected)) {
        const Declaration* body = nullptr;
        if (origin && origin->syntax && plain_anonymous(*origin->syntax)) {
            const auto found = occurrence_classes_.find(origin->syntax->anonymous.get());
            if (found != occurrence_classes_.end() && found->second == expected->symbol)
                body = origin->syntax->anonymous.get();
        }
        const bool initialized = body && std::ranges::any_of(body->members, [](const Declaration& member) {
            return member.initializer.has_value();
        });
        if (!body || (initialized && !origin->initializes)) {
            auto error = Diagnostic(expr.span, "this 'new' cannot tell which field initializers of " +
                                               describe(*expected) + " to run")
                .with_code("E0533")
                .label("constructs a fields-only anonymous class");
            if (origin && !origin->span.file.empty()) error.label(origin->span, origin->text);
            error.note("a fields-only anonymous class takes its field initializers from the class body written in "
                       "the annotation of the 'let', 'const', or 'last' binding that 'new' initializes, and here the "
                       "type arrives without that body");
            error.help("declare a binding with the class and initialize it with 'new', as in "
                       "'let value: class ( ... ) = new;', or write the body after 'new', as in "
                       "'new class ( ... )()'");
            throw error;
        }
        return construct_structural(expr, context, *expected, origin->initializes ? body : nullptr);
    }
    return allocate(expr, context, expected);
}

IrNode Lowering::construct_structural(const Expr& expr, Context& context, const IrType& object,
                                      const Declaration* body) {
    auto allocation = allocate(expr, context, &object);
    if (!body || std::ranges::none_of(body->members, [](const Declaration& member) {
            return member.initializer.has_value();
        }))
        return allocation;
    // Allocate, then run the initializers of this body in field order.
    auto result = node(IrOp::ScopedValue, expr.span, object);
    auto instance = save_temporary(std::move(allocation), result.body, context, "$object");
    for (const auto& member : body->members) {
        if (!member.initializer) continue;
        const auto field = own_member(object.symbol, member.name);
        auto place = node(IrOp::Field, member.initializer->span, symbol(field).type);
        place.symbol = field;
        place.operands.push_back(instance);
        expected_origin_ = ExpectedOrigin{member.type->span, "expected because of this type", &*member.type, true};
        auto initial = value(*member.initializer, context, &symbol(field).type);
        auto store = node(IrOp::Store, member.span);
        store.operands.push_back(std::move(place));
        store.operands.push_back(std::move(initial));
        result.body.push_back(std::move(store));
    }
    result.body.push_back(std::move(instance));
    return result;
}

IrNode Lowering::destructure_source(const Statement& stmt, Context& context) {
    const auto names = stmt.bindings.size();
    const auto names_span = join_spans(stmt.bindings.front().span, stmt.bindings.back().span);
    const auto names_text = plural(names, "name");
    const auto* annotation = stmt.binding_type ? &*stmt.binding_type : nullptr;
    if (annotation && annotation->arguments.size() != names) {
        throw Diagnostic(annotation->span, "the annotation has " + plural(annotation->arguments.size(), "type") +
                                           " for " + names_text)
            .with_code("E0436")
            .label(plural(annotation->arguments.size(), "type"))
            .label(names_span, names_text)
            .help("write one type for each name");
    }
    if (stmt.expressions.size() == 1) {
        const auto& initializer = stmt.expressions.front();
        if (!annotation) {
            return initializer.kind == ExprKind::Collection
                ? tuple(initializer, context, nullptr) : value(initializer, context);
        }
        const auto declared = type(*annotation, context);
        if (initializer.kind == ExprKind::Collection) return tuple(initializer, context, &declared);
        expected_origin_ = ExpectedOrigin{annotation->span, "expected because of this type", annotation};
        return value(initializer, context, &declared);
    }
    const auto values_span = join_spans(stmt.expressions.front().span, stmt.expressions.back().span);
    if (stmt.expressions.size() != names) {
        throw Diagnostic(values_span, plural(stmt.expressions.size(), "value") + " for " + names_text)
            .with_code("E0436")
            .label(plural(stmt.expressions.size(), "value"))
            .label(names_span, names_text)
            .note("a list of values after '=' gives one value to each name, in order")
            .help("write one value for each name, or a single tuple value");
    }
    // Each value is lowered in order, before any of the names is bound.
    auto result = node(IrOp::Tuple, values_span, IrType{TypeKind::Tuple, {}, no_symbol, {}});
    for (std::size_t i = 0; i < names; ++i) {
        std::optional<IrType> declared;
        if (annotation) {
            const auto& element = annotation->arguments[i];
            mark_binding_annotation(element);
            declared = type(element, context);
            if (declared->kind == TypeKind::Void)
                throw Diagnostic(element.span, "a binding cannot have type void").with_code("E0416");
            expected_origin_ = ExpectedOrigin{element.span, "expected because of this type", &element, true};
        }
        auto element = value(stmt.expressions[i], context, declared ? &*declared : nullptr);
        result.type.arguments.push_back(declared ? *declared : element.type);
        result.operands.push_back(std::move(element));
    }
    return result;
}

} // namespace

IrProgram lower(const std::vector<ParsedFile>& files) {
    return lower(files, {});
}

IrProgram lower(const std::vector<ParsedFile>& files, LinkedImports linked) {
    auto program = Lowering{}.run(files, std::move(linked));
    insert_polls(program);
    return program;
}

IrProgram builtin_class_ir() {
    return Lowering{}.builtins();
}

IrProgram builtin_reference_ir(const std::string& name, const Span& span) {
    return Lowering{}.reference_builtins(name, span);
}

} // namespace neura
