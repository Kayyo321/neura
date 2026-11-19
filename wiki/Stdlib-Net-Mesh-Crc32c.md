# `Std.Net.Mesh.Crc32c`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.Crc32c`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/crc32c.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.Crc32c`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Crc32c** · [`export const Polynomial: uint32 = 2197175160`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/crc32c.nra#L12)
  The reflected polynomial 0x82F63B78 (`python3 -I -c "print(0x82F63B78)"` prints 2197175160).
- **Crc32c** · [`export let of = func(bytes: uint8[list]) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/crc32c.nra#L19)
  The CRC32C of every byte of `bytes`. Tagged `#NoDiscard`.
- **Crc32c** · [`export let of_range = func(bytes: uint8[list], start: int, end: int) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/crc32c.nra#L24)
  The CRC32C of `bytes[start]` up to but not including `bytes[end]`. Traps with "invalid argument: range" unless 0 <= start <= end <= length. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
