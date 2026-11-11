# Concurrency

This page specifies the concurrency language and runtime and provides `scope`, `spawn`, and `select`, with the scope-bound, transferable, and task-safe checks, NeuraIR 1.9, polls, typed scheduler exits, NVR 1.5, the reference scheduler in NVM, cancellation and deadlines, channels, timers, overlapping and cancellable host I/O, concurrent record and replay, the scoped resource helpers, virtual time, schedule exploration with saved schedules, guards, monitors (classes whose `#Synchronized` methods tasks share), the isolated parallel map, and task settings recorded in native executables. `Std.Concurrent` has eleven modules: `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Tasks`, `WorkerGroup`, `Parallel`, `AtomicInt64`, `Semaphore`, and `Guarded`. Section 16 lists the limits: no concurrent collections, no tasks in a program that uses native bindings, and no scheduler that runs tasks on several cores. Tasks are tested on Linux; Windows is unverified. Section 13 gives each compiler stage's part. [Concurrency example](../../examples/concurrency/service.nra) is a complete program that uses tasks, a cancellable operation, and shared state.

## 1. How to read this page

- "Must" states a requirement on the implementation. Section 3 lists the rules in brief, and section 16 lists the limits of this release.
- Names of options, modules, and members are fixed here. The NVR operation codes and the exit encoding are final and defined in [NVR](../runtime/NVR.md#scheduler-operations-15); the `.nir` revision is defined in [NeuraIR format](../NirFormat.md#revision-19-tasks); the trace file of a run with tasks and the services that overlap are defined in [NVM](../runtime/NVM.md#traces-of-images-with-tasks).
- Code blocks marked `neura` compile. A fragment uses names that the program around it declares, such as `fetch_user` or `Job`; the fragments of sections 4 to 8 and 12 are compiled and run with stand-in declarations for those names. [Concurrency example](../../examples/concurrency/service.nra) is complete.

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

Neura gets **lightweight, scoped tasks**. `spawn fetch_user(id)` starts an ordinary function call as a task, as directly as Go's [`go fetchUser(id)`](https://go.dev/ref/spec#Go_statements), and a task is as cheap to hold as a [Java virtual thread](https://docs.oracle.com/en/java/javase/25/core/virtual-threads.html). A `scope` gives tasks a lifetime: no task outlives the block that started it. `Std.Concurrent` provides channels, cancellation, deadlines, timers, bounded worker groups, and shared-state handles. The reference NVM runs every task on one deterministic scheduler, so a run is reproducible, and two tools make that useful in production: a task dump that says what is stuck, and a replay that reproduces it.

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
| Thread pool, executor | `Std.Concurrent.WorkerGroup` |
| Mutex-protected object, `synchronized` method | A monitor: a class whose methods are `#Synchronized` (section 8.1); `Std.Concurrent.Guarded<T>`; or a task that owns the object |
| Thread dump | Task dump |

## 3. Rules in brief

The rules the rest of the page specifies, in one place. [Design notes](../design/Concurrency.md) record why each was chosen.

- Tasks, with one function style. There are no `async` functions and no `await`. A function called from a task may suspend at a channel, a join, or host I/O without any marking in its signature.
- Concurrency is structured. Every `spawn` is lexically inside a `scope` of the same function body, and every exit from that scope waits for its tasks and their descendants. There are no detached tasks.
- `scope`, `spawn`, and `select` are contextual. The lexer keeps producing identifier tokens; the parser recognizes the forms by position (section 4.4). Programs that use these words as names keep their meaning.
- `spawn` evaluates its callee and arguments in the parent, in the ordinary order, and returns `Std.Concurrent.Task<T>`.
- `Task<T>` and `Scope` values are scope-bound: they cannot leave the scope that created them (section 4.3).
- Tasks are isolated. Spawn arguments and channel messages must be transferable, and a spawned call must be task-safe. The objects tasks may share are the `Std.Concurrent` handles, instances of `#Immutable` classes, and monitors, whose `#Synchronized` methods exclude one another (section 8.1). The compiler checks both over the whole linked program: the scope-bound rules during lowering, and transferability and task safety on the specialized program, so a generic function is checked at each specialization (section 5).
- A join transfers the finished child's result to the joiner whatever its type. Join results are not restricted to transferable types; section 5.3 gives the reason.
- The error model does not change. Expected failures are returned values that `join` hands to the caller. A trap in any task ends the run. Nothing unwinds.
- Cancellation is cooperative and inherited through scopes. It wakes parked operations with a typed result. It never interrupts a running task, and `join` is never cancelled.
- Channels are typed and bounded, with separate send, receive, and close capabilities. There is no unbounded channel.
- The reference executor is one deterministic scheduler inside the NVM with FIFO queues. A program may rely on the memory model of section 5.4 and the channel rules of section 6. It must not rely on FIFO order, which exists to make runs reproducible.
- Preemption is by compiler-inserted polls that count language operations (loop back edges and calls), never steps, blocks, or time.
- Scheduler state is NVM state reached through explicit exits, like the heap. Graph profile 1 does not change. An image that uses tasks declares a new NVR minor version and is rejected by earlier runtimes.
- Host I/O may overlap only where the provider declares it. Completions are delivered at scheduling points and recorded by tick, so a replay delivers them at the same points (sections 10.4 and 11).
- The first concurrent image profile excludes native bindings and callbacks: a program cannot use both a scheduler operation (a scope, a spawn, a channel, a timer, a guard, a select, or a parallel map) and `Std.DynamicForeign` or `import:c(...)`. The compiler rejects the combination, at the first `spawn` when the program has one and otherwise at its first scheduler operation, and the NVR loader rejects any image that has scheduler exits or polls together with `CALB`, `NATV`, or a `neura.ffi.*` import.
- First-use static initialization stays on the root task. A spawned call that could run a static initializer is not task-safe (section 5.2).
- Multicore execution begins with `Parallel.map` over isolated computations. Its meaning is the sequential map; more workers may change only wall time.
- A deadlock ends the run with a diagnostic, a task dump, and status 134 (`Abort`).

## 4. Scoped tasks

### 4.1 `scope`, `spawn`, and `join`

<!-- example: fragment: calls fetch_user, fetch_orders, and build_response and returns a Page, none of which the page defines, and omits the result import -->
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

`scope (name) { ... }` also binds the scope's `Std.Concurrent.Scope` handle to a constant `name` visible in the block. The handle cancels the scope's tasks and sets their deadline. The form is allowed in a function with type parameters; each specialization has its own scope identity.

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

Policies that combine results are named helpers in `Std.Concurrent.Tasks`, not language rules. Each takes a list of calls, where a call is a function value of no arguments, and each opens its own scope, so it has cancelled *and drained* every call it started before it returns.

| Helper | Result |
| --- | --- |
| `all<T>(calls: (func() -> T)[list]) -> T[list]` | Runs every call as a task and returns their results in the order of the calls. It waits for all of them and cancels nothing. |
| `all_ok<T>(calls: (func() -> Result<T>)[list]) -> Result<T[list]>` | If every call returns `Ok`, the values in the order of the calls. Otherwise the first `Err` in the order the calls completed, after cancelling the calls still running and waiting for every one of them. |
| `first_ok<T>(calls: (func() -> Result<T>)[list]) -> Result<T>` | The first `Ok` in the order the calls completed, after cancelling the calls still running and waiting for every one of them. If every call fails, the `Err` of the call that completed last. |

<!-- example: fragment: calls fetch_price, which the page does not define, and omits the result and tasks imports -->
```neura
let quote = func -> result.Result<int[list]> {
    let calls: (func() -> result.Result<int>)[list] = [
        func -> result.Result<int> => fetch_price("tea"),
        func -> result.Result<int> => fetch_price("cake")
    ];
    return tasks.all_ok<int>(calls);
};
```

- The helpers take and return lists, not arrays. The language cannot build an array whose length is known only at run time, and a function with type parameters cannot be overloaded for arrays as well. A list literal writes the calls where the parameter's type is expected, and a loop builds them for a width known only at run time.
- A call must be task-safe and its captures transferable, as for any spawn (section 5). A violation is reported at the program's call of the helper (section 5.2).
- A cancelled call learns of it through `Cancellation.requested()` or through a parked operation that returns `Cancelled` (section 7). A call that never asks runs to its end, and the helper waits for it.
- With no calls, `all` returns an empty list, `all_ok` returns `Ok` of an empty list, and `first_ok` returns an `Err` whose code is `tasks.empty`.
- If the task that called `all_ok` or `first_ok` is cancelled while the helper waits, the helper cancels its calls the same way and waits for them. `all_ok` then returns an `Err` whose code is `tasks.cancelled`. `first_ok` returns the `Err` of the last call that had failed, or an `Err` with the code `tasks.cancelled` when none had.

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
| `scope {` | `scope` begins a statement and the next token is `{` | A bare name followed by `{` is not a statement in the grammar. Enum constructors with named payloads are always qualified (`Tree.Branch { ... }`). |
| `scope (name) {` | `scope` begins a statement followed by `(`, one identifier, `)`, and `{` | A call statement `scope(name);` is followed by `;`, never `{`. A call of `scope` with no argument or with several, followed by `{`, is not a statement either, and is reported as the rejection below. |
| `spawn name...` | `spawn` begins an expression and the next token is an identifier | Two adjacent identifiers are not an expression in the grammar. `spawn(x)` remains a call of a function named `spawn`; so does `spawn (f)(x)`, unless no function named `spawn` is in scope, in which case lowering reports the rejection below. |
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
| `scope (a, b) { ... }` or `scope () { ... }` | The `(`: `a scope statement names one scope or none: scope (name) { } or scope { }` |
| A program that spawns a task and imports `Std.DynamicForeign` or has a C import | The first `spawn` expression: `a program that spawns a task cannot use Std.DynamicForeign or import:c(...): tasks and native calls cannot share an image yet` |
| A program without a `spawn` that opens a scope or a channel, or uses another scheduler operation, and imports `Std.DynamicForeign` or has a C import | The first such operation, or the program's own call that reaches it when it is inside the library: `a program that uses a scope, a channel, or another scheduler operation cannot use Std.DynamicForeign or import:c(...): tasks and native calls cannot share an image yet` |

## 5. Isolation between tasks

A task's mutable state is its own. Objects, arrays, and lists that a task creates stay with it; another task can affect them only by sending it a message. This holds on one scheduler and stays true when section 12 adds cores.

### 5.1 Transferable types

A value given to another task through a spawn (the callee value, a receiver, and each argument) or a channel must have a transferable type:

| Transferable | Not transferable |
| --- | --- |
| `bool`, integers, floats, fieldless enums | Class instances, except the `Std.Concurrent` handles, `#Immutable` classes, and monitors; this includes library collections |
| `string`, and immutable bytes where a library exposes them | Arrays and lists, whose elements can be assigned, so a variadic call whose arguments are packed into an array cannot be spawned |
| A tuple whose elements are transferable | Primitive references (`&int`) |
| A payload enum all of whose variants carry transferable payloads | `MemorySegment`, arenas, and every other native handle |
| A function value, when every closure of its type that the program creates has transferable captures | A function value for which some closure of its type captures an object, array, or list |
| The handles of `Std.Concurrent`: channel endpoints, `Timer`, `AtomicInt64`, `Semaphore`, `Guarded<T>`, and the other classes declared in `std/concurrent/` other than `Task` and `Scope` | `Task<T>` and `Scope` (scope-bound, section 4.3) |
| An instance of an `#Immutable` class whose fields are transferable ([Types](Types.md#immutable-classes)) | An instance of a class marked `#NotThreadSafe` |
| An instance of a monitor (section 8.1) | |

The check is recursive and on static types, and it runs on the specialized program: a generic function is checked at each specialization, and a type that reaches itself is transferable when its other parts are. A named function has no captures, and a closure is transferable when its captures are. The rule for a function value is the union rule of section 5.2 applied to values. Copying a closure does not copy what it captured: a closure that captures an object aliases that object ([Core language](CoreLanguage.md#function-values-and-closures)), which is why such a closure is not transferable.

A class instance is transferable only if its class is a `Std.Concurrent` handle whose fields are transferable, an `#Immutable` class whose fields are transferable and whose subclasses are too, or a monitor (section 8.1), and the class is not marked `#NotThreadSafe`. Transferring such an instance shares it: the task receives a reference to the same object, which is safe because nothing in it changes or because its methods exclude one another. The arguments and the result of a parallel map item are copied into another heap instead (section 12), so an `#Immutable` instance may be one, and a monitor, which is shared through scheduler state, may not. When the class of a rejected value has `#Synchronized` methods, the diagnostic labels the member that keeps it from being a monitor, as in `'Counter' is not shareable: method 'peek' is exported and not synchronized`. The checks apply at a spawn to the callee value, the receiver, and each argument, each reported at the expression it came from: `a value of type HashMap<string, string> is not transferable, so it cannot be given to a spawned task`. The same check applies to a channel's element type, to a value sent on a channel or in a `select` send arm, and to the arguments and result of a parallel map item.

`Std.Core.Error` is not transferable as declared, because its `Custom` variant holds an `ErrorDetail` object ([Stdlib](Stdlib.md#63-error)). A `Result<T>` therefore crosses a join (section 5.3) but not a channel or a spawn. `Error` has a transferable flattening. `Std.Core.Error.PortableError` mirrors `Error` with one change: its `Custom` variant holds the detail's code and message texts in place of the object. `to_portable(e)` converts an error before it is sent and `from_portable(p)` converts it back after it is received:

<!-- example: check -->
```neura
let error = import:Std.Core.Error;
let channels = import:Std.Concurrent.Channel;

let report = func(failure: error.Error, failures: channels.Sender<error.PortableError>) {
    failures.send(error.to_portable(failure));
};
```

A `Custom` error comes back holding a `SimpleDetail` with the same code and message, so `message(from_portable(to_portable(e)))` equals `message(e)`; any other member of a subclass of `ErrorDetail` is lost. Every other variant is copied as it is.

### 5.2 Task-safe calls

A value check alone is not enough: a function can reach shared mutable state without being handed it. A spawned call must be task-safe, meaning that neither it nor anything it can call, transitively:

- reads or writes a module, file, or static binding that is reassignable or whose type is not transferable. Reading a `const` global of transferable type is allowed;
- makes an active use that could run a [static initializer](Types.md#static-initialization). A class with no static fields that need run-time initialization, and whose bases and interfaces have none, is safe to construct. The guard function that first-use initialization calls records that it started in a reassignable binding of the class's static scope, so a spawned call that reaches a guard is rejected through the first rule, with the binding `$static_started`;
- issues a `neura.ffi.*` request (section 16).

The compiler computes one effect summary per function over the whole linked program, including [prebuilt NeuraIR modules](Modules.md#prebuilt-neurair-modules), and checks it at each `spawn`. The summary of a function is its own direct reasons together with those of everything it can call: the callee of a call, every override of a virtual call, every constructor, field initializer, and destructor an allocation or `destroy` can run. For a virtual call or a call through a function value, the summary is the union over every override and over every function value of that type the program can create. A function value can be created only where code that can run allocates it: the program's own functions, initializers, and whatever those reach. A closure in a library function that nothing reaches is not counted. That is still conservative: a call rejected for an implementation it never reaches is fixed by narrowing the type or passing the needed value explicitly. The diagnostic names the function that breaks the rule and what it does, and labels the call chain from the `spawn` to the offending operation: each call on the way, and the operation itself. This is the one-line form that `--error-format=short` prints ([Tooling](Tooling.md#diagnostics)), then the default form:

```text
f.nra:11:13: error[E0601]: the spawned call is not task-safe: it reaches 'bump', which uses the reassignable binding 'counter'
```

```text
error[E0601]: the spawned call is not task-safe: it reaches 'bump', which uses the reassignable binding 'counter'
  --> f.nra:11:13
   |
 5 | let bump = func { counter = counter + 1; };
   |                   ------- uses the reassignable binding 'counter' here
 6 | let audit = func(entry: int) { bump(); };
   |                                ------ calls 'bump'
...
11 |             spawn audit(3);
   |             ^^^^^^^^^^^^^^ this call must be task-safe
   |                   -------- calls 'audit'
   |
   = note: `neura check --task-safety` reports for every function whether it is task-safe, and why not

For more information about this error, try `neura explain E0601`.
```

This is the diagnostic for a program that declares `let counter: int = 0;`, `let bump = func { counter = counter + 1; };`, and `let audit = func(entry: int) { bump(); };` and spawns `audit(3)` in its `main`.

When the rule is one that a library operation enforces, for example a transferable type that a library function requires, the diagnostic is reported at the program's own call and not inside the library. The same holds for a `spawn` that a library helper makes, such as the workers of `WorkerGroup.with_workers` or the calls of `Tasks.all`: the check is reported at the program's call into the library, and a label marks the place where the library starts the task. A function value that is not transferable has a label at the capture that makes it so. These are the messages for a `with_workers` whose `work` function writes the module binding `total`, and for one whose `work` is a lambda that captures an object of class `Box`:

```text
shop.nra:9:9: error[E0601]: the spawned call is not task-safe: it reaches 'add', which uses the reassignable binding 'total'
boxed.nra:13:9: error[E0602]: a value of type func(int32) -> void is not transferable, so it cannot be given to a spawned task
```

`neura check --task-safety` prints the summary of every function of the linked program: whether it is task-safe, the reason when it is not, and whether it could also be the worker of a parallel map (section 12). It never rejects a program for what it reports.

The root task is not restricted. It may use mutable globals as in a program without tasks, because no other task can reach them. A spawned call can read a `const` global whose type is transferable, and can print to the console: `Console.print_line` is task-safe, while reading standard input (`Console.read_line`, and the byte source that `Console.input()` returns) is not, because the console keeps its read-ahead buffer in a reassignable binding that belongs to the root task.

Each `Std` module's page must state whether its functions are task-safe; the `--task-safety` report is the list the compiler uses. [Standard library](StandardLibrary.md#task-safety) has the statement for each area, and the wiki page of each `Std.Concurrent` module, and of each module with an exception, has its own. The library was audited against this rule in two steps.

- **The pinned set.** The `concurrency-frontend` test runs the report over a program that links what a service usually needs: `Console`, `HashMap`, `ArrayList`, `TreeMap`, `Float64`, `Duration`, `SystemClock`, `Instant`, `Logger`, `TcpStream`, `TcpListener`, `Strings`, `Regex`, `Random`, and `Json` with its big numbers. It requires that the only functions listed as not task-safe are the console's input functions: `Console.read_line` and the two private functions behind `Console.input()`, `read_input` and `discard_pending`. A library function that joins the list fails the test. Two earlier sources of shared state are gone from this set: a collection's `to_string` keeps its self-containment flag in the instance ([Stdlib](Stdlib.md#52-equality-hashing-and-text)), and float parsing and the `BigInteger` and `BigDecimal` methods read no module state.
- **The whole library.** The same report over every module except `Std.DynamicForeign` lists more, and no test pins them: `Hex.decode` and the four `Base64` decoders, each of which keeps a lookup table in a reassignable module binding. (`Crc32` and `Sha256` left this list when they became kernel-backed; `tests/concurrency/parallel/map.nra` calls both from parallel items and a task.) It also shows the union rule at work: `Console.input()` returns a function value of the byte-source type whose body reads the root task's read-ahead buffer. In a program that calls `Console.input()`, the read functions of `BufferedReader` and `Utf8Reader`, which call a byte source, are therefore not task-safe whatever source they were given; in a program that never calls it they are, and `concurrency-frontend` pins both.

A constant that is an object is module state that a spawned task may not read. `BigInteger.Zero`, `One`, and `Ten`, `Duration.Zero`, `Instant.Epoch`, `LocalTime.Midnight`, and `ZoneOffset.Utc` are such constants; a task builds the value with the module's factory function, such as `of_int64` or `of_nanos`.

### 5.3 Results cross a join by transfer

When a task finishes, nothing else can reach what it built: it received only transferable values, it could not touch mutable globals, the scopes it opened have waited for its own children, and its frames are gone. Its result is therefore the only reference to that part of the heap, and handing it to the joiner shares nothing. So `join()` returns any `T`, including a list the child built or a `Result<T>` whose error holds a custom detail object. The handle cannot leave the scope's task, so a second `join()` gives the same reference to the same task, which is ordinary aliasing inside one task.

Join results are not restricted to transferable types: a restriction would make the example of section 4.1 fail to compile whenever `fetch_user` returns `Result<User>`, and no task could return a collection. `Parallel.map` results stay restricted to transferable types (section 12), because its workers may not share a heap.

### 5.4 Memory model

These are the ordering guarantees a program may rely on. An effect A is *visible to* B when:

- A is the evaluation of a spawn's callee and arguments, and B is anything the child does;
- A is anything a task does before it finishes, and B follows a `join` of that task;
- A precedes a send, and B follows the receive that obtains that message;
- A precedes `close`, and B follows an operation that reports `Closed`;
- A precedes a cancellation request, and B follows an observation of that request;
- A and B are operations on one `AtomicInt64`, which are totally ordered, each seeing the latest earlier store;
- A is inside one `Guarded<T>.with` call and B is inside a later one on the same guard;
- A is inside one call of a synchronized method on an instance, and B is inside a later call of a synchronized method on the same instance (section 8.1).

Isolation means there is nothing else to order: two tasks have no other common state. An `#Immutable` instance that two tasks share never changes after its constructor returns, and the constructor ran before the spawn or send that shared it. The reference scheduler's FIFO order is deterministic but is not part of this model (section 3).

## 6. Channels and `select`

**Channels are the default way to share work.**

<!-- example: fragment: one statement that opens a channel of Job values; Job and the channels import are not defined here -->
```neura
let channels = import:Std.Concurrent.Channel;

let [jobs, next_job, jobs_closer] = channels.open<Job>(64);
```

`open<T>(capacity)` returns a `Sender<T>`, a `Receiver<T>`, and a `Closer<T>`. `T` must be transferable; the compiler rejects an element type that is not (`Result<int>`, for example, because `Std.Core.Error` is not transferable; section 5.1 gives the transferable form of an error). A coordinator keeps the closer and hands out the endpoints, so a worker that only receives cannot send or close. All three are transferable handles. A channel is charged to the heap budget when it is opened (section 10.3).

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

<!-- example: fragment: a worker function that uses Job, Outcome, handle, and the channels import, none of which the page defines -->
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

<!-- example: fragment: a select statement, allowed only inside a function body; inbox, outbox, reply, slow, handle, note_sent, and logger are not defined -->
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

- An arm is `[let name =] operation => { block }` (`const` instead of `let` makes the binding constant), or `_ => { block }`, the default arm, which may appear once and anywhere among the arms. The operation is a call of `send` (with one argument), `receive`, `join`, or `wait` (with none), without type arguments, on an object whose class provides `select_handle()` and a `<member>_selected(code)` function: `Sender`, `Receiver`, `Task`, and `Timer` do. The binding receives what `<member>_selected` returns for the committed outcome, which is what the member would have returned on its own; the name `_` discards it, and an operation with no result cannot be bound. A send arm resolves `send` like any other call; no earlier plain `send` on that channel is needed. A select needs at least one arm that waits. It is allowed in any function body and does not need an enclosing `scope`.
- Receivers and arguments of every arm are evaluated once, in source order, before anything is attempted. The value of an unselected `send` is evaluated and then dropped.
- Exactly one operation commits, and only its block runs. No other arm has any effect.
- **Rotating preference.** Each task counts the selects it has committed, whichever arm committed, the default arm included. A select with `n` listed arms starts its scan at arm `count mod n` and proceeds cyclically in source order; the first ready arm commits. Selection is thus reproducible, and an arm that is always ready cannot starve the others. A send arm is ready when the send would complete, and committing it performs the send.
- If no arm is ready, the `_` arm runs when present. Otherwise the task parks on every arm, and the first operation another task or the runtime completes for it is the one that commits. The select is then a scheduling point.
- The order of preference is: a ready arm, then the `_` arm, then cancellation. A select whose task is cancelled while parked, or that finds its context already cancelled with nothing ready and no `_` arm, commits `Cancelled` on an arm that is not a `join` arm (a channel or timer arm), chosen by the same rotation. A `join` arm is never cancelled (C9): a select that has only `join` arms keeps waiting.
- The result of the scheduler operation is one value: the committed arm's index in the low half and its outcome in the high half. The default arm's index is the number of listed arms. Lowering turns it into a dispatch over the arms ([NVR](../runtime/NVR.md#scheduler-operations-15)).

## 7. Cancellation, deadlines, and resources

**Cancellation is cooperative and scoped.**

<!-- example: fragment: a scope statement without its function; request, call_backend, query, and durations are not defined -->
```neura
scope (request) {
    let _ = request.cancel_after(durations.of_seconds(2));
    let reply_task = spawn call_backend(query);
    return reply_task.join();  // Ok, or the backend's own Cancelled error.
}
```

- Every scope has a signal. `Scope.cancel()` requests cancellation of the scope's tasks and all their descendants. It is idempotent and cannot be undone.
- A child's context is its scope's signal joined with everything that scope's own task inherited. Cancelling an outer scope therefore cancels every task below it. A scope's deadline is the earlier of its own and its parent's; a nested scope cannot extend one.
- `cancel_after(duration)` and `cancel_at(instant)` set a deadline on the run's monotonic clock; a later call replaces the scope's own deadline. Both read the clock and return `Result<Unit>`, which is `#NoDiscard`: a statement that drops it warns, so a program that ignores a failure says so with `let _ = ...;`. The clock is granted by default; under `--deny-clock` they return the clock's `Err` (`CapabilityDenied`) and set nothing. Virtual time (section 14.4) changes what the clock reads, not who may read it, so the same holds there. `Scope.deadline()` returns the monotonic reading at which the scope's tasks are cancelled, as an `Optional<Duration>`: the earlier of the scope's own deadline and the one it inherited, or `None`.
- A request wakes every parked channel operation, every parked timer wait, and every parked cancellable host request (section 10.4) of the affected tasks. The channel operation returns `Cancelled`, and so does a timer wait. The host operation returns an `Io` error of kind `Cancelled` (`IoErrorKind.Cancelled`), unless it had already finished.
- A running task is never interrupted. A CPU loop asks: `Std.Concurrent.Cancellation.requested()` returns whether the current task's context is cancelled, and `remaining()` returns the time to its deadline, if it has one, negative once it has passed. The root task's context is never cancelled, and cancelling a scope does not cancel the task that runs the scope, only the tasks spawned below it.
- A deadline passes when the driver next reads the clock at or after its time. It reads the clock at every scheduling point while a timer or a deadline is armed: when the running task parks, finishes, or ends its turn, and when every task is parked and the scheduler waits for the clock. A task that never parks still ends its turn: when its quantum is spent and a deadline, a timer, or a host request is outstanding, the turn ends even if no other task can run, so a deadline stops a task that is the only runnable task and only computes (section 10.2). That task sees the request the next time it asks `Cancellation.requested()`. A request made with `cancel()` by a task that is running takes effect at once.
- A task whose cancellation was requested before it first runs still starts, and sees the request on entry.
- `join()` is not cancellable. It always waits for the child's actual result, so a timed-out request can cancel its work and still drain it before releasing what the work was using.
- **One commit point.** Cancellation and completion compete inside the runtime, and exactly one wins. If a send or a host write committed before the cancellation was processed, the operation reports success and the effect stays visible. A cancelled write reports how many bytes were written first.

**Resources.** The language has no destructors that run at scope exit, and this design adds none ([Object lifecycle](ObjectLifecycle.md), [Stdlib](Stdlib.md#55-resources)). Concurrent code gets a cleanup path from scoped library helpers instead:

<!-- example: fragment: one call of files.with_open without its imports, path, and summarize -->
```neura
let outcome = files.with_open<Summary>(path, files.OpenMode.Read, func(file: files.File) -> result.Result<Summary> {
    return summarize(file);
});
```

`File.with_open<T>(path, mode, body)` and `TcpStream.with_connection<T>(host, port, body)` open a resource around a body. Each helper:

- opens the resource, and returns the open error without calling the body if that fails;
- runs the body in a scope it owns, so every task the helper started that borrows the handle has finished when the body's scope exits;
- closes the resource exactly once on every ordinary completion: a returned value, a returned error, and a cooperative cancellation;
- reports both failures when both happen: a body `Err` with a failed close returns a `Context` error whose cause is the body's error and whose message carries the close error's message (`closing the resource also failed: ...` from `File.with_open`, `close failed after the body failed: ...` from `TcpStream.with_connection`). A body `Ok` with a failed close returns the close error.

`Std.Io.Closeable.release<T>(resource, outcome)` is that closing rule on its own, for a resource the two helpers do not cover: it closes the resource and combines the result with the outcome of the work, and a program calls it exactly once, after the work, on every ordinary completion. `File.with_open` is `open`, the body in a scope, and `release`. A file is closed even when the body closed it already, which reports no error, because a second `close` does nothing.

A trap still ends the run without cleanup, and the runner then closes every file and socket the run left open, as it does for any run. A body can only return or end the run, which is what makes the helper sufficient.

## 8. Shared state

**Start with task-local mutable state.** Most shared state is better owned by one task that serves requests over a channel:

<!-- example: fragment: cache_owner without the CacheRequest enum and the channels and hash_maps imports it uses -->
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

Three synchronized handles exist, each transferable:

| Handle | Contract |
| --- | --- |
| `AtomicInt64` | `create(initial)`, `load`, `store`, `add`, `compare_exchange`. Sequentially consistent: all operations on one atomic form a single order consistent with each task's program order. `add` returns the sum and is checked like any `int64` addition: it traps on overflow, leaving the value unchanged. |
| `Semaphore` | `create(permits)`; `acquire()` parks until a permit is free and returns `Acquired` or `Cancelled`; `try_acquire()` returns a `bool` and never parks; `release()` wakes the longest-waiting task. A semaphore is a channel of `bool` permits, so waiting tasks are served first come, first served and a wait can be cancelled. Releasing more permits than were created traps with `invalid state: released more permits than were created`; a negative count traps with `invalid argument: permits`. |
| `Guarded<T>` | `create<T>(initializer)` runs `initializer` in the calling task and wraps the state it builds, whose captures must be transferable; the state is reachable only through the guard. `with<U>(func(state: T) -> U) -> U` runs the function with exclusive access. The function's captures and `U` must be transferable, so no reference to the state can escape. A second task calling `with` parks until the first returns, first come, first served, and that wait is not cancellable because `with` has no outcome for it. Calling `with` on a guard the task already holds is the program trap `invalid state: guard already held`, status 134. |

There are no concurrent collections; build one on `Guarded<T>` or as an owner task. Objects of the program's own classes are shared through the rule of section 8.1.

### 8.1 Monitors

A *monitor* is a class whose instances tasks share and whose methods exclude one another. `#Synchronized` on a method makes it run with exclusive access to its receiver; on a class it marks every exported method with a body ([Tags](Tags.md#synchronized)).

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

/// A running total that tasks share.
#Synchronized
let Tally = class (
    let count: int64 = 0;

    export let add = func(amount: int64) -> int64 {
        for (let i: int64 = 0; i < amount; i++) {
            let seen = count;
            note();
            count = seen + 1;
        }
        return count;
    };

    export let add_twice = func(amount: int64) -> int64 {
        add(amount);
        return add(amount);
    };

    export let total = func -> int64 => count;

    let note = func {};
);

let contribute = func(tally: Tally) -> int64 => tally.add_twice(25);

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let tally = new Tally();
        scope {
            for (let i = 0; i < 8; i++) {
                spawn contribute(tally);
            }
        }
        console.print_line(tally.total().to_string());
        return program.ExitCode.Ok;
    };
);
```

```text
400
```

Each addition reads the count, makes a call, which is a scheduling point (section 10.2), and writes the count back. Without the guard a task could be switched out between the read and the write and lose an update; with it, the total is exact under every schedule. [monitor.nra](../../examples/concurrency/monitor.nra) is a complete program with a monitor, an `#Immutable` value class, and `#Pure` and `#ThreadSafe` functions.

**The shared-object rule.** This release records the following decision, which [Status](Status.md#recorded-decisions) lists. An instance may be shared between tasks, which means captured by a spawned call, passed to one, or sent on a channel without its ownership moving, when its class is a monitor. The compiler decides it over the linked, specialized program:

- every exported method of the class and of its bases is `#Synchronized`. Constructors and destructors are exempt, because they run before the object is shared and when nothing refers to it any more; so are bodyless requirements, whose implementations are methods of subclasses. The accessors of a class with synchronized methods take the guard automatically;
- every exported instance field is `last` or `const` with an [immutable type](Types.md#immutable-classes), or has accessors, which code outside the class always goes through;
- every instance field, exported or not, has a transferable type (section 5.1): numbers, text, `#Immutable` objects, other monitors, and `Std.Concurrent` handles. State such as a map lives in a `Guarded<T>` field or with an owner task. The runtime checks the layout of every object reachable from a value it gives to another task ([NVR](../runtime/NVR.md#scheduler-operations-15)), and this rule keeps a monitor's own fields inside that check;
- its state is reached only through the receiver of its own methods: no code reads or writes a field of an instance, or calls one of its methods that is not exported, through any reference other than `instance` (or a bare member name) in the class's own methods, constructors, and field initializers. A function value that captures the instance counts as another reference. A violation is reported as `'take_all' uses the field 'balance' of an instance other than its receiver, without that instance's guard`;
- every subclass is a monitor, and the class is not marked `#NotThreadSafe`.

The parameters and results of a monitor's methods may have any type. A method runs in the task that calls it, so a list passed to it or returned by it stays with that task, and the field rule keeps every reference into the monitor's own state inside the monitor. A class that has synchronized methods and breaks a rule still runs each synchronized method under the guard; it is only its instances that cannot be shared, and a spawn that tries is rejected with the reason as a label (section 5.1). Lowering records `class.shareable` on a class whose declaration satisfies the first two rules ([NeuraIR format](../NirFormat.md#revision-110-symbol-attributes-and-deferred-initialization)); the other rules depend on the whole program and are checked after linking.

**The guard.** Each instance's guard is built on the scheduler's guard operations, the ones `Guarded<T>` uses (`guard-open`, `guard-enter`, `guard-exit`), so it behaves as a guard does everywhere: a task that finds the guard held parks first come, first served, the wait is not cancellable, a task dump shows `parked: guard N (held by task M)`, a cycle of waits is a deadlock reported with its wait chain (section 10.5), and schedule exploration and record and replay treat it like any other scheduler operation. The field initializers of the base-most class that declares a synchronized method open the guard, before any constructor body runs, so a constructor may call synchronized methods. Each guard is charged 8 heap cells (section 10.3) and released when the instance is collected.

**Re-entry.** A synchronized method may call the methods of its own receiver, synchronized or not, written `instance.add(1)` or `add(1)`, without taking the guard again. This differs from `Guarded<T>.with`, which never re-enters. The compiler gives each synchronized method a locked copy of its body, named `$locked.<name>` in task dumps and traces: the method enters the guard, calls the locked copy, and leaves the guard, and a call on the receiver from a locked body goes to the locked copy of its target, and of the target's overrides, so dispatch is unchanged. Re-entry through any other path, such as passing `instance` to a function that calls back into it, calling through a function value that captured it, or calling through another reference to the same object, takes the guard again; the task already holds it, so that is the program trap `invalid state: guard already held` (status 134), as for `Guarded<T>.with`. A dynamic rule, with the holding task and a depth kept in the instance, would need an operation that tells a task its own identity, which the runtime does not have; the receiver rule covers a method calling its siblings without a runtime change.

**Release.** The locked copy's return is the only way out of a synchronized method: a trap ends the run (section 4.2), so does `Std.Program.Process.exit`, and cancellation is cooperative (section 7), so a cancelled task leaves a synchronized method by returning, and the guard passes to the next waiting task.

**Waiting inside.** A synchronized method may park (a receive, a send to a full channel, a join, a timer wait, a scope exit, an overlapping host request) and holds the guard while it waits; the tasks that call the instance meanwhile wait too. A task that a synchronized method spawns and that calls back into the same instance waits for the guard, so a method that then waits for that task, at a join or at its scope's exit, deadlocks. Keep waits out of synchronized methods where a caller does not expect them.

**Without tasks.** In a program that has no `spawn`, synchronized methods compile to plain methods: there is no guard, no locked copy, no extra step, and no heap cell, so the run is the same with and without the tag. A module compiled alone has no guard either and gets one when it is linked into a program that spawns, as it gets its polls (section 10.2).

**Stages.** NeuraIR lowering checks the placement of `#Synchronized` (E0921, E0922), sets the `synchronized` attribute, and records `class.shareable`. After linking and specialization, a program that spawns gets the locked copies, the guard calls around each synchronized method, and the guard opening in field initializers, and the isolation check of section 5 decides which classes are shareable. IR optimization leaves every scheduler operation in place and does not inline in a program with tasks, so no call moves across a guard. Conversion turns the guard calls into scheduler exits and marks the layouts of shareable classes in the `TRNS` table. The runtime is unchanged.

### 8.2 A monitor or `Guarded<T>`

| | Monitor (`#Synchronized`) | `Guarded<T>` |
| --- | --- | --- |
| State | Fields of transferable types | Any `T`, including collections and objects |
| Access | Calls of its methods, which may call each other | One function value at a time, through `with`; its captures and result are transferable |
| Re-entry | Calls on the receiver | The trap `invalid state: guard already held` |
| Interface | The class's own methods, so callers need not know about the guard | The state's type, behind `with` |
| Use it for | A small object whose fields change together, such as a counter, an account, or a rate limiter, with an API of methods | A collection, or state that holds references, shared by tasks |

A monitor may keep a `Guarded<T>` field for a collection and expose methods that call `with` on it, which gives a collection an API of methods.

## 9. `Std.Concurrent`

One module per type, as in the rest of the [library](Stdlib.md#42-source-layout). `Std.Concurrent` sits after `Std.Time` in the dependency order and imports only `Std.Core` and `Std.Time`.

| Module | Contents |
| --- | --- |
| `Task` | `Task<T>` with `join()` and `is_finished()` |
| `Scope` | `Scope` with `cancel()`, `is_cancelled()`, `cancel_after`, `cancel_at`, `deadline()` |
| `Channel` | `open<T>`, `Sender<T>`, `Receiver<T>`, `Closer<T>`, and the outcome enums |
| `Cancellation` | `requested()`, `remaining()` for the current task |
| `Timer` | `after(duration)`, `at(instant)`; `wait()` is selectable and returns `Fired` or `Cancelled` |
| `Tasks` | `all`, `all_ok`, `first_ok` over a list of calls (section 4.2) |
| `WorkerGroup` | `with_workers<T>(workers, capacity, work, body)`, and the handle `WorkerGroup<T>` with `submit`, `try_submit`, and `close_and_drain` |
| `Parallel` | `map<T, U>(items: T[], worker, limits) -> U[]`, `limits(steps, heap_cells)`, and `ItemLimits` (section 12) |
| `AtomicInt64`, `Semaphore`, `Guarded` | Section 8 |

[Standard library](StandardLibrary.md#stdconcurrent) gives each module's contract in one place.

**`WorkerGroup`** is the answer to overload: a service bounds its in-flight work with a group and a queue long before it reaches the run-wide limits of section 10.3. Because tasks need a scope, a group exists only inside `with_workers`:

<!-- example: fragment: one with_workers call without its groups import and its record function -->
```neura
groups.with_workers<string>(2, 8, record, func(group: groups.WorkerGroup<string>) -> void {
    group.submit("started");
    match (group.try_submit("optional detail")) {
        groups.TrySubmitOutcome.Full => { },  // Shed: the queue is full.
        _ => { },
    }
});
```

- `with_workers<T>(workers: int, capacity: int, work: func(T) -> void, body: func(WorkerGroup<T>) -> void)` starts `workers` tasks that take jobs from a queue of `capacity` jobs and run `work` on each. It runs `body` with the group, then closes the queue, lets the workers finish every queued job, and returns once they have stopped. `T` must be transferable and `work` task-safe with transferable captures, as for any spawn. Fewer than one worker traps with `invalid argument: workers`, and a negative capacity with `invalid argument: capacity`. Capacity 0 is a rendezvous: a job is handed straight to a waiting worker.
- `submit(job)` queues the job and parks while the queue is full. It returns `SubmitOutcome`: `Submitted`, `Closed` once the queue is closed, or `Cancelled` when the submitting task is cancelled while it waits; the job is then not queued.
- `try_submit(job)` never parks. It returns `TrySubmitOutcome`: `Submitted`, `Full`, or `Closed`. A service that must shed load counts `Full`.
- `close_and_drain()` closes the queue and returns once every queued job has been processed and every worker has stopped. Later submissions report `Closed`. `with_workers` calls it when the body returns, so a body calls it only to drain earlier. On one handle a second call returns at once. If the calling task is cancelled while it waits, it returns early, and `with_workers` waits for the workers instead.
- A worker also stops when its scope is cancelled. Queued jobs are then left unprocessed; the job a worker is running finishes first.
- The handle holds channel ends, so it is transferable and a task can be given it to submit jobs.

## 10. Runtime

### 10.1 What stays in the circuit and what crosses the boundary

Neuron graphs keep computing every arithmetic result, comparison, and branch decision, including which `match` arm handles an operation's outcome. Graph profile 1 does not change, and a task body is ordinary compiled blocks.

The scheduler is NVM state, like the heap: task records, the runnable queue, channel buffers and waiter queues, cancellation signals, timers, and pending host requests. A block reaches it through a typed scheduler-operation exit with resolved arguments and a resume block, the same shape as a [state operation](../runtime/NVR.md#state-operations). Host I/O still crosses the runtime boundary only as host requests with declared signatures. The scheduler is not a host service: it has no import, needs no capability, and is fully determined by the image, the inputs, the limits, and the host completion trace.

### 10.2 The reference scheduler

One scheduler runs one task at a time.

- **Queues.** The runnable queue is FIFO. A newly spawned task and a newly woken task are appended. Every waiter queue (a channel's senders and receivers, a semaphore, a guard, a task's joiners) is FIFO.
- **Scheduling points.** The running task changes only when it parks (a `join` of an unfinished task, a `send` to a full channel, a `receive` from an empty one, a `select` with nothing ready, a wait for a permit or a guard, a timer wait, a scope exit with live tasks, and an overlapping host request), when it finishes, or at a poll that finds its quantum spent. At such a poll the task goes to the back of the runnable queue when another task is runnable, or when a deadline, a timer, or a host request is outstanding, so that the driver can read the clock and deliver completions before the next decision. Otherwise, with nothing else runnable and nothing outstanding, the poll restarts its count and the task continues. A program that arms no deadline or timer and issues no overlapping request therefore schedules exactly as it did before this rule.
- **Polls.** A program that contains a `spawn` gets a poll on every `Call`, every `VirtualCall`, every call through a function value, and every loop back edge. The flag is a property of the whole program: a module compiled alone carries none, and gains them when it is linked into a program that spawns. A poll counts one language operation; when a task has counted its quantum since it was last scheduled, its turn ends as the previous rule says. The quantum is a run setting (`--task-quantum`), default 1,024. A native executable uses the quantum recorded in its policy ([Standalone](Standalone.md#5-the-container)), and a trace records the quantum of the run it holds (section 11).
- **Why polls count language operations.** Steps and blocks depend on how conversion shaped the graphs. Loop iterations and calls do not. Optimized and unoptimized builds of one program therefore execute the same polls in the same order: a poll is a flag on the call or loop it counts, and no pass removes an executed call or back edge, duplicates one, or moves one across a scheduler operation ([NeuraIR optimization](../IrOptimization.md#tasks-polls-and-scheduler-operations)).
- **Identity.** The root task is task 0. Tasks are numbered in spawn order. Channel and timer identities are numbered in creation order. A dump or trace names the same things in every run of the same trace.
- **Choosing the next task.** The reference scheduler takes the first task of the runnable queue. Any other runnable task would also be a legal choice: the memory model of section 5.4 holds under all of them. The NVM lets a `ScheduleChooser` make the choice, which is how schedule exploration and the reproduction of a saved schedule work (section 14.4).
- **Garbage collection.** The root set gains every task's frames, parked or runnable, every buffered message, every value held by a parked sender, every unjoined result, and every pending host request's arguments ([NVM](../runtime/NVM.md#garbage-collection)). Collection remains invisible to the program.

### 10.3 Limits

Existing limits keep their meaning for the run as a whole: steps and heap cells are summed over tasks. `--max-frames` bounds each task's depth. New run-wide limits, each a trap at the operation that would exceed it, checked before any state changes:

| Option | Bounds | Default | Exit status when exceeded |
| --- | --- | --- | --- |
| `--max-tasks` | Live tasks | 65,536 | 137 |
| `--max-queued-messages` | Values buffered in all channels | 1,048,576 | 137 |
| `--max-pending-requests` | Host requests issued and not yet delivered | 4,096 | 137 |
| `--task-quantum` | Polls per turn (section 10.2); a setting, not a limit | 1,024 | |

The defaults are confirmed against the memory a parked task actually costs by the measurement below. A request counts toward `--max-pending-requests` from the moment it parks its task until its completion is delivered; the limit traps before the request reaches the provider. A native executable records all four settings and the worker count of section 12 in its policy ([Standalone](Standalone.md#5-the-container)).

**Frame storage.** In an image that carries a `TRNS` table, every live frame of every task counts toward `--max-heap-cells`: one cell for the frame and one for each of its slots, checked before the frame is pushed and released when it returns. The scheduler charges the records it keeps in the same budget: a task record 20 cells, released when the scope that owns it closes (at once when the task has no handle); an open scope 1 cell; a channel 16 cells and one more for each buffered message; a timer 8; a guard 8; and a parallel-map item 8 plus the cells of its argument (section 12). The charge for a spawn, a channel, a timer, and a guard is checked before anything changes, so exhausting the budget traps at the operation that would exceed it with status 137. `--max-frames` still bounds each task's own depth. Without a run-wide bound, up to `--max-tasks` times `--max-frames` frames could be live, so the existing `--max-heap-cells` limit bounds them. Images without a `TRNS` table count only what they counted before tasks existed, so their limits, traps, and `peak_heap_cells` statistics are those of a program without tasks. Fleet admission counts these cells as part of a member's live heap.

**Measured default.** The fixture `tests/backend/task-chain.nvra` is a chain in which every task opens a scope, spawns one child, and parks at the scope exit until `--max-tasks` ends the run. On Linux x86-64 with a Release build, the peak resident memory of `neura run --max-tasks N` (median of three runs, read from `getrusage` of the child process, including the trap diagnostic that ends the run) was 11.4 MiB at N = 1 and 38.5 MiB at N = 65,536: about 27 MiB more for 65,535 parked tasks, an average of 0.43 KB each and about 0.55 KB each between 16,384 and 65,536 tasks. The defaults are kept.

### 10.4 Host I/O

Host I/O can overlap and be cancelled. In an image with tasks the standard providers declare the services below overlapping, and the driver (`src/nvm/driver.hpp`) starts their requests without blocking, keeps them while other tasks run, and delivers their completions at scheduling points. It starts no thread. In an image without tasks every request completes before the run continues. [NVM](../runtime/NVM.md#host-services) has the provider contract.

A provider may declare a service **overlapping**: the calling task parks, other tasks run, and the completion is delivered later.

- A request carries its execution-local sequence number, as now. That number is its ID.
- A live run delivers completions only at scheduling points, in ascending order of request number among those that are ready, appending each woken task to the runnable queue. If no task is runnable and requests are pending, the driver blocks until one completes or the earliest timer or deadline passes.
- Services that are not declared overlapping complete before the scheduler continues, as in a program without tasks. Console writes stay in this class, so output order is the order tasks ran.
- The overlapping set: `neura.net.connect`, `accept`, `read`, and `write`, `neura.console.read` when standard input is a descriptor, and `neura.clock.sleep`. File operations and every other service stay synchronous. `neura.net.connect` resolves the name before it returns, so name resolution holds up every task for its duration.
- **Cancellable requests.** The cancellable form of a service is interface version 2; version 1 keeps its contract and overlaps without being cancellable. The library binds version 2. Its provider answers a cancel request with exactly one of: the completion, if the operation finished first, or error 15 (`Cancelled`), with any partial effect stated in the result (a write that stored some bytes completes with that count). The library maps code 15 to `IoErrorKind.Cancelled`. A cancelled read consumed nothing, a cancelled accept took no connection, and a cancelled connect leaves no socket.
- [NVM](../runtime/NVM.md#host-services) states for each service whether it overlaps, whether it is cancellable, and what a cancelled operation may have done. The library pages of `Console`, `SystemClock`, `TcpStream`, and `TcpListener` say the same from the caller's side.

### 10.5 Deadlock

If no task is runnable, at least one is parked, no timer is armed, and no host request is pending, no event can ever wake a task. The run ends with `deadlock: all tasks are parked`, the wait chain (`wait chain: task 0 (parked: join of task 1) -> task 1 (parked: receive on channel 1 (0 of 1 queued, 0 senders waiting))`), a task dump (section 14.1), and status 134. If a host request is still pending, the run is waiting, not deadlocked; a dump taken then shows which request each task is waiting on.

## 11. Record and replay

`neura run --record` writes trace file version 3 for an image with tasks, and `neura run --replay` replays it. A trace of an image without tasks keeps its earlier version and bytes, so existing traces keep replaying. [NVM](../runtime/NVM.md#traces-of-images-with-tasks) gives the grammar.

[Record and replay](../runtime/NVM.md#determinism-limits-and-observability) extend to concurrent runs. The contract: given the same image, inputs, limits, quantum, and trace, a replay reproduces every scheduling decision, exit selection, effect, and the terminal outcome, without invoking any service and therefore without repeating any external write.

Trace file version 3 adds:

- the quantum and the scheduler limits in a `settings` line. A replay uses them in place of its own command-line settings, so replaying with another `--task-quantum` still reproduces the run;
- for each host request, the task that issued it, the tick at which it was issued, and whether it overlapped (and was cancellable);
- for each overlapping request, the tick at which its completion was delivered, and whether a cancel request had reached it before the answer was delivered (the answer is then the completion if the operation finished first, and error 15 otherwise). A request the run ended without delivering is recorded as abandoned;
- clock readings that expired a timer or a scope deadline, with their ticks (the replay hands each reading to the scheduler);
- in the outcome, a deadlock as its own kind.

A trace does not record schedule choices. A recorded run is a run of the reference scheduler, whose every decision follows from the image, the settings, and the recorded deliveries and clock readings. The schedules that `neura test --schedules` explores are saved in a file of their own (section 14.4).

A replay that reaches a recorded tick with different scheduler state stops with status 1 and names the first difference, as a diverging request does in any replay: a request that differs, a delivery for a request that is not pending, a run that waits at a tick where the trace records nothing, a `Cancel` that the trace does not record or one the run never makes, or a different outcome. A run under `--virtual-time` records and replays the same way: its clock readings are the virtual ones, and the replay needs neither the option nor a clock.

## 12. Multicore: `Parallel.map`

Tasks do not use more than one core. The first multicore feature is deliberately narrow:

<!-- example: fragment: one parallel.map call without Chunk, Digest, hash_chunk, chunks, limits, or the parallel import -->
```neura
let digests = parallel.map<Chunk, Digest>(chunks, hash_chunk, limits);
```

- **Meaning.** `map<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]` returns what calling `worker` on each item in index order would return. `T` and `U` are transferable.
- **Isolation, statically.** `worker` must be task-safe, issue no host request and no scheduler operation (it cannot spawn, open a scope, or use a channel, timer, or guard), and perform no identity operation (class hash or class text, whose numbers depend on request order). The compiler checks this from the same effect summaries as section 5.2, and reports a violation at the program's own call of `Parallel.map`: `the worker of a parallel map is not task-safe: ...` or `the worker of a parallel map must compute in isolation: ...`, each with the call chain. The arguments given to an item and its result must be transferable (section 5.1). `neura check --task-safety` marks which task-safe functions could be the worker.
- **Order.** `map` takes and returns arrays. Results are published in input order. If several items trap, the run reports the trap of the lowest index, which is the one the reference executor would have reached, and names it: `(item 2 of a parallel map)`, where the index counts from zero in the array.
- **Limits.** Each item runs under `ItemLimits` (steps and heap cells, both positive, made with `parallel.limits(steps, heap_cells)` and read back from its read-only fields `max_steps` and `max_heap_cells`), the same under any worker count. An item that exceeds one traps as that item, with status 152 for steps and 137 for heap cells. The run's totals add the steps of items in index order, so the run's own `--max-steps` applies to the sum.
- **Execution.** Each item runs to its end in a state of its own: its own heap, with copies of the program's globals whose type is transferable (an item may read only those that are `const`), and no scheduler. The reference executor evaluates items one after another on the scheduler's thread. With `--parallel-workers N`, where N is from 1 to 1,024 and the option applies to `run` and `test`, N threads evaluate items at once. Only wall time and peak process memory may differ; the values, the reported trap, and the statistics do not.

This is the same shape as a [fleet](GpuExecution.md#10-batched-running), many independent executions advanced together with results in member order, applied inside one run. It establishes what multicore Neura can deliver before any general parallel scheduler is designed. Benchmarks report I/O concurrency (tasks) and CPU speedup (`Parallel.map`) separately; neither figure stands in for the other.

**Measured.** `tests/benchmarks/concurrency.py` runs the four workloads under `tests/benchmarks/concurrency/` and prints the tables below. These figures are from 2026-11-10 on an AMD Ryzen 7 9800X3D with 8 logical CPUs, Linux 7.1 on x86-64, with a Release build of `neura` configured with `NEURA_GPU=OFF`. Each is the median of three runs. Peak memory is the high-water mark of the process's resident memory, sampled from `/proc` every 5 ms while it runs. They describe this machine and this build; the script reproduces them elsewhere.

*Parallel map.* `parallel-primes.nra` maps a CPU-bound function over 64 items. The baseline is the same program calling the function in a plain loop on the root task.

| Configuration | Seconds | Items per second | Peak memory (KiB) |
| --- | ---: | ---: | ---: |
| Plain loop (baseline) | 20.67 | 3.10 | 6,012 |
| `--parallel-workers 1` | 20.31 | 3.15 | 6,120 |
| `--parallel-workers 2` | 10.30 | 6.21 | 6,400 |
| `--parallel-workers 4` | 5.15 | 12.42 | 6,552 |
| `--parallel-workers 8` | 2.72 | 23.56 | 6,804 |

Isolating each item costs nothing measurable with one worker, and eight workers are 7.6 times as fast as the loop. Every configuration prints the same answer.

*Channels.* `channel-pipeline.nra` moves 200,000 messages from a producer through four worker tasks to a collector over two channels of capacity 64. The baseline does the same arithmetic in a plain loop.

| Configuration | Seconds | Messages per second | Peak memory (KiB) |
| --- | ---: | ---: | ---: |
| Plain loop (baseline) | 4.84 | 41,327 | 5,512 |
| Producer, four workers, collector | 5.50 | 36,350 | 5,500 |

The two channel operations and the scheduling of a message add about 3.3 microseconds to it.

*Tail latency.* `request-latency.nra` starts 2,000 request tasks at once on one scheduler. Each makes a round trip over channels to the task that owns a table and then computes briefly. The latency of a request is the time from its `spawn` event to its `finish` event, read from `--trace-events --trace-wall-time`, so it includes the cost of writing the events. There is no baseline: the figure is what a burst of that size waits on a single scheduler.

| Median | 99th percentile | 99.9th percentile | Longest | Requests per second (untraced) | Peak memory (KiB) |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,319 ms | 2,631 ms | 2,703 ms | 2,710 ms | 767 | 15,160 |

*Cancellation latency.* `cancellation-latency.nra` starts 1,000 tasks under one scope, waits until each has reported that it started, and cancels the scope. The latency is the time from the `cancel` event to the last task's `finish` event. Parked tasks wait in a channel receive; running tasks loop and ask `Cancellation.requested()` between steps.

| Tasks | Median | Longest of three | Peak memory (KiB) |
| --- | ---: | ---: | ---: |
| 1,000 parked | 1.15 ms | 1.17 ms | 6,732 |
| 1,000 running | 0.40 ms | 0.42 ms | 6,248 |

## 13. Compiler stages and the artifact

| Stage | Change | Hands on |
| --- | --- | --- |
| C import | None. A program with C imports cannot spawn or use any other scheduler operation (section 16). | |
| Lexer | None. `scope`, `spawn`, and `select` stay identifiers. | |
| Parser | The three contextual forms of section 4.4, with spans for the keyword, the operand, and each `select` arm. No typing or name resolution. | Syntax nodes for scope, spawn, and select |
| NeuraIR | Resolves `Task<T>` and `Scope`; checks scope-bound values during lowering and transferable types and task-safe calls over the specialized, linked program, including prebuilt `.nir` modules; checks `#Synchronized` and records monitors, and in a program that spawns gives each synchronized method its guard (section 8.1); represents scope entry and exit (`task_scope`), spawn (`spawn`), each other scheduler operation (`scheduler_call`: join, cancellation, channels, timers, guards, select, parallel items), and polls (a flag on a call or loop) as explicit operations. Written as NeuraIR 1.9 ([NeuraIR format](../NirFormat.md#revision-19-tasks)). | No operation after this point needs a source-level rule. Every spawn starts a function value of no arguments whose captures are transferable. |
| IR optimization | A task scope is a statement region; no pass removes a `spawn`, a scope, a scheduler call, an executed call, or a loop back edge, or moves one across another. The passes rewrite only operations that cannot be any of these ([NeuraIR optimization](../IrOptimization.md#tasks-polls-and-scheduler-operations)). | The same sequence of scheduler operations and polls as the unoptimized program |
| IRtoNeuronConversion | Task bodies become ordinary blocks. Each scheduler operation becomes a typed exit; `return`, `break`, and `continue` first leave the scopes they cross. A poll rides on the exit that takes the back edge or makes the call, so it adds no block. Marks the layouts of transferable classes, payload enums, and tuples in the required `TRNS` table. | Exits whose arguments and results are typed like state operations |
| Output | A new NVR minor version with the scheduler-operation exit and a required table marking transferable layouts. | An image a compatible runtime executes without source |

**Artifact.** [NVR](../runtime/NVR.md#scheduler-operations-15) 1.5 carries the scheduler exit with its 26 operations, the poll bit on continue and call exits, and the required `TRNS` table. The writer emits the lowest version required, so an image of a program without tasks keeps its earlier version and bytes. The loader validates the shape of each operation, the sorted and closed `TRNS` marks, and rejects a task image that contains `neura.ffi.*` imports, `CALB`, or `NATV`. The compiler emits all of it from source and from `.nir` 1.9 (`task_scope`, `spawn`, `scheduler_call`, and polls), and performs the scope-bound, transferability, and task-safety source checks of sections 4 and 5. The NVM runs the images on the reference scheduler of section 10.2, checks the layout of every object a spawn or a send transfers against `TRNS`, enforces the limits of section 10.3, and exposes task snapshots (`Execution::dump_tasks()`), events, and per-spawn-site profiles. `nvm::record` and `nvm::replay` accept task images and use trace file version 3 (section 11). A native executable records the task settings in its policy: container version 2 carries the task limit, the quantum, the queued-message and pending-request limits, and the parallel worker count, which `neura build --native` sets with the options of section 14.3 and `neura inspect` prints. `NEURA_RUN_OPTIONS` can lower the limits and the worker count and cannot change the quantum, because the quantum changes the interleaving and so the output order ([Standalone](Standalone.md#64-neura_run_options)).

**Inspection.** `disassemble` prints scheduler operations as it prints state operations, with no source map needed, and `assemble` reads them back to the same bytes. `decompile` labels each scheduler operation and poll `[decoded]`, because it is read from the exit record and not inferred, and pairs scope entries and exits when every path agrees on the open scopes; where it cannot pair them it shows the operations and says so ([Decompiler](../Decompiler.md#tasks-and-scheduler-operations)). A recovered image that has tasks can be compared with its original by replaying the original's version 3 trace, which is an observed match under that trace, not a proof; the decompiler's own behavior tests do not include a task image yet.

## 14. Tooling

### 14.1 Task dump

A task dump is a read-only snapshot taken at a scheduling point. It never changes the run and is not part of a trace.

```text
task 0  root  parked: join of task 1
    Task.Task<int32>.join at std/concurrent/task.nra:17:40
    Main.main at dead.nra:18:32
    start Main.main at dead.nra:13:5
  task 1  spawned at dead.nra:16:27  parked: receive on channel 1 (0 of 1 queued, 0 senders waiting)
      Channel.Receiver<int32>.receive at std/concurrent/channel.nra:80:53
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

A native executable takes `--task-dump`, `--trace-events`, `--trace-wall-time`, `--stats`, and `--profile` through `NEURA_RUN_OPTIONS`, and answers `SIGQUIT` with a task dump as `neura run` does.

### 14.3 Command line

| Command | Options |
| --- | --- |
| `run`, `test` | `--max-tasks N`, `--task-quantum N`, `--max-queued-messages N`, `--max-pending-requests N` (section 10.3); `--parallel-workers N`, from 1 to 1,024 (section 12); `--task-dump[=text\|json]` (section 14.1); `--virtual-time` (section 14.4) |
| `run` | `--trace-events <file>` and `--trace-wall-time` (section 14.2); `--record` and `--replay` (section 11) |
| `test` | `--schedules N`, `--schedule-seed S`, `--save-schedule <file>`, and `--schedule <file>` (section 14.4) |
| `check` | `--task-safety` (section 5.2) |
| `build --native` | `--max-tasks`, `--task-quantum`, `--max-queued-messages`, `--max-pending-requests`, and `--parallel-workers`, recorded in the executable's policy |

Each option is rejected on the other commands: `--task-dump applies only to run and test`, `--trace-events applies only to run`, `--schedules applies only to test`, `--task-safety applies only to check`, and, for a task setting on `build` without `--native`, `--task-quantum applies only with --native`. A [native executable](Standalone.md) takes its task settings from its policy. Its options variable accepts only settings that restrict or observe a run: it can lower the three limits and the worker count and select a task dump or trace events, and it refuses `--task-quantum` and `--virtual-time`.

### 14.4 Testing concurrent code

Each of the three tools rests on one mechanism of the NVM: a `TaskClock` supplies the time that timers, deadlines, and the clock services read, and a `ScheduleChooser` picks which runnable task goes next at each scheduling decision. The reference scheduler is the chooser that always takes the first task, and the host's steady clock is the default `TaskClock`.

**Virtual time.** `--virtual-time` on `run` and `test` replaces the clock with a virtual one.

- The clock starts at zero and stands still while any task can run. When every task is parked it jumps to the earliest timer, deadline, or sleep. A two-hour timeout test takes no real time and is exact.
- The monotonic clock reads the virtual clock. The wall clock reads that many nanoseconds after 2000-01-01T00:00:00Z. A sleep in a program without tasks moves the clock by its length and returns at once.
- Under `neura test`, each test has its own clock, which starts at zero.
- The option changes what the clock reads, not who may read it: under `--deny-clock` the clock services still fail with `CapabilityDenied`.
- A virtual-time run records and replays like any other; the trace holds the virtual readings. `--virtual-time` is rejected with `--replay` (`--virtual-time cannot be combined with --replay: a replay takes its clock from the trace`) and with `--each` (`--each cannot be combined with --virtual-time`).
- Real sockets and standard input are still real. When every task is parked and a timer or deadline is armed, the clock jumps to it without waiting for I/O that is not ready yet, so under virtual time a timeout wins over a peer that has not answered. A test that needs both a timeout and a real peer runs without the option.
- A native executable refuses the option in `NEURA_RUN_OPTIONS`, which admits only settings that restrict or observe a run; virtual time changes what the program reads from the clock.

**Schedule exploration.** `neura test --schedules N` runs each test under up to `N` legal schedules.

- Schedule 1 is the reference scheduler's. Schedule `k` for `k` from 2 to `N` picks uniformly among the runnable tasks at every scheduling decision, from a fixed generator seeded with `S + k - 1`, where `S` is `--schedule-seed` (default 0). The same seed gives the same schedules on every machine and in every run.
- A test stops at its first failing schedule. A test in which no decision ever offered more than one runnable task has a single schedule and is reported as `[1 schedule]`.
- Every schedule obeys section 5.4, so a failure is a real bug in the program, not an artifact. Exploration is bounded and makes no claim about schedules it did not run; the report states how many ran.
- **Exploration varies which runnable task goes next and nothing else.** It does not vary where the completions of overlapping host requests are delivered: those arrive when the host makes them ready. A test whose host requests are deterministic is therefore explored and reproduced exactly; a test that uses timers or deadlines gets that by running under `--virtual-time`.
- **With `--schedules` above 1, or with `--virtual-time`, tests run one at a time on the CPU.** They are not advanced together as a fleet, and no graph device is used.

The report adds the count to each line and one line of totals, in this form:

```text
PASS OrderTests.test_sum_is_order_free [8 schedules]
FAIL OrderTests.test_assumes_spawn_order [schedule 2 of 8, seed 1]: expected <1>, got <2> at order-tests.nra:39:9
PASS OrderTests.test_without_tasks [1 schedule]
explored 11 schedule(s) of 3 test(s), seed 0
3 test(s), 2 passed, 1 failed
```

The `seed` in a `FAIL` line is the seed of the failing schedule, `S + k - 1`. The `explored` line gives the schedules that ran over all tests and the `--schedule-seed` of the command. Which schedule of a test fails first, and so the total, follow from the seed.

**Reproduction.** `--save-schedule <file>` writes the first failing schedule of the run (with `--schedules 1`, the default, that can only be the reference schedule, which has no choices), and prints `neura: note: the failing schedule of <test> was written to <file>` to standard error. `neura test --schedule <file>` runs that one test under exactly that schedule, with the file's quantum and clock, and reports it as `FAIL <test> [saved schedule]: ...` or `PASS <test> [saved schedule]`.

**A saved schedule is a small text file of its own, not a host trace.** A test is not a runnable program, so `run` has nothing to replay it against. `neura run --replay` does not accept the file. Its form, with one example choice:

```text
neura-schedule 1
test OrderTests.test_assumes_spawn_order
seed 1
quantum 1024
virtual-time off
choose 3 1
```

The `test`, `seed`, `quantum`, and `virtual-time on|off` lines follow the version line in that order. Each `choose <tick> <position>` line records one decision that differed from the reference scheduler's: at decision `tick`, the task at `position` of the runnable queue ran in place of the first. Positions count from zero, so a recorded position is never zero, and the lines are in ascending order of tick. A schedule of the reference scheduler has no `choose` line.

- The file must name a test of the listed files: otherwise the command fails with `<file>: the schedule is for test <name>, which these files do not define`.
- A file that is not in this form is refused before any test runs, with its line number and the reason, such as `bad.schedule:1: unsupported schedule version; expected 1`.
- When a recorded position does not exist in the run, the schedule belongs to another program, and the test fails with `the schedule does not fit this program: at decision <tick> fewer tasks could run than its choice needs`.
- `--schedule` cannot be combined with `--schedules`, `--schedule-seed`, or `--save-schedule`.
- The test runs under virtual time when the file says `virtual-time on` or the command gives `--virtual-time`.


## 15. Verification

Each row names its boundary, the CTest entry that establishes it, and the kind of evidence, as [Contributing](../../CONTRIBUTING.md#verify-behavior) asks. Run them with `ctest --test-dir build -R 'concurrency|stdlib-concurrent'`, which also matches `ir-optimization-concurrency` and `backend-concurrency`.

| Boundary | Check | Test | Kind |
| --- | --- | --- | --- |
| Parser | Each form accepted; each row of section 4.4 rejected at its span; programs using `scope`, `spawn`, and `select` as names unchanged | `concurrency-frontend` | Conformance |
| NeuraIR | Scope-bound, transfer, and effect checks, including through virtual calls and function values; the call chain in the diagnostic; `check` and `build` report the same diagnostic. Prebuilt `.nir` modules are checked as part of the linked program | `concurrency-frontend` | Conformance |
| IR optimization | The NeuraIR reference evaluator, extended with its own scheduler, runs the fixtures it models before and after optimization under several quanta and task limits; results, traps, host effects, and the schedule, tick for tick, must agree. It models the scope operations: scopes, `spawn`, `join`, and `is_finished`. The structure check shows that every scheduler operation and poll the evaluator can reach survives, and that removing the polls changes the schedule | `ir-optimization-concurrency` | Differential |
| Conversion and NVM | The scope fixtures built from source (optimized, unoptimized, and through a `.nir` file) and run on the NVM must match the reference evaluator on host requests, exit status or trap class, and schedule; a second run from clean state repeats everything; fleet members give each its serial result; hand-assembled task images exercise each operation, and a handle used after its scope closed is an invalid task operation | `backend-concurrency` | Differential, determinism |
| Build routes | Every fixture under `tests/concurrency/programs`, the ones that use channels, `select`, cancellation, timers, `Tasks`, and `WorkerGroup` included, is built optimized, unoptimized, and through a `.nir` file. The two routes give the same bytes, and the optimized and unoptimized images give the same output and status under six quanta, run after run. The evaluator does not model these operations, so this comparison is between two builds on the NVM, not against an independent model | `concurrency-programs` | Differential (optimized against unoptimized), determinism |
| Format | Hand-assembled 1.5 images; scheduler operations in an image that declares an earlier version, an unclosed transferable marking, and a concurrent image with `NATV` are rejected | `backend-format`, `backend-concurrency` | Structural |
| Channels, cancellation, and guards | Rendezvous, full buffers, close with parked senders, cancellation of parked operations, nested shutdown, the task, queue, and heap limits, collection with parked tasks, a deadline that stops a task that never parks | `backend-concurrency`, `concurrency-programs` | Runtime |
| Host I/O | Completions delivered in provider order while other tasks run, a busy task that does not starve a ready completion, the pending-request limit, cancellation racing completion in both orders, and the scoped helpers closing exactly once; an echo service, cancelled reads, accepts, and sleeps, and overlapping sleeps on the loopback interface and the real clock | `backend-concurrency`, `concurrency-io` | Runtime, end to end |
| Replay | Recorded runs replay with every capability denied; a perturbed trace reports the first difference; a deadlock, a limit trap, and a process exit delivered by a completion replay as outcomes | `backend-concurrency`, `concurrency-io` | Replay |
| Library | `Tasks` and `WorkerGroup`: results in call order, the first failure or success by completion order, cancellation of the remaining calls, shedding, draining, and their traps | `stdlib-concurrent` | End to end |
| Test modes | Virtual time under `run` and `test`; exploration from a seed, repeated; a saved schedule and its reproduction; malformed and foreign schedules; the service fixture `tests/concurrency/schedules/service-tests.nra` | `concurrency-schedules` | End to end |
| Native executables | Recorded task settings, the options variable lowering them, and the observing options | `native-build`, `native-container` | End to end, structural |
| Decompiler | Scheduler operations and polls in images without `SMAP` and `NAME` | `decompiler-*` | Structural, observed |
| `Parallel.map` | One worker against several: values, the trapping item, item limits, and every statistic | `concurrency-parallel` | Differential |
| Monitors | A monitor shared by tasks gives the exact total under six quanta and under explored schedules; re-entry through the receiver; cancellation while tasks wait for the guard; a deadlock between two monitors; a trap inside a synchronized method; record and replay; a monitor and a `#Pure` function exported by a prebuilt `.nir` module; the rejected spawns with their reasons; a program without tasks takes the same steps with and without the tag | `concurrency-monitors`, `ui-concurrency`, `ui-tags` | Runtime, differential (optimized against unoptimized), replay, conformance |

These establish observed agreement on the fixtures and quanta they run. They are not proofs of equivalence, and the evaluator shares the language rules as written with the compiler. The independent model covers scopes, `spawn`, `join`, and `is_finished` only: for channels, `select`, cancellation, timers, guards, and the library helpers, the evidence that optimization preserves behavior is the comparison of optimized and unoptimized builds on the same NVM, which cannot show an error the two builds share.

## 16. Limits

- **Native code.** A program that uses a scheduler operation (a scope, a spawn, a channel, a timer, a guard, a select, or a parallel map) cannot use `Std.DynamicForeign` or `import:c(...)`. The compiler rejects the combination (section 4.4).
- **Cores.** The reference scheduler runs all tasks on one thread. Only `Parallel.map` uses more than one core (section 12).
- **Collections.** There are no concurrent collections. Build one on `Guarded<T>` or as an owner task.
- **Monitors.** A monitor's fields have transferable types, so a monitor keeps a collection in a `Guarded<T>` field. Re-entry is recognized only for calls on the method's own receiver; any other path back into a held monitor traps. A synchronized method that waits holds the guard, and the wait for a guard is not cancellable (section 8.1). Other shared mutable objects are not allowed: the general ownership and aliasing rules stay open under Lifecycle in [Status](Status.md#open-language-decisions).
- **Transfer.** A class instance is transferable only if it is a `Std.Concurrent` handle, an `#Immutable` class, or a monitor (section 5.1). `Error` and `Result<T>` are not transferable; `PortableError` is the transferable copy of an error.
- **Initialization.** `scope` is a function-body statement. In a top-level statement or an initializer it is an error, because globals are still being initialized (section 4.1).
- **Task safety.** The union rule for virtual calls and function values (section 5.2) can reject a call because of an implementation that never runs. `check --task-safety` reports the verdict for every function. Counting every closure of the linked program rejected a `BufferedReader` or `Utf8Reader` read in any program that linked `Std.Io.Console`, because the console's input source has the same function type; the rule counts only closures that code which can run creates, which leaves that case to programs that call `Console.input()`.
- **File I/O.** File services complete before the scheduler continues; they do not overlap other work (section 10.4), so a long synchronous file call holds every task.
- **Exploration.** Schedule exploration varies which runnable task goes next. It does not vary where the completions of overlapping host requests are delivered (section 14.4).
- **Platforms.** Tasks are tested on Linux. Windows is unverified.
