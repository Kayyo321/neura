# `Std.Net.Http.H2`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.H2`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.H2`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **H2** · [`export const Preface: string = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L32)
  The client connection preface, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n" (section 3.4).
- **H2** · [`export const FrameHeaderSize: int = 9`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L35)
  The size of a frame header in octets (section 4.1).
- **H2** · [`export const FrameData: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L38)
  DATA, 0x00 (section 6.1).
- **H2** · [`export const FrameHeaders: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L40)
  HEADERS, 0x01 (section 6.2).
- **H2** · [`export const FramePriority: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L42)
  PRIORITY, 0x02 (section 6.3, deprecated).
- **H2** · [`export const FrameRstStream: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L44)
  RST_STREAM, 0x03 (section 6.4).
- **H2** · [`export const FrameSettings: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L46)
  SETTINGS, 0x04 (section 6.5).
- **H2** · [`export const FramePushPromise: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L48)
  PUSH_PROMISE, 0x05 (section 6.6).
- **H2** · [`export const FramePing: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L50)
  PING, 0x06 (section 6.7).
- **H2** · [`export const FrameGoaway: int = 7`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L52)
  GOAWAY, 0x07 (section 6.8).
- **H2** · [`export const FrameWindowUpdate: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L54)
  WINDOW_UPDATE, 0x08 (section 6.9).
- **H2** · [`export const FrameContinuation: int = 9`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L56)
  CONTINUATION, 0x09 (section 6.10).
- **H2** · [`export const FlagEndStream: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L59)
  Flag END_STREAM (DATA, HEADERS) and ACK (SETTINGS, PING), 0x01.
- **H2** · [`export const FlagAck: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L61)
  Flag ACK of SETTINGS and PING, 0x01 (sections 6.5, 6.7).
- **H2** · [`export const FlagEndHeaders: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L63)
  Flag END_HEADERS, 0x04 (sections 6.2, 6.6, 6.10).
- **H2** · [`export const FlagPadded: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L65)
  Flag PADDED, 0x08 (sections 6.1, 6.2, 6.6).
- **H2** · [`export const FlagPriority: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L67)
  Flag PRIORITY of HEADERS, 0x20 (section 6.2).
- **H2** · [`export const SettingHeaderTableSize: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L70)
  SETTINGS_HEADER_TABLE_SIZE, 0x01; initial value 4,096 (section 6.5.2).
- **H2** · [`export const SettingEnablePush: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L72)
  SETTINGS_ENABLE_PUSH, 0x02; initial value 1, only 0 or 1.
- **H2** · [`export const SettingMaxConcurrentStreams: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L74)
  SETTINGS_MAX_CONCURRENT_STREAMS, 0x03; initially unlimited.
- **H2** · [`export const SettingInitialWindowSize: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L76)
  SETTINGS_INITIAL_WINDOW_SIZE, 0x04; initial value 65,535, at most 2^31-1.
- **H2** · [`export const SettingMaxFrameSize: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L78)
  SETTINGS_MAX_FRAME_SIZE, 0x05; initial value 16,384, 16,384 through 16,777,215.
- **H2** · [`export const SettingMaxHeaderListSize: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L80)
  SETTINGS_MAX_HEADER_LIST_SIZE, 0x06; initially unlimited.
- **H2** · [`export const SettingEnableConnectProtocol: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L82)
  SETTINGS_ENABLE_CONNECT_PROTOCOL, 0x08; only 0 or 1 (RFC 8441 section 3).
- **H2** · [`export const InitialHeaderTableSize: int64 = 4096`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L85)
  The initial SETTINGS_HEADER_TABLE_SIZE.
- **H2** · [`export const InitialWindowSize: int64 = 65535`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L87)
  The initial SETTINGS_INITIAL_WINDOW_SIZE, 2^16-1.
- **H2** · [`export const MinMaxFrameSize: int64 = 16384`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L89)
  The initial and smallest SETTINGS_MAX_FRAME_SIZE, 2^14.
- **H2** · [`export const MaxFrameLength: int64 = 16777215`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L91)
  The largest SETTINGS_MAX_FRAME_SIZE and frame length, 2^24-1.
- **H2** · [`export const MaxWindowSize: int64 = 2147483647`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L93)
  The largest flow-control window, 2^31-1.
- **H2** · [`export const ErrorNoError: int = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L96)
  NO_ERROR, 0x00 (section 7).
- **H2** · [`export const ErrorProtocol: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L98)
  PROTOCOL_ERROR, 0x01.
