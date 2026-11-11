#include "attributes.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <tuple>

namespace neura {
namespace {

constexpr AttributeInfo registry[] = {
#define NEURA_ATTRIBUTE(identifier, id, name, kinds, repeats, schema) \
    {AttributeId::identifier, name, kinds, repeats, schema},
#include "attributes.def"
#undef NEURA_ATTRIBUTE
};

constexpr bool registry_is_ordered() {
    for (std::size_t i = 0; i < std::size(registry); ++i) {
        if (static_cast<std::uint16_t>(registry[i].id) == 0) return false;
        if (i > 0 && registry[i].id <= registry[i - 1].id) return false;
    }
    return true;
}
static_assert(registry_is_ordered(), "attributes.def ids must be nonzero, unique, and ascending");

std::string_view kind_name(AttributeValueKind kind) {
    switch (kind) {
    case AttributeValueKind::Bool: return "bool";
    case AttributeValueKind::Signed: return "signed integer";
    case AttributeValueKind::Unsigned: return "unsigned integer";
    case AttributeValueKind::Text: return "text";
    case AttributeValueKind::Symbol: return "symbol";
    case AttributeValueKind::Type: return "type";
    case AttributeValueKind::Span: return "span";
    case AttributeValueKind::List: return "list";
    }
    return "<invalid>";
}

std::size_t skip_spaces(std::string_view schema, std::size_t at) {
    while (at < schema.size() && schema[at] == ' ') ++at;
    return at;
}

// The position just after the schema element that starts at `at`, or npos
// when the schema is malformed.
std::size_t element_end(std::string_view schema, std::size_t at) {
    if (at >= schema.size()) return std::string_view::npos;
    switch (schema[at]) {
    case '*':
        return element_end(schema, skip_spaces(schema, at + 1));
    case '[':
        at = skip_spaces(schema, at + 1);
        while (at < schema.size() && schema[at] != ']') {
            const auto end = element_end(schema, at);
            if (end == std::string_view::npos) return end;
            at = skip_spaces(schema, end);
        }
        return at < schema.size() ? at + 1 : std::string_view::npos;
    case 'u':
        ++at;
        while (at < schema.size() && schema[at] >= '0' && schema[at] <= '9') ++at;
        return at;
    case 'b': case 'i': case 't': case 's': case 'y': case 'p': case 'a':
        return at + 1;
    default:
        return std::string_view::npos;
    }
}

std::optional<std::string> mismatch(const std::string& where, std::string_view expected,
                                    const IrAttributeValue& value) {
    const auto found = kind_name(value.kind);
    const auto article = found.starts_with('u') ? " is an " : " is a ";
    return where + article + std::string(found) + ", expected " + std::string(expected);
}

std::optional<std::string> check(std::string_view schema, std::size_t at, const IrAttributeValue& value,
                                 const std::string& where) {
    if (at >= schema.size()) return "invalid attribute schema";
    const auto expect = [&](AttributeValueKind kind) -> std::optional<std::string> {
        if (value.kind != kind) return mismatch(where, kind_name(kind), value);
        return std::nullopt;
    };
    switch (schema[at]) {
    case 'b':
        if (auto problem = expect(AttributeValueKind::Bool)) return problem;
        if (value.integer > 1) return where + " is a bool other than 0 or 1";
        return std::nullopt;
    case 'i': return expect(AttributeValueKind::Signed);
    case 'u': {
        if (auto problem = expect(AttributeValueKind::Unsigned)) return problem;
        const auto end = element_end(schema, at);
        if (end > at + 1) {
            std::uint64_t maximum = 0;
            const auto digits = schema.substr(at + 1, end - at - 1);
            const auto [next, error] = std::from_chars(digits.data(), digits.data() + digits.size(), maximum);
            if (error != std::errc{}) return "invalid attribute schema";
            if (value.integer > maximum)
                return where + " is " + std::to_string(value.integer) + ", above its maximum " + std::to_string(maximum);
        }
        return std::nullopt;
    }
    case 't': return expect(AttributeValueKind::Text);
    case 's':
        if (auto problem = expect(AttributeValueKind::Symbol)) return problem;
        if (value.symbol == no_symbol) return where + " is an absent symbol reference";
        return std::nullopt;
    case 'y': return expect(AttributeValueKind::Type);
    case 'p': return expect(AttributeValueKind::Span);
    case 'a':
        if (value.kind == AttributeValueKind::Bool && value.integer > 1) return where + " is a bool other than 0 or 1";
        if (value.kind != AttributeValueKind::Bool && value.kind != AttributeValueKind::Signed &&
            value.kind != AttributeValueKind::Unsigned && value.kind != AttributeValueKind::Text)
            return mismatch(where, "literal", value);
        return std::nullopt;
    case '*': {
        if (auto problem = expect(AttributeValueKind::List)) return problem;
        const auto item = skip_spaces(schema, at + 1);
        for (std::size_t i = 0; i < value.items.size(); ++i) {
            if (auto problem = check(schema, item, value.items[i], where + " item " + std::to_string(i + 1)))
                return problem;
        }
        return std::nullopt;
    }
    case '[': {
        if (auto problem = expect(AttributeValueKind::List)) return problem;
        auto cursor = skip_spaces(schema, at + 1);
        std::size_t index = 0;
        while (cursor < schema.size() && schema[cursor] != ']') {
            if (index >= value.items.size())
                return where + " has " + std::to_string(value.items.size()) + " item(s), expected more";
            if (auto problem = check(schema, cursor, value.items[index], where + " item " + std::to_string(index + 1)))
                return problem;
            const auto end = element_end(schema, cursor);
            if (end == std::string_view::npos) return "invalid attribute schema";
            cursor = skip_spaces(schema, end);
            ++index;
        }
        if (index != value.items.size())
            return where + " has " + std::to_string(value.items.size()) + " item(s), expected " + std::to_string(index);
        return std::nullopt;
    }
    default:
        return "invalid attribute schema";
    }
}

void visit_symbols(IrAttributeValue& value, const std::function<void(SymbolId&)>& visit) {
    if (value.kind == AttributeValueKind::Symbol) visit(value.symbol);
    if (value.kind == AttributeValueKind::List)
        for (auto& item : value.items) visit_symbols(item, visit);
}

void visit_symbols(const IrAttributeValue& value, const std::function<void(SymbolId)>& visit) {
    if (value.kind == AttributeValueKind::Symbol) visit(value.symbol);
    if (value.kind == AttributeValueKind::List)
        for (const auto& item : value.items) visit_symbols(item, visit);
}

void visit_types(IrAttributeValue& value, const std::function<void(IrType&)>& visit) {
    if (value.kind == AttributeValueKind::Type) visit(value.type);
    if (value.kind == AttributeValueKind::List)
        for (auto& item : value.items) visit_types(item, visit);
}

void visit_spans(IrAttributeValue& value, const std::function<void(Span&)>& visit) {
    if (value.kind == AttributeValueKind::Span) visit(value.span);
    if (value.kind == AttributeValueKind::List)
        for (auto& item : value.items) visit_spans(item, visit);
}

void visit_spans(const IrAttributeValue& value, const std::function<void(const Span&)>& visit) {
    if (value.kind == AttributeValueKind::Span) visit(value.span);
    if (value.kind == AttributeValueKind::List)
        for (const auto& item : value.items) visit_spans(item, visit);
}

} // namespace

bool operator==(const IrAttributeValue& a, const IrAttributeValue& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case AttributeValueKind::Bool:
    case AttributeValueKind::Signed:
    case AttributeValueKind::Unsigned: return a.integer == b.integer;
    case AttributeValueKind::Text: return a.text == b.text;
    case AttributeValueKind::Symbol: return a.symbol == b.symbol;
    case AttributeValueKind::Type: return a.type == b.type;
    case AttributeValueKind::Span:
        return std::tie(a.span.file, a.span.line, a.span.column, a.span.offset, a.span.length) ==
            std::tie(b.span.file, b.span.line, b.span.column, b.span.offset, b.span.length);
    case AttributeValueKind::List: return a.items == b.items;
    }
    return false;
}

