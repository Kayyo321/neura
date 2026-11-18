# `Std.Net.Tls.TlsConfig`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Tls.TlsConfig`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Tls.TlsConfig`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **TlsConfig** · [`export let TlsVersion = enum (Tls12, Tls13)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L33)
  A TLS protocol version.
- **TlsConfig** · [`export let TlsRole = enum (Client, Server)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L36)
  Whether a configuration is for the connecting or the accepting side.
- **TlsConfig** · [`export let TlsConfig = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L40)
  A validated TLS configuration. Tagged `#Final`.
- **TlsConfig.TlsConfig** · [`export last role: TlsRole`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L42)
  Client or server.
- **TlsConfig.TlsConfig** · [`export last min_version: TlsVersion`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L44)
  The lowest version accepted.
- **TlsConfig.TlsConfig** · [`export last max_version: TlsVersion`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L46)
  The highest version offered.
- **TlsConfig.TlsConfig** · [`export last server_name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L50)
  The SNI and verification name of a client; empty to use the dialed host.
- **TlsConfig.TlsConfig** · [`export last ca_file: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L52)
  The CA bundle (PEM) a client verifies the server with; empty for the system store.
- **TlsConfig.TlsConfig** · [`export last certificate_file: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L54)
  The PEM certificate chain this side presents; empty when none.
- **TlsConfig.TlsConfig** · [`export last key_file: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L56)
  The PEM private key of the certificate; empty when none.
- **TlsConfig.TlsConfig** · [`export last client_ca_file: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L58)
  A server's CA bundle for client certificates; non-empty requires mutual TLS.
- **TlsConfig.TlsConfig** · [`export last handshake_timeout: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L60)
  The longest a handshake may take; zero waits without limit.
- **TlsConfig.TlsConfig** · [`export let init = func(for_role: TlsRole, lowest: TlsVersion, highest: TlsVersion, alpn: string[list], name: string, ca: string, certificate: string, key: string, client_ca: string, timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L63)
  Use `TlsConfigBuilder`.
- **TlsConfig.TlsConfig** · [`export let alpn = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L79)
  The ALPN protocols, most preferred first. A copy. Tagged `#NoDiscard`.
- **TlsConfig.TlsConfig** · [`export let is_mutual = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L89)
  True when this side presents a certificate as a client (mutual TLS) or requires one as a server. Tagged `#Pure`.
- **TlsConfig.TlsConfig** · [`export let allows_both_versions = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L94)
  True when both TLS 1.2 and 1.3 are allowed, the range the host's basic TLS service negotiates. Tagged `#Pure`.
- **TlsConfig.TlsConfig** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L98)
  Equal when every setting is.
- **TlsConfig.TlsConfig** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L111)
  A hash consistent with equals.
- **TlsConfig.TlsConfig** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L126)
  Text such as "TlsConfig(Client, Tls12..Tls13, alpn h2,http/1.1, sni example.com)". File paths are named; their contents are never read here.
- **TlsConfig** · [`export let TlsConfigBuilder = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L145)
  Collects settings; `build_client` or `build_server` validates them. Setting a value again replaces it. Tagged `#Final`, `#NotThreadSafe`.
- **TlsConfig.TlsConfigBuilder** · [`export let set_versions = func(lowest_version: TlsVersion, highest_version: TlsVersion)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L166)
  The versions accepted, from `lowest` through `highest` (default TLS 1.2 through 1.3).
- **TlsConfig.TlsConfigBuilder** · [`export let set_alpn = func(names: string[list])`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L172)
  The ALPN protocols, most preferred first (for example "h2", "http/1.1").
- **TlsConfig.TlsConfigBuilder** · [`export let set_server_name = func(host_name: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L181)
  The SNI name a client sends and verifies the certificate against; empty uses the host.
- **TlsConfig.TlsConfigBuilder** · [`export let set_ca_file = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L186)
  The PEM CA bundle a client trusts instead of the system store.
- **TlsConfig.TlsConfigBuilder** · [`export let set_certificate = func(certificate_path: string, key_path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L192)
  The PEM certificate chain and private key this side presents (a server's identity, or a client's for mutual TLS).
- **TlsConfig.TlsConfigBuilder** · [`export let set_client_ca_file = func(path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L198)
  A server's PEM CA bundle for client certificates; setting it requires mutual TLS.
- **TlsConfig.TlsConfigBuilder** · [`export let set_handshake_timeout = func(limit: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L203)
  The longest a handshake may take; zero waits without limit.
- **TlsConfig.TlsConfigBuilder** · [`export let build_client = func -> Result<TlsConfig>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L209)
  A validated client configuration, or a `tls.config` error. Tagged `#NoDiscard`.
- **TlsConfig.TlsConfigBuilder** · [`export let build_server = func -> Result<TlsConfig>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L223)
  A validated server configuration, or a `tls.config` error. Tagged `#NoDiscard`.
- **TlsConfig.TlsConfigBuilder** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L236)
  Text such as "TlsConfigBuilder".
- **TlsConfig** · [`export let builder = func -> TlsConfigBuilder`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L256)
  An empty builder: TLS 1.2 through 1.3, no ALPN, the system trust store, no certificate. Tagged `#NoDiscard`.
- **TlsConfig** · [`export let check_alpn = func(names: string[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L260)
  Ok when `names` is a valid ALPN list (RFC 7301 section 3.1), else a `tls.config` error. Tagged `#NoDiscard`.
- **TlsConfig** · [`export let check_server_name = func(name: string) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/tls/tls-config.nra#L277)
  Ok when `name` is a valid SNI host name (RFC 6066 section 3, see the module documentation), else a `tls.config` error. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
