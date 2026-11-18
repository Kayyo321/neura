# `Std.Net.Realtime.WebTransport.Wire`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.WebTransport.Wire`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.WebTransport.Wire`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Wire** · [`export const MaxVarint: uint64 = 4611686018427387903`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L22)
  The largest QUIC variable-length integer, 2^62 - 1.
- **Wire** · [`export const MaxQuarterStreamId: uint64 = 1152921504606846975`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L24)
  The largest quarter stream ID of an HTTP/3 datagram, 2^60 - 1 (RFC 9297 section 2.1).
- **Wire** · [`export const UniStreamType: uint64 = 84`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L26)
  The HTTP/3 unidirectional stream type of a WebTransport stream (0x54).
- **Wire** · [`export const BidiSignal: uint64 = 65`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L28)
  The signal that starts a bidirectional WebTransport stream, WT_STREAM (0x41).
- **Wire** · [`export const CloseSessionCapsule: uint64 = 10307`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L30)
  The WT_CLOSE_SESSION capsule type (0x2843).
- **Wire** · [`export const MaxCloseMessageBytes: int = 1024`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L32)
  The longest WT_CLOSE_SESSION message in bytes.
- **Wire** · [`export const FirstApplicationError: uint64 = 91141958510811`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L34)
  The first WT_APPLICATION_ERROR codepoint (0x52e4a40fa8db).
- **Wire** · [`export const LastApplicationError: uint64 = 91146396643682`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L36)
  The last WT_APPLICATION_ERROR codepoint (0x52e5ac983162).
- **Wire** · [`export let StreamType = enum ( ClientBidirectional, ServerBidirectional, ClientUnidirectional, ServerUnidirectional )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L39)
  The four stream types of RFC 9000 section 2.1, by the two low bits of the ID.
- **Wire** · [`export let stream_type = func(id: int64) -> StreamType`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L47)
  The type of a stream ID. A negative ID or one above 2^62 - 1 traps with "invalid argument: stream id". Tagged `#NoDiscard`.
- **Wire** · [`export let is_bidirectional = func(id: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L61)
  Whether the stream ID names a bidirectional stream. Tagged `#NoDiscard`.
- **Wire** · [`export let is_client_initiated = func(id: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L68)
  Whether the client opened the stream. Tagged `#NoDiscard`.
- **Wire** · [`export let is_valid_session_id = func(id: int64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L75)
  Whether the ID can be a session ID: a client-initiated bidirectional stream. Tagged `#NoDiscard`.
- **Wire** · [`export let nth_stream_id = func(kind: StreamType, index: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L79)
  The `index`-th stream ID (from 0) of a type: `4 * index` plus the type's low bits. Tagged `#NoDiscard`.
- **Wire** · [`export let append_varint = func(out: uint8[list], value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L94)
  Appends the shortest QUIC variable-length encoding of the value (RFC 9000 section 16). A value above 2^62 - 1 traps with "invalid argument: varint".
- **Wire** · [`export let encode_varint = func(value: uint64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L119)
  The shortest encoding of the value as a new list. Tagged `#NoDiscard`.
- **Wire** · [`export let WireReader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L127)
  Reads values from a byte list with an offset. Tagged `#Final`, `#NotThreadSafe`.
- **Wire.WireReader** · [`export let offset: int = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L131)
  The offset of the next byte.
- **Wire.WireReader** · [`export let init = func(data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L135)
  A reader at the start of the bytes.
- **Wire.WireReader** · [`export let remaining = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L141)
  The number of bytes not yet read. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_varint = func -> Result<uint64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L146)
  The next variable-length integer; any length is accepted, as RFC 9000 allows. Fails with UnexpectedEnd when the bytes run out. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_uint32 = func -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L165)
  The next 32-bit big-endian integer. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_bytes = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L179)
  The next `count` bytes, as a new list. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let read_rest = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L193)
  The rest of the bytes, as a new list. Tagged `#NoDiscard`.
- **Wire.WireReader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L203)
  The offset and length.
- **Wire** · [`export let uni_stream_header = func(session: int64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L210)
  The header of a unidirectional WebTransport stream for the session: 0x54 and the session ID. An invalid session ID traps with "invalid argument: session id". Tagged `#NoDiscard`.
- **Wire** · [`export let bidi_stream_header = func(session: int64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L219)
  The header of a bidirectional WebTransport stream for the session: 0x41 and the session ID. Tagged `#NoDiscard`.
- **Wire** · [`export let read_stream_header = func(reader: WireReader, bidirectional: bool) -> Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L230)
  Reads the header of a unidirectional stream (`bidirectional` false) or a bidirectional one and returns the session ID; the reader is left at the stream data. Fails for another type or signal and for a session ID that is not a client-initiated bidirectional stream ID. Tagged `#NoDiscard`.
- **Wire** · [`export let encode_datagram = func(session: int64, payload: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L246)
  An HTTP/3 datagram for the session: the quarter stream ID (session / 4) and the payload. Tagged `#NoDiscard`.
- **Wire** · [`export let Datagram = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L257)
  A received HTTP/3 datagram: its session and payload. Tagged `#Final`.
- **Wire.Datagram** · [`export last session: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L259)
  The session ID (4 times the quarter stream ID).
- **Wire.Datagram** · [`export last payload: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L261)
  The payload.
- **Wire.Datagram** · [`export let init = func(session_id: int64, data: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L264)
  Stores the fields.
- **Wire.Datagram** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L270)
  The session and payload length.
- **Wire** · [`export let decode_datagram = func(bytes: uint8[list]) -> Result<Datagram>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L276)
  Reads an HTTP/3 datagram. A quarter stream ID above 2^60 - 1 fails (RFC 9297 section 2.1). Tagged `#NoDiscard`.
- **Wire** · [`export let SessionClose = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L287)
  The reason a session ended, as a WT_CLOSE_SESSION capsule carries it. Tagged `#Final`, `#Immutable`.
- **Wire.SessionClose** · [`export last code: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L289)
  The 32-bit application error code.
- **Wire.SessionClose** · [`export last message: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L291)
  The UTF-8 message, at most 1024 bytes.
- **Wire.SessionClose** · [`export let init = func(error_code: uint32, text: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L294)
  Stores the code and message.
- **Wire.SessionClose** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L300)
  Equal when the code and message are.
- **Wire.SessionClose** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L309)
  Combines the code and message.
- **Wire.SessionClose** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L312)
  `code: message`.
- **Wire** · [`export let encode_close_capsule = func(close: SessionClose) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L318)
  The WT_CLOSE_SESSION capsule (type 0x2843, length, code, message). A message over 1024 bytes traps with "invalid argument: close message". Tagged `#NoDiscard`.
- **Wire** · [`export let decode_close_capsule = func(bytes: uint8[list]) -> Result<SessionClose>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L337)
  Reads a WT_CLOSE_SESSION capsule. Fails for another capsule type, a length below 4 or past the bytes, a message over 1024 bytes, or a message that is not UTF-8. Tagged `#NoDiscard`.
- **Wire** · [`export let to_http3_error = func(code: uint32) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L358)
  The HTTP/3 error code of a WebTransport application error code (draft section 4.4): `first + n + floor(n / 0x1e)`, skipping the reserved codepoints `0x1f * N + 0x21`. Tagged `#NoDiscard`.
- **Wire** · [`export let from_http3_error = func(code: uint64) -> Result<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/web-transport/wire.nra#L366)
  The WebTransport application error code of an HTTP/3 error code, or a failure when the code is outside the WT_APPLICATION_ERROR range or a reserved codepoint. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
