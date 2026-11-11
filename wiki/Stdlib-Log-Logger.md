# `Std.Log.Logger`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Log.Logger`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Log.Logger`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Logger** · [`export let LogLevel = enum (Trace, Debug, Info, Warn, Error, Off)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L11)
  In order; levels compare by ordinal. Off disables a logger and is never a record's level.
- **Logger** · [`export let Logger = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L14)
  A named logger with its own threshold, sink, and optional clock.
- **Logger.Logger** · [`export let init = func(name: string, target: func(string) -> void, level: LogLevel, time_source: Clock)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L25)
  Configures the logger's name, destination, threshold, and optional time source.
- **Logger.Logger** · [`export let is_enabled = func(level: LogLevel) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L34)
  True when a record at `level` would reach the sink: the level is not Off and is at least the logger's level.
- **Logger.Logger** · [`export let set_level = func(level: LogLevel)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L38)
  Changes the minimum level for future records.
- **Logger.Logger** · [`export let log = func(level: LogLevel, message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L44)
  Sends one record when the level is enabled: with a clock, the instant's text (or - when the clock fails) and a space; then the level in uppercase, a space, the name, ": ", and the message.
- **Logger.Logger** · [`export let log_lazy = func(level: LogLevel, make: func() -> string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L51)
  Calls `make` only when the level is enabled.
- **Logger.Logger** · [`export let trace = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L58)
  Logs a Trace record if that level is enabled.
- **Logger.Logger** · [`export let debug = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L61)
  Logs a Debug record if that level is enabled.
- **Logger.Logger** · [`export let info = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L64)
  Logs an Info record if that level is enabled.
- **Logger.Logger** · [`export let warn = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L67)
  Logs a Warn record if that level is enabled.
- **Logger.Logger** · [`export let error = func(message: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L70)
  Logs an Error record if that level is enabled.
- **Logger** · [`export let create = func(name: string, sink: func(string) -> void, level: LogLevel) -> Logger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L84)
  A logger without timestamps.
- **Logger** · [`export let create_timed = func(name: string, sink: func(string) -> void, level: LogLevel, source: Clock) -> Logger`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/log/logger.nra#L88)
  A logger whose records start with the clock's instant.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.6 Callbacks

- A callback parameter is written as a native function type. The library declares no named function shapes: a type overlay can be neither generic nor a function type, and the native spelling says everything a name would.
- **Concrete callbacks.** `Logger` takes a sink of type `(string) → void`, and any module can use function values whose types name no type parameter.
- **Generic callbacks specialize with their caller.** A member of `ArrayList<T>` can take `(T) → bool`. The G1 label in API tables identifies members that depend on this compiler facility.
- **Capture is by value.** A closure copies the locals it uses when it is created. A closure cannot count or accumulate in a captured local. It captures an object and mutates that. `Std.Core` provides `Cell<T>` ([section 6.5](Stdlib-Design#65-small-types)) for the purpose: a callback that sums captures a `Cell<int64>`.
- **Capture of a reference is shared.** A closure that captures a collection sees later changes to it, and changes it makes are visible outside. A callback passed to a collection's own member must not structurally modify that collection; the member compares the modification count after each call and traps on a change.
- Unless a member's description says otherwise, a callback may be called any number of times, in the order described.

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

### 14.1 `Std.Log`

`Logger`: its sink is a concrete function value and its text is built by concatenation.

- `LogLevel` is a fieldless enum, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`. Levels compare by ordinal.
- **The sink is a function value** of type `(string) → void`, called once per accepted record with the finished line, without a line terminator. `Console.print_line` is a sink. A closure that appends to a captured list is a sink for tests; because a captured list is shared, the test reads the list afterwards. A sink that can fail handles its own failure, typically by counting it in a captured `Cell`.
- Module functions `create(name: string, sink, level: LogLevel)` and `create_timed(name, sink, level, clock: Clock)`.
- Each record is one line: with a clock, the instant's text and a space; then the level in uppercase, a space, the name, `: `, and the message. If the clock fails, the timestamp is `-`.
- Without a clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, make: () → string)` (the function runs only when the level is enabled), `is_enabled(level)`, and `set_level(level)`.
- There is no global logger registry. A logger is passed to the code that uses it.

### 18.5 Time, randomness, and logging

- **Duration text.** `parse` accepts an optional sign, `P`, an optional day count, and `T` with hours, minutes, and seconds (seconds with up to nine fraction digits); days are read but never written.
- **Instant** normalizes the nanoseconds of `of_epoch_seconds` by floor division and writes every `int64` second count; `parse` accepts years -999,999 through 999,999.
- **ZoneOffset** holds whole minutes; hours and minutes share a sign, and `compare_to` orders by total seconds.
- **Clocks.** `Clock.sleep` returns at once for a zero or negative duration. `FixedClock.of(instant)` makes a fixed clock whose monotonic reading is the total of its advances and sleeps; `advance` traps for a negative duration. `Stopwatch.create(clock)`; `start` and `stop` return `Result<Unit>` and trap when repeated.
- **Calendar arithmetic** splits off 400-year cycles with one 64-bit division and works within a cycle in 32-bit arithmetic, which keeps images that format dates several megabytes smaller.
- **Random.** `seeded` and `from_entropy` are the only constructors; `from_entropy` fails with `InvalidData` when the host returns other than 8 bytes. **Uuid** stores two big-endian `uint64` halves and has `to_bytes`.
- **Trap details.** `invalid date`, `date out of range`, `invalid time`, `invalid zone offset`, `day of week`, `negative duration`, `bound must be positive`, `low must be below high`, and `negative count` after `invalid argument: `; `stopwatch is running` and `stopwatch is stopped` after `invalid state: `.
- **Logger.** `is_enabled(Off)` is false.
