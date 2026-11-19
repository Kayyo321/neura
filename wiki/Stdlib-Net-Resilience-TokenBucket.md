# `Std.Net.Resilience.TokenBucket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.TokenBucket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.TokenBucket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TokenBucket** · [`export let TokenBucket = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra#L19)
  One token bucket over an injected clock. Tagged `#Final`, `#NotThreadSafe`.
- **TokenBucket.TokenBucket** · [`export let init = func(size: int, tokens: int, refill_period: durations.Duration, source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra#L35)
  A full bucket. `capacity` and `tokens` must be positive and `period` positive, else the constructor traps with "invalid argument: <name>".
- **TokenBucket.TokenBucket** · [`export let try_acquire = func(permits: int) -> Result<limiters.RateDecision>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra#L55)
  Takes `permits` tokens when the bucket holds them. `permits` must be from 1 through the capacity, else the call traps with "invalid argument: permits". Err only when the clock fails. Tagged `#NoDiscard`.
- **TokenBucket.TokenBucket** · [`export let available = func -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra#L70)
  The whole tokens available now. Err only when the clock fails. Tagged `#NoDiscard`.
- **TokenBucket.TokenBucket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra#L76)
  Text such as "TokenBucket(capacity 10)".
- **TokenBucket** · [`export let create = func(capacity: int, tokens: int, period: durations.Duration, clock: Clock) -> TokenBucket`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/token-bucket.nra#L109)
  A full bucket of `capacity` tokens refilled at `tokens` per `period`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
