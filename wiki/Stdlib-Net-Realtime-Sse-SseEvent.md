# `Std.Net.Realtime.Sse.SseEvent`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Sse.SseEvent`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Sse.SseEvent`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SseEvent** · [`export const DefaultType: string = "message"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L15)
  The type an event has when its stream names none.
- **SseEvent** · [`export let SseEvent = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L19)
  An immutable event: type, data, last event ID and an optional reconnection time. Tagged `#Final`, `#Immutable`.
- **SseEvent.SseEvent** · [`export last event_type: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L21)
  The event type; `message` by default.
- **SseEvent.SseEvent** · [`export last data: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L23)
  The data: the `data` lines joined with LF.
- **SseEvent.SseEvent** · [`export last id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L25)
  The last event ID at dispatch; empty when none.
- **SseEvent.SseEvent** · [`export last retry: Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L27)
  The reconnection time in milliseconds set by this event's block, if any.
- **SseEvent.SseEvent** · [`export let init = func(kind: string, payload: string, identifier: string, retry_millis: Optional<int64>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L30)
  Stores the fields.
- **SseEvent.SseEvent** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L38)
  Equal when the type, data, identifier and reconnection time are equal.
- **SseEvent.SseEvent** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L47)
  Combines the hashes of the four fields in order.
- **SseEvent.SseEvent** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L58)
  `SseEvent(type, id "...", data "...")`, with the reconnection time when set.
- **SseEvent** · [`export let message = func(payload: string) -> SseEvent`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L72)
  An event of type `message` with the data and no identifier or reconnection time. Tagged `#NoDiscard`.
- **SseEvent** · [`export let of = func(kind: string, payload: string, identifier: string) -> SseEvent`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/sse-event.nra#L77)
  An event with the type, data and identifier and no reconnection time. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
