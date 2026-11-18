# `Std.Net.Http.Message`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Message`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Message`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Message** · [`export let Version = enum (Http10, Http11, Http2, Http3)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L18)
  The protocol a message travels on.
- **Message** · [`export let version_text = func(v: Version) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L22)
  The version's text: "HTTP/1.0", "HTTP/1.1", "HTTP/2", "HTTP/3". Tagged `#Pure`, `#ThreadSafe`.
- **Message** · [`export let of_alpn = func(protocol_id: string) -> Optional<Version>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L32)
  The version named by an ALPN protocol id: "http/1.1", "h2", "h3" (RFC 7301, RFC 9113 section 3.1, RFC 9114 section 3.1); None for any other id. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Message** · [`export let HttpRequest = class<Body>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L47)
  A request: method, request target, header map and body. Tagged `#Final`, `#NotThreadSafe`.
- **Message.HttpRequest** · [`export last method: methods.HttpMethod`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L49)
  The method.
- **Message.HttpRequest** · [`export last target: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L51)
  The request target as sent: origin form `/path?query`, or absolute, authority or `*` form.
- **Message.HttpRequest** · [`export last headers: header_maps.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L53)
  The header fields; a mutable map owned by the message.
- **Message.HttpRequest** · [`export last body: Body`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L55)
  The body.
- **Message.HttpRequest** · [`export last version: Version`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L57)
  The protocol that carried or will carry the message.
- **Message.HttpRequest** · [`export let init = func(request_method: methods.HttpMethod, request_target: string, fields: header_maps.Headers, content: Body, protocol: Version)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L60)
  A request over the given protocol.
- **Message.HttpRequest** · [`export let method_name = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L70)
  The method's token.
- **Message.HttpRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L73)
  The text "HttpRequest(METHOD target VERSION)".
- **Message** · [`export let HttpResponse = class<Body>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L79)
  A response: status, header map and body. Tagged `#Final`, `#NotThreadSafe`.
- **Message.HttpResponse** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L81)
  The status code, 100 through 599.
- **Message.HttpResponse** · [`export last headers: header_maps.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L83)
  The header fields; a mutable map owned by the message.
- **Message.HttpResponse** · [`export last body: Body`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L85)
  The body.
- **Message.HttpResponse** · [`export last version: Version`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L87)
  The protocol that carried or will carry the message.
- **Message.HttpResponse** · [`export let init = func(code: int, fields: header_maps.Headers, content: Body, protocol: Version)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L91)
  A response over the given protocol. A status outside 100 through 599 traps with "invalid argument: status".
- **Message.HttpResponse** · [`export let reason = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L100)
  The status's reason phrase (RFC 9110 section 15), or "".
- **Message.HttpResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/message.nra#L103)
  The text "HttpResponse(STATUS VERSION)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
