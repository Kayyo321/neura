# `Std.Net.Quic.Ledbat`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.Ledbat`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.Ledbat`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Ledbat** · [`export const CurrentFilter: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L23)
  CURRENT_FILTER: the delay samples the MIN filter keeps.
- **Ledbat** · [`export const BaseHistory: int = 10`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L25)
  BASE_HISTORY: the per-minute minima kept.
- **Ledbat** · [`export const MinSegments: int64 = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L27)
  INIT_CWND and MIN_CWND, in segments.
- **Ledbat** · [`export let Ledbat = class implements CongestionController`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L33)
  One connection's LEDBAT state. Tagged `#Final`, `#NotThreadSafe`.
- **Ledbat.Ledbat** · [`export last max_datagram_size: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L35)
  The MSS (maximum datagram size) in bytes.
- **Ledbat.Ledbat** · [`export last target_micros: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L37)
  TARGET in microseconds.
- **Ledbat.Ledbat** · [`export let init = func(datagram_size: int64, target: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L55)
  A controller with this MSS (1,200 through 65,527 bytes) and TARGET (1 microsecond through 100 ms, else "invalid argument: target").
- **Ledbat.Ledbat** · [`export let window = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L66)
  The congestion window.
- **Ledbat.Ledbat** · [`export let bytes_in_flight = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L69)
  The bytes in flight.
- **Ledbat.Ledbat** · [`export let can_send = func(bytes: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L72)
  True when `bytes` more fit in the window.
- **Ledbat.Ledbat** · [`export let queuing_delay_micros = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L76)
  The filtered current delay minus the base delay in microseconds; 0 before any sample. Tagged `#Pure`.
- **Ledbat.Ledbat** · [`export let on_packet_sent = func(bytes: int64, sent_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L84)
  Adds a sent packet to the bytes in flight.
- **Ledbat.Ledbat** · [`export let on_packet_acked = func(bytes: int64, sent_nanos: int64, now_nanos: int64, delay_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L90)
  "on acknowledgement" of section 2.4.2; `delay_nanos` is the one-way delay sample.
- **Ledbat.Ledbat** · [`export let on_packets_lost = func(bytes: int64, latest_sent_nanos: int64, now_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L120)
  "on data loss" of section 2.4.2, once per recovery period.
- **Ledbat.Ledbat** · [`export let on_congestion_event = func(sent_nanos: int64, now_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L127)
  Halves the window, at most once per recovery period.
- **Ledbat.Ledbat** · [`export let on_persistent_congestion = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L141)
  The congestion timeout: one MSS.
- **Ledbat.Ledbat** · [`export let set_app_limited = func(limited: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L147)
  Whether the sender is application- or flow-control-limited.
- **Ledbat.Ledbat** · [`export let name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L152)
  "Ledbat".
- **Ledbat.Ledbat** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/ledbat.nra#L155)
  Text such as "Ledbat(window 2400, in flight 0)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
