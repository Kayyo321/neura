#pragma once

#include "ir.hpp"

#include <cstddef>
#include <string>

namespace neura {

// The limit on the value words of one inline value (docs/design/Storage030.md,
// "Inline tuples and payload enums"). It is also the largest aggregate element
// NVR 1.7 allows.
inline constexpr std::size_t max_inline_words = 64;

// The value words a value of `type` occupies wherever it is stored: a tuple's
// elements' words, in order, with nested tuples flattened in place; every other
// type is one word, whatever it refers to. The count saturates at 2^32, so a
// hostile type cannot overflow it.
std::size_t value_words(const IrType& type);

// The source spelling of a tuple shape for diagnostics, with a run of equal
// elements shortened to `T x N`, as in "[int32 x 65]".
std::string describe_shape(const IrProgram& program, const IrType& type);

// E0449 for a tuple type of more than max_inline_words value words, at `span`.
Diagnostic inline_limit_diagnostic(const IrProgram& program, const IrType& type, const Span& span);

// Checks every type the specialized program declares or computes: a symbol's
// type, a parameter, a field, a local, and every node of every body. Generic
// declarations are skipped, as for containment, and each instance is checked
// on its own. Throws E0449 at the first type over the limit.
void check_inline_limit(const IrProgram& program);

} // namespace neura
