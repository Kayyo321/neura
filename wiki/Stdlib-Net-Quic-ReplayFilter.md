# `Std.Net.Quic.ReplayFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.ReplayFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.ReplayFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ReplayFilter** · [`export let ReplayFilter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L24)
  The double-hashing Bloom filter with two generations. Tagged `#Final`, `#NotThreadSafe`.
- **ReplayFilter.ReplayFilter** · [`export last bits: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L26)
  The number of bits per generation.
- **ReplayFilter.ReplayFilter** · [`export last hashes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L28)
  The number of probes per token.
- **ReplayFilter.ReplayFilter** · [`export let init = func(bit_count: int64, probe_count: int, secret: uint8[list], source: Clock, window: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L45)
  An empty filter. A bit count that is not a multiple of 64 from 64 through 2^32 traps with "invalid argument: bits", a probe count outside 1 through 32 with "invalid argument: hashes", and a window that is not positive with "invalid argument: window".
- **ReplayFilter.ReplayFilter** · [`export let positions = func(token: uint8[list]) -> int64[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L71)
  The probe positions of `token`, in probe order (see the module documentation). Tagged `#NoDiscard`.
- **ReplayFilter.ReplayFilter** · [`export let check_and_insert = func(token: uint8[list]) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L95)
  Records `token` and returns Ok(true) when it was not seen within the window (accept the early data), Ok(false) when it may have been (refuse it). Err only when the clock fails. Tagged `#NoDiscard`.
- **ReplayFilter.ReplayFilter** · [`export let may_contain = func(token: uint8[list]) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L109)
  True when `token` may have been recorded within the window, without recording it. Err only when the clock fails. Tagged `#NoDiscard`.
- **ReplayFilter.ReplayFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L116)
  Text such as "ReplayFilter(4096 bits, 4 hashes)". The seed is not shown.
- **ReplayFilter** · [`export let create = func(bits: int64, hashes: int, seed: uint8[list], clock: Clock, window: Duration) -> ReplayFilter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/replay-filter.nra#L141)
  A filter of `bits` bits and `hashes` probes with the server's secret `seed` and a replay window. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
