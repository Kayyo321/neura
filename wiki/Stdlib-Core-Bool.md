# `Std.Core.Bool`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Bool`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Bool`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Bool** · [`export let to_int = func(v: bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L11)
  1 for true, 0 for false. Tagged `#Pure`, `#ThreadSafe`.
- **Bool** · [`export let xor = func(a: bool, b: bool) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L15)
  Returns true when exactly one operand is true. Tagged `#Pure`, `#ThreadSafe`.
- **Bool** · [`export let compare = func(a: bool, b: bool) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L19)
  The natural order: false before true. Usable as a comparator. Tagged `#Pure`, `#ThreadSafe`.
- **Bool** · [`export let hash = func(v: bool) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L31)
  1231 for true and 1237 for false (section 5.2). Tagged `#Pure`, `#ThreadSafe`.
- **Bool** · [`export let to_string = func(v: bool) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L35)
  Returns the lowercase text `true` or `false`. Tagged `#Pure`, `#ThreadSafe`.
- **Bool** · [`export let parse = func(text: string) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/bool.nra#L47)
  Accepts exactly `true` and `false`. The empty text fails with Empty; a proper prefix of either word with UnexpectedEnd at its length; any other text with InvalidCharacter at the first byte that matches neither word. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 7.4 `Bool`

`to_int(v) → int` gives 1 or 0; `xor(a, b)`; `compare(a, b) → Ordering`; `hash(v)`; `to_string(v)`. `parse(text) → Result<bool>` accepts exactly `true` and `false` and needs T1.
