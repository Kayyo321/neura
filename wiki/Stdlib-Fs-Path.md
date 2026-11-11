# `Std.Fs.Path`

[Standard library index](Stdlib) · [Standard library specification](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Fs.Path`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Fs.Path`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Behavior details and verification limits](Stdlib-Design#18-behavior-details)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **Path** · [`export let Path = class implements Comparable<Path>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L13)
  An immutable normalized path with lexical operations independent of the filesystem.
- **Path.Path** · [`export let init = func(raw: string)`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L18)
  Normalizes `raw`; Path.of is the usual spelling.
- **Path.Path** · [`export let is_absolute = func -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L23)
  Reports whether the path starts at the root.
- **Path.Path** · [`export let join = func(other: string) -> Path`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L26)
  This path followed by `other`; an absolute `other` replaces it.
- **Path.Path** · [`export let parent = func -> Path`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L31)
  The path without its last component, or nil for the root, `.`, and a single relative component.
- **Path.Path** · [`export let file_name = func -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L44)
  The last component; None for the root, `.`, and a path ending in `..`.
- **Path.Path** · [`export let stem = func -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L53)
  The file name without its extension.
- **Path.Path** · [`export let extension = func -> optional.Optional<string>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L62)
  The text after the last `.` of the file name, when that dot is not the name's first character.
- **Path.Path** · [`export let components = func -> string[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L78)
  The components in order, starting with "/" for an absolute path. The path `.` has none.
- **Path.Path** · [`export let resolve_lexically = func -> Path`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L92)
  Removes each `..` together with the component before it, without consulting the file system. `..` directly below the root is removed; leading `..` of a relative path are kept.
- **Path.Path** · [`export let starts_with = func(other: Path) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L110)
  True when both paths are absolute or both relative and every component of `other` begins this path.
- **Path.Path** · [`export let relative_to = func(base: Path) -> Path`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L131)
  A relative path that joined to `base` names this path, or nil when one is absolute and the other relative or when `base` has a `..` component after the common part.
- **Path.Path** · [`export let compare_to = func(other: Path) -> Ordering`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L159)
  Orders paths lexicographically by their normalized UTF-8 bytes.
- **Path.Path** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L162)
  Tests equality of the normalized path text.
- **Path.Path** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L170)
  Hashes the normalized path text.
- **Path.Path** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L173)
  Returns the normalized path text.
- **Path** · [`export let of = func(text: string) -> Path`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/fs/path.nra#L177)
  Constructs a path and normalizes its separators and dot components.
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

### 18.6 Host services, files, and networking

- **Signatures.** `neura.fs.stat` takes a follow-links flag so that `remove_tree` never descends through a symbolic link, `neura.fs.remove` takes a directory flag so that `remove_file` and `remove_directory` need no separate stat, and `neura.net.local_port` reads a listener's port. `File.size` is three seeks.
- **Errors.** Other failures use code 100. `EADDRINUSE` is `AlreadyExists`; `EAGAIN` and `ETIMEDOUT` are `TimedOut`; `EPIPE`, `ECONNABORTED`, and `ENOTCONN` are `ConnectionReset`; a name that does not resolve is `NotFound`. A read or entropy request is at most 1 MiB. Network errors carry `host:port` in the error's path.
- **Path.** `components` starts with `/` for an absolute path; `.` has no components. `file_name` is `None` for `/`, `.`, and a path ending in `..`. `starts_with` and `relative_to` need both paths absolute or both relative.
- **Buffers.** `BufferedReader` passes a large read straight through when its buffer is empty; `BufferedWriter` drops its buffer when the sink fails. `Utf8Reader.read_scalar` leaves the byte that broke a sequence unread.
- **Trap details.** `maximum count below 1`, `capacity below 16`, `negative count`, `port`, and `negative timeout` after `invalid argument: `.
- **Lifetime.** File and socket IDs count from 1 in each run; the runner closes what a run left open when it ends, and after each test.
