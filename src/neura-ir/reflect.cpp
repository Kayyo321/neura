#include "reflect.hpp"

#include "frozen.hpp"
#include "pointer_safety.hpp"

#include "aggregate.hpp"
#include "any.hpp"
#include "attributes.hpp"
#include "dispatch.hpp"
#include "host_binding.hpp"
#include "reference_storage.hpp"
#include "value_layout.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// docs/design/Reflect030.md. The reflection universe, its descriptors and the
// dynamic access functions, built on the linked, specialized program from
// existing operations only.

namespace neura {
namespace {

constexpr std::string_view intrinsics_path = "std/reflect/internal/intrinsics.nra";
constexpr std::string_view policy_path = "std/reflect/core/reflect-policy.nra";

const IrType int_type{TypeKind::Integer, "int32", no_symbol, {}};
const IrType uint32_type{TypeKind::Integer, "uint32", no_symbol, {}};
const IrType uint64_type{TypeKind::Integer, "uint64", no_symbol, {}};
const IrType text_type{TypeKind::String, "string", no_symbol, {}};
const IrType bool_type{TypeKind::Bool, "bool", no_symbol, {}};
const IrType void_type{TypeKind::Void, "void", no_symbol, {}};
const IrType any_array_type{TypeKind::List, {}, no_symbol, {any_type}};
const IrType text_array_type{TypeKind::List, {}, no_symbol, {text_type}};

IrNode make(IrOp op, const Span& span, IrType type = void_type) {
    IrNode result{};
    result.op = op;
    result.span = span;
    result.type = std::move(type);
    result.symbol = no_symbol;
    return result;
}

IrNode with(IrNode node, std::vector<IrNode> operands) {
    node.operands = std::move(operands);
    return node;
}

bool concrete(const IrType& type) {
    return type.kind != TypeKind::TypeParameter && std::ranges::all_of(type.arguments, concrete);
}

// The ordinals of Std.Reflect.Type.TypeKind, in declaration order.
enum class Kind : int {
    Nil, Bool, Integer, Float, String, Class, Interface, Record, Enum, Tuple, Array, List, Function, Any,
    TypeParameter, Other
};

enum class Policy { Strip, Layout, Full };

std::string escape(const std::string& text) {
    std::string result;
    for (const char c : text) {
        if (c == '\\') result += "\\\\";
        else if (c == '\t') result += "\\t";
        else if (c == '\n') result += "\\n";
        else result += c;
    }
    return result;
}

class Reflection {
public:
    explicit Reflection(IrProgram& program) : program_(program) {}

    bool prepare() {
        find_intrinsics();
        if (intrinsics_.empty()) return false;
        bool found = false;
        for (auto& body : program_.bodies) found |= rewrite(body.instructions);
        for (auto& constant : program_.static_constants) found |= rewrite_one(constant.value);
        return found;
    }

    void expand() {
        std::vector<IrNode*> sites;
        for (auto& body : program_.bodies) collect_sites(body.instructions, sites);
        for (auto& constant : program_.static_constants) collect_sites_one(constant.value, sites);
        if (sites.empty()) return;
        find_intrinsics();
        if (module_ == no_symbol) throw Diagnostic(sites.front()->span, "reflection needs Std.Reflect.Internal.Intrinsics")
            .with_code("E0899");
        span_ = symbol(module_).span;
        setup();
        for (auto* site : sites) {
            const bool holds = site->text == "pack" || site->text == "unpack";
            for (const auto& operand : site->operands)
                if (holds) check_holdable(operand.type, site->span);
            for (const auto& argument : site->type_arguments) {
                if (!concrete(argument))
                    throw Diagnostic(site->span, "a reflection operation needs a concrete type").with_code("E1301");
                if (holds) check_holdable(argument, site->span);
                ordinal(argument);
            }
            for (const auto& operand : site->operands) {
                if (concrete(operand.type) && operand.type.kind != TypeKind::Any) ordinal(operand.type);
            }
        }
        close();
        make_boxes();
        // Every expansion refers to the functions by symbol; they are made once.
        for (auto* site : sites) {
            const auto& name = site->text;
            if (name == "get") need_get_ = true;
            if (name == "set") need_set_ = true;
            if (name == "length") need_length_ = true;
            if (name == "element") need_element_ = true;
            if (name == "variant") need_variant_ = true;
            if (name == "construct") need_construct_ = true;
            if (name == "invoke") need_invoke_ = true;
        }
        for (auto* site : sites) {
            if (site->text != "proxy") continue;
            const auto& face = site->type_arguments.empty() ? site->type : site->type_arguments.front();
            const auto k = ord_or_any(face);
            if (k < 0) throw Diagnostic(site->span, "this interface cannot be proxied").with_code("E1300");
            make_proxy(k, site->span);
        }
        build_functions();
        for (auto* site : sites) *site = expand_site(*site);
        // The unpack functions are made while sites expand; their bodies hold no
        // Reflect node.
        flush_bodies();
    }

private:
    struct Member {
        SymbolId symbol = no_symbol;
        std::string name;
        IrType type;
        bool exported = false;
        bool constant = false;
        bool last = false;
        SymbolId getter = no_symbol;
        SymbolId setter = no_symbol;
    };

    IrProgram& program_;
    std::set<SymbolId> intrinsics_;
    SymbolId module_ = no_symbol;
    SymbolId root_ = no_symbol;
    SymbolId strip_ = no_symbol;
    SymbolId layout_ = no_symbol;
    Span span_{};
    std::vector<IrType> types_;
    std::map<std::string, int> index_;
    std::deque<int> pending_;
    std::set<SymbolId> allocated_;
    std::map<int, SymbolId> boxes_;
    std::map<SymbolId, int> box_ordinal_;
    std::map<int, SymbolId> box_field_;
    std::vector<IrBody> new_bodies_;
    std::map<int, SymbolId> unpackers_;
    SymbolId ordinal_function_ = no_symbol;
    SymbolId table_function_ = no_symbol;
    SymbolId get_ = no_symbol, set_ = no_symbol, length_ = no_symbol, element_ = no_symbol, variant_ = no_symbol,
             construct_ = no_symbol, invoke_ = no_symbol;
    bool need_get_ = false, need_set_ = false, need_length_ = false, need_element_ = false, need_variant_ = false,
         need_construct_ = false, need_invoke_ = false;

    IrSymbol& symbol(SymbolId id) { return program_.symbols[id]; }
    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    bool valid(SymbolId id) const { return id < program_.symbols.size(); }

    // ---- intrinsic calls -------------------------------------------------

