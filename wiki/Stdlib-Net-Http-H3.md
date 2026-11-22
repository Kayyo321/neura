# `Std.Net.Http.H3`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.H3`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.H3`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **H3** · [`export const MaxVarint: uint64 = 4611686018427387903`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L21)
  The largest QUIC variable-length integer, 2^62 - 1 (RFC 9000 section 16).
- **H3** · [`export const FrameData: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L24)
  Frame type DATA, 0x00 (RFC 9114 section 7.2.1).
- **H3** · [`export const FrameHeaders: uint64 = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L26)
  Frame type HEADERS, 0x01 (section 7.2.2).
- **H3** · [`export const FrameCancelPush: uint64 = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L28)
  Frame type CANCEL_PUSH, 0x03 (section 7.2.3).
- **H3** · [`export const FrameSettings: uint64 = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L30)
  Frame type SETTINGS, 0x04 (section 7.2.4).
- **H3** · [`export const FramePushPromise: uint64 = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L32)
  Frame type PUSH_PROMISE, 0x05 (section 7.2.5).
- **H3** · [`export const FrameGoaway: uint64 = 7`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L34)
  Frame type GOAWAY, 0x07 (section 7.2.6).
- **H3** · [`export const FrameMaxPushId: uint64 = 13`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L36)
  Frame type MAX_PUSH_ID, 0x0d (section 7.2.7).
- **H3** · [`export const StreamControl: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L39)
  Unidirectional stream type of the control stream, 0x00 (section 6.2.1).
- **H3** · [`export const StreamPush: uint64 = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L41)
  Unidirectional stream type of a push stream, 0x01 (section 6.2.2).
- **H3** · [`export const SettingMaxFieldSectionSize: uint64 = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L44)
  SETTINGS_MAX_FIELD_SECTION_SIZE, 0x06 (section 7.2.4.1).
- **H3** · [`export const SettingEnableConnectProtocol: uint64 = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L46)
  SETTINGS_ENABLE_CONNECT_PROTOCOL, 0x08 (RFC 9220 section 3).
- **H3** · [`export const SettingH3Datagram: uint64 = 51`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L48)
  SETTINGS_H3_DATAGRAM, 0x33 (RFC 9297 section 2.1.1).
- **H3** · [`export const ErrorNoError: uint64 = 256`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L51)
  H3_NO_ERROR, 0x0100 (section 8.1).
- **H3** · [`export const ErrorGeneralProtocol: uint64 = 257`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L53)
  H3_GENERAL_PROTOCOL_ERROR, 0x0101.
- **H3** · [`export const ErrorInternal: uint64 = 258`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L55)
  H3_INTERNAL_ERROR, 0x0102.
- **H3** · [`export const ErrorStreamCreation: uint64 = 259`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L57)
  H3_STREAM_CREATION_ERROR, 0x0103.
- **H3** · [`export const ErrorClosedCriticalStream: uint64 = 260`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L59)
  H3_CLOSED_CRITICAL_STREAM, 0x0104.
- **H3** · [`export const ErrorFrameUnexpected: uint64 = 261`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L61)
  H3_FRAME_UNEXPECTED, 0x0105.
- **H3** · [`export const ErrorFrame: uint64 = 262`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L63)
  H3_FRAME_ERROR, 0x0106.
- **H3** · [`export const ErrorExcessiveLoad: uint64 = 263`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L65)
  H3_EXCESSIVE_LOAD, 0x0107.
- **H3** · [`export const ErrorId: uint64 = 264`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L67)
  H3_ID_ERROR, 0x0108.
- **H3** · [`export const ErrorSettings: uint64 = 265`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L69)
  H3_SETTINGS_ERROR, 0x0109.
- **H3** · [`export const ErrorMissingSettings: uint64 = 266`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L71)
  H3_MISSING_SETTINGS, 0x010a.
- **H3** · [`export const ErrorRequestRejected: uint64 = 267`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L73)
  H3_REQUEST_REJECTED, 0x010b.
- **H3** · [`export const ErrorRequestCancelled: uint64 = 268`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L75)
  H3_REQUEST_CANCELLED, 0x010c.
- **H3** · [`export const ErrorRequestIncomplete: uint64 = 269`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L77)
  H3_REQUEST_INCOMPLETE, 0x010d.
- **H3** · [`export const ErrorMessage: uint64 = 270`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L79)
  H3_MESSAGE_ERROR, 0x010e.
- **H3** · [`export const ErrorConnect: uint64 = 271`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L81)
  H3_CONNECT_ERROR, 0x010f.
- **H3** · [`export const ErrorVersionFallback: uint64 = 272`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L83)
  H3_VERSION_FALLBACK, 0x0110.
- **H3** · [`export let Decoded = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L87)
  A decoded value and the offset just after it. Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **H3.Decoded** · [`export last value: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L89)
  The value.
- **H3.Decoded** · [`export last next: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L91)
  The offset of the first byte after the value.
- **H3.Decoded** · [`export let init = func(decoded: uint64, after: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L94)
  Stores the value and the offset.
- **H3.Decoded** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L100)
  Equal values and offsets.
- **H3.Decoded** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L108)
  Combines the value and the offset.
- **H3.Decoded** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L113)
  "value@next".
- **H3** · [`export let FrameHeader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L118)
  A frame header: the type and the payload length, and the offset of the payload. Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **H3.FrameHeader** · [`export last frame_type: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L120)
  The frame type.
- **H3.FrameHeader** · [`export last length: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L122)
  The payload length in bytes.
