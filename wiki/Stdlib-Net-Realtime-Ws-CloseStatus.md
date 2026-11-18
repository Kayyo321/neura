# `Std.Net.Realtime.Ws.CloseStatus`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.CloseStatus`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.CloseStatus`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **CloseStatus** · [`export const Normal: int = 1000`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L19)
  1000: the purpose of the connection was fulfilled.
- **CloseStatus** · [`export const GoingAway: int = 1001`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L21)
  1001: the endpoint is going away.
- **CloseStatus** · [`export const ProtocolError: int = 1002`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L23)
  1002: a protocol error.
- **CloseStatus** · [`export const UnsupportedData: int = 1003`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L25)
  1003: a type of data the endpoint cannot accept.
- **CloseStatus** · [`export const NoStatusReceived: int = 1005`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L27)
  1005: reserved, never sent; reported when a close frame had no status.
- **CloseStatus** · [`export const AbnormalClosure: int = 1006`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L29)
  1006: reserved, never sent; reported when the connection ended without a close frame.
- **CloseStatus** · [`export const InvalidPayload: int = 1007`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L31)
  1007: message data inconsistent with its type, such as text that is not UTF-8.
- **CloseStatus** · [`export const PolicyViolation: int = 1008`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L33)
  1008: a message that violates the endpoint's policy.
- **CloseStatus** · [`export const MessageTooBig: int = 1009`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L35)
  1009: a message too big to process.
- **CloseStatus** · [`export const MandatoryExtension: int = 1010`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L37)
  1010: the client expected an extension the server did not negotiate.
- **CloseStatus** · [`export const InternalError: int = 1011`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L39)
  1011: the server met an unexpected condition.
- **CloseStatus** · [`export const TlsHandshake: int = 1015`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L41)
  1015: reserved, never sent; reported when the TLS handshake failed.
- **CloseStatus** · [`export const ErrorCode: string = "ws.protocol"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L44)
  The error code of every `ProtocolFailure`.
- **CloseStatus** · [`export const MaxReasonBytes: int = 123`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L47)
  The longest close reason in bytes: 125 minus the two status bytes.
- **CloseStatus** · [`export let is_valid_on_wire = func(code: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L52)
  Whether an endpoint may put the code in a close frame, and accept it from one (RFC 6455 section 7.4). Tagged `#NoDiscard`, `#Pure`.
- **CloseStatus** · [`export let CloseReason = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L64)
  The status and reason of a close frame. Status 1005 stands for a frame without a body. Tagged `#Final`, `#Immutable`.
- **CloseStatus.CloseReason** · [`export last code: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L66)
  The status code, or 1005 when the frame had no body.
- **CloseStatus.CloseReason** · [`export last reason: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L68)
  The reason text, possibly empty.
- **CloseStatus.CloseReason** · [`export let init = func(status: int, text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L71)
  Stores the code and reason.
- **CloseStatus.CloseReason** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L77)
  Equal when the code and reason are.
- **CloseStatus.CloseReason** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L85)
  Combines the code and the reason.
- **CloseStatus.CloseReason** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L89)
  `code reason`, or the code alone.
- **CloseStatus** · [`export let ProtocolFailure = class:errors.ErrorDetail`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L95)
  The error detail of a WebSocket failure: the status the connection closes with and a message. Tagged `#Final`.
- **CloseStatus.ProtocolFailure** · [`export last status: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L97)
  The close status this failure maps to, such as 1002 or 1009.
- **CloseStatus.ProtocolFailure** · [`export let init = func(code_value: int, message_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L102)
  Stores the status and message.
- **CloseStatus.ProtocolFailure** · [`export let.override code = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L109)
  Always `ws.protocol`.
- **CloseStatus.ProtocolFailure** · [`export let.override message = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L112)
  The message.
- **CloseStatus.ProtocolFailure** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L115)
  Equal when the status and message are.
- **CloseStatus.ProtocolFailure** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L123)
  Combines the status and message.
- **CloseStatus.ProtocolFailure** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L127)
  `ws.protocol status: message`.
- **CloseStatus** · [`export let failure = func(status: int, message: string) -> errors.Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L132)
  A Custom error holding a `ProtocolFailure`. Tagged `#NoDiscard`.
- **CloseStatus** · [`export let status_of = func(problem: errors.Error) -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L138)
  The close status a failure maps to: its `ProtocolFailure` status, otherwise 1011 for any other error. Tagged `#NoDiscard`.
- **CloseStatus** · [`export let encode_body = func(code: int, reason: string) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L146)
  The body of a close frame with the status and reason. Fails when the code is not valid on the wire or the reason is longer than 123 bytes. Tagged `#NoDiscard`.
- **CloseStatus** · [`export let decode_body = func(body: uint8[list]) -> Result<CloseReason>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/close-status.nra#L167)
  Reads a received close frame body (RFC 6455 section 5.5.1). An empty body gives status 1005; a one-byte body, a status not valid on the wire, or a reason that is not UTF-8 fails with status 1002 (1007 for the reason). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
