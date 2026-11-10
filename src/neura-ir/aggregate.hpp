#pragma once

#include "ir.hpp"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>

namespace neura {

inline bool has_enum_payload(const IrProgram& program, SymbolId enumeration) {
    return std::ranges::any_of(program.symbols, [enumeration](const IrSymbol& symbol) {
        return symbol.kind == SymbolKind::Variant && symbol.owner == enumeration && !symbol.payload_types.empty();
    });
}

// Whether `equals`, `hash_code`, and `to_string` are built in for a type
// (Types.md): scalars, strings, fieldless enums, class references, and
// tuples, arrays, and lists of such types. Enums with payloads and function
// values have none.
inline bool supports_value_members(const IrProgram& program, const IrType& type) {
    switch (type.kind) {
    case TypeKind::Void:
    case TypeKind::Module:
    case TypeKind::Callable:
        return false;
    case TypeKind::Enum:
        return !has_enum_payload(program, type.symbol);
    case TypeKind::Array:
    case TypeKind::List:
    case TypeKind::Tuple:
        return std::ranges::all_of(type.arguments, [&](const IrType& part) {
            return supports_value_members(program, part);
        });
    default:
        return true;
    }
}

inline std::size_t enum_ordinal(const IrProgram& program, SymbolId variant) {
    const auto owner = program.symbols[variant].owner;
    std::size_t ordinal = 0;
    for (SymbolId id = 0; id < variant; ++id) {
        const auto& symbol = program.symbols[id];
        if (symbol.kind == SymbolKind::Variant && symbol.owner == owner) ++ordinal;
    }
    return ordinal;
}

inline std::optional<std::size_t> aggregate_index(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t result = 0;
    const auto end = text.data() + text.size();
    const auto [next, error] = std::from_chars(text.data(), end, result);
    if (error != std::errc{} || next != end) return std::nullopt;
    return result;
}

// Named constructor operands follow source order. The comma-separated
// indices give their destination fields; an empty string means identity.
inline std::optional<std::vector<std::size_t>> enum_field_order(std::string_view text, std::size_t count) {
    std::vector<std::size_t> order;
    order.reserve(count);
    if (text.empty()) {
        for (std::size_t i = 0; i < count; ++i) order.push_back(i);
        return order;
    }
    std::vector<bool> seen(count);
    for (;;) {
        const auto comma = text.find(',');
        const auto index = aggregate_index(text.substr(0, comma));
        if (!index || *index >= count || seen[*index]) return std::nullopt;
        seen[*index] = true;
        order.push_back(*index);
        if (comma == std::string_view::npos) break;
        text.remove_prefix(comma + 1);
    }
    if (order.size() != count) return std::nullopt;
    return order;
}

} // namespace neura
