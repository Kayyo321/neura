# `Std.Net.Realtime.WebTransport.Session`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.WebTransport.Session`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.WebTransport.Session`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Session** · [`export const ProtocolToken: string = "webtransport"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L25)
  The `:protocol` value of a WebTransport extended CONNECT.
- **Session** · [`export const SettingsWtMaxSessions: uint64 = 350866729`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L27)
  SETTINGS_WT_MAX_SESSIONS (0x14e9cd29), the number of concurrent sessions a peer accepts.
- **Session** · [`export const SettingsWtInitialMaxStreamsUni: uint64 = 11108`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L29)
  SETTINGS_WT_INITIAL_MAX_STREAMS_UNI (0x2b64).
- **Session** · [`export const SettingsWtInitialMaxStreamsBidi: uint64 = 11109`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L31)
  SETTINGS_WT_INITIAL_MAX_STREAMS_BIDI (0x2b65).
- **Session** · [`export const SettingsWtInitialMaxData: uint64 = 11105`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L33)
  SETTINGS_WT_INITIAL_MAX_DATA (0x2b61).
- **Session** · [`export const BufferedStreamRejected: uint64 = 966049156`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L35)
  The HTTP/3 error WT_BUFFERED_STREAM_REJECTED (0x3994bd84).
- **Session** · [`export const SessionGone: uint64 = 386759528`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L37)
  The HTTP/3 error WT_SESSION_GONE (0x170d7b68).
- **Session** · [`export const FlowControlError: uint64 = 73221255`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L39)
  The HTTP/3 error WT_FLOW_CONTROL_ERROR (0x045d4487).
- **Session** · [`export let Field = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L43)
  One header field of an HTTP/3 request or response. Tagged `#Final`, `#Immutable`.
- **Session.Field** · [`export last name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L45)
  The lowercase field name.
- **Session.Field** · [`export last value: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L47)
  The value.
- **Session.Field** · [`export let init = func(field_name: string, text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L50)
  Stores the name and value.
- **Session.Field** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L56)
  Equal when the name and value are.
- **Session.Field** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L64)
  Combines the name and value.
- **Session.Field** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L67)
  `name: value`.
- **Session** · [`export let SessionRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L72)
  What a server learned from a valid session request. Tagged `#Final`, `#Immutable`.
- **Session.SessionRequest** · [`export last authority: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L74)
  The `:authority` value.
- **Session.SessionRequest** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L76)
  The `:path` value.
- **Session.SessionRequest** · [`export last origin: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L78)
  The `origin` value, or the empty string.
- **Session.SessionRequest** · [`export let init = func(host: string, target: string, origin_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L81)
  Stores the fields.
- **Session.SessionRequest** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L88)
  Equal when the authority, path and origin are.
- **Session.SessionRequest** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L96)
  Combines the authority, path and origin, consistent with equals.
- **Session.SessionRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L100)
  The authority, path and origin.
- **Session** · [`export let connect_request = func(authority: string, path: string, origin: string) -> Field[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L106)
  The request fields that open a session (draft section 3.2). An empty origin is left out. Tagged `#NoDiscard`.
- **Session** · [`export let validate_connect_request = func(fields: Field[list]) -> Result<SessionRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L123)
  Checks a session request as a server must: `:method CONNECT`, `:protocol webtransport`, `:scheme https`, and non-empty `:authority` and `:path`. The caller then checks the origin and answers 404 for an unknown resource or 403 for a refused origin (draft section 3.2). Tagged `#NoDiscard`.
- **Session** · [`export let accepts = func(status: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L142)
  Whether a response status accepts the session (2xx). Tagged `#NoDiscard`.
- **Session** · [`export let WebTransportSession = class implements MultiplexedTransport<int64, uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L153)
  One WebTransport session: streams identified by QUIC stream IDs carrying byte chunks, and datagrams through `send` and `receive`. The session logic is real and runs over the private QUIC host-call wrappers of this module, which trap until the runtime provides QUIC: `open_stream` writes the stream header (`Wire.uni_stream_header` or `Wire.bidi_stream_header`), `accept_stream` reads and checks it, `send` and `receive` frame datagrams with the quarter stream ID (datagrams of other sessions are skipped), and `close_with` sends the WT_CLOSE_SESSION capsule on the request stream and finishes it. Tagged `#Final`, `#NotThreadSafe`.
- **Session.WebTransportSession** · [`export last session_id: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L157)
  The session ID: the ID of the CONNECT request stream.
- **Session.WebTransportSession** · [`export last is_client: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L159)
  Whether this end is the client.
- **Session.WebTransportSession** · [`export let init = func(handle: int64, id: int64, client: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L163)
  A session with the given ID on a QUIC connection handle. An ID that is not a client-initiated bidirectional stream ID traps with "invalid argument: session id".
- **Session.WebTransportSession** · [`export let receive = func -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L174)
  The next datagram of the session, or `None` once the connection is closed. Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let send = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L194)
  Sends one datagram (unreliable, unordered). Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let open_stream = func(bidirectional: bool) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L199)
  Opens a stream and writes the WebTransport header of this session on it. Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let accept_stream = func -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L209)
  Waits for the next stream the peer opens and reads its WebTransport header. A stream for another session, or without a WebTransport header, fails. Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let send_on = func(stream: int64, frame: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L242)
  Writes bytes on a stream of this session. Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let receive_on = func(stream: int64) -> Result<Optional<uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L247)
  The next bytes of a stream (at most 64 KiB), or `None` once the peer finished it. Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let finish_stream = func(stream: int64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L255)
  Finishes the local side of a stream (QUIC FIN). Tagged `#NoDiscard`.
- **Session.WebTransportSession** · [`export let close_with = func(reason: wire.SessionClose) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L259)
  Ends the session: the WT_CLOSE_SESSION capsule (`Wire.encode_close_capsule`) in an HTTP/3 DATA frame on the request stream, which is then finished.
- **Session.WebTransportSession** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L263)
  Ends the session with code 0 and no message.
- **Session.WebTransportSession** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L266)
  The session ID and role.
- **Session** · [`export let connect = func(authority: string, path: string, origin: string) -> Result<WebTransportSession>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/session.nra#L274)
  Opens a QUIC connection to `authority`, waits for the server's settings to allow WebTransport, and sends the session request. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
