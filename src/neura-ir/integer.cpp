#include "integer.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <utility>

namespace neura {

std::optional<IntegerFormat> specified_integer_format(const IrType& type) {
    if (type.kind != TypeKind::Integer) {
        return {};
    }
    // Lowering spells `int` and `byte` as int32 and uint8. The alias names
    // are accepted too, for IR that spells them directly.
    constexpr std::array<std::pair<std::string_view, IntegerFormat>, 10> formats{{
        {"int8", {8, true}},    {"int16", {16, true}},   {"int32", {32, true}},
        {"int64", {64, true}},  {"uint8", {8, false}},   {"uint16", {16, false}},
        {"uint32", {32, false}}, {"uint64", {64, false}}, {"int", {32, true}},
        {"byte", {8, false}},
    }};
    for (const auto& [name, format] : formats) {
        if (type.name == name) {
            return format;
        }
    }
    return {};
}

std::optional<IntegerLiteral> parse_integer_literal(std::string_view text) {
    IntegerLiteral result;
    if (text.starts_with('-')) {
        result.negative = true;
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return {};
    }
    for (const char digit : text) {
        if (digit < '0' || digit > '9') {
            return {};
        }
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result.magnitude);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return {};
    }
    if (result.magnitude == 0) {
        result.negative = false;
    }
    return result;
}

bool representable(const IntegerLiteral& value, IntegerFormat format) {
    const auto unsigned_max = format.bits == 64
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t{1} << format.bits) - 1;
    if (!format.is_signed) {
        return !value.negative && value.magnitude <= unsigned_max;
    }
    // A signed range is [-2^(bits-1), 2^(bits-1) - 1].
    const auto negative_limit = std::uint64_t{1} << (format.bits - 1);
    return value.negative ? value.magnitude <= negative_limit : value.magnitude < negative_limit;
}

} // namespace neura
