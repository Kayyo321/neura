# Concurrency: tasks, channels, and select

Neura has structured concurrency with no `async`/`await`. A call runs as a *task* with `spawn`, every task belongs to a `scope` that waits for it, and tasks share no mutable state except through channels, a few handle types, monitors (classes whose methods are `#Synchronized`), and `#Immutable` objects. The compiler enforces the isolation. Specification: [Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md).

## Contents

- [The model in brief](#the-model-in-brief)
- [`scope`, `spawn`, and `join`](#scope-spawn-and-join)
- [Channels](#channels)
- [`select`](#select)
- [Cancellation, deadlines, and timers](#cancellation-deadlines-and-timers)
- [Isolation rules the compiler enforces](#isolation-rules-the-compiler-enforces)
- [Shared state](#shared-state)
- [Monitors](#monitors)
- [Immutable objects](#immutable-objects)
- [Helpers: `Tasks`, `WorkerGroup`, `Parallel.map`](#helpers-tasks-workergroup-parallelmap)
- [Running and testing concurrent programs](#running-and-testing-concurrent-programs)

## The model in brief

- One deterministic scheduler runs tasks on one core, switching at waits (join, channel, timer, host I/O) and at compiler-inserted polls. Only `Parallel.map` uses several cores.
- `spawn` does not switch tasks: a child first runs when the parent waits or its turn ends. Do not rely on this order; tests can explore other schedules.
- Expected failures are returned values (`join` hands them back). A trap in any task ends the whole run at once.
- A program that uses any scheduler operation cannot also use `Std.DynamicForeign` or `import:c(...)`.
- `scope`, `spawn`, and `select` are contextual words: `scope {`, `scope (name) {`, `spawn name(...)`, and `select {` are forms; elsewhere they are ordinary identifiers.

## `scope`, `spawn`, and `join`

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

let say = func(text: string) -> int {
    console.print_line(text);
    return text.byte_length;
};

let report = func -> int {
    scope {
        let left = spawn say("left task");       // Task<int>
        let right = spawn say("right task");
        console.print_line("parent continues");
        return left.join() + right.join();       // waits; joining again returns the same value
    }                                            // every exit waits for the scope's tasks
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line(report().to_string());
        return program.ExitCode.Ok;
    };
);
```

```text
parent continues
left task
right task
19
```

- `scope { ... }` is allowed only inside a function body (function, method, or lambda), not at top level or in an initializer.
- `spawn call` needs a call whose callee starts with a name: `fetch(id)`, `client.fetch(id)`, `Jobs.twice(21)`, `handler(request)`. Bind a lambda to a name first. The `spawn` must be lexically inside a `scope` of the **same** function body, not in a lambda written inside the scope.
- A `spawn` of a `void` function gives `Task<Unit>` (`Std.Core.Unit`). A `spawn` used as a statement discards the handle; the scope still waits.
- `task.is_finished()` never waits. A scope can exit without joining; the result is discarded.
- Nothing outlives its scope, so nothing outlives `main`.

## Channels

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let channels = import:Std.Concurrent.Channel;

let produce = func(out: channels.Sender<int>, closer: channels.Closer<int>, count: int) {
    for (let i in 0 .. count) {
        out.send(i * i);                         // parks while the buffer is full
    }
    closer.close();                              // receivers see Closed once drained
};

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let [sender, receiver, closer] = channels.open<int>(2);   // capacity 2; 0 = rendezvous
        let total = 0;
        scope {
            spawn produce(sender, closer, 5);
            let open = true;
            while (open) {
                match (receiver.receive()) {
                    channels.Received.Value(number) => { total += number; },
                    channels.Received.Closed => { open = false; },
                    channels.Received.Cancelled => { open = false; },
                }
            }
        }
        console.println("total {}", total);
        return program.ExitCode.Ok;
    };
);
```

```text
total 30
```

- `channels.open<T>(capacity)` returns the tuple `[Sender<T>, Receiver<T>, Closer<T>]`. `T` must be transferable. A negative capacity traps.
- `send(v)` returns `SendOutcome` (`Sent`, `Closed`, `Cancelled`); `receive()` returns `Received<T>` (`Value(T)`, `Closed`, `Cancelled`). Non-blocking: `try_send` (`Sent`, `Full`, `Closed`) and `try_receive` (`Value(T)`, `Empty`, `Closed`).
- `close()` is idempotent; parked senders wake with `Closed` and their values are dropped.
- Closure and cancellation are outcomes, never errors or traps. A task that waits forever with nothing able to wake it is a deadlock: the run ends with status 134 and a wait chain.

## `select`

<!-- example: check -->
```neura
let console = import:Std.Io.Console;
let channels = import:Std.Concurrent.Channel;

let drain = func(inbox: channels.Receiver<int>, spare: channels.Receiver<int>) {
    for (let round: int = 0; round < 3; round++) {
        select {
            let first = inbox.receive() => {
                match (first) {
                    channels.Received.Value(number) => { console.print_line("inbox " + number.to_string()); },
                    _ => { console.print_line("inbox ended"); },
                }
            },
            let second = spare.receive() => {
                match (second) {
                    channels.Received.Value(number) => { console.print_line("spare " + number.to_string()); },
                    _ => { console.print_line("spare ended"); },
                }
            },
            _ => {
                console.print_line("nothing ready");
            },
        }
    }
};
```

- Arms: `[let|const name =] operation => { block }`, separated by commas, plus at most one default `_ => { ... }`. An operation is a call of `receive`, `send(value)`, `join`, or `wait` on a channel end, task, or timer.
- All arm operands are evaluated once, in order, before anything is attempted. Exactly one arm commits.
- Preference rotates: each task counts its committed selects, and a select with `n` arms starts scanning at `count mod n`. With no ready arm the default runs, or the task waits for the first to complete.
- `select` works in any function body, with or without a scope.

## Cancellation, deadlines, and timers

<!-- example: check -->
```neura
let cancellation = import:Std.Concurrent.Cancellation;
let durations = import:Std.Time.Duration;
let timers = import:Std.Concurrent.Timer;
let result = import:Std.Core.Result;

let count_until_stopped = func -> int {
    let laps = 0;
    while (!cancellation.requested()) {      // cooperative: running tasks must ask
        laps = laps + 1;
    }
    return laps;
};

let stop_early = func -> bool {
    scope (group) {                          // binds group: Std.Concurrent.Scope
        let counter = spawn count_until_stopped();
        let _ = group.cancel_after(durations.of_millis(50));   // a deadline (needs the clock)
        group.cancel();                      // or cancel now
        return counter.join() >= 0;          // join is never cancelled
    }
};

let wait_briefly = func {
    match (timers.after(durations.of_millis(10))) {
        result.Result.Ok(timer) => {
            let outcome = timer.wait();      // Fired or Cancelled
        },
        result.Result.Err(failure) => { },   // the clock capability is denied
    }
};
```

- `group.cancel()` cancels the scope's tasks and their descendants. It wakes parked channel operations, timer waits, and cancellable host requests with `Cancelled`; it never interrupts a running computation.
- `cancellation.requested()` and `cancellation.remaining()` (an `Optional<Duration>`) describe the running task.
- `cancel_after`, `cancel_at`, `timers.after`, and `timers.at` read the monotonic clock and return `Result`.

## Isolation rules the compiler enforces

**Scope-bound values.** `Task<T>`, `Scope`, and any array, list, or tuple containing them must not outlive their scope. They may live in locals declared inside the scope and be passed as arguments. They cannot be returned, stored in a field, global, or enum payload, captured by a lambda, passed to `spawn`, sent on a channel, used as a type argument, or assigned to a binding declared outside the scope. Error: `a value of type Task<int32> cannot leave its scope: it cannot be returned from a function`.

**Transferable values.** Everything given to a spawned task (callee, receiver, arguments), sent on a channel, or given to `Parallel.map` must be transferable:

| Transferable | Not transferable |
| --- | --- |
| `bool`, integers, floats, `string` | Arrays and lists (so a variadic call cannot be spawned) |
| Fieldless enums; payload enums whose payloads all are (`Optional<int>` yes) | `Result<T>` (its `Error` may hold an object): send `error.to_portable(e)` |
| Tuples of transferable elements | Every other class instance, including library collections and `StringBuilder` |
| Function values whose closures capture only transferable values | Closures capturing an object, array, or list |
| `Sender`, `Receiver`, `Closer`, `Timer`, `AtomicInt64`, `Semaphore`, `Guarded<T>`, `WorkerGroup<T>` handles | Primitive references, `MemorySegment`, native handles; `Task` and `Scope` |
| Instances of `#Immutable` classes and of monitors ([below](#monitors)), shared without moving | Instances of `#NotThreadSafe` classes |

Error: `a value of type int32[] is not transferable, so it cannot be given to a spawned task`. A `join` result may be any type.

**Task-safe calls.** The spawned call and everything it can reach must not read or write a reassignable module, file, or static binding (or one whose type is not transferable; reading a transferable `const` is fine), must not trigger a class's static initializer (constructing such a class, calling its static functions), and must not call native code. Virtual calls and function values are judged over every implementation the program can create. The diagnostic lists the call chain. Pass what the task needs as arguments instead of reaching for globals. Library constants that are objects (`Duration.Zero`, `BigInteger.One`) cannot be read in a task; build them with factories (`of_nanos(0)`). `neura check --task-safety file.nra` prints the verdict for every function. `#ThreadSafe` on a function turns that verdict into a compile-time check (`E0608`); `#NotThreadSafe` forbids spawning it (`E0601`). The digests, `Std.Math.Math`, `Std.Text`, and `Std.Encoding` run on kernels and are task-safe.

## Shared state

Prefer one task owning state and serving requests over a channel (send a reply `Sender` inside the request). When state must be shared:

<!-- example: check -->
```neura
let guards = import:Std.Concurrent.Guarded;
let atomics = import:Std.Concurrent.AtomicInt64;
let semaphores = import:Std.Concurrent.Semaphore;
let maps = import:Std.Collections.HashMap;

let count_visit = func(visits: guards.Guarded<maps.HashMap<string, int>>, page: string) {
    visits.with<bool>(func(table: maps.HashMap<string, int>) -> bool {   // exclusive access
        table.set(page, table.get_or(page, 0) + 1);
        return true;
    });
};

let tally = func -> int64 {
    let visits = guards.create<maps.HashMap<string, int>>(
        func -> maps.HashMap<string, int> => maps.create<string, int>());
    let hits = atomics.create(0);
    let permits = semaphores.create(2);
    scope {
        spawn count_visit(visits, "home");
        spawn count_visit(visits, "about");
    }
    hits.add(5);
    return hits.load();
};
```

- `Guarded<T>`: `create<T>(initializer)` builds the state inside the guard; `with<U>(body)` runs `body` with exclusive access. Captures and the result must be transferable, so no reference to the state escapes. Re-entering `with` on a guard you hold traps.
- `AtomicInt64`: `load`, `store`, `add` (traps on overflow), `compare_exchange`.
- `Semaphore`: `acquire()` (`Acquired` or `Cancelled`), `try_acquire()`, `release()`.

## Monitors

A *monitor* is a class whose instances tasks share and whose methods exclude one another. `#Synchronized` on the class marks every exported method with a body; each runs under a per-instance guard, first come, first served.

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

/// A running total that tasks share.
#Synchronized
let Tally = class (
    let count: int64 = 0;

    export let add = func(amount: int64) -> int64 {
        count = count + amount;
        return count;
    };

    export let add_twice = func(amount: int64) -> int64 {
        add(amount);                      // a call on its own receiver re-enters
        return add(amount);
    };

    export let total = func -> int64 => count;
);

let contribute = func(tally: Tally) -> int64 => tally.add_twice(25);

let Main = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let tally: Tally = new;
        scope {
            for (let i = 0; i < 8; i++) {
                spawn contribute(tally);  // shared, not moved
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

The compiler lets tasks share an instance only when the class is a monitor (the **shared-object rule**):

- every exported method of the class and its bases is `#Synchronized` (constructors, destructors, and bodyless requirements are exempt); accessors of such a class take the guard too;
- every exported field is `last` or `const` of an immutable type, or has accessors;
- **every field has a transferable type**: numbers, text, `#Immutable` objects, other monitors, `Std.Concurrent` handles. A monitor cannot hold a list, an array, or a map directly; keep one in a `Guarded<T>` field or with an owner task;
- its state is reached only through `instance` (or a bare member name) inside its own methods; touching another instance's field is rejected;
- every subclass is a monitor, and the class is not `#NotThreadSafe`.

A spawn that shares a class breaking a rule is rejected with the reason as a label. Re-entry works only for calls on the receiver: calling back into the same instance through a function value or another reference while holding the guard traps with `invalid state: guard already held`. A synchronized method that waits (join, receive, timer, scope exit) holds its guard while it waits, so keep waits out of monitor methods. In a program without `spawn`, synchronized methods compile to plain methods. `#Synchronized` cannot go on a constructor, destructor, static, or bodyless method (`E0921`); an override of a synchronized method must be synchronized (`E0922`).

Choose a monitor for an object with a few short operations that tasks call directly; choose `Guarded<T>` for a collection or any state whose methods you do not own; choose an owner task and a channel for anything that waits.

## Immutable objects

`#Immutable` on a class makes the compiler check that every instance field is `last` or `const` (`E0918`) of an immutable type: `bool`, a number, `string`, a tuple or enum of immutable parts, or another `#Immutable` class (`E0919`); bases and subclasses must be `#Immutable` too (`E0920`). Tasks may then share its instances freely.

<!-- example: check -->
```neura
#[Immutable, Final]
let Reading = class (
    export last sensor: string;
    export last celsius: int32;

    export let init = func(name: string, degrees: int32) {
        sensor = name;
        celsius = degrees;
    };
);

#ThreadSafe
let describe = func(reading: Reading) -> string => reading.sensor + " " + reading.celsius.to_string();

let report = func -> string {
    let reading: Reading = new("greenhouse", 22);
    scope {
        let text = spawn describe(reading);   // an #Immutable object crosses to the task
        return text.join();
    }
};
```

## Helpers: `Tasks`, `WorkerGroup`, `Parallel.map`

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;
let parallel = import:Std.Concurrent.Parallel;
let tasks = import:Std.Concurrent.Tasks;

let square = func(value: int) -> int => value * value;
let one = func -> int => 1;
let two = func -> int => 2;

let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let squares = parallel.map<int, int>([1, 2, 3, 4], square, parallel.limits(100000, 100000));
        console.println("{} {}", squares[3], squares.length);
        let calls: (func() -> int)[list] = [one, two];
        let results = tasks.all<int>(calls);                   // results in call order
        console.println("{}", results[0] + results[1]);
        return program.ExitCode.Ok;
    };
);
```

```text
16 4
3
```

- `Tasks.all<T>(calls)`, `all_ok<T>` (first `Err` cancels the rest), `first_ok<T>`: run a list of zero-argument function values as tasks.
- `WorkerGroup.with_workers<T>(workers, capacity, work, body)`: a fixed pool over a bounded queue; `submit(job)` inside `body`.
- `Parallel.map<T, U>(items: T[], worker, limits)`: runs a pure, task-safe worker over each item on several cores (`--parallel-workers N`). The worker may not do I/O, use the scheduler, or rely on object identity. Results come back in input order, and the meaning is the sequential map.

## Running and testing concurrent programs

| Option | For |
| --- | --- |
| `--max-tasks N`, `--max-queued-messages N`, `--max-pending-requests N` | Limits (exceeding one ends the run with status 137) |
| `--task-quantum N` | How many polls a task gets per turn (changes interleaving, not results) |
| `--parallel-workers N` | Threads for `Parallel.map` |
| `--task-dump[=text\|json]` | Print every task's stack on a trap (`SIGQUIT` dumps a live run) |
| `--trace-events file`, `run --profile` | Scheduler events and per-spawn-site profiles |
| `--virtual-time` | Timers and the clock jump when every task is parked |
| `test --schedules N --schedule-seed S` | Run each test under up to N legal schedules |
| `test --save-schedule f`, `test --schedule f` | Save and replay the first failing schedule |

A trap reports the spawn chain, for example `(task 2 spawned at app.nra:10:21 by task 1 spawned at app.nra:18:24 by task 0)`.
