#include "frozen.hpp"

#include "host_binding.hpp"
#include "tags.hpp"

#include <limits>
#include <set>

namespace neura {

namespace {

bool valid(const IrProgram& program, SymbolId id) { return id < program.symbols.size(); }

std::string quoted(const std::string& name) { return "'" + name + "'"; }

std::string display(const IrType& type) {
    if (type.kind == TypeKind::Array || type.kind == TypeKind::List) {
        return (type.arguments.empty() ? std::string("?") : display(type.arguments.front())) +
               (type.kind == TypeKind::Array ? "[]" : "[list]");
    }
    return type.name.empty() ? std::string("value") : type.name;
}

void type_parameters(const IrType& type, std::set<SymbolId>& out) {
    if (type.kind == TypeKind::TypeParameter && type.symbol != no_symbol) out.insert(type.symbol);
    for (const auto& argument : type.arguments) type_parameters(argument, out);
}

// The libraries that may write #Frozen.
bool frozen_library(const IrProgram& program, SymbolId id) {
    const auto file = source_file(program, id);
    if (!valid(program, file)) return false;
    const auto& path = program.symbols[file].library_path;
    return path == "std/collections/frozen.nra" || path == "std/ui.nra" || path.starts_with("std/ui/");
}

constexpr std::size_t statement = std::numeric_limits<std::size_t>::max();

struct Position {
    const IrNode* parent = nullptr;
    std::size_t index = statement;
};

class StorageUses {
public:
    StorageUses(const IrProgram& program, const IrBody& body) : program_(program), body_(body) {}

    void run() {
        for (const auto& node : body_.instructions) walk(node, {}, {});
    }

private:
    const IrProgram& program_;
    const IrBody& body_;

    bool frozen(const IrNode& node) const {
        return node.op == IrOp::Field && valid(program_, node.symbol) && frozen_field(program_.symbols[node.symbol]);
    }

    // An element read: `storage[i]` that is not assigned, borrowed or referenced.
    static bool element_read(Position at, Position above) {
        if (!at.parent || at.parent->op != IrOp::Index || at.index != 0) return false;
        if (!above.parent) return true;
        if (above.parent->op == IrOp::Store && above.index == 0) return false;
        return above.parent->op != IrOp::Reference && above.parent->op != IrOp::ReferencePlace;
    }

    static bool read(Position at, Position above) {
        if (element_read(at, above)) return true;
        if (!at.parent || at.index != 0) return false;
        switch (at.parent->op) {
        case IrOp::Length:
        case IrOp::ValueText:
        case IrOp::ValueHash:
        case IrOp::ValueEquals:
            return true;
        default:
            return false;
        }
    }

    bool constructor_of(SymbolId field) const {
        if (!valid(program_, body_.owner)) return false;
        const auto& function = program_.symbols[body_.owner];
        return function.kind == SymbolKind::Function && function.name == "init" &&
               function.owner == program_.symbols[field].owner;
    }

    static bool fresh(const IrNode& value) {
        return value.op == IrOp::Collection || value.op == IrOp::RepeatCollection;
    }

    // `local` is initialized with a new list and only filled and read before
    // it is assigned to the field by `assignment`.
    bool filled_local(SymbolId local, const IrNode& assignment) const {
        bool initialized = false;
        bool ok = true;
        const std::function<void(const IrNode&, Position, Position)> visit = [&](const IrNode& node, Position at,
                                                                                Position above) {
            if (node.op == IrOp::Initialize && node.symbol == local) {
                initialized = node.operands.size() == 1 && fresh(node.operands.front());
            }
            if (node.op == IrOp::Load && node.symbol == local) {
                const bool used = (at.parent == &assignment && at.index == 1) ||
                                  (at.parent && at.index == 0 &&
                                   (at.parent->op == IrOp::Append || at.parent->op == IrOp::Length)) ||
                                  element_read(at, above);
                ok = ok && used;
            }
            if ((node.op == IrOp::Reference || node.op == IrOp::ReferencePlace) && node.symbol == local) ok = false;
            for (std::size_t i = 0; i < node.operands.size(); ++i) visit(node.operands[i], {&node, i}, at);
            for (const auto& child : node.body) visit(child, {}, {});
            for (const auto& child : node.alternative) visit(child, {}, {});
        };
        for (const auto& node : body_.instructions) visit(node, {}, {});
        return initialized && ok;
    }

