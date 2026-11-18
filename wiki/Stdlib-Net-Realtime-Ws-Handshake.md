# `Std.Net.Realtime.Ws.Handshake`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.Handshake`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.Handshake`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Handshake** · [`export const Guid: string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L21)
  The GUID a server appends to the client's key (RFC 6455 section 1.3).
- **Handshake** · [`export const Version: string = "13"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L24)
  The only protocol version RFC 6455 defines (section 4.1).
- **Handshake** · [`export const ErrorCode: string = "ws.handshake"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L27)
  The error code of a handshake failure.
- **Handshake** · [`export let Header = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L31)
  One header field: a name and its value. Tagged `#Final`, `#Immutable`.
- **Handshake.Header** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L33)
  The field name as written.
- **Handshake.Header** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L35)
  The field value with surrounding whitespace removed.
- **Handshake.Header** · [`export let init = func(field: string, text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L38)
  Stores the name and value.
- **Handshake.Header** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L44)
  Equal when the names match without ASCII case and the values match exactly.
- **Handshake.Header** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L53)
  Combines the lowercase name and the value.
- **Handshake.Header** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L57)
  `name: value`.
- **Handshake** · [`export let ClientRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L62)
  The opening request a client sends (RFC 6455 section 4.1). Tagged `#Final`.
- **Handshake.ClientRequest** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L64)
  The Host header value, `host` or `host:port`.
- **Handshake.ClientRequest** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L66)
  The request target: the resource name of the WebSocket URI, starting with `/`.
- **Handshake.ClientRequest** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L68)
  The base64 nonce sent as Sec-WebSocket-Key.
- **Handshake.ClientRequest** · [`export last protocols: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L70)
  The subprotocols offered, most preferred first.
- **Handshake.ClientRequest** · [`export last extensions: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L72)
  The Sec-WebSocket-Extensions offer, or the empty string for none.
- **Handshake.ClientRequest** · [`export let init = func(host_value: string, target: string, nonce: string, offered: string[list], extension_offer: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L75)
  Stores the fields; use `client_request` to make one with a fresh key.
- **Handshake.ClientRequest** · [`export let headers = func -> Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L86)
  The request headers in the order they are written. Tagged `#NoDiscard`.
- **Handshake.ClientRequest** · [`export let to_text = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L104)
  The HTTP/1.1 request head, ending with the empty line. Tagged `#NoDiscard`.
- **Handshake.ClientRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L107)
  The host, path and offered protocols; the key is shown because it is a public nonce.
- **Handshake** · [`export let ClientOffer = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L113)
  What a server learned from a valid client request (RFC 6455 section 4.2.1). Tagged `#Final`.
- **Handshake.ClientOffer** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L115)
  The request target.
- **Handshake.ClientOffer** · [`export last host: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L117)
  The Host header value.
- **Handshake.ClientOffer** · [`export last key: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L119)
  The Sec-WebSocket-Key value.
- **Handshake.ClientOffer** · [`export last origin: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L121)
  The Origin header value, or the empty string when absent.
- **Handshake.ClientOffer** · [`export last protocols: string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L123)
  The offered subprotocols in the client's order.
- **Handshake.ClientOffer** · [`export last extensions: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L125)
  The Sec-WebSocket-Extensions offer, or the empty string.
- **Handshake.ClientOffer** · [`export let init = func(target: string, host_value: string, nonce: string, origin_value: string, offered: string[list], extension_offer: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L128)
  Stores the fields; `validate_client_request` makes one.
- **Handshake.ClientOffer** · [`export let accept_headers = func(protocol: Optional<string>, extension_response: string) -> Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L141)
  The response headers that accept this offer with the chosen subprotocol and extension response (each `None` or empty to choose none), status 101. Tagged `#NoDiscard`.
- **Handshake.ClientOffer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L159)
  The path, host and offered protocols.
- **Handshake** · [`export let ServerAcceptance = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L165)
  What a client learned from a valid server response (RFC 6455 section 4.1). Tagged `#Final`, `#Immutable`.
- **Handshake.ServerAcceptance** · [`export last protocol: Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L167)
  The subprotocol the server chose, if any.
- **Handshake.ServerAcceptance** · [`export last extensions: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L169)
  The Sec-WebSocket-Extensions response, or the empty string.
- **Handshake.ServerAcceptance** · [`export let init = func(chosen: Optional<string>, extension_response: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L172)
  Stores the fields.
- **Handshake.ServerAcceptance** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L178)
  The chosen protocol and extensions.
- **Handshake** · [`export let RequestHead = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L189)
  An HTTP/1.1 request head: method, target and headers. Tagged `#Final`.
- **Handshake.RequestHead** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L191)
  The method token.
- **Handshake.RequestHead** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L193)
  The request target.
- **Handshake.RequestHead** · [`export last version: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L195)
  The HTTP version, such as `HTTP/1.1`.
- **Handshake.RequestHead** · [`export last headers: Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L197)
  The header fields in order.
