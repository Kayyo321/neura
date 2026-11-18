# `Std.Net.Resilience.RetryBudget`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.RetryBudget`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.RetryBudget`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RetryBudget** · [`export let RetryBudget = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L13)
  A ratio budget of retries to requests. Tagged `#Final`, `#NotThreadSafe`.
- **RetryBudget.RetryBudget** · [`export let init = func(ratio_percent: int, initial: int, maximum: int, open: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L25)
  A budget earning `percent` hundredths per request, starting with `initial` retries and holding at most `maximum`. Traps with "invalid argument: <name>" unless 0 <= percent, 0 <= initial <= maximum.
- **RetryBudget.RetryBudget** · [`export let deposit = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L39)
  Records one request.
- **RetryBudget.RetryBudget** · [`export let try_withdraw = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L47)
  Takes one retry from the balance; false, with nothing taken, when fewer than one is available.
- **RetryBudget.RetryBudget** · [`export let available = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L59)
  The whole retries available now.
- **RetryBudget.RetryBudget** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L62)
  Text such as "RetryBudget(20 percent, balance 1.50)", or "RetryBudget(unlimited)".
- **RetryBudget** · [`export let of = func(percent: int, initial: int, maximum: int) -> RetryBudget`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L74)
  A budget of `percent` retries per 100 requests, starting with `initial` and holding at most `maximum`. Tagged `#NoDiscard`.
- **RetryBudget** · [`export let unlimited = func -> RetryBudget`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/retry-budget.nra#L79)
  A budget that always allows a retry. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
