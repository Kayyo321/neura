# `Std.Net.Resilience.RetryFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.RetryFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.RetryFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RetryFilter** · [`export let RetryFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L21)
  Retries the next service with backoff under a budget. Tagged `#Final`, `#NotThreadSafe`.
- **RetryFilter.RetryFilter** · [`export let init = func(attempts: int, retry_if: func(Result<Resp>) -> bool, delays: backoffs.Backoff, limit: budgets.RetryBudget, source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L39)
  A retry filter. `attempts` must be at least 1, else the constructor traps with "invalid argument: max_attempts".
- **RetryFilter.RetryFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L52)
  Calls `next` and retries as described for the module.
- **RetryFilter.RetryFilter** · [`export let retries = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L68)
  The retries made so far.
- **RetryFilter.RetryFilter** · [`export let budget_refusals = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L71)
  The retries the budget refused so far.
- **RetryFilter.RetryFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L74)
  Text such as "RetryFilter(3 attempts)".
- **RetryFilter** · [`export let on_error = func<Req, Resp>(max_attempts: int, backoff: backoffs.Backoff, budget: budgets.RetryBudget, clock: Clock) -> RetryFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L79)
  A filter that retries every `Err`. Tagged `#NoDiscard`.
- **RetryFilter** · [`export let when = func<Req, Resp>(max_attempts: int, should_retry: func(Result<Resp>) -> bool, backoff: backoffs.Backoff, budget: budgets.RetryBudget, clock: Clock) -> RetryFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-filter.nra#L88)
  A filter that retries the results `should_retry` returns true for. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
