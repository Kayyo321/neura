#include "value_layout.hpp"

namespace neura {

namespace {

constexpr std::size_t saturated_words = std::size_t{1} << 32;

} // namespace

bool inline_value(const IrProgram&, const IrType& type) {
    return type.kind == TypeKind::Tuple;
}

std::size_t value_words(const IrProgram& program, const IrType& type) {
    if (!inline_value(program, type)) return 1;
    std::size_t total = 0;
    for (const auto& element : type.arguments) {
        total += value_words(program, element);
        if (total > saturated_words) return saturated_words;
    }
    return total;
}

ValueLayout value_layout(const IrProgram& program, const IrType& type, const Span& where) {
    ValueLayout layout;
    if (!inline_value(program, type)) {
        layout.words = {type};
        layout.suffixes = {""};
        return layout;
    }
    layout.form = ValueLayout::Form::Tuple;
    layout.parts = type.arguments;
    for (std::size_t i = 0; i < type.arguments.size(); ++i) {
        const auto inner = value_layout(program, type.arguments[i], where);
        auto& positions = layout.elements.emplace_back();
        for (std::size_t word = 0; word < inner.words.size(); ++word) {
            positions.push_back(layout.words.size());
            layout.words.push_back(inner.words[word]);
            layout.suffixes.push_back("." + std::to_string(i) + inner.suffixes[word]);
        }
    }
    if (layout.words.empty()) throw Diagnostic(where, "a tuple without elements has no value").with_code("E0802");
    return layout;
}

} // namespace neura
