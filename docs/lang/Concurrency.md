# Concurrency: design and implementation plan

This page specifies the planned concurrency language and runtime. The compiler still does not recognize `scope`, `spawn`, or `select`, and `Std.Concurrent` has no modules. A partial NVR 1.5 artifact/runtime slice is implemented: scheduler exits, poll markers, transferable-layout metadata and validation, manual-image task execution, task limits, and `Execution::dump_tasks()`. The compiler, source-level transferability and task-safety analyses, concurrent record/replay, and all production-I/O and service features remain unimplemented. The phase table records what is delivered and what remains.

## 1. How to read this plan

- "Must" states a requirement on the implementation. A row of section 3 is a decision. Section 18 lists what is still open; an implementer who reaches an open item stops and records the choice with the change, as the [repository rules](../../AGENTS.md) require.
- Names of options, modules, and members are fixed here. Binary codes, trace line spellings, and numeric defaults are fixed by the phase that introduces them, in the page that owns them ([NVR](../runtime/NVR.md), [NVM](../runtime/NVM.md), [NeuraIR format](../NirFormat.md)). Until then they are proposals.
- Code blocks marked `neura` show proposed syntax. None of them compiles today.

| Term | Meaning |
| --- | --- |
| Task | One independently suspendable call: its own frames, wait state, and continuation. Tasks are cheap; a service can hold many thousands. |
| Root task | The task that runs program initialization and `main`. |
| Scope | A `scope` block. It owns the tasks spawned lexically inside it and waits for them on every exit. |
| Park | Suspend a task until another task, a timer, or a host completion makes it runnable. |
| Scheduling point | A place where the scheduler may switch tasks (section 10.2). There are no others. |
| Tick | The count of scheduling decisions made so far in a run. Ticks, not wall time, order events. |
| Transferable | A type whose values can be given to another task (section 5.1). |
| Task-safe | A call whose transitive effects are allowed off the root task (section 5.2). |
| Context | The cancellation signal and earliest deadline a task inherited (section 7). |

**Stages affected.** All of them except the lexer and the C import stage. Section 13 gives each stage's part and the invariant it hands to the next.

## 2. Summary

Neura gets **lightweight, scoped tasks**. `spawn fetch_user(id)` starts an ordinary function call as a task, as directly as Go's [`go fetchUser(id)`](https://go.dev/ref/spec#Go_statements), and a task is as cheap to hold as a [Java virtual thread](https://docs.oracle.com/en/java/javase/25/core/virtual-threads.html). A `scope` gives tasks a lifetime: no task outlives the block that started it. `Std.Concurrent` provides channels, cancellation, deadlines, and bounded worker groups. The reference NVM runs every task on one deterministic scheduler, so a run is reproducible and replayable, and two tools make that useful in production: a task dump that says what is stuck, and a replay that reproduces it.

Concurrency and parallelism are separate here. Tasks make it cheap to have many operations *in flight*, most of them waiting on I/O or on each other. Using several cores for computation is a different contract, introduced later and separately as `parallel_map` over isolated work (section 12). Both use the same ordinary function style.

For readers who know another language's vocabulary:

| Familiar | In Neura |
| --- | --- |
| Thread, goroutine, virtual thread | A task, started with `spawn` |
| `async` function, `await` | Not needed. Any function may suspend; `task.join()` waits for a result |
| Future, promise | `Task<T>` |
| Structured task scope, nursery | `scope { ... }` |
| Channel, blocking queue | `Std.Concurrent.Channel` |
| `select`, `epoll` loop | `select { ... }` |
| Cancellation token, context, deadline | The scope's signal, inherited by its tasks |
| Thread pool, executor | `Std.Concurrent.WorkerGroup` |
| Mutex-protected object | `Std.Concurrent.Guarded<T>`, or a task that owns the object |
| Thread dump | Task dump |

## 3. Decisions

| ID | Decision |
| --- | --- |
| C1 | Tasks, with one function style. There are no `async` functions and no `await`. A function called from a task may suspend at a channel, a join, or host I/O without any marking in its signature. |
| C2 | Concurrency is structured. Every `spawn` is lexically inside a `scope` of the same function body, and every exit from that scope waits for its tasks and their descendants. There are no detached tasks. |
| C3 | `scope`, `spawn`, and `select` are contextual. The lexer keeps producing identifier tokens; the parser recognizes the forms by position (section 4.4). Programs that use these words as names keep their meaning. |
| C4 | `spawn` evaluates its callee and arguments in the parent, in the ordinary order, and returns `Std.Concurrent.Task<T>`. |
| C5 | `Task<T>` and `Scope` values are scope-bound: they cannot leave the scope that created them (section 4.3). |
| C6 | Tasks are isolated. Spawn arguments and channel messages must be transferable, and a spawned call must be task-safe. Lowering to NeuraIR checks both over the whole linked program. |
| C7 | A join transfers the finished child's result to the joiner whatever its type. Join results are not restricted to transferable types; section 5.3 gives the reason. |
| C8 | The error model does not change. Expected failures are returned values that `join` hands to the caller. A trap in any task ends the run. Nothing unwinds. |
| C9 | Cancellation is cooperative and inherited through scopes. It wakes parked operations with a typed result. It never interrupts a running task, and `join` is never cancelled. |
| C10 | Channels are typed and bounded, with separate send, receive, and close capabilities. There is no unbounded channel. |
| C11 | The reference executor is one deterministic scheduler inside the NVM with FIFO queues. A program may rely on the memory model of section 5.4 and the channel rules of section 6. It must not rely on FIFO order, which exists to make runs reproducible. |
| C12 | Preemption is by compiler-inserted polls that count language operations (loop back edges and calls), never steps, blocks, or time. |
| C13 | Scheduler state is NVM state reached through explicit exits, like the heap. Graph profile 1 does not change. An image that uses tasks declares a new NVR minor version and is rejected by earlier runtimes. |
| C14 | Host I/O may overlap only where the provider declares it. Completions are delivered at scheduling points and recorded by tick, so a replay delivers them at the same points. |
| C15 | The first concurrent image profile excludes native bindings and callbacks: a program cannot both spawn a task and use `Std.DynamicForeign` or `import:c(...)`. |
| C16 | First-use static initialization stays on the root task. |
| C17 | Multicore execution begins with `parallel_map` over isolated computations. Its meaning is the sequential map; more workers may change only wall time. |
| C18 | A deadlock ends the run with a diagnostic, a task dump, and status 134 (`Abort`). |

