# `Std.Net.Resilience.HedgingFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.HedgingFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.HedgingFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **HedgingFilter** · [`export const UnsupportedCode = "resilience.hedging_unsupported"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L43)
  The error code when the next service is not a `SpawnableService`.
- **HedgingFilter** · [`export let HedgingFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L47)
  Hedges slow requests with a backup request. Tagged `#Final`, `#NotThreadSafe`.
- **HedgingFilter.HedgingFilter** · [`export let init = func(recent: windows.LatencyWindow, p: int, minimum: int, limit: budgets.RetryBudget, source: Clock, backup: optional.Optional<func(Req) -> Result<Resp>>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L67)
  A hedging filter. Traps with "invalid argument: <name>" unless 1 <= percentile <= 100 and min_samples >= 1.
- **HedgingFilter.HedgingFilter** · [`export let hedge_delay = func -> optional.Optional<durations.Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L86)
  The hedge delay now: the window's percentile, or None while it holds fewer than `min_samples` or when the percentile is zero (a zero delay would hedge every request). Tagged `#NoDiscard`.
- **HedgingFilter.HedgingFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L98)
  Runs the request as described for the module.
- **HedgingFilter.HedgingFilter** · [`export let hedges = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L117)
  Backup requests sent so far.
- **HedgingFilter.HedgingFilter** · [`export let backup_wins = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L120)
  Requests whose backup answered first, so far.
- **HedgingFilter.HedgingFilter** · [`export let latencies = func -> windows.LatencyWindow`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L123)
  The latency window.
- **HedgingFilter.HedgingFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L126)
  Text such as "HedgingFilter(p95, 3 hedges)".
- **HedgingFilter** · [`export let of = func<Req, Resp>(window: int, percentile: int, min_samples: int, budget: budgets.RetryBudget, clock: Clock) -> HedgingFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L197)
  A filter hedging at percentile `percentile` of the last `window` latencies once it holds `min_samples`, with backups bounded by `budget`, sending backups to the same function. Tagged `#NoDiscard`.
- **HedgingFilter** · [`export let with_replica = func<Req, Resp>(window: int, percentile: int, min_samples: int, budget: budgets.RetryBudget, clock: Clock, replica: func(Req) -> Result<Resp>) -> HedgingFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/hedging-filter.nra#L204)
  As `of`, sending backups to `replica`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
