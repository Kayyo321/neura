# `Std.Net.Quic.SessionTicket`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Quic.SessionTicket`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Quic.SessionTicket`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **SessionTicket** · [`export const MaxLifetimeSeconds: int64 = 604800`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L20)
  The longest ticket lifetime in seconds (seven days).
- **SessionTicket** · [`export const QuicEarlyDataSentinel: int64 = 4294967295`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L22)
  The `max_early_data_size` that announces QUIC 0-RTT (0xffffffff).
- **SessionTicket** · [`export let SessionTicket = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L26)
  A received session ticket. Tagged `#Final`.
- **SessionTicket.SessionTicket** · [`export last server_name: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L30)
  The server name the ticket was issued for.
- **SessionTicket.SessionTicket** · [`export last alpn: string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L32)
  The ALPN protocol of the connection that issued it.
- **SessionTicket.SessionTicket** · [`export last received: instants.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L34)
  When the client received it.
- **SessionTicket.SessionTicket** · [`export last lifetime_seconds: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L36)
  The lifetime in seconds.
- **SessionTicket.SessionTicket** · [`export last age_add: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L38)
  The `ticket_age_add` value.
- **SessionTicket.SessionTicket** · [`export last early_data: bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L40)
  True when the server allows 0-RTT with it.
- **SessionTicket.SessionTicket** · [`export let init = func(opaque: uint8[list], name: string, protocol: string, at: instants.Instant, lifetime: int64, add: int64, allows_early_data: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L43)
  Use `create`, which checks the values.
- **SessionTicket.SessionTicket** · [`export let ticket_bytes = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L60)
  The ticket bytes. A copy. Tagged `#NoDiscard`.
- **SessionTicket.SessionTicket** · [`export let age_millis = func(now: instants.Instant) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L70)
  The ticket's age at `now` in milliseconds (negative when `now` is before receipt). Tagged `#Pure`.
- **SessionTicket.SessionTicket** · [`export let is_usable_at = func(now: instants.Instant) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L75)
  True when the ticket may be used at `now`: its age is from 0 up to (not including) the lifetime. Tagged `#Pure`.
- **SessionTicket.SessionTicket** · [`export let obfuscated_age = func(now: instants.Instant) -> int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L83)
  The `obfuscated_ticket_age` at `now`: (age in milliseconds + ticket_age_add) mod 2^32. A negative age traps with "invalid argument: ticket age". Tagged `#Pure`.
- **SessionTicket.SessionTicket** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L92)
  Equal when every field is.
- **SessionTicket.SessionTicket** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L103)
  A hash consistent with equals.
- **SessionTicket.SessionTicket** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L107)
  Text such as "SessionTicket(example.com, h3, 7200 s, 0-RTT, ticket redacted)".
- **SessionTicket** · [`export let create = func(ticket: uint8[list], server_name: string, alpn: string, received: instants.Instant, lifetime_seconds: int64, age_add: int64, max_early_data: int64) -> Result<SessionTicket>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/quic/session-ticket.nra#L116)
  A ticket from a NewSessionTicket: the opaque ticket (1 through 65,535 bytes), the server name and ALPN, the receipt time, `ticket_lifetime` (1 through 604,800 seconds; 0 means discard and is an error here), `ticket_age_add` (0 through 2^32 - 1), and the `max_early_data_size` of its early_data extension (-1 when absent; present, it must be the QUIC sentinel 0xffffffff). Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
