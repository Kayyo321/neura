# `Std.Test`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Test`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Test`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Test** · [`export let assert = func(condition: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L13)
  Traps with the default assertion diagnostic when the condition is false.
- **Test** · [`export let assert_with = func(condition: bool, message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L20)
  Traps with the supplied diagnostic when the condition is false.
- **Test** · [`export let fail = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L27)
  Fails the current test with the supplied diagnostic.
- **Test** · [`export let assert_equal_int32 = func(expected: int32, actual: int32)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L34)
  Checks exact signed 32-bit equality.
- **Test** · [`export let assert_equal_int64 = func(expected: int64, actual: int64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L41)
  Checks exact signed 64-bit equality.
- **Test** · [`export let assert_equal_uint64 = func(expected: uint64, actual: uint64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L48)
  Checks exact unsigned 64-bit equality.
- **Test** · [`export let assert_equal_bool = func(expected: bool, actual: bool)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L55)
  Checks that both Boolean values agree.
- **Test** · [`export let assert_equal_string = func(expected: string, actual: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L62)
  Checks that both strings contain the same UTF-8 bytes.
- **Test** · [`export let assert_equal_float64 = func(expected: float64, actual: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L70)
  Equal bit patterns, except that every NaN equals every NaN; 0.0 and -0.0 differ (Stdlib.md section 5.2).
- **Test** · [`export let assert_float_near = func(expected: float64, actual: float64, tolerance: float64)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L77)
  Fails when the absolute difference exceeds the tolerance or either value is NaN.
- **Test** · [`export let assert_equal = func<T>(expected: T, actual: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L88)
  Checks equality with the type's equals operation.
- **Test** · [`export let assert_not_equal = func<T>(unexpected: T, actual: T)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L95)
  Fails when the type's equals operation reports equality.
- **Test** · [`export let assert_some = func<T>(o: optional.Optional<T>) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L102)
  Returns the payload of Some or fails the test on None.
- **Test** · [`export let assert_none = func<T>(o: optional.Optional<T>)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L114)
  Fails the test if the optional contains a payload.
- **Test** · [`export let assert_ok = func<T>(r: Result<T>) -> T`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L121)
  Returns a successful payload or fails the test with the error.
- **Test** · [`export let assert_err = func<T>(r: Result<T>) -> error.Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/test.nra#L133)
  Returns the error payload or fails the test on success.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.1 Failure model

There are two kinds of failure and they never mix.

**Returned errors.** A function whose failure depends on input text, input bytes, or the outside world returns `Result<T>` ([section 6.2](Stdlib-Design#62-resultt)). The caller reads it with `match`. There is no propagation shorthand and no exception; passing an error up is an explicit `match` arm that returns it.

**Traps.** A function whose precondition is violated ends the run. The run stops at once with process status 70; no library or user code runs afterwards; nothing can observe or recover it ([Errors](https://github.com/Kayyo321/neura/wiki/Errors)). The module `Failure` in `Std.Core` has one function:

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

**Decision: the library does not re-check what the language already checks.** An index outside a built-in list, an overflow, a zero divisor, a shift count out of range, and an out-of-range conversion trap in the language with their own reasons (1 and 2) and locations. A library member that would fail the same way lets the built-in operation trap and documents that it does. `ArrayList.get` with a bad index is a bounds trap from the list read, not a library trap.

**What the absence of unwinding changes.** The first plan assumed panics that unwind, deferred actions, and recovery. None exists, and this plan asks for none.

| First plan's promise | Now |
| --- | --- |
| `Closeables.use` closes the resource even when the body panics | Removed. A body cannot fail and return; it either returns or the run ends. Callers call `close` on each return path. When a run ends, by return or by trap, the host releases every file and socket. |
| "If the predicate of `remove_if` panics, the collection is left valid" | Removed. A trap inside a callback ends the run, so no state can be observed afterwards. No collection keeps recovery code. |
| `Iterator.next` panics with `NoSuchElementError`; modification panics with `ConcurrentModificationError` | Both are traps with the messages above. The error classes for programmer mistakes are gone. |
| `assert_panics` recovers a panic and returns its error | Removed. A test that expects a trap is a program with an expected-diagnostic file, run by the test harness as the existing trap tests under `tests/backend/traps` are ([section 17](Stdlib-Design#17-verification)). |
| A fallible callback | A callback that can fail returns a `Result`, and the member that calls it documents how it stops. |

### 14.2 `Std.Test`

This module replaces the built-in `Test` as described in [section 4.6](Stdlib-Design#46-retiring-sysio-and-test). An assertion builds its message in Neura and calls `Failure.trap`. The runner reports the message from the trapping test.

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
- A failed assertion ends its test, as today. Tests are discovered and run as [Testing](https://github.com/Kayyo321/neura/wiki/Testing) describes; only the import changes.

### 18.2 Program, console, and testing

- **Entry.** `Std.Program` holds `ExitCode`, `Entry`, `Environment`, and `Process` as one module. A `Status` outside 0 through 255 traps with `invalid argument: exit status`.
- **Process exit.** `Process.exit` is available without a capability. A test that exits with a nonzero status fails.
- **Std.Test messages.** `assert_equal_float64` compares with float `equals` (NaN equals NaN, `0.0` differs from `-0.0`). `assert_none` and `assert_err` report `expected None, got Some` and `expected Err, got Ok` without the value's text, so they work for payload enums. `assert_not_equal` reports `expected a value other than <x>`.
- **Console.** Output is unbuffered. Input is read in 4,096-byte requests shared by `read_line` and `input()`. A write that makes no progress is an `Other` error.
