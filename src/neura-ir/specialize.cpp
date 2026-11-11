#include "specialize.hpp"

#include "attributes.hpp"

#include "monitors.hpp"
#include "tags.hpp"
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

class Specializer {
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
    bool in_template(SymbolId id) const {
        for (auto scope = id; scope != no_symbol && scope < original_symbols_;
             scope = program_.symbols[scope].owner) {
            const auto& item = program_.symbols[scope];
            if (!item.generics.empty() || item.kind == SymbolKind::TypeParameter) {
                return true;
            }
            // Class static members are shared across all instances and
            // cannot refer to the containing class's type parameters.
            if (is_static_scope(item)) return false;
        }
        return false;
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
            if (context.instance != no_symbol && owned_by_generic_class(item)) item = member_of(item, context.instance);
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
        // Every eager member exists now, so links between members resolve.
        specialize_attributes(instance, inner);
        std::vector<SymbolId> copies;
        for (const auto& [member, copy] : member_map_[instance]) copies.push_back(copy);
        for (const auto copy : copies) {
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
            return copy;
        }
        return member;
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
            if (node.op == IrOp::SchedulerCall) {
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

} // namespace

void specialize(IrProgram& program, const SpecializationLimits& limits) {
    Specializer(program, limits).run();
    // Polls are a property of the whole program, and the isolation rules of
    // Concurrency.md section 5 need the concrete types that exist only now:
    // a generic function is checked at each specialization.
    insert_polls(program);
    // Synchronized methods take their receiver's guard only in a program that
    // spawns; the guard operations are then part of what the isolation
    // checks see.
    lower_synchronized(program);
    const bool thread_safe_tags = std::ranges::any_of(program.symbols, [](const IrSymbol& item) {
        return has_tag(item, TagId::ThreadSafe);
    });
    if (uses_tasks(program) || thread_safe_tags) {
        const TaskAnalysis analysis(program);
        analysis.check_tags();
        if (uses_tasks(program)) analysis.check();
    }
}

} // namespace neura
