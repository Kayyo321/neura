# `Std.Net.Resilience.RateLimitFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.RateLimitFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.RateLimitFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RateLimitFilter** · [`export const LimitedCode = "resilience.rate_limited"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L19)
  The error code of a request the limiter denied.
- **RateLimitFilter** · [`export let RateLimitFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L23)
  Admits requests through a keyed limiter. Tagged `#Final`, `#NotThreadSafe`.
- **RateLimitFilter.RateLimitFilter** · [`export let init = func(rates: limiters.RateLimiter, key: func(Req) -> string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L32)
  A filter that asks `rates` for one permit under `key(request)`.
- **RateLimitFilter.RateLimitFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L38)
  Calls `next` when the limiter grants a permit for the request's key.
- **RateLimitFilter.RateLimitFilter** · [`export let last_decision = func -> optional.Optional<limiters.RateDecision>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L54)
  The decision for the latest request that reached the limiter, or None before the first. Tagged `#NoDiscard`.
- **RateLimitFilter.RateLimitFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L62)
  The fixed text "RateLimitFilter".
- **RateLimitFilter** · [`export let of = func<Req, Resp>(limiter: limiters.RateLimiter, key: func(Req) -> string) -> RateLimitFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limit-filter.nra#L67)
  A filter over `limiter` keyed by `key`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