## 4. Scoped tasks

### 4.1 `scope`, `spawn`, and `join`

```neura
let load_page = func(user_id: int64) -> result.Result<Page> {
    scope {
        let user_task = spawn fetch_user(user_id);
        let orders_task = spawn fetch_orders(user_id);

        let user = user_task.join();
        let orders = orders_task.join();

        return build_response(user, orders);
    }
};
```

**`scope { ... }`** is a statement. Its block is an ordinary block: it has its own bindings, and `return`, `break`, and `continue` act on the enclosing function or loop as they would in any block. What it adds is an exit rule:

- Every ordinary exit, by falling off the end, `return`, `break`, or `continue`, waits until every task spawned in the scope, and every descendant of those tasks, has finished.
- A `return` evaluates its expression first, then waits, then returns the value.
- Waiting is not cancellation. A scope that should stop its tasks early cancels them (section 7) and then exits; the exit still waits for them to finish.
- Scopes nest. A task belongs to the nearest enclosing scope of its `spawn`.

`scope (name) { ... }` also binds the scope's `Std.Concurrent.Scope` handle to a constant `name` visible in the block. The handle cancels the scope's tasks and sets their deadline.

**`spawn call`** is an expression. Its operand is a call that begins with a name: `fetch_user(id)`, `client.fetch(id)`, or `handler(request)` for a function value. A lambda is bound to a name first.

1. The parent evaluates the callee expression, including a method's receiver, and then the arguments from left to right, exactly as for the call itself ([Core language](CoreLanguage.md#functions-and-control-flow)). Overload resolution, casts, and variadic packing happen here.
2. The runtime creates a task for the call, appends it to the runnable queue, and returns a `Task<T>` to the parent, where `T` is the call's result type. A `void` call gives `Task<Unit>`, with `Unit` from `Std.Core`.
3. The parent continues. `spawn` is not a scheduling point; the child first runs when the parent next reaches one.

**`task.join()`** waits until the task has finished and returns its result. Joining again returns the same value. Only the task that executes the scope can join the scope's tasks, because the handle cannot be given to anyone else. A scope may exit without joining a task; its result is then discarded.

Because scopes are lexical, **no task outlives `main`**: when `main` returns, every scope it entered has already waited for its tasks. A background worker that should run for the life of a service is spawned in a scope near the top of `main`, where shutdown can cancel it and wait for it.

### 4.2 Errors and traps

Nothing in [Errors](Errors.md) changes.

- A task's function returns its failures as values. If `fetch_user` returns `Result<User>`, then `user_task.join()` is that `Result<User>`, and the caller reads it with `match`.
- A trap in any task ends the run at once, with that trap's status. No other task runs afterwards, no cleanup runs, and completed external effects stay visible. The diagnostic names the task and its spawn site.
- `Std.Program.Process.exit` from any task ends the run with its status, abandoning every task.

Policies that combine results are named helpers in `Std.Concurrent.Tasks`, not language rules: `all_ok` cancels the remaining calls at the first `Err`, and `first_ok` returns the first `Ok` and cancels the rest. Each helper opens its own scope, so it has cancelled *and drained* every call it started before it returns.

### 4.3 Scope-bound values

A `Task<T>` or `Scope` must not be reachable after its scope exits. The rule is by type: a type is scope-bound if it is `Task<T>`, `Scope`, or an array, list, or tuple with a scope-bound element. A scope-bound value may be:

- held in a local binding declared inside the scope, including a local list, for a fan-out of unknown width;
- passed as an argument to a call, which returns before the scope can exit.

It may not be returned from a function, stored in a field, global, or enum payload, captured by a lambda, passed to `spawn`, sent on a channel, or assigned to a binding declared outside the scope.

### 4.4 Grammar and rejected programs

The parser recognizes the contextual words only in these positions:

| Form | Recognized when | Why existing code is unaffected |
| --- | --- | --- |
| `scope {` | `scope` begins a statement and the next token is `{` | A bare name followed by `{` is not a statement today. Enum constructors with named payloads are always qualified (`Tree.Branch { ... }`). |
| `scope (name) {` | `scope` begins a statement followed by `(`, one identifier, `)`, and `{` | A call statement `scope(name);` is followed by `;`, never `{`. |
| `spawn name...` | `spawn` begins an expression and the next token is an identifier | Two adjacent identifiers are not an expression today. `spawn(x)` remains a call of a function named `spawn`. |
| `select {` | `select` begins a statement and the next token is `{` | As for `scope {`. |

Representative rejections. Each is reported at the named span with both the rule and the offending type or effect:

| Program | Diagnostic at |
| --- | --- |
| `spawn work(x)` outside any `scope` of the same function body, or inside a lambda nested in the scope | The `spawn` expression |
| `spawn update_cache(cache)` where `cache` is a `HashMap` | The argument `cache`: its type is not transferable (section 5.1) |
| `spawn audit(entry)` where `audit` writes a module global through two calls | The `spawn` expression, with the call chain to the write (section 5.2) |
| `return user_task;` or `tasks_field = user_task;` | The escaping expression (section 4.3) |
| `spawn (pick())(x)` | The `(`: a spawn operand begins with a name |
| A program that spawns a task and imports `Std.DynamicForeign` | The `spawn` expression that makes the image concurrent (C15) |

