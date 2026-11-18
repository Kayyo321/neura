# `Std.Crypto.Core.ConstantTime`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Crypto.Core.ConstantTime`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/constant-time.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Crypto.Core.ConstantTime`](#exported-declarations)
- [`Std.Crypto` contracts and guarantees](Stdlib-Design#20-stdcrypto)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ConstantTime** · [`export let ct_eq_uint32 = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/constant-time.nra#L12)
  1 when `a` equals `b`, otherwise 0, as a uint32. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **ConstantTime** · [`export let ct_less = func(a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/constant-time.nra#L20)
  1 when `a` is below `b`, otherwise 0, as a uint32. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **ConstantTime** · [`export let ct_select = func(choice: uint32, a: uint32, b: uint32) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/constant-time.nra#L29)
  `a` when `choice` is 1 and `b` when it is 0. Any other choice traps with "invalid argument: choice must be 0 or 1", a check on the caller's public contract rather than on secret data. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **ConstantTime** · [`export let ct_select_bytes = func(choice: uint32, a: uint8[list], b: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/constant-time.nra#L40)
  A new list holding `a` when `choice` is 1 and `b` when it is 0, byte by byte. The lists must have one length; otherwise, or for a choice other than 0 or 1, it traps. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **ConstantTime** · [`export let ct_eq = func(a: uint8[list], b: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/crypto/core/constant-time.nra#L58)
  Whether the two lists hold the same bytes. Lists of different lengths are unequal at once: the length is treated as public. Otherwise every byte is read, whatever the first difference. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is in [the standard library specification, section 20](Stdlib-Design#202-stdcryptocore). What the runtime does not guarantee (constant time, locked memory, erasure of collector copies) is stated at the start of that section.
