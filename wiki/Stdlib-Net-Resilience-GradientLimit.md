# `Std.Net.Resilience.GradientLimit`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.GradientLimit`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.GradientLimit`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **GradientLimit** · [`export let GradientLimit = class implements limits.AdaptiveLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L27)
  A gradient adaptive limit. Tagged `#Final`, `#NotThreadSafe`.
- **GradientLimit.GradientLimit** · [`export let init = func(initial: int, minimum: int, maximum: int, weight: float64, rtt_tolerance: float64, backoff: float64, floor: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L47)
  A limit starting at `initial`. Traps with "invalid argument: <name>" unless 1 <= min <= initial <= max, 0 < smoothing <= 1, tolerance >= 1, 0 < backoff <= 1, and queue_floor >= 1.
- **GradientLimit.GradientLimit** · [`export let limit = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L77)
  The integer part of the estimate.
- **GradientLimit.GradientLimit** · [`export let estimated = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L80)
  The estimate with its fraction.
- **GradientLimit.GradientLimit** · [`export let rtt_no_load = func -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L83)
  The no-load RTT, zero before the first sample.
- **GradientLimit.GradientLimit** · [`export let queue_size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L86)
  The queue allowance for the current estimate.
- **GradientLimit.GradientLimit** · [`export let on_sample = func(rtt: durations.Duration, in_flight: int, dropped: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L93)
  Updates the estimate as described for the module. A non-positive RTT traps with "invalid argument: rtt".
- **GradientLimit.GradientLimit** · [`export let reset_rtt_noload = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L120)
  Forgets the no-load RTT and cuts the estimate to `max(min_limit, queue)`, as Netflix's probe does.
- **GradientLimit.GradientLimit** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L127)
  Text such as "GradientLimit(limit 50)".
- **GradientLimit** · [`export let create = func -> GradientLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L132)
  Netflix's defaults: initial 50, min 1, max 1000, smoothing 0.2, tolerance 2, backoff 0.9, queue floor 4. Tagged `#NoDiscard`.
- **GradientLimit** · [`export let of = func(initial: int, min_limit: int, max_limit: int, smoothing: float64) -> GradientLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gradient-limit.nra#L136)
  A limit with Netflix's default tolerance, backoff, and queue floor. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
