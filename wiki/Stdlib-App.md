# `Std.App`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.App`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.App`](#exported-declarations)
- [Server host](Server-Host)
- [Implementation decisions and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **App** · [`export let LogLevel = enum (Trace, Debug, Info, Warn, Error)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L41)
  The levels of `Context.log`, in increasing severity.
- **App** · [`export let Request = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L47)
  One HTTP request as the host received it. For `Context.serve` the body is complete: the context reads it, within the host's body limit (S-6), before it calls the handler. For `Context.serve_streamed` it is empty and the handler reads the body from a `BodyReader`. Tagged `#Final`.
- **App.Request** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L49)
  The method token, such as GET or POST.
- **App.Request** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L51)
  The request target as sent: the path and, after `?`, the query.
- **App.Request** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L53)
  The target up to its first `?`.
- **App.Request** · [`export last query: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L55)
  The target after its first `?`, without it; empty when there is none.
- **App.Request** · [`export last headers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L57)
  Header fields in arrival order, with lowercase names.
- **App.Request** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L59)
  The body bytes; empty when the request has no body.
- **App.Request** · [`export last client: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L61)
  The numeric address of the client, from the connection.
- **App.Request** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L63)
  "http" or "https", from the listener that accepted the connection.
- **App.Request** · [`export last request_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L65)
  The host's identifier of this request, as in its log records.
- **App.Request** · [`export let init = func(method_value: string, target_value: string, header_list: [string, string][list], body_bytes: uint8[list], client_value: string, scheme_value: string, id: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L68)
  Builds a request; the server host's values arrive through `Context.serve`.
- **App.Request** · [`export let header = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L90)
  The value of the first field named `name`, compared without ASCII case. Tagged `#NoDiscard`.
- **App.Request** · [`export let body_text = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L102)
  The body as UTF-8 text; invalid UTF-8 is an error. Tagged `#NoDiscard`.
- **App.Request** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L105)
  The method and target, such as "GET /parts?page=2".
- **App** · [`export let BodyReader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L110)
  Reads the body of a request handled by `Context.serve_streamed` as the client sends it. Tagged `#Final`, `#NotThreadSafe`.
- **App.BodyReader** · [`export let init = func()`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L115)
  Readers come from `Context.serve_streamed`.
- **App.BodyReader** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L122)
  Up to `max` further body bytes, waiting for the client to send them; an empty list at the end of the body. Fails when the body cannot be read to its end: the client left, the request deadline (S-8) passed, or the body went past S-6. Tagged `#NoDiscard`.
- **App.BodyReader** · [`export let read_all = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L144)
  The rest of the body. Tagged `#NoDiscard`.
- **App.BodyReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L163)
  A fixed description.
