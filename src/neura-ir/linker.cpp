#include "linker.hpp"

#include "host_binding.hpp"
#include "verifier.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <tuple>

namespace neura {
namespace {

using FileKey = std::pair<bool, std::string>;
using ChildKey = std::tuple<SymbolKind, std::string, std::size_t, std::size_t, std::size_t, std::size_t>;

SymbolId index_number(std::string_view text) {
    SymbolId result = no_symbol;
    const auto end = text.data() + text.size();
    const auto [next, error] = std::from_chars(text.data(), end, result);
    return error == std::errc{} && next == end ? result : no_symbol;
}

std::string branch_name(std::string_view name, std::string_view group) {
    const auto suffix = name.rfind('$');
    const auto prefix = name.rfind('$', suffix - 1);
    return std::string(name.substr(0, prefix + 1)) + std::string(group) + std::string(name.substr(suffix));
}

bool closure_name(const IrSymbol& symbol) {
    return symbol.kind == SymbolKind::Class && symbol.name.starts_with("$closure") &&
        index_number(std::string_view(symbol.name).substr(8)) != no_symbol;
}

bool cast_name(const IrSymbol& symbol) {
    return symbol.kind == SymbolKind::Function && symbol.name.starts_with("$as$") &&
        index_number(std::string_view(symbol.name).substr(4)) != no_symbol;
}

std::string stable_name(const IrSymbol& symbol) {
    if (closure_name(symbol)) return "$closure";
    if (cast_name(symbol)) return "$as";
    if (symbol.kind == SymbolKind::Function && legacy_overload_group(symbol.name) != no_symbol)
        return branch_name(symbol.name, "");
    return symbol.name;
}

ChildKey child_key(const IrSymbol& symbol) {
    const auto& span = symbol.span;
    return {symbol.kind, stable_name(symbol), span.offset, span.length, span.line, span.column};
}

FileKey file_key(const IrSymbol& file) {
    return file.library_path.empty() ? FileKey{false, file.name} : FileKey{true, file.library_path};
}

struct Index {
    std::map<FileKey, SymbolId> files;
    std::map<std::string_view, SymbolId> file_names;
    std::vector<std::vector<SymbolId>> children;
    SymbolId umbrella = no_symbol;
};

Index index(const IrProgram& program, const Span& site) {
    Index result;
    result.children.resize(program.symbols.size());
    std::vector<bool> defined(program.symbols.size());
    for (const auto& body : program.bodies) {
        if (body.owner >= defined.size()) throw Diagnostic(site, "invalid body owner in .nir");
        defined[body.owner] = true;
    }
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& symbol = program.symbols[id];
        if (symbol.kind == SymbolKind::File) {
            if (symbol.owner != no_symbol || !result.file_names.emplace(symbol.name, id).second ||
                !result.files.emplace(file_key(symbol), id).second)
                throw Diagnostic(site, "invalid or repeated file identity '" + file_key(symbol).second + "' in .nir");
        } else if (symbol.owner < result.children.size()) {
            result.children[symbol.owner].push_back(id);
        } else if (symbol.owner == no_symbol && symbol.kind == SymbolKind::Class && symbol.name == "$class" &&
                   result.umbrella == no_symbol) {
            result.umbrella = id;
        } else {
            throw Diagnostic(site, "invalid owner of '" + symbol.name + "' in .nir");
        }
        const auto correct_type = [&](TypeKind kind) {
            return symbol.type.kind == kind && symbol.type.symbol == id;
        };
        const auto is_nominal = symbol.kind == SymbolKind::Class || symbol.kind == SymbolKind::Interface;
        if ((symbol.kind == SymbolKind::Module && !correct_type(TypeKind::Module)) ||
            (is_nominal && !correct_type(TypeKind::Object)) ||
            (symbol.kind == SymbolKind::Enum && !correct_type(TypeKind::Enum))) {
            throw Diagnostic(site, "invalid type of '" + symbol.name + "' in .nir");
        }
        if (symbol.kind == SymbolKind::Import &&
            (symbol.type.kind != TypeKind::Module || symbol.type.symbol >= program.symbols.size() ||
             program.symbols[symbol.type.symbol].kind != SymbolKind::Module)) {
            throw Diagnostic(site, "invalid imported module '" + symbol.name + "' in .nir");
        }
        if (symbol.kind == SymbolKind::Alias && symbol.type.kind == TypeKind::Object &&
            (symbol.type.symbol >= program.symbols.size() ||
             (program.symbols[symbol.type.symbol].kind != SymbolKind::Class &&
              program.symbols[symbol.type.symbol].kind != SymbolKind::Interface))) {
            throw Diagnostic(site, "invalid type alias '" + symbol.name + "' in .nir");
        }
        if (symbol.owner < program.symbols.size() && program.symbols[symbol.owner].kind == SymbolKind::Class &&
            (symbol.name == "init" || symbol.name == "deinit") &&
            (symbol.kind != SymbolKind::Function || symbol.type.kind != TypeKind::Void ||
             symbol.external || !symbol.has_body || !symbol.generics.empty() ||
             (symbol.name == "deinit" && !symbol.parameters.empty()))) {
            throw Diagnostic(site, "invalid lifecycle declaration '" + symbol.name + "' in .nir");
        }
        if (symbol.kind == SymbolKind::Function && symbol.has_body && !defined[id])
            throw Diagnostic(site, "missing body of '" + symbol.name + "' in .nir");
    }

