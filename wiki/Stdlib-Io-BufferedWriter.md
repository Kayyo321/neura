# `Std.Io.BufferedWriter`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Io.BufferedWriter`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Io.BufferedWriter`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **BufferedWriter** · [`export const DefaultCapacity: int = 8192`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L13)
  The default buffer capacity in bytes.
- **BufferedWriter** · [`export const MinimumCapacity: int = 16`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L15)
  The smallest supported buffer capacity in bytes.
- **BufferedWriter** · [`export let BufferedWriter = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L18)
  Wraps a byte sink and batches small writes.
- **BufferedWriter.BufferedWriter** · [`export let init = func(output: func(uint8[list]) -> Result<Unit>, size: int)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L27)
  Use create or with_capacity.
- **BufferedWriter.BufferedWriter** · [`export let write = func(data: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L36)
  Buffers small writes and forwards larger ones after flushing pending bytes.
- **BufferedWriter.BufferedWriter** · [`export let flush = func -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L53)
  Passes the buffered bytes to the sink.
- **BufferedWriter.BufferedWriter** · [`export let buffered = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L64)
  The number of bytes waiting for flush.
- **BufferedWriter** · [`export let create = func(sink: func(uint8[list]) -> Result<Unit>) -> BufferedWriter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L68)
  A writer collecting up to 8,192 bytes.
- **BufferedWriter** · [`export let with_capacity = func(sink: func(uint8[list]) -> Result<Unit>, capacity: int) -> BufferedWriter`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/buffered-writer.nra#L72)
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
