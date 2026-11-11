# `Std.Io.Console`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Io.Console`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Io.Console`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)
- [Console and string formatting grammar](StandardLibrary#console-formatting)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Console** · [`export let try_vformat = func(pattern: string, args: Argument[]) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L47)
  Explicit argument arrays support forwarding and runtime-built packs. Tagged `#NoDiscard`.
- **Console** · [`export let vformat = func(pattern: string, args: Argument[]) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L51)
  Formats a prebuilt argument array into text. Invalid formats trap.
- **Console** · [`export let try_format = func(pattern: string, args: ...) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L56)
  Formats variadic arguments into text or returns a parse error. Tagged `#NoDiscard`.
- **Console** · [`export let format = func(pattern: string, args: ...) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L60)
  Formats variadic arguments into text. Invalid formats trap.
- **Console** · [`export let Formatter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L64)
  Exposes text formatting through a static method. Tagged `#Final`.
- **Console.Formatter** · [`export static let format = func(pattern: string, args: ...) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L66)
  Formats variadic arguments into text with the console grammar. Invalid formats trap.
- **Console** · [`export let print_line = func(s: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L71)
  Writes the text and one LF. A failed write traps with "console write failed". Tagged `#ThreadSafe`.
- **Console** · [`export let try_print_line = func(s: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L82)
  The same write, returning its failure. The service's error code 1 maps to Other. Tagged `#NoDiscard`.
- **Console** · [`export let print = func(pattern: string, args: ...)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L89)
  Formatted output to stdout and stderr, with or without a final LF. Formats the arguments and writes to stdout. A formatting or write error traps.
- **Console** · [`export let try_print = func(pattern: string, args: ...) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L94)
  Formats the arguments and writes to stdout, returning a formatting or write error. Tagged `#NoDiscard`.
- **Console** · [`export let println = func(pattern: string, args: ...)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L98)
  Formats the arguments and writes to stdout with one LF. Errors trap.
- **Console** · [`export let try_println = func(pattern: string, args: ...) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L103)
  Formats the arguments and writes to stdout with one LF, returning any error. Tagged `#NoDiscard`.
- **Console** · [`export let eprint = func(pattern: string, args: ...)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L107)
  Formats the arguments and writes to stderr. Errors trap.
- **Console** · [`export let try_eprint = func(pattern: string, args: ...) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L112)
  Formats the arguments and writes to stderr, returning any error. Tagged `#NoDiscard`.
- **Console** · [`export let eprintln = func(pattern: string, args: ...)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L116)
  Formats the arguments and writes to stderr with one LF. Errors trap.
- **Console** · [`export let try_eprintln = func(pattern: string, args: ...) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L121)
  Formats the arguments and writes to stderr with one LF, returning any error. Tagged `#NoDiscard`.
- **Console** · [`export let write = func(s: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L125)
  Writes raw text to stdout without interpreting format fields or adding a newline.
- **Console** · [`export let try_write = func(s: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L129)
  Writes raw UTF-8 bytes to stdout and returns any write error. Tagged `#NoDiscard`.
- **Console** · [`export let error_line = func(s: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L142)
  Writes the text and one LF to the error stream. A failed write traps with "console write failed".
- **Console** · [`export let try_error_line = func(s: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L146)
  Writes raw text and one LF to stderr, returning any write error. Tagged `#NoDiscard`.
- **Console** · [`export let read_line = func -> result.Result<Optional<string>>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L153)
  The next line of input without its LF or a CR before it; None at the end of input. A final line without an LF is returned. Invalid UTF-8 fails with InvalidData and consumes the line. Tagged `#NoDiscard`.
- **Console** · [`export let output = func -> func(uint8[list]) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L189)
  The output stream as a byte sink. Tagged `#Pure`, `#ThreadSafe`.
- **Console** · [`export let error = func -> func(uint8[list]) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L194)
  The error stream as a byte sink. Tagged `#Pure`, `#ThreadSafe`.
- **Console** · [`export let input = func -> func(int) -> result.Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/console.nra#L198)
  Console input as a byte source. It first returns input that read_line read ahead.
<!-- END SOURCE API -->

## Current text and formatting contracts

### Console formatting

`Console.format(pattern, args...)` and the static `Console.Formatter.format` return a UTF-8 string. `print` and `println` format to stdout, and `eprint` and `eprintln` format to stderr. The two newline forms append one LF. The module functions have `try_` counterparts that return a `Result` containing the string or `Unit`. For an existing `Std.Core.Variadic.Argument[]`, use `vformat` or `try_vformat`. Use `write`, `print_line`, or `error_line` for already-formatted text that may contain literal braces.

```neura
let console = import:Std.Io.Console;

console.println("{}, {}", "hello", "Neura");           // hello, Neura
console.println("{{{1}}}: {0:#06x}", 42, "value");     // {value}: 0x002a
let label = console.format("{:*^7} {:+.2f}", "ok", 1.25); // **ok*** +1.25
```

`{}` uses the next argument, and `{0}`, `{1}`, and so on use zero-based indexes. A pattern uses one indexing mode throughout. Explicit indexes may repeat and unused arguments are permitted. Write `{{` and `}}` for single literal braces. A colon in a field introduces its format specification.

| Option | Behavior |
| --- | --- |
| Fill and alignment | `<` left, `>` right, `^` center. An optional Unicode scalar before the alignment symbol replaces the default space. Text defaults to left alignment and numbers to right alignment. |
| Numeric sign and padding | `+` forces a sign, `-` is the default, and a space reserves the positive sign. `#` adds a radix prefix or forces a decimal point. `0` pads after the sign and radix prefix when alignment is implicit. |
| Width and precision | A decimal width sets a minimum field size. `.precision` limits text to that many Unicode scalars or controls decimal digits for floats. Width is measured in Unicode scalars. A nested field such as `{:>{}}` or `{0:.{1}f}` takes its width or precision from a nonnegative integer argument. |
| Integer presentation | `d` decimal, `b` or `B` binary, `o` octal, `x` or `X` hexadecimal, and `c` a Unicode scalar. Booleans also support the numeric presentations other than `c`. |
| Float presentation | `f` fixed, `e` scientific, `g` significant-digit formatting, and `a` hexadecimal. Uppercase variants capitalize the result. Decimal presentations default to six digits, while hexadecimal omits trailing zeros by default. All accept precision up to 100. The default presentation uses the argument type's shortest round-trip text when precision is omitted. |
| Text presentation | `s` inserts text. `?` quotes and escapes text. Objects use their `to_string()` method and a nil reference renders as `nil`. Booleans default to `true` and `false`. |

Format strings are checked at runtime and may come from any string expression. Invalid formats return an `Error.Parse` with a zero-based byte offset from the `try_` forms, or trap from the other forms. The library finishes formatting before sending anything to a stream, so a format error produces no partial output. Output uses the same host services and capability as raw console writes.

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

### 11.1 `Std.Io`

**`Console`** is the module that replaces `SysIo.println`. It needs P2.

| Member | Behavior | Also needs |
| --- | --- | --- |
| `print_line(s: string)` | Writes the text and one LF through `neura.console.write_line`. A failed write traps with `console write failed`. | F1 for the message |
| `try_print_line(s: string) → Result<Unit>` | The same write, returning the failure. | |
| `write(s)`, `error_line(s)`, and their `try_` forms | Write raw text through `neura.console.write`, to output or to the error stream. | T1 (text to bytes) |
| `format(pattern, args...)`, `vformat(pattern, args)`, and their `try_` forms | Render a [format string](StandardLibrary#console-formatting) to UTF-8 without calling a host service. | |
| `print`, `println`, `eprint`, `eprintln`, and their `try_` forms | Format first and then write to output or to the error stream. The newline forms append one LF. | T1, P2 |
| `read_line() → Result<Optional<string>>` | A line without its terminator; `None` at the end of input. | T1 |
| `output() → ByteSink`, `error() → ByteSink`, `input() → ByteSource` | The console as streams. | |

Console output is unbuffered in the library. Each completed format is sent in program order through the existing raw write paths. The byte paths repeat partial writes until they finish or return an error.

**Streams are function values.** A reader or writer is one operation with a concrete signature, which is what a native function value is, and a bound method supplies one from any object. The library declares no `ByteReader`, `ByteWriter`, `TextReader`, or `TextWriter` interface.

| Shape | Type | Contract |
| --- | --- | --- |
| `ByteSource` | `(int) → Result<uint8[list]>` | Called with a maximum count of at least 1. Returns a new list of 1 through that many bytes, or an empty list at the end of the stream. |
| `ByteSink` | `(uint8[list]) → Result<Unit>` | Writes every byte, repeating partial host writes. Does not keep the list. |
| `TextSink` | `(string) → Result<Unit>` | Writes the string. |

The three names are this page's shorthand. A type overlay cannot name a function type, so library source spells each type out. Reading the method `read` of a `File` as a value binds the file and gives a `ByteSource`; a closure over anything else gives one too. Closing is not part of a stream: the code that opened a resource closes it.

| Class | Behavior | Needs |
| --- | --- | --- |
| `ByteBuffer` | An in-memory accumulator over a built-in `uint8` list. `write` (a `ByteSink` when bound), `to_list()`, the read-only field `size`, `clear()`, and `reader()`, a `ByteSource` over a copy. Equality, hashing, and text use the current ordered bytes. A reader's position is independent of its buffer, and mutating a buffer used as a hash key invalidates that key's stored hash. | None |
| `BufferedReader` | Built from a `ByteSource`. Holds up to 8,192 bytes (configurable, at least 16). `read` is a `ByteSource`; `read_exact(count) → Result<uint8[list]>` fails with kind `UnexpectedEnd`; `read_all()`. | None |
| `BufferedWriter` | Built from a `ByteSink`. Collects up to 8,192 bytes and passes them on when full and on `flush() → Result<Unit>`; the read-only field `buffered` is the number of bytes waiting. It does not close anything. | None |
| `Utf8Reader` | Built from a `ByteSource`. `read_scalar() → Result<Optional<uint32>>`, `read_line() → Result<Optional<string>>`, `read_to_end() → Result<string>`. Invalid UTF-8 fails with kind `InvalidData`. | T1 |
| `Utf8Writer` | Built from a `ByteSink`. `write` is a `TextSink`; `write_line(s)` adds one LF. | T1 |

`read_line` ends a line at LF and removes it, along with a CR immediately before it. A final line without an LF is returned; after that the result is `None`.

### 14.1 `Std.Log`

`Logger`: its sink is a concrete function value and its text is built by concatenation.

- `LogLevel` is a fieldless enum, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`. Levels compare by ordinal.
- **The sink is a function value** of type `(string) → void`, called once per accepted record with the finished line, without a line terminator. `Console.print_line` is a sink. A closure that appends to a captured list is a sink for tests; because a captured list is shared, the test reads the list afterwards. A sink that can fail handles its own failure, typically by counting it in a captured `Cell`.
- Module functions `create(name: string, sink, level: LogLevel)` and `create_timed(name, sink, level, clock: Clock)`.
- Each record is one line: with a clock, the instant's text and a space; then the level in uppercase, a space, the name, `: `, and the message. If the clock fails, the timestamp is `-`.
- Without a clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, make: () → string)` (the function runs only when the level is enabled), `is_enabled(level)`, and the field `level`, which a program assigns to change the logger's level.
- There is no global logger registry. A logger is passed to the code that uses it.

### 18.2 Program, console, and testing

- **Entry.** `Std.Program` holds `ExitCode`, `Entry`, `Environment`, and `Process` as one module. A `Status` outside 0 through 255 traps with `invalid argument: exit status`.
- **Process exit.** `Process.exit` is available without a capability. A test that exits with a nonzero status fails.
- **Std.Test messages.** `assert_equal_float64` compares with float `equals` (NaN equals NaN, `0.0` differs from `-0.0`). `assert_none` and `assert_err` report `expected None, got Some` and `expected Err, got Ok` without the value's text, so they work for payload enums. `assert_not_equal` reports `expected a value other than <x>`.
- **Console.** Output is unbuffered. Input is read in 4,096-byte requests shared by `read_line` and `input()`. A write that makes no progress is an `Other` error.

### Task safety

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). `read_line` and the byte source that `input()` returns are not: the read-ahead buffer is a module binding that belongs to the root task, so only the root task reads standard input. Every other function of this module, output and formatting included, is task-safe. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.
