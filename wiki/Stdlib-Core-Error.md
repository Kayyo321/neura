# `Std.Core.Error`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Core.Error`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Core.Error`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Error** · [`export let IoErrorKind = enum ( NotFound, PermissionDenied, AlreadyExists, InvalidInput, Interrupted, UnexpectedEnd, NotADirectory, IsADirectory, DirectoryNotEmpty, ConnectionRefused, ConnectionReset, TimedOut, Unsupported, CapabilityDenied, Closed, InvalidData, Other, Cancelled )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L11)
  Host error codes 1 through 14 map to the first fourteen kinds in order. The library produces Closed and InvalidData itself. Host error code 15 maps to Cancelled, the answer of a cancellable host request (a read, write, accept, connect, or sleep in a task whose scope was cancelled) that was cancelled before it finished. Cancelled comes last so that the kinds before it keep their order.
- **Error** · [`export let ParseErrorKind = enum (Empty, InvalidCharacter, OutOfRange, UnexpectedEnd, Syntax)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L18)
  Classifies malformed text, premature endings, and out-of-range values.
- **Error** · [`export let ErrorDetail = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L22)
  The base of errors defined outside the library. `code` is a stable identifier such as billing.card_declined.
- **Error.ErrorDetail** · [`export let code = func() -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L24)
  Returns the detail's application-defined code.
- **Error.ErrorDetail** · [`export let message = func() -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L26)
  Returns the detail's human-readable message.
- **Error** · [`export let SimpleDetail = class:ErrorDetail`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L30)
  Stores a custom error's code and message as strings.
- **Error.SimpleDetail** · [`export let init = func(code_value: string, message_value: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L37)
  Initializes both parts of the error detail.
- **Error.SimpleDetail** · [`export let.override code = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L44)
  Returns the stored error code.
- **Error.SimpleDetail** · [`export let.override message = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L47)
  Returns the stored detail message.
- **Error.SimpleDetail** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L50)
  Two simple details are equal when their code and message are.
- **Error.SimpleDetail** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L59)
  Combines the hashes of the code and message.
- **Error.SimpleDetail** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L63)
  Returns the formatted custom error detail.
- **Error** · [`export let Error = enum ( Io { kind: IoErrorKind, message: string, path: string }, Parse { kind: ParseErrorKind, offset: int, message: string }, Encoding { offset: int, message: string }, Custom(ErrorDetail), Context { message: string, cause: Error } )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L67)
  Represents an expected failure and its category-specific context.
- **Error** · [`export let PortableError = enum ( Io { kind: IoErrorKind, message: string, path: string }, Parse { kind: ParseErrorKind, offset: int, message: string }, Encoding { offset: int, message: string }, Custom { code: string, message: string }, Context { message: string, cause: PortableError } )`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L79)
  A transferable copy of an Error, so that a failure can cross a channel or a spawn (Concurrency.md section 5.1). It mirrors Error with one change: Custom, which holds an ErrorDetail object, is flattened to the detail's code and message texts. Convert with to_portable before sending and from_portable after receiving.
- **Error** · [`export let custom = func(code: string, text: string) -> Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L88)
  A Custom error holding a SimpleDetail.
- **Error** · [`export let context = func(cause: Error, text: string) -> Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L92)
  Wraps an error with a message that describes the operation that failed.
- **Error** · [`export let to_portable = func(e: Error) -> PortableError`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L97)
  The transferable copy of an error. A Custom error keeps its code and message but loses its ErrorDetail object, so a subclass's other members are gone; every other variant is copied as it is.
- **Error** · [`export let from_portable = func(p: PortableError) -> Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L109)
  The error a portable copy stands for. A Custom copy comes back holding a SimpleDetail with the same code and message, so message(from_portable(to_portable(e))) equals message(e).
- **Error** · [`export let io_kind_name = func(kind: IoErrorKind) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L120)
  Returns the spelling of an I/O error category.
- **Error** · [`export let parse_kind_name = func(kind: ParseErrorKind) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L123)
  Returns the spelling of a parse error category.
- **Error** · [`export let message = func(e: Error) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L126)
  Formats an error with its category and context.
- **Error** · [`export let root_cause = func(e: Error) -> Error`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L143)
  Follows Context causes to the end.
- **Error** · [`export let chain = func(e: Error) -> Error[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L158)
  The error, then each cause in order.
- **Error** · [`export let equals = func(a: Error, b: Error) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L177)
  Structural equality. Custom details compare with their equals member.
