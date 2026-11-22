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

- **Http** · [`export let Headers = header_maps.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L45)
  The header map type (`Std.Net.Http.Headers.Headers`).
- **Http** · [`export const DefaultBodyLimit: int64 = 1048576`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L48)
  The default request body limit: 1 MiB (Apps030 S-6 default).
- **Http** · [`export let BodySource = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L56)
  Where a request body comes from: bytes in memory, the server host, or a test's source.
- **Http** · [`export let MemoryBody = class implements BodySource`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L65)
  A body held in memory. Tagged `#Final`, `#NotThreadSafe`.
- **Http.MemoryBody** · [`export let init = func(content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L72)
  A source over `content`, which it keeps (it does not copy).
- **Http.MemoryBody** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L77)
  The next bytes, at most `max`.
- **Http.MemoryBody** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L87)
  Does nothing.
- **Http.MemoryBody** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L90)
  The text "MemoryBody(N bytes)".
- **Http** · [`export let HostBody = class implements BodySource`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L96)
  The body of the request the server host assigned to this worker. Outside a server worker, reads return CapabilityDenied. Tagged `#Final`, `#NotThreadSafe`.
- **Http.HostBody** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L101)
  The body of the current host request.
- **Http.HostBody** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L105)
  The next bytes from the host, at most `max`.
- **Http.HostBody** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L117)
  Does nothing: the host releases the body when the response completes.
- **Http.HostBody** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L120)
  The text "HostBody".
- **Http** · [`export let BodyReader = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L126)
  Reads a request body in chunks within the request's limit; a closeable owner (close it when done; `using` is not in this branch). Tagged `#Final`, `#NotThreadSafe`.
- **Http.BodyReader** · [`export let init = func(body: BodySource, max_bytes: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L137)
  A reader over `body` that fails once more than `max_bytes` arrive.
- **Http.BodyReader** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L145)
  Up to `max` bytes; an empty list at the end. Fails with "http.body_too_large" past the limit and with Closed after close. A maximum below 1 traps. Tagged `#NoDiscard`.
- **Http.BodyReader** · [`export let bytes_read = func -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L159)
  Bytes read so far.
- **Http.BodyReader** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L162)
  Closes the source; closing again does nothing.
- **Http.BodyReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L171)
  The text "BodyReader(N bytes read)".
- **Http** · [`export let BodySink = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L175)
  Where a response body goes: memory, or the server host.
- **Http** · [`export let MemorySink = class implements BodySink`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L184)
  A sink that collects the body in memory (for tests and conversions). Tagged `#Final`, `#NotThreadSafe`.
- **Http.MemorySink** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L191)
  An empty sink.
- **Http.MemorySink** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L195)
  Appends the bytes; fails with Closed after close.
- **Http.MemorySink** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L206)
  Marks the body complete.
- **Http.MemorySink** · [`export let bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L212)
  The bytes written so far (the sink's own list).
- **Http.MemorySink** · [`export let is_closed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L215)
  Whether close ran.
- **Http.MemorySink** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L218)
  The text "MemorySink(N bytes)".
- **Http** · [`export let HostSink = class implements BodySink`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L224)
  The response body of the current host request, after its response head has been sent. Writes and close return CapabilityDenied outside a server worker. Tagged `#Final`, `#NotThreadSafe`.
- **Http.HostSink** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L226)
  The sink of the current host response.
- **Http.HostSink** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L230)
  Hands the bytes to the host, which applies backpressure.
- **Http.HostSink** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L233)
  Ends the body at the host.
- **Http.HostSink** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L236)
  The text "HostSink".
- **Http** · [`export let Principal = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L243)
  The authenticated identity of a request, produced by an `Authenticator`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Principal** · [`export last subject: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L245)
  The identity: a user or service identifier.
- **Http.Principal** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L247)
  How it was established, such as "bearer" or "session".
- **Http.Principal** · [`export let init = func(identity: string, method: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L252)
  A principal without scopes, or with the given scopes (copied).
- **Http.Principal** · [`export let scopes = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L267)
  The scopes, as a new list. Tagged `#NoDiscard`.