## 5. Isolation between tasks

A task's mutable state is its own. Objects, arrays, and lists that a task creates stay with it; another task can affect them only by sending it a message. This holds on one scheduler and stays true when section 12 adds cores.

### 5.1 Transferable types

A value given to another task through a spawn (the callee value, a receiver, and each argument) or a channel must have a transferable type:

| Transferable | Not transferable |
| --- | --- |
| `bool`, integers, floats, fieldless enums | Class instances, including library collections |
| `string`, and immutable bytes where a library exposes them | Arrays and lists, whose elements can be assigned |
| A tuple whose elements are transferable | Primitive references (`&int`) |
| A payload enum all of whose variants carry transferable payloads | `MemorySegment`, arenas, and every other native handle |
| A named function, or a closure all of whose captures are transferable | A closure that captures an object, array, or list |
| The handles of `Std.Concurrent`: channel endpoints, `AtomicInt64`, `Semaphore`, `Guarded<T>` | `Task<T>` and `Scope` (scope-bound, section 4.3) |

The check is recursive and on static types. A generic function is checked at each specialization. Copying a closure does not copy what it captured: a closure that captures an object aliases that object ([Core language](CoreLanguage.md#function-values-and-closures)), which is why such a closure is not transferable.

`Std.Core.Error` is not transferable as declared, because its `Custom` variant holds an `ErrorDetail` object ([Stdlib](Stdlib.md#63-error)). A `Result<T>` therefore crosses a join (section 5.3) but not a channel; section 18 lists the choice that would let it.

### 5.2 Task-safe calls

A value check alone is not enough: a function can reach shared mutable state without being handed it. A spawned call must be task-safe, meaning that neither it nor anything it can call, transitively:

- reads or writes a module, file, or static binding that is reassignable or whose type is not transferable. Reading a `const` global of transferable type is allowed;
- makes an active use that could run a [static initializer](Types.md#static-initialization). A class with no static fields that need run-time initialization, and whose bases and interfaces have none, is safe to construct (C16);
- issues a `neura.ffi.*` request (C15).

Lowering computes one effect summary per function over the whole linked program, including [prebuilt NeuraIR modules](Modules.md#prebuilt-neurair-modules), and checks it at each `spawn`. For a virtual call or a call through a function value, the summary is the union over every override or function value of that type the program can create. That is conservative: a call rejected for an implementation it never reaches is fixed by narrowing the type or passing the needed value explicitly. The diagnostic lists the call chain from the `spawn` to the offending operation.

The root task is not restricted. It may use mutable globals as today, because no other task can reach them.

Each `Std` module's page must state whether its functions are task-safe. Phase 1 audits the modules a service needs first: `Std.Core`, `Std.Text`, `Std.Collections`, `Std.Io.Console`, `Std.Net`, `Std.Time`, and `Std.Log`.

### 5.3 Results cross a join by transfer

When a task finishes, nothing else can reach what it built: it received only transferable values, it could not touch mutable globals, the scopes it opened have waited for its own children, and its frames are gone. Its result is therefore the only reference to that part of the heap, and handing it to the joiner shares nothing. So `join()` returns any `T`, including a list the child built or a `Result<T>` whose error holds a custom detail object. The handle cannot leave the scope's task, so a second `join()` gives the same reference to the same task, which is ordinary aliasing inside one task.

The alternative, restricting join results to transferable types as spawn arguments are, was rejected. Under it the example of section 4.1 fails to compile whenever `fetch_user` returns `Result<User>`, and no task could return a collection. C7 keeps isolation and removes both problems. `parallel_map` results stay restricted to transferable types (section 12), because its workers may not share a heap.

### 5.4 Memory model

These are the ordering guarantees a program may rely on. An effect A is *visible to* B when:

- A is the evaluation of a spawn's callee and arguments, and B is anything the child does;
- A is anything a task does before it finishes, and B follows a `join` of that task;
- A precedes a send, and B follows the receive that obtains that message;
- A precedes `close`, and B follows an operation that reports `Closed`;
- A precedes a cancellation request, and B follows an observation of that request;
- A and B are operations on one `AtomicInt64`, which are totally ordered, each seeing the latest earlier store;
- A is inside one `Guarded<T>.with` call and B is inside a later one on the same guard.

Isolation means there is nothing else to order: two tasks have no other common state. The reference scheduler's FIFO order is deterministic but is not part of this model (C11).

## 6. Channels and `select`

**Channels are the default way to share work.**

```neura
let channels = import:Std.Concurrent.Channel;

let [jobs, next_job, jobs_closer] = channels.open<Job>(64);
```

`open<T>(capacity)` returns a `Sender<T>`, a `Receiver<T>`, and a `Closer<T>`. `T` must be transferable. A coordinator keeps the closer and hands out the endpoints, so a worker that only receives cannot send or close. All three are transferable handles.

| Rule | Behavior |
| --- | --- |
| Capacity 0 | A rendezvous: `send` parks until a receiver takes the value. |
| Capacity above 0 | A FIFO buffer of that many values. `send` parks while it is full, which is how a slow consumer slows its producers. A negative capacity traps with `invalid argument: capacity`. |
| Order | Values are received in the order their sends committed. Parked senders and parked receivers are each served first come, first served. |
| `close()` | Idempotent. Afterwards a send reports `Closed`. Values already buffered are still received, in order; receivers see `Closed` only once the buffer is empty. Senders parked at the close wake with `Closed`, and their values are not delivered. |
| Lifetime | A channel is an ordinary collected heap object. Nothing closes it automatically. |

Operations report typed outcomes. Closure and cancellation are expected events, not errors:

| Operation | Result |
| --- | --- |
| `sender.send(value)` | `Sent`, `Closed`, or `Cancelled` |
| `receiver.receive()` | `Received<T>`: `Value(T)`, `Closed`, or `Cancelled` |
| `sender.try_send(value)` | `Sent`, `Full`, or `Closed`; never parks |
| `receiver.try_receive()` | `Value(T)`, `Empty`, or `Closed`; never parks |

```neura
let worker = func(next_job: channels.Receiver<Job>, results: channels.Sender<Outcome>) {
    do {
        match (next_job.receive()) {
            channels.Received.Value(job) => {
                results.send(handle(job));
            },
            _ => {
                return;  // Closed or Cancelled: stop taking work.
            },
        }
    }
};
```

**`select`** waits on several operations and performs exactly one.

```neura
select {
    let message = inbox.receive() => {
        handle(message);
    },
    let outcome = outbox.send(reply) => {
        note_sent(outcome);
    },
    slow.join() => {
        logger.warn("backend answered late");
    },
    _ => {
        // Nothing was ready. Without this arm, select parks.
    },
}
```

- An arm is `[let name =] operation => { block }`. The operation is a call of a selectable member: `send`, `receive`, `join`, or `Timer.wait`. The binding receives what the operation would return on its own.
- Receivers and arguments of every arm are evaluated once, in source order, before anything is attempted. The value of an unselected `send` is evaluated and then dropped.
- Exactly one operation commits, and only its block runs. No other arm has any effect.
- **Rotating preference.** Each task counts the selects it has committed. A select with `n` arms starts its scan at arm `count mod n` and proceeds cyclically in source order; the first ready arm commits. Selection is thus reproducible, and an arm that is always ready cannot starve the others.
- If no arm is ready, the `_` arm runs when present. Otherwise the task parks on every arm, and the first operation another task or the runtime completes for it is the one that commits.
- A select whose task is cancelled while parked commits a ready arm if there is one. Otherwise each channel arm can commit `Cancelled`, chosen by the same rotation.

## 7. Cancellation, deadlines, and resources

**Cancellation is cooperative and scoped.**

```neura
scope (request) {
    request.cancel_after(durations.of_seconds(2));
    let reply_task = spawn call_backend(query);
    return reply_task.join();  // Ok, or the backend's own Cancelled error.
}
```

- Every scope has a signal. `Scope.cancel()` requests cancellation of the scope's tasks and all their descendants. It is idempotent and cannot be undone.
- A child's context is its scope's signal joined with everything that scope's own task inherited. Cancelling an outer scope therefore cancels every task below it. A scope's deadline is the earlier of its own and its parent's; a nested scope cannot extend one.
- `cancel_after(duration)` and `cancel_at(instant)` set a deadline on the run's monotonic clock. Both need the clock capability and return `Result<Unit>`; without it the error is `CapabilityDenied`. Under virtual time (section 14.4) they need no capability.
- A request wakes every parked channel operation and every parked cancellable host request (section 10.4) of the affected tasks. The channel operation returns `Cancelled`. The host operation returns an `Io` error of the new kind `Cancelled`.
- A running task is never interrupted. A CPU loop asks: `Std.Concurrent.Cancellation.requested()` returns whether the current task's context is cancelled, and `remaining()` returns the time to its deadline, if it has one.
- A task whose cancellation was requested before it first runs still starts, and sees the request on entry.
- `join()` is not cancellable. It always waits for the child's actual result, so a timed-out request can cancel its work and still drain it before releasing what the work was using.
- **One commit point.** Cancellation and completion compete inside the runtime, and exactly one wins. If a send or a host write committed before the cancellation was processed, the operation reports success and the effect stays visible. A cancelled write reports how many bytes were written first.

**Resources.** The language has no destructors that run at scope exit, and this plan adds none ([Object lifecycle](ObjectLifecycle.md), [Stdlib](Stdlib.md#55-resources)). Concurrent code gets a cleanup path from scoped library helpers instead:

```neura
let outcome = files.with_open<Summary>(path, files.OpenMode.Read, func(file: files.File) -> result.Result<Summary> {
    return summarize(file);
});
```

A helper such as `File.with_open` or `TcpStream.with_connection`:

- opens the resource, and returns the open error without calling the body if that fails;
- runs the body in a scope it owns, so every task the helper started that borrows the handle has finished when the body's scope exits;
- closes the resource exactly once on every ordinary completion: a returned value, a returned error, and a cooperative cancellation;
- reports both failures when both happen: a body `Err` with a failed close returns a `Context` error whose cause is the body's error and whose message carries the close error's message. A body `Ok` with a failed close returns the close error.

A trap still ends the run without cleanup, and the runner then closes every file and socket the run left open, as it does today. These helpers reverse one earlier library decision, "there is no scoped helper" ([Stdlib](Stdlib.md#55-resources)); that line was written when a body could only return or end the run, which remains true and is exactly what makes the helper sufficient.

## 8. Shared state

**Start with task-local mutable state.** Most shared state is better owned by one task that serves requests over a channel:

```neura
let cache_owner = func(requests: channels.Receiver<CacheRequest>) {
    let entries = hash_maps.create<string, string>();  // Owned by this task alone.
    do {
        match (requests.receive()) {
            channels.Received.Value(request) => {
                match (request) {
                    CacheRequest.Put { key, value } => {
                        entries.set(key, value);
                    },
                    CacheRequest.Get { key, reply } => {
                        reply.send(entries.get(key));
                    },
                }
            },
            _ => {
                return;
            },
        }
    }
};
```

`entries` is an ordinary map. No other task can reach it, so it needs no synchronization, and the request channel's capacity bounds the backlog.

Phase 4 adds three synchronized handles, each transferable:

| Handle | Contract |
| --- | --- |
| `AtomicInt64` | `load`, `store`, `add`, `compare_exchange`. Sequentially consistent: all operations on one atomic form a single order consistent with each task's program order. `add` is checked like any `int64` addition and traps on overflow, leaving the value unchanged. |
| `Semaphore` | `acquire()` parks until a permit is free and returns `Acquired` or `Cancelled`; `try_acquire()` never parks; `release()` wakes the longest-waiting task. Releasing more permits than were created traps. |
| `Guarded<T>` | Owns a mutable `T` built by an initializer that runs inside the guard. `with<U>(func(state: T) -> U) -> U` runs the function with exclusive access. The function's captures and `U` must be transferable, so no reference to the state can escape. A second task calling `with` parks until the first returns. Calling `with` on a guard the task already holds traps with `invalid state: guard already held`. |

Concurrent collections come after these, as needed, built on `Guarded<T>` or as owner tasks. General shared mutable objects wait for the ownership and aliasing decisions listed under [Lifecycle](Status.md#open-language-decisions).

## 9. `Std.Concurrent`

One module per type, as in the rest of the [library](Stdlib.md#42-source-layout). `Std.Concurrent` sits after `Std.Time` in the dependency order and imports only `Std.Core` and `Std.Time`.

| Module | Contents | Phase |
| --- | --- | --- |
| `Task` | `Task<T>` with `join()` and `is_finished()` | 1 |
| `Scope` | `Scope` with `cancel()`, `is_cancelled()`, `cancel_after`, `cancel_at`, `deadline()` | 2 |
| `Channel` | `open<T>`, `Sender<T>`, `Receiver<T>`, `Closer<T>`, and the outcome enums | 2 |
| `Cancellation` | `requested()`, `remaining()` for the current task | 2 |
| `Timer` | `after(duration)`, `at(instant)`; `wait()` is selectable | 2 |
| `Tasks` | `all_ok`, `first_ok`, `all` | 3 |
| `WorkerGroup` | A fixed number of workers over a bounded queue: `submit` parks when the queue is full, `try_submit` reports `Full`, `close_and_drain()` finishes queued work | 3 |
| `Parallel` | `map<T, U>` (section 12) | 4 |
| `AtomicInt64`, `Semaphore`, `Guarded` | Section 8 | 4 |

`WorkerGroup` is the answer to overload: a service bounds its in-flight work with a group and a queue long before it reaches the run-wide limits of section 10.3.

## 10. Runtime

### 10.1 What stays in the circuit and what crosses the boundary

Neuron graphs keep computing every arithmetic result, comparison, and branch decision, including which `match` arm handles an operation's outcome. Graph profile 1 does not change, and a task body is ordinary compiled blocks.

The scheduler is NVM state, like the heap: task records, the runnable queue, channel buffers and waiter queues, cancellation signals, timers, and pending host requests. A block reaches it through a typed scheduler-operation exit with resolved arguments and a resume block, the same shape as a [state operation](../runtime/NVR.md#state-operations). Host I/O still crosses the runtime boundary only as host requests with declared signatures. The scheduler is not a host service: it has no import, needs no capability, and is fully determined by the image, the inputs, the limits, and the host completion trace.

### 10.2 The reference scheduler

One scheduler runs one task at a time.

- **Queues.** The runnable queue is FIFO. A newly spawned task and a newly woken task are appended. Every waiter queue (a channel's senders and receivers, a semaphore, a guard, a task's joiners) is FIFO.
- **Scheduling points.** The running task changes only when it parks (a `join` of an unfinished task, a `send` to a full channel, a `receive` from an empty one, a `select` with nothing ready, a wait for a permit or a guard, an overlapping host request, a scope exit with live tasks), when it finishes, or at a poll that finds its quantum spent.
- **Polls.** After linkage, lowering inserts a poll at every loop back edge and every call in a program that contains a `spawn`. A poll counts one; when a task has counted its quantum since it was last scheduled, it goes to the back of the runnable queue. The quantum is a run setting, default 1,024, recorded in every trace.
- **Why polls count language operations.** Steps and blocks depend on how conversion shaped the graphs. Loop iterations and calls do not. Optimized and unoptimized builds of one program must therefore execute the same polls in the same order, and the optimizer treats a poll as an effect: it does not remove, duplicate, or move one across another scheduler operation ([NeuraIR optimization](../IrOptimization.md)).
- **Identity.** The root task is task 0. Tasks are numbered in spawn order. Channel and timer identities are numbered in creation order. A dump or trace names the same things in every run of the same trace.
- **Garbage collection.** The root set gains every task's frames, parked or runnable, every buffered message, every value held by a parked sender, every unjoined result, and every pending host request's arguments ([NVM](../runtime/NVM.md#garbage-collection)). Collection remains invisible to the program.

### 10.3 Limits

Existing limits keep their meaning for the run as a whole: steps and heap cells are summed over tasks. `--max-frames` bounds each task's depth. New run-wide limits, each a trap at the operation that would exceed it, checked before any state changes:

| Option | Bounds | Proposed default | Exit status when exceeded |
| --- | --- | --- | --- |
| `--max-tasks` | Live tasks | 65,536 | 137 |
| `--max-queued-messages` | Values buffered in all channels | 1,048,576 | 137 |
| `--max-pending-requests` | Host requests issued and not yet delivered | 4,096 | 137 |
| `--task-quantum` | Polls per turn (section 10.2); a setting, not a limit | 1,024 | |

Phase 1 confirms the defaults against the memory a parked task actually costs and reports the measurement.

**Frame storage (decided for phase 1, runtime).** In an image that carries a `TRNS` table, every live frame of every task counts toward `--max-heap-cells`: one cell for the frame and one for each of its slots, checked before the frame is pushed and released when it returns. Each task record and each open scope also count one cell; a task's record is released when the scope that owns it closes. `--max-frames` still bounds each task's own depth. The reason is the conservative rule for open decisions: without a run-wide bound, up to `--max-tasks` times `--max-frames` frames could be live, so the runtime adds a bound instead of leaving one out, and it reuses an existing limit instead of adding an option this plan does not name. Images without a `TRNS` table keep their existing accounting, so their limits, traps, and `peak_heap_cells` statistics do not change. Fleet admission counts these cells as part of a member's live heap.

**Measured default.** The fixture `tests/backend/task-chain.nvra` is a chain in which every task opens a scope, spawns one child, and parks at the scope exit until `--max-tasks` ends the run. On Linux x86-64 with a Release build, the peak resident memory of `neura run --max-tasks N` (median of three runs, read from `getrusage` of the child process, including the trap diagnostic that ends the run) was 11.4 MiB at N = 1 and 38.5 MiB at N = 65,536: about 27 MiB more for 65,535 parked tasks, an average of 0.43 KB each and about 0.55 KB each between 16,384 and 65,536 tasks. The proposed defaults are kept.

### 10.4 Host I/O

Today the reference executor waits for each host call before it resumes. With tasks, a provider may declare a service **overlapping**: the calling task parks, other tasks run, and the completion is delivered later.

- A request carries its execution-local sequence number, as now. That number is its ID.
- A live run delivers completions only at scheduling points, in the order the provider reported them, appending each woken task to the runnable queue. If no task is runnable and requests are pending, the scheduler blocks until one completes.
- Services that are not declared overlapping complete before the scheduler continues, exactly as today. Console writes stay in this class, so output order is the order tasks ran.
- First overlapping set: `neura.net.connect`, `accept`, `read`, and `write`, `neura.console.read`, and `neura.clock.sleep`. File operations stay synchronous at first.
- **Cancellable requests.** The cancellable form of a service is a new interface version; version 1 keeps its contract. Its provider must answer a cancel request with exactly one of: the completion, if the operation finished first, or error 15 (`Cancelled`), with any partial effect stated in the result (bytes written, for a write). The library maps code 15 to a new `IoErrorKind.Cancelled`.
- Each provider's page states which services overlap, which are cancellable, and what a cancelled operation may have done.

### 10.5 Deadlock

If no task is runnable, at least one is parked, no timer is armed, and no host request is pending, no event can ever wake a task. The run ends with `deadlock: all tasks are parked`, the wait chain, a task dump (section 14.1), and status 134. If a host request is still pending, the run is waiting, not deadlocked; a dump taken then shows which request each task is waiting on.

## 11. Record and replay

[Record and replay](../runtime/NVM.md#determinism-limits-and-observability) extend to concurrent runs. The contract: given the same image, inputs, limits, quantum, and trace, a replay reproduces every scheduling decision, exit selection, effect, and the terminal outcome, without invoking any service and therefore without repeating any external write.

A new trace version adds:

- the quantum and the scheduler limits in the header;
- for each host request, the task that issued it;
- for each overlapping request, the tick at which its completion was delivered, and whether it lost a race with cancellation;
- timer expirations, with their ticks;
- schedule choices that differ from FIFO, written only by schedule exploration (section 14.4);
- in the outcome, a deadlock as its own kind.

A replay that reaches a recorded tick with different scheduler state stops with status 1 and names the first difference, as a diverging request does today. A trace of a run with one task is unchanged, so existing traces keep replaying.

## 12. Multicore: `parallel_map`

Tasks do not use more than one core. The first multicore feature is deliberately narrow:

```neura
let digests = parallel.map<Chunk, Digest>(chunks, hash_chunk, limits);
```

- **Meaning.** `map<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]` returns what calling `worker` on each item in index order would return. `T` and `U` are transferable.
- **Isolation, statically.** `worker` must be task-safe, issue no host request and no scheduler operation, and perform no identity operation (class hash or class text, whose numbers depend on request order). The compiler checks this from the same effect summaries as section 5.2.
- **Order.** Results are published in input order. If several items trap, the run reports the trap of the lowest index, which is the one the reference executor would have reached.
- **Limits.** Each item runs under `ItemLimits` (steps and heap cells), the same under any worker count. An item that exceeds one traps as that item. The run's totals add items in index order.
- **Execution.** The reference executor evaluates items one after another on the scheduler's thread. A multicore executor evaluates them on separate cores, each in its own heap region, bounded by `--parallel-workers`. Only wall time and peak process memory may differ.

This is the same shape as a [fleet](GpuExecution.md#10-batched-running), many independent executions advanced together with results in member order, applied inside one run. It establishes what multicore Neura can deliver before any general parallel scheduler is designed. Benchmarks report I/O concurrency (tasks) and CPU speedup (`parallel_map`) separately; neither figure stands in for the other.

## 13. Compiler stages and the artifact

| Stage | Change | Hands on |
| --- | --- | --- |
| C import | None. A program with C imports cannot spawn (C15). | |
| Lexer | None. `scope`, `spawn`, and `select` stay identifiers. | |
| Parser | The three contextual forms of section 4.4, with spans for the keyword, the operand, and each `select` arm. No typing or name resolution. | Syntax nodes for scope, spawn, and select |
| NeuraIR | Resolves `Task<T>`; checks scope-bound values, transferable types, and task-safe calls over the linked program, including prebuilt `.nir` modules; represents scope entry and exit, spawn, join, each parking operation, select, and polls as explicit operations with effects. | No operation after this point needs a source-level rule. Every spawn names a function and transferable argument types. |
| IR optimization | Scheduler operations and polls are effects. Passes preserve their order relative to each other and to host effects, the evaluation order of spawn operands, and every specified failure. | The same sequence of scheduler operations and polls as the unoptimized program |
| IRtoNeuronConversion | Task bodies become ordinary blocks. Each scheduler operation becomes a typed exit. A poll rides on the exit that takes the back edge or makes the call, so it adds no block. Diagnoses a target that cannot hold the image's task tables. | Exits whose arguments and results are typed like state operations |
| Output | A new NVR minor version with the scheduler-operation exit and a required table marking transferable layouts. | An image a compatible runtime executes without source |

**Artifact and implementation status.** [NVR](../runtime/NVR.md) 1.5 is implemented for scheduler exits, poll bits, and the `TRNS` transferable-layout table. The writer emits the lowest version required, preserving older images. The loader validates sorted/closed transfer marks, scheduler exit shape, and rejects 1.5 task images containing `neura.ffi.*`, `CALB`, or `NATV`. The NVM executes manually assembled task images, checks transferred object layouts, supports the task limit and quantum, and exposes task snapshots. The compiler does not yet emit scheduler operations or transferable markings, and does not implement scope-bound, transferability, or task-safety source checks. The `.nir` format has not yet been revised for task operations. Concurrent record/replay is absent; `nvm::record` rejects task images because the trace format cannot represent scheduling decisions. A native executable accepts `--max-tasks` and `--task-quantum` only through `NEURA_RUN_OPTIONS`: the fixed container policy of [Standalone](Standalone.md#5-the-container) cannot carry baked task limits yet, so the baked-limits half of section 14.3 is not met. These runtime changes do not complete phase 1.

**Inspection.** `disassemble` prints scheduler operations as it prints state operations, with no source map needed. `decompile` labels them as decoded from the tables and reconstructs scopes from entry and exit pairs; where it cannot pair them it shows the operations and says so. Behavior comparison of a recovered image uses replay of a concurrent trace, which is an observed match under that trace, not a proof ([Decompiler](../Decompiler.md#evidence)).

## 14. Tooling

### 14.1 Task dump

A task dump is a read-only snapshot taken at a scheduling point. It never changes the run and is not part of a trace.

```text
task 0  root            parked: scope exit, waiting for 2 tasks        main at service.nra:41:5
  task 3  spawned at service.nra:44:22  parked: receive on channel 2 (0 of 64 queued, 0 senders waiting)
      Service.worker at service.nra:71:16
      Service.serve at service.nra:58:9
  task 4  spawned at service.nra:45:22  parked: host request 918 neura.net.read (socket 7), pending 2.4 s
      Std.Net.TcpStream.read at std/net/tcp-stream.nra:88:20
      Service.handle at service.nra:93:18
      cancellation: requested at tick 51377, deadline passed
```

It shows the parent and child tree, each task's spawn site and stack, its wait reason, queue depths and waiter counts for the channel it waits on, pending host requests, and its cancellation state. Without a source map it shows function and block numbers.

| Trigger | Output |
| --- | --- |
| Deadlock (section 10.5) | Always, to standard error, before the run ends |
| A trap | The trapping task and its spawn chain always; the full dump with `--task-dump` |
| `SIGQUIT` to `neura run` | To standard error; the run continues |
| `nvm::Execution::dump_tasks()` | For embedders |

`--task-dump=json` selects a JSON form with the same content for collectors.

### 14.2 Profiling and trace events

- `neura run --profile` adds, per task and per spawn site, the steps it executed and the ticks it spent runnable (queueing), parked on a channel, and parked on host I/O. This answers whether a slow request was computing, waiting on I/O, or waiting for a turn.
- `neura run --trace-events <file>` writes one JSON object per line: task spawn, start, park, wake, and finish; channel sends, receives, and closes; host requests and completions; cancellations; deadline expirations. Every event carries its tick and task. Wall-clock times are added only with `--trace-wall-time`, because they differ between runs.
- `--stats` adds peak live tasks, peak queued messages, and peak pending requests.

### 14.3 Command line

`run` and `test` accept the limits of section 10.3, `--task-dump[=text|json]`, `--trace-events`, and `--parallel-workers`. They are rejected on other commands. A [native executable](Standalone.md) takes the same settings through its baked limits and its options variable.

### 14.4 Testing concurrent code

- **Virtual time.** `--virtual-time` on `run` and `test` replaces the clock provider with a virtual one. Time moves only when every task is parked, and then jumps to the earliest timer. A two-second timeout test takes no real time and is exact.
- **Schedule exploration.** `neura test --schedules N` runs each test under up to `N` different legal schedules, varying which runnable task goes next and where overlapping completions are delivered, from `--schedule-seed`. Every schedule obeys section 5.4, so a failure is a real bug in the program, not an artifact. Exploration is bounded and makes no claim about schedules it did not run; the report states how many ran.
- **Reproduction.** A failing schedule is written with `--save-schedule <file>` as an ordinary trace. `neura test --schedule <file>` and `neura run --replay <file>` reproduce it exactly.

## 15. Verification

Each row names its boundary and the kind of evidence, as [Contributing](../../CONTRIBUTING.md#verify-behavior) asks.

| Boundary | Check | Kind |
| --- | --- | --- |
| Parser | Each form accepted; each row of section 4.4 rejected at its span; programs using `scope`, `spawn`, and `select` as names unchanged | Conformance |
| NeuraIR | Transfer and effect checks, including through virtual calls, function values, and a prebuilt `.nir`; the call chain in the diagnostic | Conformance |
| IR optimization | Optimized and unoptimized builds give the same results, traps, host effects, and tick-for-tick schedule on every concurrent fixture | Differential |
| Conversion and NVM | Fixtures against the NeuraIR reference evaluator extended with the same scheduler; repeated runs from identical state give identical traces | Differential, determinism |
| Format | Hand-assembled 1.5 images; scheduler operations in an image that declares an earlier version, an unclosed transferable marking, and a concurrent image with `NATV` are rejected | Structural |
| Channels and cancellation | Rendezvous, full buffers, close with parked senders, cancellation racing completion in both orders, nested shutdown, collection with parked tasks under a heap limit | Runtime |
| Replay | Recorded runs replay with every capability denied; a perturbed trace reports the first difference | Replay |
| Decompiler | Scheduler operations in images without `SMAP` and `NAME`; behavior under a replayed trace | Structural, observed |
| `parallel_map` | One worker against several: values, the trapping item, and item limits | Differential |

## 16. Phases and release gates

A phase does not start until the gate of the one before it is met and reported with its commands and results.

| Phase | Delivers | Gate |
| --- | --- | --- |
| **1. Scoped tasks** | `scope { }`, `spawn`, `join`; the transfer and effect checks; the reference scheduler and polls; task limits; task dumps; NVR 1.5 and the `.nir` revision | Standalone images behave identically across repeated runs. Source checks explain invalid captures, unsafe calls, and escaping handles. Optimized and unoptimized builds agree on results, traps, effects, and schedule. |
| **2. Production I/O** | Bounded channels, `select`, `scope (name)` with cancellation and deadlines, overlapping and cancellable providers, the resource helpers, concurrent record and replay | Cancellation against completion, close with parked senders, nested shutdown, and collection with parked tasks are exercised. Replay passes with providers disabled. The artifact checks pass without source maps. |
| **3. Service tooling** | `WorkerGroup`, the `Tasks` helpers, virtual time, schedule exploration, profiling, and trace events | A service fixture sheds overload through a bounded queue, times out a dependency, drains on shutdown, and reproduces a failing interleaving from a saved trace. |
| **4. Multicore and shared state** | `parallel_map`, then `AtomicInt64`, `Semaphore`, and `Guarded<T>`, then concurrent collections as needed | One worker and several agree on values, trap order, and limits. Throughput, tail latency, peak memory, and cancellation latency are published for named workloads, with the machine and baseline. |

**Current implementation status (phase 1 incomplete).** The NVR/NVM runtime/artifact slice described in section 13 is implemented, with focused runtime and format tests now present. The compiler still does not accept or emit the phase 1 language constructs, and the source-level transfer and effect checks (sections 4.3, 5.1, 5.2), the audit of `Std` modules for task safety, poll insertion and its optimizer evidence, and the `.nir` revision are absent. The runtime checks of section 13 (loader marking and spawn argument checks, the scheduler's layout check at a spawn) exist but do not replace them. Of section 14, only `Execution::dump_tasks()` exists, as a flat list of tasks with their states and stacks rather than the tree of section 14.1; `--task-dump`, the `SIGQUIT` dump, trace events, task profiling, and the task peaks in `--stats` are absent. The deadlock exit of section 10.5 is implemented but no phase 1 operation can reach it, because a task waits only for its own children, so it is untested. Concurrent record/replay is absent. Therefore phase 1's gate above is not met; phase 2 and later phases have not started. The test results for this in-progress slice are reported with the implementation handoff, not inferred from the presence of tests.

The first public concurrency release is phases 1 through 3, and its acceptance test is one small network service taken all the way through **fan-out, backpressure, deadline, graceful shutdown, and replay**. Phase 4 uses the isolated executor to establish what multicore Neura delivers before general-purpose threads are considered.

## 17. Documentation to update with the implementation

| Page | Change |
| --- | --- |
| [Resources and concurrency](ResourcesConcurrency.md) | The deferral paragraph is replaced by a pointer here; the resource paragraph gains the scoped helpers. |
| [Core language](CoreLanguage.md), [Types](Types.md) | The three forms, scope-bound and transferable types, and the root-task rule for static initialization. |
| [Errors](Errors.md) | Deadlock and the new limits in the status table; a trap in any task ends the run. |
| [Stdlib](Stdlib.md), [Standard library](StandardLibrary.md) | `Std.Concurrent`; the task-safety of each module; `IoErrorKind.Cancelled`; the scoped helpers in section 5.5. |
| [NVM](../runtime/NVM.md), [NVR](../runtime/NVR.md), [NeuraIR format](../NirFormat.md) | The scheduler, overlapping requests, the trace version, minor version 5, and the `.nir` revision. |
| [Project](../Project.md), [Backend](../Backend.md), [NeuraIR optimization](../IrOptimization.md), [Decompiler](../Decompiler.md) | Each stage's contract from section 13. |
| [Tooling](Tooling.md), [Testing](Testing.md) | The options of section 14 and the test modes. |
| [Status](Status.md), [Release](../Release.md) | Concurrency moves out of "Outside this release" phase by phase, each feature mapped to its tests. |
| An example under `examples/` | A cancellable operation and safe shared state, which [Resources and concurrency](ResourcesConcurrency.md) already asks for. |
| `wiki/` | The matching pages, following [wiki maintenance](../Wiki.md). |

## 18. Open decisions

These need a decision before the phase named. Only the frame-storage row has been settled, for the runtime, in section 10.3; the others are open.

| Area | Open | Before |
| --- | --- | --- |
| Errors on channels | `Error.Custom` holds an object, so `Result<T>` cannot be sent. Either define immutable classes that are transferable, or give `Error` a transferable flattening. | 2 |
| Immutable classes | A class whose fields are all constant and transferable could be transferable. It needs definite field initialization and a rule for `destroy` on a shared object ([Lifecycle](Status.md#open-language-decisions)). | 2 |
| Scope in initialization | Whether top-level statements and initializers may contain a `scope`, given that globals are still being initialized. Proposed: no. | 1 |
| Effect summary precision | The union rule for dynamic calls may reject too much real code. Measure on the standard library before fixing it. | 1 |
| Frame storage | Whether live frames across all tasks need their own run-wide limit or count toward heap cells. Decided for the runtime: they count toward heap cells in images with a `TRNS` table (section 10.3). | 1 |
| Blocking file I/O | When file services become overlapping, and whether a long synchronous file call needs a watchdog. | 3 |
| Native interop | A concurrency contract for native calls, upcalls, and callbacks, lifting C15. | After 4 |
| General parallel scheduler | Running tasks on several cores. It needs the ownership and aliasing rules that are open today. | After 4 |
