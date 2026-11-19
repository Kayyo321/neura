# `Std.Net.Http`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Http** · [`export let Headers = header_maps.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L44)
  The header map type (`Std.Net.Http.Headers.Headers`).
- **Http** · [`export const DefaultBodyLimit: int64 = 1048576`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L47)
  The default request body limit: 1 MiB (Apps030 S-6 default).
- **Http** · [`export let BodySource = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L55)
  Where a request body comes from: bytes in memory, the server host, or a test's source.
- **Http** · [`export let MemoryBody = class implements BodySource`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L64)
  A body held in memory. Tagged `#Final`, `#NotThreadSafe`.
- **Http.MemoryBody** · [`export let init = func(content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L71)
  A source over `content`, which it keeps (it does not copy).
- **Http.MemoryBody** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L76)
  The next bytes, at most `max`.
- **Http.MemoryBody** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L86)
  Does nothing.
- **Http.MemoryBody** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L89)
  The text "MemoryBody(N bytes)".
- **Http** · [`export let HostBody = class implements BodySource`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L95)
  The body of the request the server host assigned to this worker. Host-dependent: its reads trap until the runtime provides `neura.http.server.body.read`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.HostBody** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L100)
  The body of the current host request.
- **Http.HostBody** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L104)
  The next bytes from the host, at most `max`.
- **Http.HostBody** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L116)
  Does nothing: the host releases the body when the response completes.
- **Http.HostBody** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L119)
  The text "HostBody".
- **Http** · [`export let BodyReader = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L125)
  Reads a request body in chunks within the request's limit; a closeable owner (close it when done; `using` is not in this branch). Tagged `#Final`, `#NotThreadSafe`.
- **Http.BodyReader** · [`export let init = func(body: BodySource, max_bytes: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L136)
  A reader over `body` that fails once more than `max_bytes` arrive.
- **Http.BodyReader** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L144)
  Up to `max` bytes; an empty list at the end. Fails with "http.body_too_large" past the limit and with Closed after close. A maximum below 1 traps. Tagged `#NoDiscard`.
- **Http.BodyReader** · [`export let bytes_read = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L158)
  Bytes read so far.
- **Http.BodyReader** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L161)
  Closes the source; closing again does nothing.
- **Http.BodyReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L170)
  The text "BodyReader(N bytes read)".
- **Http** · [`export let BodySink = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L174)
  Where a response body goes: memory, or the server host.
- **Http** · [`export let MemorySink = class implements BodySink`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L183)
  A sink that collects the body in memory (for tests and conversions). Tagged `#Final`, `#NotThreadSafe`.
- **Http.MemorySink** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L190)
  An empty sink.
- **Http.MemorySink** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L194)
  Appends the bytes; fails with Closed after close.
- **Http.MemorySink** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L205)
  Marks the body complete.
- **Http.MemorySink** · [`export let bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L211)
  The bytes written so far (the sink's own list).
- **Http.MemorySink** · [`export let is_closed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L214)
  Whether close ran.
- **Http.MemorySink** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L217)
  The text "MemorySink(N bytes)".
- **Http** · [`export let HostSink = class implements BodySink`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L223)
  The response body of the current host request. Host-dependent: writes trap until the runtime provides `neura.http.server.body.write` and `neura.http.server.body.close`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.HostSink** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L225)
  The sink of the current host response.
- **Http.HostSink** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L229)
  Hands the bytes to the host, which applies backpressure.
- **Http.HostSink** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L232)
  Ends the body at the host.
- **Http.HostSink** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L235)
  The text "HostSink".
- **Http** · [`export let Principal = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L242)
  The authenticated identity of a request, produced by an `Authenticator`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Principal** · [`export last subject: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L244)
  The identity: a user or service identifier.
- **Http.Principal** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L246)
  How it was established, such as "bearer" or "session".
- **Http.Principal** · [`export let init = func(identity: string, method: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L251)
  A principal without scopes, or with the given scopes (copied).
- **Http.Principal** · [`export let scopes = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L266)
  The scopes, as a new list. Tagged `#NoDiscard`.
