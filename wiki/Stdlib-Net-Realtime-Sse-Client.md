# `Std.Net.Realtime.Sse.Client`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Sse.Client`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Sse.Client`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Client** · [`export const DefaultReconnectionMillis: int64 = 3000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L31)
  The reconnection time before any `retry` field, in milliseconds. The specification leaves it to the implementation ("a few seconds").
- **Client** · [`export const EventStreamType: string = "text/event-stream"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L33)
  The media type an event-stream response must have.
- **Client** · [`export let ReadyState = enum (Connecting, Open, Closed)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L36)
  The EventSource ready states.
- **Client** · [`export let SseRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L40)
  One connection attempt: the URL and the cursor to resume from. Tagged `#Final`, `#Immutable`.
- **Client.SseRequest** · [`export last url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L42)
  The event-stream URL.
- **Client.SseRequest** · [`export last last_event_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L44)
  The last event ID; empty when the request carries no `Last-Event-ID`.
- **Client.SseRequest** · [`export last attempt: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L46)
  The 1-based number of this attempt since the client was made.
- **Client.SseRequest** · [`export let init = func(target: string, cursor: string, number: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L49)
  Stores the fields.
- **Client.SseRequest** · [`export let header_lines = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L58)
  The request header lines: `Accept: text/event-stream`, `Cache-Control: no-store`, and `Last-Event-ID` when the cursor is not empty. Tagged `#NoDiscard`.
- **Client.SseRequest** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L69)
  Equal when the URL, cursor and attempt number are equal.
- **Client.SseRequest** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L77)
  Combines the three fields.
- **Client.SseRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L82)
  `SseRequest(url, Last-Event-ID "...", attempt n)`.
- **Client** · [`export let SseResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L89)
  The response to an attempt: status, media type, and the body as a chunk source that returns `None` at the end of the body. Tagged `#Final`.
- **Client.SseResponse** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L91)
  The HTTP status code.
- **Client.SseResponse** · [`export last content_type: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L93)
  The Content-Type header value.
- **Client.SseResponse** · [`export last body: func() -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L95)
  Reads the next chunk of the body.
- **Client.SseResponse** · [`export let init = func(code: int, media_type: string, reader: func() -> Result<Optional<uint8[list]>>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L98)
  Stores the fields.
- **Client.SseResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L105)
  `SseResponse(status, content type)`.
- **Client** · [`export let EventSourceClient = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L111)
  A reconnecting event-stream client. Not for use by several tasks at once. Tagged `#Final`, `#NotThreadSafe`.
- **Client.EventSourceClient** · [`export last url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L113)
  The event-stream URL.
- **Client.EventSourceClient** · [`export let state: ReadyState = ReadyState.Connecting { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L115)
  The ready state.
- **Client.EventSourceClient** · [`export let last_event_id: string { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L119)
  The last event ID string, sent as `Last-Event-ID` on reconnection.
- **Client.EventSourceClient** · [`export let reconnection_millis: int64 = DefaultReconnectionMillis { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L123)
  The current reconnection time in milliseconds.
- **Client.EventSourceClient** · [`export let failures: int = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L127)
  The number of consecutive failed fetches.
- **Client.EventSourceClient** · [`export let attempts: int = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L131)
  The number of fetches made so far.
- **Client.EventSourceClient** · [`export let max_attempts: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L135)
  The consecutive failed fetches after which `next` gives up; 0, the default, never gives up.
- **Client.EventSourceClient** · [`export let max_backoff_millis: int64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L138)
  The cap of exponential backoff in milliseconds; 0, the default, waits the reconnection time after every failure.
- **Client.EventSourceClient** · [`export let init = func(target: string, cursor: string, fetcher: func(SseRequest) -> Result<SseResponse>, timer: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L159)
  A client for `target` that resumes after `cursor` (empty for a new stream), fetching with `fetcher` and waiting on `timer`.
- **Client.EventSourceClient** · [`export let next = func -> Result<Optional<SseEvent>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L170)
  The next event, `None` once the stream has ended (status 204 or `close`), or an error that failed the connection. Tagged `#NoDiscard`.
- **Client.EventSourceClient** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L200)
  Closes the client: no further fetch is made and `next` returns `None`.
- **Client.EventSourceClient** · [`export let next_delay_millis = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L212)
  The wait before the next attempt: the reconnection time, doubled per consecutive failure up to `max_backoff_millis` when backoff is on. Tagged `#NoDiscard`.
- **Client.EventSourceClient** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L321)
  `EventSourceClient(url, state)`.
- **Client** · [`export let TypedEventSource = class<T> implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L328)
  Decodes each event of a client into a `T` with a decoding hook, such as a JSON decoder for streamed model tokens. Tagged `#Final`, `#NotThreadSafe`.
- **Client.TypedEventSource** · [`export last source: EventSourceClient`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L330)
  The client that supplies the events.
- **Client.TypedEventSource** · [`export let init = func(client: EventSourceClient, decoder: func(SseEvent) -> Result<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L335)
  Reads `client`, decoding with `decoder`.
- **Client.TypedEventSource** · [`export let next = func -> Result<Optional<T>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L343)
  The next decoded value, `None` at the end, or the client's or decoder's error. After a decoder error the client stays open and the next call reads the following event. Tagged `#NoDiscard`.
- **Client.TypedEventSource** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L357)
  Closes the client.
- **Client.TypedEventSource** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L360)
  `TypedEventSource(url)`.
- **Client** · [`export let connect = func(url: string, fetch: func(SseRequest) -> Result<SseResponse>, clock: Clock) -> EventSourceClient`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L365)
  A client for a new stream at `url`. Tagged `#NoDiscard`.
- **Client** · [`export let is_event_stream = func(content_type: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/sse/client.nra#L371)
  Whether a Content-Type value names `text/event-stream`, ignoring parameters, surrounding whitespace and ASCII case. Tagged `#NoDiscard`, `#Pure`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
