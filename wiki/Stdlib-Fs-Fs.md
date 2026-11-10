# `Std.Fs.Fs`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Fs.Fs`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Fs.Fs`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Fs** · [`export let FileKind = enum (File, Directory, Other)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L18)
  Distinguishes regular files, directories, and other entries.
- **Fs** · [`export let FileMetadata = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L22)
  What metadata reports about an entry. A link is followed; an entry that is neither a file nor a directory is of kind Other.
- **Fs.FileMetadata** · [`export let kind: FileKind = FileKind.Other`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L24)
  The classification reported by the filesystem provider.
- **Fs.FileMetadata** · [`export let size: int64 = 0`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L26)
  The reported size in bytes.
- **Fs.FileMetadata** · [`export let modified: instant.Instant = nil`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L28)
  The last modification instant, or nil when unavailable.
- **Fs.FileMetadata** · [`export let init = func(entry_kind: FileKind, byte_size: int64, modified_at: instant.Instant)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L31)
  Stores the entry's kind, size, and modification time.
- **Fs.FileMetadata** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L38)
  Tests equality of the kind, size, and modification time.
- **Fs.FileMetadata** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L48)
  Combines the hashes of the kind, size, and modification time.
- **Fs.FileMetadata** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L53)
  Formats the metadata as Kind(size, modified), with nil for an absent timestamp.
- **Fs** · [`export const CopyBufferSize: int = 65536`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L76)
  Bytes per read and write of copy_file and read_bytes.
- **Fs** · [`export let read_bytes = func(path: Path) -> result.Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L79)
  Reads all bytes from the path and closes the file before returning.
- **Fs** · [`export let read_text = func(path: Path) -> result.Result<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L91)
  The file's text; invalid UTF-8 fails with InvalidData.
- **Fs** · [`export let write_bytes = func(path: Path, data: uint8[list]) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L107)
  Creates or truncates the file and writes the bytes.
- **Fs** · [`export let write_text = func(path: Path, text: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L112)
  Creates or truncates the file and writes the UTF-8 bytes of the text.
- **Fs** · [`export let append_text = func(path: Path, text: string) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L117)
  Creates the file if needed and writes the text at its end.
- **Fs** · [`export let exists = func(path: Path) -> result.Result<bool>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L122)
  Ok(false) for NotFound; other failures are errors. Follows links.
- **Fs** · [`export let metadata = func(path: Path) -> result.Result<FileMetadata>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L130)
  The kind, size in bytes, and last modification time of an entry, following links.
- **Fs** · [`export let list = func(path: Path) -> result.Result<Path[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L138)
  The directory's entries joined to `path`, sorted by name bytes.
- **Fs** · [`export let create_directory = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L154)
  Creates a single directory. Missing parents and an existing entry are reported as errors.
- **Fs** · [`export let create_directories = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L159)
  Creates the directory and any missing parents; succeeds when the directory already exists.
- **Fs** · [`export let remove_file = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L189)
  Removes a file or another entry that is not a directory, such as a link (the link, not its target).
- **Fs** · [`export let remove_directory = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L194)
  Removes an empty directory.
- **Fs** · [`export let remove_tree = func(path: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L202)
  Removes `path` and, for a directory, everything below it: depth first, in sorted order. Links and other entries of kind Other are removed without descending into them. Stops at the first error.
- **Fs** · [`export let rename = func(from: Path, to: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L233)
  Renames or moves an entry, replacing an existing file at `to`.
- **Fs** · [`export let copy_file = func(from: Path, to: Path) -> result.Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/fs.nra#L239)
  Copies a file's bytes through a 65,536-byte buffer, replacing an existing target. Both files are closed on every path.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- There is no scoped helper ([section 5.1](Stdlib-Design#51-failure-model)).
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 11.2 `Std.Fs`

Needs P2 for every host operation and T1 for `Path`.

**`Path`** is an immutable class holding normalized text.

- The separator is `/` on every host. The host service translates.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- `is_absolute()` is true exactly when the text starts with `/`. Platform prefixes such as drive letters are passed to the host unchanged and are not interpreted.
- Module function `of(text)`. Members include `join(other: string) → Path`, `parent() → Path`, `file_name()`, `stem()`, `extension()`, `components()`, `resolve_lexically() → Path`, `starts_with(other: Path) → bool`, and `relative_to(base: Path) → Path`. An absolute argument to `join` replaces the path. `parent` returns `nil` when the path has no parent. `relative_to` returns `nil` when it cannot express the path relative to the base. `file_name`, `stem`, and `extension` return `Optional<string>`. The extension is the text after the last `.` of the file name when that dot is not the first character. `components` returns a built-in list of strings. Equality, hash, ordering, and text use the normalized path.

**`File`** is a class wrapping a host file ID. The module function `open(path, mode: OpenMode) → Result<File>`. `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](Stdlib-Design#55-resources)). A `File` is unbuffered.

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
