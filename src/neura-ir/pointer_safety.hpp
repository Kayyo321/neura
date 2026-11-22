#pragma once

#include "ir.hpp"
#include "host_binding.hpp"

#include <algorithm>
#include <string_view>

namespace neura {

enum class PointerKind { None, Mutable, Const, List };

// Only declarations descended from the trusted standard library file
// have pointer-specific construction, storage, or transfer rules.
inline bool trusted_pointer_declaration(const IrProgram& program, SymbolId id) {
    for (; id != no_symbol && id < program.symbols.size(); id = program.symbols[id].owner) {
        const auto& symbol = program.symbols[id];
        if (symbol.kind == SymbolKind::File) return symbol.library_path == "std/core/pointer.nra";
    }
    return false;
}

inline bool pointer_class_name(const IrSymbol& cls, std::string_view name) {
    return cls.name == name || (cls.name.size() > name.size() && cls.name.starts_with(name) &&
                               cls.name[name.size()] == '<');
}

inline PointerKind pointer_class(const IrProgram& program, SymbolId id) {
    if (id == no_symbol || id >= program.symbols.size()) return PointerKind::None;
    const auto& cls = program.symbols[id];
    if (cls.kind != SymbolKind::Class || cls.owner >= program.symbols.size()) return PointerKind::None;
    const auto& module = program.symbols[cls.owner];
    if (module.kind != SymbolKind::Module || module.name != "Pointer" || !trusted_pointer_declaration(program, id))
        return PointerKind::None;
    if (pointer_class_name(cls, "Pointer")) return PointerKind::Mutable;
    if (pointer_class_name(cls, "ConstPointer")) return PointerKind::Const;
    if (pointer_class_name(cls, "ListPointer")) return PointerKind::List;
    return PointerKind::None;
}

inline bool cell_class(const IrProgram& program, SymbolId id) {
    if (id == no_symbol || id >= program.symbols.size()) return false;
    const auto& cls = program.symbols[id];
    if (cls.kind != SymbolKind::Class || !pointer_class_name(cls, "Cell") ||
        cls.owner >= program.symbols.size())
        return false;
    const auto& module = program.symbols[cls.owner];
    return module.kind == SymbolKind::Module && module.name == "Cell" && module.owner < program.symbols.size() &&
           program.symbols[module.owner].kind == SymbolKind::File &&
           program.symbols[module.owner].library_path == "std/core/cell.nra";
}

inline bool contains_pointer_type(const IrProgram& program, const IrType& type) {
    return (type.kind == TypeKind::Object && pointer_class(program, type.symbol) != PointerKind::None) ||
           std::ranges::any_of(type.arguments, [&](const auto& argument) { return contains_pointer_type(program, argument); });
}

inline constexpr std::string_view native_argument_packing = "$native_argument";

inline bool native_declaration(const IrProgram& program, SymbolId id) {
    const auto file = source_file(program, id);
    return file != no_symbol && program.symbols[file].library_path.starts_with("std/dynamic-foreign/");
}

inline bool contains_native_type(const IrProgram& program, const IrType& type) {
    return (type.symbol != no_symbol && native_declaration(program, type.symbol)) ||
           std::ranges::any_of(type.arguments, [&](const auto& argument) { return contains_native_type(program, argument); });
}

// After specialization the class type has no source type arguments. For
// ConstPointer the private backing class still gives the exact value type.
inline const IrType* pointer_value_type(const IrProgram& program, SymbolId pointer) {
    for (const auto& field : program.symbols) {
        if (field.kind != SymbolKind::Field || field.owner != pointer || field.name != "backing" ||
            !cell_class(program, field.type.symbol))
            continue;
        if (!field.type.arguments.empty()) return &field.type.arguments.front();
        for (const auto& value : program.symbols) {
            if (value.kind != SymbolKind::Field || value.owner != field.type.symbol || value.name != "value")
                continue;
            if (value.type.kind == TypeKind::Object && value.type.name.starts_with('&')) {
                for (const auto& boxed : program.symbols)
                    if (boxed.kind == SymbolKind::Field && boxed.owner == value.type.symbol && boxed.name == "$value")
                        return &boxed.type;
            }
            return &value.type;
        }
    }
    return nullptr;
}

} // namespace neura