- **H3.FrameHeader** · [`export last payload_offset: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L124)
  The offset of the first payload byte.
- **H3.FrameHeader** · [`export let init = func(kind: uint64, size: uint64, offset: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L127)
  Stores the fields.
- **H3.FrameHeader** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L134)
  Equal fields.
- **H3.FrameHeader** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L142)
  Combines the fields.
- **H3.FrameHeader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L145)
  The fields by name.
- **H3** · [`export let Settings = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L152)
  The identifiers and values of a SETTINGS frame, in order. Tagged `#Final`, `#NotThreadSafe`.
- **H3.Settings** · [`export last ids: uint64[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L154)
  The identifiers.
- **H3.Settings** · [`export last values: uint64[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L156)
  The values, parallel to `ids`.
- **H3.Settings** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L159)
  An empty list.
- **H3.Settings** · [`export let add = func(id: uint64, value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L163)
  Appends a setting.
- **H3.Settings** · [`export let get_or = func(id: uint64, fallback: uint64) -> uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L169)
  The value of `id`, or `fallback` when absent.
- **H3.Settings** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L179)
  Equal settings in the same order.
- **H3.Settings** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L187)
  The hash of the text form.
- **H3.Settings** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L190)
  "Settings{id=value, ...}".
- **H3** · [`export let encode_varint = func(value: uint64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L205)
  The shortest encoding of `value` (RFC 9000 section 16): 1, 2, 4 or 8 bytes with the length in the two high bits. Traps with "invalid argument: varint out of range" above 2^62 - 1. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **H3** · [`export let append_varint = func(out: uint8[list], value: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L212)
  Appends the shortest encoding of `value` to `out`; traps as `encode_varint` does.
- **H3** · [`export let decode_varint = func(bytes: uint8[list], offset: int) -> Result<Decoded>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L238)
  The variable-length integer at `offset` (RFC 9000 section 16, Appendix A.1). Err (UnexpectedEnd) when the bytes end inside it. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **H3** · [`export let encode_frame_header = func(frame_type: uint64, length: uint64) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L256)
  The frame header Type (i), Length (i) (RFC 9114 section 7.1). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **H3** · [`export let decode_frame_header = func(bytes: uint8[list], offset: int) -> Result<FrameHeader>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L266)
  The frame header at `offset`; errors are those of `decode_varint`. The payload may extend past the end of `bytes`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **H3** · [`export let encode_settings = func(settings: Settings) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L274)
  The SETTINGS frame payload holding `settings` in order (section 7.2.4). Tagged `#NoDiscard`.
- **H3** · [`export let decode_settings = func(payload: uint8[list]) -> Result<Settings>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L287)
  The settings of a SETTINGS frame payload (section 7.2.4). Err for a truncated pair (UnexpectedEnd), a repeated identifier, or an HTTP/2 identifier reserved by section 11.2.2 (0x00, 0x02 through 0x05); the last two are H3_SETTINGS_ERROR conditions (Syntax, at the identifier). Unknown identifiers are kept. Tagged `#NoDiscard`.
- **H3** · [`export let is_reserved_setting = func(id: uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L309)
  Whether `id` is an HTTP/2 setting identifier reserved in HTTP/3 (section 11.2.2). Tagged `#Pure`, `#ThreadSafe`.
- **H3** · [`export let is_grease = func(value: uint64) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L314)
  Whether `value` has the form 0x1f * N + 0x21 used for reserved ("grease") types, settings and error codes (sections 6.2.3, 7.2.8, 7.2.4.1, 8.1). Tagged `#Pure`, `#ThreadSafe`.
- **H3** · [`export let H3Connection = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L318)
  An HTTP/3 connection descriptor whose transport operations currently return `Io(Unsupported)`. Tagged `#Final`, `#NotThreadSafe`.
- **H3.H3Connection** · [`export last authority: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L320)
  The authority recorded by the descriptor.
- **H3.H3Connection** · [`export let init = func(target: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L323)
  Records an authority only; this value owns no native connection.
- **H3.H3Connection** · [`export let send_frame = func(stream_id: uint64, frame_type: uint64, payload: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L329)
  Returns `Io(Unsupported)` because general HTTP/3 QUIC stream I/O is unavailable. Tagged `#NoDiscard`. Since 0.3.1.
- **H3.H3Connection** · [`export let close = func(error_code: uint64) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L334)
  Returns `Io(Unsupported)` because this value owns no native QUIC connection to close. Tagged `#NoDiscard`. Since 0.3.1.
- **H3.H3Connection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L338)
  The authority.
- **H3** · [`export let connect = func(authority: string, settings: Settings) -> Result<H3Connection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L345)
  Returns `Io(Unsupported)` because HTTP/3 connection establishment, control streams and QPACK state are unavailable. The DNS-over-QUIC provider is not a general HTTP/3 transport. No connection is opened and no request leaves the process. Tagged `#NoDiscard`. Since 0.3.1.
- **H3** · [`export let encode_field_section = func(fields: header_map.Headers) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L352)
  Returns `Io(Unsupported)` because QPACK field compression is unavailable. Since 0.3.1 this returns `Result<uint8[list]>` so callers can handle unavailable compression without trapping; the former unwrapped byte-list result never produced a value. Tagged `#NoDiscard`. Since 0.3.1.
- **H3** · [`export let decode_field_section = func(encoded: uint8[list]) -> Result<header_map.Headers>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h3.nra#L357)
  Returns `Io(Unsupported)` because QPACK field decompression is unavailable. Tagged `#NoDiscard`. Since 0.3.1.
<!-- END SOURCE API -->

## Detailed contracts

Frame and settings codecs are available. HTTP/3 connections and QPACK return typed `Unsupported` because the QUIC provider only supplies DNS exchanges. [Networking status](Status)
