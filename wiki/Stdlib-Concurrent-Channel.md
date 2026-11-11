# `Std.Concurrent.Channel`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Channel`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Channel`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Channel** · [`export let SendOutcome = enum (Sent, Closed, Cancelled)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L7)
  What a send reports. Closure and cancellation are expected events, not errors.
- **Channel** · [`export let TrySendOutcome = enum (Sent, Full, Closed)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L10)
  What a send that never waits reports.
- **Channel** · [`export let Received = enum<T> (Value(T), Closed, Cancelled)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L13)
  What a receive reports: the value, or why there is none.
- **Channel** · [`export let TryReceived = enum<T> (Value(T), Empty, Closed)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L16)
  What a receive that never waits reports.
- **Channel** · [`export let State = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L21)
  The identity the scheduler keeps a channel's buffer and waiters under. The three ends of one channel share one State; the scheduler releases the channel when the State is collected. Tagged `#Final`.
- **Channel.State** · [`export let init = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L23)
  Creates an identity that open has yet to register. Use open.
- **Channel** · [`export let Sender = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L28)
  The sending end of a channel of T. Tagged `#Final`.
- **Channel.Sender** · [`export let init = func(shared: State)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L32)
  Wraps a channel's identity. Use open, which returns the three ends of one channel.
- **Channel.Sender** · [`export let send = func(value: T) -> SendOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L37)
  Sends the value. Waits while the buffer is full, or with capacity 0 until a receiver takes it.
- **Channel.Sender** · [`export let try_send = func(value: T) -> TrySendOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L40)
  Sends the value if that needs no waiting.
- **Channel** · [`export let Receiver = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L70)
  The receiving end of a channel of T. Tagged `#Final`.
- **Channel.Receiver** · [`export let init = func(shared: State)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L74)
  Wraps a channel's identity. Use open, which returns the three ends of one channel.
- **Channel.Receiver** · [`export let receive = func -> Received<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L80)
  Receives the next value in the order the sends committed. Waits while the channel is empty and open. Values already buffered are still received after the channel closes.
- **Channel.Receiver** · [`export let try_receive = func -> TryReceived<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L83)
  Receives a value if one is there.
- **Channel** · [`export let Closer = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L113)
  Closes a channel. A coordinator keeps the closer, so a task that only sends or receives cannot. Tagged `#Final`.
- **Channel.Closer** · [`export let init = func(shared: State)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L117)
  Wraps a channel's identity. Use open, which returns the three ends of one channel.
- **Channel.Closer** · [`export let close = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L124)
  Closes the channel. Closing again does nothing. Afterwards a send reports Closed; parked senders wake with Closed and their values are not delivered; receivers see Closed once the buffer is empty.
- **Channel** · [`export let open = func<T>(capacity: int) -> [Sender<T>, Receiver<T>, Closer<T>]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/channel.nra#L132)
  Opens a channel that buffers `capacity` values of the transferable type T. Capacity 0 is a rendezvous: each send waits for its receive. A negative capacity traps. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

Typed, bounded channels are the default way for tasks to share work ([Concurrency, section 6](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#6-channels-and-select)). `open<T>(capacity)` returns the three ends of one channel: a `Sender<T>`, a `Receiver<T>`, and a `Closer<T>`. A coordinator keeps the closer and hands out the other ends, so a task that only receives cannot send or close.

```neura
let total = func(count: int) -> int {
    let [out, incoming, closer] = channels.open<int>(4);
    let sum = 0;
    scope {
        spawn produce(out, count);
        for (let i: int = 0; i < count; i++) {
            match (incoming.receive()) {
                channels.Received.Value(value) => { sum += value; },
                _ => { },
            }
        }
    }
    closer.close();
    return sum;
};
```

| Rule | Behavior |
| --- | --- |
| Element type | `T` must be transferable; the compiler rejects an element type that is not, at the program's call of `open`. `Result<int>` is not, because `Std.Core.Error` holds an object; an error is sent as a [`PortableError`](Stdlib-Core-Error). |
| Capacity 0 | A rendezvous: `send` parks until a receiver takes the value. |
| Capacity above 0 | A FIFO buffer of that many values. `send` parks while it is full, which is how a slow consumer slows its producers. A negative capacity traps with `invalid argument: capacity`. |
| Order | Values are received in the order their sends committed. Parked senders and parked receivers are each served first come, first served. |
| `close()` | Idempotent. Afterwards a send reports `Closed`. Values already buffered are still received, in order; receivers see `Closed` only once the buffer is empty. Senders parked at the close wake with `Closed`, and their values are not delivered. |
| Lifetime | A channel is an ordinary collected heap object. Nothing closes it automatically. |

| Operation | Result |
| --- | --- |
| `sender.send(value)` | `SendOutcome`: `Sent`, `Closed`, or `Cancelled` |
| `receiver.receive()` | `Received<T>`: `Value(T)`, `Closed`, or `Cancelled` |
| `sender.try_send(value)` | `TrySendOutcome`: `Sent`, `Full`, or `Closed`; never parks |
| `receiver.try_receive()` | `TryReceived<T>`: `Value(T)`, `Empty`, or `Closed`; never parks |

Closure and cancellation are expected events, not errors. A `send` or `receive` that can complete does so even when the task's context is cancelled; `Cancelled` is reported only by an operation that would otherwise park.

### Select

`sender.send(value)` and `receiver.receive()` can be arms of a `select` statement, which waits on several operations and performs exactly one. A send arm is ready when the send would complete, and committing it performs the send. A send arm needs no earlier plain `send` on that channel. `select_handle()`, `send_selected(code)`, and `receive_selected(code)` are what the statement calls; a program does not call them, and it does not construct a `State`, the identity the three ends of one channel share.

### Limits, transfer, and task safety

- The three ends are transferable handles: they can be given to a spawned task or sent on another channel.
- A channel costs 16 heap cells and one more for each buffered message. Values buffered in all channels are bounded by `--max-queued-messages`, 1,048,576 by default; exceeding it traps with status 137 at the send ([section 10.3](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#103-limits)).
- Every operation is task-safe. Each is a scheduler operation, so the worker of a parallel map cannot use a channel.
- A send is ordered before the receive that obtains its message, and `close` before an operation that reports `Closed` ([memory model](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#54-memory-model)).
- A program that uses this module cannot use `Std.DynamicForeign` or a C import ([Concurrency, section 16](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#16-limits)).
