# `Std.Net.Mesh.ProxyProtocol`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Mesh.ProxyProtocol`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Mesh.ProxyProtocol`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ProxyProtocol** · [`export const TypeAlpn: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L37)
  `PP2_TYPE_ALPN` (0x01): the application protocol, as in the TLS ALPN extension.
- **ProxyProtocol** · [`export const TypeAuthority: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L39)
  `PP2_TYPE_AUTHORITY` (0x02): the host name the client asked for (TLS SNI), UTF-8.
- **ProxyProtocol** · [`export const TypeCrc32c: int = 3`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L41)
  `PP2_TYPE_CRC32C` (0x03): the CRC32C of the whole header.
- **ProxyProtocol** · [`export const TypeNoop: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L43)
  `PP2_TYPE_NOOP` (0x04): padding, ignored.
- **ProxyProtocol** · [`export const TypeUniqueId: int = 5`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L45)
  `PP2_TYPE_UNIQUE_ID` (0x05): an opaque connection id of at most 128 bytes.
- **ProxyProtocol** · [`export const TypeSsl: int = 32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L47)
  `PP2_TYPE_SSL` (0x20): TLS facts, with sub-TLVs.
- **ProxyProtocol** · [`export const SubtypeSslVersion: int = 33`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L49)
  `PP2_SUBTYPE_SSL_VERSION` (0x21): the TLS version text.
- **ProxyProtocol** · [`export const SubtypeSslCn: int = 34`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L51)
  `PP2_SUBTYPE_SSL_CN` (0x22): the client certificate's Common Name.
- **ProxyProtocol** · [`export const SubtypeSslCipher: int = 35`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L53)
  `PP2_SUBTYPE_SSL_CIPHER` (0x23): the cipher name.
- **ProxyProtocol** · [`export const SubtypeSslSigAlg: int = 36`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L55)
  `PP2_SUBTYPE_SSL_SIG_ALG` (0x24): the certificate signature algorithm.
- **ProxyProtocol** · [`export const SubtypeSslKeyAlg: int = 37`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L57)
  `PP2_SUBTYPE_SSL_KEY_ALG` (0x25): the certificate key algorithm.
- **ProxyProtocol** · [`export const TypeNetns: int = 48`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L59)
  `PP2_TYPE_NETNS` (0x30): the network namespace name.
- **ProxyProtocol** · [`export const ClientSsl: int = 1`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L61)
  `PP2_CLIENT_SSL`: the client connected over TLS.
- **ProxyProtocol** · [`export const ClientCertConn: int = 2`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L63)
  `PP2_CLIENT_CERT_CONN`: the client gave a certificate on this connection.
- **ProxyProtocol** · [`export const ClientCertSess: int = 4`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L65)
  `PP2_CLIENT_CERT_SESS`: the client gave a certificate in this TLS session.
- **ProxyProtocol** · [`export const MaxV1Length: int = 107`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L67)
  The longest version 1 line, CRLF included (section 2.1).
- **ProxyProtocol** · [`export const MaxUniqueIdLength: int = 128`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L69)
  The longest `PP2_TYPE_UNIQUE_ID` value (section 2.2.5).
- **ProxyProtocol** · [`export const V2FixedLength: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L71)
  The fixed part of a version 2 header: signature, two bytes and the length.
- **ProxyProtocol** · [`export let Command = enum (Local, Proxy)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L75)
  What the connection is: relayed for a client (PROXY) or opened by the proxy itself (LOCAL, health checks; version 2 only).
- **ProxyProtocol** · [`export let Transport = enum (Unspec, Stream, Dgram)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L78)
  The relayed transport: unspecified, a stream (TCP, Unix stream) or datagrams (UDP, Unix datagram).
- **ProxyProtocol** · [`export let Addresses = enum ( Unspecified, Inet { source: sockets.SocketAddr, destination: sockets.SocketAddr }, Unix { source: Optional<unixes.UnixAddr>, destination: Optional<unixes.UnixAddr> } )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L82)
  The original endpoints: none, an IP pair (both IPv4 or both IPv6), or a Unix pair where `None` is an unnamed socket.
- **ProxyProtocol** · [`export let Detection = enum (NeedMore, V1, V2, NotProxy)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L90)
  What the first bytes of a connection are: too few to tell, a version 1 or 2 header, or not a PROXY header at all (section 2.2: such a connection must be dropped).
