# `Std.Net.Tls.Handshake`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.Handshake`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/handshake.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.Handshake`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Handshake** · [`export let tls13 = func(config: TlsConfig, stream: StreamSocket<SocketAddr>) -> Result<Transport<uint8[list], uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/handshake.nra#L162)
  Completes a TLS 1.3 handshake over `stream` as the configured client or server. A client verifies the configured server name, or the peer's numeric address when the name is empty. Once the engine starts, this call owns the supplied stream, including closing it on handshake failure. Tagged `#NoDiscard`.
- **Handshake** · [`export let dtls13 = func(config: TlsConfig, socket: DatagramSocket<SocketAddr>, peer: SocketAddr) -> Result<Transport<uint8[list], uint8[list]>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/handshake.nra#L204)
  Runs a DTLS 1.3 handshake with `peer` over a datagram socket. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

`tls13` wraps a connected `StreamSocket` using the host's TLS 1.3 record engine. It verifies server identity for clients and uses the file capability for explicit credentials. Positive handshake timeouts also use the clock capability. Once the engine starts, the wrapper owns the stream. `dtls13` reports typed `Unsupported` because OpenSSL 3.5 supports DTLS through 1.2.

See [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) and the source comments for the complete module contract.
