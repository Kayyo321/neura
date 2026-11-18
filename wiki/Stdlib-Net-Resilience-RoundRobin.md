# `Std.Net.Resilience.RoundRobin`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.RoundRobin`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.RoundRobin`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **RoundRobin** · [`export let RoundRobin = class<Endpoint> implements balancers.LoadBalancer<Endpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L13)
  Round-robin over a fixed endpoint list. Tagged `#Final`, `#NotThreadSafe`.
- **RoundRobin.RoundRobin** · [`export let init = func(targets: Endpoint[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L22)
  A balancer over a copy of `targets`; the first pick is the first endpoint.
- **RoundRobin.RoundRobin** · [`export let pick = func -> Result<balancers.Selection<Endpoint>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L30)
  The endpoint after the previous pick.
- **RoundRobin.RoundRobin** · [`export let report = func(selection: balancers.Selection<Endpoint>, latency: durations.Duration, success: bool) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L42)
  Ends the pending request; latency and outcome do not affect round-robin.
- **RoundRobin.RoundRobin** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L51)
  The number of endpoints.
- **RoundRobin.RoundRobin** · [`export let pending = func(index: int) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L54)
  The pending requests of endpoint `index`.
- **RoundRobin.RoundRobin** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L57)
  Text such as "RoundRobin(3 endpoints)".
- **RoundRobin** · [`export let of = func<Endpoint>(endpoints: Endpoint[list]) -> RoundRobin<Endpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/round-robin.nra#L62)
  A round-robin balancer over a copy of `endpoints`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