- **ProxyProtocol** · [`export let Tlv = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L94)
  One version 2 Type-Length-Value vector. Tagged `#Final`.
- **ProxyProtocol.Tlv** · [`export last type_code: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L96)
  The type, 0 to 255.
- **ProxyProtocol.Tlv** · [`export last value: uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L98)
  The value bytes, at most 65535.
- **ProxyProtocol.Tlv** · [`export let init = func(code: int, bytes: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L102)
  A TLV. Traps with "invalid argument: type_code" outside 0 to 255 and "invalid argument: value" above 65535 bytes.
- **ProxyProtocol.Tlv** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L114)
  Equal when the type and every value byte are.
- **ProxyProtocol.Tlv** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L122)
  Combines the type and the value bytes.
- **ProxyProtocol.Tlv** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L125)
  Text such as "Tlv(1, 2 bytes)": the type in decimal and the value length.
- **ProxyProtocol** · [`export let SslInfo = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L131)
  The decoded value of a `PP2_TYPE_SSL` TLV (section 2.2.6). Tagged `#Final`.
- **ProxyProtocol.SslInfo** · [`export last client: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L133)
  The `PP2_CLIENT_*` bit field.
- **ProxyProtocol.SslInfo** · [`export last verify: uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L135)
  Zero when the client certificate was presented and verified.
- **ProxyProtocol.SslInfo** · [`export last subs: Tlv[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L137)
  The sub-TLVs in wire order.
- **ProxyProtocol.SslInfo** · [`export let init = func(client_bits: int, verify_result: uint32, sub_tlvs: Tlv[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L140)
  An SSL record. Traps with "invalid argument: client" outside 0 to 255.
- **ProxyProtocol.SslInfo** · [`export let is_tls = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L154)
  Whether the client connected over TLS (`PP2_CLIENT_SSL`).
- **ProxyProtocol.SslInfo** · [`export let cert_on_connection = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L157)
  Whether the client gave a certificate on this connection (`PP2_CLIENT_CERT_CONN`).
- **ProxyProtocol.SslInfo** · [`export let cert_in_session = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L160)
  Whether the client gave a certificate in this TLS session (`PP2_CLIENT_CERT_SESS`).
- **ProxyProtocol.SslInfo** · [`export let verified = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L163)
  Whether `verify` is zero.
- **ProxyProtocol.SslInfo** · [`export let text = func(code: int) -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L167)
  The text of the first sub-TLV of type `code`, if present and UTF-8. Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let version = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L178)
  The TLS version (`PP2_SUBTYPE_SSL_VERSION`). Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let common_name = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L182)
  The client certificate's Common Name (`PP2_SUBTYPE_SSL_CN`). Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let cipher = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L186)
  The cipher (`PP2_SUBTYPE_SSL_CIPHER`). Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let signature_algorithm = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L190)
  The certificate signature algorithm (`PP2_SUBTYPE_SSL_SIG_ALG`). Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let key_algorithm = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L194)
  The certificate key algorithm (`PP2_SUBTYPE_SSL_KEY_ALG`). Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let to_tlv = func -> Tlv`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L198)
  The TLV this record encodes to. Tagged `#NoDiscard`.
- **ProxyProtocol.SslInfo** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L209)
  Equal when the bits, the verify result and the sub-TLVs are.
- **ProxyProtocol.SslInfo** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L217)
  Combines the fields.
- **ProxyProtocol.SslInfo** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L221)
  Text such as "SslInfo(client 7, verify 0, 5 sub-TLVs)".
- **ProxyProtocol** · [`export let ProxyHeader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L227)
  A PROXY protocol header, parsed or to be sent. Tagged `#Final`.
- **ProxyProtocol.ProxyHeader** · [`export last version: int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L229)
  The protocol version it was read in or is meant for: 1 or 2.
- **ProxyProtocol.ProxyHeader** · [`export last command: Command`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L231)
  LOCAL or PROXY.
- **ProxyProtocol.ProxyHeader** · [`export last transport: Transport`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L233)
  The relayed transport.
- **ProxyProtocol.ProxyHeader** · [`export last addresses: Addresses`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L235)
  The original endpoints.
- **ProxyProtocol.ProxyHeader** · [`export last tlvs: Tlv[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L237)
  The version 2 TLVs in wire order (empty for version 1).
- **ProxyProtocol.ProxyHeader** · [`export let init = func(protocol_version: int, cmd: Command, proto: Transport, endpoints: Addresses, extensions: Tlv[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L240)
  A header. Traps with "invalid argument: version" unless the version is 1 or 2.
- **ProxyProtocol.ProxyHeader** · [`export let source = func -> Optional<sockets.SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L258)
  The original client address, when the header carries IP addresses. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let destination = func -> Optional<sockets.SocketAddr>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L266)
  The original destination address, when the header carries IP addresses. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let is_local = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L273)
  Whether the proxy opened the connection itself (LOCAL): use the real endpoints.