std::span<const AttributeInfo> attribute_registry() {
    return registry;
}

const AttributeInfo* find_attribute_info(std::uint16_t id) {
    const auto found = std::ranges::find(registry, id, [](const AttributeInfo& info) {
        return static_cast<std::uint16_t>(info.id);
    });
    return found == std::end(registry) ? nullptr : &*found;
}

const AttributeInfo& attribute_info(AttributeId id) {
    return *find_attribute_info(static_cast<std::uint16_t>(id));
}

IrAttributeValue attribute_bool(bool value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Bool;
    result.integer = value ? 1 : 0;
    return result;
}

IrAttributeValue attribute_signed(std::int64_t value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Signed;
    result.integer = static_cast<std::uint64_t>(value);
    return result;
}

IrAttributeValue attribute_unsigned(std::uint64_t value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Unsigned;
    result.integer = value;
    return result;
}

IrAttributeValue attribute_text(std::string value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Text;
    result.text = std::move(value);
    return result;
}

IrAttributeValue attribute_symbol(SymbolId value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Symbol;
    result.symbol = value;
    return result;
}

IrAttributeValue attribute_type(IrType value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Type;
    result.type = std::move(value);
    return result;
}

IrAttributeValue attribute_span(Span value) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::Span;
    result.span = std::move(value);
    return result;
}

IrAttributeValue attribute_list(std::vector<IrAttributeValue> items) {
    IrAttributeValue result;
    result.kind = AttributeValueKind::List;
    result.items = std::move(items);
    return result;
}

std::int64_t attribute_signed_value(const IrAttributeValue& value) {
    return static_cast<std::int64_t>(value.integer);
}

const IrAttribute* find_attribute(const IrSymbol& symbol, AttributeId id) {
    const auto found = std::ranges::find(symbol.attributes, id, &IrAttribute::id);
    return found == symbol.attributes.end() ? nullptr : &*found;
}

