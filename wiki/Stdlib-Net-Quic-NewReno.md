# `Std.Net.Quic.NewReno`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.NewReno`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.NewReno`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **NewReno** · [`export let NewReno = class implements CongestionController`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L18)
  One connection's NewReno state. Tagged `#Final`, `#NotThreadSafe`.
- **NewReno.NewReno** · [`export last max_datagram_size: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L20)
  The maximum datagram size in bytes.
- **NewReno.NewReno** · [`export let init = func(datagram_size: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L34)
  A controller for a path with this maximum datagram size (1,200 through 65,527 bytes, else a trap).
- **NewReno.NewReno** · [`export let window = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L41)
  The congestion window.
- **NewReno.NewReno** · [`export let ssthresh = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L45)
  The slow-start threshold. Tagged `#Pure`.
- **NewReno.NewReno** · [`export let bytes_in_flight = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L48)
  The bytes in flight.
- **NewReno.NewReno** · [`export let in_recovery = func(sent_nanos: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L52)
  True when the sender is in a recovery period for a packet sent at `sent_nanos`. Tagged `#Pure`.
- **NewReno.NewReno** · [`export let can_send = func(bytes: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L55)
  True when `bytes` more fit in the window.
- **NewReno.NewReno** · [`export let on_packet_sent = func(bytes: int64, sent_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L58)
  Adds a sent packet to the bytes in flight (B.4).
- **NewReno.NewReno** · [`export let on_packet_acked = func(bytes: int64, sent_nanos: int64, now_nanos: int64, delay_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L64)
  OnPacketAcked (B.5).
- **NewReno.NewReno** · [`export let on_packets_lost = func(bytes: int64, latest_sent_nanos: int64, now_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L78)
  OnPacketsLost (B.8) without the persistent-congestion test, which loss detection makes.
- **NewReno.NewReno** · [`export let on_congestion_event = func(sent_nanos: int64, now_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L85)
  OnCongestionEvent (B.6).
- **NewReno.NewReno** · [`export let on_persistent_congestion = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L97)
  Sets the minimum window and ends recovery (B.8).
- **NewReno.NewReno** · [`export let set_app_limited = func(limited: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L103)
  Whether the sender is application- or flow-control-limited.
- **NewReno.NewReno** · [`export let name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L108)
  "NewReno".
- **NewReno.NewReno** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/new-reno.nra#L111)
  Text such as "NewReno(window 12000, in flight 0)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
