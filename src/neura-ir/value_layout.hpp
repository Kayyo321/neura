#pragma once

#include "ir.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace neura {

// How a value of a type is stored wherever the program keeps it: a frame
// slot, a parameter, a result, a field, a global, a closure capture or an
// element of an array or list (docs/Backend.md, "Values are words";
// docs/design/Storage030.md, "Inline tuples and payload enums").
//
// Every stage that loads, stores, copies, passes, returns, boxes, compares
// or names a value asks this layout instead of testing the type's kind, so a
// new kind of several-word value is added here and nowhere else.
struct ValueLayout {
    enum class Form {
        // One word: a scalar, a fieldless enum, or a reference.
        Word,
        // A tuple: its elements' words in order, a nested value flattened in
        // place.
        Tuple,
    };

    Form form = Form::Word;
    // The type of each word: never a tuple or another several-word value.
    std::vector<IrType> words;
    // The types this value holds directly: a tuple's elements.
    std::vector<IrType> parts;
    // For a tuple, the positions among `words` of each element's words.
    std::vector<std::vector<std::size_t>> elements;
    // The name of each word after the value's own name: "" for one word,
    // ".0", ".1.0" and so on for a tuple.
    std::vector<std::string> suffixes;

    // Stored as its words and boxed where the runtime carries one word. A
    // tuple of one word is inline too: it is boxed at those boundaries.
    bool inline_value() const { return form != Form::Word; }
    std::size_t size() const { return words.size(); }
};

// True when a value of `type` is stored as value words rather than as one
// word (ValueLayout::inline_value), without building its layout.
bool inline_value(const IrProgram& program, const IrType& type);

// The value words a value of `type` occupies: one for a type that is not
// inline. The count saturates at 2^32, so a hostile type cannot overflow it.
std::size_t value_words(const IrProgram& program, const IrType& type);

// The layout of `type`. A caller checks value_words against the inline limit
// first; the layout of a larger type is never built. Throws E0802 for a tuple
// without elements.
ValueLayout value_layout(const IrProgram& program, const IrType& type, const Span& where);

} // namespace neura