- **Http.Principal** · [`export let has_scope = func(scope: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L276)
  Whether `scope` was granted.
- **Http.Principal** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L286)
  Equal subject, scheme and scopes.
- **Http.Principal** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L306)
  Combines subject, scheme and scopes.
- **Http.Principal** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L316)
  The text "Principal(scheme:subject)".
- **Http** · [`export let Params = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L321)
  An ordered list of names and values: the path parameters of a matched route, or query pairs. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Params** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L328)
  An empty list.
- **Http.Params** · [`export let add = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L332)
  Appends a pair.
- **Http.Params** · [`export let get = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L339)
  The first value of `name`. Tagged `#NoDiscard`.
- **Http.Params** · [`export let get_all = func(name: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L350)
  Every value of `name` in order. Tagged `#NoDiscard`.
- **Http.Params** · [`export let value = func(name: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L362)
  The value of `name`, or an InvalidInput error naming it (for handlers: `params.value("id")?`). Tagged `#NoDiscard`.
- **Http.Params** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L372)
  The number of pairs.
- **Http.Params** · [`export let name_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L375)
  The name of pair `index`.
- **Http.Params** · [`export let value_at = func(index: int) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L378)
  The value of pair `index`.
- **Http.Params** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L381)
  Equal pairs in equal order.
- **Http.Params** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L397)
  Combines every name and value.
- **Http.Params** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L407)
  The text "Params{a=1, b=2}".
- **Http** · [`export let Request = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L424)
  One HTTP request: immutable fields and a body read once (Backend030, "HTTP"). `path` is the request target's path, still percent-encoded; `query` the text after `?` without it ("" when absent). An absolute-form target (`http://host/path`) keeps its path and query. `body()` reads the whole body within the limit and keeps it, so later calls and `open_body()` see the same bytes; `open_body()` before `body()` streams the body and can be called once. `headers` returns a copy, so a handler cannot change the request. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Request** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L426)
  The method token, such as "GET".
- **Http.Request** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L428)
  The request target as received.
- **Http.Request** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L430)
  The path of the target, percent-encoded as received.
- **Http.Request** · [`export last query: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L432)
  The query of the target without `?`, or "".
- **Http.Request** · [`export last client: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L434)
  The peer's address text (from the connection; see Backend030 on trusted proxies), or "".
- **Http.Request** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L436)
  "http" or "https" (from the connection).
- **Http.Request** · [`export last request_id: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L438)
  The host's identifier of this request, used in logs and the error envelope.
- **Http.Request** · [`export last body_limit: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L440)
  The most body bytes `body()` and `open_body()` accept.
