# `Std.Crypto.Kdf.Balloon`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Kdf.Balloon`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/balloon.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Kdf.Balloon`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Balloon** · [`export let derive = func<H: StreamingDigest<H>>(password: uint8[list], salt: uint8[list], space_cost: int, time_cost: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/kdf/balloon.nra#L20)
  The H-length Balloon hash of the password and salt with `space_cost` blocks of memory and `time_cost` mixing rounds; a cost below 1 traps with "invalid argument: Balloon costs". Tagged `#NoDiscard`. Since 0.3.0.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#206-stdcryptokdf). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
