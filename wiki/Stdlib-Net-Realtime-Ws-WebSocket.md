# `Std.Net.Realtime.Ws.WebSocket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.WebSocket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.WebSocket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **WebSocket** · [`export const MaxHeadBytes: int = 16384`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L39)
  The largest HTTP head `connect` and `accept` read, in bytes.
- **WebSocket** · [`export let ReadyState = enum (Open, Closing, Closed)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L42)
  The states of a connection (RFC 6455 section 4.1 and 7).
- **WebSocket** · [`export let WebSocketOptions = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L46)
  Size limits of a connection. Tagged `#Final`, `#Immutable`.
- **WebSocket.WebSocketOptions** · [`export last max_frame_size: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L48)
  The largest frame payload accepted.
- **WebSocket.WebSocketOptions** · [`export last max_message_size: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L50)
  The largest reassembled message accepted.
- **WebSocket.WebSocketOptions** · [`export last max_fragment_size: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L52)
  The largest payload of a frame `send` writes.
- **WebSocket.WebSocketOptions** · [`export let init = func(frame_limit: int, message_limit: int, fragment: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L56)
  Stores the limits. A frame limit below 125 or a fragment size below 1 traps with "invalid argument: WebSocketOptions".
- **WebSocket.WebSocketOptions** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L66)
  The three limits.
- **WebSocket** · [`export let default_options = func -> WebSocketOptions`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L73)
  1 MiB frames, 16 MiB messages, 64 KiB fragments. Tagged `#NoDiscard`.
- **WebSocket** · [`export let StreamTransport = class implements Transport<uint8[list], uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L78)
  A byte transport over read, write and close functions, such as the bound methods of a `Std.Net.TcpStream`. `receive` reads at most `chunk` bytes; an empty read is the end of the stream. Tagged `#Final`, `#NotThreadSafe`.
- **WebSocket.StreamTransport** · [`export let init = func(read: func(int) -> Result<uint8[list]>, write: func(uint8[list]) -> Result<Unit>, release: func() -> Result<Unit>, size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L89)
  Stores the functions. A chunk below 1 traps with "invalid argument: chunk".
- **WebSocket.StreamTransport** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L101)
  The next bytes, or `None` at the end of the stream.
- **WebSocket.StreamTransport** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L110)
  Writes the bytes.
- **WebSocket.StreamTransport** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L113)
  Releases the stream.
- **WebSocket.StreamTransport** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L116)
  The read size.
- **WebSocket** · [`export let over_tcp = func(stream: tcp.TcpStream) -> StreamTransport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L121)
  A byte transport over a TCP stream, reading 16 KiB at a time. Tagged `#NoDiscard`.
- **WebSocket** · [`export let WebSocket = class implements FramedTransport<Message>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L126)
  One WebSocket connection. Tagged `#Final`, `#NotThreadSafe`.
- **WebSocket.WebSocket** · [`export last role: frames.Role`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L130)
  Which end this is.
- **WebSocket.WebSocket** · [`export last protocol: Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L140)
  The subprotocol agreed in the handshake, if any.
- **WebSocket.WebSocket** · [`export let state: ReadyState = ReadyState.Open { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L142)
  The connection state.
- **WebSocket.WebSocket** · [`export let peer_close: Optional<close_status.CloseReason> = Optional<close_status.CloseReason>.None { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L146)
  The status and reason of the peer's close frame, once received.
- **WebSocket.WebSocket** · [`export let pongs: int = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L158)
  The number of pongs received.
- **WebSocket.WebSocket** · [`export let init = func(bytes: Transport<uint8[list], uint8[list]>, endpoint: frames.Role, masks: Random, limits: WebSocketOptions, agreed: Optional<string>, leftover: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L162)
  A connection whose handshake is done; `leftover` holds bytes read past the handshake.
- **WebSocket.WebSocket** · [`export let receive = func -> Result<Optional<Message>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L178)
  The next data message, `None` after the closing handshake, or the failure that ended the connection. An Io error of kind TimedOut from the transport (a TCP read timeout) is returned without closing anything: the caller may check the heartbeat and call `receive` again. Tagged `#NoDiscard`.
- **WebSocket.WebSocket** · [`export let send = func(data: Message) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L253)
  Sends a message, fragmented as the options say. Fails with an Io error of kind Closed once this end has started closing. Tagged `#NoDiscard`.
- **WebSocket.WebSocket** · [`export let ping = func(payload: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L266)
  Sends a ping with the payload (at most 125 bytes, or it fails with status 1002). Tagged `#NoDiscard`.
- **WebSocket.WebSocket** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L275)
  The closing handshake with status 1000 and no reason.
- **WebSocket.WebSocket** · [`export let close_with = func(code: int, reason: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L282)
  The closing handshake (RFC 6455 section 7.1.2): sends a close frame with the status and reason, reads until the peer's close frame (data messages are discarded) or the end of the stream, and closes the transport. A status not valid on the wire or a reason over 123 bytes fails before anything is sent. If the wait fails (a read timeout included), the transport is closed and the failure returned. Closing a closed connection returns Ok.
- **WebSocket.WebSocket** · [`export let send_close = func(code: int, reason: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L322)
  Starts the closing handshake without waiting: sends a close frame with the status and reason and moves to Closing; `receive` then returns `None` once the peer's close frame arrives. Fails like `close_with` for an invalid status or reason, and with an Io error of kind Closed when a close frame was already sent. Tagged `#NoDiscard`.
- **WebSocket.WebSocket** · [`export let supervise = func(supervisor_value: heartbeat.Heartbeat, source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L335)
  Attaches a heartbeat supervisor that reads `source`'s monotonic time. Every received frame counts as activity.
- **WebSocket.WebSocket** · [`export let check_heartbeat = func -> Result<heartbeat.HeartbeatAction>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L343)
  Runs the attached heartbeat once: sends the ping it asks for, or on `PeerDead` closes the transport without a closing handshake. Without a supervisor it answers `Wait`. Tagged `#NoDiscard`.
- **WebSocket.WebSocket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L369)
  The role, state and protocol.
- **WebSocket** · [`export let over_upgraded = func(bytes: Transport<uint8[list], uint8[list]>, role: frames.Role, random: Random, options: WebSocketOptions, protocol: Optional<string>) -> WebSocket`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L462)
  Wraps a transport whose opening handshake already happened (for instance an HTTP/2 or HTTP/3 stream opened by extended CONNECT). Tagged `#NoDiscard`.
- **WebSocket** · [`export let connect = func(bytes: Transport<uint8[list], uint8[list]>, request: handshake.ClientRequest, random: Random, options: WebSocketOptions) -> Result<WebSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L471)
  The client opening handshake: writes the request, reads and validates the response (`Handshake.validate_server_response`), and returns the open connection. A response that agrees to an extension fails with status 1010, since this connection implements none. On failure the transport is closed. Tagged `#NoDiscard`.
- **WebSocket** · [`export let accept = func(bytes: Transport<uint8[list], uint8[list]>, supported: string[list], random: Random, options: WebSocketOptions) -> Result<WebSocket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/web-socket.nra#L521)
  The server opening handshake: reads and validates the request (`Handshake.validate_client_request`), chooses the first offered protocol in `supported`, writes the 101 response without extensions, and returns the open connection. An invalid request is answered with 400 (426 and Sec-WebSocket-Version for a wrong version), the transport is closed, and the validation error is returned. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
