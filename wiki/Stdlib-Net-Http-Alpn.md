# `Std.Net.Http.Alpn`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Alpn`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Alpn`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Alpn** · [`export const ExtensionType: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L17)
  The TLS extension type of ALPN, application_layer_protocol_negotiation(16) (section 3.1).
- **Alpn** · [`export const NoApplicationProtocolAlert: int = 120`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L20)
  The alert description no_application_protocol(120) (section 3.2).
- **Alpn** · [`export const MaxNameLength: int = 255`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L23)
  The longest protocol name in bytes (section 3.1).
- **Alpn** · [`export const Http11Id: string = "http/1.1"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L26)
  The identifier of HTTP/1.1 as text (RFC 7301 section 6).
- **Alpn** · [`export const H2Id: string = "h2"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L29)
  The identifier of HTTP/2 over TLS as text (RFC 9113 section 3).
- **Alpn** · [`export const H3Id: string = "h3"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L32)
  The identifier of HTTP/3 as text (RFC 9114 section 11.1).
- **Alpn** · [`export let Selection = enum (Selected(uint8[list]), NoApplicationProtocol)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L35)
  The outcome of server-side selection (section 3.2).
- **Alpn** · [`export let http11 = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L39)
  The bytes of "http/1.1": 0x68 0x74 0x74 0x70 0x2f 0x31 0x2e 0x31 (RFC 7301 section 6). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let h2 = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L43)
  The bytes of "h2": 0x68 0x32 (RFC 9113 section 3). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let h3 = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L47)
  The bytes of "h3": 0x68 0x33 (RFC 9114 section 11.1). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let bytes_of = func(name: string) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L51)
  The bytes of a text protocol name (its UTF-8 encoding). Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let encode = func(names: uint8[list][list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L62)
  The ProtocolNameList holding `names` in order (section 3.1). Err (OutOfRange) for no names, an empty name or one over 255 bytes (at the index of the name), or a list over 65,535 bytes. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let decode = func(wire: uint8[list]) -> Result<uint8[list][list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L93)
  The names of a ProtocolNameList (section 3.1). Errors at the offending byte offset: UnexpectedEnd for fewer than two bytes or a name cut short, Syntax when the length prefix disagrees with the size, Empty for an empty list, OutOfRange for a zero-length name. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let decode_server_selection = func(wire: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L129)
  The protocol of a ServerHello ALPN extension: a ProtocolNameList with exactly one name (section 3.1). Errors are those of `decode`, and Syntax when the list holds more than one name. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let select = func(server_preference: uint8[list][list], client_offer: uint8[list][list]) -> Selection`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L141)
  Server-side selection (section 3.2): the first of `server_preference` that `client_offer` contains, or `NoApplicationProtocol`. Tagged `#NoDiscard`, `#Pure`, `#ThreadSafe`.
- **Alpn** · [`export let same = func(a: uint8[list], b: uint8[list]) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/alpn.nra#L154)
  Whether two names hold the same bytes. Tagged `#Pure`, `#ThreadSafe`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
