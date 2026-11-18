#pragma once

// Deeply immutable application data (docs/design/Components030.md, "State and
// components" and "FrozenList"): the #Frozen tag, the deep-immutability check
// of type arguments (E0580), and the frozen-storage rules (E0581).

#include "ir.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace neura {

// A field marked #Frozen: list storage that only its class reads.
bool frozen_field(const IrSymbol& symbol);

// Why `type` is not deeply immutable, naming the first mutable part (a field
// declared with let, a list, a class that is not #Immutable, a function), or
// nothing when it is. A type parameter counts as immutable here; the caller
// checks the arguments it is given.
struct MutablePart {
    std::string text;
    std::optional<Span> span;
};
std::optional<MutablePart> mutable_part(const IrProgram& program, const IrType& type);

// One written type argument of a generic declaration.
struct WrittenTypeArgument {
    SymbolId parameter;
    IrType actual;
    Span span;
};

// E0580 over every type argument written in this compilation. A type
// parameter of a #Frozen class requires a deeply immutable argument; a type
// parameter written as such an argument (in a generic function or class)
// inherits the requirement, so the check reaches the concrete type at the
// outermost written instantiation.
void check_frozen_arguments(const IrProgram& program, const std::vector<WrittenTypeArgument>& written);

// E0581: #Frozen outside Std.Collections.Frozen and Std.Ui; a #Frozen field
// that is not a `last` list; and, in the bodies from `first_body` on, any use
// of a #Frozen field other than an element read, its length, or the one
// constructor assignment from a list the constructor created and only filled.
void check_frozen_storage(const IrProgram& program, SymbolId first, std::size_t first_body,
                          const std::function<Span(SymbolId)>& name_span);

} // namespace neura
