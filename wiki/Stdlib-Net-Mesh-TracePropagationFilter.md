# `Std.Net.Mesh.TracePropagationFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.TracePropagationFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.TracePropagationFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TracePropagationFilter** · [`export let TracePropagationFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L25)
  Propagates the trace context of each request to the next service. Tagged `#Final`, `#NotThreadSafe`.
- **TracePropagationFilter.TracePropagationFilter** · [`export let init = func(reader: func(Req, string) -> Optional<string>, writer: func(Req, string, string) -> Req, source: Random, sample: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L43)
  A filter over the two header functions and a random source.
- **TracePropagationFilter.TracePropagationFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L52)
  Extracts, continues or starts, injects, and calls `next`.
- **TracePropagationFilter.TracePropagationFilter** · [`export let last_sent = func -> contexts.TraceContext`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L65)
  The context sent with the last request (empty before the first). Tagged `#NoDiscard`.
- **TracePropagationFilter.TracePropagationFilter** · [`export let continued = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L69)
  Requests that continued a received trace so far. Tagged `#NoDiscard`.
- **TracePropagationFilter.TracePropagationFilter** · [`export let started = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L73)
  Requests that started a new trace so far. Tagged `#NoDiscard`.
- **TracePropagationFilter.TracePropagationFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L76)
  Text such as "TracePropagationFilter(3 continued, 1 started)".
- **TracePropagationFilter** · [`export let of = func<Req, Resp>(get_header: func(Req, string) -> Optional<string>, with_header: func(Req, string, string) -> Req, random: Random, sample_new: bool) -> TracePropagationFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/trace-propagation-filter.nra#L83)
  A filter reading headers with `get_header`, writing them with `with_header`, drawing ids from `random`, and sampling new traces when `sample_new`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