- **Error** · [`export let hash = func(e: Error) -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L221)
  Starts from the variant's position and combines each field's hash.
- **Error** · [`export let to_string = func(e: Error) -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/core/error.nra#L234)
  The same text as message.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.7 Conformance summary

These are the intended source-module contracts. Compiler support for the operations does not imply that a collection module or adapter is already packaged.

| Type | Equality and hash | Natural order | Iterable | Interfaces (G4) |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, fieldless enums, `string`, tuples | Built in (G3a) | Companion `compare` functions | No (`Strings.scalars` gives a list) | |
| Array, list | Built in, structural (G3a) | No | Built-in for-each | |
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Module functions; payload enums have no built-in equality | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `RegexFlags`, `RegexMatch`, `Regex` | By value | No | No | |
| `StringBuilder`, `ByteBuffer` | Current byte sequence | No | No | |
| `Sequence`, `Random`, `Logger`, stream adapters, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](Stdlib-Design#111-stdio) |

### 6.1 `Optional<T>`

**`Optional<T>` is a generic payload enum with the variants `None` and `Some(T)`, read with `match`.** The enum cannot be read without handling both cases, has no trapping accessor to misuse, and needs no late field.

Requires G2. The module `Optional` exports the enum and these functions; the last column says what each needs beyond G2.

| Function | Behavior | Also needs |
| --- | --- | --- |
| `is_some<T>(o) → bool`, `is_none<T>(o) → bool` | | |
| `value_or<T>(o, fallback: T) → T` | The value, or `fallback`. | |
| `expect<T>(o, detail: string) → T` | The value, or a trap with `no such element` followed by `: ` and `detail`. | |
| `to_result<T>(o, error: Error) → Result<T>` | `Ok` of the value, or `Err` of `error`. | |
| `map<T, U>(o, f: (T) → U) → Optional<U>` | | G1 |
| `and_then<T, U>(o, f: (T) → Optional<U>) → Optional<U>` | | G1 |
| `filter<T>(o, keep: (T) → bool) → Optional<T>` | | G1 |
| `value_or_else<T>(o, make: () → T) → T` | Calls `make` only for `None`. | G1 |
| `or_else<T>(o, other: () → Optional<T>) → Optional<T>` | `o` when it is `Some`, otherwise the result of `other`. | G1 |
| `equals<T>(a, b) → bool`, `hash<T>(o) → uint32`, `to_string<T>(o) → string` | Both `None`, or both `Some` with equal values. Hash 0 for `None`, otherwise `Hash.combine(1, value hash)`. Text `None` or `Some(` value `)`. | G3a |

The mapping functions are conveniences. A `match` is useful when the cases require different control flow or the caller wants to avoid a callback.

### 6.2 `Result<T>`

**`Result<T>` is a generic payload enum with the variants `Ok(T)` and `Err(Error)`.** It is the one representation of a fallible outcome. An operation with no value returns `Result<Unit>`. The error type is fixed to `Error` so that every fallible function composes with every other.

Requires G2. The module `Result` exports the enum and:

| Function | Behavior | Also needs |
| --- | --- | --- |
| `is_ok<T>(r) → bool`, `is_err<T>(r) → bool` | | |
| `value_or<T>(r, fallback: T) → T` | | |
| `expect<T>(r, detail: string) → T` | The value, or a trap whose message is `detail`, `: `, and the error's message. For failures the caller has decided are impossible. | |
| `to_optional<T>(r) → Optional<T>` | Discards the error. | |
| `with_context<T>(r, message: string) → Result<T>` | An `Err` becomes an `Err` of a `Context` error wrapping the original; an `Ok` is returned unchanged. | |
| `map<T, U>(r, f: (T) → U) → Result<U>` | | G1 |
| `and_then<T, U>(r, f: (T) → Result<U>) → Result<U>` | | G1 |
| `map_error<T>(r, f: (Error) → Error) → Result<T>` | | G1 |

There is no accessor for the error alone: `match` reads it.

### 6.3 `Error`

**`Error` is a payload enum.** Errors are ordinary values read with `match`. A custom error can hold an `ErrorDetail` reference, which is required to be non-nil.

| Variant | Fields | Meaning |
| --- | --- | --- |
| `Io` | `kind: IoErrorKind`, `message: string`, `path: string` | A host operation failed. `path` is empty when no path applies. |
| `Parse` | `kind: ParseErrorKind`, `offset: int`, `message: string` | Text does not match the expected grammar. `offset` is the byte offset of the first offending byte, or the input's length for an unexpected end. |
| `Encoding` | `offset: int`, `message: string` | Bytes are not valid in the expected encoding. |
| `Custom` | One `ErrorDetail` | An error defined outside the library. |
| `Context` | `message: string`, `cause: Error` | Another error with a description of what was being attempted. |

- `IoErrorKind` is a fieldless enum, in order: `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidInput`, `Interrupted`, `UnexpectedEnd`, `NotADirectory`, `IsADirectory`, `DirectoryNotEmpty`, `ConnectionRefused`, `ConnectionReset`, `TimedOut`, `Unsupported`, `CapabilityDenied`, `Closed`, `InvalidData`, `Other`, `Cancelled`. The first fourteen map from host error codes 1 through 14, and `Cancelled` from code 15, the answer of a cancelled version 2 request (Concurrency, section 10.4); it comes last so that the kinds before it keep their order. `Closed` and `InvalidData` are produced by the library.
- `ParseErrorKind`, in order: `Empty`, `InvalidCharacter`, `OutOfRange`, `UnexpectedEnd`, `Syntax`.
- **`ErrorDetail`** is an abstract class with two requirements, `code() → string` (a stable identifier such as `billing.card_declined`) and `message() → string`. User code defines its own error types by deriving from it; dynamic dispatch on a non-generic class works. It is the library's one class meant to be a base. `SimpleDetail` is the concrete subclass that stores both strings.
- Adding a variant to `Error` breaks every `match` without a wildcard arm, so the variant list changes only with a major version. Code that only reports errors should end its `match` with a wildcard arm that calls `Errors.message`.
- Errors are immutable. A cause is set at construction, so chains are finite and acyclic.
- **`PortableError`** is the transferable copy of an `Error`, for a failure that must cross a channel or a spawn. `Error` itself is not transferable, because `Custom` holds an `ErrorDetail` object ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#51-transferable-types)). `PortableError` has the same variants and fields with one change: `Custom` is `Custom { code: string, message: string }`, and the cause of its `Context` is a `PortableError`.

The module `Error` exports the enum, the two kind enums, `ErrorDetail`, `SimpleDetail`, and:

| Function | Behavior |
| --- | --- |
| `custom(code: string, message: string) → Error` | A `Custom` error holding a `SimpleDetail`. |
| `context(cause: Error, message: string) → Error` | A `Context` error. |
| `message(e: Error) → string` | `Io`: the kind's name, `: `, the message, and ` (` path `)` when the path is not empty. `Parse`: `parse error at ` offset `: ` message. `Encoding`: `invalid encoding at ` offset `: ` message. `Custom`: the detail's message. `Context`: the message, `: `, and the cause's message. |
| `root_cause(e: Error) → Error` | Follows `Context` causes to the end. |
| `chain(e: Error)` | A built-in list of `Error`: the error, then each cause. |
| `to_portable(e: Error) → PortableError` | The transferable copy. A `Custom` error keeps its detail's `code()` and `message()` and loses the object, so any other member of a subclass is gone. Every other variant is copied as it is, causes included. |
| `from_portable(p: PortableError) → Error` | The error a portable copy stands for. A `Custom` copy comes back holding a `SimpleDetail` with the same code and message, so `message(from_portable(to_portable(e)))` equals `message(e)`. |
| `io_kind_name(kind) → string`, `parse_kind_name(kind) → string` | The variant's name, written as a `match`. |

`message` uses only concatenation and integer text, so it works before T1.

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](Stdlib-Design#46-retired-built-ins)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, as every program does. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, `Status(int)`, and the named statuses `SeriousFailure` (2), `CommandNotFound` (127), `IllegalInstruction` (132), `Breakpoint` (133), `Abort` (134), `ArithmeticError` (136), `OutOfMemory` (137), `BusError` (138), `SegmentationFault` (139), `CpuTimeExceeded` (152), and `FileSizeExceeded` (153). `Ok`, `Failure`, and `Status` map to 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. A payload enum lets a program return any status without a host call. Neura enums cannot carry explicit integer values, so the compiler and `status_of(code) → int` hold the table; the named variants follow declaration order after `Status`, so the original three keep their ordinals.
- **`status_of(code: ExitCode) → int`** returns the status a variant stands for, trapping for an invalid `Status`.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. The overload `Process.exit(code: ExitCode)` exits with `status_of(code)`. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 14.1 `Std.Log`

`Logger`: its sink is a concrete function value and its text is built by concatenation.

- `LogLevel` is a fieldless enum, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`. Levels compare by ordinal.
- **The sink is a function value** of type `(string) → void`, called once per accepted record with the finished line, without a line terminator. `Console.print_line` is a sink. A closure that appends to a captured list is a sink for tests; because a captured list is shared, the test reads the list afterwards. A sink that can fail handles its own failure, typically by counting it in a captured `Cell`.
- Module functions `create(name: string, sink, level: LogLevel)` and `create_timed(name, sink, level, clock: Clock)`.
- Each record is one line: with a clock, the instant's text and a space; then the level in uppercase, a space, the name, `: `, and the message. If the clock fails, the timestamp is `-`.
- Without a clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, make: () → string)` (the function runs only when the level is enabled), `is_enabled(level)`, and `set_level(level)`.
- There is no global logger registry. A logger is passed to the code that uses it.

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
- A failed assertion ends its test. Tests are discovered and run as [Testing](https://github.com/Kayyo321/neura/wiki/Testing) describes; only the import changes.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.
