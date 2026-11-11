# `Std.Core.Failure`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Failure`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/failure.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Failure`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Failure** · [`export let trap = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/failure.nra#L7)
  Stops execution and reports the supplied diagnostic message.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.1 Failure model

There are two kinds of failure and they never mix.

**Returned errors.** A function whose failure depends on input text, input bytes, or the outside world returns `Result<T>` ([section 6.2](Stdlib-Design#62-resultt)). The caller reads it with `match`. There is no propagation shorthand and no exception; passing an error up is an explicit `match` arm that returns it.

**Traps.** A function whose precondition is violated ends the run. The run stops at once with the process status of the trap's class, 134 (`Abort`) for a message trap; no library or user code runs afterwards; nothing can observe or recover it ([Errors](Errors)). The module `Failure` in `Std.Core` has one function:

| Member | Behavior |
| --- | --- |
| `trap(message: string)` | Calls `trap_with_message`. The run ends with reason 3, the message, and a source location when available. |

Library trap conditions and their messages. The message text is part of the contract. `<detail>` is fixed text given where the trap is documented.

| Condition | Message | Examples |
| --- | --- | --- |
| An element was required and none exists | `no such element` | `first()` of an empty list, `next()` after the last element, `expect` on `None` |
| An argument violates a documented precondition | `invalid argument: <detail>` | A negative count, a radix outside 2 through 36, a zero step |
| The receiver's state forbids the call | `invalid state: <detail>` | A consumed `Sequence`, `update` after `finish` on a digest |
| A collection changed structurally during iteration | `collection modified during iteration` | Any library iterator; the built-in for-each reports the same words with reason 5 |
| An assertion failed | The assertion's message | `Std.Test` |
| A host write that has no error result failed | `console write failed` | `Console.print_line` |

**The library does not re-check what the language already checks.** An index outside a built-in list, an overflow, a zero divisor, a shift count out of range, and an out-of-range conversion trap in the language with their own reasons (1 and 2) and locations. A library member that would fail the same way lets the built-in operation trap and documents that it does. `ArrayList.get` with a bad index is a bounds trap from the list read, not a library trap.

**What the absence of unwinding means.** The language has no panics that unwind, no deferred actions, and no recovery, and the library has none.

| Not provided | Instead |
| --- | --- |
| `Closeables.use` closes the resource even when the body panics | Replaced by the scoped helpers of [section 5.5](Stdlib-Design#55-resources), which close the resource on every ordinary completion. A body cannot fail and return; it either returns or the run ends, and when a run ends, by return or by trap, the host releases every file and socket. |
| "If the predicate of `remove_if` panics, the collection is left valid" | Removed. A trap inside a callback ends the run, so no state can be observed afterwards. No collection keeps recovery code. |
| `Iterator.next` panics with `NoSuchElementError`; modification panics with `ConcurrentModificationError` | Both are traps with the messages above. The error classes for programmer mistakes are gone. |
| `assert_panics` recovers a panic and returns its error | Removed. A test that expects a trap is a program with an expected-diagnostic file, run by the test harness as the existing trap tests under `tests/backend/traps` are ([section 17](Stdlib-Design#17-verification)). |
| A fallible callback | A callback that can fail returns a `Result`, and the member that calls it documents how it stops. |

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](Stdlib-Design#46-retired-built-ins)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, as every program does. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, `Status(int)`, and the named statuses `SeriousFailure` (2), `CommandNotFound` (127), `IllegalInstruction` (132), `Breakpoint` (133), `Abort` (134), `ArithmeticError` (136), `OutOfMemory` (137), `BusError` (138), `SegmentationFault` (139), `CpuTimeExceeded` (152), and `FileSizeExceeded` (153). `Ok`, `Failure`, and `Status` map to 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. A payload enum lets a program return any status without a host call. Neura enums cannot carry explicit integer values, so the compiler and `status_of(code) → int` hold the table; the named variants follow declaration order after `Status`, so the original three keep their ordinals.
- **`status_of(code: ExitCode) → int`** returns the status a variant stands for, trapping for an invalid `Status`.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. The overload `Process.exit(code: ExitCode)` exits with `status_of(code)`. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 14.2 `Std.Test`

This module replaces the built-in `Test` as described in [section 4.6](Stdlib-Design#46-retired-built-ins). An assertion builds its message in Neura and calls `Failure.trap`. The runner reports the message from the trapping test.

| Member | Failure message | Needs |
| --- | --- | --- |
| `assert(condition: bool)` | `assertion failed` | |
| `assert_with(condition: bool, message: string)` | The message | |
| `fail(message: string)` | The message | |
| `assert_equal_int32`, `assert_equal_int64`, `assert_equal_uint64`, `assert_equal_bool` `(expected, actual)` | `expected <` expected `>, got <` actual `>` | |
| `assert_equal_string(expected, actual)` | The same | T1 |
| `assert_equal_float64(expected, actual)`, `assert_float_near(expected, actual, tolerance)` | The same; `near` fails when the absolute difference exceeds the tolerance or either value is NaN | N1 for the message text |
| `assert_equal<T>(expected: T, actual: T)`, `assert_not_equal<T>(unexpected: T, actual: T)` | The same, using `equals` and `to_string` | G3 |
| `assert_some<T>(o) → T`, `assert_none<T>(o)`, `assert_ok<T>(r) → T`, `assert_err<T>(r) → Error` | `expected Some, got None`, and so on; `assert_ok` includes the error's message | G2 |

- The typed assertions remain available for callers that prefer a concrete signature.
- **There is no assertion that expects a trap.** A trap cannot be caught, so a test function cannot observe one and continue. Code that must trap is tested by a separate program with an expected diagnostic ([section 17](Stdlib-Design#17-verification)).
- A failed assertion ends its test. Tests are discovered and run as [Testing](Testing) describes; only the import changes.
