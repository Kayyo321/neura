# Concurrency: specification and implementation status

This page specifies the concurrency language and runtime and records which parts exist. `scope`, `spawn`, and `select` are implemented, with the scope-bound, transferable, and task-safe checks, NeuraIR 1.9, polls, typed scheduler exits, NVR 1.5, the reference scheduler in NVM, cancellation, channels, timers, guards, and the isolated parallel map, together with the `Std.Concurrent` modules `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Parallel`, `AtomicInt64`, `Semaphore`, and `Guarded`. These parts are still **in progress**: overlapping and cancellable host I/O with its providers, concurrent record and replay, the scoped resource helpers, the `Tasks` helpers and `WorkerGroup`, virtual time, schedule exploration, the published phase 4 measurements, and baked task settings in native executables. Section 13 gives each compiler stage's part and section 16 the status of each phase. [Concurrency example](../../examples/concurrency/service.nra) is a complete program that uses tasks, a cancellable operation, and shared state.

## 1. How to read this page

- "Must" states a requirement on the implementation. A row of section 3 is a decision. Section 18 lists what is decided and what is still open; an implementer who reaches an open item stops and records the choice with the change, as the [repository rules](../../AGENTS.md) require.
- Names of options, modules, and members are fixed here. The NVR operation codes and the exit encoding are final and defined in [NVR](../runtime/NVR.md#scheduler-operations-15); the `.nir` revision is defined in [NeuraIR format](../NirFormat.md#revision-19-tasks). The trace line spellings and the defaults of settings that are in progress are still proposals until the page that owns them records them ([NVM](../runtime/NVM.md)).
- Code blocks marked `neura` compile except where the text says a feature is in progress. A fragment uses names that the program around it declares, such as `fetch_user` or `Job`; the fragments of sections 4 to 8 and 12 are compiled and run with stand-in declarations for those names. [Concurrency example](../../examples/concurrency/service.nra) is complete.

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

Neura gets **lightweight, scoped tasks**. `spawn fetch_user(id)` starts an ordinary function call as a task, as directly as Go's [`go fetchUser(id)`](https://go.dev/ref/spec#Go_statements), and a task is as cheap to hold as a [Java virtual thread](https://docs.oracle.com/en/java/javase/25/core/virtual-threads.html). A `scope` gives tasks a lifetime: no task outlives the block that started it. `Std.Concurrent` provides channels, cancellation, deadlines, timers, and shared-state handles; bounded worker groups are in progress. The reference NVM runs every task on one deterministic scheduler, so a run is reproducible, and two tools make that useful in production: a task dump that says what is stuck, and a replay that reproduces it (replay of concurrent runs is in progress).

Concurrency and parallelism are separate here. Tasks make it cheap to have many operations *in flight*, most of them waiting on I/O or on each other. Using several cores for computation is a different contract, introduced separately as `Parallel.map` over isolated work (section 12). Both use the same ordinary function style.

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
| Thread pool, executor | `Std.Concurrent.WorkerGroup` (in progress) |
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
| C6 | Tasks are isolated. Spawn arguments and channel messages must be transferable, and a spawned call must be task-safe. The compiler checks both over the whole linked program: the scope-bound rules during lowering, and transferability and task safety on the specialized program, so a generic function is checked at each specialization (section 5). |
| C7 | A join transfers the finished child's result to the joiner whatever its type. Join results are not restricted to transferable types; section 5.3 gives the reason. |
| C8 | The error model does not change. Expected failures are returned values that `join` hands to the caller. A trap in any task ends the run. Nothing unwinds. |
| C9 | Cancellation is cooperative and inherited through scopes. It wakes parked operations with a typed result. It never interrupts a running task, and `join` is never cancelled. |
| C10 | Channels are typed and bounded, with separate send, receive, and close capabilities. There is no unbounded channel. |
| C11 | The reference executor is one deterministic scheduler inside the NVM with FIFO queues. A program may rely on the memory model of section 5.4 and the channel rules of section 6. It must not rely on FIFO order, which exists to make runs reproducible. |
| C12 | Preemption is by compiler-inserted polls that count language operations (loop back edges and calls), never steps, blocks, or time. |
| C13 | Scheduler state is NVM state reached through explicit exits, like the heap. Graph profile 1 does not change. An image that uses tasks declares a new NVR minor version and is rejected by earlier runtimes. |
| C14 | Host I/O may overlap only where the provider declares it. Completions are delivered at scheduling points and recorded by tick, so a replay delivers them at the same points. In progress: the scheduler has the mechanism, and no provider declares a service overlapping yet (section 10.4). |
| C15 | The first concurrent image profile excludes native bindings and callbacks: a program cannot both spawn a task and use `Std.DynamicForeign` or `import:c(...)`. The compiler rejects the combination at the first `spawn`, and the NVR loader rejects any image that has scheduler exits or polls together with `CALB`, `NATV`, or a `neura.ffi.*` import. |
| C16 | First-use static initialization stays on the root task. A spawned call that could run a static initializer is not task-safe (section 5.2). |
| C17 | Multicore execution begins with `Parallel.map` over isolated computations. Its meaning is the sequential map; more workers may change only wall time. |
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

A `scope` is allowed only inside a function body: a function, a method, or a lambda. In a top-level statement or in an initializer it is an error at the `scope` word, "a scope block is only allowed inside a function body", because globals are still being initialized there (section 18). A trap inside a scope does not wait for its tasks: it ends the run (section 4.2).

**`spawn call`** is an expression. Its operand is a call that begins with a name: `fetch_user(id)`, `client.fetch(id)`, `Jobs.twice(21)`, or `handler(request)` for a function value. A lambda is bound to a name first. The call must be a call of a function, a method, or a function value: `spawn Shape.Circle(2)`, which constructs an enum variant, is an error. The `spawn` must be lexically inside a `scope` of the same function body, so a `spawn` in a lambda written inside the scope does not qualify.

1. The parent evaluates the callee expression, including a method's receiver, and then the arguments from left to right, exactly as for the call itself ([Core language](CoreLanguage.md#functions-and-control-flow)). Overload resolution, casts, and variadic packing happen here.
2. The runtime creates a task for the call, appends it to the runnable queue, and returns a `Task<T>` to the parent, where `T` is the call's result type. A `void` call gives `Task<Unit>`, with `Unit` from `Std.Core` and the single value `Unit.Value`. The task starts from the values of step 1: lowering turns the callee, the receiver, and the arguments into the captures of a function value that takes no arguments and makes the call, and the task runs that value.
3. The parent continues. `spawn` is not a scheduling point; the child first runs when the parent next reaches one.

A `spawn` used as a statement, as in `spawn say("child");`, produces no handle. Nothing can join that task, so the runtime releases its record when it finishes instead of keeping its result until the scope closes. The scope still waits for it.

**`task.join()`** waits until the task has finished and returns its result. Joining again returns the same value. Only the task that executes the scope can join the scope's tasks, because the handle cannot be given to anyone else; the runtime also checks it, and a task identity that is not a live child of the running task is an invalid task operation ([NVR](../runtime/NVR.md#scheduler-operations-15)). `Task<T>` also has `is_finished()`, which never waits. A scope may exit without joining a task; its result is then discarded.

Because scopes are lexical, **no task outlives `main`**: when `main` returns, every scope it entered has already waited for its tasks. A background worker that should run for the life of a service is spawned in a scope near the top of `main`, where shutdown can cancel it and wait for it.

### 4.2 Errors and traps

Nothing in [Errors](Errors.md) changes.

- A task's function returns its failures as values. If `fetch_user` returns `Result<User>`, then `user_task.join()` is that `Result<User>`, and the caller reads it with `match`.
- A trap in any task ends the run at once, with that trap's status. No other task runs afterwards, no cleanup runs, and completed external effects stay visible. The diagnostic names the task, its spawn site, and the spawn chain up to the root task, as in `(task 2 spawned at f.nra:10:21 by task 1 spawned at f.nra:18:24 by task 0)`.
- `Std.Program.Process.exit` from any task ends the run with its status, abandoning every task.

Policies that combine results are named helpers in `Std.Concurrent.Tasks` (in progress), not language rules: `all_ok` cancels the remaining calls at the first `Err`, and `first_ok` returns the first `Ok` and cancels the rest. Each helper opens its own scope, so it has cancelled *and drained* every call it started before it returns.

### 4.3 Scope-bound values

A `Task<T>` or `Scope` must not be reachable after its scope exits. The rule is by type: a type is scope-bound if it is `Task<T>`, `Scope`, or an array, list, or tuple with a scope-bound element. A scope-bound value may be:

- held in a local binding declared inside the scope, including a local list, for a fan-out of unknown width;
- passed as an argument to a call, which returns before the scope can exit.

It may not be returned from a function, stored in a field, global, or enum payload, captured by a lambda, passed to `spawn`, sent on a channel, used as a type argument, or assigned to a binding declared outside the scope.

The implementation makes these rules precise:

- A scope-bound type is `Task<T>`, `Scope`, or an array, list, or tuple with a scope-bound element. It may not be a type argument, so `Optional<Task<int>>`, a channel of tasks, and `Cell<Task<int>>` are errors. It may not be a field or global type, an enum payload type, or the result type of a spawned call (a task cannot return a handle).
- A scope-bound value may be stored only in a local declared inside the innermost enclosing scope: an assignment to that local or to an element of it, or an `append` to a list held in it. A parameter is not such a local, so a function that receives a list of tasks cannot append to it, and a nested scope cannot store into a local of an outer scope.
- A scope-bound value may be an argument of an ordinary call, and a parameter may have a scope-bound type.
- The task and scope handles `Task<T>` and `Scope` are recognized by the trusted library file that declares them (`std/concurrent/task.nra` and `std/concurrent/scope.nra`).

Each error is reported at the escaping expression, in the form `a value of type Task<int32> cannot leave its scope: it cannot be returned from a function`. The reasons are "it cannot be returned from a function", "it cannot be stored in a field or global", "it cannot be an enum payload", "it cannot be a type argument", "a lambda cannot capture it", "it cannot be passed to spawn", "a task cannot return it", and "it can be stored only in a local declared inside that scope".

### 4.4 Grammar and rejected programs

The parser recognizes the contextual words only in these positions:

| Form | Recognized when | Why existing code is unaffected |
| --- | --- | --- |
| `scope {` | `scope` begins a statement and the next token is `{` | A bare name followed by `{` is not a statement today. Enum constructors with named payloads are always qualified (`Tree.Branch { ... }`). |
| `scope (name) {` | `scope` begins a statement followed by `(`, one identifier, `)`, and `{` | A call statement `scope(name);` is followed by `;`, never `{`. |
| `spawn name...` | `spawn` begins an expression and the next token is an identifier | Two adjacent identifiers are not an expression today. `spawn(x)` remains a call of a function named `spawn`; so does `spawn (f)(x)`, unless no function named `spawn` is in scope, in which case lowering reports the rejection below. |
| `select {` | `select` begins a statement and the next token is `{` | As for `scope {`. |

In every other position the three words are ordinary names, so a program that declares `let scope = 3;` or a module member `select` keeps its meaning. After `spawn` the parser reads one postfix expression and requires it to be a call: `spawn work;` is rejected with "spawn requires a call, such as 'spawn work(item)'". A `select` arm separator is a comma, and a trailing comma is allowed.

Representative rejections. Each is reported at the named span with both the rule and the offending type or effect. The checks do not depend on the command: `check` and `build` report the same diagnostic.

| Program | Diagnostic at |
| --- | --- |
| `spawn work(x)` outside any `scope` of the same function body, or inside a lambda nested in the scope | The `spawn` expression |
| `spawn update_cache(cache)` where `cache` is a `HashMap` | The argument `cache`: its type is not transferable (section 5.1) |
| `spawn audit(entry)` where `audit` writes a module global through two calls | The `spawn` expression, with the call chain to the write (section 5.2) |
| A `select` with only a `_` arm, or with two `_` arms | The `select` (it needs at least one arm that waits), or the second `_` arm (a select has at most one) |
| `return user_task;` or `tasks_field = user_task;` | The escaping expression (section 4.3) |
| `spawn (pick())(x)` | The `(`: a spawn operand begins with a name; bind the callee to a name first |
| `scope { ... }` in a top-level statement or an initializer | The `scope` word: a scope block is only allowed inside a function body |
| A program that spawns a task and imports `Std.DynamicForeign` | The `spawn` expression that makes the image concurrent (C15) |

## 5. Isolation between tasks

A task's mutable state is its own. Objects, arrays, and lists that a task creates stay with it; another task can affect them only by sending it a message. This holds on one scheduler and stays true when section 12 adds cores.

### 5.1 Transferable types

A value given to another task through a spawn (the callee value, a receiver, and each argument) or a channel must have a transferable type:

| Transferable | Not transferable |
| --- | --- |
| `bool`, integers, floats, fieldless enums | Class instances, except the `Std.Concurrent` handles; this includes library collections and immutable classes |
| `string`, and immutable bytes where a library exposes them | Arrays and lists, whose elements can be assigned, so a variadic call whose arguments are packed into an array cannot be spawned |
| A tuple whose elements are transferable | Primitive references (`&int`) |
| A payload enum all of whose variants carry transferable payloads | `MemorySegment`, arenas, and every other native handle |
| A function value, when every closure of its type that the program creates has transferable captures | A function value for which some closure of its type captures an object, array, or list |
| The handles of `Std.Concurrent`: channel endpoints, `Timer`, `AtomicInt64`, `Semaphore`, `Guarded<T>`, and the other classes declared in `std/concurrent/` other than `Task` and `Scope` | `Task<T>` and `Scope` (scope-bound, section 4.3) |

The check is recursive and on static types, and it runs on the specialized program: a generic function is checked at each specialization, and a type that reaches itself is transferable when its other parts are. A named function has no captures, and a closure is transferable when its captures are. The rule for a function value is the union rule of section 5.2 applied to values. Copying a closure does not copy what it captured: a closure that captures an object aliases that object ([Core language](CoreLanguage.md#function-values-and-closures)), which is why such a closure is not transferable.

A class instance is transferable only if its class is a `Std.Concurrent` handle whose fields are transferable. Decision: immutable classes are not transferable in this release (section 18). The checks apply at a spawn to the callee value, the receiver, and each argument, each reported at the expression it came from: `a value of type HashMap<string, string> is not transferable, so it cannot be given to a spawned task`. The same check applies to a channel's element type, to a value sent on a channel or in a `select` send arm, and to the arguments and result of a parallel map item.

`Std.Core.Error` is not transferable as declared, because its `Custom` variant holds an `ErrorDetail` object ([Stdlib](Stdlib.md#63-error)). A `Result<T>` therefore crosses a join (section 5.3) but not a channel; section 18 lists the choice that would let it.

### 5.2 Task-safe calls

A value check alone is not enough: a function can reach shared mutable state without being handed it. A spawned call must be task-safe, meaning that neither it nor anything it can call, transitively:

- reads or writes a module, file, or static binding that is reassignable or whose type is not transferable. Reading a `const` global of transferable type is allowed;
- makes an active use that could run a [static initializer](Types.md#static-initialization). A class with no static fields that need run-time initialization, and whose bases and interfaces have none, is safe to construct (C16). The guard function that first-use initialization calls records that it started in a reassignable binding of the class's static scope, so a spawned call that reaches a guard is rejected through the first rule, with the binding `$static_started`;
- issues a `neura.ffi.*` request (C15).

The compiler computes one effect summary per function over the whole linked program, including [prebuilt NeuraIR modules](Modules.md#prebuilt-neurair-modules), and checks it at each `spawn`. The summary of a function is its own direct reasons together with those of everything it can call: the callee of a call, every override of a virtual call, every constructor, field initializer, and destructor an allocation or `destroy` can run. For a virtual call or a call through a function value, the summary is the union over every override or function value of that type the program can create. That is conservative: a call rejected for an implementation it never reaches is fixed by narrowing the type or passing the needed value explicitly. The diagnostic lists the call chain from the `spawn` to the offending operation:

```text
f.nra:11:13: error: the spawned call is not task-safe: it reaches 'audit' (called at f.nra:11:19), and it reaches 'bump' (called at f.nra:6:32), and that code uses the reassignable binding 'counter' at f.nra:5:19
```

This is the message for a program that declares `let counter: int = 0;`, `let bump = func { counter = counter + 1; };`, and `let audit = func(entry: int) { bump(); };` and spawns `audit(3)`.

When the rule is one that a library operation enforces, for example a transferable type that a library function requires, the diagnostic is reported at the program's own call and not inside the library.

`neura check --task-safety` prints the summary of every function of the linked program: whether it is task-safe, the reason when it is not, and whether it could also be the worker of a parallel map (section 12). It never rejects a program for what it reports.

The root task is not restricted. It may use mutable globals as today, because no other task can reach them. A spawned call can read a `const` global whose type is transferable, and can print to the console: `Console.print_line` is task-safe, while reading standard input (`Console.read_line`) is not, because the console keeps its input buffer in a reassignable binding.

Each `Std` module's page must state whether its functions are task-safe; the `--task-safety` report is the list the compiler uses.

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

`open<T>(capacity)` returns a `Sender<T>`, a `Receiver<T>`, and a `Closer<T>`. `T` must be transferable; the compiler rejects an element type that is not (`Result<int>`, for example, because `Std.Core.Error` is not transferable, section 5.1). A coordinator keeps the closer and hands out the endpoints, so a worker that only receives cannot send or close. All three are transferable handles. A channel is charged to the heap budget when it is opened (section 10.3).

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
| `sender.send(value)` | `SendOutcome`: `Sent`, `Closed`, or `Cancelled` |
| `receiver.receive()` | `Received<T>`: `Value(T)`, `Closed`, or `Cancelled` |
| `sender.try_send(value)` | `TrySendOutcome`: `Sent`, `Full`, or `Closed`; never parks |
| `receiver.try_receive()` | `TryReceived<T>`: `Value(T)`, `Empty`, or `Closed`; never parks |

A `send` or `receive` that can complete does so even when the task's context is cancelled; cancellation is reported only by an operation that would otherwise park.

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

- An arm is `[let name =] operation => { block }` (`const` instead of `let` makes the binding constant), or `_ => { block }`, the default arm, which may appear once and anywhere among the arms. The operation is a call of `send` (with one argument), `receive`, `join`, or `wait` (with none), without type arguments, on an object whose class provides `select_handle()` and a `<member>_selected(code)` function: `Sender`, `Receiver`, `Task`, and `Timer` do. The binding receives what `<member>_selected` returns for the committed outcome, which is what the member would have returned on its own; the name `_` discards it, and an operation with no result cannot be bound. A select needs at least one arm that waits. It is allowed in any function body and does not need an enclosing `scope`.
- Receivers and arguments of every arm are evaluated once, in source order, before anything is attempted. The value of an unselected `send` is evaluated and then dropped.
- Exactly one operation commits, and only its block runs. No other arm has any effect.
- **Rotating preference.** Each task counts the selects it has committed, whichever arm committed, the default arm included. A select with `n` listed arms starts its scan at arm `count mod n` and proceeds cyclically in source order; the first ready arm commits. Selection is thus reproducible, and an arm that is always ready cannot starve the others. A send arm is ready when the send would complete, and committing it performs the send.
- If no arm is ready, the `_` arm runs when present. Otherwise the task parks on every arm, and the first operation another task or the runtime completes for it is the one that commits. The select is then a scheduling point.
- The order of preference is: a ready arm, then the `_` arm, then cancellation. A select whose task is cancelled while parked, or that finds its context already cancelled with nothing ready and no `_` arm, commits `Cancelled` on an arm that is not a `join` arm (a channel or timer arm), chosen by the same rotation. A `join` arm is never cancelled (C9): a select that has only `join` arms keeps waiting.
- The result of the scheduler operation is one value: the committed arm's index in the low half and its outcome in the high half. The default arm's index is the number of listed arms. Lowering turns it into a dispatch over the arms ([NVR](../runtime/NVR.md#scheduler-operations-15)).

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
- `cancel_after(duration)` and `cancel_at(instant)` set a deadline on the run's monotonic clock; a later call replaces the scope's own deadline. Both need the clock capability and return `Result<Unit>`; without it they return the clock's `Err` (`CapabilityDenied`) and set nothing. Under virtual time (section 14.4, in progress) they will need no capability. `Scope.deadline()` returns the monotonic reading at which the scope's tasks are cancelled, as an `Optional<Duration>`: the earlier of the scope's own deadline and the one it inherited, or `None`.
- A request wakes every parked channel operation, every parked timer wait, and every parked cancellable host request (section 10.4, in progress) of the affected tasks. The channel operation returns `Cancelled`, and so does a timer wait. The host operation returns an `Io` error of the new kind `Cancelled`.
- A running task is never interrupted. A CPU loop asks: `Std.Concurrent.Cancellation.requested()` returns whether the current task's context is cancelled, and `remaining()` returns the time to its deadline, if it has one, negative once it has passed. The root task's context is never cancelled, and cancelling a scope does not cancel the task that runs the scope, only the tasks spawned below it.
- A deadline passes when the scheduler next makes a decision at or after its time: when the running task parks, finishes, or polls out its quantum while another task can run, or when every task is parked and the scheduler waits for the clock. A task that is the only runnable task and never parks does not give the scheduler such a decision, so its scope's deadline does not interrupt it. A request made with `cancel()` by a task that is running takes effect at once.
- A task whose cancellation was requested before it first runs still starts, and sees the request on entry.
- `join()` is not cancellable. It always waits for the child's actual result, so a timed-out request can cancel its work and still drain it before releasing what the work was using.
- **One commit point.** Cancellation and completion compete inside the runtime, and exactly one wins. If a send or a host write committed before the cancellation was processed, the operation reports success and the effect stays visible. A cancelled write reports how many bytes were written first.

**Resources (in progress).** The scoped resource helpers below are not available at this commit: the standard library page describes them when they land. A resource still has to be closed by the program, and the paragraph in [Resources and concurrency](ResourcesConcurrency.md) stays accurate. The language has no destructors that run at scope exit, and this plan adds none ([Object lifecycle](ObjectLifecycle.md), [Stdlib](Stdlib.md#55-resources)). Concurrent code gets a cleanup path from scoped library helpers instead:

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

Three synchronized handles are implemented, each transferable:

| Handle | Contract |
| --- | --- |
| `AtomicInt64` | `create(initial)`, `load`, `store`, `add`, `compare_exchange`. Sequentially consistent: all operations on one atomic form a single order consistent with each task's program order. `add` returns the sum and is checked like any `int64` addition: it traps on overflow, leaving the value unchanged. |
| `Semaphore` | `create(permits)`; `acquire()` parks until a permit is free and returns `Acquired` or `Cancelled`; `try_acquire()` returns a `bool` and never parks; `release()` wakes the longest-waiting task. A semaphore is a channel of `bool` permits, so waiting tasks are served first come, first served and a wait can be cancelled. Releasing more permits than were created traps with `invalid state: released more permits than were created`; a negative count traps with `invalid argument: permits`. |
| `Guarded<T>` | `create<T>(initializer)` runs `initializer` in the calling task and wraps the state it builds, whose captures must be transferable; the state is reachable only through the guard. `with<U>(func(state: T) -> U) -> U` runs the function with exclusive access. The function's captures and `U` must be transferable, so no reference to the state can escape. A second task calling `with` parks until the first returns, first come, first served, and that wait is not cancellable because `with` has no outcome for it. Calling `with` on a guard the task already holds is the program trap `invalid state: guard already held`, status 134. |

Concurrent collections come after these, as needed, built on `Guarded<T>` or as owner tasks; none exists yet. General shared mutable objects wait for the ownership and aliasing decisions listed under [Lifecycle](Status.md#open-language-decisions).

## 9. `Std.Concurrent`

One module per type, as in the rest of the [library](Stdlib.md#42-source-layout). `Std.Concurrent` sits after `Std.Time` in the dependency order and imports only `Std.Core` and `Std.Time`.

| Module | Contents | Phase | Status |
| --- | --- | --- | --- |
| `Task` | `Task<T>` with `join()` and `is_finished()` | 1 | Implemented |
| `Scope` | `Scope` with `cancel()`, `is_cancelled()`, `cancel_after`, `cancel_at`, `deadline()` | 2 | Implemented |
| `Channel` | `open<T>`, `Sender<T>`, `Receiver<T>`, `Closer<T>`, and the outcome enums | 2 | Implemented |
| `Cancellation` | `requested()`, `remaining()` for the current task | 2 | Implemented |
| `Timer` | `after(duration)`, `at(instant)`; `wait()` is selectable | 2 | Implemented |
| `Tasks` | `all_ok`, `first_ok`, `all` | 3 | In progress |
| `WorkerGroup` | A fixed number of workers over a bounded queue: `submit` parks when the queue is full, `try_submit` reports `Full`, `close_and_drain()` finishes queued work | 3 | In progress |
| `Parallel` | `map<T, U>(items: T[], worker, limits) -> U[]`, `limits(steps, heap_cells)`, and `ItemLimits` (section 12) | 4 | Implemented |
| `AtomicInt64`, `Semaphore`, `Guarded` | Section 8 | 4 | Implemented |

`WorkerGroup` (in progress) is the answer to overload: a service bounds its in-flight work with a group and a queue long before it reaches the run-wide limits of section 10.3.

## 10. Runtime

### 10.1 What stays in the circuit and what crosses the boundary

Neuron graphs keep computing every arithmetic result, comparison, and branch decision, including which `match` arm handles an operation's outcome. Graph profile 1 does not change, and a task body is ordinary compiled blocks.

The scheduler is NVM state, like the heap: task records, the runnable queue, channel buffers and waiter queues, cancellation signals, timers, and pending host requests. A block reaches it through a typed scheduler-operation exit with resolved arguments and a resume block, the same shape as a [state operation](../runtime/NVR.md#state-operations). Host I/O still crosses the runtime boundary only as host requests with declared signatures. The scheduler is not a host service: it has no import, needs no capability, and is fully determined by the image, the inputs, the limits, and the host completion trace.

### 10.2 The reference scheduler

One scheduler runs one task at a time.

- **Queues.** The runnable queue is FIFO. A newly spawned task and a newly woken task are appended. Every waiter queue (a channel's senders and receivers, a semaphore, a guard, a task's joiners) is FIFO.
- **Scheduling points.** The running task changes only when it parks (a `join` of an unfinished task, a `send` to a full channel, a `receive` from an empty one, a `select` with nothing ready, a wait for a permit or a guard, a timer wait, a scope exit with live tasks, and, in progress, an overlapping host request), when it finishes, or at a poll that finds its quantum spent and another task runnable. A poll that finds nothing else runnable restarts its count and the task continues.
- **Polls.** A program that contains a `spawn` gets a poll on every `Call`, every `VirtualCall`, every call through a function value, and every loop back edge. The flag is a property of the whole program: a module compiled alone carries none, and gains them when it is linked into a program that spawns. A poll counts one language operation; when a task has counted its quantum since it was last scheduled, it goes to the back of the runnable queue. The quantum is a run setting (`--task-quantum`), default 1,024, and the quantum of a native executable is the default. A trace will record it (section 11, in progress).
- **Why polls count language operations.** Steps and blocks depend on how conversion shaped the graphs. Loop iterations and calls do not. Optimized and unoptimized builds of one program therefore execute the same polls in the same order: a poll is a flag on the call or loop it counts, and no pass removes an executed call or back edge, duplicates one, or moves one across a scheduler operation ([NeuraIR optimization](../IrOptimization.md#tasks-polls-and-scheduler-operations)).
- **Identity.** The root task is task 0. Tasks are numbered in spawn order. Channel and timer identities are numbered in creation order. A dump or trace names the same things in every run of the same trace.
- **Garbage collection.** The root set gains every task's frames, parked or runnable, every buffered message, every value held by a parked sender, every unjoined result, and every pending host request's arguments ([NVM](../runtime/NVM.md#garbage-collection)). Collection remains invisible to the program.

### 10.3 Limits

Existing limits keep their meaning for the run as a whole: steps and heap cells are summed over tasks. `--max-frames` bounds each task's depth. New run-wide limits, each a trap at the operation that would exceed it, checked before any state changes:

| Option | Bounds | Default | Exit status when exceeded |
| --- | --- | --- | --- |
| `--max-tasks` | Live tasks | 65,536 | 137 |
| `--max-queued-messages` | Values buffered in all channels | 1,048,576 | 137 |
| `--max-pending-requests` | Host requests issued and not yet delivered | 4,096 | 137 |
| `--task-quantum` | Polls per turn (section 10.2); a setting, not a limit | 1,024 | |

The defaults are confirmed against the memory a parked task actually costs by the measurement below. `--max-pending-requests` is accepted and the scheduler enforces it, but no service is overlapping yet, so nothing can reach it (section 10.4).

**Frame storage (decided for phase 1, runtime).** In an image that carries a `TRNS` table, every live frame of every task counts toward `--max-heap-cells`: one cell for the frame and one for each of its slots, checked before the frame is pushed and released when it returns. The scheduler charges the records it keeps in the same budget: a task record 20 cells, released when the scope that owns it closes (at once when the task has no handle); an open scope 1 cell; a channel 16 cells and one more for each buffered message; a timer 8; a guard 8; and a parallel-map item 8 plus the cells of its argument (section 12). The charge for a spawn, a channel, a timer, and a guard is checked before anything changes, so exhausting the budget traps at the operation that would exceed it with status 137. `--max-frames` still bounds each task's own depth. The reason is the conservative rule for open decisions: without a run-wide bound, up to `--max-tasks` times `--max-frames` frames could be live, so the runtime adds a bound instead of leaving one out, and it reuses an existing limit instead of adding an option this plan does not name. Images without a `TRNS` table keep their existing accounting, so their limits, traps, and `peak_heap_cells` statistics do not change. Fleet admission counts these cells as part of a member's live heap.

**Measured default.** The fixture `tests/backend/task-chain.nvra` is a chain in which every task opens a scope, spawns one child, and parks at the scope exit until `--max-tasks` ends the run. On Linux x86-64 with a Release build, the peak resident memory of `neura run --max-tasks N` (median of three runs, read from `getrusage` of the child process, including the trap diagnostic that ends the run) was 11.4 MiB at N = 1 and 38.5 MiB at N = 65,536: about 27 MiB more for 65,535 parked tasks, an average of 0.43 KB each and about 0.55 KB each between 16,384 and 65,536 tasks. The defaults are kept.

### 10.4 Host I/O

**In progress.** At this commit the reference executor waits for each host call before it resumes, for every service: the runner declares none overlapping, so a blocking call such as `neura.clock.sleep` or a network read stops every task. The scheduler implements the mechanism described below: the `Poll` and `Waiting` events, delivery of a completion, a cancel request, and the pending-request limit. The `Poll` and `Waiting` events serve timers and deadlines now, because the runner reads the monotonic clock when the scheduler asks (section 7). The delivery, cancel, and limit paths are not exercised by any provider or test yet.

With tasks, a provider may declare a service **overlapping**: the calling task parks, other tasks run, and the completion is delivered later.

- A request carries its execution-local sequence number, as now. That number is its ID.
- A live run delivers completions only at scheduling points, in the order the provider reported them, appending each woken task to the runnable queue. If no task is runnable and requests are pending, the scheduler blocks until one completes.
- Services that are not declared overlapping complete before the scheduler continues, exactly as today. Console writes stay in this class, so output order is the order tasks ran.
- First overlapping set: `neura.net.connect`, `accept`, `read`, and `write`, `neura.console.read`, and `neura.clock.sleep`. File operations stay synchronous at first.
- **Cancellable requests.** The cancellable form of a service is a new interface version; version 1 keeps its contract. Its provider must answer a cancel request with exactly one of: the completion, if the operation finished first, or error 15 (`Cancelled`), with any partial effect stated in the result (bytes written, for a write). The library maps code 15 to a new `IoErrorKind.Cancelled`.
- Each provider's page states which services overlap, which are cancellable, and what a cancelled operation may have done.

### 10.5 Deadlock

If no task is runnable, at least one is parked, no timer is armed, and no host request is pending, no event can ever wake a task. The run ends with `deadlock: all tasks are parked`, the wait chain (`wait chain: task 0 (parked: join of task 1) -> task 1 (parked: receive on channel 1 (0 of 1 queued, 0 senders waiting))`), a task dump (section 14.1), and status 134. If a host request is still pending, the run is waiting, not deadlocked; a dump taken then shows which request each task is waiting on.

## 11. Record and replay

**In progress.** Concurrent record and replay are not implemented. `nvm::record` rejects an image that has tasks, because the trace format cannot represent scheduling decisions, and `neura run --record` on such an image reports `recording tasks requires a concurrent trace version` and exits with status 1. A trace of an image without tasks records and replays as before. The rest of this section is the contract the new trace version must meet.

[Record and replay](../runtime/NVM.md#determinism-limits-and-observability) extend to concurrent runs. The contract: given the same image, inputs, limits, quantum, and trace, a replay reproduces every scheduling decision, exit selection, effect, and the terminal outcome, without invoking any service and therefore without repeating any external write.

A new trace version adds:

- the quantum and the scheduler limits in the header;
- for each host request, the task that issued it;
- for each overlapping request, the tick at which its completion was delivered, and whether it lost a race with cancellation;
- timer expirations, with their ticks;
- schedule choices that differ from FIFO, written only by schedule exploration (section 14.4);
- in the outcome, a deadlock as its own kind.

A replay that reaches a recorded tick with different scheduler state stops with status 1 and names the first difference, as a diverging request does today. A trace of a run with one task is unchanged, so existing traces keep replaying.

## 12. Multicore: `Parallel.map`

Tasks do not use more than one core. The first multicore feature is deliberately narrow:

```neura
let digests = parallel.map<Chunk, Digest>(chunks, hash_chunk, limits);
```

- **Meaning.** `map<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]` returns what calling `worker` on each item in index order would return. `T` and `U` are transferable.
- **Isolation, statically.** `worker` must be task-safe, issue no host request and no scheduler operation (it cannot spawn, open a scope, or use a channel, timer, or guard), and perform no identity operation (class hash or class text, whose numbers depend on request order). The compiler checks this from the same effect summaries as section 5.2, and reports a violation at the program's own call of `Parallel.map`: `the worker of a parallel map is not task-safe: ...` or `the worker of a parallel map must compute in isolation: ...`, each with the call chain. The arguments given to an item and its result must be transferable (section 5.1). `neura check --task-safety` marks which task-safe functions could be the worker.
- **Order.** `map` takes and returns arrays. Results are published in input order. If several items trap, the run reports the trap of the lowest index, which is the one the reference executor would have reached, and names it: `(item 2 of a parallel map)`, where the index counts from zero in the array.
- **Limits.** Each item runs under `ItemLimits` (steps and heap cells, both positive, made with `parallel.limits(steps, heap_cells)`), the same under any worker count. An item that exceeds one traps as that item, with status 152 for steps and 137 for heap cells. The run's totals add the steps of items in index order, so the run's own `--max-steps` applies to the sum.
- **Execution.** Each item runs to its end in a state of its own: its own heap, with copies of the program's globals whose type is transferable (an item may read only those that are `const`), and no scheduler. The reference executor evaluates items one after another on the scheduler's thread. With `--parallel-workers N`, where N is from 1 to 1,024 and the option applies to `run` and `test`, N threads evaluate items at once. Only wall time and peak process memory may differ; the values, the reported trap, and the statistics do not.

This is the same shape as a [fleet](GpuExecution.md#10-batched-running), many independent executions advanced together with results in member order, applied inside one run. It establishes what multicore Neura can deliver before any general parallel scheduler is designed. Benchmarks report I/O concurrency (tasks) and CPU speedup (`Parallel.map`) separately; neither figure stands in for the other.

## 13. Compiler stages and the artifact

| Stage | Change | Hands on |
| --- | --- | --- |
| C import | None. A program with C imports cannot spawn (C15). | |
| Lexer | None. `scope`, `spawn`, and `select` stay identifiers. | |
| Parser | The three contextual forms of section 4.4, with spans for the keyword, the operand, and each `select` arm. No typing or name resolution. | Syntax nodes for scope, spawn, and select |
| NeuraIR | Resolves `Task<T>` and `Scope`; checks scope-bound values during lowering and transferable types and task-safe calls over the specialized, linked program, including prebuilt `.nir` modules; represents scope entry and exit (`task_scope`), spawn (`spawn`), each other scheduler operation (`scheduler_call`: join, cancellation, channels, timers, guards, select, parallel items), and polls (a flag on a call or loop) as explicit operations. Written as NeuraIR 1.9 ([NeuraIR format](../NirFormat.md#revision-19-tasks)). | No operation after this point needs a source-level rule. Every spawn starts a function value of no arguments whose captures are transferable. |
| IR optimization | A task scope is a statement region; no pass removes a `spawn`, a scope, a scheduler call, an executed call, or a loop back edge, or moves one across another. The passes rewrite only operations that cannot be any of these ([NeuraIR optimization](../IrOptimization.md#tasks-polls-and-scheduler-operations)). | The same sequence of scheduler operations and polls as the unoptimized program |
| IRtoNeuronConversion | Task bodies become ordinary blocks. Each scheduler operation becomes a typed exit; `return`, `break`, and `continue` first leave the scopes they cross. A poll rides on the exit that takes the back edge or makes the call, so it adds no block. Marks the layouts of transferable classes, payload enums, and tuples in the required `TRNS` table. | Exits whose arguments and results are typed like state operations |
| Output | A new NVR minor version with the scheduler-operation exit and a required table marking transferable layouts. | An image a compatible runtime executes without source |

**Artifact and implementation status.** [NVR](../runtime/NVR.md#scheduler-operations-15) 1.5 is implemented: the scheduler exit with its 26 operations, the poll bit on continue and call exits, and the required `TRNS` table. The writer emits the lowest version required, so an image of a program without tasks keeps its earlier version and bytes. The loader validates the shape of each operation, the sorted and closed `TRNS` marks, and rejects a task image that contains `neura.ffi.*` imports, `CALB`, or `NATV`. The compiler emits all of it from source and from `.nir` 1.9 (`task_scope`, `spawn`, `scheduler_call`, and polls), and performs the scope-bound, transferability, and task-safety source checks of sections 4 and 5. The NVM runs the images on the reference scheduler of section 10.2, checks the layout of every object a spawn or a send transfers against `TRNS`, enforces the limits of section 10.3, and exposes task snapshots (`Execution::dump_tasks()`), events, and per-spawn-site profiles. Concurrent record and replay are in progress (section 11): `nvm::record` rejects task images. A native executable accepts `--max-tasks`, which can only lower the bound, through `NEURA_RUN_OPTIONS`, and rejects `--task-quantum` there because the quantum changes the interleaving and so the output order; the quantum of a native executable is the default 1,024. The fixed container policy of [Standalone](Standalone.md#5-the-container) cannot carry baked task settings yet, so the baked-settings half of section 14.3 is not met (in progress).

**Inspection.** `disassemble` prints scheduler operations as it prints state operations, with no source map needed, and `assemble` reads them back to the same bytes. `decompile` labels each scheduler operation and poll `[decoded]`, because it is read from the exit record and not inferred, and pairs scope entries and exits when every path agrees on the open scopes; where it cannot pair them it shows the operations and says so ([Decompiler](../Decompiler.md#tasks-and-scheduler-operations)). Behavior comparison of a recovered image that has tasks will use replay of a concurrent trace (in progress), which is an observed match under that trace, not a proof.

## 14. Tooling

### 14.1 Task dump

A task dump is a read-only snapshot taken at a scheduling point. It never changes the run and is not part of a trace.

```text
task 0  root  parked: join of task 1
    Task.Task<int32>.join at std/concurrent/task.nra:15:40
    Main.main at dead.nra:18:32
    start Main.main at dead.nra:13:5
  task 1  spawned at dead.nra:16:27  parked: receive on channel 1 (0 of 1 queued, 0 senders waiting)
      Channel.Receiver<int32>.receive at std/concurrent/channel.nra:74:53
      wait_forever at dead.nra:6:19
      $closure662.$invoke at dead.nra:16:33
```

This is the dump of a run that ended in a deadlock. Each child is indented below its parent, and a task's frames follow its line, innermost first. A spawned task's innermost caller is the generated `$invoke` method of the function value that `spawn` created.

It shows the parent and child tree, each task's spawn site and stack, its wait reason, queue depths and waiter counts for the channel it waits on, pending host requests, and, for a task whose context is cancelled, a `cancellation:` line with the tick of the request. The wait reasons are `running`, `runnable`, `parked: join of task N`, `parked: scope exit, waiting for N tasks`, `parked: send on channel N (...)`, `parked: receive on channel N (...)`, `parked: timer N`, `parked: select of ...`, `parked: host request N`, and `finished`. Without a source map it shows function and block numbers.

| Trigger | Output |
| --- | --- |
| Deadlock (section 10.5) | Always, to standard error, before the run ends |
| A trap | The trapping task and its spawn chain always, in the trap diagnostic; the full dump before it with `--task-dump` |
| `SIGQUIT` to `neura run` | To standard error; the run continues |
| `nvm::Execution::dump_tasks()` | For embedders |

`--task-dump=json` selects a JSON form with the same content for collectors: an object with a `tasks` array whose members have `id`, `parent`, `spawned_at`, `state`, and `stack`.

### 14.2 Profiling and trace events

- `neura run --profile` adds, per spawn site (and for the root task), the number of tasks started there, the steps they executed, and the scheduling decisions that passed while they were runnable and waiting for a turn, parked on a channel or timer, and parked on a host request. This answers whether a slow request was computing, waiting on I/O, or waiting for a turn.
- `neura run --trace-events <file>` writes one JSON object per line: `spawn`, `start`, `park`, `wake`, and `finish` of tasks; channel `send`, `receive`, and `close`; `host-request` and `host-completion`; `cancel`; `deadline`. Every event carries its `tick` and `task`, and the others add the child, channel, scope, timer, or request it concerns, a park's reason, and a spawn's site. Wall-clock times are added only with `--trace-wall-time`, because they differ between runs.
- `--stats` adds peak live tasks, peak queued messages, and peak pending requests.

Task dumps, profiles, trace events, and the task peaks of `--stats` are implemented.

### 14.3 Command line

`run` and `test` accept the limits of section 10.3, `--task-dump[=text|json]`, and `--parallel-workers`, and `run` accepts `--trace-events` and `--trace-wall-time`. Each is rejected on the other commands, and `check` accepts `--task-safety`. A [native executable](Standalone.md) takes the limits of section 10.3 through its baked policy and its options variable, which accepts only settings that restrict or observe a run. The quantum is not among them: a native executable runs with the default 1,024 until a container revision bakes task settings.

### 14.4 Testing concurrent code

**In progress.** None of the options below exists yet: `--virtual-time`, `--schedules`, `--schedule-seed`, `--save-schedule`, and `--schedule` are not accepted. The NVM exposes the mechanism they need to an embedder: a `ScheduleChooser` picks which runnable task goes next at each scheduling decision, and `RunLimits::schedule` records the task chosen at each tick. The reference scheduler is the chooser that always takes the first.

- **Virtual time.** `--virtual-time` on `run` and `test` replaces the clock provider with a virtual one. Time moves only when every task is parked, and then jumps to the earliest timer. A two-second timeout test takes no real time and is exact.
- **Schedule exploration.** `neura test --schedules N` runs each test under up to `N` different legal schedules, varying which runnable task goes next and where overlapping completions are delivered, from `--schedule-seed`. Every schedule obeys section 5.4, so a failure is a real bug in the program, not an artifact. Exploration is bounded and makes no claim about schedules it did not run; the report states how many ran.
- **Reproduction.** A failing schedule is written with `--save-schedule <file>` as an ordinary trace. `neura test --schedule <file>` and `neura run --replay <file>` reproduce it exactly.

## 15. Verification

Each row names its boundary, the CTest entry that establishes it, and the kind of evidence, as [Contributing](../../CONTRIBUTING.md#verify-behavior) asks. Run them with `ctest --test-dir build -R 'concurrency|ir-optimization-concurrency|backend-concurrency'`.

| Boundary | Check | Test | Kind |
| --- | --- | --- | --- |
| Parser | Each form accepted; each row of section 4.4 rejected at its span; programs using `scope`, `spawn`, and `select` as names unchanged | `concurrency-frontend` | Conformance |
| NeuraIR | Scope-bound, transfer, and effect checks, including through virtual calls and function values; the call chain in the diagnostic; `check` and `build` report the same diagnostic. Prebuilt `.nir` modules are checked as part of the linked program | `concurrency-frontend` | Conformance |
| IR optimization | The NeuraIR reference evaluator, extended with its own scheduler, runs every concurrent fixture before and after optimization under several quanta and task limits; results, traps, host effects, and the schedule, tick for tick, must agree. The structure check shows that every scheduler operation and poll the evaluator can reach survives, and that removing the polls changes the schedule | `ir-optimization-concurrency` | Differential |
| Conversion and NVM | Fixtures built from source (optimized, unoptimized, and through a `.nir` file) and run on the NVM must match the reference evaluator on host requests, exit status or trap class, and schedule; a second run from clean state repeats everything; fleet members give each its serial result; hand-assembled task images exercise each operation, and a handle used after its scope closed is an invalid task operation | `backend-concurrency`, `concurrency-programs` | Differential, determinism |
| Format | Hand-assembled 1.5 images; scheduler operations in an image that declares an earlier version, an unclosed transferable marking, and a concurrent image with `NATV` are rejected | `backend-format`, `backend-concurrency` | Structural |
| Channels, cancellation, and guards | Rendezvous, full buffers, close with parked senders, cancellation of parked operations, nested shutdown, the task, queue, and heap limits, collection with parked tasks | `backend-concurrency`, `concurrency-programs` | Runtime |
| Replay | Recorded runs replay with every capability denied; a perturbed trace reports the first difference | In progress | Replay |
| Decompiler | Scheduler operations and polls in images without `SMAP` and `NAME` | `decompiler-*` | Structural, observed |
| `Parallel.map` | One worker against several: values, the trapping item, item limits, and every statistic | `concurrency-parallel` | Differential |

These establish observed agreement on the fixtures and quanta they run. They are not proofs of equivalence, and the evaluator shares the language rules as written with the compiler.

## 16. Phases and release gates

A phase does not start until the gate of the one before it is met and reported with its commands and results.

| Phase | Delivers | Gate |
| --- | --- | --- |
| **1. Scoped tasks** | `scope { }`, `spawn`, `join`; the transfer and effect checks; the reference scheduler and polls; task limits; task dumps; NVR 1.5 and the `.nir` revision | Standalone images behave identically across repeated runs. Source checks explain invalid captures, unsafe calls, and escaping handles. Optimized and unoptimized builds agree on results, traps, effects, and schedule. |
| **2. Production I/O** | Bounded channels, `select`, `scope (name)` with cancellation and deadlines, overlapping and cancellable providers, the resource helpers, concurrent record and replay | Cancellation against completion, close with parked senders, nested shutdown, and collection with parked tasks are exercised. Replay passes with providers disabled. The artifact checks pass without source maps. |
| **3. Service tooling** | `WorkerGroup`, the `Tasks` helpers, virtual time, schedule exploration, profiling, and trace events | A service fixture sheds overload through a bounded queue, times out a dependency, drains on shutdown, and reproduces a failing interleaving from a saved trace. |
| **4. Multicore and shared state** | `Parallel.map`, then `AtomicInt64`, `Semaphore`, and `Guarded<T>`, then concurrent collections as needed | One worker and several agree on values, trap order, and limits. Throughput, tail latency, peak memory, and cancellation latency are published for named workloads, with the machine and baseline. |

**Current implementation status.** The status of each phase at this commit:

- **Phase 1 is implemented.** The three language forms, the scope-bound, transferable, and task-safe checks, polls, the reference scheduler, the task limits, task dumps (also `SIGQUIT` and `--task-dump`), NVR 1.5, and NeuraIR 1.9 exist, and so do the evidence rows of section 15 for them. Its gate rows are covered by `concurrency-frontend` (source checks that explain invalid captures, unsafe calls, and escaping handles), `ir-optimization-concurrency` and `backend-concurrency` (optimized and unoptimized builds agree), and `concurrency-programs` (the same output and status from repeated runs and from each build route); all five pass at this commit. The standalone-executable half of the first gate row is partial: a native executable runs the same image, but cannot yet carry baked task settings (in progress, section 14.3).
- **Phase 2 is partly implemented.** Implemented: bounded channels with typed outcomes, `select` with the default arm and rotating preference, `scope (name)` with `cancel`, `is_cancelled`, `cancel_after`, `cancel_at`, and `deadline`, `Cancellation`, `Timer`, deadlines and cancellation of parked channel, timer, and select operations, and deadlock detection with its diagnostic. In progress: overlapping and cancellable host I/O with its providers (section 10.4), concurrent record and replay (section 11), and the scoped resource helpers (section 7). Its gate is not met, because replay with providers disabled and the artifact-level cancellation races cannot be exercised yet.
- **Phase 3 is mostly in progress.** Implemented: profiling, trace events, and the task peaks of `--stats` (section 14.2). In progress: `WorkerGroup`, the `Tasks` helpers, virtual time, schedule exploration, and the saved-schedule workflow (section 14.4). Its gate is not met.
- **Phase 4 is implemented for the language and runtime and unpublished for measurements.** `Parallel.map` with `--parallel-workers`, `AtomicInt64`, `Semaphore`, and `Guarded<T>` exist, with `concurrency-parallel` showing that one worker and several agree on values, trap order, and limits. The published throughput, tail-latency, peak-memory, and cancellation-latency measurements are in progress, so the gate is not met. Concurrent collections do not exist.

The first public concurrency release is phases 1 through 3, and its acceptance test is one small network service taken all the way through **fan-out, backpressure, deadline, graceful shutdown, and replay**. [Concurrency example](../../examples/concurrency/service.nra) is the part of it that needs no network: state owned by a task behind a channel, a cancellable race that stops the slow backends, and shared state behind a guard. Phase 4 uses the isolated executor to establish what multicore Neura delivers before general-purpose threads are considered.

## 17. Documentation to update with the implementation

| Page | Change | State |
| --- | --- | --- |
| [Resources and concurrency](ResourcesConcurrency.md) | The deferral paragraph is replaced by a pointer here and to the example; the resource paragraph keeps the scoped helpers as described in the standard library page. | Done |
| [Core language](CoreLanguage.md), [Types](Types.md) | The three forms, scope-bound and transferable types, and the root-task rule for static initialization. | Done |
| [Errors](Errors.md) | Deadlock and the new limits in the status table; a trap in any task ends the run. | Done |
| [Stdlib](Stdlib.md), [Standard library](StandardLibrary.md) | `Std.Concurrent`; the task-safety of each module; `IoErrorKind.Cancelled`; the scoped helpers in section 5.5. | In progress |
| [NVM](../runtime/NVM.md) | The scheduler, overlapping requests, and the trace version. | In progress |
| [NVR](../runtime/NVR.md), [NeuraIR format](../NirFormat.md) | Minor version 5 with the scheduler operations, and the `.nir` revision 1.9. | Done |
| [Project](../Project.md), [Backend](../Backend.md), [NeuraIR optimization](../IrOptimization.md), [Decompiler](../Decompiler.md) | Each stage's contract from section 13. | Done |
| [Tooling](Tooling.md), [Testing](Testing.md) | The options of section 14 and the test modes. | In progress |
| [Status](Status.md), [Release](../Release.md) | Concurrency moves out of "Outside this release" phase by phase, each feature mapped to its tests. | In progress |
| An example under `examples/` | A cancellable operation and safe shared state, which [Resources and concurrency](ResourcesConcurrency.md) already asks for. | Done: `examples/concurrency/service.nra` |
| `wiki/` | The matching pages, following [wiki maintenance](../Wiki.md). | Pages that this change touches are updated; this page is not a wiki page |

## 18. Decisions and open questions

Each row names an area, what was decided or is still open, and the phase by which a decision is needed. Rows marked decided were settled with the implementation and are part of the specification; the others remain open.

| Area | Decision or open question | State |
| --- | --- | --- |
| Errors on channels | `Error.Custom` holds an object, so `Result<T>` cannot be sent. Either define immutable classes that are transferable, or give `Error` a transferable flattening. | Open, before 2 |
| Immutable classes | Decided: not transferable in this release. A class instance is transferable only if it is a `Std.Concurrent` handle (section 5.1). A class whose fields are all constant and transferable could be transferable later; it needs definite field initialization and a rule for `destroy` on a shared object ([Lifecycle](Status.md#open-language-decisions)). | Decided |
| Scope in initialization | Decided: no. `scope` is a function-body statement; in a top-level statement or an initializer it is an error, because globals are still being initialized. | Decided |
| Effect summary precision | The union rule for dynamic calls may reject too much real code. `check --task-safety` reports the summary of every function, which is how to measure it on the standard library before fixing the rule. | Open, before 2 |
| Frame storage | Decided: live frames across all tasks count toward `--max-heap-cells` in images with a `TRNS` table (section 10.3), as do the scheduler's records (20 cells for a task, 16 for a channel plus one per buffered message, 8 for a timer, 8 for a guard, 1 for an open scope). | Decided |
| Scope-bound values | Decided: a scope-bound type may not be a type argument. A scope-bound value may be stored only in a local declared inside the innermost enclosing scope; a parameter is not such a local (section 4.3). | Decided |
| Function values | Decided: a function value is transferable when every closure of its type that the program creates has transferable captures (section 5.1). | Decided |
| Variadic arguments | Decided: they are packed in the parent into an array, which is not transferable, so a variadic call with packed arguments cannot be spawned. | Decided |
| Polls | Decided: a program that contains a `spawn` gets a poll on every `Call`, `VirtualCall`, call through a function value, and loop back edge; the count is per language operation and identical for optimized and unoptimized builds (section 10.2). | Decided |
| Exit statuses | Decided: the task limit, queued-message limit, and pending-request limit end the run with 137; an invalid task operation with 132; a deadlock with 134 (section 10.3 and [Errors](Errors.md)). | Decided |
| Guard re-entry | Decided: `Guarded<T>.with` on a guard the task holds is the program trap `invalid state: guard already held`; a task parked in `with` is not cancellable. | Decided |
| `Parallel.map` | Decided: it takes and returns arrays, returns results in input order, reports the trap of the lowest index as `(item N of a parallel map)`, runs each item in its own heap with copies of the program's transferable globals, and takes its worker count from `--parallel-workers` (section 12). | Decided |
| Blocking file I/O | When file services become overlapping, and whether a long synchronous file call needs a watchdog. | Open, before 3 |
| Deadlines for a lone task | A scope's deadline does not interrupt a task that is the only runnable task and never parks (section 7). Whether the scheduler should consult the clock at polls, and what that costs, is open. | Open |
| Native interop | A concurrency contract for native calls, upcalls, and callbacks, lifting C15. | Open, after 4 |
| General parallel scheduler | Running tasks on several cores. It needs the ownership and aliasing rules that are open today. | Open, after 4 |
