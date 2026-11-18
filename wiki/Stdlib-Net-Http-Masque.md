# `Std.Net.Http.Masque`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Http.Masque`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Http.Masque`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Masque** · [`export const ConnectUdp: string = "connect-udp"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L28)
  The upgrade token of UDP proxying (RFC 9298 section 3).
- **Masque** · [`export const ConnectIp: string = "connect-ip"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L31)
  The upgrade token of IP proxying (RFC 9484 section 4).
- **Masque** · [`export const Wildcard: string = "*"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L34)
  The wildcard value of the CONNECT-IP template variables (RFC 9484 section 4.6).
- **Masque** · [`export const UdpPayloadContext: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L37)
  The Context ID of UDP payloads (RFC 9298 section 4).
- **Masque** · [`export let ConnectRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L41)
  An extended CONNECT request: the pseudo-header fields and the regular fields. Tagged `#Final`, `#NotThreadSafe`.
- **Masque.ConnectRequest** · [`export last method: string = "CONNECT"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L43)
  `:method`, always CONNECT.
- **Masque.ConnectRequest** · [`export last protocol: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L45)
  `:protocol`, the upgrade token.
- **Masque.ConnectRequest** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L47)
  `:scheme`.
- **Masque.ConnectRequest** · [`export last authority: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L49)
  `:authority`, the proxy's authority.
- **Masque.ConnectRequest** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L51)
  `:path`, the expanded template path.
- **Masque.ConnectRequest** · [`export last fields: header_map.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L53)
  The regular header fields (`capsule-protocol: ?1`).
- **Masque.ConnectRequest** · [`export let init = func(token: string, request_scheme: string, proxy_authority: string, request_path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L56)
  Stores the pseudo-header values and adds `capsule-protocol: ?1`.
- **Masque.ConnectRequest** · [`export let pseudo_names = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L68)
  The pseudo-header names in the order of the RFC examples: :method, :protocol, :scheme, :path, :authority. Tagged `#NoDiscard`.
- **Masque.ConnectRequest** · [`export let pseudo_values = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L80)
  The pseudo-header values, parallel to `pseudo_names`. Tagged `#NoDiscard`.
- **Masque.ConnectRequest** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L91)
  Equal pseudo-header values and fields.
- **Masque.ConnectRequest** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L99)
  Combines the pseudo-header values and the fields.
- **Masque.ConnectRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L103)
  The HEADERS lines as in the RFC figures, "name = value" joined by "; ".
- **Masque** · [`export let Tunnel = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L119)
  A proxied UDP or IP flow carried by a request stream. Tagged `#Final`, `#NotThreadSafe`.
- **Masque.Tunnel** · [`export last request: ConnectRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L121)
  The request that opened the tunnel.
- **Masque.Tunnel** · [`export let init = func(opening: ConnectRequest)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L124)
  Records the request; only `open` creates tunnels.
- **Masque.Tunnel** · [`export let send_datagram = func(datagram: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L130)
  Sends one HTTP Datagram with the payload (for CONNECT-UDP, use `encode_udp_datagram`).
- **Masque.Tunnel** · [`export let receive_datagram = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L135)
  Receives the next HTTP Datagram payload. Tagged `#NoDiscard`.
- **Masque.Tunnel** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L139)
  Closes the request stream and with it the tunnel.
- **Masque.Tunnel** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L142)
  The protocol and path.
- **Masque** · [`export let udp_path = func(target_host: string, target_port: int) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L148)
  The default CONNECT-UDP path for a target (RFC 9298 section 2), for example `/.well-known/masque/udp/192.0.2.6/443/`. Tagged `#NoDiscard`.
- **Masque** · [`export let ip_path = func(target: string, ipproto: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L160)
  The default CONNECT-IP path (RFC 9484 section 3), for example `/.well-known/masque/ip/*/*/`. `target` is `*`, a host, or an address with an optional `/length`; `ipproto` is `*` or 0 through 255. Tagged `#NoDiscard`.
- **Masque** · [`export let udp_request = func(proxy_authority: string, target_host: string, target_port: int) -> Result<ConnectRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L169)
  The extended CONNECT request for UDP proxying to `target_host`:`target_port` through the proxy at `proxy_authority`, with the default template (RFC 9298 section 3.4). Errors are those of `udp_path`. Tagged `#NoDiscard`.
- **Masque** · [`export let ip_request = func(proxy_authority: string, target: string, ipproto: string) -> Result<ConnectRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L178)
  The extended CONNECT request for IP proxying through the proxy at `proxy_authority`, with the default template (RFC 9484 section 4.4). Errors are those of `ip_path`. Tagged `#NoDiscard`.
- **Masque** · [`export let expand = func(value: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L185)
  RFC 6570 simple string expansion of one variable value, with `*` alone kept as the wildcard. Tagged `#Pure`, `#ThreadSafe`.
- **Masque** · [`export let encode_udp_datagram = func(payload: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L213)
  An HTTP Datagram payload carrying a UDP payload: Context ID 0, then the bytes (RFC 9298 section 5). Tagged `#NoDiscard`.
- **Masque** · [`export let decode_udp_datagram = func(datagram: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L225)
  The UDP payload of an HTTP Datagram payload with Context ID 0 (RFC 9298 section 5). Err (UnexpectedEnd) without a complete Context ID; Err (Syntax) for another Context ID, which the receiver drops or buffers. Tagged `#NoDiscard`.
- **Masque** · [`export let open = func(request: ConnectRequest) -> Result<Tunnel>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L241)
  Sends `request` on an HTTP/3 connection to the proxy and, on a 2xx response, returns the tunnel (RFC 9298 section 3.5, RFC 9484 section 4.5). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
