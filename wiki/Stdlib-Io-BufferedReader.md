# `Std.Io.BufferedReader`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Io.BufferedReader`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Io.BufferedReader`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BufferedReader** · [`export const DefaultCapacity: int = 8192`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L12)
  The default buffer capacity in bytes.
- **BufferedReader** · [`export const MinimumCapacity: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L14)
  The smallest supported buffer capacity in bytes.
- **BufferedReader** · [`export let BufferedReader = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L17)
  Wraps a byte source and retains unread bytes between reads.
- **BufferedReader.BufferedReader** · [`export let init = func(input: func(int) -> Result<uint8[list]>, size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L28)
  Use create or with_capacity.
- **BufferedReader.BufferedReader** · [`export let read = func(max: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L37)
  Up to `max` bytes, at least one unless the source has ended. A maximum below 1 traps.
- **BufferedReader.BufferedReader** · [`export let read_exact = func(count: int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L67)
  Exactly `count` bytes; the source ending first fails with UnexpectedEnd. A negative count traps.
- **BufferedReader.BufferedReader** · [`export let read_all = func -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L89)
  Every remaining byte.
- **BufferedReader** · [`export let create = func(source: func(int) -> Result<uint8[list]>) -> BufferedReader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L112)
  A reader holding up to 8,192 bytes.
- **BufferedReader** · [`export let with_capacity = func(source: func(int) -> Result<uint8[list]>, capacity: int) -> BufferedReader`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-reader.nra#L116)
  A capacity below 16 traps with "invalid argument: capacity below 16".
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 11.1 `Std.Io`

**`Console`** is the module that replaces `SysIo.println`. It needs P2.

| Member | Behavior | Also needs |
| --- | --- | --- |
| `print_line(s: string)` | Writes the text and one LF through `neura.console.write_line`. A failed write traps with `console write failed`, as `SysIo.println` does today. | F1 for the message |
| `try_print_line(s: string) → Result<Unit>` | The same write, returning the failure. | |
| `write(s)`, `error_line(s)`, and their `try_` forms | Write raw text through `neura.console.write`, to output or to the error stream. | T1 (text to bytes) |
| `format(pattern, args...)`, `vformat(pattern, args)`, and their `try_` forms | Render a [format string](https://github.com/Kayyo321/neura/wiki/StandardLibrary#console-formatting) to UTF-8 without calling a host service. | |
| `print`, `println`, `eprint`, `eprintln`, and their `try_` forms | Format first and then write to output or to the error stream. The newline forms append one LF. | T1, P2 |
| `read_line() → Result<Optional<string>>` | A line without its terminator; `None` at the end of input. | T1 |
| `output() → ByteSink`, `error() → ByteSink`, `input() → ByteSource` | The console as streams. | |

**Decision:** console output is unbuffered in the library. Each completed format is sent in program order through the existing raw write paths. The byte paths repeat partial writes until they finish or return an error.

**Streams are function values.** The first plan declared interfaces (`ByteReader`, `ByteWriter`, `TextReader`, `TextWriter`) so that a buffer could wrap any reader. Each of those is one operation with a concrete signature, which is what a native function value is, and a bound method supplies one from any object. No interface is needed and nothing here waits for G4.

| Shape | Type | Contract |
| --- | --- | --- |
| `ByteSource` | `(int) → Result<uint8[list]>` | Called with a maximum count of at least 1. Returns a new list of 1 through that many bytes, or an empty list at the end of the stream. |
| `ByteSink` | `(uint8[list]) → Result<Unit>` | Writes every byte, repeating partial host writes. Does not keep the list. |
| `TextSink` | `(string) → Result<Unit>` | Writes the string. |

The three names are this page's shorthand. A type overlay cannot name a function type today (probed), so library source spells each type out. Reading the method `read` of a `File` as a value binds the file and gives a `ByteSource`; a closure over anything else gives one too. The probe passed a bound method of a class to a function expecting a byte source and drained it, in both build modes. Closing is not part of a stream: the code that opened a resource closes it.

| Class | Behavior | Needs |
| --- | --- | --- |
| `ByteBuffer` | An in-memory accumulator over a built-in `uint8` list. `write` (a `ByteSink` when bound), `to_list()`, `size()`, `clear()`, and `reader()`, a `ByteSource` over a copy. Equality, hashing, and text use the current ordered bytes. A reader's position is independent of its buffer, and mutating a buffer used as a hash key invalidates that key's stored hash. | Today |
| `BufferedReader` | Built from a `ByteSource`. Holds up to 8,192 bytes (configurable, at least 16). `read` is a `ByteSource`; `read_exact(count) → Result<uint8[list]>` fails with kind `UnexpectedEnd`; `read_all()`. | Today |
| `BufferedWriter` | Built from a `ByteSink`. Collects up to 8,192 bytes and passes them on when full and on `flush() → Result<Unit>`. It does not close anything. | Today |
| `Utf8Reader` | Built from a `ByteSource`. `read_scalar() → Result<Optional<uint32>>`, `read_line() → Result<Optional<string>>`, `read_to_end() → Result<string>`. Invalid UTF-8 fails with kind `InvalidData`. | T1 |
| `Utf8Writer` | Built from a `ByteSink`. `write` is a `TextSink`; `write_line(s)` adds one LF. | T1 |

`read_line` ends a line at LF and removes it, along with a CR immediately before it. A final line without an LF is returned; after that the result is `None`.

### 18.6 Host services, files, and networking

- **Signatures.** `neura.fs.stat` takes a follow-links flag so that `remove_tree` never descends through a symbolic link, `neura.fs.remove` takes a directory flag so that `remove_file` and `remove_directory` need no separate stat, and `neura.net.local_port` reads a listener's port. `File.size` is three seeks.
- **Errors.** Other failures use code 100. `EADDRINUSE` is `AlreadyExists`; `EAGAIN` and `ETIMEDOUT` are `TimedOut`; `EPIPE`, `ECONNABORTED`, and `ENOTCONN` are `ConnectionReset`; a name that does not resolve is `NotFound`. A read or entropy request is at most 1 MiB. Network errors carry `host:port` in the error's path.
- **Path.** `components` starts with `/` for an absolute path; `.` has no components. `file_name` is `None` for `/`, `.`, and a path ending in `..`. `starts_with` and `relative_to` need both paths absolute or both relative.
- **Buffers.** `BufferedReader` passes a large read straight through when its buffer is empty; `BufferedWriter` drops its buffer when the sink fails. `Utf8Reader.read_scalar` leaves the byte that broke a sequence unread.
- **Trap details.** `maximum count below 1`, `capacity below 16`, `negative count`, `port`, and `negative timeout` after `invalid argument: `.
- **Lifetime.** File and socket IDs count from 1 in each run; the runner closes what a run left open when it ends, and after each test.

### Task safety

A function that a spawned task calls must be task-safe ([Concurrency, section 5.2](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#52-task-safe-calls)). `read`, `read_exact`, and `read_all` call the reader's byte source, which is a function value, and the compiler judges such a call by every function value of that type the program can create. `Std.Io.Console` provides one that is not task-safe, the source that `Console.input()` returns. In a program that links `Std.Io.Console`, these three functions are therefore not task-safe whatever source the reader was built from, and a spawned task reads a stream with the stream's own `read`. `create` and `with_capacity` are task-safe. [Task safety of the library](StandardLibrary#task-safety) lists every exception, and `neura check --task-safety` reports the answer for any function.
