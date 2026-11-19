#pragma once

#include "ir.hpp"

#include <cstdint>
#include <optional>

// The inline type `any` (docs/design/Matching030.md, "any"): one value of a
// fixed set of types together with which type it is. The tags are fixed
// numbers, the same in every module, and an `any` of all zero words is nil.

namespace neura {

// Tag 0 is a class or interface reference, or nil.
inline constexpr std::uint32_t any_reference_tag = 0;

// The tag of a value of `type` held in an `any`, or none when an `any`
// cannot hold such a value. A class reference is any object type except a
// primitive reference layout, `&T`.
inline std::optional<std::uint32_t> any_tag(const IrType& type) {
    switch (type.kind) {
    case TypeKind::Object:
        if (type.name.starts_with('&')) return std::nullopt;
        return any_reference_tag;
    case TypeKind::Bool:
        return 1;
    case TypeKind::Integer: {
        constexpr const char* names[]{"int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64"};
        for (std::uint32_t i = 0; i < 8; ++i)
            if (type.name == names[i]) return 2 + i;
        return std::nullopt;
    }
    case TypeKind::Float:
        if (type.name == "float16") return 10;
        if (type.name == "bfloat16") return 11;
        if (type.name == "float32") return 12;
        if (type.name == "float64") return 13;
        return std::nullopt;
    case TypeKind::String:
        return 14;
    default:
        return std::nullopt;
    }
}

// Whether an `any` can hold a value of `type`.
inline bool any_holdable(const IrType& type) { return any_tag(type).has_value(); }

// An `any` is four value words: the tag, the bits of a scalar, the text of
// a string, and a reference (docs/design/Matching030.md, "Representation").
inline constexpr std::size_t any_words = 4;

inline const IrType any_type{TypeKind::Any, "any", no_symbol, {}};

} // namespace neura
