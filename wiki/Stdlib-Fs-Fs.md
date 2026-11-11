# `Std.Fs.Fs`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Fs.Fs`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Fs.Fs`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Fs** · [`export let FileKind = enum (File, Directory, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L19)
  Distinguishes regular files, directories, and other entries.
- **Fs** · [`export let FileMetadata = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L24)
  What metadata reports about an entry. A link is followed; an entry that is neither a file nor a directory is of kind Other. The fields are read-only. Tagged `#Final`.
- **Fs.FileMetadata** · [`export last kind: FileKind`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L26)
  The classification reported by the filesystem provider.
- **Fs.FileMetadata** · [`export last size: int64`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L28)
  The reported size in bytes.
- **Fs.FileMetadata** · [`export last modified: instant.Instant`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L30)
  The last modification instant, or nil when unavailable.
- **Fs.FileMetadata** · [`export let init = func(entry_kind: FileKind, byte_size: int64, modified_at: instant.Instant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L33)
  Stores the entry's kind, size, and modification time.
- **Fs.FileMetadata** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L40)
  Tests equality of the kind, size, and modification time.
- **Fs.FileMetadata** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L50)
  Combines the hashes of the kind, size, and modification time.
- **Fs.FileMetadata** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L55)
  Formats the metadata as Kind(size, modified), with nil for an absent timestamp.
- **Fs** · [`export const CopyBufferSize: int = 65536`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L78)
  Bytes per read and write of copy_file and read_bytes.
- **Fs** · [`export let read_bytes = func(path: Path) -> result.Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L82)
  Reads all bytes from the path and closes the file before returning. Tagged `#NoDiscard`.
- **Fs** · [`export let read_text = func(path: Path) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L95)
  The file's text; invalid UTF-8 fails with InvalidData. Tagged `#NoDiscard`.
- **Fs** · [`export let write_bytes = func(path: Path, data: uint8[list]) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L112)
  Creates or truncates the file and writes the bytes. Tagged `#NoDiscard`.
- **Fs** · [`export let write_text = func(path: Path, text: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L118)
  Creates or truncates the file and writes the UTF-8 bytes of the text. Tagged `#NoDiscard`.
- **Fs** · [`export let append_text = func(path: Path, text: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L124)
  Creates the file if needed and writes the text at its end. Tagged `#NoDiscard`.
- **Fs** · [`export let exists = func(path: Path) -> result.Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L130)
  Ok(false) for NotFound; other failures are errors. Follows links. Tagged `#NoDiscard`.
- **Fs** · [`export let metadata = func(path: Path) -> result.Result<FileMetadata>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L139)
  The kind, size in bytes, and last modification time of an entry, following links. Tagged `#NoDiscard`.
- **Fs** · [`export let list = func(path: Path) -> result.Result<Path[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L148)
  The directory's entries joined to `path`, sorted by name bytes. Tagged `#NoDiscard`.
- **Fs** · [`export let create_directory = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L165)
  Creates a single directory. Missing parents and an existing entry are reported as errors. Tagged `#NoDiscard`.
- **Fs** · [`export let create_directories = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L171)
  Creates the directory and any missing parents; succeeds when the directory already exists. Tagged `#NoDiscard`.
- **Fs** · [`export let remove_file = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L202)
  Removes a file or another entry that is not a directory, such as a link (the link, not its target). Tagged `#NoDiscard`.
- **Fs** · [`export let remove_directory = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L208)
  Removes an empty directory. Tagged `#NoDiscard`.
- **Fs** · [`export let remove_tree = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L217)
  Removes `path` and, for a directory, everything below it: depth first, in sorted order. Links and other entries of kind Other are removed without descending into them. Stops at the first error. Tagged `#NoDiscard`.
- **Fs** · [`export let rename = func(from: Path, to: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L249)
  Renames or moves an entry, replacing an existing file at `to`. Tagged `#NoDiscard`.
- **Fs** · [`export let copy_file = func(from: Path, to: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L256)
  Copies a file's bytes through a 65,536-byte buffer, replacing an existing target. Both files are closed on every path. Tagged `#NoDiscard`.
<!-- END SOURCE API -->

## Detailed contracts

The following sections are taken from the [standard library specification](Stdlib-Design) and the module's source comments. The source and the [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Results that must be used.** The functions and methods of `Std.Io`, `Std.Fs`, and `Std.Net` whose `Result` can report a failed read, write, open, connection, or file system operation are tagged `#NoDiscard`, and so are the classes `File`, `TcpStream`, and `TcpListener`: a statement that drops such a result warns with `W0003` ([Tags](Tags#nodiscard)), and `let _ = ...;` drops one on purpose. `close()` is not tagged.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- **Scoped helpers.** `File.with_open<T>(path, mode, body: (File) → Result<T>) → Result<T>` and `TcpStream.with_connection<T>(host, port, body: (TcpStream) → Result<T>) → Result<T>` open the resource, run the body, and close the resource exactly once on every ordinary completion: a returned value, a returned error, and a return after the body's scope was cancelled. With tasks a body can end early by cancellation, and the close must still happen ([Concurrency](Concurrency#7-cancellation-deadlines-and-resources)).
  - If the open fails, the helper returns that error and does not call the body.
  - The body runs in a `scope` the helper owns, so every task started in that scope has finished before the resource is closed.
  - If the close succeeds, the helper returns the body's result unchanged. If the close fails after a body `Ok`, it returns the close error. If the close fails after a body `Err`, it returns a `Context` error whose cause is the body's error and whose message carries the close error's message: `closing the resource also failed: ` and that message from `with_open`, `close failed after the body failed: ` and that message from `with_connection`.
  - A file is closed even when the body closed it already, which reports no error. `with_connection` connects without a time limit.
  - A trap in the body ends the run without cleanup; the runner then closes what the run left open.
- **`Closeable.release<T>(resource: Closeable, outcome: Result<T>) → Result<T>`** is the closing rule on its own: it closes the resource and combines the result with the outcome of the work, as `with_open` does. A program that owns another kind of closeable resource calls it exactly once, after the work, on every ordinary completion.
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 11.2 `Std.Fs`

Needs P2 for every host operation and T1 for `Path`.

**`Path`** is an immutable class holding normalized text.

- The separator is `/` on every host. The host service translates.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- The read-only field `is_absolute` is true exactly when the text starts with `/`. Platform prefixes such as drive letters are passed to the host unchanged and are not interpreted.
- Module function `of(text)`. Members include `join(other: string) → Path`, `parent() → Path`, `file_name()`, `stem()`, `extension()`, `components()`, `resolve_lexically() → Path`, `starts_with(other: Path) → bool`, and `relative_to(base: Path) → Path`. An absolute argument to `join` replaces the path. `parent` returns `nil` when the path has no parent. `relative_to` returns `nil` when it cannot express the path relative to the base. `file_name`, `stem`, and `extension` return `Optional<string>`. The extension is the text after the last `.` of the file name when that dot is not the first character. `components` returns a built-in list of strings. Equality, hash, ordering, and text use the normalized path.

**`File`** is a class wrapping a host file ID. The module functions `open(path, mode: OpenMode) → Result<File>` and `with_open<T>(path, mode, body)`, the scoped helper of [section 5.5](Stdlib-Design#55-resources). `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](Stdlib-Design#55-resources)). A `File` is unbuffered.

**`Fs`** module:

| Member | Behavior |
| --- | --- |
| `read_bytes(path) → Result<uint8[list]>`, `read_text(path) → Result<string>` | `read_text` fails with `InvalidData` for invalid UTF-8. Both close the file on every path, including a failed read. |
| `write_bytes(path, bytes)`, `write_text(path, text)`, `append_text(path, text)` `→ Result<Unit>` | Create the file if needed; the `write_` forms truncate. A failed write is reported after the file is closed. |
| `exists(path) → Result<bool>` | `Ok(false)` for `NotFound`; other failures are errors. |
| `metadata(path) → Result<FileMetadata>` | `FileMetadata` is a class with the read-only fields `kind` (`FileKind`: `File`, `Directory`, `Other`), `size: int64`, and `modified: Instant`. |
| `list(path)` | A `Result` of a built-in list of `Path`: the directory's entries joined to `path`, sorted by name bytes. |
| `create_directory(path)`, `create_directories(path)` | The second creates missing parents and succeeds when the directory exists. |
| `remove_file(path)`, `remove_directory(path)` | `remove_directory` requires an empty directory. |
| `remove_tree(path)` | Removes depth first, in sorted order, without descending into an entry of kind `Other`. Stops at the first error. |
| `rename(from, to)`, `copy_file(from, to)` | `copy_file` streams through a 65,536-byte buffer, replaces an existing target, and closes both files on every path. |

"On every path" is written out in each function as explicit `match` arms that close before returning the error. There is nothing to defer to.
