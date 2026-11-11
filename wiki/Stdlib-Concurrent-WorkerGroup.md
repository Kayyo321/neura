# `Std.Concurrent.WorkerGroup`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Concurrent.WorkerGroup`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Concurrent.WorkerGroup`](#exported-declarations)
- [The `Std.Concurrent` modules and their contracts](Stdlib-Design#19-stdconcurrent)
- [Concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md): tasks, isolation, the memory model, the scheduler, and limits
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **WorkerGroup** · [`export let SubmitOutcome = enum (Submitted, Closed, Cancelled)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L11)
  What submit reports. Closure and cancellation are expected events, not errors.
- **WorkerGroup** · [`export let TrySubmitOutcome = enum (Submitted, Full, Closed)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L14)
  What try_submit reports.
- **WorkerGroup** · [`export let WorkerGroup = class<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L18)
  The handle through which a body gives a group its jobs. It holds channel ends, so it can be given to a task. A job is a value of the transferable type T.
- **WorkerGroup.WorkerGroup** · [`export let init = func(jobs: channels.Sender<T>, closing: channels.Closer<T>, stops: channels.Receiver<int>, worker_count: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L26)
  Wraps the ends of a group's channels. Use with_workers.
- **WorkerGroup.WorkerGroup** · [`export let submit = func(job: T) -> SubmitOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L37)
  Queues the job. Waits while the queue is full. Reports Closed once close_and_drain has closed the queue, and Cancelled when the submitting task is cancelled while it waits; the job is then not queued.
- **WorkerGroup.WorkerGroup** · [`export let try_submit = func(job: T) -> TrySubmitOutcome`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L44)
  Queues the job if there is room. Never waits, so a service that must shed load counts Full.
- **WorkerGroup.WorkerGroup** · [`export let close_and_drain = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L54)
  Closes the queue and returns once every queued job has been processed and every worker has stopped. Later submissions report Closed. Call it from one task; on this handle a second call returns at once. If the calling task is cancelled while it waits, it returns early and with_workers waits for the workers instead.
- **WorkerGroup** · [`export let with_workers = func<T>(workers: int, capacity: int, work: func(T) -> void, body: func(WorkerGroup<T>) -> void)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/concurrent/worker-group.nra#L80)
  Starts `workers` tasks that take jobs from a queue of `capacity` jobs and run `work` on each; runs `body` with the group; then closes the queue, lets the workers finish every queued job, and returns once they have stopped. A worker also stops when its scope is cancelled, leaving queued jobs unprocessed; the jobs it was running finish first. T must be transferable and `work` task-safe, with transferable captures, as for any spawn. Fewer than one worker or a negative capacity traps with `invalid argument: workers` or `invalid argument: capacity`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections give this module's contract from the [standard-library design](Stdlib-Design#19-stdconcurrent) and the [concurrency specification](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md). The source and current [language status](Status) identify shipped behavior.

### Contract

A `WorkerGroup` is a fixed number of workers over a bounded queue of jobs ([Concurrency, section 9](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#9-stdconcurrent)). It is the answer to overload: a service bounds its in-flight work with a group and a queue long before it reaches the run-wide limits. Because tasks need a scope, a group exists only inside `with_workers`, which starts the workers, runs the body that submits the jobs, and drains the queue before it returns.

```neura
groups.with_workers<string>(2, 8, record, func(group: groups.WorkerGroup<string>) -> void {
    group.submit("started");
    match (group.try_submit("optional detail")) {
        groups.TrySubmitOutcome.Full => { },  // Shed: the queue is full.
        _ => { },
    }
});
```

- `with_workers<T>(workers: int, capacity: int, work: func(T) -> void, body: func(WorkerGroup<T>) -> void)` starts `workers` tasks that take jobs from a queue of `capacity` jobs and run `work` on each. It runs `body` with the group, then closes the queue, lets the workers finish every queued job, and returns once they have stopped.
- Fewer than one worker traps with `invalid argument: workers`, and a negative capacity with `invalid argument: capacity`. Capacity 0 is a rendezvous: a job is handed straight to a waiting worker.
- `submit(job)` queues the job and parks while the queue is full. It returns `SubmitOutcome`: `Submitted`, `Closed` once the queue is closed, or `Cancelled` when the submitting task is cancelled while it waits; the job is then not queued.
- `try_submit(job)` never parks. It returns `TrySubmitOutcome`: `Submitted`, `Full`, or `Closed`. A service that must shed load counts `Full`.
- `close_and_drain()` closes the queue and returns once every queued job has been processed and every worker has stopped. Later submissions report `Closed`. `with_workers` calls it when the body returns, so a body calls it only to drain earlier. On one handle a second call returns at once. If the calling task is cancelled while it waits, it returns early, and `with_workers` waits for the workers instead.
- A worker also stops when its scope is cancelled. Queued jobs are then left unprocessed; the job a worker is running finishes first.
- The handle's constructor is exported so that `with_workers` can build it. Programs use `with_workers`.

### Transfer and task safety

- `T` must be transferable, because a job crosses a channel. A job type that is not is reported at the program's call of `with_workers`: `a value of type Box is not transferable, so it cannot be a channel's element type`.
- `work` must be task-safe and its captures transferable, as for any spawn. A violation is reported at the program's call of `with_workers`, with the call chain and the place where the library starts the worker ([section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)).
- The handle holds channel ends, so it is transferable: a task can be given it to submit jobs.
- The functions are task-safe. They use scheduler operations, so the worker of a parallel map cannot use a group.
- A program that uses this module cannot use `Std.DynamicForeign` or a C import ([decision C15](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#3-decisions)).
