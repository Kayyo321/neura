# `Std.Net.Realtime.Ws.PermessageDeflate`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.PermessageDeflate`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.PermessageDeflate`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **PermessageDeflate** · [`export const ExtensionName: string = "permessage-deflate"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L34)
  The registered extension name.
- **PermessageDeflate** · [`export let DeflateParams = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L41)
  The four agreed or offered parameters. A window-bits field is 0 when the parameter is absent and 8 to 15 when it has a value; `client_max_window_bits` is -1 for the valueless form, which only an offer may use. Tagged `#Final`, `#Immutable`.
- **PermessageDeflate.DeflateParams** · [`export last server_no_context_takeover: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L43)
  The server does not reuse its LZ77 window between messages.
- **PermessageDeflate.DeflateParams** · [`export last client_no_context_takeover: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L45)
  The client does not reuse its LZ77 window between messages.
- **PermessageDeflate.DeflateParams** · [`export last server_max_window_bits: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L47)
  The server's window bits, or 0.
- **PermessageDeflate.DeflateParams** · [`export last client_max_window_bits: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L49)
  The client's window bits, 0, or -1 (offered without a value).
- **PermessageDeflate.DeflateParams** · [`export let init = func(server_no_takeover: bool, client_no_takeover: bool, server_bits: int, client_bits: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L53)
  Stores the parameters. Window bits outside 0 and 8 to 15 (and -1 for the client) trap with "invalid argument: window bits".
- **PermessageDeflate.DeflateParams** · [`export let to_extension = func -> exts.Extension`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L69)
  The element, as written in a Sec-WebSocket-Extensions header. Tagged `#NoDiscard`.
- **PermessageDeflate.DeflateParams** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L91)
  Equal when the four parameters are.
- **PermessageDeflate.DeflateParams** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L102)
  Combines the four parameters.
- **PermessageDeflate.DeflateParams** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L110)
  The header form.
- **PermessageDeflate** · [`export let ServerConfig = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L115)
  What a server is willing to agree to. Tagged `#Final`, `#Immutable`.
- **PermessageDeflate.ServerConfig** · [`export last accept_server_window_limit: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L117)
  Accept offers that limit the server's window (`server_max_window_bits`).
- **PermessageDeflate.ServerConfig** · [`export last server_no_context_takeover: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L119)
  Always answer `server_no_context_takeover`.
- **PermessageDeflate.ServerConfig** · [`export last client_no_context_takeover: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L121)
  Ask the client not to take over context (`client_no_context_takeover`).
- **PermessageDeflate.ServerConfig** · [`export last client_max_window_bits: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L123)
  The client window bits to request when the client supports the parameter, or 0.
- **PermessageDeflate.ServerConfig** · [`export let init = func(accept_limit: bool, server_no_takeover: bool, client_no_takeover: bool, client_bits: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L126)
  Stores the policy. Window bits outside 0 and 8 to 15 trap with "invalid argument: window bits".
- **PermessageDeflate.ServerConfig** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L138)
  Equal when the four settings are.
- **PermessageDeflate.ServerConfig** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L149)
  Combines the four settings, consistent with equals.
- **PermessageDeflate.ServerConfig** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L154)
  The four settings.
- **PermessageDeflate** · [`export let parse_offer = func(element: exts.Extension) -> Result<DeflateParams>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L165)
  Reads a client's offer element (RFC 7692 section 7). Fails for another extension name, an unknown or repeated parameter, a value on a takeover parameter, or window bits that are not 8 to 15 without leading zeros (`server_max_window_bits` needs a value; `client_max_window_bits` may have none). Tagged `#NoDiscard`.
- **PermessageDeflate** · [`export let parse_response = func(element: exts.Extension) -> Result<DeflateParams>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L170)
  Reads a server's response element (RFC 7692 section 7): as `parse_offer`, except that `client_max_window_bits` needs a value. Tagged `#NoDiscard`.
- **PermessageDeflate** · [`export let negotiate = func(offers: exts.Extension[list], config: ServerConfig) -> Optional<DeflateParams>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L181)
  The server's answer to a client's offers: the agreed parameters of the first acceptable permessage-deflate offer, or `None` to decline them all. An offer is skipped when it is invalid, or when it limits the server's window and the configuration does not accept that. The answer carries `server_no_context_takeover` when the offer or the configuration asks for it, the offer's `server_max_window_bits`, `client_no_context_takeover` when the configuration asks for it, and the configuration's `client_max_window_bits` (capped by the offer's value) only when the offer included that parameter (section 7.1.2.2). Tagged `#NoDiscard`.
- **PermessageDeflate** · [`export let validate_response = func(offers: exts.Extension[list], response: exts.Extension) -> Result<DeflateParams>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L214)
  The client's check of a server's response element against its offers (RFC 7692 sections 5 and 7.1): the response must parse, and some valid offer must allow it: `client_max_window_bits` only when that offer had the parameter, and `server_max_window_bits` no larger than that offer's value. Tagged `#NoDiscard`.
- **PermessageDeflate** · [`export const ClientOffer: string = "permessage-deflate; client_max_window_bits"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L240)
  The offer a client sends: permessage-deflate, able to limit its own window when the server asks.
- **PermessageDeflate** · [`export let strip_tail = func(compressed: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L245)
  The compressed payload with the final `00 00 ff ff` removed, as a sender sends it (RFC 7692 section 7.2.1). A payload without that tail is returned unchanged. Tagged `#NoDiscard`.
- **PermessageDeflate** · [`export let append_tail = func(payload: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L260)
  The received payload with `00 00 ff ff` appended, as a receiver inflates it (RFC 7692 section 7.2.2). Tagged `#NoDiscard`.
- **PermessageDeflate** · [`export let DeflateCodec = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L279)
  The per-connection compressor and decompressor for agreed parameters and a role. This end compresses with its own window bits (`client_max_window_bits` for a client, `server_max_window_bits` for a server; 15 when absent) and always decompresses with a 15-bit window (see the module description). A direction whose `*_no_context_takeover` was agreed starts each message with an empty window. Tagged `#Final`, `#NotThreadSafe`.
- **PermessageDeflate.DeflateCodec** · [`export last params: DeflateParams`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L281)
  The agreed parameters.
- **PermessageDeflate.DeflateCodec** · [`export last role: Role`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L283)
  The endpoint this codec serves.
- **PermessageDeflate.DeflateCodec** · [`export let init = func(agreed: DeflateParams, endpoint: Role)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L290)
  Stores the agreement and role and prepares both directions.
- **PermessageDeflate.DeflateCodec** · [`export let compress = func(message: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L307)
  The payload of a compressed message: DEFLATE of the message with a sync flush, tail stripped (section 7.2.1). Tagged `#NoDiscard`.
- **PermessageDeflate.DeflateCodec** · [`export let decompress = func(payload: uint8[list], max_size: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L318)
  The message of a compressed payload (section 7.2.2), at most `max_size` bytes. A larger message fails like an uncompressed one, with status 1009 and "message exceeds the limit of N bytes"; malformed data fails with status 1007. After a failure the codec starts over with empty windows. Tagged `#NoDiscard`.
- **PermessageDeflate.DeflateCodec** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/permessage-deflate.nra#L339)
  The agreement and role.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
