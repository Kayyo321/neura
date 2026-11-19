# `Std.Net.Resilience.RateLimiter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.RateLimiter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.RateLimiter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RateLimiter** · [`export let RateLimiter = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L13)
  Decides whether work for a key may proceed now.
- **RateLimiter** · [`export let RateDecision = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L24)
  The answer of a limiter: whether the work may proceed, how many more single permits would be granted at once, how long to wait before the same request could succeed, and how long until the limiter is back to its full allowance. Tagged `#Final`, `#Immutable`, `#NoDiscard`.
- **RateLimiter.RateDecision** · [`export last allowed: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L26)
  Whether the permits were granted.
- **RateLimiter.RateDecision** · [`export last remaining: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L28)
  The single permits still available at once after this decision.
- **RateLimiter.RateDecision** · [`export last retry_after: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L30)
  Zero when allowed; otherwise the wait before the same request would be granted.
- **RateLimiter.RateDecision** · [`export last reset_after: durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L32)
  The wait until the full allowance is available again.
- **RateLimiter.RateDecision** · [`export let init = func(granted: bool, left: int, retry: durations.Duration, reset: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L35)
  A decision with each part.
- **RateLimiter.RateDecision** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L43)
  Equal when every part is equal.
- **RateLimiter.RateDecision** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L52)
  Combines the hashes of the parts in order.
- **RateLimiter.RateDecision** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/rate-limiter.nra#L60)
  Text such as "RateDecision(allowed, remaining 2, retry after PT0S, reset after PT0.1S)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