    for (const auto& symbol : program.symbols) {
        if (symbol.overload_group != no_symbol) {
            const auto encoded = legacy_overload_group(symbol.name);
            if (encoded != no_symbol && encoded != symbol.overload_group)
                throw Diagnostic(site, "inconsistent overload group name in .nir");
        }
        if (symbol.base == no_symbol) continue;
        if (symbol.base >= program.symbols.size())
            throw Diagnostic(site, "invalid base symbol in .nir");
        const auto kind = program.symbols[symbol.base].kind;
        if (!((symbol.kind == SymbolKind::Module && kind == SymbolKind::Module) ||
              (symbol.kind == SymbolKind::Class && kind == SymbolKind::Class) ||
              (symbol.kind == SymbolKind::Interface && symbol.base == result.umbrella))) {
            throw Diagnostic(site, "invalid base of '" + symbol.name + "' in .nir");
        }
    }
    // Lowering and conversion walk these chains without cycle checks.
    for (const auto member : {&IrSymbol::owner, &IrSymbol::base, &IrSymbol::overrides}) {
        enum class State { Unseen, Visiting, Done };
        std::vector<State> state(program.symbols.size(), State::Unseen);
        for (SymbolId id = 0; id < state.size(); ++id) {
            auto cursor = id;
            while (cursor != no_symbol) {
                if (cursor >= state.size()) throw Diagnostic(site, "invalid symbol chain in .nir");
                if (state[cursor] == State::Done) break;
                if (state[cursor] == State::Visiting)
                    throw Diagnostic(site, "cyclic owner, base, or override chain in .nir");
                state[cursor] = State::Visiting;
                cursor = program.symbols[cursor].*member;
            }
            for (cursor = id; cursor != no_symbol && state[cursor] == State::Visiting;
                 cursor = program.symbols[cursor].*member)
                state[cursor] = State::Done;
        }
    }
    return result;
}

[[noreturn]] void conflict(const Span& site, std::string_view file, std::string_view item) {
    throw Diagnostic(site, "incompatible definitions of '" + std::string(file) +
                           "' in linked NeuraIR (at '" + std::string(item) + "')");
}

void pair_symbols(const IrProgram& destination, const Index& indexed, const IrProgram& incoming,
                  const Index& imported, SymbolId previous, SymbolId current, std::string_view file,
                  const Span& site, std::vector<SymbolId>& mapping) {
    std::vector<std::pair<SymbolId, SymbolId>> pending{{previous, current}};
    while (!pending.empty()) {
        const auto [old_id, new_id] = pending.back();
        pending.pop_back();
        mapping[new_id] = old_id;
        const auto& old_children = indexed.children[old_id];
        const auto& new_children = imported.children[new_id];
        if (old_children.size() != new_children.size()) conflict(site, file, incoming.symbols[new_id].name);

        // Spans distinguish same-named locals, constructor branches, and
        // generated declarations. Occurrence order handles identical keys.
        std::map<ChildKey, std::vector<SymbolId>> by_key;
        for (auto it = old_children.rbegin(); it != old_children.rend(); ++it)
            by_key[child_key(destination.symbols[*it])].push_back(*it);
        std::vector<SymbolId> incoming_layout;
        for (const auto child : new_children) {
            const auto& symbol = incoming.symbols[child];
            auto& matches = by_key[child_key(symbol)];
            if (matches.empty()) conflict(site, file, symbol.name);
            const auto match = matches.back();
            matches.pop_back();
            pending.emplace_back(match, child);
            if (symbol.kind == SymbolKind::Field || symbol.kind == SymbolKind::Variant)
                incoming_layout.push_back(match);
        }
        std::vector<SymbolId> existing_layout;
        for (const auto child : old_children) {
            const auto kind = destination.symbols[child].kind;
            if (kind == SymbolKind::Field || kind == SymbolKind::Variant) existing_layout.push_back(child);
        }
        if (incoming_layout != existing_layout) conflict(site, file, incoming.symbols[new_id].name);
    }
}

bool same_span(const Span& a, const Span& b) {
    return std::tie(a.file, a.line, a.column, a.offset, a.length) ==
        std::tie(b.file, b.line, b.column, b.offset, b.length);
}

bool same_node(const IrNode& a, const IrNode& b) {
    return std::tie(a.op, a.type, a.symbol, a.text, a.type_arguments) ==
            std::tie(b.op, b.type, b.symbol, b.text, b.type_arguments) &&
        same_span(a.span, b.span) &&
        std::ranges::equal(a.operands, b.operands, same_node) &&
        std::ranges::equal(a.body, b.body, same_node) &&
        std::ranges::equal(a.alternative, b.alternative, same_node);
}

bool same_host(const std::optional<IrHostBinding>& a, const std::optional<IrHostBinding>& b) {
    return a.has_value() == b.has_value() &&
        (!a || std::tie(a->name, a->version) == std::tie(b->name, b->version));
}

bool same_symbol(const IrSymbol& a, const IrSymbol& b, bool legacy_signatures) {
    const auto signature = [&](auto current, auto imported, auto absent) {
        return current == imported || (legacy_signatures && imported == absent);
    };
    return std::tie(a.kind, a.name, a.owner, a.base, a.overrides, a.type, a.parameters, a.generics, a.bound,
                    a.exported, a.constant, a.external, a.has_body, a.payload_types, a.library_path,
                    a.interfaces, a.additional_bounds) ==
        std::tie(b.kind, b.name, b.owner, b.base, b.overrides, b.type, b.parameters, b.generics, b.bound,
                 b.exported, b.constant, b.external, b.has_body, b.payload_types, b.library_path,
                 b.interfaces, b.additional_bounds) &&
        same_span(a.span, b.span) && same_host(a.host_binding, b.host_binding) &&
        signature(a.overload_group, b.overload_group, no_symbol) &&
        signature(a.variadic, b.variadic, VariadicKind::None) &&
        signature(a.user_cast, b.user_cast, false) &&
        signature(a.singleton_instance, b.singleton_instance, no_symbol);
}

class Remapping {
public:
    Remapping(const IrProgram& destination, const IrProgram& incoming,
              const std::vector<SymbolId>& mapping);

