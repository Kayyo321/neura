#include "specialize.hpp"

#include "frozen.hpp"
#include "pointer_safety.hpp"
#include "any.hpp"
#include "reference_storage.hpp"

#include "attributes.hpp"
#include "type_of.hpp"
#include "reflect.hpp"
#include "class_hooks.hpp"
#include "linker.hpp"

#include "monitors.hpp"
#include "purity.hpp"
#include "task_analysis.hpp"
#include "nominal.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace neura {
namespace {

using Substitution = std::map<SymbolId, IrType>;

// Where a chain of instantiations started in non-generic code, for
// diagnostics about specialization that does not terminate.
struct Origin {
    Span span;
    std::string name;
};

// A body to copy into an instance: a generic function's body into a
// function instance, or a generic class's field initializers into a class
// instance.
struct Work {
    std::size_t body = 0;
    SymbolId owner = no_symbol;
    Substitution substitution;
    // Parameters of the original function mapped to the instance's.
    std::map<SymbolId, SymbolId> symbols;
    // The declaration that owns locals in the original body.
    SymbolId original = no_symbol;
    // The class instance that the body belongs to, if any.
    SymbolId instance = no_symbol;
    Origin origin;
};

struct Context {
    const Substitution* substitution = nullptr;
    std::map<SymbolId, SymbolId>* symbols = nullptr;
    SymbolId original = no_symbol;
    SymbolId owner = no_symbol;
    SymbolId instance = no_symbol;
    Origin origin;
    // True while copying a generic body into an instance. Instantiations
    // found while copying continue the chain that created the instance.
    bool copying = false;
};

std::string type_text(const IrType& type) {
    if ((type.kind == TypeKind::Array || type.kind == TypeKind::List) && type.arguments.size() == 1)
        return type_text(type.arguments.front()) + (type.kind == TypeKind::Array ? "[]" : "[list]");
    if (type.kind == TypeKind::Tuple) {
        std::string text = "[";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? ", " : "") + type_text(type.arguments[i]);
        return text + "]";
    }
    if (type.arguments.empty()) return type.name;
    std::string text = type.name + "<";
    for (std::size_t i = 0; i < type.arguments.size(); ++i) text += (i ? ", " : "") + type_text(type.arguments[i]);
    return text + ">";
}

bool concrete(const IrType& type) {
    if (type.kind == TypeKind::TypeParameter) {
        return false;
    }
    return std::ranges::all_of(type.arguments, concrete);
}

std::string key(const IrType& type) {
    std::string result = std::to_string(static_cast<int>(type.kind)) + ":" + type.name + "#" +
                         (type.symbol == no_symbol ? std::string("-") : std::to_string(type.symbol));
    if (!type.arguments.empty()) {
        result += "<";
        for (const auto& argument : type.arguments) {
            result += key(argument) + ",";
        }
        result += ">";
    }
    return result;
}