- **H2** · [`export const ErrorInternal: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L100)
  INTERNAL_ERROR, 0x02.
- **H2** · [`export const ErrorFlowControl: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L102)
  FLOW_CONTROL_ERROR, 0x03.
- **H2** · [`export const ErrorSettingsTimeout: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L104)
  SETTINGS_TIMEOUT, 0x04.
- **H2** · [`export const ErrorStreamClosed: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L106)
  STREAM_CLOSED, 0x05.
- **H2** · [`export const ErrorFrameSize: int = 6`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L108)
  FRAME_SIZE_ERROR, 0x06.
- **H2** · [`export const ErrorRefusedStream: int = 7`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L110)
  REFUSED_STREAM, 0x07.
- **H2** · [`export const ErrorCancel: int = 8`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L112)
  CANCEL, 0x08.
- **H2** · [`export const ErrorCompression: int = 9`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L114)
  COMPRESSION_ERROR, 0x09.
- **H2** · [`export const ErrorConnect: int = 10`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L116)
  CONNECT_ERROR, 0x0a.
- **H2** · [`export const ErrorEnhanceYourCalm: int = 11`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L118)
  ENHANCE_YOUR_CALM, 0x0b.
- **H2** · [`export const ErrorInadequateSecurity: int = 12`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L120)
  INADEQUATE_SECURITY, 0x0c.
- **H2** · [`export const ErrorHttp11Required: int = 13`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L122)
  HTTP_1_1_REQUIRED, 0x0d.
- **H2** · [`export let FrameHeader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L126)
  A frame header (section 4.1). Tagged `#Final`, `#Immutable`, `#ThreadSafe`.
- **H2.FrameHeader** · [`export last length: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L128)
  The payload length, 0 through 2^24-1.
- **H2.FrameHeader** · [`export last frame_type: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L130)
  The frame type, 0 through 255.
- **H2.FrameHeader** · [`export last flags: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L132)
  The flags, 0 through 255.
- **H2.FrameHeader** · [`export last stream_id: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L134)
  The stream identifier, 0 through 2^31-1.
- **H2.FrameHeader** · [`export let init = func(size: int, kind: int, flag_bits: int, stream: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L137)
  Stores the fields; `encode_frame_header` checks their ranges.
- **H2.FrameHeader** · [`export let has_flag = func(flag: int) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L145)
  Whether every bit of `flag` is set.
- **H2.FrameHeader** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L148)
  Equal fields.
- **H2.FrameHeader** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L157)
  Combines the fields.
- **H2.FrameHeader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L160)
  The fields by name.
