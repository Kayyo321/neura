# `Std.Net.Resilience.GcraLimiter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.GcraLimiter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.GcraLimiter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **GcraLimiter** · [`export const ContentionCode = "resilience.rate_store_contention"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L30)
  The error code after too many lost compare-and-set races.
- **GcraLimiter** · [`export let GcraLimiter = class implements limiters.RateLimiter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L34)
  A GCRA limiter over a rate store. Tagged `#Final`, `#NotThreadSafe`.
- **GcraLimiter.GcraLimiter** · [`export let init = func(rate: int, period: durations.Duration, burst_size: int, state: stores.RateStore, source: Clock, max_attempts: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L49)
  A limiter of `rate` requests per `period` with bursts of `burst`, over `state`. Traps with "invalid argument: <name>" unless `rate`, `burst`, and `max_attempts` are positive and `period / rate` is at least one nanosecond.
- **GcraLimiter.GcraLimiter** · [`export let emission_interval = func -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L71)
  The emission interval `T`.
- **GcraLimiter.GcraLimiter** · [`export let burst_tolerance = func -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L74)
  The tolerance `tau = T * (burst - 1)`.
- **GcraLimiter.GcraLimiter** · [`export let try_acquire = func(key: string, permits: int) -> Result<limiters.RateDecision>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L79)
  Takes `permits` for `key`. `permits` must be from 1 through the burst, else the call traps with "invalid argument: permits".
- **GcraLimiter.GcraLimiter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L110)
  Text such as "GcraLimiter(interval PT0.1S, burst 3)".
- **GcraLimiter** · [`export let in_memory = func(rate: int, period: durations.Duration, burst: int, clock: Clock) -> GcraLimiter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L116)
  A limiter of `rate` per `period` with bursts of `burst`, keeping its state in a new in-memory store. Tagged `#NoDiscard`.
- **GcraLimiter** · [`export let distributed = func(rate: int, period: durations.Duration, burst: int, store: stores.RateStore, clock: Clock) -> GcraLimiter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/gcra-limiter.nra#L122)
  A limiter of `rate` per `period` with bursts of `burst` over a shared store, retrying a lost compare-and-set up to 8 times. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
