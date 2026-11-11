# `Std.Fs.File`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Fs.File`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Fs.File`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **File** · [`export let OpenMode = enum (Read, Write, Append, ReadWrite, CreateNew)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L17)
  Read: must exist. Write: create or truncate. Append: create, and write at the end. ReadWrite: must exist. CreateNew: read and write a new file; fails with AlreadyExists.
- **File** · [`export let SeekOrigin = enum (Start, Current, End)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L19)
  Selects the start, current position, or end as the reference point for seeking.
- **File** · [`export let File = class implements Closeable`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L41)
  An unbuffered host file with an explicit, idempotent close operation.
- **File.File** · [`export let init = func(opened: FileHandle)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L48)
  Use File.open; the handle comes from the library.
- **File.File** · [`export let read = func(max: int) -> result.Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L54)
  Up to `max` bytes from the current position; an empty list at the end of the file. A maximum below 1 traps.
- **File.File** · [`export let write = func(data: uint8[list]) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L66)
  Writes every byte, repeating partial host writes.
- **File.File** · [`export let seek = func(offset: int64, origin: SeekOrigin) -> result.Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L91)
  Moves the position and returns it, counted from the start.
- **File.File** · [`export let size = func -> result.Result<int64>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L102)
  The file's size in bytes. The position is unchanged.
- **File.File** · [`export let close = func -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L129)
  Releases the file. Closing again does nothing and returns Ok; the file is closed even when the host reports an error.
- **File** · [`export let open = func(path: Path, mode: OpenMode) -> result.Result<File>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L142)
  Opens the normalized path using the selected mode and returns an owned file.
