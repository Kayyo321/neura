# `Std.Net.Filter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Filter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Filter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Filter** · [`export let Filter = interface<Req, Resp, In, Out>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L15)
  Middleware that handles a `Req` with the help of a `Service<In, Out>`.
- **Filter** · [`export let SimpleFilter = interface<Req, Resp>:Filter<Req, Resp, Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L21)
  A filter that passes requests and responses of the same types through.
- **Filter** · [`export let FilteredService = class<Req, Resp, In, Out> implements Service<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L25)
  A filter bound to the service it calls: a service again. Tagged `#Final`.
- **Filter.FilteredService** · [`export let init = func(outer: Filter<Req, Resp, In, Out>, inner: Service<In, Out>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L32)
  Binds `outer` to `inner`.
- **Filter.FilteredService** · [`export let call = func(request: Req) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L38)
  Applies the filter with the bound service.
- **Filter.FilteredService** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L41)
  The fixed text "FilteredService".
- **Filter** · [`export let ComposedFilter = class<A, B, C, D, E, F> implements Filter<A, B, E, F>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L46)
  Two filters run one inside the other: `outer` sees the request first and `inner` calls the service. Tagged `#Final`.
- **Filter.ComposedFilter** · [`export let init = func(first: Filter<A, B, C, D>, second: Filter<C, D, E, F>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L53)
  Composes `first` around `second`.
- **Filter.ComposedFilter** · [`export let apply = func(request: A, next: Service<E, F>) -> Result<B>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L59)
  Applies `outer` with `inner` bound to `next`.
- **Filter.ComposedFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L63)
  The fixed text "ComposedFilter".
- **Filter** · [`export let FilterChain = class<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L69)
  Simple filters run in list order around a service: the first filter added sees each request first and each response last. Tagged `#Final`, `#NotThreadSafe`.
- **Filter.FilterChain** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L74)
  An empty chain.
- **Filter.FilterChain** · [`export let add = func(filter: SimpleFilter<Req, Resp>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L78)
  Appends a filter; it runs after the filters added before it.
- **Filter.FilterChain** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L83)
  The number of filters.
- **Filter.FilterChain** · [`export let then = func(service: Service<Req, Resp>) -> Service<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L88)
  The service that runs every filter, in order, around `service`. Later changes to the chain do not affect a service already built. Tagged `#NoDiscard`.
- **Filter.FilterChain** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L97)
  The text "FilterChain(N filters)".
- **Filter** · [`export let bind = func<Req, Resp, In, Out>(filter: Filter<Req, Resp, In, Out>, service: Service<In, Out>) -> Service<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L102)
  `filter` bound to `service`. Tagged `#NoDiscard`.
- **Filter** · [`export let compose = func<A, B, C, D, E, F>(outer: Filter<A, B, C, D>, inner: Filter<C, D, E, F>) -> Filter<A, B, E, F>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L107)
  `outer` around `inner`. Tagged `#NoDiscard`.
- **Filter** · [`export let chain = func<Req, Resp> -> FilterChain<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/filter.nra#L112)
  A new empty chain. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
