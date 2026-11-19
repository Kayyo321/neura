# `Std.Net.Resilience.Backoff`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.Backoff`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.Backoff`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Backoff** · [`export let Jitter = enum (None, Full, Equal)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L22)
  How the delay is spread below its ceiling.
- **Backoff** · [`export let Backoff = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L26)
  Exponential backoff with jitter. Tagged `#Final`, `#NotThreadSafe`.
- **Backoff.Backoff** · [`export let init = func(initial: durations.Duration, maximum: durations.Duration, spread: Jitter, source: randoms.Random)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L38)
  Backoff from `initial` doubling up to `maximum`. Traps with "invalid argument: <name>" unless `initial` is positive and `maximum` is at least `initial`.
- **Backoff.Backoff** · [`export let ceiling = func(attempt: int) -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L54)
  The ceiling `min(cap, base * 2^(attempt - 1))`; `attempt` must be at least 1, else the call traps with "invalid argument: attempt".
- **Backoff.Backoff** · [`export let delay = func(attempt: int) -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L66)
  The delay before retry `attempt`, drawing one random value for `Full` and `Equal` jitter.
- **Backoff.Backoff** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L76)
  Text such as "Backoff(PT0.1S to PT10S, Full jitter)".
- **Backoff** · [`export let exponential = func(initial: durations.Duration, maximum: durations.Duration, jitter: Jitter, random: randoms.Random) -> Backoff`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/backoff.nra#L86)
  Exponential backoff with jitter. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