std::string display(const IrType& type) {
    if ((type.kind == TypeKind::Array || type.kind == TypeKind::List) && type.arguments.size() == 1) {
        return display(type.arguments.front()) + (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    std::string result = type.name;
    if (!type.arguments.empty()) {
        result += "<";
        for (std::size_t i = 0; i < type.arguments.size(); ++i) {
            result += (i == 0 ? "" : ", ") + display(type.arguments[i]);
        }
        result += ">";
    }
    return result;
}

std::string display(const std::vector<IrType>& arguments) {
    std::string result = "<";
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        result += (i == 0 ? "" : ", ") + display(arguments[i]);
    }
    return result + ">";
}

IrNode node_at(IrOp op, const IrType& type, const Span& span) {
    IrNode result{};
    result.op = op;
    result.type = type;
    result.span = span;
    return result;
}

IrNode native_argument_failure(const IrType& type, const Span& span) {
    auto message = node_at(IrOp::String, type, span);
    message.text = "invalid argument: argument does not fit the native layout";
    auto failure = node_at(IrOp::TrapMessage, {}, span);
    failure.operands.push_back(std::move(message));

    auto result = node_at(IrOp::ScopedValue, type, span);
    result.body.push_back(std::move(failure));
    result.body.push_back(node_at(IrOp::String, type, span));
    return result;
}

class Specializer {
    // Static requirements (docs/design/Records030.md): by requirement and
    // type key, the function that implements it.
    bool implementations_built_ = false;
    std::map<std::pair<SymbolId, std::string>, SymbolId> implementations_;

public:
    Specializer(IrProgram& program, const SpecializationLimits& limits)
        : program_(program), limits_(limits), original_symbols_(program.symbols.size()),
          original_bodies_(program.bodies.size()) {}

    void run() {
        for (SymbolId id = 0; id < original_symbols_; ++id) {
            const auto owner = program_.symbols[id].owner;
            if (owner != no_symbol) {
                members_[owner].push_back(id);
            }
        }
        for (std::size_t i = 0; i < original_bodies_; ++i) {
            body_of_[program_.bodies[i].owner] = i;
        }

        // Non-generic declarations keep their symbols; only the types they
        // mention are rewritten. Their bodies are rewritten in place.
        Substitution none;
        std::map<SymbolId, SymbolId> identity;
        for (SymbolId id = 0; id < original_symbols_; ++id) {
            if (in_template(id)) {
                continue;
            }
            Context context{&none, &identity, no_symbol, no_symbol, no_symbol,
                            {program_.symbols[id].span, program_.symbols[id].name}, false};
            // type() can append symbols, so it gets copies, not references
            // into the symbol table.
            const auto original = program_.symbols[id].type;
            const auto span = program_.symbols[id].span;
            auto rewritten = type(original, context, span);
            program_.symbols[id].type = std::move(rewritten);
            const auto payload = program_.symbols[id].payload_types;
            for (std::size_t field = 0; field < payload.size(); ++field) {
                auto rewritten_field = type(payload[field], context, span);
                program_.symbols[id].payload_types[field] = std::move(rewritten_field);
            }
            const auto interfaces = program_.symbols[id].interfaces;
            for (std::size_t interface = 0; interface < interfaces.size(); ++interface) {
                auto rewritten_interface = type(interfaces[interface], context, span);
                program_.symbols[id].interfaces[interface] = std::move(rewritten_interface);
            }
            auto attributes = specialized_attributes(program_.symbols[id].attributes, context, span);
            program_.symbols[id].attributes = std::move(attributes);
        }
        for (std::size_t i = 0; i < original_bodies_; ++i) {
            const auto owner = program_.bodies[i].owner;
            if (in_template(owner)) {
                continue;
            }
            Context context{&none, &identity, no_symbol, owner, no_symbol, {program_.symbols[owner].span, ""}, false};
            auto instructions = program_.bodies[i].instructions;
            program_.bodies[i].instructions = region(instructions, context);
        }
        while (!work_.empty()) {
            auto item = std::move(work_.front());
            work_.pop_front();
            Context context{&item.substitution, &item.symbols, item.original, item.owner, item.instance,
                            item.origin, true};
            const auto instructions = program_.bodies[item.body].instructions;
            auto copied = region(instructions, context);
            program_.bodies.push_back({item.owner, std::move(copied)});
        }
        check_result();
        // Template bodies belong to pre-specialization IR. Their references
        // still use structural types, while concrete declarations have been
        // rewritten to layouts. Only instantiated bodies enter optimization.
        std::erase_if(program_.bodies, [&](const IrBody& body) { return in_template(body.owner); });
    }

private:
    IrSymbol& symbol(SymbolId id) { return program_.symbols.at(id); }

    // True for a declaration inside a generic class or function, including
    // the generic declaration itself and its parameters and locals.
    // Whether `id` is a static requirement of an interface, or one of its
    // parameters, and `scope` the interface's static scope.
    bool requirement_signature(SymbolId id, const IrSymbol& scope) const {
        if (scope.owner == no_symbol || program_.symbols[scope.owner].kind != SymbolKind::Interface) return false;
        auto function = id;
        if (program_.symbols[function].kind == SymbolKind::Parameter) function = program_.symbols[function].owner;
        const auto& item = program_.symbols[function];
        return item.kind == SymbolKind::Function && !item.has_body && !item.external &&
               &program_.symbols[item.owner] == &scope;
    }

    bool in_template(SymbolId id) const {
        bool hook = false;
        for (auto scope = id; scope != no_symbol && scope < original_symbols_;
             scope = program_.symbols[scope].owner) {
            const auto& item = program_.symbols[scope];
            if (!item.generics.empty() || item.kind == SymbolKind::TypeParameter) {
                return true;
            }
            hook = hook || has_attribute(item, AttributeId::HookOf);
            // Class static members are shared across all instances and
            // Static interface requirements and hook functions specialize
            // with their declaring type.
            if (is_static_scope(item) && !hook && !requirement_signature(id, item)) return false;
        }
        return false;
    }

    // The hook function of `generic`'s class hook clause, cloned for
    // `instance` with the instance as its class.
    void clone_hooks(SymbolId generic, SymbolId instance, const Substitution& substitution, const Context& context) {
        for (const auto kind : {HookKind::Get, HookKind::Set}) {
            const auto function = declared_hook(program_.symbols[generic], kind);
            if (function == no_symbol || function >= original_symbols_) continue;
            const auto copy = clone_function(function, program_.symbols[function].owner, substitution,
                                             program_.symbols[function].name, context);
            set_attribute(symbol(copy), AttributeId::HookOf,
                          attribute_list({attribute_symbol(instance),
                                          attribute_unsigned(kind == HookKind::Set ? 1 : 0)}));
            member_map_[instance][function] = copy;
        }
    }

    bool generic_hook(SymbolId id) const {
        if (id == no_symbol || id >= original_symbols_) return false;
        const auto cls = hook_owner(program_.symbols[id]);
        return cls != no_symbol && cls < original_symbols_ && !program_.symbols[cls].generics.empty();
    }

    std::size_t depth(const IrType& type) const {
        if (!type.arguments.empty()) {
            return 1 + depth(type.arguments);
        }
        if (type.kind == TypeKind::Object || type.kind == TypeKind::Enum) {
            const auto found = instance_depth_.find(type.symbol);
            return found == instance_depth_.end() ? 0 : found->second;
        }
        return 0;
    }

    std::size_t depth(const std::vector<IrType>& arguments) const {
        std::size_t result = 0;
        for (const auto& argument : arguments) {
            result = std::max(result, depth(argument));
        }
        return result;
    }

    // The targets each pointer kind accepts (docs/lang/Neura0.3.1.md, section
    // 2): UniquePointer owns a class instance; SharedPointer, WeakPointer, and
    // SoftPointer share a managed object, which an array or list also is; an
    // AtomicPointer holds a deeply immutable class instance; LockedPointer
    // guards any value. Checked on each concrete instantiation.
    void check_pointer_target(PointerKind pointer, const std::string& name, const IrType& target, const Span& where,
                              const Origin& origin) {
        if (target.kind == TypeKind::TypeParameter) return;
        const bool object = target.kind == TypeKind::Object && !is_primitive_reference(target);
        const bool interface = object && target.symbol < program_.symbols.size() &&
                               symbol(target.symbol).kind == SymbolKind::Interface;
        std::string needs;
        switch (pointer) {
        case PointerKind::Unique:
            if (!object) needs = "a class or interface type, whose instance it owns";
            break;
        case PointerKind::Shared:
        case PointerKind::Weak:
        case PointerKind::Soft:
            if (!object && target.kind != TypeKind::Array && target.kind != TypeKind::List)
                needs = "a managed object type: a class, an interface, an array, or a list";
            break;
        case PointerKind::Atomic:
            if (!object || interface) needs = "an #Immutable class type";
            break;
        default:
            break;
        }
        const auto label = [&](Diagnostic& error) {
            if (!origin.span.file.empty() && origin.span.offset != where.offset)
                error.label(origin.span, "instantiated here");
        };
        if (!needs.empty()) {
            auto error = Diagnostic(where, std::string(pointer_kind_name(pointer)) + " needs " + needs + ", not " +
                                               type_text(target))
                .with_code("E0418").label("in '" + name + "'");
            label(error);
            if (pointer != PointerKind::Atomic) error.help("hold a value in a LockedPointer, or in a Pointer to a Cell");
            throw error;
        }
        if (pointer == PointerKind::Atomic) {
            if (const auto problem = mutable_part(program_, target)) {
                auto error = Diagnostic(where, "an AtomicPointer can hold only a deeply immutable value")
                    .with_code("E0580").label("'" + name + "' would publish a mutable " + type_text(target));
                if (problem->span) error.label(*problem->span, problem->text);
                label(error);
                error.help("mark the class #Immutable, or guard mutable state with a LockedPointer");
                throw error;
            }
        }
    }

    void check_limits(const std::string& name, std::size_t nesting, const Span& where, const Origin& origin) {
        // The instantiation that the program wrote, where the chain starts.
        const auto start = [&](Diagnostic& error) {
            error.label(origin.span, origin.name.empty() ? "the chain of instantiations starts here"
                                                         : "in this instantiation of '" + origin.name + "'");
        };
        if (nesting > limits_.max_depth) {
            auto error = Diagnostic(where, "generic specialization does not terminate: the type arguments of '" + name +
                                               "' nest deeper than " + std::to_string(limits_.max_depth) + " levels")
                .with_code("E0808").label("instantiates '" + name + "'");
            start(error);
            error.note("each instantiation here instantiates a deeper one, so specialization would never finish");
            throw error;
        }
        if (instances_.size() >= limits_.max_instances) {
            auto error = Diagnostic(where, "generic specialization creates more than " +
                                               std::to_string(limits_.max_instances) + " instances")
                .with_code("E0808").label("instantiates '" + name + "'");
            start(error);
            throw error;
        }
    }

    IrType type(const IrType& original, const Context& context, const Span& where) {
        if (original.kind == TypeKind::TypeParameter) {
            const auto found = context.substitution->find(original.symbol);
            return found == context.substitution->end() ? original : found->second;
        }
        IrType result = original;
        for (auto& argument : result.arguments) {
            argument = type(argument, context, where);
        }
        if (is_function_value(result) && std::ranges::all_of(result.arguments, concrete)) {
            return callable_type(result, where);
        }
        if ((result.kind == TypeKind::Object || result.kind == TypeKind::Enum) && result.symbol < original_symbols_ &&
            !program_.symbols[result.symbol].generics.empty() && !result.arguments.empty() &&
            std::ranges::all_of(result.arguments, concrete)) {
            const auto instance = nominal_instance(result.symbol, result.arguments, where, context);
            return program_.symbols[instance].type;
        }
        return result;
    }

    SymbolId add(IrSymbol item) {
        program_.symbols.push_back(std::move(item));
        return program_.symbols.size() - 1;
    }

    // A copy's attributes (attributes.hpp): types are substituted like the
    // copy's own, and a reference to a member of a generic class, or to a
    // parameter or local of the function being copied, names that member's
    // copy in the instance. A deferred method it names is copied now.
    std::vector<IrAttribute> specialized_attributes(std::vector<IrAttribute> attributes, const Context& context,
                                                    const Span& where) {
        if (attributes.empty()) return attributes;
        for_each_attribute_type(attributes, [&](IrType& item) { item = type(item, context, where); });
        for_each_attribute_symbol(attributes, [&](SymbolId& item) {
            if (context.symbols) {
                if (const auto found = context.symbols->find(item); found != context.symbols->end()) {
                    item = found->second;
                    return;
                }
            }
            if (context.instance != no_symbol && (owned_by_generic_class(item) || generic_hook(item)))
                item = member_of(item, context.instance);
        });
        return attributes;
    }

    void specialize_attributes(SymbolId id, const Context& context) {
        if (program_.symbols[id].attributes.empty()) return;
        const auto span = program_.symbols[id].span;
        auto attributes = specialized_attributes(program_.symbols[id].attributes, context, span);
        program_.symbols[id].attributes = std::move(attributes);
    }

    IrType callable_type(const IrType& signature, const Span& span) {
        const auto signature_key = key(signature);
        if (const auto found = callable_types_.find(signature_key); found != callable_types_.end()) {
            return symbol(found->second).type;
        }
        IrSymbol layout{};
        layout.kind = SymbolKind::Class;
        layout.name = "$signature" + std::to_string(callable_types_.size());
        layout.span = span;
        const auto layout_id = add(std::move(layout));
        std::string name = "func(";
        for (std::size_t i = 0; i + 1 < signature.arguments.size(); ++i) {
            name += (i == 0 ? "" : ", ") + display(signature.arguments[i]);
        }
        name += ") -> " + display(signature.arguments.back());
        symbol(layout_id).type = {TypeKind::Object, std::move(name), layout_id, {}};
        instance_depth_[layout_id] = 1 + depth(signature.arguments);
        callable_types_.emplace(signature_key, layout_id);

        IrSymbol method{};
        method.kind = SymbolKind::Function;
        method.name = "$invoke";
        method.span = span;
        method.owner = layout_id;
        method.type = signature.arguments.back();
        const auto method_id = add(std::move(method));
        for (std::size_t i = 0; i + 1 < signature.arguments.size(); ++i) {
            IrSymbol parameter{};
            parameter.kind = SymbolKind::Parameter;
            parameter.name = "$arg" + std::to_string(i);
            parameter.span = span;
            parameter.owner = method_id;
            parameter.type = signature.arguments[i];
            const auto id = add(std::move(parameter));
            symbol(method_id).parameters.push_back(id);
        }
        callable_methods_.emplace(layout_id, method_id);
        return symbol(layout_id).type;
    }

    SymbolId nominal_instance(SymbolId generic, const std::vector<IrType>& arguments, const Span& where,
                              const Context& context) {
        const auto kind = symbol(generic).type.kind;
        const auto name = key(IrType{kind, "", generic, arguments});
        if (const auto found = instances_.find(name); found != instances_.end()) {
            return found->second;
        }
        IrSymbol copy = program_.symbols[generic];
        const auto display_name = copy.name + display(arguments);
        const Origin origin = context.copying ? context.origin : Origin{where, display_name};
        check_limits(display_name, 1 + depth(arguments), where, origin);

        copy.name = display_name;
        copy.generics.clear();
        const auto instance = add(std::move(copy));
        symbol(instance).type = {kind, display_name, instance, {}};
        instances_.emplace(name, instance);
        instance_depth_[instance] = 1 + depth(arguments);
        instance_of_[instance] = {generic, arguments};
        if (const auto pointer = pointer_class(program_, instance); pointer != PointerKind::None && !arguments.empty()) {
            program_.pointer_pointees.emplace_back(instance, arguments.front());
            check_pointer_target(pointer, display_name, arguments.front(), where, origin);
        }

        Substitution substitution;
        const auto generics = program_.symbols[generic].generics;
        for (std::size_t i = 0; i < generics.size() && i < arguments.size(); ++i) {
            substitution.emplace(generics[i], arguments[i]);
        }
        Context inner{&substitution, nullptr, no_symbol, instance, instance, origin, true};
        const auto interfaces = program_.symbols[generic].interfaces;
        for (std::size_t interface = 0; interface < interfaces.size(); ++interface) {
            auto concrete_interface = type(interfaces[interface], inner, where);
            symbol(instance).interfaces[interface] = std::move(concrete_interface);
        }
        // Variants, fields and the non-generic methods that dispatch can reach
        // are copied now so that variant remapping and dispatch can find
        // them. Other methods of a class are copied when first referenced, so
        // a method whose signature names a larger instance of its own class
        // (Sequence<T>.enumerate() -> Sequence<[int, T]>) instantiates that
        // class only when the program calls it.
        instance_origin_[instance] = origin;
        std::set<std::string> dispatched;
        if (kind == TypeKind::Object && symbol(generic).kind == SymbolKind::Class) {
            std::set<SymbolId> seen;
            for (const auto& interface : symbol(instance).interfaces) {
                interface_methods(interface.symbol, dispatched, seen);
            }
            for (auto base = symbol(generic).base; base != no_symbol; base = symbol(base).base) {
                for (const auto& interface : symbol(base).interfaces) {
                    interface_methods(interface.symbol, dispatched, seen);
                }
            }
        }
        for (const auto member : members_[generic]) {
            // A copy: creating instances appends to the symbol table.
            const IrSymbol item = program_.symbols[member];
            if (item.kind == SymbolKind::Variant) {
                IrSymbol variant = item;
                variant.owner = instance;
                variant.type = symbol(instance).type;
                for (auto& payload : variant.payload_types) {
                    payload = type(payload, inner, item.span);
                }
                member_map_[instance][member] = add(std::move(variant));
            } else if (item.kind == SymbolKind::Field) {
                IrSymbol field = item;
                field.owner = instance;
                field.type = type(item.type, inner, item.span);
                member_map_[instance][member] = add(std::move(field));
            } else if (item.kind == SymbolKind::Function && item.generics.empty()) {
                if (deferrable(item, instance, dispatched)) {
                    deferred_[instance].insert(member);
                } else {
                    member_map_[instance][member] = clone_function(member, instance, substitution, item.name, inner);
                }
            }
        }
        if (symbol(generic).kind == SymbolKind::Class) clone_hooks(generic, instance, substitution, inner);
        // Every eager member exists now, so links between members resolve.
        specialize_attributes(instance, inner);
        std::vector<SymbolId> copies;
        for (const auto& [member, copy] : member_map_[instance]) copies.push_back(copy);
        for (const auto copy : copies) {
            specialize_overload_group(copy, instance);
            specialize_attributes(copy, inner);
            for (const auto parameter : std::vector<SymbolId>(symbol(copy).parameters))
                specialize_attributes(parameter, inner);
        }
        if (const auto body = body_of_.find(generic); body != body_of_.end()) {
            work_.push_back({body->second, instance, substitution, {}, generic, instance, origin});
        }
        return instance;
    }

    SymbolId clone_function(SymbolId original, SymbolId owner, const Substitution& substitution, std::string name,
                            const Context& context) {
        const IrSymbol declaration = program_.symbols[original];
        IrSymbol copy = declaration;
        copy.name = std::move(name);
        copy.owner = owner;
        copy.generics.clear();
        copy.parameters.clear();
        Context inner{&substitution, nullptr, no_symbol, owner, context.instance, context.origin, true};
        copy.type = type(declaration.type, inner, declaration.span);
        const auto parameters = declaration.parameters;
        const auto function = add(std::move(copy));
        Work item{0, function, substitution, {}, original, context.instance, context.origin};
        for (const auto parameter : parameters) {
            IrSymbol cloned = program_.symbols[parameter];
            cloned.owner = function;
            cloned.type = type(cloned.type, inner, cloned.span);
            const auto id = add(std::move(cloned));
            symbol(function).parameters.push_back(id);
            item.symbols[parameter] = id;
        }
        Context links = inner;
        links.symbols = &item.symbols;
        specialize_attributes(function, links);
        for (const auto parameter : std::vector<SymbolId>(symbol(function).parameters))
            specialize_attributes(parameter, links);
        if (const auto body = body_of_.find(original); body != body_of_.end()) {
            item.body = body->second;
            work_.push_back(std::move(item));
        }
        return function;
    }

    // The instance of a generic function or method for `arguments`, owned by
    // `owner`: the class instance for a method of a generic class, otherwise
    // the declaration's own owner.
    SymbolId function_instance(SymbolId generic, SymbolId owner, const std::vector<IrType>& arguments,
                               const Span& where, const Context& context) {
        const auto name = "F" + std::to_string(generic) + "@" + std::to_string(owner) +
                          key(IrType{TypeKind::Callable, "", no_symbol, arguments});
        if (const auto found = instances_.find(name); found != instances_.end()) {
            return found->second;
        }
        const auto display_name = program_.symbols[generic].name + display(arguments);
        const Origin origin = context.copying ? context.origin : Origin{where, display_name};
        check_limits(display_name, depth(arguments), where, origin);
        Substitution substitution;
        if (const auto found = instance_of_.find(owner); found != instance_of_.end()) {
            const auto& class_generics = program_.symbols[found->second.first].generics;
            for (std::size_t i = 0; i < class_generics.size(); ++i) {
                substitution.emplace(class_generics[i], found->second.second[i]);
            }
        }
        const auto generics = program_.symbols[generic].generics;
        for (std::size_t i = 0; i < generics.size() && i < arguments.size(); ++i) {
            substitution.emplace(generics[i], arguments[i]);
        }
        Context inner = context;
        inner.origin = origin;
        inner.instance = instance_of_.contains(owner) ? owner : no_symbol;
        const auto function = clone_function(generic, owner, substitution, display_name, inner);
        instances_.emplace(name, function);
        return function;
    }

    // The copy of `member` in a class instance, or `member` itself when its
    // owner is not generic. A deferred method is copied on first use.
    SymbolId member_of(SymbolId member, SymbolId instance) {
        if (const auto found = member_map_.find(instance); found != member_map_.end()) {
            if (const auto copy = found->second.find(member); copy != found->second.end()) {
                return copy->second;
            }
        }
        if (const auto deferred = deferred_.find(instance);
            deferred != deferred_.end() && deferred->second.erase(member) != 0) {
            const auto& [generic, arguments] = instance_of_.at(instance);
            Substitution substitution;
            const auto& generics = program_.symbols[generic].generics;
            for (std::size_t i = 0; i < generics.size() && i < arguments.size(); ++i) {
                substitution.emplace(generics[i], arguments[i]);
            }
            Context inner{&substitution, nullptr, no_symbol, instance, instance, instance_origin_.at(instance), true};
            const auto name = program_.symbols[member].name;
            const auto copy = clone_function(member, instance, substitution, name, inner);
            member_map_[instance][member] = copy;
            specialize_overload_group(copy, instance);
            return copy;
        }
        return member;
    }

    void specialize_overload_group(SymbolId function, SymbolId instance) {
        const auto group = symbol(function).overload_group;
        if (group == no_symbol) return;
        const auto specialized = member_of(group, instance);
        symbol(function).overload_group = symbol(specialized).owner == instance ? specialized : no_symbol;
    }

    // Whether a method of a class instance can be copied on first use: it
    // is not a constructor, destructor, override, interface implementation,
    // or compiler-generated member, so no dispatch reaches it by name.
    bool deferrable(const IrSymbol& method, SymbolId instance, const std::set<std::string>& dispatched) const {
        if (program_.symbols[instance].type.kind != TypeKind::Object) {
            return false;
        }
        const auto generic = instance_of_.at(instance).first;
        if (program_.symbols[generic].kind != SymbolKind::Class) {
            return false;
        }
        return method.overrides == no_symbol && method.name != "init" && method.name != "deinit" &&
               !method.name.starts_with("$") && !dispatched.contains(method.name);
    }

    // The method names of an interface and of the interfaces it extends.
    void interface_methods(SymbolId interface, std::set<std::string>& names, std::set<SymbolId>& seen) const {
        auto declaration = interface;
        if (const auto found = instance_of_.find(interface); found != instance_of_.end()) {
            declaration = found->second.first;
        }
        if (declaration == no_symbol || !seen.insert(declaration).second) {
            return;
        }
        if (const auto found = members_.find(declaration); found != members_.end()) {
            for (const auto member : found->second) {
                if (program_.symbols[member].kind == SymbolKind::Function) {
                    names.insert(program_.symbols[member].name);
                }
            }
        }
        for (const auto& base : program_.symbols[declaration].interfaces) {
            interface_methods(base.symbol, names, seen);
        }
    }

    bool owned_by_generic_class(SymbolId member) const {
        if (member == no_symbol || member >= original_symbols_) {
            return false;
        }
        const auto owner = program_.symbols[member].owner;
        return owner != no_symbol && owner < original_symbols_ &&
               class_or_interface(program_.symbols[owner].kind) && !program_.symbols[owner].generics.empty();
    }

    std::vector<IrNode> region(const std::vector<IrNode>& nodes, Context& context) {
        std::vector<IrNode> result;
        result.reserve(nodes.size());
        for (const auto& node : nodes) {
            result.push_back(rewrite(node, context));
        }
        return result;
    }

    IrNode rewrite(const IrNode& node, Context& context) {
        IrNode result;
        result.op = node.op;
        result.span = node.span;
        result.text = node.text;
        result.symbol = node.symbol;
        result.poll = node.poll;
        // `scope (name)` introduces the local that carries the scope's
        // identity, and its body reads it. The instance gets its own copy
        // before the body is rewritten, as an Initialize gives one to a
        // declared local.
        if (node.op == IrOp::TaskScope && node.symbol != no_symbol && context.original != no_symbol &&
            context.symbols && program_.symbols[node.symbol].kind == SymbolKind::Local &&
            program_.symbols[node.symbol].owner == context.original && !context.symbols->contains(node.symbol)) {
            IrSymbol local = program_.symbols[node.symbol];
            local.owner = context.owner;
            local.type = type(local.type, context, local.span);
            local.attributes = specialized_attributes(std::move(local.attributes), context, local.span);
            const auto copy = add(std::move(local));
            (*context.symbols)[node.symbol] = copy;
        }
        result.operands = region(node.operands, context);
        result.body = region(node.body, context);
        result.alternative = region(node.alternative, context);
        result.type = type(node.type, context, node.span);
        for (const auto& argument : node.type_arguments) {
            result.type_arguments.push_back(type(argument, context, node.span));
        }
        const bool native_object_carrier = node.op == IrOp::ScopedValue && result.body.size() == 1;
        const bool native_text_carrier = node.op == IrOp::ValueText && result.operands.size() == 1;
        if (node.text == native_argument_packing && (native_object_carrier || native_text_carrier)) {
            const auto& argument = (native_object_carrier ? result.body.front() : result.operands.front()).type;
            if (contains_pointer_type(program_, argument)) {
                auto error = Diagnostic(node.span, "a managed pointer is not a native value in Std.DynamicForeign")
                    .with_code("E0403");
                if (!context.origin.span.file.empty()) error.label(context.origin.span, "instantiated here");
                throw error;
            }
            if (native_object_carrier) return std::move(result.body.front());
            if (argument.kind == TypeKind::Any) {
                // Keep evaluation of the erased operand, then fail before packing.
                auto evaluated = node_at(IrOp::Discard, {}, node.span);
                evaluated.operands.push_back(std::move(result.operands.front()));
                auto failure = native_argument_failure(result.type, node.span);
                failure.body.insert(failure.body.begin(), std::move(evaluated));
                return failure;
            }
            // The late guard checks erased classes destined for Text.
            if (argument.kind != TypeKind::Object || is_primitive_reference(argument))
                result.text.clear();
        }
        // A function value has become an object layout of a callable type,
        // which an any does not take for a reference.
        const auto function_layout = [&](const IrType& item) {
            if (item.kind != TypeKind::Object) return false;
            for (auto scope = item.symbol; scope != no_symbol && scope < program_.symbols.size();
                 scope = program_.symbols[scope].base)
                if (callable_methods_.contains(scope)) return true;
            return false;
        };
        if (node.op == IrOp::AnyPack && !result.operands.empty() &&
            (!any_holdable(result.operands.front().type) || function_layout(result.operands.front().type))) {
            // What an `any` holds is checked on the specialized program
            // (docs/design/Matching030.md, E0492).
            auto error = Diagnostic(node.span, "a value of type " + type_text(result.operands.front().type) +
                                                   " cannot be held in any")
                .with_code("E0492").label("packed here in a generic body");
            if (!context.origin.span.file.empty()) error.label(context.origin.span, "instantiated here");
            error.help("hold it in a class and pack the reference");
            throw error;
        }
        if (node.op == IrOp::VirtualCall && node.symbol < program_.symbols.size() &&
            !result.operands.empty()) {
            const auto& called = symbol(node.symbol);
            const auto pointer = pointer_class(program_, called.owner);
            if ((pointer == PointerKind::Const && called.name == "read") ||
                (pointer == PointerKind::Mutable && called.name == "as_const")) {
                const auto& receiver = result.operands.front().type;
                const auto instance = instance_of_.find(receiver.symbol);
                if (instance != instance_of_.end() && !instance->second.second.empty()) {
                    const auto& stored = instance->second.second.front();
                    if (const auto problem = mutable_part(program_, stored)) {
                        auto error = Diagnostic(node.span, "a ConstPointer can read only a deeply immutable value")
                            .with_code("E0580").label("the pointer would expose a mutable " + type_text(stored));
                        if (problem->span) error.label(*problem->span, problem->text);
                        if (context.copying && !context.origin.span.file.empty() &&
                            context.origin.span.offset != node.span.offset)
                            error.label(context.origin.span, "instantiated here");
                        throw error;
                    }
                }
            }
        }
        if (node.op == IrOp::Reflect) {
            // What Std.Reflect can hold and proxy is checked on the specialized
            // types (docs/design/Reflect030.md, E1300 and E1301), at the call
            // that instantiated the library function when there is one.
            const auto at = context.origin.span.file.empty() ? node.span : context.origin.span;
            if (node.text == "pack" || node.text == "unpack") {
                std::vector<IrType> held;
                for (const auto& operand : result.operands) held.push_back(operand.type);
                for (const auto& argument : result.type_arguments) held.push_back(argument);
                for (const auto& item : held) {
                    const auto problem = reflection_hold_problem(program_, item);
                    if (!problem) continue;
                    throw Diagnostic(at, *problem + " cannot be held by a Std.Reflect dynamic value")
                        .with_code("E1301").label("reflects a value of type " + type_text(item))
                        .help("reflect a value that can be copied out of its scope; copy an array into a list");
                }
            }
            if (node.text == "proxy" && !result.type_arguments.empty()) {
                const auto& face = result.type_arguments.front();
                if (const auto problem = reflection_proxy_problem(program_, face))
                    throw Diagnostic(at, "the type " + type_text(face) + " cannot be proxied").with_code("E1300")
                        .label(*problem).help("proxy an interface whose requirements are non-generic instance methods");
            }
        }
        if (node.op == IrOp::CallClosure) {
            result.op = IrOp::VirtualCall;
            result.symbol = callable_methods_.at(result.operands.front().type.symbol);
            return result;
        }
        if (node.op == IrOp::MakeClosure) {
            const auto generic = symbol(node.symbol).owner;
            const auto layout = result.type_arguments.empty()
                ? generic : nominal_instance(generic, result.type_arguments, node.span, context);
            const auto base = result.type.symbol;
            symbol(layout).base = base;
            const auto method = callable_methods_.at(base);
            for (const auto member : members_[generic]) {
                if (symbol(member).name == "$invoke") {
                    symbol(member_of(member, layout)).overrides = method;
                    break;
                }
            }
            result.op = IrOp::Allocate;
            result.symbol = member_of(node.symbol, layout);
            result.type_arguments.clear();
            return result;
        }
        if (node.symbol == no_symbol) {
            return result;
        }
        if (context.symbols) {
            if (const auto found = context.symbols->find(node.symbol); found != context.symbols->end()) {
                result.symbol = found->second;
                return result;
            }
        }
        switch (node.op) {
        case IrOp::ConformanceCall:
            // docs/design/Records030.md: the direct call of the implementation
            // for the type the call now goes through, once it is concrete.
            if (concrete(result.type_arguments.front())) {
                result.op = IrOp::Call;
                result.symbol = implementation(node.symbol, result.type_arguments.front(), node.span);
                result.type_arguments.clear();
            }
            break;
        case IrOp::Reference:
        case IrOp::EnumConstruct:
            if (result.type.kind == TypeKind::Enum) {
                result.symbol = member_of(node.symbol, result.type.symbol);
            }
            break;
        case IrOp::EnumProject:
            if (!result.operands.empty()) {
                result.symbol = member_of(node.symbol, result.operands.front().type.symbol);
            }
            break;
        case IrOp::Initialize:
            if (context.original != no_symbol && context.symbols &&
                program_.symbols[node.symbol].kind == SymbolKind::Local &&
                program_.symbols[node.symbol].owner == context.original) {
                IrSymbol local = program_.symbols[node.symbol];
                local.owner = context.owner;
                local.type = type(local.type, context, local.span);
                local.attributes = specialized_attributes(std::move(local.attributes), context, local.span);
                result.symbol = add(std::move(local));
                (*context.symbols)[node.symbol] = result.symbol;
            } else if (owned_by_generic_class(node.symbol)) {
                result.symbol = member_of(node.symbol, context.instance);
            }
            break;
        case IrOp::Field:
            if (owned_by_generic_class(node.symbol) && !result.operands.empty()) {
                result.symbol = member_of(node.symbol, result.operands.front().type.symbol);
            }
            break;
        case IrOp::ReferencePlace: {
            const auto* place = &result.operands.front();
            while (place->op == IrOp::ScopedValue && !place->body.empty()) place = &place->body.back();
            result.symbol = place->symbol;
            break;
        }
        case IrOp::Receiver:
        case IrOp::Destroy:
            if (node.symbol < original_symbols_ && !program_.symbols[node.symbol].generics.empty()) {
                result.symbol = node.op == IrOp::Receiver ? context.instance : result.operands.front().type.symbol;
            }
            break;
        case IrOp::Allocate:
            if (owned_by_generic_class(node.symbol)) {
                result.symbol = member_of(node.symbol, result.type.symbol);
            }
            break;
        case IrOp::Call:
        case IrOp::VirtualCall: {
            const auto target = node.symbol;
            SymbolId owner = program_.symbols[target].owner;
            if (owned_by_generic_class(target) && node.op == IrOp::VirtualCall && !result.operands.empty()) {
                if (symbol(owner).kind == SymbolKind::Interface) {
                    const auto views = nominal_views(program_, result.operands.front().type);
                    const auto match = std::ranges::find_if(views, [&](const IrType& view) {
                        const auto instance = instance_of_.find(view.symbol);
                        return instance != instance_of_.end() && instance->second.first == owner;
                    });
                    if (match != views.end()) owner = match->symbol;
                } else {
                    owner = result.operands.front().type.symbol;
                }
            }
            if (!program_.symbols[target].generics.empty()) {
                result.symbol = function_instance(target, owner, result.type_arguments, node.span, context);
                result.type_arguments.clear();
            } else if (owned_by_generic_class(target)) {
                result.symbol = member_of(target, owner);
            }
            break;
        }
        default:
            break;
        }
        return result;
    }

    // The function that implements static requirement `requirement` for
    // `type`: the one whose static.implements names the requirement and the
    // type, or the nearest base of a class type that has one. A class
    // instance is looked up as its generic class.
    SymbolId implementation(SymbolId requirement, const IrType& type, const Span& span) {
        if (!implementations_built_) {
            implementations_built_ = true;
            for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
                for (const auto* attribute : find_attributes(program_.symbols[id], AttributeId::StaticImplements)) {
                    const auto& items = attribute->value.items;
                    implementations_.emplace(std::pair{items[0].symbol, key(items[1].type)}, id);
                }
            }
        }
        const auto lookup = [&](const IrType& item) -> SymbolId {
            const auto found = implementations_.find({requirement, key(item)});
            return found == implementations_.end() ? no_symbol : found->second;
        };
        if (type.kind == TypeKind::Object) {
            std::set<SymbolId> seen;
            for (auto cls = type.symbol; cls < program_.symbols.size() && seen.insert(cls).second;
                 cls = program_.symbols[cls].base) {
                auto generic = cls;
                if (const auto instance = instance_of_.find(cls); instance != instance_of_.end())
                    generic = instance->second.first;
                if (const auto found = lookup(program_.symbols[generic].type); found != no_symbol) return found;
            }
        } else if (const auto found = lookup(type); found != no_symbol) {
            return found;
        }
        throw Diagnostic(span, "no implementation of the static requirement '" + program_.symbols[requirement].name +
                                   "' for " + display(type)).with_code("E0899");
    }

    void check_type(const IrType& type, const std::string& where) const {
        if (!concrete(type)) {
            throw std::logic_error("specialization left a type parameter in " + where);
        }
    }

    void check_nodes(const std::vector<IrNode>& nodes, const std::string& where) const {
        for (const auto& node : nodes) {
            check_type(node.type, where);
            // A type test names its target, and a typed scheduler operation its
            // element type, as a concrete type argument.
            if (node.op == IrOp::SchedulerCall || node.op == IrOp::TypeOf || node.op == IrOp::Reflect) {
                for (const auto& argument : node.type_arguments) check_type(argument, where);
            } else if (!node.type_arguments.empty() && node.op != IrOp::ClassIs) {
                throw std::logic_error("specialization left type arguments in " + where);
            }
            check_nodes(node.operands, where);
            check_nodes(node.body, where);
            check_nodes(node.alternative, where);
        }
    }

    void check_result() const {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            if (id >= original_symbols_ || !in_template(id)) {
                check_type(program_.symbols[id].type, "symbol " + program_.symbols[id].name);
                for (const auto& payload : program_.symbols[id].payload_types) {
                    check_type(payload, "payload of " + program_.symbols[id].name);
                }
                for (const auto& interface : program_.symbols[id].interfaces) {
                    check_type(interface, "interface of " + program_.symbols[id].name);
                }
            }
        }
        for (const auto& body : program_.bodies) {
            if (body.owner >= original_symbols_ || !in_template(body.owner)) {
                check_nodes(body.instructions, "the body of " + program_.symbols[body.owner].name);
            }
        }
    }

    IrProgram& program_;
    SpecializationLimits limits_;
    const std::size_t original_symbols_;
    const std::size_t original_bodies_;
    std::map<SymbolId, std::vector<SymbolId>> members_;
    std::map<SymbolId, std::size_t> body_of_;
    std::map<std::string, SymbolId> instances_;
    std::map<SymbolId, std::size_t> instance_depth_;
    std::map<SymbolId, std::pair<SymbolId, std::vector<IrType>>> instance_of_;
    std::map<SymbolId, std::map<SymbolId, SymbolId>> member_map_;
    // Methods of class instances not copied yet, and where each instance's
    // chain of instantiations started.
    std::map<SymbolId, std::set<SymbolId>> deferred_;
    std::map<SymbolId, Origin> instance_origin_;
    std::map<std::string, SymbolId> callable_types_;
    std::map<SymbolId, SymbolId> callable_methods_;
    std::deque<Work> work_;
};

