# `Std.Concurrent.Scope`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Scope`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Scope`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Scope** · [`export let Scope = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra#L15)
  A scope's cancellation signal and deadline. Tagged `#Final`.
- **Scope.Scope** · [`export let cancel = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra#L25)
  Requests cancellation of the scope's tasks and all their descendants. Idempotent, and it cannot be undone. It wakes their parked channel operations and cancellable host requests; it never interrupts a running task, and the scope's exit still waits for every task.
- **Scope.Scope** · [`export let is_cancelled = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra#L30)
  Whether cancellation of this scope was requested, by cancel or by its deadline.
- **Scope.Scope** · [`export let cancel_after = func(span: duration.Duration) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra#L36)
  Cancels the scope once the duration has passed on the monotonic clock. Needs the clock capability. A later call replaces this scope's own deadline; its tasks still observe the earlier of it and any deadline they inherited. Tagged `#NoDiscard(reason: "without the clock capability no deadline is set")`.
- **Scope.Scope** · [`export let cancel_at = func(moment: instant.Instant) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra#L44)
  Cancels the scope at the instant. Needs the clock capability. Tagged `#NoDiscard(reason: "without the clock capability no deadline is set")`.
- **Scope.Scope** · [`export let deadline = func -> optional.Optional<duration.Duration>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/scope.nra#L54)
  The reading of the monotonic clock at which the scope's tasks are cancelled: the earlier of this scope's deadline and the one it inherited, or None.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Scope` is the handle that `scope (name) { ... }` binds: the scope's cancellation signal and its deadline ([Concurrency, section 7](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#7-cancellation-deadlines-and-resources)). A program does not construct one.

```neura
let stop_after = func(limit: durations.Duration) -> int {
    scope (group) {
        let _ = group.cancel_after(limit);
        let counting = spawn count_until_stopped();
        return counting.join();
    }
};
```

- `cancel()` requests cancellation of the scope's tasks and all their descendants. It is idempotent and cannot be undone. It does not cancel the task that runs the scope, only the tasks spawned below it.
- A request wakes every parked channel operation, timer wait, and cancellable host request of the affected tasks. A channel operation and a timer wait return `Cancelled`, and a host operation returns an `Io` error of kind `Cancelled`, unless it had already finished.
- A running task is never interrupted. It learns of the request by asking [`Cancellation.requested()`](Stdlib-Concurrent-Cancellation).
- The scope's exit still waits for every task. `join()` is not cancellable, so a timed-out request can cancel its work and still drain it.
- `is_cancelled()` reports whether cancellation of this scope was requested, by `cancel` or by its deadline.
- `cancel_after(duration)` cancels the scope once the duration has passed on the monotonic clock, and `cancel_at(instant)` at the instant. A later call replaces this scope's own deadline. Both read the clock and return `Result<Unit>`: the clock is granted by default, and under `--deny-clock` they return the clock's `Err` (kind `CapabilityDenied`) and set nothing.
- `deadline()` is the reading of the monotonic clock at which the scope's tasks are cancelled, as an `Optional<Duration>`: the earlier of this scope's deadline and the one it inherited, or `None`. A nested scope cannot extend a deadline.
- A deadline also stops a task that never parks: while a deadline is armed, a task's turn ends when its quantum is spent even if no other task can run, so that the clock is read ([section 10.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#102-the-reference-scheduler)).
- A task whose cancellation was requested before it first runs still starts, and sees the request on entry.
- Under `--virtual-time` the deadline is a reading of the virtual clock, so a timeout takes no real time ([section 14.4](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#144-testing-concurrent-code)).

### Scope-bound handles

A `Scope` cannot leave its block: it cannot be returned, stored in a field or a global, captured by a lambda, passed to `spawn`, or sent on a channel ([section 4.3](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#43-scope-bound-values)). It can be passed as an argument to an ordinary call. `scope (name)` is allowed in a function with type parameters, and names exactly one scope: `scope (a, b) { }` is reported as `a scope statement names one scope or none: scope (name) { } or scope { }`.

### Task safety

Every member is task-safe. `cancel_after` and `cancel_at` issue a host request to read the clock. A program that uses this module cannot use `Std.DynamicForeign` or a C import ([Concurrency, section 16](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#16-limits)).