- **File** · [`export let with_open = func<T>(path: Path, mode: OpenMode, body: func(File) -> result.Result<T>) -> result.Result<T>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/file.nra#L157)
  Opens the file, runs `body` with it in a scope this helper owns, and closes it exactly once on every ordinary completion (Concurrency.md section 7, "Resources"). If the open fails, returns that error and does not call the body. Otherwise returns the body's result, with two exceptions for a failed close: a body Err becomes a Context error whose cause is the body's error and whose message carries the close error's message, and a body Ok becomes the close error (Std.Io.Closeable.release does the combining). The file is closed even if the body closed it already, which reports no error. A trap in the body ends the run without cleanup, and the runner then closes the files the run left open. Needs the fs capability.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- **Scoped helpers.** `File.with_open<T>(path, mode, body: (File) → Result<T>) → Result<T>` and `TcpStream.with_connection<T>(host, port, body: (TcpStream) → Result<T>) → Result<T>` open the resource, run the body, and close the resource exactly once on every ordinary completion: a returned value, a returned error, and a return after the body's scope was cancelled. The earlier decision here was "there is no scoped helper"; it is reversed, because with tasks a body can end early by cancellation and the close must still happen ([Concurrency](https://github.com/Kayyo321/neura/blob/main/docs/lang/Concurrency.md#7-cancellation-deadlines-and-resources)).
  - If the open fails, the helper returns that error and does not call the body.
  - The body runs in a `scope` the helper owns, so every task started in that scope has finished before the resource is closed.
  - If the close succeeds, the helper returns the body's result unchanged. If the close fails after a body `Ok`, it returns the close error. If the close fails after a body `Err`, it returns a `Context` error whose cause is the body's error and whose message carries the close error's message: `closing the resource also failed: ` and that message from `with_open`, `close failed after the body failed: ` and that message from `with_connection`.
  - A file is closed even when the body closed it already, which reports no error. `with_connection` connects without a time limit.
  - A trap in the body ends the run without cleanup; the runner then closes what the run left open.
- **`Closeable.release<T>(resource: Closeable, outcome: Result<T>) → Result<T>`** is the closing rule on its own: it closes the resource and combines the result with the outcome of the work, as `with_open` does. A program that owns another kind of closeable resource calls it exactly once, after the work, on every ordinary completion.
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 6.4 Concrete aliases and absence

Define `Optional<T>` and `Result<T>` directly as generic payload enums. A concrete public name such as `File.Result` can be a type overlay of `Result<File>`. Reuse the same `Ok` and `Err` variants and payload types through the overlay.

Use `nil` for an internal absent reference when it cannot be confused with a real value, such as a missing parent link or an unused iterator reference. Do not replace `Optional<T>` with a nil convention on a generic return, or on an API where a present nil reference must be distinguishable from absence. JSON `Null` remains its own payload enum variant.

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

### 11.2 `Std.Fs`

Needs P2 for every host operation and T1 for `Path`.

**`Path`** is an immutable class holding normalized text.

- The separator is `/` on every host. The host service translates.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- `is_absolute()` is true exactly when the text starts with `/`. Platform prefixes such as drive letters are passed to the host unchanged and are not interpreted.
- Module function `of(text)`. Members include `join(other: string) → Path`, `parent() → Path`, `file_name()`, `stem()`, `extension()`, `components()`, `resolve_lexically() → Path`, `starts_with(other: Path) → bool`, and `relative_to(base: Path) → Path`. An absolute argument to `join` replaces the path. `parent` returns `nil` when the path has no parent. `relative_to` returns `nil` when it cannot express the path relative to the base. `file_name`, `stem`, and `extension` return `Optional<string>`. The extension is the text after the last `.` of the file name when that dot is not the first character. `components` returns a built-in list of strings. Equality, hash, ordering, and text use the normalized path.

**`File`** is a class wrapping a host file ID. The module functions `open(path, mode: OpenMode) → Result<File>` and `with_open<T>(path, mode, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](Stdlib-Design#55-resources)). A `File` is unbuffered.

**`Fs`** module:

| Member | Behavior |
| --- | --- |
| `read_bytes(path) → Result<uint8[list]>`, `read_text(path) → Result<string>` | `read_text` fails with `InvalidData` for invalid UTF-8. Both close the file on every path, including a failed read. |
| `write_bytes(path, bytes)`, `write_text(path, text)`, `append_text(path, text)` `→ Result<Unit>` | Create the file if needed; the `write_` forms truncate. A failed write is reported after the file is closed. |
| `exists(path) → Result<bool>` | `Ok(false)` for `NotFound`; other failures are errors. |
| `metadata(path) → Result<FileMetadata>` | `FileMetadata` is a class: `kind` (`FileKind`: `File`, `Directory`, `Other`), `size: int64`, `modified: Instant`. |
| `list(path)` | A `Result` of a built-in list of `Path`: the directory's entries joined to `path`, sorted by name bytes. |
| `create_directory(path)`, `create_directories(path)` | The second creates missing parents and succeeds when the directory exists. |
| `remove_file(path)`, `remove_directory(path)` | `remove_directory` requires an empty directory. |
| `remove_tree(path)` | Removes depth first, in sorted order, without descending into an entry of kind `Other`. Stops at the first error. |
| `rename(from, to)`, `copy_file(from, to)` | `copy_file` streams through a 65,536-byte buffer, replaces an existing target, and closes both files on every path. |

"On every path" is written out in each function as explicit `match` arms that close before returning the error. There is nothing to defer to.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](https://github.com/Kayyo321/neura/wiki/ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.

### 18.6 Host services, files, and networking

- **Signatures.** `neura.fs.stat` takes a follow-links flag so that `remove_tree` never descends through a symbolic link, `neura.fs.remove` takes a directory flag so that `remove_file` and `remove_directory` need no separate stat, and `neura.net.local_port` reads a listener's port. `File.size` is three seeks.
- **Errors.** Other failures use code 100. `EADDRINUSE` is `AlreadyExists`; `EAGAIN` and `ETIMEDOUT` are `TimedOut`; `EPIPE`, `ECONNABORTED`, and `ENOTCONN` are `ConnectionReset`; a name that does not resolve is `NotFound`. A read or entropy request is at most 1 MiB. Network errors carry `host:port` in the error's path.
- **Path.** `components` starts with `/` for an absolute path; `.` has no components. `file_name` is `None` for `/`, `.`, and a path ending in `..`. `starts_with` and `relative_to` need both paths absolute or both relative.
- **Buffers.** `BufferedReader` passes a large read straight through when its buffer is empty; `BufferedWriter` drops its buffer when the sink fails. `Utf8Reader.read_scalar` leaves the byte that broke a sequence unread.
- **Trap details.** `maximum count below 1`, `capacity below 16`, `negative count`, `port`, and `negative timeout` after `invalid argument: `.
- **Lifetime.** File and socket IDs count from 1 in each run; the runner closes what a run left open when it ends, and after each test.