// Defer the native codec guard until after linking and specialization.
// Loading Pointer's generic templates does not create a managed pointer.
void guard_managed_pointers_in_native_packing(IrProgram& program) {
    SymbolId marker = no_symbol;
    SymbolId codec = no_symbol;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& symbol = program.symbols[id];
        if (symbol.kind == SymbolKind::File && symbol.library_path == "std/dynamic-foreign/internal/codec.nra") {
            codec = id;
        } else if (symbol.kind == SymbolKind::Class && symbol.name == "ManagedPointer" &&
                   trusted_pointer_declaration(program, id)) {
            marker = id;
        }
    }
    const auto makes_pointer = [&](const auto& self, const IrNode& node) -> bool {
        if (node.op == IrOp::RetainCellStorage ||
            (node.op == IrOp::Allocate && pointer_class(program, node.type.symbol) != PointerKind::None)) return true;
        for (const auto* nodes : {&node.operands, &node.body, &node.alternative}) {
            if (std::ranges::any_of(*nodes, [&](const IrNode& child) { return self(self, child); })) return true;
        }
        return false;
    };
    const bool used = marker != no_symbol && std::ranges::any_of(program.bodies, [&](const IrBody& body) {
        return std::ranges::any_of(body.instructions, [&](const IrNode& node) { return makes_pointer(makes_pointer, node); });
    });
    const auto marker_type = used ? program.symbols[marker].type : IrType{};
    const auto guard = [&](const auto& self, std::vector<IrNode>& region, SymbolId owner, bool in_codec) -> void {
        for (auto& test : region) {
            // Prerelease artifacts could already contain the codec guard.
            if (in_codec && test.op == IrOp::ScopedValue && test.body.size() == 2 &&
                test.body.front().op == IrOp::Initialize) {
                const auto held = test.body.front().symbol;
                if (held < program.symbols.size() && program.symbols[held].name == "$native_object") continue;
            }
            self(self, test.operands, owner, in_codec);
            self(self, test.body, owner, in_codec);
            self(self, test.alternative, owner, in_codec);
            const bool generic = test.op == IrOp::ValueText && test.text == native_argument_packing;
            if (generic) {
                test.text.clear();
                if (!used) continue;
            } else {
                if (!in_codec || test.op != IrOp::ClassIs || test.type_arguments.size() != 1 ||
                    test.operands.size() != 1) continue;
                const auto target = test.type_arguments.front().symbol;
                if (target >= program.symbols.size() || program.symbols[target].name != "MemorySegment" ||
                    !native_declaration(program, target)) continue;
            }

            auto receiver = std::move(test.operands.front());
            IrSymbol local{};
            local.kind = SymbolKind::Local;
            local.name = "$native_object";
            local.span = receiver.span;
            local.owner = owner;
            local.type = receiver.type;
            local.constant = true;
            const auto hold = static_cast<SymbolId>(program.symbols.size());
            program.symbols.push_back(std::move(local));

            auto save = node_at(IrOp::Initialize, {}, receiver.span);
            save.symbol = hold;
            auto load = node_at(IrOp::Load, receiver.type, receiver.span);
            load.symbol = hold;
            save.operands.push_back(std::move(receiver));

            const IrType boolean{TypeKind::Bool, "bool", no_symbol, {}};
            auto pointer_test = node_at(IrOp::ClassIs, boolean, test.span);
            pointer_test.type_arguments.push_back(marker_type);
            pointer_test.operands.push_back(load);
            auto rejected = generic ? native_argument_failure(test.type, test.span)
                                    : node_at(IrOp::Boolean, boolean, test.span);
            if (!generic) rejected.text = "false";

            auto selected = node_at(IrOp::SelectValue, test.type, test.span);
            selected.operands.push_back(std::move(pointer_test));
            selected.body.push_back(std::move(rejected));
            test.operands.front() = std::move(load);
            selected.alternative.push_back(std::move(test));

            auto checked = node_at(IrOp::ScopedValue, selected.type, selected.span);
            checked.body.push_back(std::move(save));
            checked.body.push_back(std::move(selected));
            test = std::move(checked);
        }
    };
    for (auto& body : program.bodies) {
        const bool in_codec = used && codec != no_symbol && source_file(program, body.owner) == codec;
        guard(guard, body.instructions, body.owner, in_codec);
    }
}

} // namespace

