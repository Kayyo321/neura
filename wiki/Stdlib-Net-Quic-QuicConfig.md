# `Std.Net.Quic.QuicConfig`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.QuicConfig`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.QuicConfig`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **QuicConfig** · [`export let CongestionAlgorithm = enum (NewReno, Cubic, Ledbat, Bbr3)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L26)
  The congestion controllers.
- **QuicConfig** · [`export let QuicConfig = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L30)
  A validated QUIC configuration. Tagged `#Final`.
- **QuicConfig.QuicConfig** · [`export last tls_config: tls.TlsConfig`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L32)
  The TLS 1.3 configuration, with ALPN.
- **QuicConfig.QuicConfig** · [`export last max_bidirectional_streams: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L34)
  `initial_max_streams_bidi`: bidirectional streams the peer may open.
- **QuicConfig.QuicConfig** · [`export last max_unidirectional_streams: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L36)
  `initial_max_streams_uni`: unidirectional streams the peer may open.
- **QuicConfig.QuicConfig** · [`export last max_datagram_frame_size: uint64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L38)
  `max_datagram_frame_size`; 0 refuses DATAGRAM frames.
- **QuicConfig.QuicConfig** · [`export last idle_timeout: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L40)
  `max_idle_timeout`; zero means none.
- **QuicConfig.QuicConfig** · [`export last max_datagram_size: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L42)
  The maximum UDP payload the endpoint sends.
- **QuicConfig.QuicConfig** · [`export last congestion: CongestionAlgorithm`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L44)
  The congestion controller of new connections.
- **QuicConfig.QuicConfig** · [`export last early_data: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L46)
  True when a client sends, or a server accepts, 0-RTT data.
- **QuicConfig.QuicConfig** · [`export last read_timeout: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L50)
  The default timeout of a connection's waiting operations; zero, the default, waits without limit, as `TcpStream.read_timeout` does. Each waiting operation also has a form with its own timeout. Since 0.3.1.
- **QuicConfig.QuicConfig** · [`export let init = func(tls_settings: tls.TlsConfig, bidirectional: int64, unidirectional: int64, datagram_frame_size: uint64, idle: Duration, datagram_size: int64, algorithm: CongestionAlgorithm, zero_rtt: bool, default_timeout: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L53)
  Use `create`.
- **QuicConfig.QuicConfig** · [`export let new_controller = func -> CongestionController`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L70)
  A new congestion controller of the configured algorithm (Cubic with fast convergence, LEDBAT with a 100 ms target). Bbr3 traps (see `Bbr.bbr3`). Tagged `#NoDiscard`.
- **QuicConfig.QuicConfig** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L82)
  Text such as "QuicConfig(Cubic, alpn h3, 100/100 streams, datagrams 65535)".
- **QuicConfig** · [`export let create = func(tls_config: tls.TlsConfig, max_bidirectional_streams: int64, max_unidirectional_streams: int64, max_datagram_frame_size: uint64, idle_timeout: Duration, max_datagram_size: int64, congestion: CongestionAlgorithm, early_data: bool) -> Result<QuicConfig>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/quic-config.nra#L96)
  A checked configuration (see the module documentation). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
