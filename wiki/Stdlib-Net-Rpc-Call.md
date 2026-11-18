# `Std.Net.Rpc.Call`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Rpc.Call`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Rpc.Call`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Call** · [`export const GrpcContentType: string = "application/grpc"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L24)
  The content type prefix of native gRPC.
- **Call** · [`export let MethodPath = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L28)
  A method path `/Service-Name/method`, where the service name is `package.Service` for protobuf. Tagged `#Final`.
- **Call.MethodPath** · [`export last service: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L30)
  The fully qualified service name, such as `grpc.health.v1.Health`.
- **Call.MethodPath** · [`export last method: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L32)
  The method name, such as `Check`.
- **Call.MethodPath** · [`export let init = func(service_name: string, method_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L36)
  A path from its parts; an empty part or one containing `/` traps with "invalid argument: method path".
- **Call.MethodPath** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L46)
  Equal when the service and method are (case-sensitive).
- **Call.MethodPath** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L54)
  Combines both parts.
- **Call.MethodPath** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L58)
  The `:path` value `/service/method`.
- **Call** · [`export let parse_path = func(text: string) -> Result<MethodPath>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L63)
  Parses a `:path` of the form `/service/method`; anything else is a Parse error. Tagged `#NoDiscard`.
- **Call** · [`export let RpcRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L75)
  One call's request: the method, metadata, messages, and optional timeout. Tagged `#Final`.
- **Call.RpcRequest** · [`export last path: MethodPath`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L77)
  The method.
- **Call.RpcRequest** · [`export last metadata: meta.Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L79)
  Custom metadata.
- **Call.RpcRequest** · [`export last messages: uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L81)
  The request messages (one for a unary or server-streaming call).
- **Call.RpcRequest** · [`export last timeout: Optional<Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L83)
  The time the client allows, sent as `grpc-timeout`.
- **Call.RpcRequest** · [`export let init = func(method_path: MethodPath, request_metadata: meta.Metadata, request_messages: uint8[list][list], budget: Optional<Duration>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L86)
  A request with every part given.
- **Call.RpcRequest** · [`export let with_timeout = func(budget: Optional<Duration>) -> RpcRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L96)
  The same request with another timeout. Tagged `#NoDiscard`.
- **Call.RpcRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L100)
  The path, message count and timeout.
- **Call** · [`export let unary_request = func(method_path: MethodPath, message: uint8[list], request_metadata: meta.Metadata, budget: Optional<Duration>) -> RpcRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L110)
  A unary request with one message. Tagged `#NoDiscard`.
- **Call** · [`export let RpcResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L119)
  One call's response: header metadata, messages, final status and trailing metadata. Tagged `#Final`.
- **Call.RpcResponse** · [`export last headers: meta.Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L121)
  Custom metadata from the response headers.
- **Call.RpcResponse** · [`export last messages: uint8[list][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L123)
  The response messages.
- **Call.RpcResponse** · [`export last status: statuses.RpcStatus`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L125)
  The final statuses.
- **Call.RpcResponse** · [`export last trailers: meta.Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L127)
  Custom trailing metadata (without the `grpc-status`, `grpc-message`, details entries).
- **Call.RpcResponse** · [`export let init = func(header_metadata: meta.Metadata, response_messages: uint8[list][list], final_status: statuses.RpcStatus, trailer_metadata: meta.Metadata)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L130)
  A response with every part given.
- **Call.RpcResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L139)
  The status and message count.
- **Call** · [`export let unary_response = func(message: uint8[list]) -> RpcResponse`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L145)
  An OK response with one message and no metadata. Tagged `#NoDiscard`.
- **Call** · [`export let status_response = func(final_status: statuses.RpcStatus) -> RpcResponse`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L153)
  A response with no messages and a status (sent Trailers-Only when the status is not OK). Tagged `#NoDiscard`.
- **Call** · [`export let WireRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L158)
  An HTTP request as gRPC sends it: header pairs in order and the whole body. Tagged `#Final`.
- **Call.WireRequest** · [`export last headers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L160)
  Header pairs, pseudo-headers first.
- **Call.WireRequest** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L162)
  The length-prefixed body.
- **Call.WireRequest** · [`export let init = func(header_pairs: [string, string][list], data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L165)
  A request with both parts.
- **Call.WireRequest** · [`export let header = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L172)
  The first value of header `name` (case-insensitive). Tagged `#NoDiscard`.
- **Call.WireRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L175)
  The header count and body size.
- **Call** · [`export let WireResponse = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L181)
  An HTTP response as gRPC receives it: status, header pairs, whole body, trailer pairs. Tagged `#Final`.
- **Call.WireResponse** · [`export last http_status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L183)
  The HTTP statuses.
- **Call.WireResponse** · [`export last headers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L185)
  Header pairs (without `:status`).
- **Call.WireResponse** · [`export last body: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L187)
  The length-prefixed body.
- **Call.WireResponse** · [`export last trailers: [string, string][list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L189)
  Trailer pairs (empty for Trailers-Only).
- **Call.WireResponse** · [`export let init = func(code: int, header_pairs: [string, string][list], data: uint8[list], trailer_pairs: [string, string][list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L192)
  A response with every part.
- **Call.WireResponse** · [`export let header = func(name: string) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L202)
  The first value of header `name` (case-insensitive). Tagged `#NoDiscard`.
- **Call.WireResponse** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L205)
  The status, header count, body size and trailer count.
- **Call** · [`export let is_grpc_content_type = func(content_type: string) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L212)
  Whether a content type is gRPC: `application/grpc` alone or followed by `+` or `;`. Tagged `#NoDiscard`.
- **Call** · [`export let encode_request = func(request: RpcRequest, authority: string, scheme: string, subtype: string) -> WireRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L222)
  The client's HTTP request: `:method POST`, `:scheme`, `:path`, `:authority`, then `grpc-timeout` when set, `content-type application/grpc+<subtype>`, `te trailers`, then the custom metadata; the body holds each message uncompressed. Tagged `#NoDiscard`.
- **Call** · [`export let decode_request = func(wire: WireRequest, max_message_size: int) -> Result<RpcRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L253)
  The server's reading of an HTTP request (content type already checked): the path (UNIMPLEMENTED when malformed), `grpc-timeout` (INTERNAL when malformed), `grpc-encoding` (UNIMPLEMENTED unless absent or `identity`), custom metadata (reserved `grpc-` entries and pseudo-headers removed), and the messages (framing errors as in `Framing.MessageDecoder`). Failures are `Std.Net.Rpc.Status` errors. Tagged `#NoDiscard`.
- **Call** · [`export let encode_response = func(response: RpcResponse, subtype: string) -> WireResponse`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L298)
  The server's HTTP response: `:status 200` with `content-type`; Trailers-Only (status in the header block) when there are no messages and the status is not OK; otherwise headers, the framed messages, and trailers with the status first and then the custom trailing metadata. Tagged `#NoDiscard`.
- **Call** · [`export let decode_response = func(wire: WireResponse, max_message_size: int) -> Result<RpcResponse>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L332)
  The client's reading of an HTTP response. A non-200 status or a non-gRPC content type gives a status synthesized from the HTTP status (http-grpc-status-mapping.md); a Trailers-Only response takes its status from the header block. Fails only on framing errors of the body (INTERNAL or RESOURCE_EXHAUSTED). Tagged `#NoDiscard`.
- **Call** · [`export let custom_metadata = func(pairs: [string, string][list]) -> meta.Metadata`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/rpc/call.nra#L364)
  Custom metadata of header pairs: pseudo-headers, the call-definition headers and reserved `grpc-` names removed. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
