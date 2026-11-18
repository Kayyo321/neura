# `Std.Net.Quic.Cubic`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.Cubic`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.Cubic`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Cubic** · [`export let Cubic = class implements CongestionController`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L33)
  One connection's CUBIC state. Tagged `#Final`, `#NotThreadSafe`.
- **Cubic.Cubic** · [`export last max_datagram_size: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L35)
  The maximum datagram size (SMSS) in bytes.
- **Cubic.Cubic** · [`export last fast_convergence: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L37)
  True when fast convergence (section 4.7) is enabled.
- **Cubic.Cubic** · [`export let init = func(datagram_size: int64, use_fast_convergence: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L61)
  A controller for a path with this maximum datagram size (1,200 through 65,527 bytes, else a trap), with or without fast convergence.
- **Cubic.Cubic** · [`export let window = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L69)
  The congestion window.
- **Cubic.Cubic** · [`export let ssthresh = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L73)
  The slow-start threshold. Tagged `#Pure`.
- **Cubic.Cubic** · [`export let max_window = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L77)
  W_max in bytes (0 before the first congestion event or epoch). Tagged `#Pure`.
- **Cubic.Cubic** · [`export let k_of_epoch = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L81)
  K of the current epoch in milliseconds. Tagged `#Pure`.
- **Cubic.Cubic** · [`export let reno_estimate = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L85)
  W_est in bytes. Tagged `#Pure`.
- **Cubic.Cubic** · [`export let bytes_in_flight = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L88)
  The bytes in flight.
- **Cubic.Cubic** · [`export let can_send = func(bytes: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L91)
  True when `bytes` more fit in the window.
- **Cubic.Cubic** · [`export let on_packet_sent = func(bytes: int64, sent_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L94)
  Adds a sent packet to the bytes in flight.
- **Cubic.Cubic** · [`export let on_packet_acked = func(bytes: int64, sent_nanos: int64, now_nanos: int64, delay_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L100)
  Grows the window for an acknowledgment; `delay_nanos` is the smoothed RTT.
- **Cubic.Cubic** · [`export let on_packets_lost = func(bytes: int64, latest_sent_nanos: int64, now_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L141)
  Removes lost bytes and reacts as to a congestion event.
- **Cubic.Cubic** · [`export let on_congestion_event = func(sent_nanos: int64, now_nanos: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L148)
  Multiplicative decrease with fast convergence (sections 4.6 and 4.7).
- **Cubic.Cubic** · [`export let on_persistent_congestion = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L168)
  Sets the minimum window, ends recovery and the epoch.
- **Cubic.Cubic** · [`export let set_app_limited = func(limited: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L175)
  Whether the sender is application- or flow-control-limited.
- **Cubic.Cubic** · [`export let name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L180)
  "Cubic".
- **Cubic.Cubic** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L183)
  Text such as "Cubic(window 12000, in flight 0)".
- **Cubic** · [`export let k_millis = func(w_max: int64, cwnd_epoch: int64, max_datagram_size: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L190)
  K in whole milliseconds, rounded down: cbrt((w_max - cwnd_epoch) / (C * size)) seconds, with windows in bytes; 0 when `w_max` is not above `cwnd_epoch`. Tagged `#Pure`.
- **Cubic** · [`export let w_cubic = func(t_millis: int64, k: int64, w_max: int64, max_datagram_size: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L197)
  W_cubic(t) in bytes for `t_millis` into the epoch (see the module documentation). Tagged `#Pure`.
- **Cubic** · [`export let w_est_increase = func(acked: int64, max_datagram_size: int64, cwnd: int64, full_alpha: bool) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L221)
  The W_est increase for `acked` bytes: alpha * acked * size / cwnd, with alpha 9/17, or 1 when `full_alpha` (W_est has reached cwnd_prior), rounded down. Tagged `#Pure`.
- **Cubic** · [`export let integer_cube_root = func(value: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/cubic.nra#L226)
  The largest n with n^3 <= value (value non-negative). Tagged `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
