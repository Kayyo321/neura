# `Std.Net.Resilience.CircuitBreakerFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.CircuitBreakerFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.CircuitBreakerFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CircuitBreakerFilter** · [`export const OpenCode = "resilience.circuit_open"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L18)
  The error code of a request rejected by an open breaker.
- **CircuitBreakerFilter** · [`export let CircuitBreakerFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L22)
  Guards the next service with a circuit breaker. Tagged `#Final`, `#NotThreadSafe`.
- **CircuitBreakerFilter.CircuitBreakerFilter** · [`export let init = func(guard: breakers.CircuitBreaker, classify: func(Result<Resp>) -> bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L29)
  A filter over `guard` that counts the results `classify` returns true for as failures.
- **CircuitBreakerFilter.CircuitBreakerFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L36)
  Calls `next` when the breaker admits the request and reports the outcome; otherwise returns the `resilience.circuit_open` error without calling it.
- **CircuitBreakerFilter.CircuitBreakerFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L51)
  Text such as "CircuitBreakerFilter(CircuitBreaker(Open))".
- **CircuitBreakerFilter** · [`export let of = func<Req, Resp>(breaker: breakers.CircuitBreaker) -> CircuitBreakerFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L56)
  A filter over `breaker` that counts every `Err` as a failure. Tagged `#NoDiscard`.
- **CircuitBreakerFilter** · [`export let classified = func<Req, Resp>(breaker: breakers.CircuitBreaker, classify: func(Result<Resp>) -> bool) -> CircuitBreakerFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L61)
  A filter over `breaker` with a failure classifier. Tagged `#NoDiscard`.
- **CircuitBreakerFilter** · [`export let is_err = func<Resp>(outcome: Result<Resp>) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/circuit-breaker-filter.nra#L66)
  Whether `outcome` is an `Err`. Tagged `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