    void symbol(IrSymbol& value, SymbolId id) const;
    void node(IrNode& value) const;
    SymbolId reference(SymbolId id) const { return id == no_symbol ? id : mapping_.at(id); }

private:
    const std::vector<SymbolId>& mapping_;
    std::vector<std::string> old_names_;
    std::vector<std::string> new_names_;
    std::map<std::string, std::string, std::less<>> file_names_;

    void span(Span& value) const;
    void type(IrType& value) const;
};

Remapping::Remapping(const IrProgram& destination, const IrProgram& incoming,
                     const std::vector<SymbolId>& mapping)
    : mapping_(mapping) {
    for (SymbolId id = 0; id < incoming.symbols.size(); ++id) {
        const auto& symbol = incoming.symbols[id];
        const auto mapped = reference(id);
        auto name = symbol.name;
        if (mapped < destination.symbols.size()) {
            name = destination.symbols[mapped].name;
        } else if (closure_name(symbol)) {
            name = "$closure" + std::to_string(mapped);
        } else if (cast_name(symbol)) {
            name = "$as$" + std::to_string(mapped);
        } else if (symbol.kind == SymbolKind::Function) {
            const auto group = legacy_overload_group(symbol.name);
            if (group < mapping.size()) name = branch_name(symbol.name, std::to_string(reference(group)));
        }
        old_names_.push_back(symbol.name);
        new_names_.push_back(std::move(name));
        if (symbol.kind == SymbolKind::File && symbol.name != new_names_.back())
            file_names_.emplace(symbol.name, new_names_.back());
    }
}

void Remapping::span(Span& value) const {
    if (const auto found = file_names_.find(value.file); found != file_names_.end()) value.file = found->second;
}

void Remapping::type(IrType& value) const {
    if (value.symbol != no_symbol && value.name == old_names_.at(value.symbol))
        value.name = new_names_[value.symbol];
    value.symbol = reference(value.symbol);
    for (auto& argument : value.arguments) type(argument);
}

void Remapping::symbol(IrSymbol& value, SymbolId id) const {
    value.name = new_names_[id];
    value.owner = reference(value.owner);
    value.base = reference(value.base);
    value.overrides = reference(value.overrides);
    value.overload_group = reference(value.overload_group);
    value.singleton_instance = reference(value.singleton_instance);
    span(value.span);
    type(value.type);
    for (auto& parameter : value.parameters) parameter = reference(parameter);
    for (auto& generic : value.generics) generic = reference(generic);
    if (value.bound) type(*value.bound);
    for (auto* types : {&value.payload_types, &value.interfaces, &value.additional_bounds})
        for (auto& item : *types) type(item);
}

void Remapping::node(IrNode& value) const {
    value.symbol = reference(value.symbol);
    span(value.span);
    type(value.type);
    for (auto& argument : value.type_arguments) type(argument);
    for (auto* region : {&value.operands, &value.body, &value.alternative})
        for (auto& child : *region) node(child);
}

} // namespace