std::vector<const IrAttribute*> find_attributes(const IrSymbol& symbol, AttributeId id) {
    std::vector<const IrAttribute*> result;
    for (const auto& attribute : symbol.attributes)
        if (attribute.id == id) result.push_back(&attribute);
    return result;
}

bool has_attribute(const IrSymbol& symbol, AttributeId id) {
    return find_attribute(symbol, id) != nullptr;
}

void add_attribute(IrSymbol& symbol, AttributeId id, IrAttributeValue value) {
    symbol.attributes.push_back({id, std::move(value)});
}

void set_attribute(IrSymbol& symbol, AttributeId id, IrAttributeValue value) {
    const auto found = std::ranges::find(symbol.attributes, id, &IrAttribute::id);
    if (found != symbol.attributes.end()) {
        found->value = std::move(value);
    } else {
        add_attribute(symbol, id, std::move(value));
    }
}

std::size_t remove_attributes(IrSymbol& symbol, AttributeId id) {
    return std::erase_if(symbol.attributes, [id](const IrAttribute& attribute) { return attribute.id == id; });
}

std::optional<std::string> schema_problem(std::string_view schema, const IrAttributeValue& value) {
    const auto start = skip_spaces(schema, 0);
    const auto end = element_end(schema, start);
    if (end == std::string_view::npos || skip_spaces(schema, end) != schema.size())
        return "invalid attribute schema '" + std::string(schema) + "'";
    return check(schema, start, value, "the payload");
}

std::optional<std::string> attribute_problem(SymbolKind kind, const std::vector<IrAttribute>& attributes) {
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const auto& attribute = attributes[i];
        const auto* info = find_attribute_info(static_cast<std::uint16_t>(attribute.id));
        if (!info) return "unknown symbol attribute " + std::to_string(static_cast<unsigned>(attribute.id));
        const auto name = "attribute " + std::string(info->name);
        if ((info->kinds & symbol_kind_bit(kind)) == 0) return name + " is not allowed on this kind of symbol";
        if (!info->repeats) {
            for (std::size_t j = 0; j < i; ++j) {
                if (attributes[j].id == attribute.id) return name + " is repeated";
            }
        }
        if (auto problem = schema_problem(info->schema, attribute.value)) return name + ": " + *problem;
    }
    return std::nullopt;
}

void for_each_attribute_symbol(std::vector<IrAttribute>& attributes, const std::function<void(SymbolId&)>& visit) {
    for (auto& attribute : attributes) visit_symbols(attribute.value, visit);
}

void for_each_attribute_symbol(const std::vector<IrAttribute>& attributes,
                               const std::function<void(SymbolId)>& visit) {
    for (const auto& attribute : attributes) visit_symbols(attribute.value, visit);
}

void for_each_attribute_type(std::vector<IrAttribute>& attributes, const std::function<void(IrType&)>& visit) {
    for (auto& attribute : attributes) visit_types(attribute.value, visit);
}

void for_each_attribute_span(std::vector<IrAttribute>& attributes, const std::function<void(Span&)>& visit) {
    for (auto& attribute : attributes) visit_spans(attribute.value, visit);
}

void for_each_attribute_span(const std::vector<IrAttribute>& attributes,
                             const std::function<void(const Span&)>& visit) {
    for (const auto& attribute : attributes) visit_spans(attribute.value, visit);
}

IrAttributeValue tag_payload(std::uint16_t tag, std::string name, std::vector<TagArgument> arguments, Span span) {
    std::vector<IrAttributeValue> items;
    items.reserve(arguments.size());
    for (auto& argument : arguments) {
        items.push_back(attribute_list({attribute_text(std::move(argument.name)), std::move(argument.value)}));
    }
    return attribute_list({attribute_unsigned(tag), attribute_text(std::move(name)), attribute_list(std::move(items)),
                           attribute_span(std::move(span))});
}

IrAttributeValue accessor_of_payload(SymbolId field, AccessorKind kind) {
    return attribute_list({attribute_symbol(field), attribute_unsigned(static_cast<std::uint64_t>(kind))});
}

IrAttributeValue anonymous_class_payload(bool structural, std::string signature, Span span) {
    return attribute_list({attribute_bool(structural), attribute_text(std::move(signature)),
                           attribute_span(std::move(span))});
}

IrAttributeValue kernel_binding_payload(std::string kernel, std::uint32_t abi_version) {
    return attribute_list({attribute_text(std::move(kernel)), attribute_unsigned(abi_version)});
}

std::optional<bool> last_binding(const IrSymbol& symbol) {
    const auto* found = find_attribute(symbol, AttributeId::BindingLast);
    if (!found || found->value.kind != AttributeValueKind::Bool) return std::nullopt;
    return found->value.integer != 0;
}

bool deferred_binding(const IrSymbol& symbol) {
    return last_binding(symbol).value_or(false);
}

InlineDirective inline_directive(const IrSymbol& symbol) {
    const auto* found = find_attribute(symbol, AttributeId::InlineDirective);
    if (!found || found->value.kind != AttributeValueKind::Unsigned || found->value.integer > 2)
        return InlineDirective::Auto;
    return static_cast<InlineDirective>(found->value.integer);
}

} // namespace neura
