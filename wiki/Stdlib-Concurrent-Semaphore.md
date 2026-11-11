# `Std.Concurrent.Semaphore`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.Semaphore`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.Semaphore`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Semaphore** · [`export let AcquireOutcome = enum (Acquired, Cancelled)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L8)
  What an acquire reports.
- **Semaphore** · [`export let Semaphore = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L12)
  A fixed number of permits. A permit is a place in a channel: taking one is a receive, so tasks that wait are served first come, first served, and a wait can be cancelled.
- **Semaphore.Semaphore** · [`export let init = func(taking: channels.Receiver<bool>, giving: channels.Sender<bool>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L17)
  A semaphore that owns the two ends of its permit channel. Use create.
- **Semaphore.Semaphore** · [`export let acquire = func -> AcquireOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L23)
  Waits until a permit is free and takes it.
- **Semaphore.Semaphore** · [`export let try_acquire = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L29)
  Takes a permit if one is free; never waits.
- **Semaphore.Semaphore** · [`export let release = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L36)
  Returns a permit and wakes the task that has waited longest. Releasing more permits than the semaphore was created with traps.
- **Semaphore** · [`export let create = func(permits: int) -> Semaphore`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/semaphore.nra#L47)
  A semaphore with `permits` permits, all free. A negative count traps.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

`Semaphore` is a counting semaphore that tasks may share ([Concurrency, section 8](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#8-shared-state)). It bounds how many tasks do something at once.

```neura
let limited = func(permits: semaphores.Semaphore, job: int) -> int {
    match (permits.acquire()) {
        semaphores.AcquireOutcome.Acquired => {
            let answer = square(job);
            permits.release();
            return answer;
        },
        semaphores.AcquireOutcome.Cancelled => {
            return -1;
        },
    }
};
```

- `create(permits)` returns a semaphore with that many permits, all free. A negative count traps with `invalid argument: permits`.
- `acquire()` parks until a permit is free and takes it. It returns `AcquireOutcome.Acquired`, or `AcquireOutcome.Cancelled` when the task's scope is cancelled while it waits; no permit is then held.
- `try_acquire()` takes a permit if one is free and returns whether it did. It never parks.
- `release()` returns a permit and wakes the task that has waited longest. Releasing more permits than the semaphore was created with traps with `invalid state: released more permits than were created`.
- A permit is a place in a channel of `bool`: taking one is a receive. Waiting tasks are therefore served first come, first served, and a wait can be cancelled.
- Nothing releases a permit automatically. A task that returns early, on cancellation for example, releases what it holds first.

### Transfer and task safety

A `Semaphore` is a transferable handle. Every member is task-safe. They use channel operations, so the worker of a parallel map cannot use a semaphore. A program that uses this module cannot use `Std.DynamicForeign` or a C import ([Concurrency, section 16](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#16-limits)).