- **H2** · [`export let Settings = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L167)
  The identifiers and values of a SETTINGS frame, in order. Tagged `#Final`, `#NotThreadSafe`.
- **H2.Settings** · [`export last ids: int[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L169)
  The identifiers, 0 through 65,535.
- **H2.Settings** · [`export last values: int64[list] = new`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L171)
  The values, 0 through 2^32-1, parallel to `ids`.
- **H2.Settings** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L174)
  An empty list.
- **H2.Settings** · [`export let add = func(id: int, value: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L179)
  Appends a setting. Traps with "invalid argument: setting" when the identifier or value does not fit 16 or 32 bits.
- **H2.Settings** · [`export let get_or = func(id: int, fallback: int64) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L188)
  The last value of `id` (later entries override earlier ones, section 6.5.3), or `fallback`.
- **H2.Settings** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L199)
  Equal settings in the same order.
- **H2.Settings** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L207)
  The hash of the text form.
- **H2.Settings** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L210)
  "Settings{id=value, ...}".
- **H2** · [`export let preface_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L224)
  The preface as bytes. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **H2** · [`export let encode_frame_header = func(header: FrameHeader) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L235)
  The 9 octets of `header`, the reserved bit unset. Traps with "invalid argument: frame header" when a field is out of range. Tagged `#NoDiscard`.
- **H2** · [`export let decode_frame_header = func(bytes: uint8[list], offset: int) -> Result<FrameHeader>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L258)
  The frame header at `offset`; the reserved bit is ignored. Err (UnexpectedEnd) when fewer than 9 bytes remain. Tagged `#NoDiscard`.
- **H2** · [`export let encode_settings = func(settings: Settings) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L272)
  The SETTINGS payload holding `settings` in order (section 6.5.1). Tagged `#NoDiscard`.
- **H2** · [`export let decode_settings = func(payload: uint8[list]) -> Result<Settings>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L292)
  The settings of a SETTINGS payload. Errors are `error.custom` with the HTTP/2 error name as the code: FRAME_SIZE_ERROR for a length that is not a multiple of 6 (section 6.5); PROTOCOL_ERROR for ENABLE_PUSH or ENABLE_CONNECT_PROTOCOL other than 0 or 1, or MAX_FRAME_SIZE outside 16,384 through 16,777,215; FLOW_CONTROL_ERROR for INITIAL_WINDOW_SIZE above 2^31-1 (section 6.5.2). Tagged `#NoDiscard`.
- **H2** · [`export let H2Connection = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L337)
  A low-level HTTP/2 frame sender over a verified TLS connection. It does not read peer frames, acknowledge their settings, enforce stream lifecycle, or multiplex requests; `Http.Fetch` owns that driver. Tagged `#Final`, `#NotThreadSafe`.
- **H2.H2Connection** · [`export last authority: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L339)
  The authority the connection was opened to.
- **H2.H2Connection** · [`export let init = func(seal: Seal, target: string, stream: int64, timeout: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L345)
  Created only by `connect` or `connect_with`.
- **H2.H2Connection** · [`export let send_frame = func(header: FrameHeader, payload: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L356)
  Writes one complete frame within the connection's positive per-operation deadline. A write waiting on TLS lets other tasks run. Length/payload disagreements and fixed-size frame violations return FRAME_SIZE_ERROR; invalid stream identifiers return PROTOCOL_ERROR. Frames remain limited to 16,384 payload bytes. Tagged `#NoDiscard`. Since 0.3.1.
- **H2.H2Connection** · [`export let close = func(error_code: int) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L377)
  Sends GOAWAY with last-stream-id zero, sends close_notify and discards inbound data until peer close_notify, within one deadline. TLS waits let other tasks run. Releases TLS on success or failure. Closing again succeeds. A negative `error_code` returns InvalidInput without closing. Tagged `#NoDiscard`. Since 0.3.1.
- **H2.H2Connection** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L409)
  The authority.
- **H2** · [`export let connect = func(authority: string, settings: Settings) -> Result<H2Connection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L479)
  Opens verified TLS with ALPN h2 and system trust, then writes the preface and SETTINGS. The complete connect operation has a thirty-second deadline. Network waits park the calling task while other tasks run. Needs net and clock capabilities. Tagged `#NoDiscard`. Since 0.3.1.
- **H2** · [`export let connect_with = func(authority: string, settings: Settings, config: tls_config.TlsConfig) -> Result<H2Connection>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L490)
  As `connect`, with explicit trust/credentials and a positive total handshake/preface deadline. `config` must be a client configuration offering exactly h2. Later sends use the same duration as a fresh per-operation deadline. Explicit trust/credential files also require the fs capability. Tagged `#NoDiscard`. Since 0.3.1.
- **H2** · [`export let hpack_encode = func(fields: header_map.Headers) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L565)
  Compresses regular header fields as an independent HPACK block. Names are lowercased. Unlike the former stub, encoding returns a Result. See `hpack_encode_fields` for limits and state. Tagged `#NoDiscard`. Since 0.3.1.
- **H2** · [`export let hpack_decode = func(block: uint8[list]) -> Result<header_map.Headers>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L576)
  Decodes regular fields into Headers. A pseudo-header returns PROTOCOL_ERROR rather than reaching Headers' token-name trap; use `hpack_decode_fields` for complete HTTP/2 field sections. Tagged `#NoDiscard`. Since 0.3.1.
- **H2** · [`export let hpack_encode_fields = func(fields: [string, string][list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L594)
  Compresses lowercase name/value pairs, including pseudo-header names. Each call uses a new encoder with dynamic table size zero and no indexing. At most 256 fields and 65,536 total name/value bytes or encoded bytes; a conservative encoder allocation bound also applies. Invalid field text is PROTOCOL_ERROR, limits are hpack.limit, and an HTTP/2-disabled provider returns typed Unsupported. Tagged `#NoDiscard`. Since 0.3.1.
- **H2** · [`export let hpack_decode_fields = func(block: uint8[list]) -> Result<[string, string][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/h2.nra#L613)
  Decodes a complete independent block with an initial 4,096-byte dynamic table, including RFC 7541 Huffman strings and table references created within this block. No table state survives a call. Bounds are those of `hpack_encode_fields`; malformed text/encoding is COMPRESSION_ERROR. Tagged `#NoDiscard`. Since 0.3.1.
<!-- END SOURCE API -->

## Detailed contracts

Connections use verified TLS with ALPN `h2` and expose raw frame writes. HPACK is stateless between calls and requires a build with nghttp2. [Native host services](NVM#http-client-services)
