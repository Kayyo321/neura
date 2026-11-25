#pragma once

#include "ir.hpp"
#include "host_binding.hpp"

#include <algorithm>
#include <string_view>

namespace neura {

enum class PointerKind { None, Mutable, Const, List, Unique, Shared, Weak, Soft, Atomic, Locked };

// The source name of each pointer class, for diagnostics.
inline std::string_view pointer_kind_name(PointerKind kind) {
    switch (kind) {
    case PointerKind::Mutable: return "Pointer";
    case PointerKind::Const: return "ConstPointer";
    case PointerKind::List: return "ListPointer";
    case PointerKind::Unique: return "UniquePointer";
    case PointerKind::Shared: return "SharedPointer";
    case PointerKind::Weak: return "WeakPointer";
    case PointerKind::Soft: return "SoftPointer";
    case PointerKind::Atomic: return "AtomicPointer";
    case PointerKind::Locked: return "LockedPointer";
    case PointerKind::None: break;
    }
    return {};
}

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
    if (pointer_class_name(cls, "UniquePointer")) return PointerKind::Unique;
    if (pointer_class_name(cls, "SharedPointer")) return PointerKind::Shared;
    if (pointer_class_name(cls, "WeakPointer")) return PointerKind::Weak;
    if (pointer_class_name(cls, "SoftPointer")) return PointerKind::Soft;
    if (pointer_class_name(cls, "AtomicPointer")) return PointerKind::Atomic;
    if (pointer_class_name(cls, "LockedPointer")) return PointerKind::Locked;
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

// The pointee of a specialized pointer class: its type argument, which
// specialization records because the class type no longer carries it.
// Null before specialization and for any other class.
inline const IrType* pointer_value_type(const IrProgram& program, SymbolId pointer) {
    for (const auto& [instance, pointee] : program.pointer_pointees)
        if (instance == pointer) return &pointee;
    return nullptr;
}

} // namespace neura
