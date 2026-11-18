# `Std.Net.Socket.Offload`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Socket.Offload`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/offload.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Socket.Offload`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Offload** · [`export const MaxUdpPayload: int = 65507`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/offload.nra#L16)
  The largest UDP payload over IPv4: 65,535 bytes minus the 8-byte UDP and 20-byte IPv4 headers.
- **Offload** · [`export let segment_count = func(length: int, segment_size: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/offload.nra#L21)
  The number of datagrams a buffer of `length` bytes becomes with segments of `segment_size` bytes. A segment size below 1 or a negative length traps with "invalid argument". Tagged `#Pure`.
- **Offload** · [`export let split = func(data: uint8[list], segment_size: int) -> uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/socket/offload.nra#L29)
  Cuts `data` into datagrams of `segment_size` bytes; the last holds the rest. An empty buffer gives no datagrams. A segment size below 1 or above `MaxUdpPayload` traps with "invalid argument". Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
