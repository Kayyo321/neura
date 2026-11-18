# `Std.Net.Resilience.LoadBalancer`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Resilience.LoadBalancer`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Resilience.LoadBalancer`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **LoadBalancer** · [`export const NoEndpointsCode = "resilience.no_endpoints"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L15)
  The error code of a pick from a balancer with no endpoints.
- **LoadBalancer** · [`export let LoadBalancer = interface<Endpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L18)
  Chooses endpoints of type `Endpoint` for requests.
- **LoadBalancer** · [`export let Selection = class<Endpoint>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L34)
  One pick: the endpoint and its position in the balancer's list. Tagged `#Final`, `#NoDiscard`.
- **LoadBalancer.Selection** · [`export last index: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L36)
  The position of the endpoint in the balancer's list.
- **LoadBalancer.Selection** · [`export last endpoint: Endpoint`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L38)
  The chosen endpoint.
- **LoadBalancer.Selection** · [`export let init = func(position: int, chosen: Endpoint)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L41)
  The pick of `chosen` at `position`.
- **LoadBalancer.Selection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/resilience/load-balancer.nra#L47)
  Text such as "Selection(index 2)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
