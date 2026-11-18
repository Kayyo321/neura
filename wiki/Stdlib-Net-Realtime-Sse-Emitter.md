# `Std.Net.Realtime.Sse.Emitter`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Sse.Emitter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Sse.Emitter`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Emitter** · [`export const ErrorCode: string = "sse.emit"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L21)
  The error code of an event that cannot be written.
- **Emitter** · [`export const ContentType: string = "text/event-stream"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L23)
  The media type of an event stream.
- **Emitter** · [`export let format = func(event: SseEvent) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L28)
  The text of one event. The identifier is written only when it is not empty; use `format_fields` to send an empty `id` line that resets the client's cursor. Tagged `#NoDiscard`.
- **Emitter** · [`export let format_fields = func(event_type: string, data: string, id: Optional<string>, retry: Optional<int64>) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L36)
  The text of one event from its parts. An empty type or `message` writes no `event:` line; `id` `Some("")` writes a bare `id` line, which resets the last event ID. Tagged `#NoDiscard`.
- **Emitter** · [`export let comment = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L71)
  Comment lines, ignored by the parser: each line of the text after `: `. Tagged `#NoDiscard`.
- **Emitter** · [`export let keep_alive = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L81)
  A keep-alive: a single empty comment line. Tagged `#NoDiscard`.
- **Emitter** · [`export let retry_only = func(millis: int64) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L86)
  A block that only sets the client's reconnection time; it dispatches no event. A negative time fails. Tagged `#NoDiscard`.
- **Emitter** · [`export let EventWriter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L95)
  Writes events to a byte sink, such as the write method of a stream or a response body. Tagged `#Final`, `#NotThreadSafe`.
- **Emitter.EventWriter** · [`export let init = func(target: func(uint8[list]) -> Result<Unit>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L100)
  Writes through the sink.
- **Emitter.EventWriter** · [`export let send = func(event: SseEvent) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L106)
  Writes one event, or fails without writing when it cannot be formatted. Tagged `#NoDiscard`.
- **Emitter.EventWriter** · [`export let send_data = func(data: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L115)
  Writes a block of `data` lines with the default type and no identifier. Tagged `#NoDiscard`.
- **Emitter.EventWriter** · [`export let send_comment = func(text: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L119)
  Writes a comment. Tagged `#NoDiscard`.
- **Emitter.EventWriter** · [`export let send_keep_alive = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L123)
  Writes a keep-alive comment. Tagged `#NoDiscard`.
- **Emitter.EventWriter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L126)
  `EventWriter`.
- **Emitter** · [`export let TypedEventWriter = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L131)
  Writes values of `T` as events through an encoder, for typed streams such as model tokens. Tagged `#Final`, `#NotThreadSafe`.
- **Emitter.TypedEventWriter** · [`export let init = func(target: EventWriter, encoder: func(T) -> Result<SseEvent>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L138)
  Writes through `target`, encoding each value with `encoder`.
- **Emitter.TypedEventWriter** · [`export let send = func(value: T) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L145)
  Encodes and writes one value; an encoder error is returned and nothing is written. Tagged `#NoDiscard`.
- **Emitter.TypedEventWriter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L153)
  `TypedEventWriter`.
- **Emitter** · [`export let split_lines = func(text: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/emitter.nra#L158)
  The lines of the text split at CRLF, LF and CR; the empty text is one empty line. Tagged `#NoDiscard`, `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