SymbolId legacy_overload_group(std::string_view name) {
    const auto suffix = name.rfind('$');
    if (suffix == std::string_view::npos || suffix == 0) return no_symbol;
    const auto prefix = name.rfind('$', suffix - 1);
    if (prefix == std::string_view::npos || prefix == 0 || index_number(name.substr(suffix + 1)) == no_symbol)
        return no_symbol;
    return index_number(name.substr(prefix + 1, suffix - prefix - 1));
}

std::vector<SymbolId> link_ir(IrProgram& destination, IrProgram incoming, const Span& import_site,
                              bool legacy_signatures) {
    // One image holds one native library: an imported artifact may bring
    // one only when the program has none or the same one.
    if (incoming.native_library) {
        if (destination.native_library && !(*destination.native_library == *incoming.native_library))
            throw Diagnostic(import_site, "the imported NeuraIR artifact links a different native library; "
                                          "an image holds one native library");
        destination.native_library = incoming.native_library;
    }
    for (auto& unit : incoming.native_units) destination.native_units.push_back(std::move(unit));
    const auto imported = index(incoming, import_site);
    if (const auto issues = verify_ir(incoming); !issues.empty())
        throw Diagnostic(import_site, "invalid imported NeuraIR: " + issues.front().message);
    const auto indexed = index(destination, import_site);
    std::vector<SymbolId> mapping(incoming.symbols.size(), no_symbol);
    for (const auto& [key, current] : imported.files) {
        const auto& file = incoming.symbols[current];
        if (const auto existing_name = indexed.file_names.find(file.name);
            existing_name != indexed.file_names.end() && file_key(destination.symbols[existing_name->second]) != key) {
            conflict(import_site, file.name, "file identity");
        }
        if (const auto previous = indexed.files.find(key); previous != indexed.files.end()) {
            pair_symbols(destination, indexed, incoming, imported, previous->second, current,
                         key.second, import_site, mapping);
        }
    }
    if (indexed.umbrella != no_symbol && imported.umbrella != no_symbol)
        pair_symbols(destination, indexed, incoming, imported, indexed.umbrella, imported.umbrella,
                     "<class>", import_site, mapping);

    const auto existing = destination.symbols.size();
    const auto trusted_metadata = [&](SymbolId id) {
        if (!legacy_signatures || id >= existing) return false;
        const auto file = source_file(destination, id);
        return file != no_symbol && !destination.symbols[file].library_path.empty();
    };
    auto next = existing;
    for (auto& id : mapping)
        if (id == no_symbol) id = next++;

    const Remapping remapping(destination, incoming, mapping);
    for (SymbolId id = 0; id < incoming.symbols.size(); ++id) {
        auto& symbol = incoming.symbols[id];
        const auto mapped = mapping[id];
        remapping.symbol(symbol, id);
        // For a coalesced symbol the chosen name is the existing one. Its
        // original name and scope were checked when pairing the definitions.
        if (mapped < existing) {
            const auto& previous = destination.symbols[mapped];
            if (!same_symbol(previous, symbol, trusted_metadata(mapped)))
                conflict(import_site, previous.span.file, previous.name);
        }
    }

    std::map<SymbolId, const IrBody*> old_bodies;
    for (const auto& body : destination.bodies) old_bodies.emplace(body.owner, &body);
    for (auto& body : incoming.bodies) {
        body.owner = remapping.reference(body.owner);
        for (auto& node : body.instructions) remapping.node(node);
        if (body.owner >= existing) continue;
        const auto previous = old_bodies.find(body.owner);
        if (previous == old_bodies.end() ||
            !std::ranges::equal(previous->second->instructions, body.instructions, same_node)) {
            const auto& symbol = destination.symbols[body.owner];
            conflict(import_site, symbol.span.file, symbol.name);
        }
        old_bodies.erase(previous);
    }
    std::map<SymbolId, const IrNode*> old_constants;
    for (const auto& entry : destination.static_constants) old_constants.emplace(entry.field, &entry.value);
    for (auto& entry : incoming.static_constants) {
        entry.field = remapping.reference(entry.field);
        remapping.node(entry.value);
        if (entry.field >= existing) continue;
        const auto previous = old_constants.find(entry.field);
        if (previous == old_constants.end() || !same_node(*previous->second, entry.value)) {
            const auto& symbol = destination.symbols[entry.field];
            conflict(import_site, symbol.span.file, symbol.name);
        }
        old_constants.erase(previous);
    }
    for (const auto id : mapping) {
        if (old_bodies.contains(id) || (old_constants.contains(id) && !trusted_metadata(id))) {
            const auto& symbol = destination.symbols[id];
            conflict(import_site, symbol.span.file, symbol.name);
        }
    }

    destination.symbols.reserve(next);
    destination.bodies.reserve(destination.bodies.size() + incoming.bodies.size());
    destination.static_constants.reserve(destination.static_constants.size() + incoming.static_constants.size());
    for (SymbolId id = 0; id < incoming.symbols.size(); ++id)
        if (mapping[id] >= existing) destination.symbols.push_back(std::move(incoming.symbols[id]));
    for (auto& body : incoming.bodies)
        if (body.owner >= existing) destination.bodies.push_back(std::move(body));
    for (auto& entry : incoming.static_constants)
        if (entry.field >= existing) destination.static_constants.push_back(std::move(entry));
    return mapping;
}

} // namespace neura