- **Http.Request** · [`export let init = func(request_method: string, request_target: string, request_headers: Headers, content: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L455)
  A request with an in-memory body (for tests and conversions): client "", scheme "http", request id "", the default body limit. The second form gives every field. An invalid method traps with "invalid argument: method".
- **Http.Request** · [`export let headers: Headers { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L490)
  A copy of the header fields.
- **Http.Request** · [`export let header = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L495)
  The first value of header `name`. Tagged `#NoDiscard`.
- **Http.Request** · [`export let host = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L498)
  The host the request is for: the Host header's host, lowercase, without a port; "" when absent.
- **Http.Request** · [`export let query_params = func -> Params`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L505)
  The query's pairs, decoded as `application/x-www-form-urlencoded`. Tagged `#NoDiscard`.
- **Http.Request** · [`export let principal = func -> Optional<Principal>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L509)
  The authenticated principal, set by `require`. Tagged `#NoDiscard`.
- **Http.Request** · [`export let body = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L517)
  The whole body within `body_limit`, read once and kept. Fails with "http.body_too_large" past the limit, and with InvalidInput after `open_body()` streamed it. Tagged `#NoDiscard`.
- **Http.Request** · [`export let body_text = func -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L543)
  The body as UTF-8 text; invalid UTF-8 is an Encoding error. Tagged `#NoDiscard`.
- **Http.Request** · [`export let open_body = func -> Result<BodyReader>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L548)
  The body as a chunk reader within `body_limit`; close it when done. After `body()` it reads the kept bytes; otherwise it can be opened once. Tagged `#NoDiscard`.
- **Http.Request** · [`export let with_principal = func(who: Principal) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L561)
  The same request carrying `who` as its principal. The copy shares the body. Tagged `#NoDiscard`.
- **Http.Request** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L580)
  The text "Request(METHOD target)".
- **Http** · [`export let Response = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L590)
  An HTTP response: a status, headers, and a body of bytes or a stream (Backend030, "HTTP"). The header map is the response's own and mutable, so middleware can add fields on the way out. A streamed body is a producer that writes to a `BodySink` when the response is sent (`write_to`). Tagged `#Final`, `#NotThreadSafe`.
- **Http.Response** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L592)
  The status code, 100 through 599.
- **Http.Response** · [`export last headers: Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L594)
  The header fields (mutable).
- **Http.Response** · [`export let init = func(code: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L604)
  A response with no fields and an empty body; with fields and a body; or streamed from `writer`. A status outside 100 through 599 traps with "invalid argument: status".
- **Http.Response** · [`export let body = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L628)
  The body bytes (the response's own list); empty for a streamed body.
- **Http.Response** · [`export let body_text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L631)
  The body as UTF-8 text, invalid sequences replaced.
- **Http.Response** · [`export let is_streamed = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L634)
  Whether the body is a stream.
- **Http.Response** · [`export let write_to = func(sink: BodySink) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L638)
  Writes the body to `sink` and closes it: the bytes, or whatever the producer writes. Tagged `#NoDiscard`.
- **Http.Response** · [`export let without_body = func -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L651)
  The same status and a copy of the fields with no body, as a HEAD answer: a byte body's length becomes `Content-Length` unless the field is already set; a stream is not run. Tagged `#NoDiscard`.
- **Http.Response** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L660)
  The text "Response(STATUS, N bytes)" or "Response(STATUS, streamed)".
- **Http** · [`export let empty = func(status: int) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L668)
  A response with an empty body. Tagged `#NoDiscard`.
- **Http** · [`export let text = func(status: int, body: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L672)
  A `text/plain; charset=utf-8` response. Tagged `#NoDiscard`.
- **Http** · [`export let binary = func(status: int, content_type: string, body: uint8[list]) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L680)
  A response with the given content type and bytes. Tagged `#NoDiscard`.
- **Http** · [`export let json = func<T>(status: int, value: T, encode: func(T) -> Result<string>) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L692)
  An `application/json` response of `value` encoded with `encode`, the codec's encode function (Records030, "The JSON codec": pass `SomeCodec.encode`). An encoding error is returned. Backend030 writes `http.json(status, value, codec)` with the codec itself; `Std.Encoding.Json.Codec<T>` is a static contract not in this branch, so this form takes its `encode` function. Tagged `#NoDiscard`.
- **Http** · [`export let json_value = func(status: int, value: json_text.JsonValue) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L699)
  An `application/json` response of a JSON tree. Tagged `#NoDiscard`.
- **Http** · [`export let stream = func(status: int, fields: Headers, writer: func(BodySink) -> Result<Unit>) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L704)
  A streamed response whose body `writer` produces when it is sent. Tagged `#NoDiscard`.
- **Http** · [`export let envelope = func(status: int, code: string, message: string, request_id: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L710)
  The framework envelope `{"error":{"code":...,"message":...,"request_id":...}}` as an `application/json` response. Tagged `#NoDiscard`.
- **Http** · [`export let request = func(method: string, target: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L722)
  A request with an empty header map and body (for tests). Tagged `#NoDiscard`.
- **Http** · [`export let request_with = func(method: string, target: string, fields: Headers, body: uint8[list]) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L727)
  A request with the given fields and in-memory body (for tests). Tagged `#NoDiscard`.
- **Http** · [`export let host_request = func(method: string, target: string, fields: Headers, limit: int64, peer: string, scheme: string, request_id: string) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L733)
  The request the server host assigned (`neura.http.server.next` gives these fields to `Std.App`). Its body is read from the host. Tagged `#NoDiscard`.
- **Http** · [`export let to_message = func(request: Request) -> Result<messages.HttpRequest<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L741)
  The request as a protocol-agnostic message with its whole body (read within the limit). Tagged `#NoDiscard`.
- **Http** · [`export let from_message = func(message: messages.HttpRequest<uint8[list]>) -> Request`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L754)
  A request with the message's method, target, fields and body. Tagged `#NoDiscard`.
- **Http** · [`export let response_to_message = func(response: Response) -> Result<messages.HttpResponse<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L759)
  The response as a message; a streamed body is run into memory. Tagged `#NoDiscard`.
- **Http** · [`export let response_from_message = func(message: messages.HttpResponse<uint8[list]>) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L769)
  A response with the message's status, fields and body. Tagged `#NoDiscard`.
- **Http** · [`export let Next = class implements services.Service<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L777)
  The rest of the chain a middleware calls: the next middleware or the handler. A `Std.Net.Service<Request, Response>`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Next** · [`export let init = func(service: services.Service<Request, Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L782)
  Continues with `service`.
- **Http.Next** · [`export let call = func(request: Request) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L787)
  Runs the rest of the chain.
- **Http.Next** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L790)
  The text "Next".
- **Http** · [`export let MiddlewareFilter = class implements filters.SimpleFilter<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L891)
  A middleware as a `Std.Net.Filter.SimpleFilter<Request, Response>`. Tagged `#Final`, `#NotThreadSafe`.
- **Http.MiddlewareFilter** · [`export let init = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L896)
  Wraps `middleware`.
- **Http.MiddlewareFilter** · [`export let apply = func(request: Request, next: services.Service<Request, Response>) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L901)
  Runs the middleware with `next` as the rest of the chain.
- **Http.MiddlewareFilter** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L907)
  The text "MiddlewareFilter".
- **Http** · [`export let as_filter = func(middleware: func(Request, Next) -> Result<Response>) -> filters.SimpleFilter<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L912)
  `middleware` as a simple filter. Tagged `#NoDiscard`.
- **Http** · [`export let from_filter = func(filter: filters.SimpleFilter<Request, Response>) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L917)
  A simple filter as a middleware. Tagged `#NoDiscard`.
- **Http** · [`export let request_log_to = func(logger: logging.Logger, clock: clocks.Clock) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L924)
  A middleware that logs one line per request ("ID METHOD PATH STATUS Nms", or "error" in place of the status) at Info through `logger`, timing with `clock`'s monotonic time. Tagged `#NoDiscard`.
- **Http** · [`export let request_log = func -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L941)
  `request_log_to` with a logger named "http" writing to standard error and the system clock. Tagged `#NoDiscard`.
- **Http** · [`export let Route = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L948)
  One route: method, path pattern, optional host pattern, handler and the route's own middleware. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Route** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L950)
  The method token.
- **Http.Route** · [`export last pattern: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L952)
  The path pattern, such as "/parts/{id}".
- **Http.Route** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L954)
  The host pattern, or "" for any host.
- **Http.Route** · [`export last handler: func(Request, Params) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L956)
  The handler.
- **Http.Route** · [`export last layers: Layer[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L958)
  The route's middleware, in order.
- **Http.Route** · [`export let init = func(route_method: string, path_pattern: string, host_pattern: string, body: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L961)
  A route without middleware.
- **Http.Route** · [`export let use = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L970)
  Appends a middleware that runs after the earlier ones and before the handler.
- **Http.Route** · [`export let middleware_count = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L975)
  The number of the route's own middlewares.
- **Http.Route** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L978)
  The text "Route(METHOD host/pattern)".
- **Http** · [`export let Routes = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L988)
  A group of routes for `Router.mount`, such as a generated contract adapter's. A group may be bound to a host pattern (an addition to Backend030): an exact host name such as "api.example.com", or "*.example.com" for any host with one or more labels before ".example.com"; case is ignored and the request's port is not compared. The group's `use` middlewares run before each route's own middlewares, after the router's. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Routes** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L990)
  The host pattern, or "".
- **Http.Routes** · [`export last layers: Layer[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L994)
  The group's middleware.
- **Http.Routes** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L997)
  A group for any host, or for `host_pattern` (lowercased).
- **Http.Routes** · [`export let get = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1004)
  Adds a GET route.
- **Http.Routes** · [`export let post = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1008)
  Adds a POST route.
- **Http.Routes** · [`export let put = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1012)
  Adds a PUT route.
- **Http.Routes** · [`export let patch = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1016)
  Adds a PATCH route.
- **Http.Routes** · [`export let delete = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1020)
  Adds a DELETE route.
- **Http.Routes** · [`export let add = func(method: string, pattern: string, handler: func(Request, Params) -> Result<Response>) -> Route`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1025)
  Adds a route for any method token and returns it, so the caller can add the route's own middleware with `Route.use`.
- **Http.Routes** · [`export let use = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1033)
  Adds a middleware for every route of the group.
- **Http.Routes** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1038)
  The number of routes.
- **Http.Routes** · [`export let route_at = func(index: int) -> Route`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1041)
  Route `index`.
- **Http.Routes** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1044)
  The text "Routes(host, N routes)".
- **Http** · [`export let Router = class implements services.Service<Request, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1102)
  The application's routes and middleware; dispatches one request to one response (Backend030, "HTTP"). A `Std.Net.Service<Request, Response>` whose `call` never fails. Patterns are "/" or "/" followed by segments separated by "/": a literal segment of path characters without braces, or `{name}` with an identifier name, which matches one non-empty segment. Registration never fails; an invalid pattern or a second route with the same host, method and shape (parameter names ignored) is recorded and reported by `check`, the startup check, and such a route is never matched. Dispatch: the request's host selects the trees to search: exact host groups, then wildcard groups, in the order mounted, then the routes for any host. The first tree with a route for the method and path wins (literal before parameter at every segment, backtracking). HEAD uses the GET route and drops the body. Without a match, a path that some route matches under other methods is 405 with `Allow` (GET, HEAD, POST, PUT, PATCH, DELETE in that order, then other methods as registered; HEAD is listed with GET), otherwise 404; both carry the framework envelope. The router's `use` middlewares wrap every request, including 404 and 405 answers (so `cors` can answer preflight requests and `request_log` sees misses); a route's group and own middlewares wrap only that route's handler. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Router** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1115)
  An empty router.
- **Http.Router** · [`export let get = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1120)
  Adds a GET route.
- **Http.Router** · [`export let post = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1124)
  Adds a POST route.
- **Http.Router** · [`export let put = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1128)
  Adds a PUT route.
- **Http.Router** · [`export let patch = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1132)
  Adds a PATCH route.
- **Http.Router** · [`export let delete = func(pattern: string, handler: func(Request, Params) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1136)
  Adds a DELETE route.
- **Http.Router** · [`export let mount = func(group: Routes)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1140)
  Adds every route of `group`, with the group's host pattern and middleware.
- **Http.Router** · [`export let use = func(middleware: func(Request, Next) -> Result<Response>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1151)
  Adds a middleware around every request; it runs after the ones added before it.
- **Http.Router** · [`export let on_error = func(sink: func(string) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1157)
  Sends each handler or middleware error, as "request ID failed: MESSAGE", to `sink` instead of standard error.
- **Http.Router** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1162)
  The number of routes registered without problems.
- **Http.Router** · [`export let check = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1173)
  The startup check: Ok when every route is valid and unique; otherwise an error whose message lists the problems. Tagged `#NoDiscard`.
- **Http.Router** · [`export let problems_found = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1180)
  The registration problems, as a new list. Tagged `#NoDiscard`.
- **Http.Router** · [`export let startup_response = func -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1191)
  The answer to the host's startup check request, without calling a route: 200 `{"status":"ok","routes":N}`, or 500 with the envelope code "startup_check_failed" and the problems. Tagged `#NoDiscard`.
- **Http.Router** · [`export let dispatch = func(request: Request) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1204)
  Answers `request`. Never fails: an `Err` from a handler or middleware is logged and becomes 500 with the framework envelope. Tagged `#NoDiscard`.
- **Http.Router** · [`export let dispatch_for_host = func(request: Request) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1210)
  `dispatch` for a host that drops a HEAD response's body itself and computes `Content-Length` from the body it was given (the `Std.App` server host): a HEAD request is answered from the GET route with the body kept. Tagged `#NoDiscard`.
- **Http.Router** · [`export let report_failure = func(request_id: string, problem: errors.Error)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1258)
  Sends "request ID failed: MESSAGE" for `problem` to the error sink (`on_error`), as `dispatch` does for a handler or middleware error. Adapters that fail after dispatch, such as `Std.Net.Http.AppHandler` collecting a streamed body, report through it so every failure reaches one sink.
- **Http.Router** · [`export let call = func(request: Request) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1264)
  `dispatch` as a service call: always Ok.
- **Http.Router** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1267)
  The text "Router(N routes)".
- **Http** · [`export let Authenticator = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1348)
  Establishes who sent a request: Ok(Some) for an authenticated principal, Ok(None) for none, Err when the check itself failed (Backend030, "HTTP").
- **Http** · [`export let BearerAuthenticator = class implements Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1356)
  Bearer tokens (RFC 6750 section 2.1) checked by a function the application supplies. The library defines no token format. Tagged `#Final`, `#NotThreadSafe`.
- **Http.BearerAuthenticator** · [`export let init = func(check: func(string) -> Result<Optional<Principal>>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1361)
  Checks tokens with `check`.
- **Http.BearerAuthenticator** · [`export let authenticate = func(request: Request) -> Result<Optional<Principal>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1367)
  None without an `Authorization: Bearer` field or with a malformed token; otherwise the check's answer.
- **Http.BearerAuthenticator** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1376)
  The fixed text "BearerAuthenticator" (it holds no token).
- **Http** · [`export let bearer = func(check: func(string) -> Result<Optional<Principal>>) -> Authenticator`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1381)
  A bearer authenticator over `check`. Tagged `#NoDiscard`.
- **Http** · [`export let bearer_token = func(request: Request) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1387)
  The token of an `Authorization: Bearer <token>` field (scheme case-insensitive, token of b64token characters, RFC 6750 section 2.1), or None. Tagged `#NoDiscard`.
- **Http** · [`export let require = func(authenticator: Authenticator) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1419)
  A middleware that requires a principal: 401 with the envelope (and `WWW-Authenticate: Bearer` for a bearer authenticator) when there is none; otherwise the rest of the chain sees the request carrying it (`request.principal()`). An `Err` from the authenticator is returned (500). Tagged `#NoDiscard`.
- **Http** · [`export let same_origin = func -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1439)
  A middleware for cookie-authenticated routes: a request with an unsafe method (not GET, HEAD, OPTIONS, TRACE) passes only with `Sec-Fetch-Site: same-origin`, or, without that field, an `Origin` equal to the request's own origin (scheme and Host); otherwise 403 with the envelope. Safe methods pass. Tagged `#NoDiscard`.
- **Http** · [`export let CorsPolicy = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1467)
  Which cross-origin requests `cors` admits: an explicit list of origins (no wildcard), methods, request headers, exposed headers, credentials and the preflight cache time. Tagged `#Final`, `#NotThreadSafe`.
- **Http.CorsPolicy** · [`export let init = func(allowed_origins: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1483)
  A policy for `allowed_origins` (each "scheme://host[:port]"), admitting GET, HEAD and POST and no extra headers. "*", "null" or an empty origin traps with "invalid argument: origin".
- **Http.CorsPolicy** · [`export let allow_methods = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1496)
  Replaces the admitted methods.
- **Http.CorsPolicy** · [`export let allow_headers = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1506)
  Replaces the admitted request headers.
- **Http.CorsPolicy** · [`export let expose_headers = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1516)
  Replaces the exposed response headers.
- **Http.CorsPolicy** · [`export let allow_credentials = func(allowed: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1526)
  Sets whether credentials are allowed.
- **Http.CorsPolicy** · [`export let set_max_age = func(seconds: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1531)
  Sets the preflight cache time; negative sends none.
- **Http.CorsPolicy** · [`export let admits_origin = func(origin: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1536)
  Whether `origin` is admitted.
- **Http.CorsPolicy** · [`export let admits_method = func(method: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1547)
  Whether `method` is admitted.
- **Http.CorsPolicy** · [`export let admits_headers = func(requested: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1557)
  Whether every comma-separated name in `requested` is admitted.
- **Http.CorsPolicy** · [`export let decorate = func(response: Response, origin: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1571)
  Adds the response fields for an admitted actual request from `origin`.
- **Http.CorsPolicy** · [`export let preflight = func(origin: string) -> Response`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1584)
  The 204 answer to an admitted preflight request from `origin`. Tagged `#NoDiscard`.
- **Http.CorsPolicy** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1598)
  The text "CorsPolicy(origins)".
- **Http** · [`export let cors = func(policy: CorsPolicy) -> func(Request, Next) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1608)
  A middleware applying `policy` (Fetch standard CORS protocol). A request without `Origin` passes unchanged. A preflight (OPTIONS with `Access-Control-Request-Method`) is answered here: 204 with the policy's fields when the origin, method and requested headers are admitted, 403 with the envelope otherwise. Other requests run the chain; an admitted origin's response gets `Access-Control-Allow-Origin` (the origin itself, never "*") and `Vary: Origin`; a refused origin's response is left without them, so the browser withholds it. Tagged `#NoDiscard`.
- **Http** · [`export let CredentialsMode = enum (Omit, SameOrigin, Include)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1635)
  Whether the browser sends cookies and HTTP authentication with a fetch. Native requests have no ambient credentials.
- **Http** · [`export let credentials_code = func(mode: CredentialsMode) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1639)
  The `uint32` code of `mode` for `neura.http.fetch`. Tagged `#Pure`, `#ThreadSafe`.
- **Http** · [`export let ClientRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1647)
  An outgoing request: method, URL, header fields, body, deadline and credentials mode. Tagged `#Final`, `#NotThreadSafe`.
- **Http.ClientRequest** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1649)
  The method token.
- **Http.ClientRequest** · [`export last url: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1651)
  The absolute `http` or `https` URL.
- **Http.ClientRequest** · [`export last headers: Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1653)
  The header fields (mutable).
- **Http.ClientRequest** · [`export let init = func(request_method: string, target_url: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1663)
  A request without fields or body; or with them. An invalid method traps with "invalid argument: method".
- **Http.ClientRequest** · [`export let body = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1677)
  The body bytes.
- **Http.ClientRequest** · [`export let set_body = func(body: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1680)
  Replaces the body.
- **Http.ClientRequest** · [`export let deadline = func -> durations.Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1685)
  The deadline as a duration from the start of the exchange.
- **Http.ClientRequest** · [`export let set_deadline = func(span: durations.Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1688)
  Sets the deadline; a negative one traps with "invalid argument: negative deadline".
- **Http.ClientRequest** · [`export let credentials_mode = func -> CredentialsMode`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1696)
  The credentials mode (default SameOrigin, as in the Fetch standard).
- **Http.ClientRequest** · [`export let set_credentials = func(mode: CredentialsMode)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1699)
  Sets the credentials mode.
- **Http.ClientRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1704)
  The text "ClientRequest(METHOD url)".
- **Http** · [`export let Transport = interface`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1708)
  What carries a client request: the host's `neura.http.fetch`, or a test double.
- **Http** · [`export let HostTransport = class implements Transport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1715)
  The host transport through `neura.http.fetch` version 1. Requires the `http_client` capability. Tagged `#Final`, `#NotThreadSafe`.
- **Http.HostTransport** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1717)
  The host transport.
- **Http.HostTransport** · [`export let fetch = func(request: ClientRequest) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1721)
  Sends the request through the host.
- **Http.HostTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1724)
  The text "HostTransport".
- **Http** · [`export let Client = class implements services.Service<ClientRequest, Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1731)
  An HTTP client over a transport (Components030, "HTTP clients"); a `Std.Net.Service<ClientRequest, Response>`. The URL must be absolute `http` or `https`; otherwise the send fails with InvalidInput before the transport is called. Tagged `#Final`, `#NotThreadSafe`.
- **Http.Client** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1738)
  A client over the host's `neura.http.fetch`, or over `carrier`.
- **Http.Client** · [`export let add_default_header = func(name: string, value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1745)
  Adds a field sent with every request that lacks it.
- **Http.Client** · [`export let send = func(request: ClientRequest) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1751)
  Sends `request`. Tagged `#NoDiscard`.
- **Http.Client** · [`export let call = func(request: ClientRequest) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1765)
  `send` as a service call.
- **Http.Client** · [`export let get = func(url: string) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1769)
  A GET of `url`. Tagged `#NoDiscard`.
- **Http.Client** · [`export let post = func(url: string, content_type: string, body: uint8[list]) -> Result<Response>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1773)
  A POST of `body` with `content_type` to `url`. Tagged `#NoDiscard`.
- **Http.Client** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1781)
  The text "Client".
- **Http** · [`export let parse_query = func(query: string) -> Params`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1837)
  The pairs of an `application/x-www-form-urlencoded` query: split on "&", then on the first "="; "+" is a space and percent escapes are decoded (invalid escapes and invalid UTF-8 are kept as written). Tagged `#NoDiscard`.
- **Http** · [`export let percent_decode_segment = func(text: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http.nra#L1859)
  `text` with percent escapes decoded; the text unchanged when an escape is malformed or the result is not UTF-8. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

Host-backed request and response bodies use the assigned server worker. The default client calls the native or browser fetch provider under the HTTP client capability. [Native host services](NVM#http-client-services)