void specialize(IrProgram& program, const SpecializationLimits& limits) {
    erase_laws(program);
    // Std.Reflect (docs/design/Reflect030.md): calls of the trusted
    // intrinsics become Reflect nodes, which specialization makes concrete.
    const bool reflects = prepare_reflection(program);
    Specializer(program, limits).run();
    guard_managed_pointers_in_native_packing(program);
    // typeof (docs/design/Expressions030.md) needs the concrete types and
    // the program's classes, which exist only now.
    expand_type_of(program);
    if (reflects) expand_reflection(program);
    // Polls are a property of the whole program, and the isolation rules of
    // Concurrency.md section 5 need the concrete types that exist only now:
    // a generic function is checked at each specialization.
    insert_polls(program);
    // #Pure on a generic function holds when each specialization is pure:
    // a call through a type parameter has its targets only now. The whole
    // program is checked, so a function whose callee is a specialization, or
    // an implementation in another module, is checked with it too.
    check_pure_tags(program, {}, true);
    check_render_factories(program, {}, true);
    // Synchronized methods take their receiver's guard only in a program that
    // spawns; the guard operations are then part of what the isolation
    // checks see.
    lower_synchronized(program);
    // Hooked programs get their implicit calls during hook elaboration.
    if (!has_class_hooks(program)) check_task_safety(program);
}

} // namespace neura