- **App** · [`export let BodyWriter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L168)
  Writes the body of a streamed response (`stream`) to the client as it is produced. Tagged `#Final`, `#NotThreadSafe`.
- **App.BodyWriter** · [`export let init = func()`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L170)
  Writers come from `Context.serve` for a streamed response.
- **App.BodyWriter** · [`export let write = func(content: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L176)
  Sends `content` to the client now, as one chunk; waits while the client reads slowly (backpressure). Fails when the client left or the request deadline (S-8) passed. Tagged `#NoDiscard`.
- **App.BodyWriter** · [`export let write_text = func(content: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L188)
  Sends `content` as UTF-8. Tagged `#NoDiscard`.
- **App.BodyWriter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L191)
  A fixed description.
- **App** · [`export let Response = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L198)
  An HTTP response: a status from 100 through 599, header fields, and the body bytes, or for a response made by `stream` a function that writes the body. The host writes Content-Length, Transfer-Encoding and Connection itself and ignores those fields here. Tagged `#Final`.
- **App.Response** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L200)
  The status code.
- **App.Response** · [`export last headers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L202)
  Header fields in the order they are sent.
- **App.Response** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L204)
  The body bytes, within the host's response limit (S-7); empty for a streamed response.
- **App.Response** · [`export let init = func(code: int, fields: [string, string][list], content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L211)
  Builds a response from its parts.
- **App.Response** · [`export let with_header = func(name: string, value: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L219)
  A copy with one more header field. Tagged `#NoDiscard`.
- **App.Response** · [`export let with_stream = func(function: func(BodyWriter) -> Result<Unit>) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L232)
  A copy whose body is written by `function` while the client receives it, instead of `body`; `stream` builds one directly. Tagged `#NoDiscard`.
- **App.Response** · [`export let is_streamed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L242)
  Whether the body is streamed by a function (`stream`). Tagged `#NoDiscard`.
- **App.Response** · [`export let write_stream = func(writer: BodyWriter) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L247)
  Writes a streamed body through `writer`; `Context.serve` calls it after sending the head. For a response that is not streamed it writes nothing. Tagged `#NoDiscard`.
- **App.Response** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L250)
  The status and the body size, such as "200 (5 bytes)".
- **App** · [`export let stream = func(status: int, fields: [string, string][list], producer: func(BodyWriter) -> Result<Unit>) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L263)
  A response whose body `producer` writes while the client receives it, sent with chunked transfer coding: a large download, server-sent events, a long report. The status and fields are sent first; the body is not buffered and S-7 does not apply to it; the request deadline (S-8) bounds it. When `producer` fails, the connection is closed without the end of the body, so the client sees it truncated. Tagged `#NoDiscard`.
- **App** · [`export let StreamingHandler = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L271)
  What `Context.serve_streamed` calls with the request, whose `body` is empty, and the reader of its body.
- **App** · [`export let FunctionStreamingHandler = class implements StreamingHandler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L278)
  A streaming handler that calls a function. Tagged `#Final`.
- **App.FunctionStreamingHandler** · [`export let init = func(function: func(Request, BodyReader) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L283)
  Wraps `function`.
- **App.FunctionStreamingHandler** · [`export let handle = func(request: Request, body: BodyReader) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L288)
  Calls the function.
- **App.FunctionStreamingHandler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L291)
  A fixed description.
- **App** · [`export let streaming_handler_of = func(function: func(Request, BodyReader) -> Result<Response>) -> StreamingHandler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L296)
  A streaming handler that calls `function` for each request. Tagged `#NoDiscard`.
- **App** · [`export let Handler = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L301)
  What `Context.serve` calls with the request. `Std.Net.Http.Router` implements it; `handler_of` adapts a function.
- **App** · [`export let FunctionHandler = class implements Handler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L308)
  A handler that calls a function. Tagged `#Final`.
- **App.FunctionHandler** · [`export let init = func(function: func(Request) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L313)
  Wraps `function`.
- **App.FunctionHandler** · [`export let handle = func(request: Request) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L318)
  Calls the function.
- **App.FunctionHandler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L321)
  A fixed description.
- **App** · [`export let handler_of = func(function: func(Request) -> Result<Response>) -> Handler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L326)
  A handler that calls `function` for each request. Tagged `#NoDiscard`.
- **App** · [`export let text = func(status: int, content: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L331)
  A response whose body is UTF-8 text, with Content-Type text/plain; charset=utf-8. Tagged `#NoDiscard`.
- **App** · [`export let html = func(status: int, content: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L336)
  A response whose body is HTML, with Content-Type text/html; charset=utf-8. Tagged `#NoDiscard`.
- **App** · [`export let json = func(status: int, encoded: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L341)
  A response whose body is JSON text the caller encoded, with Content-Type application/json. Tagged `#NoDiscard`.
- **App** · [`export let empty = func(status: int) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L345)
  A response with no body, such as 204 or a redirect with a Location field added. Tagged `#NoDiscard`.
- **App** · [`export let Context = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L360)
  The execution's view of its host: the assigned request, configuration and the log. Tagged `#Final`, `#NotThreadSafe`.
- **App.Context** · [`export let init = func()`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L365)
  Contexts come from `run`.
- **App.Context** · [`export let config = func(key: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L371)
  The host configuration value of `key`: from the host's --config options, its configuration file, or the environment under its prefix. None when it is not set or there is no host. Tagged `#NoDiscard`.
- **App.Context** · [`export let log = func(level: LogLevel, message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L380)
  Writes a JSON log record through the host, with this request's identifier. Without a host the record is dropped.
- **App.Context** · [`export let count = func(name: string, amount: int64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L392)
  Adds `amount` (zero or more) to the host counter `name`, shown on the admin listener's /metrics. A name is [A-Za-z_:][A-Za-z0-9_:]*, at most 128 bytes, and keeps the kind it was first used with. Tagged `#NoDiscard`.
- **App.Context** · [`export let gauge = func(name: string, value: int64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L396)
  Sets the host gauge `name` to `value`, shown on the admin listener's /metrics. Tagged `#NoDiscard`.
- **App.Context** · [`export let serve_streamed = func(handler: StreamingHandler) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L410)
  Like `serve`, but calls `handler` at once with an empty `Request.body` and a `BodyReader`, so the handler reads the body as the client sends it: a large upload is processed without holding it whole. What the handler leaves unread is discarded. Tagged `#NoDiscard`.
- **App.Context** · [`export let serve = func(handler: Handler) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L440)
  Receives the request the host assigned, calls `handler` and sends its response. A startup check is answered with 204 without calling the handler. A handler error is answered with status 500 and the body {"error":"internal"}, and the error is logged with the request identifier; it does not make `serve` fail. `serve` fails when there is no assigned request (for example under `neura run`), when it is called a second time, and when the response cannot be sent. Tagged `#NoDiscard`.
- **App.Context** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L545)
  A fixed description.
- **App** · [`export let run = func(serve: func(Context) -> Result<Unit>) -> program.ExitCode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L580)
  Builds the context, calls `serve` and turns its result into the exit code: Ok for Ok; for an error, Failure after the error's message is written to the log (or, without a host, to the console's error stream). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

`Std.App` is the entry of a server image. `neura-server-host` runs the image once for each request in a fresh worker process; `Context.serve` receives that request, calls the handler and sends its response. Under `neura run` there is no assigned request and `serve` fails with `CapabilityDenied`. The [Server host](Server-Host) page describes the host, its options and limits.
