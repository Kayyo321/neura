# `Std.Net.Realtime.Ws.ExtendedConnect`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.ExtendedConnect`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.ExtendedConnect`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ExtendedConnect** · [`export const SettingsEnableConnectProtocol: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L25)
  The identifier of SETTINGS_ENABLE_CONNECT_PROTOCOL in HTTP/2 (RFC 8441 section 9.1) and HTTP/3 (RFC 9220 section 5).
- **ExtendedConnect** · [`export const ProtocolToken: string = "websocket"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L28)
  The `:protocol` value that bootstraps a WebSocket.
- **ExtendedConnect** · [`export let enables_extended_connect = func(value: int64) -> Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L33)
  Whether a received SETTINGS_ENABLE_CONNECT_PROTOCOL value allows extended CONNECT: 1 allows it, 0 does not, and any other value is a connection error (RFC 8441 section 3). Tagged `#NoDiscard`.
- **ExtendedConnect** · [`export let request_headers = func(secure: bool, authority: string, path: string, protocols: string[list], extensions: string, origin: string) -> handshake.Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L44)
  The request fields of an extended CONNECT for a WebSocket (RFC 8441 section 5 and its example in section 5.1). `secure` selects `https` (wss) or `http` (ws); an empty `origin` or `extensions` is left out, as is an empty protocol list. Tagged `#NoDiscard`.
- **ExtendedConnect** · [`export let validate_request = func(headers: handshake.Header[list]) -> Result<handshake.ClientOffer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L71)
  Checks an extended CONNECT request as a server must. Requires lowercase field names, `:method CONNECT`, `:protocol websocket` (another token fails with a message naming it; RFC 9220 section 3 suggests answering 501), non-empty `:scheme` (`http` or `https`), `:path` and `:authority`, no Connection or Upgrade field, and `sec-websocket-version: 13`. The offer's key is empty: extended CONNECT has no key. Tagged `#NoDiscard`.
- **ExtendedConnect** · [`export let response_headers = func(protocol: Optional<string>, extensions: string) -> handshake.Header[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L106)
  The fields of a successful response (`:status 200`) with the chosen subprotocol and extension response. Tagged `#NoDiscard`.
- **ExtendedConnect** · [`export let validate_response = func(offered_protocols: string[list], offered_extensions: string, headers: handshake.Header[list]) -> Result<handshake.ServerAcceptance>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L124)
  Checks a response as a client must: `:status` 2xx (a CONNECT succeeds with 2xx, RFC 9110 section 9.3.6), a subprotocol the client offered if any, and only extensions the client offered. Tagged `#NoDiscard`.
- **ExtendedConnect** · [`export let connect_http2 = func(secure: bool, authority: string, path: string, protocols: string[list], random: Random, options: ws.WebSocketOptions) -> Result<ws.WebSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L165)
  Opens a WebSocket on a new HTTP/2 stream of a connection to `authority` with extended CONNECT. Tagged `#NoDiscard`.
- **ExtendedConnect** · [`export let connect_http3 = func(authority: string, path: string, protocols: string[list], random: Random, options: ws.WebSocketOptions) -> Result<ws.WebSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/extended-connect.nra#L174)
  Opens a WebSocket on a new HTTP/3 request stream of a QUIC connection to `authority` with extended CONNECT. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
