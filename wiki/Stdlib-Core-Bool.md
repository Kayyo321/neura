# `Std.Core.Bool`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Bool`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Bool`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Bool** · [`export let to_int = func(v: bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L8)
  1 for true, 0 for false.
- **Bool** · [`export let xor = func(a: bool, b: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L10)
- **Bool** · [`export let compare = func(a: bool, b: bool) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L13)
  The natural order: false before true. Usable as a comparator.
- **Bool** · [`export let hash = func(v: bool) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L24)
  1231 for true and 1237 for false (section 5.2).
- **Bool** · [`export let to_string = func(v: bool) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L26)
- **Bool** · [`export let parse = func(text: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L37)
  Accepts exactly `true` and `false`. The empty text fails with Empty; a proper prefix of either word with UnexpectedEnd at its length; any other text with InvalidCharacter at the first byte that matches neither word.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 7.4 `Bool`

`to_int(v) → int` gives 1 or 0; `xor(a, b)`; `compare(a, b) → Ordering`; `hash(v)`; `to_string(v)`. `parse(text) → Result<bool>` accepts exactly `true` and `false` and needs T1.
