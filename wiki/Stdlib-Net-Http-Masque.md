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

- **Masque** · [`export const ConnectUdp: string = "connect-udp"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L29)
  The upgrade token of UDP proxying (RFC 9298 section 3).
- **Masque** · [`export const ConnectIp: string = "connect-ip"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L32)
  The upgrade token of IP proxying (RFC 9484 section 4).
- **Masque** · [`export const Wildcard: string = "*"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L35)
  The wildcard value of the CONNECT-IP template variables (RFC 9484 section 4.6).
- **Masque** · [`export const UdpPayloadContext: uint64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L38)
  The Context ID of UDP payloads (RFC 9298 section 4).
- **Masque** · [`export let ConnectRequest = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L42)
  An extended CONNECT request: the pseudo-header fields and the regular fields. Tagged `#Final`, `#NotThreadSafe`.
- **Masque.ConnectRequest** · [`export last method: string = "CONNECT"`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L44)
  `:method`, always CONNECT.
- **Masque.ConnectRequest** · [`export last protocol: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L46)
  `:protocol`, the upgrade token.
- **Masque.ConnectRequest** · [`export last scheme: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L48)
  `:scheme`.
- **Masque.ConnectRequest** · [`export last authority: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L50)
  `:authority`, the proxy's authority.
- **Masque.ConnectRequest** · [`export last path: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L52)
  `:path`, the expanded template path.
- **Masque.ConnectRequest** · [`export last fields: header_map.Headers`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L54)
  The regular header fields (`capsule-protocol: ?1`).
- **Masque.ConnectRequest** · [`export let init = func(token: string, request_scheme: string, proxy_authority: string, request_path: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L57)
  Stores the pseudo-header values and adds `capsule-protocol: ?1`.
- **Masque.ConnectRequest** · [`export let pseudo_names = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L69)
  The pseudo-header names in the order of the RFC examples: :method, :protocol, :scheme, :path, :authority. Tagged `#NoDiscard`.
- **Masque.ConnectRequest** · [`export let pseudo_values = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L81)
  The pseudo-header values, parallel to `pseudo_names`. Tagged `#NoDiscard`.
- **Masque.ConnectRequest** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L92)
  Equal pseudo-header values and fields.
- **Masque.ConnectRequest** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L100)
  Combines the pseudo-header values and the fields.
- **Masque.ConnectRequest** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L104)
  The HEADERS lines as in the RFC figures, "name = value" joined by "; ".
- **Masque** · [`export let Tunnel = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L120)
  A proxied UDP or IP flow descriptor whose transport operations return `Io(Unsupported)`. Tagged `#Final`, `#NotThreadSafe`.
- **Masque.Tunnel** · [`export last request: ConnectRequest`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L122)
  The request associated with the descriptor.
- **Masque.Tunnel** · [`export let init = func(opening: ConnectRequest)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L125)
  Records a request only; this value owns no native tunnel.
- **Masque.Tunnel** · [`export let send_datagram = func(datagram: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L132)
  Returns `Io(Unsupported)` because MASQUE QUIC DATAGRAM transport is unavailable. Tagged `#NoDiscard`. Since 0.3.1.
- **Masque.Tunnel** · [`export let receive_datagram = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L138)
  Returns `Io(Unsupported)` because MASQUE QUIC DATAGRAM transport is unavailable. Tagged `#NoDiscard`. Since 0.3.1.
- **Masque.Tunnel** · [`export let close = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L144)
  Returns `Io(Unsupported)` because this value owns no native MASQUE tunnel to close. Tagged `#NoDiscard`. Since 0.3.1.
- **Masque.Tunnel** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L148)
  The protocol and path.
- **Masque** · [`export let udp_path = func(target_host: string, target_port: int) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L154)
  The default CONNECT-UDP path for a target (RFC 9298 section 2), for example `/.well-known/masque/udp/192.0.2.6/443/`. Tagged `#NoDiscard`.
- **Masque** · [`export let ip_path = func(target: string, ipproto: string) -> Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L166)
  The default CONNECT-IP path (RFC 9484 section 3), for example `/.well-known/masque/ip/*/*/`. `target` is `*`, a host, or an address with an optional `/length`; `ipproto` is `*` or 0 through 255. Tagged `#NoDiscard`.
- **Masque** · [`export let udp_request = func(proxy_authority: string, target_host: string, target_port: int) -> Result<ConnectRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L175)
  The extended CONNECT request for UDP proxying to `target_host`:`target_port` through the proxy at `proxy_authority`, with the default template (RFC 9298 section 3.4). Errors are those of `udp_path`. Tagged `#NoDiscard`.
- **Masque** · [`export let ip_request = func(proxy_authority: string, target: string, ipproto: string) -> Result<ConnectRequest>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L184)
  The extended CONNECT request for IP proxying through the proxy at `proxy_authority`, with the default template (RFC 9484 section 4.4). Errors are those of `ip_path`. Tagged `#NoDiscard`.
- **Masque** · [`export let expand = func(value: string) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L191)
  RFC 6570 simple string expansion of one variable value, with `*` alone kept as the wildcard. Tagged `#Pure`, `#ThreadSafe`.
- **Masque** · [`export let encode_udp_datagram = func(payload: uint8[list]) -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L219)
  An HTTP Datagram payload carrying a UDP payload: Context ID 0, then the bytes (RFC 9298 section 5). Tagged `#NoDiscard`.
- **Masque** · [`export let decode_udp_datagram = func(datagram: uint8[list]) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L231)
  The UDP payload of an HTTP Datagram payload with Context ID 0 (RFC 9298 section 5). Err (UnexpectedEnd) without a complete Context ID; Err (Syntax) for another Context ID, which the receiver drops or buffers. Tagged `#NoDiscard`.
- **Masque** · [`export let open = func(request: ConnectRequest) -> Result<Tunnel>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/http/masque.nra#L247)
  Returns `Io(Unsupported)` because MASQUE needs HTTP/3 request streams and QUIC DATAGRAM transport. The DNS-over-QUIC provider supplies neither; no proxy connection is opened. Tagged `#NoDiscard`. Since 0.3.1.
<!-- END SOURCE API -->

## Detailed contracts

Capsule and target codecs are available. Tunnel operations return typed `Unsupported` because the QUIC provider only supplies DNS exchanges. [Networking status](Status)
