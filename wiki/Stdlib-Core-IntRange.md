# `Std.Core.IntRange`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.IntRange`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.IntRange`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **IntRange** · [`export let IntRange = class implements iterables.Iterable<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L15)
  An immutable half-open arithmetic progression with a nonzero stride. Tagged `#Final`, `#Immutable`, `#ThreadSafe`. Since 0.1.0.
- **IntRange.IntRange** · [`export last start: int { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L18)
  The inclusive start bound. Since 0.2.0.
- **IntRange.IntRange** · [`export last end: int { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L23)
  The exclusive end bound. Since 0.2.0.
- **IntRange.IntRange** · [`export last step: int { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L28)
  The signed stride between successive values; never zero. Since 0.2.0.
- **IntRange.IntRange** · [`export let init = func(start_value: int, end_value: int, step_value: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L32)
  Stores the bounds and step. A zero step traps.
- **IntRange.IntRange** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L43)
  Progressions compare by their defining bounds and step. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L53)
  Combines the start, end, and step into a hash. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L61)
  Formats the progression as `start..end:step`. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L67)
  The number of values. Computed in int64; a range with more than Int32.Max values traps (reason 1) in the conversion to int. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let is_empty = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L71)
  Reports whether the bounds leave no values in the stride's direction. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let contains = func(value: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L75)
  Tests whether the value lies within the bounds and on a stride boundary. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let get = func(index: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L87)
  The value at `index`. An index outside 0 through size() - 1 traps with "invalid argument: index out of range". Tagged `#Pure`.
- **IntRange.IntRange** · [`export let to_list = func -> int[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L96)
  A new built-in list of the values in order. Tagged `#Pure`.
- **IntRange.IntRange** · [`export let iterator = func -> iterators.Iterator<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L117)
  An iterator over the values in order, for for-each and other Iterable consumers. Tagged `#Pure`.
- **IntRange** · [`export let of = func(start: int, end: int) -> IntRange`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L168)
  The values from `start` up to, but not including, `end`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`. Since 0.1.0.
- **IntRange** · [`export let stepped = func(start: int, end: int, step: int) -> IntRange`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/int-range.nra#L172)
  A zero step traps with "invalid argument: zero step". Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`. Since 0.1.0.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 6.5 Small types

| Type | Definition | Available |
| --- | --- | --- |
| `Unit` | A fieldless enum with the single variant `Value`. | Yes |
| `Cell<T>` | A class with one mutable field `value`, and the module function `of<T>(value)`. It exists so that closures, which capture by value, can share mutable state. | Yes |
| `IntRange` | An immutable class with the get-only fields `start` (inclusive), `end` (exclusive), and `step` (nonzero). Module functions `of(start, end)` and `stepped(start, end, step)`; a zero step traps. A negative step counts down while the value is greater than `end`. Members `size()`, `is_empty()`, `contains(value)`, `get(index)`, `to_list()`, and `iterator()`; it implements `Iterable<int>`, so `for (let i in range)` visits the values in order. `size` and iteration are computed in `int64` so that no intermediate overflows. | Yes |

`Pair`, `Entry`, and `Boxed` do not exist: a pair is the native tuple `[A, B]` (`partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`), and a map entry is the tuple `[K, V]`. No API on this page takes an `Object`.

Named types are kept where the name carries a contract a tuple cannot: `RegexMatch` (group lookup by name), `FileMetadata` (three fields of unrelated types that will grow), and the value classes of `Std.Time`.

### 18.3 Numbers and text building

- **Text of numbers.** Integer and float `to_string` use the NVM's scalar text, which implements [section 7.5](Stdlib-Design#75-float-text-conversion); `StringBuilder.append_int` and the other number appends append that text.
- **Integer modules.** Every module has `to_unsigned` and `to_signed` (the same-signedness one is the identity). `format` counts the sign in the width; a width at or below the text length pads nothing. `parse` reports a syntax error before a range error and accepts leading zeros. `floor_div` and `floor_mod` share one divider circuit.
- **Trap details.** `low exceeds high`, `negative exponent`, `radix outside 2 through 36`, `pad is not one scalar value`, `negative count`, `not a Unicode scalar value`, `byte length out of range`, `not a character boundary`, `zero step`, `index out of range`, `NaN bound`, and `digits outside 0 through 100`, each after `invalid argument: `.
- **Floats.** `min`, `max`, and the rounding functions return the canonical NaN for a NaN operand; `clamp` orders `-0.0` below `0.0`. `to_fixed` and `to_exponential` print `NaN` and the infinities as `to_string` does, keep the sign of a negative value that rounds to zero, and take 0 through 100 digits. `parse` accepts `e` and `E`, rejects `+NaN` and `-NaN`, and treats digits beyond 800 significant digits as a sticky bit. Fast paths (Clinger's for `float64`, a guarded `float64` path for narrower formats) are used only where they are exact; exhaustive tests check every `float16` and `bfloat16` pattern.
- **IntRange** exposes `start()`, `end()`, and `step()` as methods. Equality and hashing use those three values in that order. The text is `start..end:step`. `size()` traps with reason 1 beyond `Int32.Max`.
