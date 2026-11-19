# `Std.Net.Realtime.Sse.Parser`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Sse.Parser`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Sse.Parser`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Parser** · [`export const DefaultMaxLineBytes: int = 1048576`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L27)
  The default longest line, in bytes: 1 MiB.
- **Parser** · [`export const DefaultMaxEventBytes: int = 16777216`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L29)
  The default largest event data, in bytes: 16 MiB.
- **Parser** · [`export const MaxRetryMillis: int64 = 1000000000000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L32)
  The largest reconnection time a `retry` field can set, in milliseconds (10^12, about 31.7 years); longer digit strings saturate here instead of overflowing.
- **Parser** · [`export const LimitErrorCode: string = "sse.limit"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L34)
  The error code of a limit failure.
- **Parser** · [`export let EventStreamParser = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L38)
  An incremental event-stream parser holding the WHATWG data, event type and last event ID buffers. Tagged `#Final`, `#NotThreadSafe`.
- **Parser.EventStreamParser** · [`export last max_line_bytes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L40)
  The longest line accepted, in bytes.
- **Parser.EventStreamParser** · [`export last max_event_bytes: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L42)
  The largest data of one event accepted, in bytes.
- **Parser.EventStreamParser** · [`export let last_event_id: string { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L44)
  The last event ID string: the ID buffer as of the most recent dispatch.
- **Parser.EventStreamParser** · [`export let reconnection_time: Optional<int64> = Optional<int64>.None { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L48)
  The reconnection time in milliseconds that the latest valid `retry` field set, if any.
- **Parser.EventStreamParser** · [`export let init = func(initial_id: string, line_limit: int, event_limit: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L76)
  A parser with the limits whose ID buffer starts as `initial_id` (empty for a new source). A limit below 1 traps with "invalid argument: limit below 1".
- **Parser.EventStreamParser** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L87)
  `EventStreamParser(max line N bytes, max event M bytes)`; the buffered text is not shown.
- **Parser.EventStreamParser** · [`export let feed = func(chunk: uint8[list]) -> Result<SseEvent[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L93)
  Parses the chunk and returns the events it completes, in order, or a `sse.limit` error. Tagged `#NoDiscard`.
- **Parser.EventStreamParser** · [`export let finish = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L115)
  Ends the stream: the unfinished line and the undispatched block are discarded, and the parser is ready for a new stream with the same last event ID.
- **Parser** · [`export let create = func -> EventStreamParser`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L253)
  A parser for a new stream with the default limits and no last event ID. Tagged `#NoDiscard`.
- **Parser** · [`export let resuming = func(last_event_id: string) -> EventStreamParser`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L258)
  A parser for a resumed stream: its ID buffer starts as `last_event_id`, so an event without an `id` field keeps the cursor. Tagged `#NoDiscard`.
- **Parser** · [`export let parse_retry = func(value: string) -> Optional<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/parser.nra#L264)
  The value of a `retry` field: one or more ASCII digits read in base ten, saturating at `MaxRetryMillis`; `None` for anything else, including the empty value. Tagged `#NoDiscard`, `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
