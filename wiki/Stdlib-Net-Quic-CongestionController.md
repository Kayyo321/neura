# `Std.Net.Quic.CongestionController`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.CongestionController`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.CongestionController`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CongestionController** · [`export const MaxWindow: int64 = 1000000000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L15)
  The largest congestion window, 1,000,000,000 bytes, which keeps the window arithmetic in 64 bits.
- **CongestionController** · [`export const MinDatagramSize: int64 = 1200`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L17)
  The smallest maximum datagram size QUIC allows (RFC 9002 section B.2).
- **CongestionController** · [`export let CongestionController = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L20)
  A congestion controller.
- **CongestionController** · [`export let initial_window = func(max_datagram_size: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L45)
  The initial window: min(10 * size, max(14,720, 2 * size)) (RFC 9002 section 7.2). Tagged `#Pure`.
- **CongestionController** · [`export let minimum_window = func(max_datagram_size: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L53)
  The minimum window: 2 * size (RFC 9002 section 7.2). Tagged `#Pure`.
- **CongestionController** · [`export let check_datagram_size = func(max_datagram_size: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L56)
  Traps with "invalid argument: max datagram size" unless the size is from 1,200 through 65,527.
- **CongestionController** · [`export let check_event = func(bytes: int64, time_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L63)
  Traps unless the byte count and time are non-negative.
- **CongestionController** · [`export let remove_in_flight = func(in_flight: int64, bytes: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L74)
  `bytes_in_flight - bytes`, trapping with "invalid argument: more bytes than in flight" below zero. Tagged `#Pure`.
- **CongestionController** · [`export let capped = func(window: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/congestion-controller.nra#L83)
  `window` capped at `MaxWindow`. Tagged `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
