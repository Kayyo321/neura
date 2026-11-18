# `Std.Net.Mesh.TraceContext`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.TraceContext`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.TraceContext`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TraceContext** · [`export const TraceParentHeader = "traceparent"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L23)
  The `traceparent` header name.
- **TraceContext** · [`export const TraceStateHeader = "tracestate"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L25)
  The `tracestate` header name.
- **TraceContext** · [`export const BaggageHeader = "baggage"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L27)
  The `baggage` header name.
- **TraceContext** · [`export let TraceContext = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L31)
  The propagated context of one request. Tagged `#Final`.
- **TraceContext.TraceContext** · [`export last parent: Optional<parents.TraceParent>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L33)
  The parent span, absent when the request carried no valid `traceparent`.
- **TraceContext.TraceContext** · [`export last state: states.TraceState`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L35)
  The vendor state (empty without a valid parent).
- **TraceContext.TraceContext** · [`export last baggage: baggages.Baggage`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L37)
  The application baggage.
- **TraceContext.TraceContext** · [`export let init = func(trace_parent: Optional<parents.TraceParent>, trace_state: states.TraceState, items: baggages.Baggage)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L40)
  A context with every part given.
- **TraceContext.TraceContext** · [`export let has_parent = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L49)
  Whether a valid parent was received. Tagged `#NoDiscard`.
- **TraceContext.TraceContext** · [`export let next_hop = func(random: Random, sample_new: bool) -> TraceContext`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L58)
  The context to send on: with a parent, the same trace with a new random span id as parent id and the state kept; without one, a new trace (sampled when `sample_new`) with an empty state. The baggage is kept either way. Tagged `#NoDiscard`.
- **TraceContext.TraceContext** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L66)
  Equal when the parent, state and baggage are.
- **TraceContext.TraceContext** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L75)
  Combines the parts' hashes.
- **TraceContext.TraceContext** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L78)
  Text such as "TraceContext(00-...-01, rojo=1, Baggage(2 members))".
- **TraceContext** · [`export let empty = func -> TraceContext`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L86)
  An empty context: no parent, state or baggage. Tagged `#NoDiscard`.
- **TraceContext** · [`export let extract = func<Req>(request: Req, get_header: func(Req, string) -> Optional<string>) -> TraceContext`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L91)
  The context carried by `request`, read through `get_header` as described for the module. Tagged `#NoDiscard`.
- **TraceContext** · [`export let inject = func<Req>(context: TraceContext, request: Req, with_header: func(Req, string, string) -> Req) -> Req`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-context.nra#L141)
  `request` with the context's headers written through `with_header`: `traceparent` (removed, by an empty value, when there is no parent), `tracestate` (removed when the state is empty) and `baggage` (removed when the baggage is empty). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
