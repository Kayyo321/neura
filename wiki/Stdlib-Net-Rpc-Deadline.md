# `Std.Net.Rpc.Deadline`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Deadline`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Deadline`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Deadline** · [`export let Deadline = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L23)
  A deadline as a monotonic reading of a clock. Tagged `#Final`.
- **Deadline.Deadline** · [`export last at: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L25)
  The monotonic reading at which the deadline passes.
- **Deadline.Deadline** · [`export let init = func(when: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L28)
  A deadline at monotonic reading `when`.
- **Deadline.Deadline** · [`export let remaining = func(clock: Clock) -> Result<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L34)
  The time left on `clock`: zero or negative once passed. Tagged `#NoDiscard`.
- **Deadline.Deadline** · [`export let is_expired = func(clock: Clock) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L38)
  Whether the deadline has passed on `clock`. Tagged `#NoDiscard`.
- **Deadline.Deadline** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L44)
  Equal when the readings are.
- **Deadline.Deadline** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L47)
  The reading's hash.
- **Deadline.Deadline** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L50)
  `Deadline(at ...)`.
- **Deadline** · [`export let after = func(clock: Clock, budget: Duration) -> Result<Deadline>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L55)
  The deadline `budget` after now on `clock`. Tagged `#NoDiscard`.
- **Deadline** · [`export let of_request = func(clock: Clock, request: calls.RpcRequest) -> Result<optional.Optional<Deadline>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L60)
  The deadline of an incoming request: its timeout after now, or `None` without a timeout. Tagged `#NoDiscard`.
- **Deadline** · [`export let DeadlineFilter = class implements SimpleFilter<calls.RpcRequest, calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L73)
  Propagates a deadline to the calls it filters. Before a call: when the deadline has passed it fails with DEADLINE_EXCEEDED without calling the next service; otherwise the request goes on with timeout `min(request timeout, remaining)`. After a call that returns once the deadline has passed, the result is replaced by DEADLINE_EXCEEDED. Tagged `#Final`.
- **Deadline.DeadlineFilter** · [`export last deadline: Deadline`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L77)
  The deadline of the enclosing work.
- **Deadline.DeadlineFilter** · [`export let init = func(time: Clock, limit: Deadline)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L80)
  A filter for `limit` on `time`.
- **Deadline.DeadlineFilter** · [`export let apply = func(request: calls.RpcRequest, next: Service<calls.RpcRequest, calls.RpcResponse>) -> Result<calls.RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L86)
  Checks the budget, forwards with the remaining timeout, and checks again after the call.
- **Deadline.DeadlineFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/deadline.nra#L106)
  The deadline.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