    void find_intrinsics() {
        intrinsics_.clear();
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.kind == SymbolKind::Module && item.name == "Intrinsics") {
                const auto file = source_file(program_, id);
                if (file != no_symbol && symbol(file).library_path == intrinsics_path) module_ = id;
            }
            if (item.kind == SymbolKind::Class && item.name == "$class") root_ = id;
            if (item.kind == SymbolKind::Interface && (item.name == "Strip" || item.name == "Layout")) {
                const auto file = source_file(program_, id);
                if (file != no_symbol && symbol(file).library_path == policy_path)
                    (item.name == "Strip" ? strip_ : layout_) = id;
            }
        }
        if (module_ == no_symbol) return;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.kind == SymbolKind::Function && item.owner == module_ && !item.has_body) intrinsics_.insert(id);
        }
    }

    bool rewrite_one(IrNode& node) {
        // Every part is rewritten: no short circuit.
        bool found = rewrite(node.operands);
        found = rewrite(node.body) || found;
        found = rewrite(node.alternative) || found;
        if ((node.op != IrOp::Call && node.op != IrOp::VirtualCall) || !intrinsics_.contains(node.symbol))
            return found;
        const auto& callee = symbol(node.symbol);
        auto operands = std::move(node.operands);
        if (operands.size() == callee.parameters.size() + 1) operands.erase(operands.begin());
        node.op = IrOp::Reflect;
        node.text = callee.name;
        node.symbol = no_symbol;
        node.operands = std::move(operands);
        return true;
    }

    bool rewrite(std::vector<IrNode>& nodes) {
        bool found = false;
        for (auto& node : nodes) found |= rewrite_one(node);
        return found;
    }

    void collect_sites_one(IrNode& node, std::vector<IrNode*>& sites) {
        collect_sites(node.operands, sites);
        collect_sites(node.body, sites);
        collect_sites(node.alternative, sites);
        if (node.op == IrOp::Reflect) sites.push_back(&node);
    }

    void collect_sites(std::vector<IrNode>& nodes, std::vector<IrNode*>& sites) {
        for (auto& node : nodes) collect_sites_one(node, sites);
    }

    void collect_allocations(const std::vector<IrNode>& nodes) {
        for (const auto& node : nodes) {
            if (node.op == IrOp::Allocate && valid(node.type.symbol)) allocated_.insert(node.type.symbol);
            collect_allocations(node.operands);
            collect_allocations(node.body);
            collect_allocations(node.alternative);
        }
    }

    // ---- the universe ----------------------------------------------------

    bool program_class(SymbolId id) const {
        const auto& item = symbol(id);
        return (item.kind == SymbolKind::Class || item.kind == SymbolKind::Interface) && item.generics.empty() &&
               !item.name.starts_with('$') && !item.name.starts_with('&');
    }

    bool scope_bound(const IrType& type) const {
        if (type.kind == TypeKind::Object && valid(type.symbol)) {
            const auto file = source_file(program_, type.symbol);
            if (file == no_symbol) return false;
            const auto& path = symbol(file).library_path;
            const auto& name = symbol(type.symbol).name;
            return (name.starts_with("Task") && path == "std/concurrent/task.nra") ||
                   (name == "Scope" && path == "std/concurrent/scope.nra");
        }
        return std::ranges::any_of(type.arguments, [&](const IrType& part) { return scope_bound(part); });
    }

    void check_holdable(const IrType& type, const Span& span) const {
        if (type.kind == TypeKind::Object && is_primitive_reference(type))
            throw Diagnostic(span, "a primitive reference cannot be held dynamically").with_code("E1301")
                .help("reflect the value it refers to");
        if (scope_bound(type))
            throw Diagnostic(span, "a scope-bound value cannot be held dynamically").with_code("E1301")
                .help("reflect a value that can be copied out of its scope");
        if (region_value(type))
            throw Diagnostic(span, "an array lives in its region and cannot be held dynamically").with_code("E1301")
                .help("copy the elements into a list and reflect the list");
    }

    bool reflectable(const IrType& type) const {
        if (!concrete(type) || scope_bound(type)) return false;
        switch (type.kind) {
        case TypeKind::Bool: case TypeKind::Integer: case TypeKind::Float: case TypeKind::String:
        case TypeKind::Array: case TypeKind::List: case TypeKind::Tuple:
            return std::ranges::all_of(type.arguments, [&](const IrType& part) { return reflectable(part); });
        case TypeKind::Enum: return valid(type.symbol) && symbol(type.symbol).generics.empty();
        case TypeKind::Object:
            return valid(type.symbol) && !is_primitive_reference(type) && symbol(type.symbol).generics.empty() &&
                   !symbol(type.symbol).name.starts_with('&');
        default: return false;
        }
    }

    int ordinal(const IrType& type) {
        if (type.kind == TypeKind::Any) return -1;
        if (!reflectable(type)) return -1;
        if (const auto tag = any_tag(type); tag && type.kind != TypeKind::Object) return static_cast<int>(*tag);
        if (type.kind == TypeKind::Object && valid(root_) && type.symbol == root_) return add(symbol(root_).type);
        return add(type);
    }

    int add(const IrType& type) {
        const auto key = type_identity(type);
        if (const auto found = index_.find(key); found != index_.end()) return found->second;
        const int result = static_cast<int>(types_.size());
        types_.push_back(type);
        index_.emplace(key, result);
        pending_.push_back(result);
        return result;
    }

    void setup() {
        types_.clear();
        index_.clear();
        // 0 is nil; 1 to 14 are the scalar and string tags of any.hpp.
        types_.push_back(void_type);
        constexpr const char* scalars[]{"bool", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32",
                                        "uint64", "float16", "bfloat16", "float32", "float64", "string"};
        for (int tag = 1; tag <= 14; ++tag) {
            const std::string name = scalars[tag - 1];
            const auto kind = tag == 1 ? TypeKind::Bool : tag <= 9 ? TypeKind::Integer
                              : tag <= 13 ? TypeKind::Float : TypeKind::String;
            types_.push_back(IrType{kind, name, no_symbol, {}});
        }
        // `class` is always described: a live object of a class outside the
        // universe answers as it, never as nil.
        if (valid(root_)) ordinal(symbol(root_).type);
        for (auto& body : program_.bodies) collect_allocations(body.instructions);
        for (const auto& constant : program_.static_constants) collect_allocations({constant.value});
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            if (symbol(id).kind == SymbolKind::Class && program_class(id) && allocated_.contains(id))
                ordinal(symbol(id).type);
        }
    }

    std::vector<SymbolId> children(SymbolId owner, SymbolKind kind) const {
        std::vector<SymbolId> result;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id)
            if (symbol(id).owner == owner && symbol(id).kind == kind) result.push_back(id);
        return result;
    }

    std::vector<SymbolId> variants(SymbolId enumeration) const { return children(enumeration, SymbolKind::Variant); }

    std::vector<Member> fields(SymbolId cls) const {
        // Pointer backing references and view coordinates are implementation
        // details and never enter descriptor-driven field or codec walkers.
        if (pointer_class(program_, cls) != PointerKind::None) return {};
        std::vector<SymbolId> chain;
        for (auto scope = cls; valid(scope) && scope != root_ && chain.size() < 256; scope = symbol(scope).base)
            chain.push_back(scope);
        std::vector<Member> result;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            for (const auto id : children(*it, SymbolKind::Field)) {
                const auto& item = symbol(id);
                if (item.name.starts_with('$')) continue;
                Member member;
                member.symbol = id;
                member.name = item.name;
                member.type = item.type;
                member.exported = item.exported;
                member.constant = item.constant;
                if (const auto* last = find_attribute(item, AttributeId::BindingLast)) {
                    (void)last;
                    member.last = true;
                }
                if (const auto* get = find_attribute(item, AttributeId::AccessorGet)) member.getter = get->value.symbol;
                if (const auto* set = find_attribute(item, AttributeId::AccessorSet)) member.setter = set->value.symbol;
                result.push_back(std::move(member));
            }
        }
        return result;
    }

    bool record(SymbolId cls) const { return has_attribute(symbol(cls), AttributeId::ClassRecord); }

    std::vector<SymbolId> constructors(SymbolId cls) const {
        std::vector<SymbolId> result;
        for (const auto id : children(cls, SymbolKind::Function))
            if (symbol(id).name == "init" && symbol(id).generics.empty()) result.push_back(id);
        return result;
    }

    bool abstract(SymbolId cls) const {
        for (auto scope = cls; valid(scope) && scope != root_; scope = symbol(scope).base)
            for (const auto id : children(scope, SymbolKind::Function))
                if (!symbol(id).has_body && symbol(id).name != "init" && symbol(id).name != "deinit") {
                    // An override further down supplies the body.
                    bool supplied = false;
                    for (auto lower = cls; valid(lower) && lower != scope; lower = symbol(lower).base)
                        for (const auto other : children(lower, SymbolKind::Function))
                            if (symbol(other).name == symbol(id).name && symbol(other).has_body) supplied = true;
                    if (!supplied) return true;
                }
        return false;
    }

    // Instance methods visible on a class or interface: the most-derived
    // declaration of each name, base methods after the type's own.
    std::vector<SymbolId> methods(SymbolId type) const {
        std::vector<SymbolId> result;
        std::set<std::string> seen;
        const auto take = [&](SymbolId scope) {
            for (const auto id : children(scope, SymbolKind::Function)) {
                const auto& item = symbol(id);
                if (item.name == "init" || item.name == "deinit" || item.name.starts_with('$') ||
                    item.name.find('<') != std::string::npos || !item.generics.empty())
                    continue;
                const auto pointer = pointer_class(program_, scope);
                if ((pointer == PointerKind::Const && item.name == "read") ||
                    (pointer == PointerKind::Mutable && item.name == "as_const") ||
                    (pointer == PointerKind::Locked && item.name == "read")) {
                    const auto* value = pointer_value_type(program_, scope);
                    if (!value || mutable_part(program_, *value)) continue;
                }
                if (seen.insert(item.name).second) result.push_back(id);
            }
        };
        if (valid(type) && symbol(type).kind == SymbolKind::Interface) {
            // An interface's own methods, then those of the interfaces it extends.
            std::vector<SymbolId> pending{type};
            std::set<SymbolId> visited;
            while (!pending.empty()) {
                const auto scope = pending.front();
                pending.erase(pending.begin());
                if (!valid(scope) || !visited.insert(scope).second) continue;
                take(scope);
                for (const auto& face : symbol(scope).interfaces) pending.push_back(face.symbol);
            }
            return result;
        }
        for (auto scope = type; valid(scope) && scope != root_ && seen.size() < 4096; scope = symbol(scope).base)
            take(scope);
        return result;
    }

    // ---- proxies ---------------------------------------------------------

    std::map<int, SymbolId> proxies_;
    std::map<int, SymbolId> proxy_makers_;
    SymbolId dispatcher_ = no_symbol;
    SymbolId dispatcher_module_ = no_symbol;

    void find_dispatcher(const Span& span) {
        if (dispatcher_ != no_symbol) return;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.kind != SymbolKind::Function || item.name != "proxy_dispatch" || !valid(item.owner)) continue;
            const auto file = source_file(program_, id);
            if (file != no_symbol && symbol(file).library_path == "std/reflect/internal/model.nra") {
                dispatcher_ = id;
                dispatcher_module_ = item.owner;
            }
        }
        if (dispatcher_ == no_symbol)
            throw Diagnostic(span, "proxies need Std.Reflect.Internal.Model.proxy_dispatch").with_code("E0899");
    }

    void make_proxy(int k, const Span& site) {
        if (proxies_.contains(k)) return;
        const auto& face = types_[k];
        if (const auto problem = reflection_proxy_problem(program_, face))
            throw Diagnostic(site, "this interface cannot be proxied").with_code("E1300")
                .label(*problem).help("proxy an interface whose requirements are non-generic instance methods");
        find_dispatcher(site);
        IrSymbol proxy{};
        proxy.kind = SymbolKind::Class;
        proxy.name = "$reflect_proxy" + std::to_string(k);
        proxy.span = span_;
        proxy.owner = module_;
        proxy.base = root_;
        proxy.interfaces.push_back(face);
        const auto id = add_symbol(std::move(proxy));
        symbol(id).type = {TypeKind::Object, symbol(id).name, id, {}};
        IrSymbol handler{};
        handler.kind = SymbolKind::Field;
        handler.name = "handler";
        handler.span = span_;
        handler.owner = id;
        handler.type = symbol(root_).type;
        const auto handler_field = add_symbol(std::move(handler));
        proxies_[k] = id;
        const auto all = methods(face.symbol);
        for (std::size_t m = 0; m < all.size(); ++m) {
            const auto required = all[m];
            std::vector<std::pair<std::string, IrType>> parameters;
            for (const auto parameter : symbol(required).parameters)
                parameters.emplace_back(symbol(parameter).name, symbol(parameter).type);
            auto method = function(symbol(required).name, symbol(required).type, parameters);
            symbol(method.id).owner = id;
            symbol(method.id).exported = true;
            symbol(method.id).overrides = required;
            bool usable_all = true;
            for (const auto& [name, type] : parameters) usable_all = usable_all && usable(type);
            const auto result = symbol(required).type;
            usable_all = usable_all && (result.kind == TypeKind::Void || usable(result));
            if (!usable_all) {
                method.body.push_back(trap("reflection: " + describe(face).name + "." + symbol(required).name +
                                           " cannot be called through a proxy"));
                finish(method);
                continue;
            }
            const auto arguments = local(method.id, "$arguments", any_array_type);
            method.body.push_back(initialize(arguments, make(IrOp::Collection, span_, any_array_type)));
            for (std::size_t i = 0; i < parameters.size(); ++i) {
                auto append = with(make(IrOp::Append, span_),
                                   {load(arguments), pack(load(method.parameters[i]), parameters[i].second, method.id)});
                method.body.push_back(with(make(IrOp::Discard, span_), {std::move(append)}));
            }
            auto receiver = make(IrOp::Receiver, span_, symbol(id).type);
            receiver.symbol = id;
            auto call = make(IrOp::VirtualCall, span_, any_type);
            call.symbol = dispatcher_;
            auto qualifier = make(IrOp::Reference, span_, symbol(dispatcher_module_).type);
            qualifier.symbol = dispatcher_module_;
            call.operands.push_back(std::move(qualifier));
            call.operands.push_back(field(std::move(receiver), handler_field));
            call.operands.push_back(integer(k));
            call.operands.push_back(integer(static_cast<std::int64_t>(m)));
            call.operands.push_back(load(arguments));
            if (result.kind == TypeKind::Void) {
                method.body.push_back(with(make(IrOp::Discard, span_), {std::move(call)}));
                method.body.push_back(make(IrOp::Return, span_));
            } else if (result.kind == TypeKind::Any) {
                method.body.push_back(ret(std::move(call)));
            } else {
                method.body.push_back(ret(call_function(unpacker(ord_or_any(result)), {std::move(call)})));
            }
            finish(method);
        }
        // The function that makes one proxy around a handler.
        auto maker = function("$reflect_proxy_new" + std::to_string(k), face, {{"handler", symbol(root_).type}});
        const auto made = local(maker.id, "$proxy", symbol(id).type);
        maker.body.push_back(initialize(made, make(IrOp::Allocate, span_, symbol(id).type)));
        maker.body.push_back(store(field(load(made), handler_field), load(maker.parameters[0])));
        maker.body.push_back(ret(load(made)));
        proxy_makers_[k] = maker.id;
        finish(maker);
    }

    Policy policy(SymbolId cls) const {
        if (!valid(cls) || symbol(cls).kind != SymbolKind::Class) return Policy::Full;
        const auto marked = [&](SymbolId marker) {
            if (!valid(marker)) return false;
            for (auto scope = cls; valid(scope); scope = symbol(scope).base)
                for (const auto& face : symbol(scope).interfaces)
                    if (face.symbol == marker) return true;
            return false;
        };
        const bool strip = marked(strip_);
        const bool layout = marked(layout_);
        if (strip && layout)
            throw Diagnostic(symbol(cls).span, "a class keeps one reflection policy").with_code("E1302")
                .label("implements both ReflectPolicy.Strip and ReflectPolicy.Layout").help("keep one marker");
        return strip ? Policy::Strip : layout ? Policy::Layout : Policy::Full;
    }

    std::map<SymbolId, Policy> policies_;
    Policy policy_of(const IrType& type) {
        if (type.kind != TypeKind::Object || !valid(type.symbol)) return Policy::Full;
        const auto found = policies_.find(type.symbol);
        if (found != policies_.end()) return found->second;
        return policies_[type.symbol] = policy(type.symbol);
    }

    bool invocable(SymbolId method) {
        const auto& item = symbol(method);
        if (!item.generics.empty() || item.variadic != VariadicKind::None) return false;
        for (const auto parameter : item.parameters)
            if (!usable(symbol(parameter).type)) return false;
        return item.type.kind == TypeKind::Void || usable(item.type);
    }

    void close() {
        while (!pending_.empty()) {
            const int at = pending_.front();
            pending_.pop_front();
            const auto type = types_[at];
            for (const auto& argument : type.arguments) ordinal(argument);
            if (type.kind == TypeKind::Enum && valid(type.symbol)) {
                for (const auto variant : variants(type.symbol))
                    for (const auto& payload : symbol(variant).payload_types) ordinal(payload);
            }
            if (type.kind == TypeKind::Object && valid(type.symbol)) {
                const auto id = type.symbol;
                if (symbol(id).name.starts_with("$signature")) continue;
                if (valid(symbol(id).base) && symbol(id).base != root_) ordinal(symbol(symbol(id).base).type);
                for (const auto& face : symbol(id).interfaces) ordinal(face);
                if (policy_of(type) == Policy::Strip) continue;
                for (const auto& field : fields(id)) ordinal(field.type);
                for (const auto init : constructors(id))
                    for (const auto parameter : symbol(init).parameters) ordinal(symbol(parameter).type);
                if (policy_of(type) == Policy::Full)
                    for (const auto method : methods(id)) {
                        for (const auto parameter : symbol(method).parameters) ordinal(symbol(parameter).type);
                        ordinal(symbol(method).type);
                    }
            }
        }
    }

    // Arrays live in their region (Storage030.md), so a box on the heap cannot
    // hold one, nor anything that contains one.
    static bool region_value(const IrType& type) {
        return type.kind == TypeKind::Array || std::ranges::any_of(type.arguments, region_value);
    }

    // Whether dynamic code can hold a value of `type`: an any, or a type of the
    // universe that is held directly or has a box.
    bool usable(const IrType& type) const {
        const auto k = ord_or_any(type);
        if (k == -2) return true;
        if (k < 0) return false;
        return !boxed(type) || boxes_.contains(k);
    }

    bool boxed(const IrType& type) const {
        if (type.kind == TypeKind::Object) return false;
        return !any_tag(type).has_value();
    }

    void make_boxes() {
        for (int k = 15; k < static_cast<int>(types_.size()); ++k) {
            if (!boxed(types_[k]) || region_value(types_[k])) continue;
            IrSymbol box{};
            box.kind = SymbolKind::Class;
            box.name = "$reflect_box" + std::to_string(k);
            box.span = span_;
            box.owner = module_;
            box.base = root_;
            const auto id = add_symbol(std::move(box));
            symbol(id).type = {TypeKind::Object, symbol(id).name, id, {}};
            IrSymbol field{};
            field.kind = SymbolKind::Field;
            field.name = "value";
            field.span = span_;
            field.owner = id;
            field.type = types_[k];
            field.exported = true;
            const auto field_id = add_symbol(std::move(field));
            boxes_[k] = id;
            box_ordinal_[id] = k;
            box_field_[k] = field_id;
        }
    }

    SymbolId add_symbol(IrSymbol item) {
        program_.symbols.push_back(std::move(item));
        return program_.symbols.size() - 1;
    }

    // ---- names, as typeof spells them -----------------------------------

    struct Parts {
        Kind kind = Kind::Other;
        std::string name;
        std::string qualified;
    };

    std::string qualified(SymbolId id) const {
        std::string name = symbol(id).name;
        for (auto owner = symbol(id).owner; valid(owner) && symbol(owner).kind != SymbolKind::File;
             owner = symbol(owner).owner) {
            if (!is_static_scope(symbol(owner))) name = symbol(owner).name + "." + name;
        }
        return name;
    }

    Parts describe(const IrType& type) const {
        switch (type.kind) {
        case TypeKind::Bool: return {Kind::Bool, "bool", "bool"};
        case TypeKind::Integer: return {Kind::Integer, type.name, type.name};
        case TypeKind::Float: return {Kind::Float, type.name, type.name};
        case TypeKind::String: return {Kind::String, "string", "string"};
        case TypeKind::Any: return {Kind::Any, "any", "any"};
        case TypeKind::Void: return {Kind::Nil, "nil", "nil"};
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
        case TypeKind::Enum:
            if (!valid(type.symbol)) return {Kind::Enum, type.name, type.name};
            return {Kind::Enum, symbol(type.symbol).name, qualified(type.symbol)};
        case TypeKind::Object: {
            if (!valid(type.symbol)) return {Kind::Other, type.name, type.name};
            const auto& item = symbol(type.symbol);
            if (type.symbol == root_) return {Kind::Class, "class", "class"};
            if (item.name.starts_with("$signature")) return {Kind::Function, type.name, type.name};
            if (item.kind == SymbolKind::Interface) return {Kind::Interface, item.name, qualified(type.symbol)};
            return {record(type.symbol) ? Kind::Record : Kind::Class, item.name, qualified(type.symbol)};
        }
        default: return {Kind::Other, type.name, type.name};
        }
    }

    std::string identity(const IrType& type) const {
        const auto parts = describe(type);
        std::string result = std::to_string(static_cast<int>(parts.kind)) + ":" + parts.qualified;
        SymbolId declared = type.kind == TypeKind::Object || type.kind == TypeKind::Enum ? type.symbol : no_symbol;
        if (valid(declared)) {
            const auto file = source_file(program_, declared);
            if (file != no_symbol) {
                const auto& item = symbol(file);
                std::string name = item.library_path.empty() ? item.name : "std/" + item.library_path;
                if (item.library_path.empty()) {
                    // Only the file's own name: the directory is not part of
                    // the program's identity.
                    const auto slash = name.find_last_of("/\\");
                    if (slash != std::string::npos) name = name.substr(slash + 1);
                }
                result += "@" + name;
            }
        }
        return result;
    }

    // ---- descriptors -----------------------------------------------------

    std::string tags(const IrSymbol& item) const {
        std::string result;
        for (const auto* tag : find_attributes(item, AttributeId::Tag)) {
            const auto& value = tag->value;
            if (value.kind != AttributeValueKind::List || value.items.size() < 3) continue;
            result += "@\t" + escape(value.items[1].text);
            if (value.items[2].kind == AttributeValueKind::List) {
                for (const auto& argument : value.items[2].items) {
                    if (argument.kind != AttributeValueKind::List || argument.items.size() < 2) continue;
                    result += "\t" + escape(argument.items[0].text) + "=" + escape(attribute_text_of(argument.items[1]));
                }
            }
            result += "\n";
        }
        return result;
    }

    std::string attribute_text_of(const IrAttributeValue& value) const {
        switch (value.kind) {
        case AttributeValueKind::Bool: return value.integer ? "true" : "false";
        case AttributeValueKind::Signed: return std::to_string(static_cast<std::int64_t>(value.integer));
        case AttributeValueKind::Unsigned: return std::to_string(value.integer);
        case AttributeValueKind::Text: return value.text;
        case AttributeValueKind::Symbol: return valid(value.symbol) ? qualified(value.symbol) : "";
        case AttributeValueKind::Type: return describe(value.type).name;
        case AttributeValueKind::Span: return "";
        case AttributeValueKind::List: {
            std::string result = "[";
            for (std::size_t i = 0; i < value.items.size(); ++i)
                result += (i ? ", " : "") + attribute_text_of(value.items[i]);
            return result + "]";
        }
        }
        return "";
    }

    int ord_or_any(const IrType& type) const {
        if (type.kind == TypeKind::Any) return -2;
        if (type.kind == TypeKind::Void) return 0;
        if (const auto tag = any_tag(type); tag && type.kind != TypeKind::Object) return static_cast<int>(*tag);
        const auto found = index_.find(type_identity(type));
        return found == index_.end() ? -1 : found->second;
    }

    std::string params(SymbolId function) const {
        std::string result;
        for (const auto parameter : symbol(function).parameters) {
            result += "P\t" + escape(symbol(parameter).name) + "\t" + std::to_string(ord_or_any(symbol(parameter).type)) +
                      "\n" + tags(symbol(parameter));
        }
        return result;
    }

    std::string descriptor(int k, ValueLayouts& layouts) {
        const auto& type = types_[k];
        const auto parts = describe(type);
        std::size_t words = 1;
        std::string form = "word";
        bool plain = true;
        std::string bits = "0";
        if (k == 0) {
            words = 0;
        } else {
            words = layouts.value_words(type);
            const auto& layout = layouts.layout(type, span_);
            switch (layout.form) {
            case ValueLayout::Form::Word: form = "word"; break;
            case ValueLayout::Form::Tuple: form = "tuple"; break;
            case ValueLayout::Form::Enum: form = "enum"; break;
            case ValueLayout::Form::Reference: form = "reference"; break;
            case ValueLayout::Form::Any: form = "any"; break;
            }
            for (const auto& word : layout.words)
                if (word.kind == TypeKind::String || word.kind == TypeKind::Object || word.kind == TypeKind::Any ||
                    word.kind == TypeKind::Array || word.kind == TypeKind::List)
                    plain = false;
            if (type.kind == TypeKind::Object) {
                form = "reference";
                plain = false;
            }
            if (type.kind == TypeKind::Bool) bits = "1";
            if (type.kind == TypeKind::Integer || type.kind == TypeKind::Float) {
                std::string digits;
                for (const char c : type.name)
                    if (c >= '0' && c <= '9') digits += c;
                bits = digits.empty() ? "0" : digits;
            }
        }
        const auto pol = policy_of(type);
        std::string result = "T\t" + std::to_string(static_cast<int>(parts.kind)) + "\t" + escape(parts.name) + "\t" +
                             escape(parts.qualified) + "\t" + escape(identity(type)) + "\t" + std::to_string(words) +
                             "\t" + form + "\t" + (plain ? "1" : "0") + "\t" + bits + "\t" +
                             (pol == Policy::Strip ? "strip" : pol == Policy::Layout ? "layout" : "full");
        int base = 0;
        std::string origin;
        if (type.kind == TypeKind::Object && valid(type.symbol)) {
            const auto& item = symbol(type.symbol);
            if (valid(item.base) && item.base != root_) base = ord_or_any(symbol(item.base).type);
            const auto open = item.name.find('<');
            if (open != std::string::npos) origin = item.name.substr(0, open);
        }
        if (type.kind == TypeKind::Enum && valid(type.symbol)) {
            const auto open = symbol(type.symbol).name.find('<');
            if (open != std::string::npos) origin = symbol(type.symbol).name.substr(0, open);
        }
        result += "\t" + std::to_string(base) + "\t" + escape(origin) + "\n";
        if (type.kind == TypeKind::Object || type.kind == TypeKind::Enum) {
            // A type's own tags are kept under every policy: the codec reads #Sensitive
            // and #SerialName from them.
            if (valid(type.symbol)) result += tags(symbol(type.symbol));
        }
        if (!origin.empty()) {
            // The written type arguments of a generic instance.
            const auto& name = symbol(type.symbol).name;
            const auto inner = name.substr(origin.size() + 1, name.size() - origin.size() - 2);
            int depth = 0;
            std::string current;
            for (const char c : inner) {
                if (c == '<' || c == '[' || c == '(') ++depth;
                if (c == '>' || c == ']' || c == ')') --depth;
                if (c == ',' && depth == 0) {
                    result += "G\t" + escape(current) + "\n";
                    current.clear();
                    continue;
                }
                if (!(c == ' ' && current.empty())) current += c;
            }
            if (!current.empty()) result += "G\t" + escape(current) + "\n";
        }
        if (type.kind == TypeKind::Array || type.kind == TypeKind::List || type.kind == TypeKind::Tuple)
            for (const auto& argument : type.arguments) result += "E\t" + std::to_string(ord_or_any(argument)) + "\n";
        if (type.kind == TypeKind::Enum && valid(type.symbol)) {
            for (const auto variant : variants(type.symbol)) {
                const auto& item = symbol(variant);
                result += "V\t" + escape(item.name) + "\n" + tags(item);
                for (std::size_t i = 0; i < item.payload_types.size(); ++i) {
                    const auto name = i < item.payload_names.size() ? item.payload_names[i] : std::to_string(i);
                    result += "P\t" + escape(name) + "\t" + std::to_string(ord_or_any(item.payload_types[i])) + "\n";
                }
            }
        }
        if (type.kind == TypeKind::Object && valid(type.symbol)) {
            const auto id = type.symbol;
            for (const auto& face : symbol(id).interfaces) result += "I\t" + std::to_string(ord_or_any(face)) + "\n";
            if (pol != Policy::Strip && !symbol(id).name.starts_with("$signature")) {
                for (const auto& field : fields(id)) {
                    std::string flags;
                    if (field.exported) flags += 'e';
                    if (field.constant) flags += 'c';
                    if (field.last) flags += 'l';
                    if (field.getter != no_symbol) flags += 'g';
                    if (field.setter != no_symbol) flags += 's';
                    // The dynamic layer can read and write it (an any, or a type it can hold).
                    if (usable(field.type)) flags += 'd';
                    result += "F\t" + escape(field.name) + "\t" + std::to_string(ord_or_any(field.type)) + "\t" + flags +
                              "\n" + tags(symbol(field.symbol));
                }
                if (symbol(id).kind == SymbolKind::Class && !abstract(id)) {
                    const auto inits = constructors(id);
                    for (std::size_t j = 0; j < inits.size(); ++j)
                        result += "C\t" + std::to_string(j) + "\t" + (symbol(inits[j]).exported ? "e" : "") + "\n" +
                                  params(inits[j]);
                    if (inits.empty()) result += "C\t0\te\n";
                }
                if (pol == Policy::Full) {
                    for (const auto method : methods(id)) {
                        const auto& item = symbol(method);
                        std::string flags;
                        if (item.exported) flags += 'e';
                        if (item.exported && invocable(method)) flags += 'v';
                        result += "M\t" + escape(item.name) + "\t" + std::to_string(ord_or_any(item.type)) + "\t" +
                                  flags + "\n" + tags(item) + params(method);
                    }
                }
            }
        }
        return result;
    }

    // ---- building blocks -------------------------------------------------

    IrNode integer(std::int64_t value, const IrType& type = int_type) const {
        auto result = make(IrOp::Integer, span_, type);
        result.text = std::to_string(value);
        return result;
    }

    IrNode text(const std::string& value) const {
        auto result = make(IrOp::String, span_, text_type);
        result.text = value;
        return result;
    }

    IrNode load(SymbolId id) const {
        auto result = make(IrOp::Load, span_, symbol(id).type);
        result.symbol = id;
        return result;
    }

    IrNode binary(const std::string& op, IrNode a, IrNode b, const IrType& type = bool_type) const {
        auto result = make(IrOp::Binary, span_, type);
        result.text = op;
        result.operands.push_back(std::move(a));
        result.operands.push_back(std::move(b));
        return result;
    }

    IrNode trap(const std::string& message) const { return with(make(IrOp::TrapMessage, span_), {text(message)}); }

    IrNode ret(IrNode value) const { return with(make(IrOp::Return, span_), {std::move(value)}); }

    IrNode branch(IrNode condition, std::vector<IrNode> body, std::vector<IrNode> otherwise = {}) const {
        auto result = with(make(IrOp::Branch, span_), {std::move(condition)});
        result.body = std::move(body);
        result.alternative = std::move(otherwise);
        return result;
    }

    IrNode store(IrNode destination, IrNode value) const {
        return with(make(IrOp::Store, span_), {std::move(destination), std::move(value)});
    }

    IrNode field(IrNode object, SymbolId id) const {
        auto result = make(IrOp::Field, span_, symbol(id).type);
        result.symbol = id;
        result.operands.push_back(std::move(object));
        return result;
    }

    IrNode module_reference() const {
        auto result = make(IrOp::Reference, span_, symbol(module_).type);
        result.symbol = module_;
        return result;
    }

    IrNode call_function(SymbolId function, std::vector<IrNode> arguments) const {
        auto result = make(IrOp::VirtualCall, span_, symbol(function).type);
        result.symbol = function;
        result.operands.push_back(module_reference());
        for (auto& argument : arguments) result.operands.push_back(std::move(argument));
        return result;
    }

    IrNode class_is(IrNode value, const IrType& type) const {
        auto result = with(make(IrOp::ClassIs, span_, bool_type), {std::move(value)});
        result.type_arguments.push_back(type);
        return result;
    }

    IrNode class_cast(IrNode value, const IrType& type) const {
        return with(make(IrOp::ClassCast, span_, type), {std::move(value)});
    }

    IrNode any_tag_is(SymbolId local, std::uint32_t tag) const {
        auto read = with(make(IrOp::AnyTag, span_, uint32_type), {load(local)});
        return binary("==", std::move(read), integer(tag, uint32_type));
    }

    IrNode root_nil() const { return make(IrOp::Nil, span_, symbol(root_).type); }

    // A function being built: its symbol, its parameters and its statements.
    struct Builder {
        SymbolId id;
        std::vector<SymbolId> parameters;
        std::vector<IrNode> body;
    };

    Builder function(const std::string& name, const IrType& result,
                     const std::vector<std::pair<std::string, IrType>>& parameters) {
        IrSymbol item{};
        item.kind = SymbolKind::Function;
        item.name = name;
        item.span = span_;
        item.owner = module_;
        item.type = result;
        item.has_body = true;
        const auto id = add_symbol(std::move(item));
        Builder builder{id, {}, {}};
        for (const auto& [parameter_name, type] : parameters) {
            IrSymbol parameter{};
            parameter.kind = SymbolKind::Parameter;
            parameter.name = parameter_name;
            parameter.span = span_;
            parameter.owner = id;
            parameter.type = type;
            const auto parameter_id = add_symbol(std::move(parameter));
            symbol(id).parameters.push_back(parameter_id);
            builder.parameters.push_back(parameter_id);
        }
        return builder;
    }

    void finish(Builder& builder) { new_bodies_.push_back(IrBody{builder.id, std::move(builder.body)}); }

    void flush_bodies() {
        for (auto& body : new_bodies_) program_.bodies.push_back(std::move(body));
        new_bodies_.clear();
    }

    SymbolId local(SymbolId owner, const std::string& name, const IrType& type) {
        IrSymbol item{};
        item.kind = SymbolKind::Local;
        item.name = name;
        item.span = span_;
        item.owner = owner;
        item.type = type;
        item.constant = true;
        return add_symbol(std::move(item));
    }

    IrNode initialize(SymbolId local_id, IrNode value) const {
        auto result = with(make(IrOp::Initialize, span_), {std::move(value)});
        result.symbol = local_id;
        return result;
    }

    // The value `node` of `type` as an any: packed, or in a new box.
    IrNode pack(IrNode node, const IrType& type, SymbolId owner) {
        if (type.kind == TypeKind::Any) return node;
        if (!boxed(type)) return with(make(IrOp::AnyPack, span_, any_type), {std::move(node)});
        const auto k = ord_or_any(type);
        if (k < 0 || !boxes_.contains(k))
            throw Diagnostic(node.span, "this type is not in the reflection universe").with_code("E1301");
        const auto box = boxes_.at(k);
        const auto held = local(owner, "$box", symbol(box).type);
        auto allocation = make(IrOp::Allocate, span_, symbol(box).type);
        auto scoped = make(IrOp::ScopedValue, span_, any_type);
        scoped.body.push_back(initialize(held, std::move(allocation)));
        scoped.body.push_back(store(field(load(held), box_field_.at(k)), std::move(node)));
        scoped.body.push_back(with(make(IrOp::AnyPack, span_, any_type), {load(held)}));
        return scoped;
    }

    // Statements that read the any in the constant local `source` as a value of
    // ordinal `k` and continue with `next` on it; a value of another type
    // runs `otherwise`. Nil passes as a class reference of any class type.
    std::vector<IrNode> unpack_then(SymbolId source, int k, SymbolId owner,
                                    const std::function<std::vector<IrNode>(IrNode)>& next,
                                    const std::vector<IrNode>& otherwise, bool allow_nil) {
        const auto& type = k < 0 ? any_type : types_[k];
        std::vector<IrNode> result;
        if (k == -2 || type.kind == TypeKind::Any) {
            auto body = next(load(source));
            for (auto& item : body) result.push_back(std::move(item));
            return result;
        }
        if (k >= 1 && k <= 14) {
            const auto value = local(owner, "$value", type);
            std::vector<IrNode> body;
            body.push_back(initialize(value, with(make(IrOp::AnyUnpack, span_, type), {load(source)})));
            for (auto& item : next(load(value))) body.push_back(std::move(item));
            result.push_back(branch(any_tag_is(source, static_cast<std::uint32_t>(k)), std::move(body), otherwise));
            return result;
        }
        const auto object = local(owner, "$object", symbol(root_).type);
        std::vector<IrNode> inner;
        inner.push_back(initialize(object, with(make(IrOp::AnyUnpack, span_, symbol(root_).type), {load(source)})));
        if (boxed(type)) {
            const auto box = boxes_.at(k);
            std::vector<IrNode> found;
            auto content = field(class_cast(load(object), symbol(box).type), box_field_.at(k));
            const auto value = local(owner, "$value", type);
            found.push_back(initialize(value, std::move(content)));
            for (auto& item : next(load(value))) found.push_back(std::move(item));
            inner.push_back(branch(class_is(load(object), symbol(box).type), std::move(found), otherwise));
        } else {
            // A class or interface reference; nil is accepted where allowed.
            const auto value = local(owner, "$value", type);
            auto chosen = make(IrOp::SelectValue, span_, type);
            chosen.operands.push_back(class_is(load(object), type));
            chosen.body.push_back(class_cast(load(object), type));
            chosen.alternative.push_back(make(IrOp::Nil, span_, type));
            std::vector<IrNode> found;
            found.push_back(initialize(value, std::move(chosen)));
            for (auto& item : next(load(value))) found.push_back(std::move(item));
            IrNode admitted = class_is(load(object), type);
            if (allow_nil) {
                auto either = make(IrOp::ShortCircuit, span_, bool_type);
                either.text = "||";
                either.operands.push_back(std::move(admitted));
                either.body.push_back(with(make(IrOp::Unary, span_, bool_type), {class_is(load(object), symbol(root_).type)}));
                either.body.back().text = "!";
                admitted = std::move(either);
            }
            inner.push_back(branch(std::move(admitted), std::move(found), otherwise));
        }
        result.push_back(branch(any_tag_is(source, 0), std::move(inner), otherwise));
        return result;
    }

    std::string type_name(int k) const { return k == -2 ? "any" : k < 0 ? "?" : describe(types_[k]).name; }

    // Dispatch on an int32 parameter over sorted keys, as a balanced tree of
    // tests, each leaf running its own statements.
    std::vector<IrNode> dispatch(SymbolId selector, const std::vector<std::pair<int, std::vector<IrNode>>>& arms,
                                 std::size_t from, std::size_t to) const {
        std::vector<IrNode> result;
        if (from >= to) return result;
        if (to - from <= 3) {
            for (std::size_t i = from; i < to; ++i)
                result.push_back(branch(binary("==", load(selector), integer(arms[i].first)), arms[i].second));
            return result;
        }
        const auto middle = from + (to - from) / 2;
        result.push_back(branch(binary("<", load(selector), integer(arms[middle].first)),
                                dispatch(selector, arms, from, middle), dispatch(selector, arms, middle, to)));
        return result;
    }

    // Each argument of `arguments` (an any[] parameter), checked against
    // the ordinals in `kinds`, then `action` with the unpacked values.
    std::vector<IrNode> with_arguments(SymbolId arguments, const std::vector<int>& kinds, SymbolId owner,
                                       const std::string& what,
                                       const std::function<std::vector<IrNode>(std::vector<IrNode>)>& action) {
        std::vector<IrNode> result;
        auto count = with(make(IrOp::Length, span_, int_type), {load(arguments)});
        result.push_back(branch(binary("!=", std::move(count), integer(static_cast<std::int64_t>(kinds.size()))),
                                {trap("reflection: " + what + " takes " + std::to_string(kinds.size()) +
                                      " argument(s)")}));
        std::function<std::vector<IrNode>(std::size_t, std::vector<IrNode>)> step =
            [&](std::size_t i, std::vector<IrNode> values) -> std::vector<IrNode> {
            if (i == kinds.size()) return action(std::move(values));
            const auto held = local(owner, "$argument", any_type);
            auto element = with(make(IrOp::Index, span_, any_type), {load(arguments), integer(static_cast<std::int64_t>(i))});
            std::vector<IrNode> statements;
            statements.push_back(initialize(held, std::move(element)));
            const std::vector<IrNode> mismatch{trap("reflection: argument " + std::to_string(i + 1) + " of " + what +
                                                    " must be " + type_name(kinds[i]))};
            auto rest = unpack_then(held, kinds[i], owner, [&](IrNode value) {
                auto next = values;
                next.push_back(std::move(value));
                return step(i + 1, std::move(next));
            }, mismatch, true);
            for (auto& item : rest) statements.push_back(std::move(item));
            return statements;
        };
        for (auto& item : step(0, {})) result.push_back(std::move(item));
        return result;
    }

    std::vector<int> ordinals() const {
        std::vector<int> result;
        for (int k = 15; k < static_cast<int>(types_.size()); ++k) result.push_back(k);
        return result;
    }

    // ---- the functions ---------------------------------------------------

    void build_functions() {
        // The descriptor table. The layouts are read before any symbol is added.
        std::vector<std::string> descriptors;
        {
            ValueLayouts layouts(program_);
            for (int k = 0; k < static_cast<int>(types_.size()); ++k) descriptors.push_back(descriptor(k, layouts));
        }
        {
            auto builder = function("$reflect_table", text_array_type, {});
            auto table = make(IrOp::Collection, span_, text_array_type);
            for (const auto& item : descriptors) table.operands.push_back(text(item));
            builder.body.push_back(ret(std::move(table)));
            table_function_ = builder.id;
            finish(builder);
        }
        // The dynamic ordinal of an any: its tag, or its class, a box giving
        // its content's ordinal; nil is 0.
        {
            auto builder = function("$reflect_ordinal", int_type, {{"value", any_type}, {"fallback", int_type}});
            const auto source = local(builder.id, "$source", any_type);
            builder.body.push_back(initialize(source, load(builder.parameters[0])));
            for (std::uint32_t tag = 1; tag <= 14; ++tag)
                builder.body.push_back(branch(any_tag_is(source, tag), {ret(integer(tag))}));
            const auto object = local(builder.id, "$object", symbol(root_).type);
            std::vector<IrNode> classes;
            classes.push_back(initialize(object, with(make(IrOp::AnyUnpack, span_, symbol(root_).type), {load(source)})));
            std::vector<std::pair<std::size_t, int>> candidates;
            for (int k = 15; k < static_cast<int>(types_.size()); ++k) {
                const auto& type = types_[k];
                SymbolId cls = no_symbol;
                if (boxed(type) && !boxes_.contains(k)) continue;
                if (boxed(type)) cls = boxes_.at(k);
                else if (type.kind == TypeKind::Object && valid(type.symbol) && symbol(type.symbol).kind == SymbolKind::Class &&
                         type.symbol != root_)
                    cls = type.symbol;
                if (cls == no_symbol) continue;
                std::size_t depth = 0;
                for (auto base = symbol(cls).base; valid(base) && depth < program_.symbols.size(); base = symbol(base).base) ++depth;
                candidates.emplace_back(depth, k);
            }
            std::ranges::stable_sort(candidates, [](const auto& a, const auto& b) { return a.first > b.first; });
            // A proxy answers as its interface.
            for (const auto& [k, proxy] : proxies_) candidates.emplace_back(1, k);
            std::ranges::stable_sort(candidates, [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& [depth, k] : candidates) {
                (void)depth;
                const auto cls = proxies_.contains(k) ? proxies_.at(k) : boxed(types_[k]) ? boxes_.at(k) : types_[k].symbol;
                classes.push_back(branch(class_is(load(object), symbol(cls).type), {ret(integer(k))}));
            }
            // A live object of a class the universe left out answers as the
            // caller's static type, as typeof does (`class` for an any).
            classes.push_back(branch(class_is(load(object), symbol(root_).type), {ret(load(builder.parameters[1]))}));
            builder.body.push_back(branch(any_tag_is(source, 0), std::move(classes)));
            builder.body.push_back(ret(integer(0)));
            ordinal_function_ = builder.id;
            finish(builder);
        }
        if (need_get_ || need_set_) build_members();
        if (need_length_ || need_element_ || need_variant_) build_values();
        if (need_construct_) build_construct();
        if (need_invoke_) build_invoke();
    }

    IrNode read_member(IrNode object, const Member& member) const {
        if (member.getter != no_symbol) {
            auto call = make(IrOp::VirtualCall, span_, symbol(member.getter).type);
            call.symbol = member.getter;
            call.operands.push_back(std::move(object));
            return call;
        }
        return field(std::move(object), member.symbol);
    }

    void build_members() {
        auto get = function("$reflect_get", any_type, {{"target", any_type}, {"type", int_type}, {"member", int_type}});
        auto set = function("$reflect_set", void_type,
                            {{"target", any_type}, {"type", int_type}, {"member", int_type}, {"value", any_type}});
        std::vector<std::pair<int, std::vector<IrNode>>> get_arms, set_arms;
        const auto get_source = local(get.id, "$target", any_type);
        const auto set_source = local(set.id, "$target", any_type);
        const auto set_value = local(set.id, "$value", any_type);
        for (const int k : ordinals()) {
            const auto& type = types_[k];
            if (type.kind != TypeKind::Object || !valid(type.symbol) || symbol(type.symbol).kind != SymbolKind::Class ||
                type.symbol == root_ || symbol(type.symbol).name.starts_with("$signature"))
                continue;
            const auto members = fields(type.symbol);
            const auto pol = policy_of(type);
            const std::vector<IrNode> nil_target{trap("reflection: the target is nil or not a " + describe(type).name)};
            if (pol == Policy::Strip) {
                const std::vector<IrNode> stripped{trap("reflection: " + describe(type).name +
                                                        " keeps no members (ReflectPolicy.Strip)")};
                get_arms.emplace_back(k, stripped);
                set_arms.emplace_back(k, stripped);
                continue;
            }
            get_arms.emplace_back(k, unpack_then(get_source, k, get.id, [&](IrNode object) {
                const auto held = local(get.id, "$self", type);
                std::vector<IrNode> statements;
                statements.push_back(initialize(held, std::move(object)));
                for (std::size_t i = 0; i < members.size(); ++i) {
                    if (!usable(members[i].type)) continue;
                    const auto selected = binary("==", load(get.parameters[2]), integer(static_cast<std::int64_t>(i)));
                    if (!members[i].exported) {
                        statements.push_back(branch(selected, {trap("reflection: field " + members[i].name + " of " +
                                                                    describe(type).name + " is not exported")}));
                        continue;
                    }
                    statements.push_back(branch(selected, {ret(pack(read_member(load(held), members[i]), members[i].type, get.id))}));
                }
                return statements;
            }, nil_target, false));
            set_arms.emplace_back(k, unpack_then(set_source, k, set.id, [&](IrNode object) {
                const auto held = local(set.id, "$self", type);
                std::vector<IrNode> statements;
                statements.push_back(initialize(held, std::move(object)));
                for (std::size_t i = 0; i < members.size(); ++i) {
                    const auto& member = members[i];
                    const auto selected = binary("==", load(set.parameters[2]), integer(static_cast<std::int64_t>(i)));
                    const bool writable = pol == Policy::Full || pol == Policy::Layout;
                    if (!member.exported) {
                        statements.push_back(branch(selected, {trap("reflection: field " + member.name + " of " +
                                                                    describe(type).name + " is not exported")}));
                        continue;
                    }
                    if (!writable || member.constant || member.last || (member.getter != no_symbol && member.setter == no_symbol) ||
                        !usable(member.type)) {
                        statements.push_back(branch(selected, {trap("reflection: field " + member.name + " of " +
                                                                    describe(type).name + " is not writable")}));
                        continue;
                    }
                    const std::vector<IrNode> mismatch{trap("reflection: field " + member.name + " of " +
                                                            describe(type).name + " takes " + describe(member.type).name)};
                    auto write = unpack_then(set_value, ord_or_any(member.type), set.id, [&](IrNode value) {
                        std::vector<IrNode> done;
                        if (member.setter != no_symbol) {
                            auto call = make(IrOp::VirtualCall, span_, symbol(member.setter).type);
                            call.symbol = member.setter;
                            call.operands.push_back(load(held));
                            call.operands.push_back(std::move(value));
                            done.push_back(with(make(IrOp::Discard, span_), {std::move(call)}));
                        } else {
                            done.push_back(store(field(load(held), member.symbol), std::move(value)));
                        }
                        done.push_back(make(IrOp::Return, span_));
                        return done;
                    }, mismatch, true);
                    statements.push_back(branch(selected, std::move(write)));
                }
                return statements;
            }, nil_target, false));
        }
        get.body.push_back(initialize(get_source, load(get.parameters[0])));
        for (auto& item : dispatch(get.parameters[1], get_arms, 0, get_arms.size())) get.body.push_back(std::move(item));
        get.body.push_back(trap("reflection: no such field"));
        set.body.push_back(initialize(set_source, load(set.parameters[0])));
        set.body.push_back(initialize(set_value, load(set.parameters[3])));
        for (auto& item : dispatch(set.parameters[1], set_arms, 0, set_arms.size())) set.body.push_back(std::move(item));
        set.body.push_back(trap("reflection: no such field"));
        get_ = get.id;
        set_ = set.id;
        finish(get);
        finish(set);
    }

    IrNode enum_tag(IrNode value, const IrType& type) const {
        return with(make(IrOp::EnumTag, span_, has_enum_payload(program_, type.symbol) ? uint32_type : type),
                    {std::move(value)});
    }

    IrNode variant_value(const IrType& type, SymbolId variant) const {
        auto result = make(IrOp::Reference, span_, type);
        result.symbol = variant;
        return result;
    }

    // The statements that run `per_variant(i, variant)` for the active variant of
    // the enum value `value`.
    std::vector<IrNode> on_variant(IrNode value, const IrType& type, SymbolId owner,
                                   const std::function<std::vector<IrNode>(std::size_t, SymbolId)>& per_variant) {
        std::vector<IrNode> result;
        const auto all = variants(type.symbol);
        const bool payload = has_enum_payload(program_, type.symbol);
        const auto held = local(owner, "$enum", type);
        result.push_back(initialize(held, std::move(value)));
        const auto tag = local(owner, "$tag", uint32_type);
        if (payload) result.push_back(initialize(tag, enum_tag(load(held), type)));
        for (std::size_t i = 0; i < all.size(); ++i) {
            // A fieldless enum compares its value with the variant.
            auto test = payload ? binary("==", load(tag), integer(static_cast<std::int64_t>(i), uint32_type))
                                : binary("==", load(held), variant_value(type, all[i]));
            result.push_back(branch(std::move(test), per_variant(i, all[i])));
        }
        return result;
    }

    void build_values() {
        auto length = function("$reflect_length", int_type, {{"target", any_type}, {"type", int_type}});
        auto element = function("$reflect_element", any_type, {{"target", any_type}, {"type", int_type}, {"index", int_type}});
        auto variant = function("$reflect_variant", int_type, {{"target", any_type}, {"type", int_type}});
        const auto length_source = local(length.id, "$target", any_type);
        const auto element_source = local(element.id, "$target", any_type);
        const auto variant_source = local(variant.id, "$target", any_type);
        std::vector<std::pair<int, std::vector<IrNode>>> length_arms, element_arms, variant_arms;
        const std::vector<IrNode> mismatch{trap("reflection: the value does not have the type given")};
        for (const int k : ordinals()) {
            const auto type = types_[k];
            if (!usable(type)) continue;
            if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
                const auto element_type = type.arguments.front();
                length_arms.emplace_back(k, unpack_then(length_source, k, length.id, [&](IrNode value) {
                    return std::vector<IrNode>{ret(with(make(IrOp::Length, span_, int_type), {std::move(value)}))};
                }, mismatch, false));
                if (usable(element_type))
                    element_arms.emplace_back(k, unpack_then(element_source, k, element.id, [&](IrNode value) {
                        auto index = with(make(IrOp::Index, span_, element_type), {std::move(value), load(element.parameters[2])});
                        return std::vector<IrNode>{ret(pack(std::move(index), element_type, element.id))};
                    }, mismatch, false));
            } else if (type.kind == TypeKind::Tuple) {
                length_arms.emplace_back(k, std::vector<IrNode>{ret(integer(static_cast<std::int64_t>(type.arguments.size())))});
                element_arms.emplace_back(k, unpack_then(element_source, k, element.id, [&](IrNode value) {
                    const auto held = local(element.id, "$tuple", type);
                    std::vector<IrNode> statements;
                    statements.push_back(initialize(held, std::move(value)));
                    for (std::size_t i = 0; i < type.arguments.size(); ++i) {
                        if (!usable(type.arguments[i])) continue;
                        auto project = with(make(IrOp::Project, span_, type.arguments[i]), {load(held)});
                        project.text = std::to_string(i);
                        statements.push_back(branch(binary("==", load(element.parameters[2]), integer(static_cast<std::int64_t>(i))),
                                                    {ret(pack(std::move(project), type.arguments[i], element.id))}));
                    }
                    return statements;
                }, mismatch, false));
            } else if (type.kind == TypeKind::Enum && valid(type.symbol)) {
                length_arms.emplace_back(k, unpack_then(length_source, k, length.id, [&](IrNode value) {
                    return on_variant(std::move(value), type, length.id, [&](std::size_t, SymbolId v) {
                        return std::vector<IrNode>{ret(integer(static_cast<std::int64_t>(symbol(v).payload_types.size())))};
                    });
                }, mismatch, false));
                variant_arms.emplace_back(k, unpack_then(variant_source, k, variant.id, [&](IrNode value) {
                    return on_variant(std::move(value), type, variant.id, [&](std::size_t i, SymbolId) {
                        return std::vector<IrNode>{ret(integer(static_cast<std::int64_t>(i)))};
                    });
                }, mismatch, false));
                element_arms.emplace_back(k, unpack_then(element_source, k, element.id, [&](IrNode value) {
                    const auto held = local(element.id, "$payload_of", type);
                    std::vector<IrNode> statements;
                    statements.push_back(initialize(held, std::move(value)));
                    for (auto& item : on_variant(load(held), type, element.id, [&](std::size_t, SymbolId v) {
                             std::vector<IrNode> arms;
                             // Boxing a payload can add a local and move the symbol table.
                             const auto payloads = symbol(v).payload_types;
                             for (std::size_t j = 0; j < payloads.size(); ++j) {
                                 if (!usable(payloads[j])) continue;
                                 auto project = with(make(IrOp::EnumProject, span_, payloads[j]), {load(held)});
                                 project.symbol = v;
                                 project.text = std::to_string(j);
                                 arms.push_back(branch(binary("==", load(element.parameters[2]), integer(static_cast<std::int64_t>(j))),
                                                       {ret(pack(std::move(project), payloads[j], element.id))}));
                             }
                             return arms;
                         }))
                        statements.push_back(std::move(item));
                    return statements;
                }, mismatch, false));
            }
        }
        length.body.push_back(initialize(length_source, load(length.parameters[0])));
        for (auto& item : dispatch(length.parameters[1], length_arms, 0, length_arms.size())) length.body.push_back(std::move(item));
        length.body.push_back(trap("reflection: the value has no elements"));
        element.body.push_back(initialize(element_source, load(element.parameters[0])));
        for (auto& item : dispatch(element.parameters[1], element_arms, 0, element_arms.size())) element.body.push_back(std::move(item));
        element.body.push_back(trap("reflection: no such element"));
        variant.body.push_back(initialize(variant_source, load(variant.parameters[0])));
        for (auto& item : dispatch(variant.parameters[1], variant_arms, 0, variant_arms.size())) variant.body.push_back(std::move(item));
        variant.body.push_back(trap("reflection: the value is not an enum"));
        length_ = length.id;
        element_ = element.id;
        variant_ = variant.id;
        finish(length);
        finish(element);
        finish(variant);
    }

    void build_construct() {
        auto build = function("$reflect_construct", any_type,
                              {{"type", int_type}, {"selector", int_type}, {"arguments", any_array_type}});
        const auto arguments = build.parameters[2];
        std::vector<std::pair<int, std::vector<IrNode>>> arms;
        for (const int k : ordinals()) {
            const auto type = types_[k];
            const auto name = describe(type).name;
            std::vector<IrNode> statements;
            if (type.kind == TypeKind::Object && valid(type.symbol) && symbol(type.symbol).kind == SymbolKind::Class &&
                type.symbol != root_ && !symbol(type.symbol).name.starts_with("$signature")) {
                if (policy_of(type) == Policy::Strip || abstract(type.symbol)) continue;
                const auto inits = constructors(type.symbol);
                if (inits.empty()) {
                    statements.push_back(branch(binary("==", load(build.parameters[1]), integer(0)),
                                                with_arguments(arguments, {}, build.id, "new " + name, [&](std::vector<IrNode>) {
                                                    auto allocation = make(IrOp::Allocate, span_, type);
                                                    return std::vector<IrNode>{ret(pack(std::move(allocation), type, build.id))};
                                                })));
                }
                for (std::size_t j = 0; j < inits.size(); ++j) {
                    std::vector<int> kinds;
                    bool usable = symbol(inits[j]).variadic == VariadicKind::None;
                    for (const auto parameter : symbol(inits[j]).parameters) {
                        kinds.push_back(ord_or_any(symbol(parameter).type));
                        if (!this->usable(symbol(parameter).type)) usable = false;
                    }
                    if (!usable || !symbol(inits[j]).exported) {
                        statements.push_back(branch(binary("==", load(build.parameters[1]), integer(static_cast<std::int64_t>(j))),
                                                    {trap("reflection: constructor " + std::to_string(j) + " of " + name +
                                                          (usable ? " is not exported" : " cannot be called dynamically"))}));
                        continue;
                    }
                    const auto init = inits[j];
                    statements.push_back(branch(binary("==", load(build.parameters[1]), integer(static_cast<std::int64_t>(j))),
                                                with_arguments(arguments, kinds, build.id, "new " + name, [&](std::vector<IrNode> values) {
                                                    auto allocation = make(IrOp::Allocate, span_, type);
                                                    allocation.symbol = init;
                                                    allocation.operands = std::move(values);
                                                    return std::vector<IrNode>{ret(pack(std::move(allocation), type, build.id))};
                                                })));
                }
            } else if (type.kind == TypeKind::Tuple) {
                std::vector<int> kinds;
                bool usable = true;
                for (const auto& argument : type.arguments) {
                    kinds.push_back(ord_or_any(argument));
                    if (!this->usable(argument)) usable = false;
                }
                if (!usable) continue;
                for (auto& item : with_arguments(arguments, kinds, build.id, name, [&](std::vector<IrNode> values) {
                         auto tuple = make(IrOp::Tuple, span_, type);
                         tuple.operands = std::move(values);
                         return std::vector<IrNode>{ret(pack(std::move(tuple), type, build.id))};
                     }))
                    statements.push_back(std::move(item));
            } else if (type.kind == TypeKind::Enum && valid(type.symbol)) {
                const auto all = variants(type.symbol);
                const bool payload = has_enum_payload(program_, type.symbol);
                for (std::size_t i = 0; i < all.size(); ++i) {
                    std::vector<int> kinds;
                    bool usable = true;
                    for (const auto& payload_type : symbol(all[i]).payload_types) {
                        kinds.push_back(ord_or_any(payload_type));
                        if (!this->usable(payload_type)) usable = false;
                    }
                    if (!usable) continue;
                    const auto v = all[i];
                    statements.push_back(branch(binary("==", load(build.parameters[1]), integer(static_cast<std::int64_t>(i))),
                                                with_arguments(arguments, kinds, build.id, name + "." + symbol(v).name,
                                                               [&](std::vector<IrNode> values) {
                                                                   IrNode made = variant_value(type, v);
                                                                   if (payload) {
                                                                       made.op = IrOp::EnumConstruct;
                                                                       made.operands = std::move(values);
                                                                   }
                                                                   return std::vector<IrNode>{ret(pack(std::move(made), type, build.id))};
                                                               })));
                }
            } else if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
                const auto element_type = type.arguments.front();
                const int element_kind = ord_or_any(element_type);
                if (!usable(type) || !usable(element_type)) continue;
                // An empty collection, then each element checked and added.
                const auto count = local(build.id, "$count", int_type);
                statements.push_back(initialize(count, with(make(IrOp::Length, span_, int_type), {load(arguments)})));
                const auto made = local(build.id, "$made", type);
                if (type.kind == TypeKind::List) {
                    statements.push_back(initialize(made, make(IrOp::Collection, span_, type)));
                } else {
                    // An array of `count` copies of the first element, then each
                    // element stored; an empty array when there is none.
                    const auto first = local(build.id, "$first", any_type);
                    std::vector<IrNode> nonempty;
                    nonempty.push_back(initialize(first, with(make(IrOp::Index, span_, any_type), {load(arguments), integer(0)})));
                    const std::vector<IrNode> wrong{trap("reflection: element 1 of " + name + " must be " +
                                                         type_name(element_kind))};
                    for (auto& item : unpack_then(first, element_kind, build.id, [&](IrNode value) {
                             const auto filled = local(build.id, "$filled", type);
                             auto repeat = make(IrOp::RepeatCollection, span_, type);
                             auto wide = with(make(IrOp::Convert, span_, uint64_type), {load(count)});
                             repeat.operands = {std::move(wide), std::move(value)};
                             std::vector<IrNode> body;
                             body.push_back(initialize(filled, std::move(repeat)));
                             for (auto& step : fill_array(filled, count, arguments, element_type, element_kind, build.id, name))
                                 body.push_back(std::move(step));
                             body.push_back(ret(pack(load(filled), type, build.id)));
                             return body;
                         }, wrong, true))
                        nonempty.push_back(std::move(item));
                    statements.push_back(branch(binary(">", load(count), integer(0)), std::move(nonempty)));
                    statements.push_back(ret(pack(make(IrOp::Collection, span_, type), type, build.id)));
                    arms.emplace_back(k, std::move(statements));
                    continue;
                }
                const auto index = local(build.id, "$index", int_type);
                symbol(index).constant = false;
                statements.push_back(initialize(index, integer(0)));
                auto loop = with(make(IrOp::Loop, span_), {binary("<", load(index), load(count))});
                const auto item_local = local(build.id, "$item", any_type);
                loop.body.push_back(initialize(item_local, with(make(IrOp::Index, span_, any_type), {load(arguments), load(index)})));
                const std::vector<IrNode> wrong{trap("reflection: an element of " + name + " must be " + type_name(element_kind))};
                for (auto& item : unpack_then(item_local, element_kind, build.id, [&](IrNode value) {
                         auto append = with(make(IrOp::Append, span_), {load(made), std::move(value)});
                         return std::vector<IrNode>{with(make(IrOp::Discard, span_), {std::move(append)})};
                     }, wrong, true))
                    loop.body.push_back(std::move(item));
                loop.body.push_back(store(load(index), binary("+", load(index), integer(1), int_type)));
                statements.push_back(std::move(loop));
                statements.push_back(ret(pack(load(made), type, build.id)));
            } else {
                continue;
            }
            arms.emplace_back(k, std::move(statements));
        }
        for (auto& item : dispatch(build.parameters[0], arms, 0, arms.size())) build.body.push_back(std::move(item));
        build.body.push_back(trap("reflection: the type cannot be constructed with these arguments"));
        construct_ = build.id;
        finish(build);
    }

    std::vector<IrNode> fill_array(SymbolId filled, SymbolId count, SymbolId arguments, const IrType& element_type,
                                   int element_kind, SymbolId owner, const std::string& name) {
        std::vector<IrNode> result;
        const auto index = local(owner, "$index", int_type);
        symbol(index).constant = false;
        result.push_back(initialize(index, integer(1)));
        auto loop = with(make(IrOp::Loop, span_), {binary("<", load(index), load(count))});
        const auto item_local = local(owner, "$item", any_type);
        loop.body.push_back(initialize(item_local, with(make(IrOp::Index, span_, any_type), {load(arguments), load(index)})));
        const std::vector<IrNode> wrong{trap("reflection: an element of " + name + " must be " + type_name(element_kind))};
        for (auto& item : unpack_then(item_local, element_kind, owner, [&](IrNode value) {
                 auto target = with(make(IrOp::Index, span_, element_type), {load(filled), load(index)});
                 return std::vector<IrNode>{store(std::move(target), std::move(value))};
             }, wrong, true))
            loop.body.push_back(std::move(item));
        loop.body.push_back(store(load(index), binary("+", load(index), integer(1), int_type)));
        result.push_back(std::move(loop));
        return result;
    }

    void build_invoke() {
        auto build = function("$reflect_invoke", any_type,
                              {{"target", any_type}, {"type", int_type}, {"method", int_type}, {"arguments", any_array_type}});
        const auto source = local(build.id, "$target", any_type);
        std::vector<std::pair<int, std::vector<IrNode>>> arms;
        for (const int k : ordinals()) {
            const auto type = types_[k];
            if (type.kind != TypeKind::Object || !valid(type.symbol) || type.symbol == root_ ||
                symbol(type.symbol).name.starts_with("$signature") || policy_of(type) != Policy::Full)
                continue;
            const auto all = methods(type.symbol);
            if (all.empty()) continue;
            const auto name = describe(type).name;
            const std::vector<IrNode> nil_target{trap("reflection: the receiver is nil or not a " + name)};
            arms.emplace_back(k, unpack_then(source, k, build.id, [&](IrNode object) {
                const auto held = local(build.id, "$self", type);
                std::vector<IrNode> statements;
                statements.push_back(initialize(held, std::move(object)));
                for (std::size_t m = 0; m < all.size(); ++m) {
                    const auto method = all[m];
                    const auto selected = binary("==", load(build.parameters[2]), integer(static_cast<std::int64_t>(m)));
                    if (!symbol(method).exported) {
                        statements.push_back(branch(selected, {trap("reflection: method " + symbol(method).name + " of " +
                                                                    name + " is not exported")}));
                        continue;
                    }
                    if (!invocable(method)) {
                        statements.push_back(branch(selected, {trap("reflection: method " + symbol(method).name + " of " +
                                                                    name + " cannot be invoked dynamically")}));
                        continue;
                    }
                    std::vector<int> kinds;
                    for (const auto parameter : symbol(method).parameters) kinds.push_back(ord_or_any(symbol(parameter).type));
                    statements.push_back(branch(selected, with_arguments(build.parameters[3], kinds, build.id,
                                                                          name + "." + symbol(method).name,
                                                                          [&](std::vector<IrNode> values) {
                        auto call = make(IrOp::VirtualCall, span_, symbol(method).type);
                        call.symbol = method;
                        call.operands.push_back(load(held));
                        for (auto& value : values) call.operands.push_back(std::move(value));
                        std::vector<IrNode> done;
                        if (symbol(method).type.kind == TypeKind::Void) {
                            done.push_back(with(make(IrOp::Discard, span_), {std::move(call)}));
                            done.push_back(ret(with(make(IrOp::AnyPack, span_, any_type), {root_nil()})));
                        } else {
                            done.push_back(ret(pack(std::move(call), symbol(method).type, build.id)));
                        }
                        return done;
                    })));
                }
                return statements;
            }, nil_target, false));
        }
        build.body.push_back(initialize(source, load(build.parameters[0])));
        for (auto& item : dispatch(build.parameters[1], arms, 0, arms.size())) build.body.push_back(std::move(item));
        build.body.push_back(trap("reflection: no such method"));
        invoke_ = build.id;
        finish(build);
    }

    SymbolId unpacker(int k) {
        if (const auto found = unpackers_.find(k); found != unpackers_.end()) return found->second;
        const auto type = types_[k];
        auto build = function("$reflect_unpack" + std::to_string(k), type, {{"value", any_type}});
        const auto source = local(build.id, "$source", any_type);
        build.body.push_back(initialize(source, load(build.parameters[0])));
        const std::vector<IrNode> wrong{};
        for (auto& item : unpack_then(source, k, build.id, [&](IrNode value) { return std::vector<IrNode>{ret(std::move(value))}; },
                                      wrong, true))
            build.body.push_back(std::move(item));
        build.body.push_back(trap("reflection: the value is not a " + describe(type).name));
        unpackers_[k] = build.id;
        finish(build);
        return build.id;
    }

    // ---- the sites -------------------------------------------------------

    IrNode expand_site(const IrNode& node) {
        const auto& name = node.text;
        const auto span = node.span;
        auto args = node.operands;
        IrNode result = make(IrOp::Nil, span, node.type);
        if (name == "ordinal") {
            const auto& operand = args.front();
            const auto k = ord_or_any(operand.type);
            const auto root_ordinal = ord_or_any(symbol(root_).type);
            if (operand.type.kind == TypeKind::Any) {
                result = call_function(ordinal_function_, {std::move(args.front()), integer(root_ordinal)});
            } else if (operand.type.kind == TypeKind::Object) {
                const auto fallback = k < 0 ? root_ordinal : k;
                result = call_function(ordinal_function_, {with(make(IrOp::AnyPack, span, any_type), {std::move(args.front())}),
                                                            integer(fallback)});
            } else {
                // A value type: its static ordinal, the operand still evaluated.
                auto scoped = make(IrOp::ScopedValue, span, int_type);
                scoped.body.push_back(with(make(IrOp::Discard, span), {std::move(args.front())}));
                scoped.body.push_back(integer(k < 0 ? 0 : k));
                result = std::move(scoped);
            }
        } else if (name == "ordinal_of") {
            const auto k = ord_or_any(node.type_arguments.front());
            result = integer(k == -2 ? 0 : k < 0 ? 0 : k);
        } else if (name == "proxy") {
            const auto& face = node.type_arguments.empty() ? node.type : node.type_arguments.front();
            result = call_function(proxy_makers_.at(ord_or_any(face)), {std::move(args.front())});
        } else if (name == "table") {
            result = call_function(table_function_, {});
        } else if (name == "pack") {
            const auto type = args.front().type;
            result = pack_at_site(std::move(args.front()), type);
        } else if (name == "unpack") {
            const auto target = node.type_arguments.empty() ? node.type : node.type_arguments.front();
            const auto k = ord_or_any(target);
            if (k == -2) {
                result = std::move(args.front());
            } else {
                if (k < 0 || !usable(target))
                    throw Diagnostic(span, "this type cannot be held dynamically").with_code("E1301");
                result = call_function(unpacker(k), {std::move(args.front())});
            }
        } else {
            SymbolId target = no_symbol;
            if (name == "get") target = get_;
            if (name == "set") target = set_;
            if (name == "length") target = length_;
            if (name == "element") target = element_;
            if (name == "variant") target = variant_;
            if (name == "construct") target = construct_;
            if (name == "invoke") target = invoke_;
            if (target == no_symbol)
                throw Diagnostic(span, "unknown reflection intrinsic '" + name + "'").with_code("E0899");
            result = call_function(target, std::move(args));
        }
        result.span = span;
        return result;
    }

    // `pack` at a site: the box is filled in a ScopedValue with a hidden local of
    // the function that holds the site, made through a call of a per-type
    // packing function so that no site needs a local of its own.
    std::map<int, SymbolId> packers_;
    IrNode pack_at_site(IrNode value, const IrType& type) {
        if (type.kind == TypeKind::Any) return value;
        if (!boxed(type)) return with(make(IrOp::AnyPack, span_, any_type), {std::move(value)});
        const auto k = ord_or_any(type);
        if (k < 0 || !usable(type))
            throw Diagnostic(value.span, "this type cannot be held dynamically").with_code("E1301");
        auto found = packers_.find(k);
        if (found == packers_.end()) {
            auto build = function("$reflect_pack" + std::to_string(k), any_type, {{"value", type}});
            build.body.push_back(ret(pack(load(build.parameters[0]), type, build.id)));
            found = packers_.emplace(k, build.id).first;
            finish(build);
        }
        return call_function(found->second, {std::move(value)});
    }
};

} // namespace