    void walk(const IrNode& node, Position at, Position above) {
        if (frozen(node)) check(node, at, above);
        for (std::size_t i = 0; i < node.operands.size(); ++i) walk(node.operands[i], {&node, i}, at);
        for (const auto& child : node.body) walk(child, {}, {});
        for (const auto& child : node.alternative) walk(child, {}, {});
    }

    void check(const IrNode& node, Position at, Position above) {
        if (read(at, above)) return;
        const auto field = node.symbol;
        if (at.parent && at.parent->op == IrOp::Store && at.index == 0 && at.parent->operands.size() == 2 &&
            constructor_of(field)) {
            const auto& value = at.parent->operands[1];
            if (fresh(value)) return;
            if (value.op == IrOp::Load && valid(program_, value.symbol) &&
                program_.symbols[value.symbol].kind == SymbolKind::Local && filled_local(value.symbol, *at.parent))
                return;
        }
        std::string reason = "the storage escapes here";
        if (at.parent) {
            switch (at.parent->op) {
            case IrOp::Return:
                reason = "returned, so the caller could change it";
                break;
            case IrOp::Store:
                reason = at.index == 0 ? "assigned outside the constructor, or from a list the constructor did not "
                                         "create"
                                       : "stored where other code can reach it";
                break;
            case IrOp::Append:
            case IrOp::RemoveAt:
                reason = "changed after construction";
                break;
            case IrOp::Index:
                reason = "an element is assigned or borrowed";
                break;
            case IrOp::Call:
            case IrOp::VirtualCall:
            case IrOp::CallClosure:
            case IrOp::ExternalCall:
                reason = "passed to a call, which could keep or change it";
                break;
            case IrOp::Initialize:
                reason = "stored in another binding";
                break;
            default:
                break;
            }
        }
        const auto& declared = program_.symbols[field];
        auto error = Diagnostic(node.span, "frozen storage is exposed or changed").with_code("E0581").label(reason);
        if (const auto tag = tag_span(declared, TagId::Frozen)) {
            error.label(*tag, quoted(declared.name) + " is #Frozen here");
        }
        error.note("a #Frozen field may only be read by index and length, after one constructor assignment from a "
                   "list the constructor created");
        error.help("copy the elements instead");
        throw error;
    }
};

} // namespace

bool frozen_field(const IrSymbol& symbol) {
    return symbol.kind == SymbolKind::Field && has_tag(symbol, TagId::Frozen);
}

std::optional<MutablePart> mutable_part(const IrProgram& program, const IrType& type) {
    std::set<SymbolId> visiting;
    const std::function<std::optional<MutablePart>(const IrType&)> find =
        [&](const IrType& item) -> std::optional<MutablePart> {
        switch (item.kind) {
        case TypeKind::Bool:
        case TypeKind::Integer:
        case TypeKind::Float:
        case TypeKind::String:
        case TypeKind::TypeParameter:
            return std::nullopt;
        case TypeKind::Tuple:
            for (const auto& part : item.arguments) {
                if (auto found = find(part)) return found;
            }
            return std::nullopt;
        case TypeKind::Enum: {
            if (!valid(program, item.symbol)) return MutablePart{"an unknown enum", std::nullopt};
            // A generic enum's payloads name its type parameters: check the arguments.
            for (const auto& argument : item.arguments) {
                if (auto found = find(argument)) return found;
            }
            if (!visiting.insert(item.symbol).second) return std::nullopt;
            for (const auto& variant : program.symbols) {
                if (variant.kind != SymbolKind::Variant || variant.owner != item.symbol) continue;
                for (const auto& payload : variant.payload_types) {
                    if (auto found = find(payload)) return found;
                }
            }
            return std::nullopt;
        }
        case TypeKind::Object: {
            if (!valid(program, item.symbol)) return MutablePart{"an unknown class", std::nullopt};
            const auto& cls = program.symbols[item.symbol];
            if (cls.kind == SymbolKind::Interface) {
                return MutablePart{"interface " + quoted(cls.name) + " may be implemented by a mutable class",
                                   cls.span};
            }
            if (cls.kind != SymbolKind::Class || !has_tag(cls, TagId::Immutable)) {
                return MutablePart{"class " + quoted(cls.name) + " is not #Immutable", cls.span};
            }
            for (const auto& argument : item.arguments) {
                if (auto found = find(argument)) return found;
            }
            if (!visiting.insert(item.symbol).second) return std::nullopt;
            std::set<SymbolId> chain;
            for (auto scope = item.symbol; valid(program, scope) && chain.insert(scope).second;
                 scope = program.symbols[scope].base) {}
            for (const auto& field : program.symbols) {
                if (field.kind != SymbolKind::Field || !chain.contains(field.owner)) continue;
                if (!field.constant) {
                    return MutablePart{"field " + quoted(field.name) + " of " + quoted(cls.name) +
                                           " can be assigned after construction",
                                       field.span};
                }
                if (frozen_field(field) && field.type.kind == TypeKind::List && field.type.arguments.size() == 1) {
                    if (auto found = find(field.type.arguments.front())) return found;
                    continue;
                }
                if (auto found = find(field.type)) return found;
            }
            return std::nullopt;
        }
        case TypeKind::List:
            return MutablePart{display(item) + " is a list, which can change after it is created", std::nullopt};
        case TypeKind::Array:
            return MutablePart{display(item) + " is an array, whose elements can be assigned", std::nullopt};
        case TypeKind::Callable:
            return MutablePart{"a function value can reach mutable state", std::nullopt};
        default:
            return MutablePart{display(item) + " is not deeply immutable", std::nullopt};
        }
    };
    return find(type);
}

void check_frozen_arguments(const IrProgram& program, const std::vector<WrittenTypeArgument>& written) {
    std::set<SymbolId> required;
    for (const auto& item : program.symbols) {
        if (item.kind == SymbolKind::Class && has_tag(item, TagId::Frozen)) {
            required.insert(item.generics.begin(), item.generics.end());
        }
    }
    if (required.empty()) return;
    // A type parameter given as a required argument is required in turn.
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& item : written) {
            if (!required.contains(item.parameter)) continue;
            std::set<SymbolId> inner;
            type_parameters(item.actual, inner);
            for (const auto id : inner) changed = required.insert(id).second || changed;
        }
    }
    for (const auto& item : written) {
        if (!required.contains(item.parameter)) continue;
        const auto part = mutable_part(program, item.actual);
        if (!part) continue;
        const auto& parameter = program.symbols[item.parameter];
        auto error = Diagnostic(item.span, "type " + display(item.actual) + " is not deeply immutable")
            .with_code("E0580").label("given for " + quoted(parameter.name) + ", which must be deeply immutable");
        if (part->span) {
            error.label(*part->span, part->text);
        } else {
            error.note(part->text);
        }
        error.note("deeply immutable types are bool, numbers, string, tuples and enums of such types, #Immutable "
                   "classes whose fields are too, and FrozenList of them");
        error.help("make the class #Immutable, or replace a list with a FrozenList");
        throw error;
    }
}

void check_frozen_storage(const IrProgram& program, SymbolId first, std::size_t first_body,
                          const std::function<Span(SymbolId)>& name_span) {
    for (SymbolId id = first; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (!has_tag(item, TagId::Frozen)) continue;
        const auto tag = tag_span(item, TagId::Frozen).value_or(name_span(id));
        if (!frozen_library(program, id)) {
            throw Diagnostic(tag, "#Frozen is reserved for the standard library").with_code("E0581")
                .label("used outside Std.Collections.Frozen and Std.Ui")
                .help("use frozen.FrozenList, which holds its own frozen storage");
        }
        if (item.kind == SymbolKind::Field && (!item.constant || item.type.kind != TypeKind::List)) {
            throw Diagnostic(name_span(id), "frozen storage must be a last list field").with_code("E0581")
                .label(item.constant ? "not a list" : "declared with let, so it can be assigned again")
                .label(tag, "#Frozen given here");
        }
    }
    for (std::size_t index = first_body; index < program.bodies.size(); ++index) {
        StorageUses(program, program.bodies[index]).run();
    }
}

} // namespace neura
