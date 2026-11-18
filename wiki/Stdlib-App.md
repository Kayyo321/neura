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
- **App** · [`export let Request = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L46)
  One HTTP request as the host received it. The body is complete: the host reads it before it starts the worker, within the host's body limit (S-6). Tagged `#Final`.
- **App.Request** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L48)
  The method token, such as GET or POST.
- **App.Request** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L50)
  The request target as sent: the path and, after `?`, the query.
- **App.Request** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L52)
  The target up to its first `?`.
- **App.Request** · [`export last query: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L54)
  The target after its first `?`, without it; empty when there is none.
- **App.Request** · [`export last headers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L56)
  Header fields in arrival order, with lowercase names.
- **App.Request** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L58)
  The body bytes; empty when the request has no body.
- **App.Request** · [`export last client: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L60)
  The numeric address of the client, from the connection.
- **App.Request** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L62)
  "http" or "https", from the listener that accepted the connection.
- **App.Request** · [`export last request_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L64)
  The host's identifier of this request, as in its log records.
- **App.Request** · [`export let init = func(method_value: string, target_value: string, header_list: [string, string][list], body_bytes: uint8[list], client_value: string, scheme_value: string, id: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L67)
  Builds a request; the server host's values arrive through `Context.serve`.
- **App.Request** · [`export let header = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L89)
  The value of the first field named `name`, compared without ASCII case. Tagged `#NoDiscard`.
- **App.Request** · [`export let body_text = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L101)
  The body as UTF-8 text; invalid UTF-8 is an error. Tagged `#NoDiscard`.
- **App.Request** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L104)
  The method and target, such as "GET /parts?page=2".
- **App** · [`export let Response = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L110)
  An HTTP response: a status from 100 through 599, header fields, and the body bytes. The host writes Content-Length and Connection itself and ignores those fields here. Tagged `#Final`.
- **App.Response** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L112)
  The status code.
- **App.Response** · [`export last headers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L114)
  Header fields in the order they are sent.
- **App.Response** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L116)
  The body bytes, within the host's response limit (S-7).
- **App.Response** · [`export let init = func(code: int, fields: [string, string][list], content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L119)
  Builds a response from its parts.
- **App.Response** · [`export let with_header = func(name: string, value: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L127)
  A copy with one more header field. Tagged `#NoDiscard`.
- **App.Response** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L137)
  The status and the body size, such as "200 (5 bytes)".
- **App** · [`export let Handler = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L143)
  What `Context.serve` calls with the request. `Std.Net.Http.Router` implements it; `handler_of` adapts a function.
- **App** · [`export let FunctionHandler = class implements Handler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L150)
  A handler that calls a function. Tagged `#Final`.
- **App.FunctionHandler** · [`export let init = func(function: func(Request) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L155)
  Wraps `function`.
- **App.FunctionHandler** · [`export let handle = func(request: Request) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L160)
  Calls the function.
- **App.FunctionHandler** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L163)
  A fixed description.
- **App** · [`export let handler_of = func(function: func(Request) -> Result<Response>) -> Handler`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L168)
  A handler that calls `function` for each request. Tagged `#NoDiscard`.
- **App** · [`export let text = func(status: int, content: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L173)
  A response whose body is UTF-8 text, with Content-Type text/plain; charset=utf-8. Tagged `#NoDiscard`.
- **App** · [`export let html = func(status: int, content: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L178)
  A response whose body is HTML, with Content-Type text/html; charset=utf-8. Tagged `#NoDiscard`.
- **App** · [`export let json = func(status: int, encoded: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L183)
  A response whose body is JSON text the caller encoded, with Content-Type application/json. Tagged `#NoDiscard`.
- **App** · [`export let empty = func(status: int) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L187)
  A response with no body, such as 204 or a redirect with a Location field added. Tagged `#NoDiscard`.
- **App** · [`export let Context = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L202)
  The execution's view of its host: the assigned request, configuration and the log. Tagged `#Final`, `#NotThreadSafe`.
- **App.Context** · [`export let init = func()`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L207)
  Contexts come from `run`.
- **App.Context** · [`export let config = func(key: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L213)
  The host configuration value of `key`: from the host's --config options, its configuration file, or the environment under its prefix. None when it is not set or there is no host. Tagged `#NoDiscard`.
- **App.Context** · [`export let log = func(level: LogLevel, message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L222)
  Writes a JSON log record through the host, with this request's identifier. Without a host the record is dropped.
- **App.Context** · [`export let serve = func(handler: Handler) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L237)
  Receives the request the host assigned, calls `handler` and sends its response. A startup check is answered with 204 without calling the handler. A handler error is answered with status 500 and the body {"error":"internal"}, and the error is logged with the request identifier; it does not make `serve` fail. `serve` fails when there is no assigned request (for example under `neura run`), when it is called a second time, and when the response cannot be sent. Tagged `#NoDiscard`.
- **App.Context** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L316)
  A fixed description.
- **App** · [`export let run = func(serve: func(Context) -> Result<Unit>) -> program.ExitCode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/app.nra#L351)
  Builds the context, calls `serve` and turns its result into the exit code: Ok for Ok; for an error, Failure after the error's message is written to the log (or, without a host, to the console's error stream). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

`Std.App` is the entry of a server image. `neura-server-host` runs the image once for each request in a fresh worker process; `Context.serve` receives that request, calls the handler and sends its response. Under `neura run` there is no assigned request and `serve` fails with `CapabilityDenied`. The [Server host](Server-Host) page describes the host, its options and limits.
