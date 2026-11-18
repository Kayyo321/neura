# `Std.Net.Resilience.ConcurrencyLimitFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.ConcurrencyLimitFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.ConcurrencyLimitFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ConcurrencyLimitFilter** · [`export const ExceededCode = "resilience.limit_exceeded"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L23)
  The error code of a request shed by the limit.
- **ConcurrencyLimitFilter** · [`export let ConcurrencyLimitFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L27)
  Sheds load above an adaptive limit. Tagged `#Final`, `#NotThreadSafe`.
- **ConcurrencyLimitFilter.ConcurrencyLimitFilter** · [`export let init = func(adaptive: limits.AdaptiveLimit, drop_if: func(Result<Resp>) -> bool, source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L40)
  A filter over `adaptive` measuring on `source`.
- **ConcurrencyLimitFilter.ConcurrencyLimitFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L47)
  Calls `next` unless the limit is reached, and samples its latency.
- **ConcurrencyLimitFilter.ConcurrencyLimitFilter** · [`export let in_flight = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L68)
  The requests in flight now.
- **ConcurrencyLimitFilter.ConcurrencyLimitFilter** · [`export let rejected = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L71)
  The requests shed so far.
- **ConcurrencyLimitFilter.ConcurrencyLimitFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L74)
  Text such as "ConcurrencyLimitFilter(VegasLimit(limit 20), 0 in flight)".
- **ConcurrencyLimitFilter** · [`export let of = func<Req, Resp>(limit: limits.AdaptiveLimit, clock: Clock) -> ConcurrencyLimitFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L80)
  A filter that counts every `Err` as a drop. Tagged `#NoDiscard`.
- **ConcurrencyLimitFilter** · [`export let classified = func<Req, Resp>(limit: limits.AdaptiveLimit, is_drop: func(Result<Resp>) -> bool, clock: Clock) -> ConcurrencyLimitFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/concurrency-limit-filter.nra#L88)
  A filter with a drop classifier. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
