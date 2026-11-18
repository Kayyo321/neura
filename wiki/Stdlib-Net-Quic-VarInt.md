# `Std.Net.Quic.VarInt`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.VarInt`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.VarInt`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **VarInt** · [`export const MaxValue: uint64 = 4611686018427387903`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L16)
  The largest encodable value, 2^62 - 1.
- **VarInt** · [`export let encoded_length = func(value: uint64) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L21)
  The shortest encoding length of `value`: 1 through 63 take 1 byte, through 16,383 2, through 1,073,741,823 4, and the rest 8. Tagged `#Pure`.
- **VarInt** · [`export let encode = func(value: uint64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L35)
  The shortest encoding of `value`. Tagged `#NoDiscard`.
- **VarInt** · [`export let encode_with_length = func(value: uint64, length: int) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L40)
  The encoding of `value` in `length` bytes (1, 2, 4 or 8). Another length, or one too short for the value, traps with "invalid argument: varint length". Tagged `#NoDiscard`.
- **VarInt** · [`export let append = func(out: uint8[list], value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L47)
  Appends the shortest encoding of `value` to `out`.
- **VarInt** · [`export let append_with_length = func(out: uint8[list], value: uint64, length: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L52)
  Appends the encoding of `value` in `length` bytes to `out` (see `encode_with_length`).
- **VarInt** · [`export let decode = func(bytes: uint8[list], offset: int) -> Result<[uint64, int]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/var-int.nra#L69)
  The value encoded at `offset` and the number of bytes it took (RFC 9000 section A.1). A Parse error of kind UnexpectedEnd, at `offset`, when the bytes end inside it; an offset outside the list traps. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