- **Http.Principal** · [`export let has_scope = func(scope: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L275)
  Whether `scope` was granted.
- **Http.Principal** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L285)
  Equal subject, scheme and scopes.
- **Http.Principal** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L305)
  Combines subject, scheme and scopes.
- **Http.Principal** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L315)
  The text "Principal(scheme:subject)".
- **Http** · [`export let Params = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L320)
  An ordered list of names and values: the path parameters of a matched route, or query pairs. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Params** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L327)
  An empty list.
- **Http.Params** · [`export let add = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L331)
  Appends a pair.
- **Http.Params** · [`export let get = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L338)
  The first value of `name`. Tagged `#NoDiscard`.
- **Http.Params** · [`export let get_all = func(name: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L349)
  Every value of `name` in order. Tagged `#NoDiscard`.
- **Http.Params** · [`export let value = func(name: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L361)
  The value of `name`, or an InvalidInput error naming it (for handlers: `params.value("id")?`). Tagged `#NoDiscard`.
- **Http.Params** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L371)
  The number of pairs.
- **Http.Params** · [`export let name_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L374)
  The name of pair `index`.
- **Http.Params** · [`export let value_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L377)
  The value of pair `index`.
- **Http.Params** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L380)
  Equal pairs in equal order.
- **Http.Params** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L396)
  Combines every name and value.
- **Http.Params** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L406)
  The text "Params{a=1, b=2}".
- **Http** · [`export let Request = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L423)
  One HTTP request: immutable fields and a body read once (Backend030, "HTTP"). `path` is the request target's path, still percent-encoded; `query` the text after `?` without it ("" when absent). An absolute-form target (`http://host/path`) keeps its path and query. `body()` reads the whole body within the limit and keeps it, so later calls and `open_body()` see the same bytes; `open_body()` before `body()` streams the body and can be called once. `headers` returns a copy, so a handler cannot change the request. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Request** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L425)
  The method token, such as "GET".
- **Http.Request** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L427)
  The request target as received.
- **Http.Request** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L429)
  The path of the target, percent-encoded as received.
- **Http.Request** · [`export last query: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L431)
  The query of the target without `?`, or "".
- **Http.Request** · [`export last client: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L433)
  The peer's address text (from the connection; see Backend030 on trusted proxies), or "".
- **Http.Request** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L435)
  "http" or "https" (from the connection).
- **Http.Request** · [`export last request_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L437)
  The host's identifier of this request, used in logs and the error envelope.
- **Http.Request** · [`export last body_limit: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L439)
  The most body bytes `body()` and `open_body()` accept.
