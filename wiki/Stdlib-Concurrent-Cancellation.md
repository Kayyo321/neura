# `Std.Concurrent.Cancellation`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Cancellation`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/cancellation.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Cancellation`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Cancellation** · [`export let requested = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/cancellation.nra#L12)
  Whether cancellation was requested for the scope that started this task, or for any scope above it. The root task is never cancelled.
- **Cancellation** · [`export let remaining = func -> optional.Optional<duration.Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/cancellation.nra#L16)
  The time left until the task's deadline, or None when it has none or the clock cannot be read. The duration is negative once the deadline has passed.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Cancellation` is what the running task can ask about its own cancellation ([Concurrency, section 7](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#7-cancellation-deadlines-and-resources)). Cancellation is cooperative: a running task is never interrupted, so a loop that computes for long asks.

```neura
let count_until_stopped = func -> int {
    let rounds = 0;
    while (!cancellation.requested()) {
        rounds += 1;
    }
    return rounds;
};
```

- `requested()` is whether cancellation was requested for the scope that started this task, or for any scope above it. A request is made by [`Scope.cancel()`](Stdlib-Concurrent-Scope) or by a scope's deadline.
- The root task is never cancelled, and cancelling a scope does not cancel the task that runs the scope.
- A task whose cancellation was requested before it first runs still starts, and sees the request on entry.
- `remaining()` is the time left until the task's deadline as an `Optional<Duration>`. It is negative once the deadline has passed, and `None` when the task has no deadline or the clock cannot be read, as under `--deny-clock`.
- A parked channel operation, timer wait, or cancellable host request does not need to ask: the request wakes it with `Cancelled`.
- A task that only computes still sees its scope's deadline. While a deadline is armed its turn ends when its quantum is spent and the clock is read, so `requested()` returns true once the deadline has passed ([section 10.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#102-the-reference-scheduler)).

### Task safety

Both functions are task-safe. `remaining()` issues a host request to read the clock. Both are scheduler operations, so the worker of a parallel map cannot call them. A program that uses this module cannot use `Std.DynamicForeign` or a C import ([decision C15](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#3-decisions)).
