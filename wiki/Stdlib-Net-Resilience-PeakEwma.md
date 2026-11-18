# `Std.Net.Resilience.PeakEwma`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.PeakEwma`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.PeakEwma`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PeakEwma** · [`export let PeakEwma = class<Endpoint> implements balancers.LoadBalancer<Endpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L29)
  P2C over Peak EWMA cost. Tagged `#Final`, `#NotThreadSafe`.
- **PeakEwma.PeakEwma** · [`export let init = func(targets: Endpoint[list], decay: durations.Duration, default_rtt: durations.Duration, source: randoms.Random, time: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L48)
  A balancer over a copy of `targets`. Every estimate starts at `default_rtt` stamped with the clock's reading at construction (zero if the clock fails then). Traps with "invalid argument: <name>" unless `decay` is positive and `default_rtt` is not negative.
- **PeakEwma.PeakEwma** · [`export let pick = func -> Result<balancers.Selection<Endpoint>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L72)
  The lower-cost of two random distinct endpoints.
- **PeakEwma.PeakEwma** · [`export let report = func(selection: balancers.Selection<Endpoint>, latency: durations.Duration, success: bool) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L95)
  Ends the pending request and observes its latency.
- **PeakEwma.PeakEwma** · [`export let cost = func(index: int) -> Result<float64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L107)
  The cost of endpoint `index` now: the decayed estimate times (pending + 1). Tagged `#NoDiscard`.
- **PeakEwma.PeakEwma** · [`export let estimate = func(index: int) -> float64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L113)
  The latency estimate of endpoint `index` in nanoseconds, as of its last update.
- **PeakEwma.PeakEwma** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L116)
  The number of endpoints.
- **PeakEwma.PeakEwma** · [`export let pending = func(index: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L119)
  The pending requests of endpoint `index`.
- **PeakEwma.PeakEwma** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L122)
  Text such as "PeakEwma(3 endpoints)".
- **PeakEwma** · [`export let of = func<Endpoint>(endpoints: Endpoint[list], tau: durations.Duration, default_rtt: durations.Duration, random: randoms.Random, clock: Clock) -> PeakEwma<Endpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/peak-ewma.nra#L154)
  A Peak EWMA balancer over a copy of `endpoints` with decay time `tau` and initial estimate `default_rtt`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
