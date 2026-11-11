# `Std.Concurrent.Timer`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Timer`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Timer`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Timer** · [`export let TimerOutcome = enum (Fired, Cancelled)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L11)
  What a wait reports.
- **Timer** · [`export let Timer = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L14)
  A timer that fires once, at a reading of the monotonic clock.
- **Timer.Timer** · [`export let init = func(at: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L16)
  A timer that fires when the monotonic clock reads `at` nanoseconds. Use after or at.
- **Timer.Timer** · [`export let wait = func -> TimerOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L21)
  Waits until the timer has fired. Returns at once when it already has.
- **Timer.Timer** · [`export let select_handle = func -> Timer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L24)
  What a select waits on for this timer. Used by the select statement.
- **Timer.Timer** · [`export let wait_selected = func(code: uint32) -> TimerOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L27)
  The outcome of a wait that a select committed. Used by the select statement.
- **Timer** · [`export let after = func(span: duration.Duration) -> result.Result<Timer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L36)
  A timer that fires once the duration has passed. Needs the clock capability.
- **Timer** · [`export let at = func(moment: instant.Instant) -> result.Result<Timer>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/timer.nra#L43)
  A timer that fires at the instant. Needs the clock capability.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Timer` is a one-shot timer on the monotonic clock ([Concurrency, section 9](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#9-stdconcurrent)). Its wait is selectable, which is how a `select` gives up on its other arms after a time.

```neura
let receive_within = func(incoming: channels.Receiver<int>, limit: durations.Duration) -> int {
    match (timers.after(limit)) {
        result.Result.Ok(timeout) => {
            select {
                let message = incoming.receive() => {
                    match (message) {
                        channels.Received.Value(value) => { return value; },
                        _ => { return -1; },
                    }
                },
                timeout.wait() => {
                    return -2;
                },
            }
        },
        result.Result.Err(_) => { },
    }
    return -3;
};
```

- `after(duration)` returns a timer that fires once the duration has passed, and `at(instant)` one that fires at the instant. Both read the clock and return `Result<Timer>`: the clock is granted by default, and under `--deny-clock` they return the clock's `Err` (kind `CapabilityDenied`).
- `wait()` parks until the timer has fired and returns `TimerOutcome.Fired`. It returns at once when the timer has already fired. When the task's scope is cancelled first, it returns `TimerOutcome.Cancelled`.
- The constructor takes the reading of the monotonic clock, in nanoseconds, at which the timer fires. Programs use `after` and `at`.
- A timer fires when the driver reads the clock at or after its time, which it does at every scheduling point while a timer is armed. A run in which every task is parked and a timer is armed is waiting, not deadlocked.
- Under `--virtual-time` the timer is a reading of the virtual clock: when every task is parked the clock jumps to the earliest timer, so a wait takes no real time and is exact ([section 14.4](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#144-testing-concurrent-code)).

### Select

`timer.wait()` can be an arm of a `select` statement. `select_handle()` and `wait_selected(code)` are what the statement calls; a program does not call them.

### Limits, transfer, and task safety

- A `Timer` is a transferable handle and costs 8 heap cells.
- Every member is task-safe. `after` and `at` issue a host request to read the clock, and `wait` is a scheduler operation, so the worker of a parallel map cannot use a timer.
- A program that uses this module cannot use `Std.DynamicForeign` or a C import ([decision C15](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#3-decisions)).
