#pragma once

#include "ir.hpp"

#include <algorithm>
#include <optional>
#include <string_view>

namespace neura {

inline bool host_data_type(const IrType& type) {
    switch (type.kind) {
    case TypeKind::Bool:
    case TypeKind::String:
    case TypeKind::Integer:
    case TypeKind::Float:
        return true;
    case TypeKind::Array:
    case TypeKind::List:
        return type.arguments.size() == 1 && host_data_type(type.arguments.front());
    case TypeKind::Tuple:
        return !type.arguments.empty() && std::ranges::all_of(type.arguments, host_data_type);
    default:
        return false;
    }
}

inline bool host_service_name(std::string_view name) {
    return !name.empty() && name.find('.') != std::string_view::npos && name.front() != '.' &&
           name.back() != '.' && name.find("..") == std::string_view::npos &&
           std::ranges::all_of(name, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
           });
}

struct HostVariants {
    SymbolId success = no_symbol;
    SymbolId error = no_symbol;
};

inline std::optional<HostVariants> host_variants(const IrProgram& program, const IrType& result) {
    if (result.kind != TypeKind::Enum || result.symbol >= program.symbols.size()) return std::nullopt;
    HostVariants variants;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& variant = program.symbols[id];
        if (variant.kind != SymbolKind::Variant || variant.owner != result.symbol) continue;
        const auto& payload = variant.payload_types;
        if (variant.name == "Ok" && variants.success == no_symbol &&
            (payload.empty() || (payload.size() == 1 && host_data_type(payload.front())))) {
            variants.success = id;
        } else if (variant.name == "Error" && variants.error == no_symbol &&
                   payload.size() == 1 && payload.front() == IrType{TypeKind::Integer, "uint32", no_symbol, {}}) {
            variants.error = id;
        } else {
            return std::nullopt;
        }
    }
    if (variants.success == no_symbol || variants.error == no_symbol) return std::nullopt;
    return variants;
}

inline SymbolId source_file(const IrProgram& program, SymbolId symbol) {
    for (std::size_t count = 0; symbol < program.symbols.size() && count < program.symbols.size(); ++count) {
        const auto& item = program.symbols[symbol];
        if (item.kind == SymbolKind::File) return symbol;
        symbol = item.owner;
    }
    return no_symbol;
}

} // namespace neura
