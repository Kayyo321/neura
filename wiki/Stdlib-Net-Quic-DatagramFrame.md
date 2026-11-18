# `Std.Net.Quic.DatagramFrame`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.DatagramFrame`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.DatagramFrame`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **DatagramFrame** · [`export const TypeWithoutLength: uint64 = 48`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L16)
  The frame type without a Length field.
- **DatagramFrame** · [`export const TypeWithLength: uint64 = 49`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L18)
  The frame type with a Length field.
- **DatagramFrame** · [`export const MaxDatagramFrameSizeParameter: uint64 = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L20)
  The transport parameter ID of `max_datagram_frame_size`.
- **DatagramFrame** · [`export let encode = func(data: uint8[list], with_length: bool) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L25)
  The frame carrying `data`, with a Length field when `with_length` (required unless the frame is the last in its packet). Tagged `#NoDiscard`.
- **DatagramFrame** · [`export let encoded_size = func(data_length: int, with_length: bool) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L39)
  The size of the encoded frame. Tagged `#Pure`.
- **DatagramFrame** · [`export let fits = func(data_length: int, with_length: bool, max_frame_size: uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L45)
  True when a frame of this datagram stays within the peer's `max_datagram_frame_size` (zero means the peer does not accept DATAGRAM frames). Tagged `#Pure`.
- **DatagramFrame** · [`export let decode = func(bytes: uint8[list], offset: int) -> Result<[uint8[list], int]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/datagram-frame.nra#L52)
  Decodes the DATAGRAM frame at `offset` of a packet payload that ends at `bytes.length`: the data and the offset after the frame. A Parse error when the type is not 0x30 or 0x31 (Syntax) or the bytes end early (UnexpectedEnd). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