- **ProxyProtocol.ProxyHeader** · [`export let tlv = func(code: int) -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L277)
  The value of the first TLV of type `code`. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let alpn = func -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L288)
  The ALPN protocol bytes (`PP2_TYPE_ALPN`). Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let authority = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L292)
  The authority (`PP2_TYPE_AUTHORITY`), when present and UTF-8. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let unique_id = func -> Optional<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L296)
  The unique connection id (`PP2_TYPE_UNIQUE_ID`). Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let netns = func -> Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L300)
  The network namespace (`PP2_TYPE_NETNS`), when present and UTF-8. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let crc32c = func -> Optional<uint32>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L304)
  The checksum carried in a `PP2_TYPE_CRC32C` TLV (already verified by the decoder). Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let ssl = func -> Optional<SslInfo>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L312)
  The decoded `PP2_TYPE_SSL` TLV. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyHeader** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L325)
  Equal when every field and TLV are.
- **ProxyProtocol.ProxyHeader** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L334)
  Combines the fields.
- **ProxyProtocol.ProxyHeader** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L342)
  Text such as "ProxyHeader(v2 PROXY STREAM 192.168.0.1:56324 -> 192.168.0.11:443, 0 TLVs)".
- **ProxyProtocol** · [`export let tcp = func(version: int, source: sockets.SocketAddr, destination: sockets.SocketAddr) -> ProxyHeader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L355)
  A PROXY header for a TCP connection from `source` to `destination` in `version` 1 or 2. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let v2 = func(transport: Transport, addresses: Addresses, tlvs: Tlv[list]) -> ProxyHeader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L361)
  A version 2 PROXY header with every part given. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let local = func(tlvs: Tlv[list]) -> ProxyHeader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L366)
  A version 2 LOCAL header (a health check), with no addresses. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let unknown = func -> ProxyHeader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L371)
  A version 1 `PROXY UNKNOWN` header. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let text_tlv = func(code: int, text: string) -> Tlv`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L376)
  A TLV holding UTF-8 `text`. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let encode = func(header: ProxyHeader, with_crc: bool) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L380)
  The header in its own version's form; version 2 adds a CRC32C TLV when `with_crc` is true. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let encode_v1 = func(header: ProxyHeader) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L388)
  The version 1 line, CRLF included. Fails for LOCAL, for Unix or datagram addresses, for mixed families and for an IPv6 zone, none of which version 1 can say. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let encode_v2 = func(header: ProxyHeader, with_crc: bool) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L404)
  The version 2 bytes; with `with_crc` any CRC32C TLV is replaced by a computed one at the end. Fails for mixed IP families, for addresses with an UNSPEC transport, and above 65535 bytes after the fixed part. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let detect = func(bytes: uint8[list]) -> Detection`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L470)
  What `bytes` start with (section 2.2 "Identifying the protocol version"), decided as soon as the prefix cannot match either signature. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let parse = func(bytes: uint8[list]) -> Result<ProxyHeader>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L474)
  The header at the start of `bytes`, which must be complete; bytes after it are ignored. Tagged `#NoDiscard`.
- **ProxyProtocol** · [`export let ProxyDecoder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L489)
  Reads one PROXY header from a byte stream that arrives in chunks of any size. Push bytes, then call `next`: it returns the header, `None` while more bytes are needed, or the failure. `needed` says how many more bytes `next` needs at least, so a reader can avoid reading past the header. After the header, `remaining` holds the bytes that followed it. After a failure the decoder stays failed. Tagged `#Final`, `#NotThreadSafe`.
- **ProxyProtocol.ProxyDecoder** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L500)
  An empty decoder.
- **ProxyProtocol.ProxyDecoder** · [`export let push = func(chunk: uint8[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L504)
  Adds received bytes.
- **ProxyProtocol.ProxyDecoder** · [`export let buffered = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L509)
  The number of bytes received.
- **ProxyProtocol.ProxyDecoder** · [`export let next = func -> Result<Optional<ProxyHeader>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L513)
  The header, `None` while more bytes are needed, or the failure. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyDecoder** · [`export let needed = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L544)
  The fewest further bytes `next` needs to make progress: 8 to tell the version (both forms are at least 15 bytes long), then for version 1 one byte at a time up to the CRLF, and for version 2 the rest of the 16 fixed bytes and then the announced length. Reading exactly this many never reads past the header. Zero once a header or a failure is known. Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyDecoder** · [`export let remaining = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L568)
  The bytes received after the header (empty before the header is decoded). Tagged `#NoDiscard`.
- **ProxyProtocol.ProxyDecoder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/mesh/proxy-protocol.nra#L580)
  Text such as "ProxyDecoder(12 bytes buffered)".
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
