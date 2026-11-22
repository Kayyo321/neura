# `Std.Net.Tls.TlsAcceptor`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.TlsAcceptor`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.TlsAcceptor`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TlsAcceptor** · [`export let TlsAcceptor = class implements Listener<SocketAddr, TlsStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L29)
  A listening socket that accepts TLS streams. Tagged `#Final`, `#NoDiscard(reason: "an acceptor must be closed")`, `#NotThreadSafe`.
- **TlsAcceptor.TlsAcceptor** · [`export last config: TlsConfig`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L35)
  The server configuration.
- **TlsAcceptor.TlsAcceptor** · [`export let init = func(opened: socket.Handle, address: SocketAddr, server: TlsConfig)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L40)
  Use `bind`; the handle comes from the library.
- **TlsAcceptor.TlsAcceptor** · [`export let address = func -> SocketAddr`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L47)
  The bound address; its port is the one the host chose for a bind to port 0.
- **TlsAcceptor.TlsAcceptor** · [`export let accept = func -> Result<TlsStream>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L52)
  Accepts one connection and completes the TLS handshake (`neura.net.tls.accept`); fails with Unsupported in a build without TLS, and with Closed after close. Tagged `#NoDiscard`.
- **TlsAcceptor.TlsAcceptor** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L70)
  Releases the listener. Closing again returns Ok.
- **TlsAcceptor.TlsAcceptor** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L81)
  Text such as "TlsAcceptor(127.0.0.1:8443)".
- **TlsAcceptor** · [`export let bind = func(address: SocketAddr, server: TlsConfig) -> Result<TlsAcceptor>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-acceptor.nra#L88)
  Listens on `address` (port 0 asks for a free port) for TLS connections with a server configuration; a client configuration traps with "invalid argument: a TLS acceptor needs a server configuration". Works today; accepting does not. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

An OpenSSL-enabled build accepts TLS 1.2 or 1.3 with the configured range. A client CA file requires mutual TLS. The network and file capabilities must permit the listener and credential access.

See [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) and the source comments for the complete module contract.
