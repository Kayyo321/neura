#pragma once

// NeuraIR 1.10 symbol attributes (docs/NirFormat.md, Revision 1.10).
//
// A symbol carries an ordered list of IrAttribute: a registered id and a
// payload value tree. attributes.def is the registry. To add one, append a
// line there with the next free id; the .nir reader and writer, equality, the
// emit-ir listing, linker remapping, and specialization handle it with no
// further change. Typical use:
//
//     set_attribute(symbol, AttributeId::BindingLast, attribute_bool(true));
//     add_attribute(symbol, AttributeId::Tag, tag_payload(7, "Deprecated", {{"as_of", attribute_text("0.2.0")}}, span));
//     if (const auto* link = find_attribute(symbol, AttributeId::AccessorGet)) use(link->value.symbol);
//     for (const auto* tag : find_attributes(symbol, AttributeId::Tag)) inspect(tag->value);
//     if (deferred_binding(symbol)) ...
//
// Symbol references inside payloads ("s" in a schema) are remapped wherever
// the compiler renumbers symbols, so store a reference as a Symbol value, never
// as an integer.

#include "ir.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

constexpr std::uint32_t symbol_kind_bit(SymbolKind kind) {
    return std::uint32_t{1} << static_cast<unsigned>(kind);
}

inline constexpr std::uint32_t on_local = symbol_kind_bit(SymbolKind::Local);
inline constexpr std::uint32_t on_field = symbol_kind_bit(SymbolKind::Field);
inline constexpr std::uint32_t on_function = symbol_kind_bit(SymbolKind::Function);
inline constexpr std::uint32_t on_class = symbol_kind_bit(SymbolKind::Class);
// Every kind that a source declaration introduces: all but File.
inline constexpr std::uint32_t on_declarations =
    symbol_kind_bit(SymbolKind::Module) | symbol_kind_bit(SymbolKind::Class) | symbol_kind_bit(SymbolKind::Alias) |
    symbol_kind_bit(SymbolKind::Import) | symbol_kind_bit(SymbolKind::Field) | symbol_kind_bit(SymbolKind::Function) |
    symbol_kind_bit(SymbolKind::Parameter) | symbol_kind_bit(SymbolKind::Local) |
    symbol_kind_bit(SymbolKind::Global) | symbol_kind_bit(SymbolKind::TypeParameter) |
    symbol_kind_bit(SymbolKind::Enum) | symbol_kind_bit(SymbolKind::Variant) |
    symbol_kind_bit(SymbolKind::Interface);

struct AttributeInfo {
    AttributeId id;
    std::string_view name;
    // symbol_kind_bit of every kind that may carry it.
    std::uint32_t kinds;
    bool repeats;
    std::string_view schema;
};

// Every registered attribute in id order.
std::span<const AttributeInfo> attribute_registry();
// The registry entry for a raw id, or nullptr when none is registered.
const AttributeInfo* find_attribute_info(std::uint16_t id);
const AttributeInfo& attribute_info(AttributeId id);

// Payload values.
IrAttributeValue attribute_bool(bool value);
IrAttributeValue attribute_signed(std::int64_t value);
IrAttributeValue attribute_unsigned(std::uint64_t value);
IrAttributeValue attribute_text(std::string value);
IrAttributeValue attribute_symbol(SymbolId value);
IrAttributeValue attribute_type(IrType value);
IrAttributeValue attribute_span(Span value);
IrAttributeValue attribute_list(std::vector<IrAttributeValue> items);
// The signed value of a Signed payload.
std::int64_t attribute_signed_value(const IrAttributeValue& value);

// Symbol access. find_attribute returns the first one with `id`;
// find_attributes returns all of them in order.
const IrAttribute* find_attribute(const IrSymbol& symbol, AttributeId id);
std::vector<const IrAttribute*> find_attributes(const IrSymbol& symbol, AttributeId id);
bool has_attribute(const IrSymbol& symbol, AttributeId id);
// Appends, for a repeated attribute or the first of any attribute.
void add_attribute(IrSymbol& symbol, AttributeId id, IrAttributeValue value);
// Replaces the first attribute with `id`, or appends one.
void set_attribute(IrSymbol& symbol, AttributeId id, IrAttributeValue value);
// Removes every attribute with `id` and returns how many there were.
std::size_t remove_attributes(IrSymbol& symbol, AttributeId id);

// Why `value` does not match `schema` (attributes.def), or nothing.
std::optional<std::string> schema_problem(std::string_view schema, const IrAttributeValue& value);
// The first registry rule a symbol of `kind` with these attributes breaks:
// an unregistered id, a kind that may not carry the attribute, a repeat of a
// non-repeating one, or a payload that does not match its schema. Symbol
// references are not resolved here; the verifier checks them.
std::optional<std::string> attribute_problem(SymbolKind kind, const std::vector<IrAttribute>& attributes);

// Visit every symbol reference, type, or span inside the payloads, so that a
// pass renumbering symbols or renaming files can rewrite them in place.
void for_each_attribute_symbol(std::vector<IrAttribute>& attributes, const std::function<void(SymbolId&)>& visit);
void for_each_attribute_symbol(const std::vector<IrAttribute>& attributes,
                               const std::function<void(SymbolId)>& visit);
void for_each_attribute_type(std::vector<IrAttribute>& attributes, const std::function<void(IrType&)>& visit);
void for_each_attribute_span(std::vector<IrAttribute>& attributes, const std::function<void(Span&)>& visit);
void for_each_attribute_span(const std::vector<IrAttribute>& attributes,
                             const std::function<void(const Span&)>& visit);

// Payloads of the registered attributes, matching their schemas.

struct TagArgument {
    // Empty for a positional argument.
    std::string name;
    // A Bool, Signed, Unsigned, or Text literal.
    IrAttributeValue value;
};

// tag: the tag registry's id, the name as written, its arguments, and its span.
IrAttributeValue tag_payload(std::uint16_t tag, std::string name, std::vector<TagArgument> arguments, Span span);

enum class AccessorKind : std::uint8_t { Get = 0, Set = 1 };
// accessor.of: the field a hidden accessor function serves, and which one it is.
IrAttributeValue accessor_of_payload(SymbolId field, AccessorKind kind);

// class.anonymous: whether the class is structural (fields only), its
// canonical structural signature, and the occurrence's span.
IrAttributeValue anonymous_class_payload(bool structural, std::string signature, Span span);

// kernel.binding: the kernel's name and ABI version.
IrAttributeValue kernel_binding_payload(std::string kernel, std::uint32_t abi_version);

enum class InlineDirective : std::uint8_t { Auto = 0, Always = 1, Never = 2 };

// binding.last: present, with the deferred flag, on a `last` binding.
std::optional<bool> last_binding(const IrSymbol& symbol);
// True for a `last` binding declared without an initializer.
bool deferred_binding(const IrSymbol& symbol);
// inline.directive, or Auto when the function has none.
InlineDirective inline_directive(const IrSymbol& symbol);

} // namespace neura
