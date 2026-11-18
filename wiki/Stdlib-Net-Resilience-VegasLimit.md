# `Std.Net.Resilience.VegasLimit`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.VegasLimit`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.VegasLimit`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **VegasLimit** · [`export let VegasLimit = class implements limits.AdaptiveLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L30)
  A TCP Vegas style adaptive limit. Tagged `#Final`, `#NotThreadSafe`.
- **VegasLimit.VegasLimit** · [`export let init = func(initial: int, minimum: int, maximum: int, weight: float64, alpha: int, beta: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L48)
  A limit starting at `initial`. Traps with "invalid argument: <name>" unless 1 <= min <= initial <= max, 0 < smoothing <= 1, and alpha and beta are both 0 (the defaults) or 0 < alpha <= beta.
- **VegasLimit.VegasLimit** · [`export let limit = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L70)
  The integer part of the estimate.
- **VegasLimit.VegasLimit** · [`export let estimated = func -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L73)
  The estimate with its fraction.
- **VegasLimit.VegasLimit** · [`export let rtt_no_load = func -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L76)
  The no-load RTT, zero before the first sample.
- **VegasLimit.VegasLimit** · [`export let on_sample = func(rtt: durations.Duration, in_flight: int, dropped: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L80)
  Updates the estimate as described for the module. A non-positive RTT traps with "invalid argument: rtt".
- **VegasLimit.VegasLimit** · [`export let reset_rtt_noload = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L116)
  Forgets the no-load RTT; the next sample sets it again.
- **VegasLimit.VegasLimit** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L121)
  Text such as "VegasLimit(limit 20)".
- **VegasLimit** · [`export let create = func -> VegasLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L135)
  Netflix's defaults: initial 20, min 1, max 1000, smoothing 1, alpha `3 * log(L)`, beta `6 * log(L)`. Tagged `#NoDiscard`.
- **VegasLimit** · [`export let of = func(initial: int, min_limit: int, max_limit: int, smoothing: float64) -> VegasLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L139)
  A limit with the default alpha and beta. Tagged `#NoDiscard`.
- **VegasLimit** · [`export let with_thresholds = func(initial: int, min_limit: int, max_limit: int, smoothing: float64, alpha: int, beta: int) -> VegasLimit`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/vegas-limit.nra#L144)
  A limit with fixed queue thresholds, as in classic TCP Vegas. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