- **Http.Request** · [`export let init = func(request_method: string, request_target: string, request_headers: Headers, content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L454)
  A request with an in-memory body (for tests and conversions): client "", scheme "http", request id "", the default body limit. The second form gives every field. An invalid method traps with "invalid argument: method".
- **Http.Request** · [`export let headers: Headers { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L489)
  A copy of the header fields.
- **Http.Request** · [`export let header = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L494)
  The first value of header `name`. Tagged `#NoDiscard`.
- **Http.Request** · [`export let host = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L497)
  The host the request is for: the Host header's host, lowercase, without a port; "" when absent.
- **Http.Request** · [`export let query_params = func -> Params`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L504)
  The query's pairs, decoded as `application/x-www-form-urlencoded`. Tagged `#NoDiscard`.
- **Http.Request** · [`export let principal = func -> Optional<Principal>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L508)
  The authenticated principal, set by `require`. Tagged `#NoDiscard`.
- **Http.Request** · [`export let body = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L516)
  The whole body within `body_limit`, read once and kept. Fails with "http.body_too_large" past the limit, and with InvalidInput after `open_body()` streamed it. Tagged `#NoDiscard`.
- **Http.Request** · [`export let body_text = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L542)
  The body as UTF-8 text; invalid UTF-8 is an Encoding error. Tagged `#NoDiscard`.
- **Http.Request** · [`export let open_body = func -> Result<BodyReader>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L547)
  The body as a chunk reader within `body_limit`; close it when done. After `body()` it reads the kept bytes; otherwise it can be opened once. Tagged `#NoDiscard`.
- **Http.Request** · [`export let with_principal = func(who: Principal) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L560)
  The same request carrying `who` as its principal. The copy shares the body. Tagged `#NoDiscard`.
- **Http.Request** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L579)
  The text "Request(METHOD target)".
- **Http** · [`export let Response = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L589)
  An HTTP response: a status, headers, and a body of bytes or a stream (Backend030, "HTTP"). The header map is the response's own and mutable, so middleware can add fields on the way out. A streamed body is a producer that writes to a `BodySink` when the response is sent (`write_to`). Tagged `#Final`, `#NotThreadSafe`.
- **Http.Response** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L591)
  The status code, 100 through 599.
- **Http.Response** · [`export last headers: Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L593)
  The header fields (mutable).
- **Http.Response** · [`export let init = func(code: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L603)
  A response with no fields and an empty body; with fields and a body; or streamed from `writer`. A status outside 100 through 599 traps with "invalid argument: status".
- **Http.Response** · [`export let body = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L627)
  The body bytes (the response's own list); empty for a streamed body.
- **Http.Response** · [`export let body_text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L630)
  The body as UTF-8 text, invalid sequences replaced.
- **Http.Response** · [`export let is_streamed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L633)
  Whether the body is a stream.
- **Http.Response** · [`export let write_to = func(sink: BodySink) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L637)
  Writes the body to `sink` and closes it: the bytes, or whatever the producer writes. Tagged `#NoDiscard`.
- **Http.Response** · [`export let without_body = func -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L650)
  The same status and a copy of the fields with no body, as a HEAD answer: a byte body's length becomes `Content-Length` unless the field is already set; a stream is not run. Tagged `#NoDiscard`.
- **Http.Response** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L659)
  The text "Response(STATUS, N bytes)" or "Response(STATUS, streamed)".
- **Http** · [`export let empty = func(status: int) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L667)
  A response with an empty body. Tagged `#NoDiscard`.
- **Http** · [`export let text = func(status: int, body: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L671)
  A `text/plain; charset=utf-8` response. Tagged `#NoDiscard`.
- **Http** · [`export let binary = func(status: int, content_type: string, body: uint8[list]) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L679)
  A response with the given content type and bytes. Tagged `#NoDiscard`.
- **Http** · [`export let json = func<T>(status: int, value: T, encode: func(T) -> Result<string>) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L691)
  An `application/json` response of `value` encoded with `encode`, the codec's encode function (Records030, "The JSON codec": pass `SomeCodec.encode`). An encoding error is returned. Backend030 writes `http.json(status, value, codec)` with the codec itself; `Std.Encoding.Json.Codec<T>` is a static contract not in this branch, so this form takes its `encode` function. Tagged `#NoDiscard`.
- **Http** · [`export let json_value = func(status: int, value: json_text.JsonValue) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L698)
  An `application/json` response of a JSON tree. Tagged `#NoDiscard`.
- **Http** · [`export let stream = func(status: int, fields: Headers, writer: func(BodySink) -> Result<Unit>) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L703)
  A streamed response whose body `writer` produces when it is sent. Tagged `#NoDiscard`.
- **Http** · [`export let envelope = func(status: int, code: string, message: string, request_id: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L709)
  The framework envelope `{"error":{"code":...,"message":...,"request_id":...}}` as an `application/json` response. Tagged `#NoDiscard`.
- **Http** · [`export let request = func(method: string, target: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L721)
  A request with an empty header map and body (for tests). Tagged `#NoDiscard`.
- **Http** · [`export let request_with = func(method: string, target: string, fields: Headers, body: uint8[list]) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L726)
  A request with the given fields and in-memory body (for tests). Tagged `#NoDiscard`.
- **Http** · [`export let host_request = func(method: string, target: string, fields: Headers, limit: int64, peer: string, scheme: string, request_id: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L732)
  The request the server host assigned (`neura.http.server.next` gives these fields to `Std.App`). Its body is read from the host. Tagged `#NoDiscard`.
- **Http** · [`export let to_message = func(request: Request) -> Result<messages.HttpRequest<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L740)
  The request as a protocol-agnostic message with its whole body (read within the limit). Tagged `#NoDiscard`.
- **Http** · [`export let from_message = func(message: messages.HttpRequest<uint8[list]>) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L753)
  A request with the message's method, target, fields and body. Tagged `#NoDiscard`.
- **Http** · [`export let response_to_message = func(response: Response) -> Result<messages.HttpResponse<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L758)
  The response as a message; a streamed body is run into memory. Tagged `#NoDiscard`.
- **Http** · [`export let response_from_message = func(message: messages.HttpResponse<uint8[list]>) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L768)
  A response with the message's status, fields and body. Tagged `#NoDiscard`.
- **Http** · [`export let Next = class implements services.Service<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L776)
  The rest of the chain a middleware calls: the next middleware or the handler. A `Std.Net.Service<Request, Response>`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Next** · [`export let init = func(service: services.Service<Request, Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L781)
  Continues with `service`.
- **Http.Next** · [`export let call = func(request: Request) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L786)
  Runs the rest of the chain.
- **Http.Next** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L789)
  The text "Next".
- **Http** · [`export let MiddlewareFilter = class implements filters.SimpleFilter<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L890)
  A middleware as a `Std.Net.Filter.SimpleFilter<Request, Response>`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.MiddlewareFilter** · [`export let init = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L895)
  Wraps `middleware`.
- **Http.MiddlewareFilter** · [`export let apply = func(request: Request, next: services.Service<Request, Response>) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L900)
  Runs the middleware with `next` as the rest of the chain.
- **Http.MiddlewareFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L906)
  The text "MiddlewareFilter".
- **Http** · [`export let as_filter = func(middleware: func(Request, Next) -> Result<Response>) -> filters.SimpleFilter<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L911)
  `middleware` as a simple filter. Tagged `#NoDiscard`.
- **Http** · [`export let from_filter = func(filter: filters.SimpleFilter<Request, Response>) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L916)
  A simple filter as a middleware. Tagged `#NoDiscard`.
- **Http** · [`export let request_log_to = func(logger: logging.Logger, clock: clocks.Clock) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L923)
  A middleware that logs one line per request ("ID METHOD PATH STATUS Nms", or "error" in place of the status) at Info through `logger`, timing with `clock`'s monotonic time. Tagged `#NoDiscard`.
- **Http** · [`export let request_log = func -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L940)
  `request_log_to` with a logger named "http" writing to standard error and the system clock. Tagged `#NoDiscard`.
- **Http** · [`export let Route = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L947)
  One route: method, path pattern, optional host pattern, handler and the route's own middleware. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Route** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L949)
  The method token.
- **Http.Route** · [`export last pattern: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L951)
  The path pattern, such as "/parts/{id}".
- **Http.Route** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L953)
  The host pattern, or "" for any host.
- **Http.Route** · [`export last handler: func(Request, Params) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L955)
  The handler.
- **Http.Route** · [`export last layers: Layer[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L957)
  The route's middleware, in order.
- **Http.Route** · [`export let init = func(route_method: string, path_pattern: string, host_pattern: string, body: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L960)
  A route without middleware.
- **Http.Route** · [`export let use = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L969)
  Appends a middleware that runs after the earlier ones and before the handler.
- **Http.Route** · [`export let middleware_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L974)
  The number of the route's own middlewares.
- **Http.Route** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L977)
  The text "Route(METHOD host/pattern)".
- **Http** · [`export let Routes = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L987)
  A group of routes for `Router.mount`, such as a generated contract adapter's. A group may be bound to a host pattern (an addition to Backend030): an exact host name such as "api.example.com", or "*.example.com" for any host with one or more labels before ".example.com"; case is ignored and the request's port is not compared. The group's `use` middlewares run before each route's own middlewares, after the router's. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Routes** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L989)
  The host pattern, or "".
- **Http.Routes** · [`export last layers: Layer[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L993)
  The group's middleware.
- **Http.Routes** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L996)
  A group for any host, or for `host_pattern` (lowercased).
- **Http.Routes** · [`export let get = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1003)
  Adds a GET route.
- **Http.Routes** · [`export let post = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1007)
  Adds a POST route.
- **Http.Routes** · [`export let put = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1011)
  Adds a PUT route.
- **Http.Routes** · [`export let patch = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1015)
  Adds a PATCH route.
- **Http.Routes** · [`export let delete = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1019)
  Adds a DELETE route.
- **Http.Routes** · [`export let add = func(method: string, pattern: string, handler: func(Request, Params) -> Result<Response>) -> Route`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1024)
  Adds a route for any method token and returns it, so the caller can add the route's own middleware with `Route.use`.
- **Http.Routes** · [`export let use = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1032)
  Adds a middleware for every route of the group.
- **Http.Routes** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1037)
  The number of routes.
- **Http.Routes** · [`export let route_at = func(index: int) -> Route`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1040)
  Route `index`.
- **Http.Routes** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1043)
  The text "Routes(host, N routes)".
- **Http** · [`export let Router = class implements services.Service<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1101)
  The application's routes and middleware; dispatches one request to one response (Backend030, "HTTP"). A `Std.Net.Service<Request, Response>` whose `call` never fails. Patterns are "/" or "/" followed by segments separated by "/": a literal segment of path characters without braces, or `{name}` with an identifier name, which matches one non-empty segment. Registration never fails; an invalid pattern or a second route with the same host, method and shape (parameter names ignored) is recorded and reported by `check`, the startup check, and such a route is never matched. Dispatch: the request's host selects the trees to search: exact host groups, then wildcard groups, in the order mounted, then the routes for any host. The first tree with a route for the method and path wins (literal before parameter at every segment, backtracking). HEAD uses the GET route and drops the body. Without a match, a path that some route matches under other methods is 405 with `Allow` (GET, HEAD, POST, PUT, PATCH, DELETE in that order, then other methods as registered; HEAD is listed with GET), otherwise 404; both carry the framework envelope. The router's `use` middlewares wrap every request, including 404 and 405 answers (so `cors` can answer preflight requests and `request_log` sees misses); a route's group and own middlewares wrap only that route's handler. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Router** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1114)
  An empty router.
- **Http.Router** · [`export let get = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1119)
  Adds a GET route.
- **Http.Router** · [`export let post = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1123)
  Adds a POST route.
- **Http.Router** · [`export let put = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1127)
  Adds a PUT route.
- **Http.Router** · [`export let patch = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1131)
  Adds a PATCH route.
- **Http.Router** · [`export let delete = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1135)
  Adds a DELETE route.
- **Http.Router** · [`export let mount = func(group: Routes)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1139)
  Adds every route of `group`, with the group's host pattern and middleware.
- **Http.Router** · [`export let use = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1150)
  Adds a middleware around every request; it runs after the ones added before it.
- **Http.Router** · [`export let on_error = func(sink: func(string) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1156)
  Sends each handler or middleware error, as "request ID failed: MESSAGE", to `sink` instead of standard error.
- **Http.Router** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1161)
  The number of routes registered without problems.
- **Http.Router** · [`export let check = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1172)
  The startup check: Ok when every route is valid and unique; otherwise an error whose message lists the problems. Tagged `#NoDiscard`.
- **Http.Router** · [`export let problems_found = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1179)
  The registration problems, as a new list. Tagged `#NoDiscard`.
- **Http.Router** · [`export let startup_response = func -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1190)
  The answer to the host's startup check request, without calling a route: 200 `{"status":"ok","routes":N}`, or 500 with the envelope code "startup_check_failed" and the problems. Tagged `#NoDiscard`.
- **Http.Router** · [`export let dispatch = func(request: Request) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1203)
  Answers `request`. Never fails: an `Err` from a handler or middleware is logged and becomes 500 with the framework envelope. Tagged `#NoDiscard`.
- **Http.Router** · [`export let dispatch_for_host = func(request: Request) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1209)
  `dispatch` for a host that drops a HEAD response's body itself and computes `Content-Length` from the body it was given (the `Std.App` server host): a HEAD request is answered from the GET route with the body kept. Tagged `#NoDiscard`.
- **Http.Router** · [`export let report_failure = func(request_id: string, problem: errors.Error)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1257)
  Sends "request ID failed: MESSAGE" for `problem` to the error sink (`on_error`), as `dispatch` does for a handler or middleware error. Adapters that fail after dispatch, such as `Std.Net.Http.AppHandler` collecting a streamed body, report through it so every failure reaches one sink.
- **Http.Router** · [`export let call = func(request: Request) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1263)
  `dispatch` as a service call: always Ok.
- **Http.Router** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1266)
  The text "Router(N routes)".
- **Http** · [`export let Authenticator = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1347)
  Establishes who sent a request: Ok(Some) for an authenticated principal, Ok(None) for none, Err when the check itself failed (Backend030, "HTTP").
- **Http** · [`export let BearerAuthenticator = class implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1355)
  Bearer tokens (RFC 6750 section 2.1) checked by a function the application supplies. The library defines no token format. Tagged `#Final`, `#NotThreadSafe`.
- **Http.BearerAuthenticator** · [`export let init = func(check: func(string) -> Result<Optional<Principal>>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1360)
  Checks tokens with `check`.
- **Http.BearerAuthenticator** · [`export let authenticate = func(request: Request) -> Result<Optional<Principal>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1366)
  None without an `Authorization: Bearer` field or with a malformed token; otherwise the check's answer.
- **Http.BearerAuthenticator** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1375)
  The fixed text "BearerAuthenticator" (it holds no token).
- **Http** · [`export let bearer = func(check: func(string) -> Result<Optional<Principal>>) -> Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1380)
  A bearer authenticator over `check`. Tagged `#NoDiscard`.
- **Http** · [`export let bearer_token = func(request: Request) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1386)
  The token of an `Authorization: Bearer <token>` field (scheme case-insensitive, token of b64token characters, RFC 6750 section 2.1), or None. Tagged `#NoDiscard`.
- **Http** · [`export let require = func(authenticator: Authenticator) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1418)
  A middleware that requires a principal: 401 with the envelope (and `WWW-Authenticate: Bearer` for a bearer authenticator) when there is none; otherwise the rest of the chain sees the request carrying it (`request.principal()`). An `Err` from the authenticator is returned (500). Tagged `#NoDiscard`.
- **Http** · [`export let same_origin = func -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1438)
  A middleware for cookie-authenticated routes: a request with an unsafe method (not GET, HEAD, OPTIONS, TRACE) passes only with `Sec-Fetch-Site: same-origin`, or, without that field, an `Origin` equal to the request's own origin (scheme and Host); otherwise 403 with the envelope. Safe methods pass. Tagged `#NoDiscard`.
- **Http** · [`export let CorsPolicy = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1466)
  Which cross-origin requests `cors` admits: an explicit list of origins (no wildcard), methods, request headers, exposed headers, credentials and the preflight cache time. Tagged `#Final`, `#NotThreadSafe`.
- **Http.CorsPolicy** · [`export let init = func(allowed_origins: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1482)
  A policy for `allowed_origins` (each "scheme://host[:port]"), admitting GET, HEAD and POST and no extra headers. "*", "null" or an empty origin traps with "invalid argument: origin".
- **Http.CorsPolicy** · [`export let allow_methods = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1495)
  Replaces the admitted methods.
- **Http.CorsPolicy** · [`export let allow_headers = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1505)
  Replaces the admitted request headers.
- **Http.CorsPolicy** · [`export let expose_headers = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1515)
  Replaces the exposed response headers.
- **Http.CorsPolicy** · [`export let allow_credentials = func(allowed: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1525)
  Sets whether credentials are allowed.
- **Http.CorsPolicy** · [`export let set_max_age = func(seconds: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1530)
  Sets the preflight cache time; negative sends none.
- **Http.CorsPolicy** · [`export let admits_origin = func(origin: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1535)
  Whether `origin` is admitted.
- **Http.CorsPolicy** · [`export let admits_method = func(method: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1546)
  Whether `method` is admitted.
- **Http.CorsPolicy** · [`export let admits_headers = func(requested: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1556)
  Whether every comma-separated name in `requested` is admitted.
- **Http.CorsPolicy** · [`export let decorate = func(response: Response, origin: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1570)
  Adds the response fields for an admitted actual request from `origin`.
- **Http.CorsPolicy** · [`export let preflight = func(origin: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1583)
  The 204 answer to an admitted preflight request from `origin`. Tagged `#NoDiscard`.
- **Http.CorsPolicy** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1597)
  The text "CorsPolicy(origins)".
- **Http** · [`export let cors = func(policy: CorsPolicy) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1607)
  A middleware applying `policy` (Fetch standard CORS protocol). A request without `Origin` passes unchanged. A preflight (OPTIONS with `Access-Control-Request-Method`) is answered here: 204 with the policy's fields when the origin, method and requested headers are admitted, 403 with the envelope otherwise. Other requests run the chain; an admitted origin's response gets `Access-Control-Allow-Origin` (the origin itself, never "*") and `Vary: Origin`; a refused origin's response is left without them, so the browser withholds it. Tagged `#NoDiscard`.
- **Http** · [`export let CredentialsMode = enum (Omit, SameOrigin, Include)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1634)
  Whether a fetch sends credentials (cookies, HTTP authentication): the `neura.http.fetch` mode codes 0, 1, 2 (Components030, "HTTP clients").
- **Http** · [`export let credentials_code = func(mode: CredentialsMode) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1638)
  The `uint32` code of `mode` for `neura.http.fetch`. Tagged `#Pure`, `#ThreadSafe`.
- **Http** · [`export let ClientRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1646)
  An outgoing request: method, URL, header fields, body, deadline and credentials mode. Tagged `#Final`, `#NotThreadSafe`.
- **Http.ClientRequest** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1648)
  The method token.
- **Http.ClientRequest** · [`export last url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1650)
  The absolute `http` or `https` URL.
- **Http.ClientRequest** · [`export last headers: Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1652)
  The header fields (mutable).
- **Http.ClientRequest** · [`export let init = func(request_method: string, target_url: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1662)
  A request without fields or body; or with them. An invalid method traps with "invalid argument: method".
- **Http.ClientRequest** · [`export let body = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1676)
  The body bytes.
- **Http.ClientRequest** · [`export let set_body = func(body: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1679)
  Replaces the body.
- **Http.ClientRequest** · [`export let deadline = func -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1684)
  The deadline as a duration from the start of the exchange.
- **Http.ClientRequest** · [`export let set_deadline = func(span: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1687)
  Sets the deadline; a negative one traps with "invalid argument: negative deadline".
- **Http.ClientRequest** · [`export let credentials_mode = func -> CredentialsMode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1695)
  The credentials mode (default SameOrigin, as in the Fetch standard).
- **Http.ClientRequest** · [`export let set_credentials = func(mode: CredentialsMode)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1698)
  Sets the credentials mode.
- **Http.ClientRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1703)
  The text "ClientRequest(METHOD url)".
- **Http** · [`export let Transport = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1707)
  What carries a client request: the host's `neura.http.fetch`, or a test double.
- **Http** · [`export let HostTransport = class implements Transport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1715)
  The host transport: `neura.http.fetch` version 1. Host-dependent: traps until the runtime provides the service. Tagged `#Final`, `#NotThreadSafe`.
- **Http.HostTransport** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1717)
  The host transport.
- **Http.HostTransport** · [`export let fetch = func(request: ClientRequest) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1721)
  Sends the request through the host.
- **Http.HostTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1733)
  The text "HostTransport".
- **Http** · [`export let Client = class implements services.Service<ClientRequest, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1740)
  An HTTP client over a transport (Components030, "HTTP clients"); a `Std.Net.Service<ClientRequest, Response>`. The URL must be absolute `http` or `https`; otherwise the send fails with InvalidInput before the transport is called. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Client** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1747)
  A client over the host's `neura.http.fetch`, or over `carrier`.
- **Http.Client** · [`export let add_default_header = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1754)
  Adds a field sent with every request that lacks it.
- **Http.Client** · [`export let send = func(request: ClientRequest) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1760)
  Sends `request`. Tagged `#NoDiscard`.
- **Http.Client** · [`export let call = func(request: ClientRequest) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1774)
  `send` as a service call.
- **Http.Client** · [`export let get = func(url: string) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1778)
  A GET of `url`. Tagged `#NoDiscard`.
- **Http.Client** · [`export let post = func(url: string, content_type: string, body: uint8[list]) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1782)
  A POST of `body` with `content_type` to `url`. Tagged `#NoDiscard`.
- **Http.Client** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1790)
  The text "Client".
- **Http** · [`export let parse_query = func(query: string) -> Params`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1829)
  The pairs of an `application/x-www-form-urlencoded` query: split on "&", then on the first "="; "+" is a space and percent escapes are decoded (invalid escapes and invalid UTF-8 are kept as written). Tagged `#NoDiscard`.
- **Http** · [`export let percent_decode_segment = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1851)
  `text` with percent escapes decoded; the text unchanged when an escape is malformed or the result is not UTF-8. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
