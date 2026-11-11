# `Std.Core.Variadic`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Variadic`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/variadic.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Variadic`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Variadic** · [`export let Argument = enum ( Text(string), Signed(int64), Unsigned(uint64), Floating16(float16), BFloating16(bfloat16), Floating32(float32), Floating(float64), Boolean(bool), Object(class) )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/variadic.nra#L5)
  Carries a packed argument with its primitive format or object-reference category preserved.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

This module has no dedicated section in the [standard library specification](Stdlib-Design). Use the declaration reference and source above for its public API; the shared contracts of the specification apply where relevant.
