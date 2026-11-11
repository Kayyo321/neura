# Concurrency: design notes

This page records why the concurrency design is as it is, for contributors. [Concurrency](../lang/Concurrency.md) is authoritative for behavior: the language forms, the checks, the runtime rules, and the limits are defined there, and this page adds no rule to them. The identifiers are kept so that earlier discussion stays searchable.

## Decisions

| ID | Decision |
| --- | --- |
| C1 | Tasks, with one function style. There are no `async` functions and no `await`. A function called from a task may suspend at a channel, a join, or host I/O without any marking in its signature. |
| C2 | Concurrency is structured. Every `spawn` is lexically inside a `scope` of the same function body, and every exit from that scope waits for its tasks and their descendants. There are no detached tasks. |
| C3 | `scope`, `spawn`, and `select` are contextual. The lexer keeps producing identifier tokens; the parser recognizes the forms by position (section 4.4). Programs that use these words as names keep their meaning. |
| C4 | `spawn` evaluates its callee and arguments in the parent, in the ordinary order, and returns `Std.Concurrent.Task<T>`. |
| C5 | `Task<T>` and `Scope` values are scope-bound: they cannot leave the scope that created them (section 4.3). |
| C6 | Tasks are isolated. Spawn arguments and channel messages must be transferable, and a spawned call must be task-safe. The compiler checks both over the whole linked program: the scope-bound rules during lowering, and transferability and task safety on the specialized program, so a generic function is checked at each specialization (section 5). |
| C7 | A join transfers the finished child's result to the joiner whatever its type. Join results are not restricted to transferable types; section 5.3 gives the reason. |
| C8 | The error model does not change. Expected failures are returned values that `join` hands to the caller. A trap in any task ends the run. Nothing unwinds. |
| C9 | Cancellation is cooperative and inherited through scopes. It wakes parked operations with a typed result. It never interrupts a running task, and `join` is never cancelled. |
| C10 | Channels are typed and bounded, with separate send, receive, and close capabilities. There is no unbounded channel. |
| C11 | The reference executor is one deterministic scheduler inside the NVM with FIFO queues. A program may rely on the memory model of section 5.4 and the channel rules of section 6. It must not rely on FIFO order, which exists to make runs reproducible. |
| C12 | Preemption is by compiler-inserted polls that count language operations (loop back edges and calls), never steps, blocks, or time. |
| C13 | Scheduler state is NVM state reached through explicit exits, like the heap. Graph profile 1 does not change. An image that uses tasks declares a new NVR minor version and is rejected by earlier runtimes. |
| C14 | Host I/O may overlap only where the provider declares it. Completions are delivered at scheduling points and recorded by tick, so a replay delivers them at the same points (sections 10.4 and 11). |
| C15 | The first concurrent image profile excludes native bindings and callbacks: a program cannot use both a scheduler operation (a scope, a spawn, a channel, a timer, a guard, a select, or a parallel map) and `Std.DynamicForeign` or `import:c(...)`. The compiler rejects the combination, at the first `spawn` when the program has one and otherwise at its first scheduler operation, and the NVR loader rejects any image that has scheduler exits or polls together with `CALB`, `NATV`, or a `neura.ffi.*` import. |
| C16 | First-use static initialization stays on the root task. A spawned call that could run a static initializer is not task-safe (section 5.2). |
| C17 | Multicore execution begins with `Parallel.map` over isolated computations. Its meaning is the sequential map; more workers may change only wall time. |
| C18 | A deadlock ends the run with a diagnostic, a task dump, and status 134 (`Abort`). |

## Join results are not restricted to transferable types

Restricting join results to transferable types, as spawn arguments are, was rejected. Under it the example of section 4.1 of the specification fails to compile whenever `fetch_user` returns `Result<User>`, and no task could return a collection. C7 keeps isolation and removes both problems. `Parallel.map` results stay restricted to transferable types, because its workers may not share a heap.

## Choices made with the implementation

