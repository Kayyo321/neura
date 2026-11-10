# Errors and failure handling

Neura distinguishes expected failures, which a program handles, from runtime failures, which end the run.

**Expected failures are returned values.** A function that can fail returns a value that says so, such as an enum variant or an object describing the failure, and its caller inspects it. There is no exception mechanism and no implicit propagation. The [standard library](StandardLibrary.md) provides `Std.Core.Result` and `Std.Core.Optional` for this; a program can also declare its own enums and classes for outcomes.

```neura
export let Parse = enum (Ok, Empty, TooLong);

let check = func(words: string[], limit: int) -> Parse {
    if (words.length == 0) {
        return Parse.Empty;
    }
    if (words.length > limit) {
        return Parse.TooLong;
    }
    return Parse.Ok;
};
```

**Runtime failures trap.** Integer overflow, a zero divisor, an out-of-range conversion ([Numbers](Numbers.md)), a negative index, a list modified during for-each, a failed console write, and a failed test assertion are program traps with documented reason codes. An index at or past a collection's length, a read of an uninitialized field or global, use of a destroyed object, and exhausted run limits are faults that NVM detects. Both end the run immediately with a diagnostic that names the source location when the image has a source map ([NVM](../runtime/NVM.md)). A trap cannot be caught, and no cleanup runs after it.

The process status names the trap's class with the shell convention for a process that a signal ended, 128 plus the Linux signal number, and the matching [`Std.Program.ExitCode`](Modules.md#program-entry) variant:

| Trap | Status | `ExitCode` variant |
| --- | --- | --- |
| Integer overflow, a zero divisor, `MIN / -1`, an out-of-range conversion (reason 1) | 136 | `ArithmeticError` |
| A negative index (reason 2), an index at or past the length, a read of an uninitialized slot, field, or global, an invalid handle, a nil class reference, or the frame depth limit | 139 | `SegmentationFault` |
| An explicit message trap or failed assertion (reason 3), a list modified during for-each (reason 5), or a failed class cast | 134 | `Abort` |
| Invalid dispatch (reason 4), a malformed graph, text, or host completion, or an invalid task operation | 132 | `IllegalInstruction` |
| The heap or handle limit, or the task, queued-message, or pending-request limit | 137 | `OutOfMemory` |
| The step limit | 152 | `CpuTimeExceeded` |
| A deadlock: every task is parked and nothing can wake one | 134 | `Abort` |

A message trap or failed assertion in any task has the status of its class, and so does the trap `invalid state: guard already held` that `Guarded<T>.with` raises when the task already holds the guard. An invalid task operation reports a violation of the scheduler's contract, such as using a task identity that is not a live child of the running task. The compiler's checks rule it out for a compiled program, and it is reachable from a hand-assembled or edited image.

## Tasks

Concurrency ([Core language](CoreLanguage.md#tasks-scopes-and-select)) adds no way to catch a failure and does not change the error model.

**Expected failures are still returned values.** A task's function returns its failures as values. If it returns `Result<User>`, `task.join()` is that `Result<User>`, and the caller reads it. Closure and cancellation are expected events of channels, timers, and selects and are returned as typed outcomes, such as `Closed` and `Cancelled`, not raised.

**A trap in any task ends the run.** The run stops at once with that trap's status. No other task runs afterwards, no scope waits for its tasks, no cleanup runs, and effects that have already happened stay visible. The diagnostic names the task, the place it was spawned, and the chain of spawns up to the root task, which is task 0. For a task that divides by zero two spawns below `main`:

```text
child-trap.nvr: trap: source arithmetic failure (trap reason 1): (task 2 spawned at child-trap.nra:10:21 by task 1 spawned at child-trap.nra:18:24 by task 0) in function 11 block 1 at child-trap.nra:6:50
```

The text after the colon is the diagnostic of the same trap in a program without tasks, with the spawn chain added in parentheses. `neura run --task-dump` also prints every task, with its stack and what it waits for, before the diagnostic. The program is `tests/concurrency/programs/child-trap.nra`, and its status is 136.

**`Process.exit` from any task ends the run** with its status and abandons every other task, including the root task and tasks that are running or parked.

**A deadlock ends the run.** If no task can run, at least one is parked, no timer is armed, and no host request is pending, nothing can ever wake a task. The run ends with status 134, a diagnostic that begins `deadlock: all tasks are parked`, the chain of tasks that wait for each other, and a dump of every task:

```text
dead.nvr: trap: deadlock: all tasks are parked
wait chain: task 0 (parked: join of task 1) -> task 1 (parked: receive on channel 1 (0 of 1 queued, 0 senders waiting))
```

A run that waits for a timer, a deadline, or a host request is waiting and not deadlocked.

**Limits.** A program that would have more live tasks than `--max-tasks` (65,536 by default), buffer more than `--max-queued-messages` values (1,048,576) in channels, or have more than `--max-pending-requests` (4,096) overlapping host requests in flight ends with status 137 at the operation that would exceed the limit, before it changes any state: `trap: task limit exceeded: spawning another task (task 0) ...`. The memory that tasks, scopes, channels, timers, and guards use also counts toward `--max-heap-cells`. No service overlaps yet, so the pending-request limit cannot be reached at this time.


For the intended use cases, see [errors.nra](../../examples/guide/errors.nra). Its commented pseudocode illustrates a file-read failure and invalid input; file input belongs to the standard library.
