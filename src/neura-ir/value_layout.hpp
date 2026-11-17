#pragma once

#include "ir.hpp"

#include <cstddef>
#include <map>
#include <set>
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
        // A payload enum: a uint32 tag, then its payload words pooled by word
        // type. Each variant uses the first words of each type in payload
        // order; a word that a variant does not use holds zero or nil.
        Enum,
    };

    Form form = Form::Word;
    // The type of each word: never a tuple or another several-word value.
    // A pooled word has the type of its first use.
    std::vector<IrType> words;
    // The types this value holds directly: a tuple's elements, or every
    // payload of every variant of an enum, in declaration order.
    std::vector<IrType> parts;
    // For a tuple, the positions among `words` of each element's words.
    std::vector<std::vector<std::size_t>> elements;
    // For an enum, its variants in ordinal order, and for each variant the
    // positions among `words` of each payload's words. The tag is word 0.
    std::vector<SymbolId> variants;
    std::vector<std::vector<std::vector<std::size_t>>> payloads;
    // The name of each word after the value's own name: "" for one word,
    // ".0", ".1.0" and so on for a tuple, ".tag" and ".Some.0" for an enum
    // (a pooled word lists each use, as in ".Ok.0|Err.0.tag").
    std::vector<std::string> suffixes;

    // Stored as its words and boxed where the runtime carries one word. A
    // tuple of one word is inline too: it is boxed at those boundaries.
    bool inline_value() const { return form != Form::Word; }
    std::size_t size() const { return words.size(); }
};

// The layouts of a specialized program's value types. The program must not
// gain or lose symbols while this object is in use.
class ValueLayouts {
public:
    explicit ValueLayouts(const IrProgram& program);

    // True when a value of `type` is stored as value words rather than as
    // one word (ValueLayout::inline_value): a tuple or a payload enum.
    bool inline_value(const IrType& type) const;

    // The value words a value of `type` occupies: one for a type that is not
    // inline. The count saturates at 2^32, so a hostile type cannot overflow
    // it. Throws E0448 for an enum that contains itself.
    std::size_t value_words(const IrType& type);

    // The layout of `type`. A caller checks value_words against the inline
    // limit first; the layout of a larger type is never built. Throws E0802
    // for a tuple without elements and E0448 for an enum that contains
    // itself.
    const ValueLayout& layout(const IrType& type, const Span& where);

    // The variants of an enum, in ordinal order.
    const std::vector<SymbolId>& variants(SymbolId enumeration) const;

    // The pool of a word's type: two words share a pool exactly when they
    // have the same NVR type, except that a collection of inline values of
    // two or more words is pooled by its element type.
    std::string word_key(const IrType& word);

private:
    using Counts = std::vector<std::pair<std::string, std::size_t>>;
    Counts counts(const IrType& type);
    void enter(SymbolId enumeration);
    ValueLayout build(const IrType& type, const Span& where);

    const IrProgram& program_;
    std::map<SymbolId, std::vector<SymbolId>> variants_;
    std::set<SymbolId> payload_enums_;
    std::map<SymbolId, Counts> enum_counts_;
    std::map<std::string, ValueLayout> layouts_;
    std::set<SymbolId> active_;
};

// A key that identifies an IR type exactly.
std::string type_identity(const IrType& type);

// True when a value of `type` is stored as value words; for a caller that
// does not keep a ValueLayouts. It scans the program's symbols for an enum.
bool inline_value(const IrProgram& program, const IrType& type);

} // namespace neura
