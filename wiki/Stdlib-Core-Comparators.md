# `Std.Core.Comparators`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Comparators`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/comparators.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Comparators`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Comparators** · [`export let reversed = func<T>(c: func(T, T) -> Ordering) -> func(T, T) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/comparators.nra#L8)
  The opposite order of `c`.
- **Comparators** · [`export let by_key = func<T, K>(key: func(T) -> K, c: func(K, K) -> Ordering) -> func(T, T) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/comparators.nra#L16)
  Orders values by the keys that `key` extracts, compared with `c`. `key` runs once per operand on each comparison.
- **Comparators** · [`export let then = func<T>(first: func(T, T) -> Ordering, second: func(T, T) -> Ordering) -> func(T, T) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/comparators.nra#L24)
  `first`, and `second` only where `first` finds the values equal.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`. All of this works today.
- **A comparator is a native function value** of type `(T, T) → Ordering`. There is no comparator interface or wrapper class. A named function, a bound method, or a closure all serve. A comparator must define a total order; if it does not, the library stays memory safe and terminates, and the resulting order or lookup is unspecified.
- **Natural orders** are named functions in the companion modules, usable as comparators directly: `Int32.compare`, `Float64.total_compare`, `Strings.compare`, and so on.

| Type | Natural order |
| --- | --- |
| Integers | Numeric value |
| `bool` | `false` before `true` |
| Floats | Negative infinity, negative finite values, `-0.0`, `0.0`, positive finite values, positive infinity, then NaN; all NaNs equal |
| `string` | Byte-wise lexicographic; a proper prefix sorts first. This equals ordering by Unicode scalar value. |
| Fieldless enum | Ordinal, obtained by the caller with `as` |

- The module **`Comparators`** uses generic function values (G1) for `reversed<T>(c)`, `by_key<T, K>(key: (T) → K, c: (K, K) → Ordering)`, and `then<T>(first, second)`.
- **`Comparable<T>`**, an interface with `compare_to(other: T) → Ordering`, arrives with G4. Nothing in the library requires it: sorted structures always take a comparator.
