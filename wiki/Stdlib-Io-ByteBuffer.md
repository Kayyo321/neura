# `Std.Io.ByteBuffer`

[Standard library index](Stdlib) · [Shared contracts and implementation decisions](Stdlib-Design) · [Language status](Status)

Import this module as `Std.Io.ByteBuffer`. Its [source](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra) defines the shipped declarations. The [declaration reference](#exported-declarations) lists its exported members and links each declaration to its source.

## API and shared contracts

- [Exported declarations for `Std.Io.ByteBuffer`](#exported-declarations)
- [Failure, equality, ordering, iteration, and resource contracts](Stdlib-Design#5-core-contracts)
- [Implementation decisions and verification limits](Stdlib-Design#18-implementation-decisions)

## Exported declarations

These declarations are extracted from the module source. Notes reproduce adjacent source comments; an absent note does not imply the member has no preconditions or failures. Follow the source links for implementation details. Inherited members are described with their declaring type.

<!-- BEGIN SOURCE API -->
The following public declarations are taken from the module source; each links to its definition. Notes reproduce adjacent source comments where present. Inherited members are documented on their declaring types. Consult the contracts and source for behavior, preconditions, and failures that are not stated here.

- **ByteBuffer** · [`export let ByteBuffer = class`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L10)
  A growable in-memory byte sink that can create independent readers.
- **ByteBuffer.ByteBuffer** · [`export let write = func(input: uint8[list]) -> Result<Unit>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L15)
  Appends every byte; never fails.
- **ByteBuffer.ByteBuffer** · [`export let to_list = func -> uint8[list]`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L21)
  A copy of the bytes written so far.
- **ByteBuffer.ByteBuffer** · [`export let size = func -> int`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L24)
  Returns the current number of bytes.
- **ByteBuffer.ByteBuffer** · [`export let clear = func`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L27)
  Discards all accumulated bytes.
- **ByteBuffer.ByteBuffer** · [`export let.override equals = func(other: class) -> bool`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L34)
  Only the current byte sequence participates. Readers hold their own copies and do not affect the buffer's equality or hash.
- **ByteBuffer.ByteBuffer** · [`export let.override hash_code = func -> uint32`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L43)
  Hashes the current byte sequence.
- **ByteBuffer.ByteBuffer** · [`export let.override to_string = func -> string`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L46)
  Returns the built-in list representation of the bytes.
- **ByteBuffer.ByteBuffer** · [`export let reader = func -> func(int) -> Result<uint8[list]>`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L49)
  A byte source over a copy of the current contents. A maximum count below 1 traps.
- **ByteBuffer** · [`export let create = func -> ByteBuffer`](https://github.com/Kayyo321/neura/blob/main/stdlib/std/io/byte-buffer.nra#L66)
  Creates an empty byte buffer.
<!-- END SOURCE API -->

## Detailed contracts

The following sections preserve the relevant descriptions and later implementation decisions from the standard-library design. Later implementation decisions qualify earlier plan text. The source and current [language status](Status) identify shipped behavior.

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
