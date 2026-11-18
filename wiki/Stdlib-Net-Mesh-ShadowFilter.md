# `Std.Net.Mesh.ShadowFilter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.ShadowFilter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.ShadowFilter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ShadowFilter** · [`export const AllBasisPoints: int = 10000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L43)
  The rate that mirrors every request, in basis points.
- **ShadowFilter** · [`export let ShadowFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L47)
  Mirrors requests to a channel that a shadow worker drains; never waits. Tagged `#Final`, `#NotThreadSafe`.
- **ShadowFilter.ShadowFilter** · [`export let init = func(queue: channels.Sender<Req>, basis_points: int, source: Random)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L61)
  A filter mirroring `basis_points` of 10000 requests to `queue`. A rate outside 0 to 10000 traps with "invalid argument: basis_points".
- **ShadowFilter.ShadowFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L69)
  Offers the request to the channel when selected, then calls `next` and returns its answer.
- **ShadowFilter.ShadowFilter** · [`export let mirrored = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L88)
  Requests handed to the channel so far. Tagged `#NoDiscard`.
- **ShadowFilter.ShadowFilter** · [`export let dropped = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L92)
  Requests selected but dropped so far. Tagged `#NoDiscard`.
- **ShadowFilter.ShadowFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L95)
  Text such as "ShadowFilter(500 bp, 3 mirrored, 1 dropped)".
- **ShadowFilter** · [`export let ConcurrentShadowFilter = class<Req, Resp> implements SimpleFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L101)
  Mirrors requests to a shadow task that runs beside the primary call; returns after both. Tagged `#Final`, `#NotThreadSafe`.
- **ShadowFilter.ConcurrentShadowFilter** · [`export let init = func(target: func(Req) -> Result<Resp>, basis_points: int, source: Random)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L115)
  A filter mirroring `basis_points` of 10000 requests to `target`. A rate outside 0 to 10000 traps with "invalid argument: basis_points".
- **ShadowFilter.ConcurrentShadowFilter** · [`export let apply = func(request: Req, next: Service<Req, Resp>) -> Result<Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L124)
  When selected, spawns the shadow call, calls `next` in this task, waits for the shadow, and returns the primary answer; otherwise just calls `next`.
- **ShadowFilter.ConcurrentShadowFilter** · [`export let mirrored = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L146)
  Shadow calls made so far. Tagged `#NoDiscard`.
- **ShadowFilter.ConcurrentShadowFilter** · [`export let shadow_failures = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L150)
  Shadow calls that returned Err so far. Tagged `#NoDiscard`.
- **ShadowFilter.ConcurrentShadowFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L153)
  Text such as "ConcurrentShadowFilter(500 bp, 3 mirrored, 0 failed)".
- **ShadowFilter** · [`export let WorkerReport = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L159)
  What a shadow worker did before its channel closed. Tagged `#Final`, `#Immutable`.
- **ShadowFilter.WorkerReport** · [`export last handled: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L161)
  Requests passed to the shadow function.
- **ShadowFilter.WorkerReport** · [`export last failed: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L163)
  Of those, the calls that returned Err.
- **ShadowFilter.WorkerReport** · [`export let init = func(count: int64, failures: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L166)
  A report.
- **ShadowFilter.WorkerReport** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L172)
  Equal when both counts are.
- **ShadowFilter.WorkerReport** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L181)
  Combines the counts.
- **ShadowFilter.WorkerReport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L185)
  Text such as "WorkerReport(3 handled, 1 failed)".
- **ShadowFilter** · [`export let run_worker = func<Req, Resp>(queue: channels.Receiver<Req>, shadow: func(Req) -> Result<Resp>) -> WorkerReport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L192)
  Calls `shadow` for each request received from `queue`, in order, until the channel is closed (or this task is cancelled); run it with `spawn` in the scope that serves requests. Tagged `#NoDiscard`.
- **ShadowFilter** · [`export let of_percent = func<Req, Resp>(queue: channels.Sender<Req>, percent: int, random: Random) -> ShadowFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L221)
  A non-blocking filter mirroring `percent` % (0 to 100) of requests to `queue`. Tagged `#NoDiscard`.
- **ShadowFilter** · [`export let concurrent_percent = func<Req, Resp>(shadow: func(Req) -> Result<Resp>, percent: int, random: Random) -> ConcurrentShadowFilter<Req, Resp>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/shadow-filter.nra#L231)
  A concurrent filter mirroring `percent` % (0 to 100) of requests to `shadow`. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