bool prepare_reflection(IrProgram& program) { return Reflection(program).prepare(); }

std::optional<std::string> reflection_proxy_problem(const IrProgram& program, const IrType& type) {
    const auto valid = [&](SymbolId id) { return id < program.symbols.size(); };
    if (type.kind != TypeKind::Object || !valid(type.symbol) || program.symbols[type.symbol].kind != SymbolKind::Interface)
        return "only an interface can be proxied";
    std::vector<SymbolId> pending{type.symbol};
    std::set<SymbolId> visited;
    while (!pending.empty()) {
        const auto scope = pending.back();
        pending.pop_back();
        if (!valid(scope) || !visited.insert(scope).second) continue;
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            const auto& item = program.symbols[id];
            if (item.owner == scope && item.kind == SymbolKind::Function) {
                if (item.name.starts_with('$')) return "it requires a property, which a proxy cannot implement";
                if (!item.generics.empty()) return "its method '" + item.name + "' is generic";
                if (item.variadic != VariadicKind::None) return "its method '" + item.name + "' is variadic";
            }
            if (item.owner == scope && is_static_scope(item)) {
                for (SymbolId inner = 0; inner < program.symbols.size(); ++inner)
                    if (program.symbols[inner].owner == id && program.symbols[inner].kind == SymbolKind::Function)
                        return "it has a static requirement";
            }
        }
        for (const auto& face : program.symbols[scope].interfaces) pending.push_back(face.symbol);
    }
    return std::nullopt;
}

std::optional<std::string> reflection_hold_problem(const IrProgram& program, const IrType& type) {
    const std::function<bool(const IrType&)> scope_bound = [&](const IrType& item) {
        if (item.kind == TypeKind::Object && item.symbol < program.symbols.size()) {
            const auto file = source_file(program, item.symbol);
            if (file == no_symbol) return false;
            const auto& path = program.symbols[file].library_path;
            const auto& name = program.symbols[item.symbol].name;
            return (name.starts_with("Task") && path == "std/concurrent/task.nra") ||
                   (name == "Scope" && path == "std/concurrent/scope.nra");
        }
        return std::ranges::any_of(item.arguments, scope_bound);
    };
    const std::function<bool(const IrType&)> region = [&](const IrType& item) {
        return item.kind == TypeKind::Array || std::ranges::any_of(item.arguments, region);
    };
    if (type.kind == TypeKind::Object && is_primitive_reference(type)) return "a primitive reference";
    if (scope_bound(type)) return "a scope-bound value";
    if (region(type)) return "an array, which lives in its region,";
    return std::nullopt;
}

void expand_reflection(IrProgram& program) { Reflection(program).expand(); }

} // namespace neura
