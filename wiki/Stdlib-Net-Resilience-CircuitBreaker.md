# `Std.Net.Resilience.CircuitBreaker`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.CircuitBreaker`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.CircuitBreaker`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CircuitBreaker** · [`export let CircuitState = enum (Closed, Open, HalfOpen)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L21)
  The three breaker states.
- **CircuitBreaker** · [`export let SlidingWindow = enum (Count(int), Time { buckets: int, span: durations.Duration })`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L25)
  The window of recent outcomes the failure rate is computed over: the last `Count` calls, or the calls of the last `buckets` time buckets of length `span`.
- **CircuitBreaker** · [`export let CircuitBreaker = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L29)
  A failure-rate circuit breaker over an injected clock. Tagged `#Final`, `#NotThreadSafe`.
- **CircuitBreaker.CircuitBreaker** · [`export let init = func(shape: SlidingWindow, failure_rate_threshold: int, minimum_calls: int, open_duration: durations.Duration, half_open_calls: int, source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L74)
  A closed breaker. `threshold` is a percentage from 1 through 100; `minimum_calls`, `half_open_calls`, and the window size or bucket count must be positive; `open_duration` and a bucket span must be positive. A violation traps with "invalid argument: <name>".
- **CircuitBreaker.CircuitBreaker** · [`export let state = func -> CircuitState`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L120)
  The state as last changed; reading it does not move Open to HalfOpen (`try_acquire` does).
- **CircuitBreaker.CircuitBreaker** · [`export let try_acquire = func -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L126)
  Asks to make a call. Closed admits it; Open rejects it until the open duration has passed and then moves to HalfOpen; HalfOpen admits up to `half_open_calls` trials. An admitted call must report with `record_success` or `record_failure`. Err only when the clock fails. Tagged `#NoDiscard`.
- **CircuitBreaker.CircuitBreaker** · [`export let record_success = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L147)
  Records a call that succeeded.
- **CircuitBreaker.CircuitBreaker** · [`export let record_failure = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L150)
  Records a call that failed.
- **CircuitBreaker.CircuitBreaker** · [`export let window_calls = func -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L154)
  The calls in the closed window, after dropping expired time buckets. Tagged `#NoDiscard`.
- **CircuitBreaker.CircuitBreaker** · [`export let window_failures = func -> Result<int>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L161)
  The failures in the closed window, after dropping expired time buckets. Tagged `#NoDiscard`.
- **CircuitBreaker.CircuitBreaker** · [`export let reset = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L167)
  Closes the breaker and empties the window.
- **CircuitBreaker.CircuitBreaker** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L173)
  Text such as "CircuitBreaker(Closed)".
- **CircuitBreaker** · [`export let count_based = func(size: int, failure_rate_threshold: int, minimum_calls: int, open_duration: durations.Duration, half_open_calls: int, clock: Clock) -> CircuitBreaker`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L317)
  A breaker over the last `size` calls. Tagged `#NoDiscard`.
- **CircuitBreaker** · [`export let time_based = func(buckets: int, span: durations.Duration, failure_rate_threshold: int, minimum_calls: int, open_duration: durations.Duration, half_open_calls: int, clock: Clock) -> CircuitBreaker`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker.nra#L324)
  A breaker over the calls of the last `buckets` buckets of length `span`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
