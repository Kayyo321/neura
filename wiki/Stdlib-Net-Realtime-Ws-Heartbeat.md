# `Std.Net.Realtime.Ws.Heartbeat`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Net.Realtime.Ws.Heartbeat`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Net.Realtime.Ws.Heartbeat`](#exported-declarations)
- [The `Std.Net` contracts and packages](Stdlib-Design#114-stdnet)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Heartbeat** · [`export let HeartbeatAction = enum (Wait, SendPing, PeerDead)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L15)
  What the connection should do now.
- **Heartbeat** · [`export let Heartbeat = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L19)
  The supervision state of one connection. Tagged `#Final`, `#NotThreadSafe`.
- **Heartbeat.Heartbeat** · [`export last interval: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L21)
  The idle time after which a ping is sent.
- **Heartbeat.Heartbeat** · [`export last timeout: Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L23)
  How long a ping may go unanswered.
- **Heartbeat.Heartbeat** · [`export let pings: int = 0 { get }`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L33)
  The number of pings `poll` asked for.
- **Heartbeat.Heartbeat** · [`export let init = func(every: Duration, patience: Duration, now: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L38)
  A supervisor started at monotonic time `now`. A non-positive interval or timeout traps with "invalid argument: heartbeat duration".
- **Heartbeat.Heartbeat** · [`export let awaiting_pong = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L49)
  Whether a ping is waiting for an answer. Tagged `#NoDiscard`.
- **Heartbeat.Heartbeat** · [`export let record_activity = func(now: Duration)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L52)
  Records that something arrived from the peer at `now`.
- **Heartbeat.Heartbeat** · [`export let poll = func(now: Duration) -> HeartbeatAction`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L62)
  The action due at `now`. `SendPing` marks the ping as sent at `now`. Tagged `#NoDiscard`.
- **Heartbeat.Heartbeat** · [`export let next_deadline = func -> Duration`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L84)
  The monotonic time of the next action: the ping deadline, or the timeout of the outstanding ping. Tagged `#NoDiscard`.
- **Heartbeat.Heartbeat** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/net/realtime/ws/heartbeat.nra#L87)
  The interval, timeout and state.
<!-- END SOURCE API -->

## Detailed contracts

The contract of this module is [section 11.4 `Std.Net`](Stdlib-Design#114-stdnet) of the standard library specification, under its package, together with the module's source comments. This module was added in 0.3.0 and is proposed: it was implemented by autonomous agents and awaits the owner's confirmation. A member marked `@stub` in the source traps as unsupported in 0.3.0. The design note [`docs/design/Net030.md`](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) in the repository records how each item of the networking specification is provided, the tests, and the host service each stub needs. The source and the [language status](Status) identify shipped behavior.