- **Handshake.RequestHead** · [`export let init = func(verb: string, request_target: string, http_version: string, fields: Header[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L200)
  Stores the fields.
- **Handshake.RequestHead** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L208)
  The request line.
- **Handshake** · [`export let ResponseHead = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L213)
  An HTTP/1.1 response head: status, reason and headers. Tagged `#Final`.
- **Handshake.ResponseHead** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L215)
  The three-digit status code.
- **Handshake.ResponseHead** · [`export last reason: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L217)
  The reason phrase, possibly empty.
- **Handshake.ResponseHead** · [`export last headers: Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L219)
  The header fields in order.
- **Handshake.ResponseHead** · [`export let init = func(code: int, phrase: string, fields: Header[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L222)
  Stores the fields.
- **Handshake.ResponseHead** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L229)
  The status line.
- **Handshake** · [`export let sha1 = func(bytes: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L237)
  The 20-byte SHA-1 hash (FIPS 180-4) of the bytes. Exposed for the handshake and for tests of it. SHA-1 is not collision resistant; do not use it for new integrity or signature purposes. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let accept_key = func(key: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L241)
  The Sec-WebSocket-Accept value for a key: base64(SHA-1(key + Guid)) (RFC 6455 section 4.2.2). Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let generate_key = func(random: Random) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L249)
  A fresh Sec-WebSocket-Key: 16 bytes from `random`, base64 encoded (RFC 6455 section 4.1). RFC 6455 asks for a randomly selected nonce; pass a Random seeded from entropy in production and a seeded one in tests. Tagged `#NoDiscard`.
- **Handshake** · [`export let is_valid_key = func(key: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L253)
  Whether the key is base64 text that decodes to exactly 16 bytes. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let client_request = func(host: string, path: string, random: Random, protocols: string[list], extensions: string) -> ClientRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L261)
  A client request for `path` on `host` with a key from `random`, offering the protocols and the extension offer (empty for none). Tagged `#NoDiscard`.
- **Handshake** · [`export let header_value = func(headers: Header[list], name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L267)
  The comma-joined value of every header named `name` (ASCII case ignored), or `None` when absent. Tagged `#NoDiscard`.
- **Handshake** · [`export let split_list = func(value: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L281)
  The trimmed, non-empty elements of a comma-separated header value. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let list_contains = func(value: string, token: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L294)
  Whether a comma-separated header value contains `token`, ASCII case ignored. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let validate_client_request = func(head: RequestHead) -> Result<ClientOffer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L310)
  Checks a client's opening request as a server must (RFC 6455 section 4.2.1). Requires the GET method, HTTP/1.1 or later, a Host header, an Upgrade list containing `websocket` and a Connection list containing `Upgrade` (ASCII case ignored), a Sec-WebSocket-Key that decodes to 16 bytes, and Sec-WebSocket-Version 13. A wrong version fails with a message naming it; a server should then answer with `version_mismatch_headers` and status 426. Tagged `#NoDiscard`.
- **Handshake** · [`export let version_mismatch_headers = func -> Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L343)
  The headers of a 426 response to a request with an unsupported version (RFC 6455 section 4.4). Tagged `#NoDiscard`.
- **Handshake** · [`export let choose_protocol = func(offered: string[list], supported: string[list]) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L352)
  The first of the client's protocols that the server supports, in the client's order of preference, or `None`. Tagged `#NoDiscard`.
- **Handshake** · [`export let validate_server_response = func(request: ClientRequest, head: ResponseHead) -> Result<ServerAcceptance>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L369)
  Checks a server's response as a client must (RFC 6455 section 4.1, "the client MUST validate"). Requires status 101, an Upgrade list containing `websocket`, a Connection list containing `Upgrade`, a Sec-WebSocket-Accept equal to `accept_key(request.key)`, a Sec-WebSocket-Protocol (if any) that the request offered, and Sec-WebSocket-Extensions (if any) naming only extensions the request offered. Tagged `#NoDiscard`.
- **Handshake** · [`export let extension_names = func(value: string) -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L421)
  The extension names of a Sec-WebSocket-Extensions value, in order (parameters dropped). Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let request_text = func(method: string, target: string, headers: Header[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L433)
  An HTTP/1.1 request head: request line, header lines and the empty line, each ending in CRLF. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let response_text = func(status: int, reason: string, headers: Header[list]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L443)
  An HTTP/1.1 response head with the status and reason, ending with the empty line. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let head_end = func(bytes: uint8[list]) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L453)
  The byte offset just past the first CRLF CRLF in `bytes` (the end of an HTTP head), or -1. Tagged `#NoDiscard`, `#Pure`.
- **Handshake** · [`export let parse_request_head = func(text: string) -> Result<RequestHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L465)
  Parses an HTTP/1.1 request head (request line and header lines, CRLF line ends; the final empty line may be present or not). Tagged `#NoDiscard`.
- **Handshake** · [`export let parse_response_head = func(text: string) -> Result<ResponseHead>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/handshake.nra#L490)
  Parses an HTTP/1.1 response head (status line and header lines). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