| Area | Decision or open question | State |
| --- | --- | --- |
| Errors on channels | Decided: `Error` has a transferable flattening. `Std.Core.Error.PortableError` copies an error with `Custom` reduced to its code and message; `to_portable` and `from_portable` convert (section 5.1). `Error` itself and `Result<T>` stay untransferable, and immutable classes stay as the next row says. | Decided |
| Immutable classes | Decided: not transferable in this release. A class instance is transferable only if it is a `Std.Concurrent` handle (section 5.1). A class whose fields are all constant and transferable could be transferable later; it needs definite field initialization and a rule for `destroy` on a shared object ([Lifecycle](../lang/Status.md#open-language-decisions)). | Decided |
| Scope in initialization | Decided: no. `scope` is a function-body statement; in a top-level statement or an initializer it is an error, because globals are still being initialized. | Decided |
| Effect summary precision | The union rule for dynamic calls may reject too much real code. `check --task-safety` reports the summary of every function, which is how to measure it. Measured on the standard library (section 5.2): counting every closure of the linked program rejected a `BufferedReader` or `Utf8Reader` read in any program that linked `Std.Io.Console`, because the console's input source has the same function type. The rule now counts only closures that code which can run creates, which leaves that case to programs that call `Console.input()`. Whether to make it more precise still is open. | Narrowed; open |
| Frame storage | Decided: live frames across all tasks count toward `--max-heap-cells` in images with a `TRNS` table (section 10.3), as do the scheduler's records (20 cells for a task, 16 for a channel plus one per buffered message, 8 for a timer, 8 for a guard, 1 for an open scope). | Decided |
| Scope-bound values | Decided: a scope-bound type may not be a type argument. A scope-bound value may be stored only in a local declared inside the innermost enclosing scope; a parameter is not such a local (section 4.3). | Decided |
| Function values | Decided: a function value is transferable when every closure of its type that the program creates has transferable captures (section 5.1). | Decided |
| Variadic arguments | Decided: they are packed in the parent into an array, which is not transferable, so a variadic call with packed arguments cannot be spawned. | Decided |
| Polls | Decided: a program that contains a `spawn` gets a poll on every `Call`, `VirtualCall`, call through a function value, and loop back edge; the count is per language operation and identical for optimized and unoptimized builds (section 10.2). | Decided |
| Exit statuses | Decided: the task limit, queued-message limit, and pending-request limit end the run with 137; an invalid task operation with 132; a deadlock with 134 (section 10.3 and [Errors](../lang/Errors.md)). | Decided |
| Guard re-entry | Decided: `Guarded<T>.with` on a guard the task holds is the program trap `invalid state: guard already held`; a task parked in `with` is not cancellable. | Decided |
| `Parallel.map` | Decided: it takes and returns arrays, returns results in input order, reports the trap of the lowest index as `(item N of a parallel map)`, runs each item in its own heap with copies of the program's transferable globals, and takes its worker count from `--parallel-workers` (section 12). | Decided |
| Blocking file I/O | File services complete before the scheduler continues (section 10.4). When they become overlapping, and whether a long synchronous file call needs a watchdog, is open. | Open |
| Deadlines for a lone task | Decided: a turn ends when its quantum is spent and a deadline, a timer, or a host request is outstanding, even when no other task can run, so the driver reads the clock and a deadline stops a task that only computes (sections 7 and 10.2). A program with nothing outstanding schedules as before. | Decided |
| Saved schedules | Decided: a failing schedule is saved as a text file of its own (`neura-schedule 1`), not as a host trace, and `neura run --replay` does not take it (section 14.4). | Decided |
| Schedule exploration | Decided: exploration varies which runnable task goes next and not where overlapping completions are delivered; with `--schedules` above 1 or `--virtual-time`, tests run one at a time on the CPU (section 14.4). Varying delivery points is open. | Decided |
| Task settings of a native executable | Decided: the policy records the task limit, the quantum, the queued-message and pending-request limits, and the parallel worker count (container version 2). The options variable can lower the limits and the worker count and cannot change the quantum ([Standalone](../lang/Standalone.md#5-the-container)). | Decided |
| Native interop | A concurrency contract for native calls, upcalls, and callbacks, lifting C15. | Open, after 4 |
| General parallel scheduler | Running tasks on several cores. It needs the ownership and aliasing rules that are open today. | Open, after 4 |

## Earlier choices that changed

- A saved schedule is a text file of its own and not a host trace. An earlier design wrote it as a trace that `neura run --replay` would also take; a test is not a runnable program, so `run` has nothing to replay it against, and a trace would add host responses that a schedule does not need.
- Schedule exploration varies which runnable task goes next and nothing else. An earlier design also varied where the completions of overlapping host requests are delivered.
- The scoped resource helpers `File.with_open` and `TcpStream.with_connection` reverse an earlier library choice, "there is no scoped helper", which was made when a body could only return or end the run. That remains true and is what makes the helper sufficient.
