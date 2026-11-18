<!-- Generated from docs/lang/Stdlib.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Standard library specification

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page specifies the Neura standard library's source modules, contracts, and algorithms. The language and runtime supply the facilities in [section 3](#3-language-facilities). Every module and host service on this page is implemented in `stdlib/` and the runner, and tested as [section 17](#17-verification) describes; [Status](Status) and [Standard library](StandardLibrary) list what ships. [Section 18](#18-behavior-details) states the rules that complete the contracts of the earlier sections. [Design notes](https://github.com/Kayyo321/neura/blob/main/docs/design/Stdlib.md) record why the library is shaped as it is.

## 1. How to read this page

- **Syntax is not specified here.** This page describes behavior, data layout, and algorithms in prose and tables. It contains no Neura source.
- **Member notation.** Tables write a member as `name(parameter: Type, ...) → Result`. A function type is written `(A, B) → R`. A tuple type is written `[A, B]`, as in the language. This is documentation notation.
- **Names.** Module, type, and member names on this page are the names in `stdlib/`. They follow the [coding style](CodingStyle). No member is named with a keyword ([section 15](#15-names)).
- **Dependency labels.** Labels such as G1 and T1 point to the compiler facilities of [section 3](#3-language-facilities) that an API uses. Host bindings also require registered providers.
- **Sizes and indices** are `int` (`int32`), matching the built-in `length`. Hash codes are `uint32`.
- **Runtime-sized data is a built-in list.** An array's length is fixed by its literal, so a function cannot build an array whose length it computes. Every API that takes or returns a sequence of bytes, strings, or elements uses a built-in list (`uint8[list]`, `string[list]`, `T[list]`).
- **"Traps"** always means the run ends: see [section 5.1](#51-failure-model).

## 2. Principles

1. **The library is Neura source.** Its arithmetic, hashing, ordering, parsing, and mathematics compile to neuron graphs. The NVM supplies typed storage, bytewise text operations, scalar text formatting, traps, and explicit host service dispatch. Pure computation has no host imports.
2. **Use the language that exists.** A sum of cases is a payload enum read with `match`. Several results that are all valid are a tuple. A callback is a native function value. The library adds a class, a wrapper, or a built-in only where one of these does not fit.
3. **Fallible outcomes are tagged values.** A public fallible operation returns `Result<T>`. A generic value that may be absent uses `Optional<T>`. The primitive UTF-8 constructor has its own low-level tuple result, which the library converts to `Result<string>`.
4. **Returned errors and traps are different things.** An expected failure is a returned `Error` value. A violated precondition or a broken invariant is a trap that ends the run; nothing can catch it and no cleanup runs after it. The library promises nothing about state after a trap.
5. **Deterministic by construction.** For the same image, arguments, and host responses, every operation produces the same result, iteration order, hash codes, and text. There is no per-run hash seed and no dependence on handle values.
6. **Reference absence is explicit.** Use `nil` for absent class or interface references, such as internal tree links, when there is no distinct present-but-nil case. Use `Optional<T>` when `Some(nil)` is meaningful or the payload may be a value type. A position search uses -1 to represent no match.
7. **Use companion modules for factories and helpers.** A type lives in a module of the same name ([section 4.2](#42-source-layout)).
8. **Storage is reclaimed; resources are closed.** Garbage collection has shipped, so no API has an owner that must `destroy` it. A host resource is released only by an explicit `close`. Reclamation never runs `deinit` and never closes anything.
9. **Collection type arguments are invariant.** `List<Alert>` and `List<Renderable>` are distinct types. Interfaces define extension points. A library class is meant to be subclassed only where its contract says so.
10. **Pay only for what is reachable.** An image contains only what the entry point can reach.

## 3. Language facilities

The compiler and runtime supply these facilities to the library. The labels are used by the API tables below.

### G1. Function types over type parameters

Function types may mention enclosing type parameters in signatures, locals, fields, tuples, collections, and type arguments. Generic bodies may create closures that capture such values. Specialization resolves their types and generates the concrete callable and capture layouts. Captured local bindings are copied. Mutating a captured object's fields mutates the shared object.

This supports generic predicates, comparators, collection callbacks, `Sequence` adapters, and `Optional` and `Result` mapping helpers.

### G2. Generic enums

Generic declarations use `enum<T>(None, Some(T))` and specialize per concrete argument list. Variant construction supplies explicit type arguments, as in `Optional<T>.Some(value)`. A pattern uses the enum qualifier, as in `Optional.Some(value)`, or a concrete type overlay. The subject's type fixes the pattern's payload types.

A specialized enum can hold tuples, collection elements, callbacks, class references, or nested enum values. Its variants remain exhaustive. This permits direct `Optional<T>` and `Result<T>` source implementations. Nested patterns, guards, and inferred type arguments remain outside this design.

### G3. Universal members

- **G3a. Value members.** Supported scalars, strings, fieldless enums, tuples, arrays, and lists have `equals`, `hash_code`, and `to_string`. Generic calls resolve after specialization. Tuples and collections recurse into supported element types. Payload enums also have value members when every payload has them. Calling a generic value operation with an unsupported specialized type is diagnosed.
- **G3b. Class members.** The root type `class` holds class and interface references. Classes inherit `equals(other: class)`, `hash_code()`, and `to_string()`, with ordinary override dispatch. An override can use `other is MyClass` before a checked `other as MyClass` to compare fields. `==` and `!=` on class references compare identity and never call an override. Default hashing and text allocate an identity number on the first request; the number follows request order rather than allocation order. Variant and class names used by these operations are execution data.
- **Nil.** A class or interface reference can be explicitly `nil`. An uninitialized field remains a separate runtime fault. Direct member dispatch on a nil receiver traps. Nil participates in identity comparison. Generic and composite value operations treat two nil references as equal, hash nil to 0, and print `nil`.

These contracts support default key operations in hash collections. A type may still supply custom comparison, equality, and hash callbacks where a collection constructor accepts them.

### G4. Interfaces

Interfaces can be generic, inherit other interfaces, and declare required and default methods. A class can implement several interfaces. Multiple bounds are expressible on a type parameter. Inherited default conflicts require an explicit override. Interface values use the existing class dispatch machinery and can hold `nil`.

The standard collection and stream interface declarations are source work. Existing callback-based adapters can remain where a function type accurately describes a single operation.

### G5. For-each over library collections

For-each over an interface iterable calls `iterator()` once, tests `has_next()`, and calls `next()` for each element. Collection iterators own their structural-change checks. For-each over a built-in list uses the existing NVM structural-version rule. A library adapter should keep its order and modification contract the same through either entry point.

### T1. Text primitives

Strings are immutable UTF-8 with byte length bounded by `int`. The language supplies:

| Primitive | Contract |
| --- | --- |
| `text.byte_length` | Byte count as `int`. |
| `text.byte_at(index)` | One `uint8` at the checked byte index. |
| `text.slice(start, end)` | Half-open byte range. Both endpoints must be UTF-8 boundaries; the endpoint at the end of the string is allowed. |
| `string.from_utf8(bytes)` | Copies a `uint8[]` or `uint8[list]` and returns `[bool, string]`. Invalid UTF-8 returns `[false, ""]`; empty valid input returns `[true, ""]`. The library maps validation failure to its documented encoding error. |
| `==`, `!=` | Compare UTF-8 bytes without normalization. |

These operations supply the substrate for `Std.Text`, parsing, encodings, and string-keyed collections.

### N1. Float bit access

A float value's `to_bits()` returns the unsigned integer of the same width. `float16.from_bits`, `bfloat16.from_bits`, `float32.from_bits`, and `float64.from_bits` preserve every bit, including NaN payloads and the sign of zero. The `float` alias has the `float32` operation. Reinterpreting bits does not round or perform a numeric `as` conversion. Circuit conversion connects the same value lanes to a differently typed result.

### F1. Trap with a message

The built-in statement `trap_with_message(message)` ends execution with trap reason 3 and includes its string in the diagnostic. It does not need an assertion host service. `Failure.trap` wraps this primitive; its callers do not catch it.

Use the primitive directly in a value-returning function's trap-only branch so that return analysis sees a terminator. Calls to a void wrapper follow the ordinary function typing rules.

### P1. Library root

The installed tool finds its standard-library source root relative to the executable. Reserved `Std` imports resolve in that trusted tree, separately from ordinary `-I` roots. A user file cannot declare or shadow the `Std` namespace. Imports containing an `Internal` component are reserved for trusted library source. No implicit prelude is added.

### P2. Host bindings in source

Trusted library source can bind a bodyless function with the suffix `host("service.name", version)`. The return type is a payload enum with `Ok` and `Error(uint32)` variants, with an optional success payload on `Ok`. Both continuations return to the caller. The emitted import records the signature and version and includes only reachable bindings. A service still needs a matching runtime provider and a capability policy. A declaration alone cannot create either.

### P2a. Kernel bindings in source

Trusted library source can also bind a bodyless function with the suffix `kernel("neura.area.name", version)`. A built-in C kernel of the runtime then computes it ([Kernels](Kernels)): a deterministic, pure function that keeps the results, errors, and traps of the Neura code it replaces. The function's signature must be the one the kernel's parameter and result kinds give ([Binding a kernel](Kernels#binding-a-kernel)); lowering checks it against the compiler's kernel table and rejects a binding in user code (`E0780`), a signature that differs (`E0781`), and an unknown kernel or ABI version (`E0782`). A call is a runtime state operation, not a host request: it needs no import, provider, or capability, it is task-safe, and a trace never records it. The image lists the kernels it calls with their signatures, and a runtime without one of them refuses the image before it runs.

`scripts/gen-kernels.py` writes the declarations of every kernel into the internal modules `Std.Internal.Kernels.*` (`stdlib/std/internal/kernels/`), and a public module wraps them. Kernel-backed today are `Std.Digest.Crc32` and `Std.Digest.Sha256`; every function of `Std.Math.Math`; `Std.Collections.Sorting.sort_ascending`; in `Std.Text.Strings` the searches, comparisons, trims, `split`, `split_limit`, `lines`, replacements, ASCII case mappings, `scalar_count`, `hash`, and the error path of `from_bytes`, and the `String` companion members built on them, with its own comparisons, ASCII case comparison, and `trim`; `StringBuilder.hash_code`; the integer companions' `to_string_radix` and `parse_radix`; and `Std.Encoding.Utf8.validate`, `Base64`, and `Hex` (sections 7.1, 8.1, 9, 10.1, 13.1, and 13.2). Every other module, and every other member of these, runs its Neura code. The scalar hash kernels (`neura.hash.combine`, `spread`, `i64`, `u64`, and `f64`) exist but no module calls them: a kernel call costs more steps than the few operations of Neura code they would replace.

A kernel call is charged, before it runs, one step plus its work in units of no more than a block step's time, where the work is the most the call can do with the arguments it has: the bytes of text or elements of a list it can read or write (`s - from` for a search from an offset, at most a window for `index_of`'s bounded search), or `n log n` comparisons for a sort. The charge is a function of the arguments' lengths and scalar values only, so it is the same in every execution mode; it never lets a call do unbounded work for a bounded charge, and it never charges for bytes the call cannot touch. A library loop that would call a kernel on the same text repeatedly instead calls one that does the whole operation, or searches in doubling windows, so its total charge stays proportional to its work ([Kernels](Kernels#running-a-kernel)). The Neura code a kernel replaced moves unchanged to the module's tests as the oracle that the kernel is checked against ([CONTRIBUTING](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md#kernel-oracles)).

### P3. Entry point from the library

An override of `Std.Program.Entry.main` can be the image entry point; when modules inherit the module that declares it, the entry point is `main` of the one module of that line that no other module inherits ([Modules](Modules#interfaces-and-inheritance)). Its `ExitCode` maps `Ok` to 0, `Failure` to 1, `Status(int)` to the validated process status, and each named variant to its fixed status. The compiler rejects an image with two entry points.

### What the language does not provide

The language has no hexadecimal integer literals, no inference of type arguments, no re-export of generic classes, generic functions, or payload enums, no list operation that allocates a list of a given length, inserts at an index, or truncates, and no bulk copy of a range between lists. The library builds these from the operations it has; [design notes](https://github.com/Kayyo321/neura/blob/main/docs/design/Stdlib.md) list them and the members that would use them.

## 4. Architecture

### 4.1 Modules

| Module | Contents | Section |
| --- | --- | --- |
| `Std.Core` | `Failure`, `Ordering`, `Hash`, `Error`, `Optional`, `Result`, `Unit`, `IntRange`, the primitive companion modules, `StringBuilder`, `Character`, `UnicodeScript`, `UnicodeBlock` | [5](#5-core-contracts), [6](#6-stdcore-types), [7](#7-primitive-companion-modules), [8.5](#85-character) |
| `Std.Text` | `Strings`, `Scalars`, `Regex` | [8](#8-stdtext) |
| `Std.Collections` | `ArrayList`, `ArrayDeque`, `LinkedList`, `HashMap`, `HashSet`, `TreeMap`, `TreeSet`, `PriorityQueue`, `BitSet`, `OrdinalMap`, `Sequence`, `Sorting`, the interfaces | [9](#9-stdcollections) |
| `Std.Math` | `Math`, `BigInteger`, `BigDecimal` | [10](#10-stdmath) |
| `Std.Io` | `Console`, byte and text streams, buffers | [11.1](#111-stdio) |
| `Std.Fs` | `Path`, `File`, `Fs` | [11.2](#112-stdfs) |
| `Std.Program` | `Entry`, `ExitCode`, `Environment`, `Process` | [11.3](#113-stdprogram) |
| `Std.Net` | TCP streams and listeners; the contracts and the `Core`, `Socket`, `Dns`, `Tls`, `Quic`, `Http`, `Rpc`, `Realtime`, `Resilience` and `Mesh` packages; HTTP requests through the host (`Std.Net.Http.Fetch`) | [11.4](#114-stdnet) |
| `Std.App` | `run`, `Context`, `Request`, `Response`, `Handler`, `handler_of`, `Context.sql_pool`: the entry of a server image | [Server host](Server-Host) |
| `Std.Data` | `Sql`: pools, connections, transactions, cursors and rows of SQLite and PostgreSQL | [SQL](Sql) |
| `Std.Time` | `Duration`, `Instant`, `Clock`, calendar types | [12.1](#121-stdtime) |
| `Std.Random` | `Random`, `Uuid` | [12.2](#122-stdrandom) |
| `Std.Encoding` | `Utf8`, `Hex`, `Base64`, `Json`, `Csv` | [13.1](#131-stdencoding) |
| `Std.Digest` | `Crc32`, `Sha256` | [13.2](#132-stddigest) |
| `Std.Log` | `Logger` | [14.1](#141-stdlog) |
| `Std.Test` | Assertions | [14.2](#142-stdtest) |
| `Std.DynamicForeign` | `Arena`, `MemorySegment`, `MemoryLayout`, `ValueLayout`, `FunctionDescriptor`, `SymbolLookup`, `Linker`, `MethodHandle`, `ForeignValue` | [Foreign interop](ForeignInterop) |
| `Std.Concurrent` | `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Tasks`, `WorkerGroup`, `Parallel`, `AtomicInt64`, `Semaphore`, `Guarded` | [19](#19-stdconcurrent), [Concurrency](Concurrency#9-stdconcurrent) |
| `Std.Events` | `Event`, `EventView`, `Subscription` | [20](#20-stdevents) |
| `Std.Crypto` | The contracts, and the packages `Core`, `Random`, `Hash`, `Mac`, `Kdf` and `Symmetric` | [21](#21-stdcrypto) |

`Std.Program`, `Std.Io.Console`, and `Std.Test` replace the built-in `SysIo` and `Test` modules, which are deleted ([section 4.6](#46-retired-built-ins)).

### 4.2 Source layout

- The library source tree is `stdlib/`, laid out by the rules in [Project structure](ProjectStructure). The installed tool discovers its trusted copy automatically. Ordinary `-I` roots cannot provide `Std` modules.
- **One module per type, named after the type.** `stdlib/std/collections/array-list.nra` declares the module `ArrayList`, imported as `Std.Collections.ArrayList`, which exports the class `ArrayList`.
- **Factories are module functions.** Where Java writes a static method on a class, this library has an exported function in the type's module. No class-level members are needed. In the tables below, a type's "module functions" are these.
- **Internal helpers** are unexported members of the module that uses them, or separate modules under an `Internal` component (`Std.Collections.Internal.HashCore`) when two types share them. Nothing outside `Std` imports an `Internal` module; P1's namespace rule extends to enforce that.
- **No facades.** A module such as `Std.Collections` that re-exports every collection type needs re-export of generic classes, which the language does not have ([section 16](#16-not-provided)). The type modules are the import paths.
- **There is no general prelude.** Import modules to use their names. Primitive member syntax can resolve functions from the primitive's trusted companion module.
- The library is compiled together with the program. There is no separately compiled library image.

### 4.3 Dependency order

A module may import only modules above it. Cycles are forbidden.

1. `Std.Core`
2. `Std.Collections`
3. `Std.Text`
4. `Std.Math`
5. `Std.Text.Regex`, `Std.Encoding`, `Std.Digest`, `Std.Random`, `Std.Time`
6. `Std.Io`, `Std.Program`, `Std.Concurrent`
7. `Std.Fs`, `Std.Net`, `Std.DynamicForeign`
8. `Std.Log`, `Std.Test`
9. `Std.Crypto`: its contracts, then `Core`, `Hash`, `Mac`, `Kdf`, `Symmetric` and `Random`, each above the next. It imports `Std.Core`, `Std.Concurrent.AtomicInt64`, `Std.Digest.Sha256`, `Std.Encoding`, `Std.Math.BigInteger` and `Std.Time`.

`Std.Net.Http` imports `Std.Log` for `request_log`, and `Std.Net.Http.AppHandler` imports `Std.App`; `Std.Log` imports no `Std.Net` module and `Std.App` only the internal `Std.Net.Internal.HttpServer`, so no cycle forms ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md#decisions-taken-without-the-owner)). `StringBuilder` is in `Std.Core` because number formatting needs it. `IntRange` imports the `Iterable` and `Iterator` interfaces from `Std.Collections` so that for-each can visit a range; those two modules import nothing, so no cycle forms. `Std.Core` never names a collection class: where a core function returns several values it returns a tuple or a built-in list. The `String` companion is an exception to the order above. It depends on `Std.Text` and the pure formatting module in `Std.Io.Internal`. `Std.Test` is last in the import order but early in the schedule: its assertions for built-in types import only `Std.Core`. `Character` is a second exception: its full case mappings use the casing routines of `Std.Text.Internal.UnicodeCase`, which import nothing above `Std.Text`.


### 4.4 Built-in floor

The library relies on text inspection and UTF-8 construction (T1), float bit access (N1), message traps (F1), typed class identity and scalar formatting (G3), and explicit host bindings (P2). These are compiler and NVM operations, not library services.

Wrapping arithmetic, integer sign reinterpretation, vacant storage, bulk copy, and enum reflection are unnecessary for the chosen implementations.

### 4.5 Host services

Every interaction with the outside world is a host service with a name, a version, typed arguments, and typed success and error results ([NVM](NVM#host-calls-and-standard-output)). An error result is a `uint32` code that the library maps to an `IoErrorKind` ([section 6.3](#63-error)): 1 `NotFound`, 2 `PermissionDenied`, 3 `AlreadyExists`, 4 `InvalidInput`, 5 `Interrupted`, 6 `UnexpectedEnd`, 7 `NotADirectory`, 8 `IsADirectory`, 9 `DirectoryNotEmpty`, 10 `ConnectionRefused`, 11 `ConnectionReset`, 12 `TimedOut`, 13 `Unsupported`, 14 `CapabilityDenied`, 15 `Cancelled` (the answer of a cancelled version 2 request), and every other code `Other` (the providers use 100).

| Service | Version | Arguments | Success | Used by |
| --- | --- | --- | --- | --- |
| `neura.console.write_line` | 1 | text | none | `Console.print_line`; its error code 1 maps to `Other` |
| `neura.console.write` | 1 | stream (1 output, 2 error), bytes | count written | `Console` |
| `neura.console.read` | 1, 2 | maximum count | bytes; empty at end of input | `Console` (version 2); overlaps in an image with tasks |
| `neura.fs.open`, `read`, `write`, `seek`, `close` | 1 | path and mode; file ID and count, bytes, or offset and origin | file ID, bytes, count, or position | `File` |
| `neura.fs.stat`, `list`, `make_directory`, `remove`, `rename` | 1 | path text (two for rename); `stat` also takes a follow-links flag and `remove` a directory flag | kind, size, and modified time; list of names; none | `Fs` |
| `neura.clock.wall`, `monotonic` | 1 | none | seconds and nanoseconds; nanoseconds | `SystemClock` |
| `neura.clock.sleep` | 1, 2 | nanoseconds | none | `SystemClock` (version 2); overlaps in an image with tasks |
| `neura.entropy.fill` | 1 | count | bytes | `Random.from_entropy`, `Std.Crypto.Random.Entropy.os_bytes` |
| `neura.env.get` | 1 | name text | found flag and value text | `Environment` |
| `neura.process.exit` | 1 | status | does not return | `Process.exit` |
| `neura.net.listen`, `local_port`, `close` | 1 | [section 11.4](#114-stdnet) | socket ID or port | `Std.Net` |
| `neura.net.connect`, `accept`, `read`, `write` | 1, 2 | [section 11.4](#114-stdnet) | socket ID, bytes, or count | `Std.Net` (version 2); overlap in an image with tasks |
| `neura.http.fetch` | 1 | method, URL, headers, body, timeout, credentials ([Providers](Providers#http-fetch)) | status, headers, body | `Std.Net.Http.Fetch`; overlaps in an image with tasks; served by the browser host only in 0.3.0 |
| `neura.ffi.abi`, `arena.open`, `arena.close`, `alloc`, `library.open`, `library.default`, `library.embedded`, `symbol`, `read`, `write`, `strlen`, `call` | 1 | [NVM](NVM#host-services) | layouts, IDs, addresses, bytes, or packed results | `Std.DynamicForeign` |

The exact signatures are in [NVM](NVM#host-services). The retired `neura.test.assert` and `neura.test.assert_equal_int32` services are no longer registered.

- **Capabilities.** `neura run` and `neura test` grant every group by default: files, network, environment, clock, entropy, and native code, as well as the console group and `neura.process.exit`. `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, and `--deny-all` turn groups off; `--allow-*` options grant a group again, and `--allow-fs` takes an optional list of path prefixes that narrows files to them. Options apply in command-line order. A denied service still binds, and each call returns error 14. The library default of `nvm::Capabilities` is all-denied: an embedder grants each group explicitly.
- **Replay.** The NVM library already records and replays host interactions (`nvm::record`, `nvm::replay`). `neura run --record file` and `--replay file` expose that. Tests of I/O, time, and entropy run under replay.
- **Ordering.** `neura.fs.list` returns names sorted by their UTF-8 bytes. The host sorts.
- Host IDs are opaque `int64` values that the library wraps in classes and never exposes.

### 4.6 Retired built-ins

The compiler has no built-in `SysIo` or `Test` module. Importing `SysIo` is an error, `the built-in SysIo module was removed; use Std.Io.Console and Std.Program`, and importing `Test` is an error, `the built-in Test module was removed; use Std.Test`. An image built with the removed host services behaves as follows: an image whose only import is `neura.console.write_line` runs, and an image that calls the removed assertion services fails to bind (`tests/backend/legacy`).

| Removed | Replacement | Needs |
| --- | --- | --- |
| `SysIo.println(text)`; a failed write traps with reason 3 | `Console.print_line(text)` in `Std.Io.Console`: a function bound to `neura.console.write_line` version 1, wrapped so that a failed write calls `Failure.trap` | P2, F1 |
| `SysIo.Entry`, a module with the requirement `main` | `Std.Program.Entry`, a library module with the same requirement | P3 |
| `SysIo.Entry.ExitCode`, with the single value `Ok` | `Std.Program.ExitCode`, a payload enum: `Ok` (status 0), `Failure` (status 1), `Status(int)` (0 through 255; anything else traps) | P3 |
| `Test.assert(condition)` and `Test.assert_equal_int32(expected, actual)`, host services that record a message and return an error that traps | `Std.Test` ([section 14.2](#142-stdtest)): Neura functions that build the message and call `Failure.trap` | F1 |
| The runner prints the recorded assertion message | The runner prints the trap's message | F1 |

A program with two entry points is rejected.

### 4.7 Versioning

The library has one version, equal to the compiler's. `Std.Core.Version` exports `Major`, `Minor`, and `Patch`. The text produced by `to_string` functions, the hash algorithms, the iteration orders, the trap messages, and the float formatting rules on this page are part of the contract and change only with a major version.

## 5. Core contracts

### 5.1 Failure model

There are two kinds of failure and they never mix.

**Returned errors.** A function whose failure depends on input text, input bytes, or the outside world returns `Result<T>` ([section 6.2](#62-resultt)). The caller reads it with `match`. There is no propagation shorthand and no exception; passing an error up is an explicit `match` arm that returns it.

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
| `Closeables.use` closes the resource even when the body panics | Replaced by the scoped helpers of [section 5.5](#55-resources), which close the resource on every ordinary completion. A body cannot fail and return; it either returns or the run ends, and when a run ends, by return or by trap, the host releases every file and socket. |
| "If the predicate of `remove_if` panics, the collection is left valid" | Removed. A trap inside a callback ends the run, so no state can be observed afterwards. No collection keeps recovery code. |
| `Iterator.next` panics with `NoSuchElementError`; modification panics with `ConcurrentModificationError` | Both are traps with the messages above. The error classes for programmer mistakes are gone. |
| `assert_panics` recovers a panic and returns its error | Removed. A test that expects a trap is a program with an expected-diagnostic file, run by the test harness as the existing trap tests under `tests/backend/traps` are ([section 17](#17-verification)). |
| A fallible callback | A callback that can fail returns a `Result`, and the member that calls it documents how it stops. |

### 5.2 Equality, hashing, and text

The compiler supplies these results for supported value members. Companion functions such as `Int32.hash` and `Strings.equals` use the same contracts. For types with custom equality, equal values must have the same hash.

| Type | Equal when | Hash | Text |
| --- | --- | --- | --- |
| `bool` | Same value | 1231 for true, 1237 for false | `true` or `false` |
| Integers up to 32 bits | Same value | The value's 32-bit two's complement pattern as a `uint32` | Decimal, `-` for negatives, no leading zeros |
| `int64`, `uint64` | Same value | Low 32 bits exclusive-or high 32 bits | Decimal, as above |
| Floats | Same bit pattern, except that every NaN equals every NaN. `0.0` and `-0.0` are **not** equal. | The bit pattern with every NaN replaced by the canonical NaN; 16-bit patterns zero-extended, the 64-bit pattern folded like `uint64` | [Section 7.5](#75-float-text-conversion) |
| Fieldless enum | Same variant | The ordinal | The variant name |
| Payload enum | Same variant and equal payloads in declaration order | Start with ordinal + 1, then multiply by 31 and add each payload hash with wrapping arithmetic | Variant name, then positional values in parentheses or named fields in braces, separated by commas and spaces |
| `string` | Same bytes | Start with 0; for each byte, 31 times the hash plus the byte, wrapping | The string itself |
| Tuple | Same element types and each pair of elements equal, in order | Start with 1; for each element, 31 times the hash plus the element's hash, wrapping | `[`, the elements' text separated by `, `, `]` |
| Array, list | Same kind, same length, and each pair of elements equal in index order | As for a tuple | As for a tuple |
| Class (default, G3b) | Identity | The identity number times 2,654,435,761, wrapping | The qualified class name, `#`, and the identity number, for example `Acme.Shapes.Circle#3` |
| Function value | Not comparable | None | None |

Float equality here deliberately differs from `==`: it is an equivalence relation consistent with the hash, which hash tables need, while `==` stays IEEE 754. The multiplier 31 is chosen because multiplying by 31 is a shift and a subtraction.

The module **`Hash`** has the building blocks, all ordinary functions:

| Member | Behavior |
| --- | --- |
| `combine(seed: uint32, value: uint32) → uint32` | 31 times `seed` plus `value`, wrapping. Computed in `uint64`: the product is below 2^37, so nothing overflows before the mask. |
| `spread(hash: uint32) → uint32` | `hash` exclusive-or `hash` shifted right by 16. Hash tables apply this before masking. |
| `of_bool`, `of_int64`, `of_uint64` | The table's results for those types. |

A class's hash starts from 17 and combines each field's hash in declaration order.

**The contract for a class that overrides the G3b members:** `equals` is an equivalence relation; equal values have equal hashes; neither traps nor modifies the value; and while an object is a key in a hash collection its hash and equality do not change. The library does not detect violations.

**Self-containing collections.** A collection's text prints `(this collection)` wherever that text reaches the collection again, so a collection that contains itself, directly or through other collections, has finite text. The marker names the collection whose text was already being built: for a list `a` holding a list `b` that holds `a`, `a.to_string()` is `[[(this collection)]]`.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`.
- **A comparator is a native function value** of type `(T, T) → Ordering`. There is no comparator interface or wrapper class. A named function, a bound method, or a closure all serve. A comparator must define a total order; if it does not, the library stays memory safe and terminates, and the resulting order or lookup is unspecified.
- **Natural orders** are named functions in the companion modules, usable as comparators directly: `Int32.compare`, `Float64.total_compare`, `Strings.compare`, and so on.

| Type | Natural order |
| --- | --- |
| Integers | Numeric value |
| `bool` | `false` before `true` |
| Floats | Negative infinity, negative finite values, `-0.0`, `0.0`, positive finite values, positive infinity, then NaN; all NaNs equal |
| `string` | Byte-wise lexicographic; a proper prefix sorts first. This equals ordering by Unicode scalar value. |
| Fieldless enum | Ordinal, obtained by the caller with `as` |

- The module **`Comparators`** uses generic function values (G1) for `reversed<T>(c)`, `by_key<T, K>(key: (T) → K, c: (K, K) → Ordering)`, and `then<T>(first, second)`.
- **`Comparable<T>`**, an interface with `compare_to(other: T) → Ordering`, arrives with G4. Nothing in the library requires it: sorted structures always take a comparator.

### 5.4 Iteration

The loop form depends on the source value.

| Way | Available | Notes |
| --- | --- | --- |
| For-each over the built-in list from `ArrayList.items()`, or from any collection's `to_list()` | Yes | The built-in loop compares the list's structural version before each advance and traps with reason 5 on a change. `items()` is live; `to_list()` is a copy. |
| An index loop over `size()` and `get(index)` | Yes | For lists and deques. No modification check. |
| The collection's iterator object: `iterator()` returns a class with `has_next() → bool` and `next() → T` | Yes, as one concrete iterator class per collection | For a collection's iterator, `has_next` has no effect and can be repeated. For every iterator, a generator's included, `has_next` consumes nothing and repeating it repeats no work ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Lifetime030.md), implemented, proposed). `next` returns the next element and advances, and traps with `no such element` at the end. An iterator is used once. |
| For-each directly over the collection | Yes | Lowers to the iterator protocol when the source implements `Iterable<T>`. Every public collection, `Sequence`, and `IntRange` implement it; `tests/stdlib/for-each` loops over each one. |

- **Iteration does not allocate per element.** `has_next` and `next` stay two calls. A single `next` returning `Optional<T>` would read well with `match`, but it would allocate an enum value for every element.
- **Map iteration yields tuples.** A map's iterator returns `[K, V]`. The tuple is an immutable snapshot of the entry, which is an immutable snapshot of one association.
- **No removal through iterators.** Collections remove in bulk with `remove_if` and `retain_if`.
- **Fail-fast iterator objects.** A wrapper over a single built-in list uses that list's `uint64` structural version. Other mutable collections use a `uint64` modification count and increment it after each structural change. An iterator records its version when created. `has_next` and `next` compare first and trap on a difference. Replacing an element by index or replacing the value for an existing key is not structural.
- The iteration order of every collection is specified in section 9.
- `Iterator<T>` and `Iterable<T>` are the interfaces for this protocol. Concrete iterator classes and collections implement these member signatures.
- A [generator](CoreLanguage#generators) is a function that returns a `Sequence<T>` over a generated `Iterator<T>`: its first `has_next` with nothing buffered runs the body to its next `yield`, and further calls run nothing until `next` takes the buffered value.

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
- A [`using`](CoreLanguage#using) block applies the same rule to the value a `return` carries out of it, for a close that has already run ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Lifetime030.md), implemented, proposed). The rule is held by `close_outcome` in an internal module, `std/io/internal/close-outcome.nra`, which only a program that writes `using` loads, so that the programs that do not keep their images.
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](NVM#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 5.6 Callbacks

- A callback parameter is written as a native function type. The library declares no named function shapes: a type overlay can be neither generic nor a function type, and the native spelling says everything a name would.
- **Concrete callbacks.** `Logger` takes a sink of type `(string) → void`, and any module can use function values whose types name no type parameter.
- **Generic callbacks specialize with their caller.** A member of `ArrayList<T>` can take `(T) → bool`. The G1 label in API tables identifies members that depend on this compiler facility.
- **Capture is by value.** A closure copies the locals it uses when it is created. A closure cannot count or accumulate in a captured local. It captures an object and mutates that. `Std.Core` provides `Cell<T>` ([section 6.5](#65-small-types)) for the purpose: a callback that sums captures a `Cell<int64>`.
- **Capture of a reference is shared.** A closure that captures a collection sees later changes to it, and changes it makes are visible outside. A callback passed to a collection's own member must not structurally modify that collection; the member compares the modification count after each call and traps on a change.
- Unless a member's description says otherwise, a callback may be called any number of times, in the order described.

### 5.7 Conformance summary

These are the intended source-module contracts. Compiler support for the operations does not imply that a collection module or adapter is already packaged.

| Type | Equality and hash | Natural order | Iterable | Interfaces (G4) |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, fieldless enums, `string`, tuples | Built in (G3a) | Companion `compare` functions | No (`Strings.scalars` gives a list) | |
| Array, list | Built in, structural (G3a) | No | Built-in for-each | |
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Built in when each payload supports value members | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `RegexFlags`, `RegexMatch`, `Regex` | By value | No | No | |
| `StringBuilder`, `ByteBuffer` | Current byte sequence | No | No | |
| `Sequence`, `Random`, `Logger`, stream adapters, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](#111-stdio) |

### 5.8 Task safety

A function that a spawned task calls must be task-safe ([Concurrency](Concurrency#52-task-safe-calls)). For library code that means: no reassignable binding at module or file level, no module constant that is an object of a class that is not `#Immutable` on the path of a function a task should be able to call, and no first-use static initialization. State that a function needs between calls belongs in an instance or in an argument.

- **How to check.** `neura check --task-safety <files>` lists every function of the linked program with `task-safe`, `task-safe; not for a parallel map: <reason>`, or `not task-safe: <reason>`, grouped by source file. Run it on a program that imports the module.
- **What is pinned.** The `concurrency-frontend` test runs the report over a program that links `Console`, `HashMap`, `ArrayList`, `TreeMap`, `Float64`, `Duration`, `SystemClock`, `Instant`, `Logger`, `TcpStream`, `TcpListener`, `Strings`, `Regex`, `Random`, and `Json`, and fails when a function other than console input is not task-safe.
- **What is not task-safe.** Console input (`Console.read_line`, the source of `Console.input()`) and, in a program that calls `Console.input()`, the read functions of `BufferedReader` and `Utf8Reader`. [Standard library](StandardLibrary#task-safety) gives the reason for each and the statement for every area.
- **Changes made for it.** A collection's text conversion keeps its self-containment flag in the instance ([section 5.2](#52-equality-hashing-and-text)). Float parsing reads no module state. The `BigInteger` and `BigDecimal` methods build the small values they need and do not read the exported constants `Zero`, `One`, and `Ten`, which stay for the root task's use.
- **Checked tags.** The library states these properties with [tags](Tags) that the compiler checks. `#Immutable` marks the value classes `Duration`, `Instant`, `LocalDate`, `LocalTime`, `LocalDateTime`, `ZoneOffset`, `IntRange`, `Uuid`, `Path`, `FileMetadata`, `ItemLimits`, and `PortableErrorLink`, so tasks share their instances and may read constants such as `Duration.Zero`; each is also `#ThreadSafe`. `BigInteger`, `BigDecimal`, `RegexMatch`, `MemoryLayout`, `FunctionDescriptor`, and the immutable collections keep their contents in lists, so they are not `#Immutable` and are not transferable. `#ThreadSafe` marks the module functions of `Std.Core`, `Std.Math`, `Std.Text`, `Std.Encoding`, `Std.Digest`, and `Std.Time`, the `Std.Concurrent` handles, the `Cancellation` functions, and `Console.print_line`; a function that calls a method a program may override, such as `Error.message`, which calls `ErrorDetail.message`, is left untagged because a program's override decides whether it is task-safe. `#ThreadSafe` also marks the factory functions of other modules that build an instance without a host request, such as `ByteBuffer.create`, `Logger.create`, and `Random.seeded`, and the `Std.Test` assertions. `#Pure` marks the functions and methods that compute their result from their arguments and the state reachable from them, such as every `Math` function, the integer and float companions, the arithmetic and queries of the time value types, and the queries of a class such as `StringBuilder.is_empty` or `BitSet.contains`. A function that builds its result through a helper that fills a `StringBuilder` or a list it allocated, such as `Json.write`, `Float64.to_fixed`, `Regex.compile`, or the `BigInteger` arithmetic, is `#Pure` too. Three kinds of function are left without it: a generic function, such as a collection method, because the check applies to each specialization and a program's element type with an impure `to_string`, `equals`, or `hash_code` would make it fail; a function that can reach a method a program may override, such as `Error.message`, `Error.to_portable`, or a function that traps with an error's message through `Result.expect`, such as `Charsets.encode` and the `String` functions that take a regular expression; and a function whose result other tasks change, `AtomicInt64.load`. `#NotThreadSafe` marks the classes whose instances hold mutable or native state: `StringBuilder`, `Regex`, `Random`, `Logger`, `Stopwatch`, `FixedClock`, `ByteBuffer`, `BufferedReader`, `BufferedWriter`, `Utf8Reader`, `Utf8Writer`, `File`, `TcpStream`, `TcpListener`, the mutable collections (`ArrayList`, `ArrayDeque`, `LinkedList`, `HashMap`, `HashSet`, `StringMap`, `IntMap`, `OrdinalMap`, `TreeMap`, `TreeSet`, `PriorityQueue`, `BitSet`, and `Sequence`), the `Std.Events` classes (`Event`, `EventView`, and `Subscription`), and `Arena`, `MemorySegment`, `MethodHandle`, `Linker`, and `SymbolLookup`. A task may create and use instances of its own, a task's result may be one, and a `Guarded<T>` may hold one; giving one to another task, sending one on a channel, or making one a parallel item's argument or result is `E0602`, as it was before the tag, because none of these classes is transferable, and the error now points at the tag ([Tags](Tags#threadsafe-and-notthreadsafe)). The standard library's `equals(other: class)` overrides cannot promise `#Pure`: reading `other` may run a getter supplied by the linked application, even when the receiver class is final. Their effect summaries still follow the implementation in each linked program.


## 6. `Std.Core` types

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
| `Context` | `message: string`, `cause: ErrorLink` | Another error with a description of what was being attempted. The cause is `cause.cause`. |

- `IoErrorKind` is a fieldless enum, in order: `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidInput`, `Interrupted`, `UnexpectedEnd`, `NotADirectory`, `IsADirectory`, `DirectoryNotEmpty`, `ConnectionRefused`, `ConnectionReset`, `TimedOut`, `Unsupported`, `CapabilityDenied`, `Closed`, `InvalidData`, `Other`, `Cancelled`. The first fourteen map from host error codes 1 through 14, and `Cancelled` from code 15, the answer of a cancelled version 2 request ([Concurrency](Concurrency#104-host-io)); it comes last so that the kinds before it keep their order. `Closed` and `InvalidData` are produced by the library.
- `ParseErrorKind`, in order: `Empty`, `InvalidCharacter`, `OutOfRange`, `UnexpectedEnd`, `Syntax`.
- **`ErrorDetail`** is an abstract class with two requirements, `code() → string` (a stable identifier such as `billing.card_declined`) and `message() → string`. User code defines its own error types by deriving from it; dynamic dispatch on a non-generic class works. It is the library's one class meant to be a base. `SimpleDetail` is the concrete subclass that stores both strings.
- Adding a variant to `Error` breaks every `match` without a wildcard arm, so the variant list changes only with a major version. Code that only reports errors should end its `match` with a wildcard arm that calls `Errors.message`.
- Errors are immutable. A cause is set at construction, so chains are finite and acyclic.
- **`ErrorLink`** is a final class with one `last` field, `cause: Error`. A `Context` error holds its cause through it because an enum value cannot contain a value of its own type ([Types](Types)); `Errors.context` builds the link. Until 0.3.0 the field was the `Error` itself, so code that matches `Context { message, cause }` now reads `cause.cause`.
- **`PortableError`** is the transferable copy of an `Error`, for a failure that must cross a channel or a spawn. `Error` itself is not transferable, because `Custom` holds an `ErrorDetail` object ([Concurrency](Concurrency#51-transferable-types)). `PortableError` has the same variants and fields with one change: `Custom` is `Custom { code: string, message: string }`, and the cause of its `Context` is a `PortableErrorLink`, the same link as `ErrorLink` for a `PortableError`, marked `#Immutable` so that a portable error stays transferable.

The module `Error` exports the enum, the two kind enums, `ErrorDetail`, `SimpleDetail`, `ErrorLink`, `PortableErrorLink`, and:

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


### 6.4 Concrete aliases and absence

Define `Optional<T>` and `Result<T>` directly as generic payload enums. A concrete public name such as `File.Result` can be a type overlay of `Result<File>`. Reuse the same `Ok` and `Err` variants and payload types through the overlay.

Use `nil` for an internal absent reference when it cannot be confused with a real value, such as a missing parent link or an unused iterator reference. Do not replace `Optional<T>` with a nil convention on a generic return, or on an API where a present nil reference must be distinguishable from absence. JSON `Null` remains its own payload enum variant.

### 6.5 Small types

| Type | Definition | Available |
| --- | --- | --- |
| `Unit` | A fieldless enum with the single variant `Value`. | Yes |
| `Cell<T>` | A class with one mutable field `value`, and the module function `of<T>(value)`. It exists so that closures, which capture by value, can share mutable state. | Yes |
| `IntRange` | An immutable class with the get-only fields `start` (inclusive), `end` (exclusive), and `step` (nonzero). Module functions `of(start, end)` and `stepped(start, end, step)`; a zero step traps. A negative step counts down while the value is greater than `end`. Members `size()`, `is_empty()`, `contains(value)`, `get(index)`, `to_list()`, and `iterator()`; it implements `Iterable<int>`, so `for (let i in range)` visits the values in order. `size` and iteration are computed in `int64` so that no intermediate overflows. | Yes |

`Pair`, `Entry`, and `Boxed` do not exist: a pair is the native tuple `[A, B]` (`partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`), and a map entry is the tuple `[K, V]`. No API on this page takes an `Object`.

Named types are kept where the name carries a contract a tuple cannot: `RegexMatch` (group lookup by name), `FileMetadata` (three fields of unrelated types that will grow), and the value classes of `Std.Time`.

### 6.6 `Arithmetic`

*Status: proposed in [the 0.3.0 records note](https://github.com/Kayyo321/neura/blob/main/docs/design/Records030.md); implemented.*

`Std.Core.Arithmetic` declares four static contracts ([Polymorphism](Polymorphism#static-interface-requirements)): interfaces of static requirements only, which a generic function calls through its type parameter, as `T.zero()`, and which each specialization calls directly.

| Contract | Static requirements | Conforming primitives |
| --- | --- | --- |
| `Additive<T>` | `zero() -> T`, `add(left: T, right: T) -> T` | Every sized integer and float |
| `Multiplicative<T>` | `one() -> T`, `multiply(left: T, right: T) -> T` | Every sized integer and float |
| `Ordered<T>` | `compare(a: T, b: T) -> Ordering` | Every sized integer, `bool` |
| `Parseable<T>` | `parse(text: string) -> Result<T>` | Every sized integer and float, `bool` |

A primitive conforms only through its companion module, which declares `module implements` in the standard library; no type conforms by having functions of the right names, and only a trusted companion may declare a conformance ([E0578](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0578.md)). The companions' `zero`, `add`, `one` and `multiply` are the operators at the type's width: integer `add` and `multiply` trap on overflow (reason 1), and float ones round as float arithmetic does. `compare` and `parse` are the companions' existing functions. Floats do not conform to `Ordered`, since their companions have no total `compare`; `int` and `float` use the companions of the types they name. A class conforms by listing a contract after `implements` and declaring each requirement as an exported static function.

<!-- example: check -->
```neura
let.promote _ = import:Std.Core.Arithmetic;

let sum = func<T: Additive<T>>(items: T[]) -> T {
    let total = T.zero();
    for (let item in items) {
        total = T.add(total, item);
    }
    return total;
};

let small: int8[] = [1, 2, 3];
let total = sum(small);
let precise = sum<float64>([0.1, 0.2, 0.3]);
```

A contract's name promises nothing beyond its signature: no pass reassociates or reorders calls because a requirement is named `add`, so `precise` is the left-to-right sum, bit for bit. A companion's conformance is checked, and `Std.Core.Arithmetic` loaded, only when a bound needs it, so a program that uses no contract builds the image it built before.

## 7. Primitive companion modules

`Std.Core` has one module per primitive type: `Int8`, `Int16`, `Int32`, `Int64`, `UInt8`, `UInt16`, `UInt32`, `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, `Bool`, and [`String`](StandardLibrary#string). The integer modules have identical member lists, as do the float modules. Each is a separate hand-written source file.

### 7.1 Integer modules

`X` denotes the module's integer type. Parsing uses T1; the other operations use checked integer arithmetic and the modular recipes below.

| Member | Behavior |
| --- | --- |
| Constants `Min`, `Max`, `Bits` | The type's range and width. |
| `abs(v) → X` | Signed types only. Traps (reason 1) for `Min`, from the negation. |
| `sign(v) → int` | -1, 0, or 1. |
| `min(a, b)`, `max(a, b)`, `clamp(v, low, high)` | `clamp` traps with `invalid argument: low exceeds high`. |
| `compare(a, b) → Ordering` | The natural order; a comparator. |
| `pow(base, exponent: int) → X` | Square-and-multiply with checked multiplication. A negative exponent traps. `pow(0, 0)` is 1. |
| `gcd(a, b) → X` | Euclid's algorithm on magnitudes. `gcd(0, 0)` is 0. Traps (reason 1) when the result does not fit. |
| `checked_add`, `checked_sub`, `checked_mul`, `checked_div`, `checked_rem` `→ Optional<X>` | `None` where the operator would trap. Each tests its operands before operating: for addition, `b > Max - a` when both are positive, and the mirror image when both are negative. |
| `wrapping_add`, `wrapping_sub`, `wrapping_mul`, `wrapping_neg` | The result modulo 2^`Bits`, written with checked operators ([section 7.2](#72-arithmetic-without-wrapping-intrinsics)). |
| `saturating_add`, `saturating_sub`, `saturating_mul` | Clamp to `Min` or `Max`. |
| `floor_div(a, b)`, `floor_mod(a, b)` | Quotient rounded toward negative infinity, and the remainder with the divisor's sign. |
| `count_ones`, `leading_zeros`, `trailing_zeros` `→ int` | Bit counts over `Bits` bits, by masks and shifts. |
| `rotate_left(v, n: int)`, `rotate_right(v, n: int)` | `n` is first reduced to 0 through `Bits - 1`. A rotation by 0 returns `v`; otherwise the two shifts have counts between 1 and `Bits - 1`, so neither traps. |
| `to_unsigned(v)`, `to_signed(v)` | The same-width type with the same bit pattern ([section 7.2](#72-arithmetic-without-wrapping-intrinsics)). |
| `hash(v) → uint32` | [Section 5.2](#52-equality-hashing-and-text). |
| `to_string(v) → string` | Decimal. Built by concatenating one-character literals, most significant digit first; negative values are handled in the negative range so `Min` needs no negation. |
| `to_string_radix(v, radix: int) → string` | Lowercase digits. The kernel `neura.integer.format_i64` or `format_u64` writes them (P2a). |
| `format(v, radix: int, min_width: int, pad: string) → string` | As `to_string_radix`, then left-padded with copies of `pad` to `min_width` digits and sign. When `pad` is `0`, padding goes after the sign. |
| `zero() → X`, `add(left, right) → X`, `one() → X`, `multiply(left, right) → X` | The operators at the type's width; `add` and `multiply` trap on overflow (reason 1). They implement `Additive<X>` and `Multiplicative<X>` of [section 6.6](#66-arithmetic), with `compare` and `parse` for `Ordered<X>` and `Parseable<X>`. |
| `parse(text) → Result<X>`, `parse_radix(text, radix: int) → Result<X>` | **T1.** Grammar below. The kernel `neura.integer.parse_i64` or `parse_u64` parses the text as a 64-bit value, which a narrower type then checks against its range (P2a). |

**Parsing grammar.** An optional `+` or `-`, then one or more digits of the radix, letters in either case. No whitespace, separators, or radix prefix. An empty input fails with kind `Empty`; a sign with no digits with `UnexpectedEnd`; any other byte with `InvalidCharacter` at its offset; a value outside the type with `OutOfRange` at offset 0. A `-` on an unsigned type is accepted only for zero. The parser accumulates in the negative range for signed types, so `Min` parses without overflow.

### 7.2 Arithmetic without wrapping intrinsics

The language has no wrapping arithmetic or sign reinterpretation. The checked operators, shifts that discard high bits, and masks are enough. Each recipe below says why no intermediate value leaves its type. The `uint32` and `uint64` multiplication recipes are tested in both build modes.

| Operation | Recipe | Why it fits |
| --- | --- | --- |
| Wrapping add or subtract, widths up to 32 | Convert both operands to the 64-bit type of the same signedness, operate, mask to the width, convert back. For signed types go through the unsigned pattern first. | The sum of two values below 2^32 is below 2^33. |
| Wrapping multiply, widths up to 32 | Convert to `uint64`, multiply, mask, convert back. | The product of two values below 2^32 is below 2^64. |
| Wrapping add, `uint64` | Let `room` be `Max - a`. If `b` exceeds `room`, the result is `b - room - 1`; otherwise `a + b`. | `Max - a` cannot underflow; `b - room - 1` is at least 0 because `b > room`; the other branch is at most `Max`. |
| Wrapping subtract, `uint64` | If `a` is at least `b`, `a - b`; otherwise `Max - (b - a) + 1`. | `b - a` is between 1 and `Max`, so `Max - (b - a)` is at most `Max - 1`. |
| Wrapping multiply, `uint64` | Split each operand into low and high 32-bit halves. The result is `low × low`, plus the low 32 bits of (`high_a × low_b` + `low_a × high_b`) shifted left by 32, with wrapping adds. | Each partial product has two factors below 2^32. The two cross products are each masked to 32 bits before they are added, so their sum is below 2^33. The shift discards the bits above 64 by definition. |
| Multiply by 5, 9, or 31 | A shift and a wrapping add or subtract. | Shifts discard high bits. |
| Signed to unsigned, same width up to 32 | A non-negative value converts directly. A negative value is converted to `int64`, 2^width is added, and the sum is converted. | A negative value is at least -2^(width-1), so the sum is between 2^(width-1) and 2^width - 1. |
| Unsigned to signed, same width up to 32 | A value below 2^(width-1) converts directly. Otherwise convert to `int64`, subtract 2^width, convert. | The difference is between -2^(width-1) and -1. |
| `int64` to `uint64` | A non-negative value converts directly. For a negative value, add `Int64.Max`, add 1, convert, and add 2^63. | Adding `Max` to a negative value gives at least -1; adding 1 gives a value from 0 through 2^63 - 1; adding 2^63 as `uint64` stays below 2^64. |
| `uint64` to `int64` | A value below 2^63 converts directly. Otherwise subtract 2^63, convert, add `Int64.Min`. | The difference is below 2^63, and adding `Min` to it gives a value from `Min` through -1. |
| Limb arithmetic for big numbers | Limbs are `uint32`; every intermediate is `uint64`. | A limb product plus an incoming carry plus an existing limb is at most (2^32 - 1)^2 + 2(2^32 - 1), which is 2^64 - 1. |

Signed wrapping operations convert to the unsigned pattern, operate, and convert back.

**What the recipes do not cover.** A float's bit pattern (N1). Everything else is on this table: hashing uses the 32-bit rows, `Random` and `Sha256` use the `uint64` and `uint32` rows, `BitSet` uses plain masks and shifts on `uint32`, and `BigInteger` uses the limb row.

### 7.3 Float modules

The float companions use N1 for bit-level operations. `is_nan` can also use ordinary float comparison because a NaN differs from itself.

| Member | Behavior |
| --- | --- |
| Constants `Max`, `MinPositive`, `MinNormal`, `Epsilon`, `Infinity`, `NegativeInfinity`, `NaN` | Construct exact boundary and special encodings with `from_bits`. Use the format's canonical quiet NaN. |
| `is_nan`, `is_infinite`, `is_finite`, `is_negative` | `is_negative` reads the sign bit, so it is true for `-0.0`. |
| `abs`, `copy_sign(magnitude, sign)` | Sign-bit operations; never round. |
| `min(a, b)`, `max(a, b)` | NaN if either operand is NaN. `-0.0` is less than `0.0`. |
| `clamp(v, low, high)` | Traps when `low` exceeds `high` or either is NaN. |
| `floor`, `ceil`, `trunc`, `round`, `round_even` | `round` rounds halves away from zero. All preserve the sign of zero. |
| `to_bits(v)`, `from_bits(bits)` | N1 itself. |
| `total_compare(a, b) → Ordering` | The total order of [section 5.3](#53-ordering). |
| `equals(a, b)`, `hash(v)` | [Section 5.2](#52-equality-hashing-and-text). |
| `to_string(v)`, `to_fixed(v, digits: int)`, `to_exponential(v, digits: int)` | [Section 7.5](#75-float-text-conversion). `to_fixed` gives exactly `digits` fraction digits (0 through 100), correctly rounded, ties to even. |
| `parse(text) → Result<X>` | Also T1. |
| `zero() → X`, `add(left, right) → X`, `one() → X`, `multiply(left, right) → X` | The operators, rounded as float arithmetic is. They implement `Additive<X>` and `Multiplicative<X>` of [section 6.6](#66-arithmetic), with `parse` for `Parseable<X>`. |

### 7.4 `Bool`

`to_int(v) → int` gives 1 or 0; `xor(a, b)`; `compare(a, b) → Ordering`; `hash(v)`; `to_string(v)`. `parse(text) → Result<bool>` accepts exactly `true` and `false` and needs T1. `bool` conforms to `Ordered<bool>` and `Parseable<bool>` of [section 6.6](#66-arithmetic).

### 7.5 Float text conversion

**`to_string`.** The output is the shortest decimal digit string that parses back to the same value:

1. NaN is `NaN`; infinities are `Infinity` and `-Infinity`; zeros are `0.0` and `-0.0`.
2. Otherwise find the decimal number with the fewest significant digits that lies in the interval of values rounding to this float under ties-to-even parsing. Among several, take the one closest to the exact value; if still tied, the one with an even last digit.
3. With digits `d1 d2 ... dn` and decimal exponent `e` (the value is `d1.d2...dn × 10^e`): if `e` is at least -4 and less than 16, print positional notation with at least one digit on each side of the point (`1.0`, `0.001`, `123456.7`). Otherwise print `d1`, a point, the remaining digits or `0`, `e`, and the exponent with `-` when negative and no `+` (`1.0e16`, `2.5e-7`).
4. Prefix `-` for negative values.

Every finite output is a valid Neura float literal for its type. The digits are computed for the value's own format.

**`parse`.** Accepts `NaN`, `Infinity`, `+Infinity`, `-Infinity`, or: an optional sign, digits with an optional point (at least one digit overall), and an optional exponent. The decimal value is converted exactly and rounded once, ties to even. A magnitude too large becomes an infinity and one too small becomes a signed zero; neither is an error, unlike a source literal.

**Reference arithmetic.** Both directions are defined by exact integer arithmetic on the significand and powers of 2 and 10. `Std.Core` contains an unexported class `WideUint`, a non-negative integer stored as a built-in `uint32` list of limbs, least significant first, with add, subtract, compare, multiply by a `uint32`, shift, and division with remainder by a `uint32`, all by the limb recipe of section 7.2. A faster algorithm may replace it only if its output is identical for every `float16` and `bfloat16` value (checked exhaustively) and for the test corpus of [section 17](#17-verification).

## 8. `Std.Text`

### 8.1 `Strings`

All of `Strings` needs T1. String operations are functions that take the string first. All positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

| Member | Behavior |
| --- | --- |
| `byte_length(s) → int` | Constant time. |
| `scalar_count(s) → int` | Counts bytes that are not continuation bytes. Linear. |
| `is_empty(s) → bool` | |
| `byte_at(s, offset) → uint8` | |
| `scalar_at(s, offset) → uint32` | Decodes the character starting at `offset`. |
| `is_boundary(s, offset) → bool` | True for 0, for the byte length, and for any offset whose byte is not a continuation byte. False out of range. |
| `scalars(s)` | A built-in list of `uint32`: the scalar values in order. For-each works on it directly. |
| `scalar_offsets(s)` | A built-in list of `[int, uint32]`: each starting offset with its scalar value. |
| `slice(s, start, end) → string`, `slice_from(s, start) → string` | Both ends on boundaries, `start` not above `end`. |
| `equals(a, b) → bool`, `hash(s) → uint32`, `compare(a, b) → Ordering` | [Sections 5.2](#52-equality-hashing-and-text) and [5.3](#53-ordering). |
| `starts_with(s, prefix)`, `ends_with(s, suffix)`, `contains(s, needle)` `→ bool` | The empty string is a prefix, suffix, and substring of every string. |
| `index_of(s, needle) → int`, `index_of_from(s, needle, from) → int` | Offset of the first occurrence at or after `from`, or -1. An empty needle matches at `from`. |
| `last_index_of(s, needle) → int` | Offset of the last occurrence, or -1. |
| `split(s, separator)` | A built-in list of strings: every piece between occurrences, left to right without overlap. Empty pieces are kept, so `a,,b,` gives four pieces and the empty string gives one. An empty separator traps. |
| `split_limit(s, separator, limit)` | At most `limit` pieces; the last holds the remainder. `limit` below 1 traps. |
| `lines(s)` | Splits at LF; a CR immediately before an LF is removed; a final empty piece after a trailing LF is dropped. |
| `join(parts: string[list], separator) → string` | |
| `repeat(s, count) → string` | A negative count traps. |
| `trim(s)`, `trim_start(s)`, `trim_end(s)` | Remove space, tab, LF, vertical tab, form feed, and CR. |
| `strip_prefix(s, prefix)`, `strip_suffix(s, suffix)` `→ Optional<string>` | The remainder when the affix is present. |
| `replace(s, target, replacement)`, `replace_first(...)` | Every non-overlapping occurrence, left to right. An empty target traps. |
| `to_ascii_upper(s)`, `to_ascii_lower(s)`, `equals_ignore_ascii_case(a, b)` | Change or ignore only the letters A through Z and a through z. |
| `pad_start(s, width, pad: string)`, `pad_end(...)` | Pads to `width` scalar values with copies of `pad`, which must be one scalar value. |
| `reverse(s) → string` | Reverses the order of scalar values. |
| `to_bytes(s)` | A built-in list of `uint8`, a copy. |
| `from_bytes(bytes: uint8[list]) → Result<string>` | `Encoding` error with the offset of the first invalid byte. |
| `from_bytes_lossy(bytes: uint8[list]) → string` | Each maximal invalid sequence becomes one U+FFFD. |
| `from_scalars(scalars: uint32[list]) → string` | Traps for a value that is not a Unicode scalar value. |

Functions that return several strings return a built-in list, not an `ArrayList`: the built-in for-each works on it, and `Std.Text` then needs nothing from `Std.Collections`.

**Search algorithm.** The searches, comparisons, trims, splits, `lines`, replacements, ASCII case mappings, `scalar_count`, and `hash` run on the `neura.text`, `neura.hash`, and `neura.encoding` kernels (P2a). The text searches use the two-way string-matching algorithm, so a search is linear in the length of the text it reads plus the needle's length, never their product. A kernel is charged by the work its arguments allow (P2a), so the steps of each member are linear: `index_of` and `index_of_from` search in windows that start at 128 bytes and double, so their steps grow with the distance from `from` to the occurrence rather than with the rest of the text, and a loop that calls `index_of_from` after each occurrence is linear in the text; `split`, `split_limit`, and `lines` find every separator in two kernel calls over the text (one counts, one writes the offsets) and then slice the pieces, so they are linear in the text plus the number of pieces.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator, the `neura.encoding.utf8_check` and `utf8_validate` kernels, must agree with the validation NVM performs when constructing text; its Neura original is the oracle of the `strings` and `utf8` tests.

**Unicode scope.** `Std.Text.Strings` handles UTF-8 encoding, decoding, and iteration for all Unicode scalar values. Its original ASCII case and classification methods keep their scope. The [`String` companion](StandardLibrary#string) adds a fixed whitespace set for strip and indentation, Unicode 17.0.0 case conversion and folding, and standard charset conversion from `Std.Text.Charsets`. These are ordinary Neura routines with embedded Unicode data and use no host locale or charset services.

### 8.2 `StringBuilder`

A class in `Std.Core` that accumulates text. Needs T1. The module function `create()` makes a builder: a method cannot name its own receiver, so the builder keeps a reference to itself that the factory sets, and `new StringBuilder()` is not available outside the module. Every `append` member returns that same builder.

- **Layout.** One built-in `uint8` list, always valid UTF-8 because every append adds whole characters.

| Member | Behavior |
| --- | --- |
| `append(s: string) → StringBuilder` | Returns the builder for chaining. |
| `append_scalar(scalar: uint32)`, `append_line(s)`, `append_repeated(s, count: int)` | `append_line` appends `s` and one LF. |
| `append_int(v: int64)`, `append_uint(v: uint64)`, `append_float(v: float64)`, `append_bool(v: bool)` | Append the value's text without building an intermediate string. |
| `append_value<T>(value: T)` | Appends `value.to_string()`. **G3.** |
| `byte_length: int` | A get-only field ([accessor](Types#accessors)): the number of bytes, read as `builder.byte_length` like a string's. |
| `is_empty() → bool` | |
| `truncate(byte_length: int)`, `clear()` | `truncate` requires a character boundary. |
| `to_string() → string` | Constructs a string from the bytes. The builder stays usable. |
| `equals(other: class) → bool`, `hash_code() → uint32` | Compare and hash the current UTF-8 bytes. The hash changes with the contents, so a builder used as a hash key must stay unchanged while stored. |

### 8.3 `Scalars`

**There is no `char` type.** A code point is a `uint32` holding a Unicode scalar value. `StringBuilder.append_scalar` and `Strings.from_scalars` print a code point as its character explicitly.

`Scalars` is a module of functions on `uint32`: `is_valid` (at most 0x10FFFF and not a surrogate), `utf8_length` (1 through 4), `is_ascii`, `is_ascii_digit`, `is_ascii_hex_digit`, `is_ascii_alphabetic`, `is_ascii_alphanumeric`, `is_ascii_uppercase`, `is_ascii_lowercase`, `is_ascii_whitespace`, `is_ascii_punctuation`, `is_ascii_control`, `to_ascii_upper`, `to_ascii_lower`, `digit_value(scalar, radix: int) → int` (-1 when it is not a digit of the radix), and `from_digit(value: int, radix: int) → uint32` (lowercase; traps when out of range). Each is a range comparison; none uses a table. The constant `Replacement` is 0xFFFD. [`Character`](#85-character) classifies and maps every code point with the Unicode data.

### 8.4 `Regex`

A class holding a compiled regular expression. Needs T1 only: its types are all concrete. Matching runs in time proportional to the pattern size times the input length and never backtracks.

**Syntax.** Literals; `.`; character classes with ranges and negation; `\d`, `\w`, `\s` and their negations (ASCII definitions); `\t`, `\n`, `\r`, `\\`, escaped metacharacters, and `\u{...}`; anchors `^` and `$`; `\b` and `\B`; grouping, non-capturing groups, and named groups; alternation; the quantifiers `*`, `+`, `?`, `{n}`, `{n,}`, `{n,m}`, each with a lazy form. **Not supported:** backreferences, lookahead, lookbehind, possessive quantifiers, and Unicode property classes; a pattern that uses them fails to compile.

**Compilation.** The module functions `compile(pattern) → Result<Regex>` and `compile_with(pattern, flags: RegexFlags) → Result<Regex>`. `RegexFlags` is a class with three `bool` fields: `ignore_ascii_case`, `multi_line`, `dot_all`. A recursive-descent parser builds a syntax tree, which is a concrete recursive payload enum (literal, class, concatenation, alternation, repetition, group, assertion). A compiler turns it into a program whose instructions are another payload enum: scalar, any, class, split (two targets in priority order), jump, save, assert, and match. Limits: a repetition count of at most 1,000, at most 10,000 instructions, and group nesting of at most 250. Exceeding a limit, or a syntax error, returns a `Parse` error with the pattern offset.

**Matching.** A Pike machine: it advances a list of threads over the input one scalar value at a time, in priority order, with at most one thread per instruction per position. Each thread carries its capture positions. The result is the leftmost match, and among matches at that start the one a backtracking matcher would find first. It reports byte offsets.

| Member | Behavior |
| --- | --- |
| `is_match(input) → bool`, `matches_fully(input) → bool` | |
| `find(input) → RegexMatch`, `find_from(input, offset) → RegexMatch` | The first match at or after the offset, or `nil` when none exists. A successful match is always a real object, including an empty match. |
| `find_all(input)` | A built-in list of `RegexMatch`: successive non-overlapping matches. After an empty match the search resumes one scalar value later. |
| `replace_all(input, replacement) → string`, `replace_first(...)` | `$0` through `$99` and `${name}` insert groups; `$$` is a dollar sign. A reference to a group that does not exist traps; a group that did not participate inserts nothing. |
| `split(input)` | A built-in list of strings, with the rules of `Strings.split`. |
| `group_count: int`, `pattern: string` | Get-only fields ([accessors](Types#accessors)), set at compilation. |

`RegexMatch` is an immutable class: the get-only fields `start` and `end` (byte offsets), `text()`, `group(index) → Optional<string>`, `group_start(index) → int` (-1 when the group did not participate), and `named_group(name) → Optional<string>`. Group 0 is the whole match. Its capture positions are kept in a list, so it is not marked `#Immutable` and tasks cannot share it.

### 8.5 `Character`

`Std.Core.Character` is Java's `Character` over Unicode 17.0.0, the version of the `String` case tables, in the library's snake_case names. It has the two shapes Java has:

- **Module functions on code points.** Each takes a code point as a `uint32`, as Java's static methods take an `int`, and is total over `uint32`: a value above 0x10FFFF has the properties of an unassigned code point, and only the members that build text, UTF-8, or UTF-16 trap for it. The surrogate code points U+D800 through U+DFFF have the category `Surrogate`. There is still no `char` type ([section 8.3](#83-scalars)).
- **The class `Character`**, an `#Immutable` value holding one Unicode scalar value, the get-only field `code_point`. `of(code_point)` and `new Character(code_point)` trap with `invalid argument: not a Unicode scalar value` for a surrogate or a value above 0x10FFFF, because a `Character`'s text must be one character; `try_of` returns `None` instead, and `from_text(text)` gives the character of a string that holds exactly one scalar value. `equals` compares the code points, `hash_code` is the code point (Java's `hashCode` is the `char` value, and a `uint32` hashes to itself, [section 5.2](#52-equality-hashing-and-text)), `compare_to` orders by code point, and `to_string` is the character. Its methods apply the common module functions to the value: the classification predicates through `is_mirrored`, `get_type`, `get_directionality`, the case mappings (which return `Character`), `digit`, `numeric_value`, `char_count`, and `utf8_length`. The identifier, emoji, and `bidi_class` functions have no method; call them with `code_point`.

| Member | Behavior | Java |
| --- | --- | --- |
| `MinCodePoint`, `MaxCodePoint`, `MinSupplementaryCodePoint`, `Min`/`MaxHighSurrogate`, `Min`/`MaxLowSurrogate`, `Min`/`MaxSurrogate`, `MinRadix`, `MaxRadix`, `UnicodeVersion` | 0, 0x10FFFF, 0x10000, the surrogate bounds, 2, 36, and `"17.0.0"`. | The constants of the same names |
| `get_type(cp) → GeneralCategory`, `category_code(c)` | The General_Category, an enum in Unicode's order (`UppercaseLetter` … `Unassigned`), and its two-letter code. | `getType` |
| `get_directionality(cp) → BidiClass` | The Bidi_Class of a defined code point; `Undefined` when the category is `Unassigned`, including noncharacters. | `getDirectionality` |
| `bidi_class(cp)`, `bidi_code(b)` | The Bidi_Class with the UCD's defaults for unassigned code points (`RightToLeft` in the Hebrew block, `BoundaryNeutral` for noncharacters), and its abbreviation. | |
| `is_upper`, `is_lower` | The Uppercase and Lowercase properties: category Lu or Ll, or Other_Uppercase or Other_Lowercase. | `isUpperCase`, `isLowerCase` |
| `is_title_case`, `is_letter`, `is_digit`, `is_letter_or_digit` | Category Lt; L; Nd; L or Nd. | `isTitleCase`, `isLetter`, `isDigit`, `isLetterOrDigit` |
| `is_alphabetic`, `is_ideographic`, `is_mirrored` | The Alphabetic, Ideographic, and Bidi_Mirrored properties. | `isAlphabetic`, `isIdeographic`, `isMirrored` |
| `is_alpha`, `is_numeric`, `is_alpha_numeric` | A letter; a code point with a Unicode Numeric_Value (any Numeric_Type but None, including fractions, Roman numerals, and CJK ideographs with Unihan values); either. Python's `str.isalpha`, `isnumeric`, and `isalnum` for one character. | |
| `is_whitespace` | Java's set: category Zs, Zl, or Zp except U+00A0, U+2007, and U+202F, and U+0009 through U+000D and U+001C through U+001F. The `String` companion's strip and indentation use it. | `isWhitespace` |
| `is_space_char` | Category Zs, Zl, or Zp. | `isSpaceChar` |
| `is_space` | The White_Space property: U+0009 through U+000D, U+0020, U+0085, U+00A0, and the separators. | Not Java's deprecated `isSpace`, which tests five ASCII characters |
| `is_iso_control`, `is_defined` | U+0000 through U+001F and U+007F through U+009F; a category other than `Unassigned` (surrogates and private use are defined, noncharacters are not). | `isISOControl`, `isDefined` |
| `is_identifier_start`, `is_identifier_part`, `is_identifier_ignorable` | Java's profile of UAX #31: ID_Start and U+2E2F; ID_Continue, U+2E2F, and the ignorable set; U+0000 through U+0008, U+000E through U+001B, U+007F through U+009F, and category Cf. | `isUnicodeIdentifierStart`, `isUnicodeIdentifierPart`, `isIdentifierIgnorable` |
| `is_java_identifier_start`, `is_java_identifier_part` | A letter or category Nl, Sc, or Pc; a letter, category Sc, Pc, Nd, Nl, Mc, or Mn, or the ignorable set. | `isJavaIdentifierStart`, `isJavaIdentifierPart` |
| `is_emoji`, `is_emoji_presentation`, `is_emoji_modifier`, `is_emoji_modifier_base`, `is_emoji_component`, `is_extended_pictographic` | The UTS #51 properties. | `isEmoji` … `isExtendedPictographic` |
| `to_upper`, `to_lower`, `to_title` `→ uint32` | The simple one-to-one mappings of UnicodeData.txt; the code point itself when there is none. The titlecase mapping is the uppercase one where UnicodeData.txt gives none. | `toUpperCase`, `toLowerCase`, `toTitleCase` |
| `to_upper_string(cp)`, `to_lower_string(cp)`, each also with a language tag | The full mappings of `String.to_upper_case` and `to_lower_case` ([String](StandardLibrary#unicode-case-and-locale)): U+00DF gives `"SS"`, U+0130 gives `i` and U+0307, and `"tr"` maps `i` to U+0130. A lone U+03A3 has no cased letter before it and maps to U+03C3. | `String.toUpperCase` on one character |
| `digit(cp, radix) → int` | The decimal digit value of a category Nd code point, or 10 through 35 for A through Z and a through z in ASCII or fullwidth form, when below the radix; otherwise -1. | `digit`; Java returns -1 for a bad radix, this traps |
| `for_digit(value, radix) → uint32` | The lowercase ASCII digit. | `forDigit`; Java returns U+0000 for bad input, this traps |
| `numeric_value(cp) → int` | 10 through 35 for the Latin letters as in `digit`; the Numeric_Value when it is an integer from 0 through 2^31 - 1; -2 for a fraction, a negative value, or a larger value; -1 for none. | `getNumericValue`, over the full Numeric_Value property |
| `numeric_value_text(cp) → Optional<string>` | The exact Numeric_Value, such as `"1/2"`, `"-1/2"`, or `"1000000000000"`. | |
| `is_valid_code_point`, `is_scalar_value`, `is_bmp`, `is_supplementary` | At most 0x10FFFF; and not a surrogate; below 0x10000; 0x10000 through 0x10FFFF. | `isValidCodePoint`, `isBmpCodePoint`, `isSupplementaryCodePoint` |
| `is_surrogate`, `is_high_surrogate`, `is_low_surrogate`, `is_surrogate_pair(high, low)` | Range tests on a `uint32` unit. A `uint16` unit, such as a `String.char_at` result, takes `as uint32`: `uint16` overloads would make every call with a literal, such as `is_surrogate(55296)`, ambiguous, since a literal fits both alternatives only as a contextual match ([overloads](CoreLanguage#function-overloads)). | The methods of the same names |
| `to_code_point(high, low)`, `high_surrogate(cp)`, `low_surrogate(cp)` | Surrogate pair conversion. A pair that is not one, or a code point that is not supplementary, traps. | `toCodePoint`, `highSurrogate`, `lowSurrogate`, which do not validate |
| `char_count(cp)`, `to_chars(cp) → uint16[list]` | The UTF-16 units: one below 0x10000, where a surrogate code point gives itself, and a pair above. `char_count` does not validate; `to_chars` traps above 0x10FFFF. | `charCount`, `toChars` |
| `code_point_at(units, i)`, `code_point_before(units, i)`, `code_point_count(units, start, end)`, `offset_by_code_points(units, i, n)` | The `uint16[list]` forms. An unpaired surrogate counts as one code point. A range or a move outside the list traps. | The `char[]` forms |
| `utf8_length(cp)`, `utf16_length(cp)`, `to_utf8(cp)` | Byte and unit counts and the UTF-8 bytes of a scalar value. | |
| `to_string(cp)`, `hash(cp)`, `compare(a, b) → Ordering`, `reverse_bytes(unit: uint16)` | The one-character string (traps unless a scalar value), the code point, numeric order, and the unit with its bytes swapped. | `toString(int)`, `hashCode(char)`, `compare`, `reverseBytes` |
| `get_name(cp) → Optional<string>` | Java's name: the Unicode Name of a code point that UnicodeData.txt lists by name (`LATIN SMALL LETTER A`, `CJK COMPATIBILITY IDEOGRAPH-F900`); for a control, its Unicode 1.0 name (`NULL`, `LINE FEED (LF)`), except `BEL` for U+0007 and the aliases `PADDING CHARACTER`, `HIGH OCTET PRESET`, and `SINGLE GRAPHIC CHARACTER INTRODUCER` for U+0080, U+0081, and U+0099; for every other assigned code point, the block form: the block's name in capitals with its spaces, hyphens, and underscores as spaces, a space, and the code point in uppercase hex without leading zeros (`CJK UNIFIED IDEOGRAPHS 4E00`, `HANGUL SYLLABLES AC00`, `TANGUT 17000`, `HIGH SURROGATES D800`, `PRIVATE USE AREA E000`, `LATIN 1 SUPPLEMENT 84`). `None` for an unassigned code point or noncharacter and above 0x10FFFF. | `getName`; Java throws above 0x10FFFF |
| `unicode_name(cp) → Optional<string>` | The Unicode Name property, generated for the Hangul syllables (`HANGUL SYLLABLE GAG`) and the patterned ranges of DerivedName.txt (`CJK UNIFIED IDEOGRAPH-4E00`, `TANGUT IDEOGRAPH-17000`, `KHITAN SMALL SCRIPT CHARACTER-18B00`, `NUSHU CHARACTER-1B170`, `EGYPTIAN HIEROGLYPH-13460`, `CJK COMPATIBILITY IDEOGRAPH-F900`). `None` for controls, surrogates, private use, unassigned code points, and above 0x10FFFF. | Python's `unicodedata.name` |
| `code_point_of(name) → uint32`, `try_code_point_of(name) → Optional<uint32>` | The code point with a name that `get_name` or `unicode_name` returns. The text is compared after removing leading and trailing characters up to U+0020 and uppercasing with the root rules, so `" latin small letter a "` and `"croß mark"` match. Name aliases other than Java's control names, the loose matching of UAX #44, and named sequences do not. `code_point_of` traps for any other text; `try_code_point_of` returns `None`. | `codePointOf`; unlike Java, it also accepts the Unicode names of the Hangul syllables and the CJK unified and Tangut ideographs |

**Traps.** Each message follows `invalid argument: `: `not a Unicode scalar value` (`of`, `new Character`, `to_string`, `to_upper_string`, `to_lower_string`, `utf8_length`, `utf16_length`, `to_utf8`), `not a Unicode code point` (`to_chars`), `not a surrogate pair` (`to_code_point`), `not a supplementary code point` (`high_surrogate`, `low_surrogate`), `radix outside 2 through 36` (`digit`, `for_digit`), `digit out of range` (`for_digit`), `range outside the list` (`code_point_count`), `offset outside the list` (`offset_by_code_points`), `not a Unicode character name` (`code_point_of`), and `NoBlock has no range` (`UnicodeBlock.first` and `last`). A UTF-16 index outside the list in `code_point_at` and `code_point_before` is the list read's own trap.

**Not provided.** Java members that have no counterpart, and why: `MIN_VALUE`, `MAX_VALUE`, `SIZE`, `BYTES`, `charValue`, `valueOf(char)`, and `hashCode(char)` concern the 16-bit `char`, which Neura does not have (`code_point`, `of`, and `hash` take their place); the deprecated `isJavaLetter`, `isJavaLetterOrDigit`, and `isSpace`; `toChars(int, char[], int)` and the `char[]` overloads with an offset, count, start, or limit, since a list slice serves; `codePointAt(CharSequence, int)` and its relatives, which are `String.code_point_at` and its relatives on strings; `describeConstable`; and the deprecated constructor.

`Std.Core.UnicodeScript` and `Std.Core.UnicodeBlock` are Java's `Character.UnicodeScript` and `Character.UnicodeBlock`. Each exports a generated enum, `UnicodeScript` with one variant per Script value sorted by long name, and `UnicodeBlock` with `NoBlock` and the blocks of Blocks.txt in code point order, and the module functions `of(cp)` (`Unknown` or `NoBlock` for a code point outside every script or block and above 0x10FFFF), `name(v)` (`Old_Italic`, `Latin-1 Supplement`), `for_name(text) → Optional` (long names, codes, and aliases under the loose matching of UAX #44: ASCII case, spaces, underscores, and hyphens are ignored), and `all()`. `UnicodeScript.code(v)` is the ISO 15924 code, and `UnicodeBlock.first(v)` and `last(v)` are a block's bounds, which trap for `NoBlock`.

<!-- example: check -->
```neura
let character = import:Std.Core.Character;
let unicode_script = import:Std.Core.UnicodeScript;

let sigma = character.of(931);
let lower = sigma.to_lower();                        // σ, U+03C3
let category = character.get_type(453);             // GeneralCategory.TitlecaseLetter
let title = character.to_title(454);                 // U+01C5
let sharp_s = character.to_upper_string(223);        // "SS"
let fifty = character.numeric_value(8556);           // 50, ROMAN NUMERAL FIFTY
let script = unicode_script.of(1488);                // UnicodeScript.Hebrew
let java_name = character.get_name(44032);           // Some("HANGUL SYLLABLES AC00")
let unicode_name = character.unicode_name(44032);    // Some("HANGUL SYLLABLE GA")
let face = character.code_point_of("grinning face"); // 128512
```

**Data and lookup.** `scripts/generate-unicode-tables.py` writes the property tables from the Unicode Character Database 17.0.0 as Perl's `Unicode::UCD` exposes it (the names below come from the UCD files); it checks the version and the SHA-256 of the data it reads, and `--check` compares the checked-in files. Each table is a string of printable bytes, six bits each: a range table holds the first code point of each range with its value, and a boundary table the boundaries of a property's inversion list. A lookup bisects the table, so it reads about log2(ranges) entries: 4,144 General_Category ranges, 1,268 Bidi_Class ranges, 1,510 case ranges, 2,215 numeric ranges, and between 2 and 1,598 boundaries for each binary property. The tables come to about 88 KB of source; a program that does not call a member does not carry its table.

**Names.** `get_name` follows Java's documented rule ([Java SE 25 `getName`](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/lang/Character.html#getName(int))): the Unicode Name for a code point that UnicodeData.txt lists by name, and the block form for one it lists only as part of a range, such as the CJK unified ideographs, Hangul syllables, and Tangut ideographs. The names of the controls, which have no Unicode Name, were observed with OpenJDK 17.0.21: the Unicode 1.0 name where UnicodeData.txt has one, `BEL` for U+0007, whose Unicode 1.0 name `BELL` is the name of U+1F514, the figment aliases of U+0080, U+0081, and U+0099, and the block form for U+0084. `unicode_name` is the Unicode Name property, which Java does not offer. `code_point_of` accepts both kinds of names, so `code_point_of(get_name(cp))` and `code_point_of(unicode_name(cp))` give `cp` back for every named code point. It accepts no other aliases, as Java does not.

Of the Unicode names, 34,594 are stored. The others, about 125,000, are generated from the code point: the Hangul syllables from the jamo short names of Jamo.txt (Unicode section 3.12), and the patterned ranges from their prefix and the code point in hex. The stored names are front-coded in code point order: each record gives the number of leading words it shares with the previous name, then its other words, each spelled out or as a code into a dictionary of 2,122 frequent words. Every block of 32 records starts with a whole name, so `get_name` decodes at most 32 records. `code_point_of` reads a trailing hex number or Hangul jamo first. For any other name, it decodes the blocks whose names start with the same first word. The tables are `Std.Core.Internal.UnicodeNames` (314 KB of source, 236 KB of it the records), which `scripts/generate-unicode-tables.py` writes from the pinned UCD 17.0.0 files UnicodeData.txt, DerivedName.txt, NameAliases.txt, Jamo.txt, and SpecialCasing.txt, not from Perl. Only the four name members import the tables. A program that does not call them builds the same image as before they existed: byte for byte with `--no-source-map`, and differing only in the line numbers of `character.nra` with the source map. A program that calls `get_name` grows from 370 KB to 856 KB, and one that calls `code_point_of` to 1.03 MB. About 300 KB of that growth is the tables, and the rest is the circuits for the operations the lookup uses.

**Verification.** `tests/stdlib/character` checks every code point from 0 through 0x10FFFF, for each of 38 module functions (every predicate, `get_type`, `get_directionality`, `bidi_class`, the three simple case mappings, `digit` with radix 36, `numeric_value`, and the script and block), against digests that its generator computes per code point from the unpacked UCD data; it adds targeted cases (ASCII, Latin-1, final sigma, dotless i, titlecase digraphs, surrogates, private use, noncharacters, unassigned code points, and the largest code point) and spot checks of `numeric_value_text`, the full-case strings, and `digit` at other radices. This proves the packing and the lookups against the Perl 17.0.0 data. Before writing the test, the generator checks that data against three other sources and stops on any difference it does not list: Python's `unicodedata` (Unicode 15.1.0) for every code point assigned by 15.1, for the category, bidi class, mirroring, decimal and numeric values, `isalpha`, `isnumeric`, `isalnum`, `isupper`, `islower`, and the one-to-one upper, lower, and title mappings (the listed differences are 16.0 and 17.0 changes); Perl's regular expression engine for every code point and the binary properties (Alphabetic, Uppercase, Lowercase, Ideographic, ID_Start, ID_Continue, Bidi_Mirrored, White_Space, the six emoji properties) and the categories L, Lt, Nd, and Cn; and the simple case mappings against the 17.0.0 case tables of `String`, built from the unmodified UCD files. The script and block tables and the Unicode 16.0 and 17.0 additions to the Bidi_Class and numeric tables have no source other than the Perl data. Parity with Java is by definition, taken from the Java SE 25 documentation, except for the names, which a Java runtime checked; no Java runtime is part of the tests.

`names-tests.nra` checks `get_name` and `unicode_name` for every code point, against per-plane digests that `generators/names.py` computes from the unpacked UCD files. It also checks that `code_point_of` gives back the code point of every name of both kinds, 299,382 Java names and 159,801 Unicode names. Before writing the test, the generator compares the Unicode names with Perl's `Unicode::UCD` 17.0.0 for every code point, with Python's `unicodedata` for every code point assigned by 15.1, and with ICU 77's `Any-Name` transliterator (Unicode 16.0) for every code point assigned by 16.0. It compares the Java names with every name that OpenJDK 17.0.21's `Character.getName` returns (`generators/JavaNames.java`, Unicode 13.0). The targeted cases of `character-tests.nra` come from the same JDK (`generators/JavaCodePointOf.java`). Each comparison agrees except for two listed differences. Perl names U+18D00 through U+18D1E `TANGUT IDEOGRAPH SUPPLEMENT-…`, where DerivedName.txt and ICU have `TANGUT IDEOGRAPH-…`. CPython generates no names for the Tangut ideographs.

## 9. `Std.Collections`

### 9.1 Contracts

This section fixes the member sets that the concrete classes and the interfaces in [section 9.10](#910-interfaces-and-read-only-views) share.

The **Needs** column names the compiler facilities of [section 3](#3-language-facilities) that a member uses.

**List contract** (`ArrayList`, `LinkedList`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size() → int`, `is_empty() → bool` | | |
| `get(index) → T`, `set(index, value)` | `set` is not structural. | |
| `first() → T`, `last() → T` | Trap with `no such element` when empty. | |
| `add(value)`, `add_at(index, value)`, `add_all(items: T[list])` | `add` appends. `add_at` accepts an index equal to the size. | |
| `remove_at(index) → T`, `remove_first() → T`, `remove_last() → T` | The last two trap when empty. | |
| `swap(i, j)`, `reverse()`, `fill(value)`, `truncate(size)`, `clear()` | `truncate` traps for a size above the current size. | |
| `slice(start, end)` | A new collection of the same class. It copies; there are no sublist views. | |
| `to_list()` | A new built-in list in order. | |
| `iterator()` | [Section 5.4](#54-iteration). | |
| `first_or_none()`, `last_or_none()` `→ Optional<T>` | | G2 |
| `for_each(action: (T) → void)` | In order. | G1 |
| `remove_if(drop: (T) → bool) → int`, `retain_if(keep: (T) → bool) → int` | The callback runs exactly once per element, in order. Returns how many were removed. | G1 |
| `index_where(test: (T) → bool) → int` | The first matching index, or -1. | G1 |
| `sort(c: (T, T) → Ordering)`, `binary_search(value, c) → int` | [Section 9.11](#911-sorting-and-searching). | G1 |
| `sequence() → Sequence<T>` | | G1 |
| `contains(value) → bool`, `index_of(value) → int`, `last_index_of(value) → int`, `remove(value) → bool` | By `equals`. | G3 |
| `equals(other)`, `hash_code()`, `to_string()` | [Section 9.2](#92-equality-of-collections). | G3 |

**Queue and deque contracts** (`ArrayDeque`, `LinkedList`; the queue part also `PriorityQueue`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size()`, `is_empty()`, `clear()`, `to_list()`, `iterator()` | | |
| `enqueue(value)`, `dequeue() → T` | `dequeue` traps when empty. For a deque, `enqueue` is `add_last` and `dequeue` is `remove_first`. | |
| `add_first(value)`, `add_last(value)`, `remove_first() → T`, `remove_last() → T`, `first() → T`, `last() → T` | Deques only. | |
| `peek() → Optional<T>`, `try_dequeue() → Optional<T>` | | G2 |

**Map contract** (`HashMap`, `TreeMap`, `IntMap`, `StringMap`, `OrdinalMap`)

| Member | Behavior | Needs |
| --- | --- | --- |
| `size()`, `is_empty()`, `clear()` | | |
| `contains_key(key) → bool` | | |
| `get_or(key, fallback: V) → V` | | |
| `require(key) → V` | Traps with `no such element` when the key is absent. | |
| `set(key, value)` | Associates the value, replacing any previous one. | |
| `delete(key) → bool` | Removes the key; returns whether it was present. | |
| `keys()`, `values()`, `entries()` | New built-in lists of `K`, of `V`, and of `[K, V]`, in iteration order. They are copies. | |
| `iterator()` | Yields `[K, V]`. | |
| `put_all(other)` | From a map of the same class, in its iteration order. | |
| `get(key) → Optional<V>`, `put(key, value) → Optional<V>`, `remove(key) → Optional<V>` | `put` and `remove` return the previous value. | G2 |
| `for_each(action: (K, V) → void)`, `remove_if(drop: (K, V) → bool) → int` | | G1 |
| `get_or_put(key, make: () → V) → V` | Returns the existing value, or stores and returns the new one. | G1 |
| `merge(key, value, combine: (V, V) → V) → V` | Stores `value` when absent, otherwise the combination of old and new. | G1 |
| `compute(key, f: (K, Optional<V>) → Optional<V>) → Optional<V>` | Stores a `Some` result and removes the key for `None`. | G1, G2 |
| `contains_value(value) → bool`, `equals`, `hash_code`, `to_string` | | G3 |

`set` and `delete` avoid constructing an `Optional` when the caller does not need the previous value.

**Set contract** (`HashSet`, `TreeSet`, `BitSet`): `size`, `is_empty`, `clear`, `contains(value)`, `add(value) → bool` (false for a duplicate), `remove(value) → bool`, `to_list`, `iterator`, `add_all(items: T[list])`, and `is_subset_of`, `is_disjoint_from`, `union`, `intersection`, `difference` with a set of the same class (each of the last three returns a new set of that class: this set's elements first, then for `union` the other's new elements). With G1: `for_each`, `remove_if`, `retain_if`. With G3: `equals`, `hash_code`, `to_string`.

**Sorted contract** (`TreeSet`, `TreeMap`): `first`, `last` (trap when empty), `range(from, to)` (a built-in list of the elements or entries from `from` inclusive to `to` exclusive, ascending), `descending()` (a built-in list in descending order), and `remove_first`, `remove_last`. With G2: `floor`, `ceiling`, `lower`, `higher`, and for maps the `_entry` forms, each returning `Optional`.

### 9.2 Equality of collections

All of this needs G3.

| Kind | Equal when | Hash | Text |
| --- | --- | --- | --- |
| List | Same class and size, with equal elements at each index | The list formula of [section 5.2](#52-equality-hashing-and-text) | `[a, b, c]` |
| Set | Same class and size, and this set contains each of the other's elements | The wrapping sum of the element hashes | `{a, b, c}` in iteration order |
| Map | Same class and size, and for each of the other's entries this map holds an equal value for the key | The wrapping sum over entries of key hash exclusive-or value hash | `{k: v, k2: v2}` in iteration order |
| `ArrayDeque`, `PriorityQueue`, `Sequence` | Identity | Identity | `[a, b, c]` in iteration order; `Sequence` prints `Sequence` |

`equals(other: class)` first tests whether `other` is an instance of the same collection type. It then downcasts and compares contents. Comparing an `ArrayList` with a `LinkedList` by content is the separate `Lists.same_elements` function over the list interface.

### 9.3 `ArrayList<T>`

`ArrayList` is a thin class over one built-in list. Each built-in list operation is a single NVM storage operation; a library-managed backing array would spend graph evaluations on capacity checks for every append and would need runtime-length arrays, which do not exist.

- **Layout.** One built-in list `storage`. Its `version` is the modification counter.
- **Module functions.** `create<T>()`, `of<T>(items: T[list])` (copies), `filled<T>(count, value)`, and `wrap<T>(items: T[list])` (adopts the list without copying).
- **`items() → T[list]`** returns the live built-in list. A built-in for-each over it traps with reason 5 on structural modification. For-each over the wrapper calls its iterator through `Iterable<T>`.
- **Aliasing.** `wrap` and `items` share the same backing list. The wrapper preserves the identity of that list for its lifetime. Its iterators compare the backing version, so modifications through a direct alias are detected as well.

| Operation | Cost | How |
| --- | --- | --- |
| `get`, `set`, `size`, `add`, `remove_last` | One storage operation | Built-in index, length, `append`, and `remove_at` of the last index. |
| `remove_at` | One storage operation | Built-in `remove_at`. |
| `add_at` | One operation per element after the index | At the end, append the new value directly. Otherwise append the last element again, shift the suffix right by one with indexed writes, and store the new value.  |
| `truncate`, `clear` | One operation per removed element | Repeatedly remove the last index from the same backing list.  |
| `remove_if` | Linear | Check the version after each callback, compact the kept elements by indexed writes, then remove the tail from the end. The backing version advances on each actual removal. |
| `reverse`, `fill`, `slice`, `to_list` | Linear | Element reads and writes. |

No removal leaves a stale reference: the built-in list drops the slot, and the removed element is reclaimed.

### 9.4 `ArrayDeque<T>`

The deque is two built-in lists used as stacks. A ring buffer would keep a reference to every removed element until its slot was reused, because the language has no vacant slots.

- **Layout.** `front` holds the first part of the deque in reverse order, so that its last element is the deque's first. `back` holds the rest in order. Plus the modification count.
- `add_first` appends to `front`; `add_last` appends to `back`. `remove_first` removes the last element of `front`; `remove_last` removes the last element of `back`. Each is one storage operation, and the removed slot is gone, so nothing is retained.
- **Rebalancing.** When `remove_first` finds `front` empty and `back` not empty, it moves the first half of `back` (rounded up) into `front` in reverse order and shifts the rest of `back` down, in a helper function. `remove_last` on an empty `back` mirrors it. A rebalance of `n` elements is followed by at least `n / 2` cheap removals from that end, so every end operation is amortized constant time.
- **Indexing.** `get(i)` reads `front[front.length - 1 - i]` when `i` is less than `front.length`, and `back[i - front.length]` otherwise. Constant time.
- **Iteration** is first to last: `front` from its end to its start, then `back`.

### 9.5 `LinkedList<T>`

Nodes are separately allocated and reclaimed by the NVM. Missing neighbors are ordinary nil references.

- **Layout.** An unexported generic class `Node<T>` holds `value`, `next`, and `previous`. The list holds `head`, `tail`, `count`, and the modification count. An empty list has two nil endpoints. The head has no previous node, and the tail has no next node. Every allocated node has an initialized value.
- **Insertion** sets both neighbors of a new node and updates each adjacent link or the corresponding endpoint. **Removal** reconnects each existing neighbor or updates the endpoint. Clear the removed node's links so that an iterator holding the removed node does not retain the remaining chain.
- **Positional access** walks from the first element when the index is in the first half and from the last otherwise.
- **Why nodes and not an arena.** Each element costs one small object either way. The arena saved allocations only by reusing slots through a free list, and it paid for that with array growth copies, a free list to maintain, and vacant slots the language does not have.

It follows both the list and the deque contracts. Prefer `ArrayList` or `ArrayDeque` unless elements are inserted or removed in the middle while iterating by position.

### 9.6 Hash collections

Hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including `IntMap` and `StringMap`.

| Field | Type | Meaning |
| --- | --- | --- |
| `index` | `int` list of length `T` | Open-addressed table. Each slot holds an entry number, -1 (`Empty`), or -2 (`Tombstone`). `T` is a power of two, at least 8. |
| `hashes` | `uint32` list | Each entry's spread hash, in insertion order. Its length is `used`. |
| `live` | `bool` list | False for a removed entry. Same length. |
| `count` | `int` | Live entries. |
| `modifications` | `uint64` | The modification count ([section 5.4](#54-iteration)). |

`E`, the entry capacity, is `T / 2`. At most half of the index is ever non-empty, because each entry occupies one slot. One half is chosen because lookups use linear probing: at half load an unsuccessful search inspects about 2.5 slots on average and a successful one about 1.5, and every inspected entry costs a comparison circuit. The index holds only integers, so a sparse table is cheap.

The core's operations, as the owner uses them:

- **Lookup.** The owner computes the spread hash, asks for the home slot (the hash masked with `T - 1`), and walks slots in increasing order, wrapping. `Empty` ends the search: absent. `Tombstone` continues. An entry number whose cached hash equals the probe's hash is a candidate: the owner compares its stored key with the probe key, and stops on a match. The probe key is the first argument to the equality. The loop terminates because at least half the slots are `Empty`.
- **Claim.** For a new key the owner remembers the first `Tombstone` it passed. The core appends the hash and `true` to its lists, writes the new entry number into the remembered tombstone or the final `Empty` slot, and advances `count` and the modification count. The owner appends the key and value to its lists in the same step, so entry numbers agree.
- **Release.** The slot becomes `Tombstone`, the entry's `live` flag becomes false, `count` decreases, and the modification count advances. If the released entry is the last one, it and any dead entries before it are removed from the ends of all the dense lists at once, so stack-like use retains nothing.
- **Compaction.** The core returns the old entry numbers of the live entries in order. The owner builds new key and value lists in that order; the core rebuilds `hashes`, `live`, and `index` by probing from each cached hash for the first `Empty` slot. No hash or equality function is called. It runs in a helper function that returns the new lists ([section 5.5](#55-resources)).

When compaction happens:

1. A new key arrives and `used` equals `E`, or the live entries plus the tombstones in the index fill `E` (trailing releases leave tombstones behind). If `count` is greater than `E / 2`, `T` doubles first. A `T` above 2^30 traps with `invalid argument: capacity`.
2. A release leaves more dead entries than live ones and at least 8 dead entries. `T` is unchanged.

**Retention bound.** A removed key and value stay in the owner's dense lists, and so stay reachable, until the next compaction. Rule 2 bounds this: a hash collection never holds more dead entries than `max(count, 7)`. Compaction costs time proportional to `count` and happens after more than `count` releases, so removal stays amortized constant time. Nothing observable depends on the retained references, since reclamation runs no code; only heap use does.

`clear` replaces every list with a new one of the initial size. The table never shrinks on its own; `trim()` compacts at the smallest `T` whose `E` holds `count`.

**Costs.** Lookup, insertion, and removal are expected constant time with well-distributed hashes and linear in `used` in the worst case. There is no conversion of long probe sequences into trees. Code that hashes adversarial keys uses `TreeMap`.

#### The collections

| Class | Keys | Key operations | Needs |
| --- | --- | --- | --- |
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing |
| `StringMap<V>` | `string` | `Strings.hash` and `Strings.equals` | T1 |
| `HashMap<K, V>`, `HashSet<T>` created with `with_keys<K, V>(hash: (K) → uint32, same: (K, K) → bool)` | Any | The two function values, stored in the collection | G1 |
| `HashMap<K, V>`, `HashSet<T>` created with `create<K, V>()` | Any | `hash_code` and `equals` | G3 |

- `IntMap` and `StringMap` are permanent, not stopgaps. They follow the map contract with their key type fixed, call their key operations directly without a function value, and are the right choice for those key types after `HashMap` exists.
- `HashMap` stores its two key operations as fields either way. `create` fills them with the universal members, so the rest of the class is written once.
- `HashSet` owns a `HashCore` and a keys list. `HashMap` owns a `HashCore`, a keys list, and a values list.
- `with_capacity` variants of each constructor choose the smallest power-of-two `T` that is at least `max(8, 2n)`; a negative `n` or one above 2^29 traps.
- Setting a value for an existing key overwrites the value only: the entry keeps its position and the modification count does not change. A key removed and added again goes to the end of the order.
- Iteration visits entry numbers in order and skips dead entries.
- A callback passed to `get_or_put`, `merge`, or `compute` must not modify the map ([section 5.6](#56-callbacks)).

### 9.7 `TreeMap<K, V>` and `TreeSet<T>`

Sorted collections ordered by a comparator given at construction. They need G1, because the comparator's type mentions `K`. A comparator is always required; for a built-in type pass its `compare` function. Two keys are the same key when the comparator returns `Equal`; `equals` is not consulted.

The balanced tree is an **AVL tree**. Lookups dominate, each comparison is a call through a function value, and an AVL tree is at most about 1.44 log2 of the size tall against about 2 log2 for a red-black tree.

Nodes are separately allocated. Missing child and parent links use `nil`.

- **Layout.** An unexported class `Node<K, V>` has `key`, `value`, `left`, `right`, `parent`, and `height` (`int`). Every node has a key and value. The tree stores a nullable `root`, `count`, its comparator, and the modification count. The root's parent is nil. A height helper returns 0 for nil without dereferencing it; a leaf has height 1.
- **Lookup** descends from the root calling the comparator with the probe key first: `Less` goes left, `Greater` goes right, `Equal` stops, and reaching `nil` means absent.
- **Insertion** descends the same way. On `Equal` it overwrites the value (not structural). Otherwise it attaches a new node, then walks up through `parent`: recompute each ancestor's height; if its children's heights differ by 2, rebalance with a single rotation when the taller child leans the same way and a double rotation when it leans the other way; stop when an ancestor's height is unchanged.
- **Removal.** A node with two children takes the key and value of its in-order successor, and the successor is unlinked instead. A node with at most one child is replaced by that child. Then walk up, recomputing heights and rebalancing, until a height is unchanged. The unlinked node becomes unreachable and is reclaimed; there is no free list and no slot to clear.
- **Rotations** update the child links and the parent link of each moved non-nil node. If the rotated subtree had no parent, update `root`. Never read or write a field on nil.
- **Iteration** is ascending: start at the leftmost node and step to the in-order successor through `right` and `parent`, with no stack.
- `TreeSet<T>` is a class holding a `TreeMap<T, Unit>`. A separate node class without a value would save one cell per element and duplicate the balancing code.

Lookup, insertion, and removal take logarithmic time.

### 9.8 `PriorityQueue<T>`

A binary min-heap following the queue contract: `dequeue` returns a least element under the comparator given at construction. Needs G1.

- **Layout.** A built-in list in heap order (children of index `i` at `2i + 1` and `2i + 2`), the comparator, and the modification count.
- **Sift up:** while the item is not at the root and the comparator says it is `Less` than its parent, move the parent down.
- **Sift down:** pick the left child, or the right child when it exists and is `Less` than the left; if the child is `Less` than the item, move it up and continue; otherwise stop.
- `dequeue` takes the root, moves the last element to the root, removes the last slot, and sifts down.
- The module function `from_list(items, comparator)` appends everything and then sifts down each index from `size / 2 - 1` to 0.
- Iteration is in heap order, which is not sorted. `drain_sorted()` dequeues everything into a built-in list.

These rules fix which of several equal elements comes out first; they are not first-in first-out among equals.

### 9.9 `BitSet` and `OrdinalMap<V>`


- **`BitSet`** is a set of non-negative `int` values following the set contract. Layout: a built-in `uint32` list of words (bit `i` is in word `i / 32` at position `i % 32`) and a cached `count`. A negative element traps. Setting a bit beyond the last word appends zero words. Besides the contract it has `flip(i)`, the in-place `union_with`, `intersect_with`, `subtract`, and `symmetric_difference_with`, and `next_set(from) → int` (the next member at or after `from`, or -1). Every operation is a mask, a shift by 0 through 31, or a comparison on `uint32`; nothing wraps. Iteration is ascending. Equality is set equality, so trailing zero words do not matter.
- **`OrdinalMap<V>`** is a map from the integers 0 through `n - 1` to values, with `n` fixed at construction. Layout: an `int` list `position` of length `n` (the key's place in the dense lists, or -1), a dense `int` list of keys, and a dense list of values. `set` appends to the dense lists; `delete` moves the last dense entry into the removed place and removes the last slot, so nothing is retained. Every operation is constant time. Iteration is in key order, by scanning `position`.

**Enum keys.** There is no `EnumSet<E>` or `EnumMap<E, V>`. A fieldless enum already converts to its ordinal with `as`, so a set of variants is a `BitSet` of ordinals and a map keyed by variants is an `OrdinalMap<V>` sized by the caller with the number of variants. The caller converts at the call. Going back from an ordinal to a variant is a `match`-style function the enum's author writes. This applies to fieldless enums only: a payload enum has no ordinal conversion, and nothing here constructs one.

### 9.10 Interfaces and read-only views

All of this needs G4. The interfaces are declared in `Std.Collections` and the concrete classes are marked as implementing them; no member changes.

```text
Iterable<T>
└── Collection<T>            size, is_empty, contains, to_list, iterator
    ├── MutableCollection<T> add, remove, clear, ...
    ├── List<T> ──────────── MutableList<T>
    ├── Set<T> ───────────── MutableSet<T>
    │   └── SortedSet<T>
    └── Queue<T> ─────────── Deque<T>

Map<K, V> ── MutableMap<K, V>
└── SortedMap<K, V>
```

- Read-only and mutable interfaces are separate. A function that only reads takes the read-only interface, and no member simulates immutability by trapping.
- Members that need only other members (`is_empty`, `first`, `contains_all`, `for_each`) become default methods written once in the interface.
- `ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are classes that copy their contents at construction and implement only the read-only interfaces. Their module functions `of` take a built-in list (for the map, a list of `[K, V]`; a duplicate key traps). They hold their contents in a list or a library collection, so they are not marked `#Immutable` and are not transferable.
- Members that an interface declares, such as `size()` and `is_empty()`, stay methods in every class, because an interface cannot require an [accessor](Types#accessors); `ArrayList.items()` stays a method with them. The immutable classes and `Sequence` are `#Final` and `#NoDiscard`, and every query that returns an `Optional` (`get`, `first_or_none`, `peek`, `floor`, and the like) is `#NoDiscard` ([Tags](Tags)).
- `Lists.same_elements`, `Sets.same_elements`, and `Maps.same_entries` compare collections of different classes through the read-only interfaces. They also need G3.
- For-each accepts an `Iterable<T>` through G5.

### 9.11 Sorting and searching

The module `Sorting`. Generic sorting needs G1 for the comparator.

**`sort<T>(items: T[list], c: (T, T) → Ordering)`** is stable:

1. Sort each consecutive block of 16 elements (the last may be shorter) in place with binary insertion sort: for each element, binary-search the sorted prefix for the position after the last element not `Greater` than it, and shift the tail right by one.
2. Create a buffer: a second built-in list of the same length, filled by appending the elements themselves. They are references the list already holds, so the buffer keeps nothing extra alive, and no "empty" value is needed.
3. Merge bottom-up. For widths 16, 32, 64, and so on while the width is less than the length, merge each adjacent pair of runs from the source list into the destination, then swap the two lists' roles. If the last element of the left run is not `Greater` than the first of the right run, the pair is copied without comparisons. When merging, take from the right run only when its head is `Less` than the left run's head; this makes the sort stable.
4. If the result ended in the buffer, write it back to `items` by index. Element writes do not change the list's structural version.

It makes at most about `n log2 n` comparator calls. `ArrayList.sort` calls it on `storage`. `sort_range(items, start, end, c)` sorts part of a list.

**`sort_ascending(items)`** sorts an `int64`, `uint64`, `float64`, `int32`, or `uint32` list in ascending order, with the result `sort` gives with the type's `compare` (`Float64.total_compare` for `float64`, so `-0.0` comes before `0.0` and every NaN comes last, in its original order). The kernels `neura.sort.i64`, `u64`, and `f64` do the work (P2a) for one step per eight element comparisons; an `int32` or `uint32` list is widened into a new list for the kernel and written back by index. Like `sort`, it writes elements and leaves the list's version unchanged. The comparator `sort` stays in Neura, because a kernel cannot call back into the program.

**`binary_search<T>(items: T[list], value: T, c) → int`** requires `items` sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index, the first of any equal run. Otherwise it returns `-(insertion point) - 1`.

The module **`Lists`** has helpers for built-in lists that need no callback: `copy<T>(items)`, `concat<T>(a, b)`, `reverse<T>(items)`, `swap<T>(items, i, j)`, `fill<T>(items, value)`, `insert_at<T>(items, index, value)`, `truncate<T>(items, length)`, `clear<T>(items)`, `filled<T>(count, value)`, and `from_array<T>(items: T[])`. There is no conversion from a list to an array, because an array's length must come from a literal.

### 9.12 `Sequence<T>`

A class for lazy pipelines, the counterpart of Java's streams. Needs G1; the members that return `Optional` also need G2.

A sequence is two native function values, and each operation is a closure over the previous two.

- **Layout.** `has_next: () → bool`, `next: () → T`, and a `bool` `consumed`.
- **Why closures fit.** An adapter's state is exactly what a closure captures. `map` captures the upstream pair and the mapping function and needs no state of its own. Operations that do keep state (`take`'s remaining count, `filter`'s buffered element, `enumerate`'s index) hold it in a captured `Cell`, because captured locals are copies ([section 5.6](#56-callbacks)).
- **Single use.** Every operation marks the sequence consumed; a second operation on the same sequence traps with `invalid state: sequence already used`.
- **Evaluation.** Pull-based and sequential. Nothing runs until a terminal operation, and each element passes through the whole pipeline before the next is requested.
- **Sources.** Module functions `from_list<T>(items: T[list])`, `empty<T>()`, `generate<T>(make: () → T)` (infinite), `iterate<T>(seed, step: (T) → T)` (infinite), and `range(start, end)` for `int`. Each collection's `sequence()` member closes over one of its iterator objects, so the pipeline is fail-fast.

| Intermediate | Behavior |
| --- | --- |
| `map<U>(f)`, `filter(keep)`, `flat_map<U>(f: (T) → U[list])` | |
| `take(n)`, `skip(n)`, `take_while(test)`, `skip_while(test)` | A negative `n` traps. `take` stops pulling after `n` elements. |
| `enumerate() → Sequence<[int, T]>` | Each element with its position from 0. |
| `zip<U>(other: Sequence<U>) → Sequence<[T, U]>` | Ends with the shorter input. |
| `chain(other: Sequence<T>)`, `peek(action)` | |
| `chunked(n)`, `windowed(n)` | Sequences of built-in lists, each list new. `n` below 1 traps. |
| `sorted(c)`, `reversed()` | Buffer the whole input when first pulled. `sorted` is stable. |
| `distinct()` | Keeps the first of each group of equal elements. Needs G3. |

| Terminal | Behavior |
| --- | --- |
| `to_list()`, `to_array_list()` | A built-in list; an `ArrayList<T>`. |
| `partition(test) → [ArrayList<T>, ArrayList<T>]` | Matching elements first. |
| `fold<A>(initial: A, f: (A, T) → A) → A` | Left to right. |
| `count() → int`, `for_each(action)` | `count` traps (reason 1) beyond the `int` range. |
| `any(test)`, `all(test)`, `none(test)` `→ bool` | Stop at the first deciding element. |
| `reduce(f) → Optional<T>`, `first()`, `last()`, `find(test)`, `min(c)`, `max(c)` | G2. `min` and `max` return the first of several equal extremes. |
| `to_set()`, `to_map<K, V>(key, value)`, `group_by<K>(key)` | G3, for the default key operations. A duplicate key in `to_map` traps. `group_by` returns a `HashMap<K, ArrayList<T>>` in first-seen key order. |

The module also has functions for sequences of one element type, which a member of a generic class cannot express: `join(s: Sequence<string>, separator: string) → string`, `sum_int32`, `sum_int64` (checked; overflow traps), and `sum_float64` (left-to-right addition).

## 10. `Std.Math`

### 10.1 `Math`

`Math` works on `float64` only and needs N1. Code using another float type converts to `float64`, calls `Math`, and converts back.

- **Constants:** `Pi`, `Tau`, `E`, `Ln2`, `Ln10`, `Sqrt2`, each the nearest `float64`.
- **`sqrt`** is correctly rounded. It unpacks the operand, computes the integer square root of the significand shifted to an even exponent, one result bit per step with a running remainder, and rounds to nearest, ties to even. Negative operands other than `-0.0` give NaN.
- **Other functions:** `cbrt`, `hypot`, `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `to_radians`, and `to_degrees`.

These functions are a port of fdlibm 5.3, operation for operation, including its argument reduction, its polynomial coefficients (written as exact bit patterns through `Float64.from_bits`), and its special cases. fdlibm relies only on IEEE 754 double arithmetic with round-to-nearest and no fused or reassociated operations, which is what [Numbers](Numbers) guarantees, and on reading and writing the high and low 32-bit words of a double, which is N1. The functions are therefore defined by their source: every runtime produces bit-identical results. Every NaN result is the canonical NaN. fdlibm's copyright notice must be kept in the ported files, following the third-party rule in [CONTRIBUTING](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md).

Every function binds its `neura.math.*` kernel (P2a, [Kernels](Kernels)), a C transliteration of the same port, so a call costs one to four steps and adds no float circuit to the image. The Neura port is the kernels' oracle: `tests/stdlib/math/kernel-oracle.nra` compares the two bit for bit on an edge corpus and seeded inputs, and the vector tests check the kernels against fdlibm's results.

### 10.2 `BigInteger`

An immutable arbitrary-precision integer. Its magnitude is a list of limbs, so the class is not marked `#Immutable`, and a task cannot be given one or read the constants `Zero`, `One`, and `Ten`. Parsing uses T1 and conversion to `float64` uses N1.

- **Layout.** `sign` (`int`: -1, 0, or 1) and `magnitude`, a built-in `uint32` list of limbs, least significant first, with no zero limb at the top. Zero has sign 0 and an empty magnitude. The form is unique, so equality compares the fields. No method changes the list after construction.
- **Limits.** At most 2^26 limbs; a larger result traps with `invalid argument: magnitude too large`.
- **Module functions.** The constants `Zero`, `One`, `Ten`; `of_int64`, `of_uint64`; `parse(text)` and `parse_radix(text, radix)` returning `Result<BigInteger>`, with the grammar of [section 7.1](#71-integer-modules) and no range failure.

| Operation | Algorithm |
| --- | --- |
| `add`, `subtract` | Limb-wise with carry or borrow held in `uint64`; signs handled by comparing magnitudes. |
| `multiply` | Schoolbook with `uint64` partial products, which fit by the limb row of [section 7.2](#72-arithmetic-without-wrapping-intrinsics). When both operands have at least 48 limbs, Karatsuba: split at half of the larger length and recurse. |
| `divide`, `remainder` | Knuth's Algorithm D in base 2^32. The quotient truncates toward zero and the remainder takes the dividend's sign, matching the integer operators. A single-limb divisor uses the short division loop. A zero divisor traps (reason 1), from the division it performs. |
| `divide_and_remainder(other) → [BigInteger, BigInteger]` | Both results from one division, as a tuple. |
| `floor_mod(m)` | Non-negative result for a positive modulus. |
| `pow(exponent: int)` | Square and multiply. A negative exponent traps. |
| `gcd(other)` | Euclid's algorithm with `remainder`; the result is non-negative. |
| `mod_pow(exponent, modulus)` | Left-to-right binary exponentiation, reducing after each step. A negative exponent or a non-positive modulus traps. |
| `mod_inverse(modulus) → BigInteger` | Extended Euclid. Returns `nil` when no inverse exists. |
| `negate`, `abs`, `sign`, `is_zero`, `is_even` | |
| `shift_left(n)`, `shift_right(n)` | `shift_right` rounds toward negative infinity. A negative `n` traps. Limb shifts use counts from 1 through 31, so the built-in shifts never trap. |
| `bit_length() → int`, `test_bit(n) → bool` | Of the magnitude. |
| `compare_to(other) → Ordering`, `equals(other)`, `hash_code()` | Hash: start at 0, combine each limb from most to least significant, then negate (wrapping) for a negative value. |
| `to_int64() → Optional<int64>`, `to_float64() → float64` | `to_float64` rounds to nearest, ties to even, and gives an infinity beyond the range. |
| `to_string()`, `to_string_radix(radix)` | Radix 10 divides repeatedly by 10^9 and joins nine-digit groups. |

`BigInteger` has no bitwise and, or, or exclusive or.

### 10.3 `BigDecimal`

An immutable decimal number stores an unscaled `BigInteger` and an `int` `scale`, representing unscaled × 10^(-scale), in the read-only fields `unscaled` and `scale`. Parsing uses T1 and float conversion uses N1.

- `equals` and `compare_to` are numeric, so `2.0` equals `2.00`. `hash_code` is computed on the form with trailing zeros stripped.
- `add` and `subtract` give the larger scale; `multiply` gives the sum of the scales; all are exact.
- Division always takes an explicit result scale and `RoundingMode`: `divide(other, scale, mode)`. There is no exact division that could fail on a non-terminating quotient.
- `RoundingMode` is a fieldless enum, in order: `Up`, `Down`, `Ceiling`, `Floor`, `HalfUp`, `HalfDown`, `HalfEven`, with Java's meanings.
- Other members: `set_scale(scale, mode)`, `round(precision, mode)`, `strip_trailing_zeros`, `negate`, `abs`, `sign`, `pow(exponent: int)`, `to_big_integer(mode)`, `to_float64()`, and the module functions `of_int64`, `of_big_integer(value, scale)`, `of_float64(v)` (the exact binary value; NaN and infinities trap), and `parse(text) → Result<BigDecimal>`.
- `to_string` always uses plain notation with exactly `scale` fraction digits when the scale is positive, and appends zeros for a negative scale (zero with a negative scale prints as `0`).
- A scale that leaves the `int` range traps (reason 1).

## 11. Input and output

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

### 11.2 `Std.Fs`

Needs P2 for every host operation and T1 for `Path`.

**`Path`** is an `#Immutable` class holding normalized text.

- The separator is `/` on every host. The host service translates.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- The read-only field `is_absolute` is true exactly when the text starts with `/`. Platform prefixes such as drive letters are passed to the host unchanged and are not interpreted.
- Module function `of(text)`. Members include `join(other: string) → Path`, `parent() → Path`, `file_name()`, `stem()`, `extension()`, `components()`, `resolve_lexically() → Path`, `starts_with(other: Path) → bool`, and `relative_to(base: Path) → Path`. An absolute argument to `join` replaces the path. `parent` returns `nil` when the path has no parent. `relative_to` returns `nil` when it cannot express the path relative to the base. `file_name`, `stem`, and `extension` return `Optional<string>`. The extension is the text after the last `.` of the file name when that dot is not the first character. `components` returns a built-in list of strings. Equality, hash, ordering, and text use the normalized path.

**`File`** is a class wrapping a host file ID. The module functions `open(path, mode: OpenMode) → Result<File>` and `with_open<T>(path, mode, body)`, the scoped helper of [section 5.5](#55-resources). `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](#55-resources)). A `File` is unbuffered.

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

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](#46-retired-built-ins)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, as every program does. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, `Status(int)`, and the named statuses `SeriousFailure` (2), `CommandNotFound` (127), `IllegalInstruction` (132), `Breakpoint` (133), `Abort` (134), `ArithmeticError` (136), `OutOfMemory` (137), `BusError` (138), `SegmentationFault` (139), `CpuTimeExceeded` (152), and `FileSizeExceeded` (153). `Ok`, `Failure`, and `Status` map to 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. A payload enum lets a program return any status without a host call. Neura enums cannot carry explicit integer values, so the compiler and `status_of(code) → int` hold the table; the named variants follow declaration order after `Status`, so the original three keep their ordinals.
- **`status_of(code: ExitCode) → int`** returns the status a variant stands for, trapping for an invalid `Status`.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. The overload `Process.exit(code: ExitCode)` exits with `status_of(code)`. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 11.4 `Std.Net`

The contract tree, the TCP stream and listener, and the packages of 0.3.0. The 0.3.0 part is proposed: autonomous agents implemented it as [the networking design](https://github.com/Kayyo321/neura/blob/main/docs/design/Net030.md) records, and it awaits the owner's confirmation. A member that needs a host service this runtime lacks traps with `unsupported: <Module>.<member> is not implemented in Neura 0.3.0 (<reason>)` and is marked `@stub` in its source; the design note lists each one with the service it needs. Addresses, views and messages are heap objects under the tracing collector, so the specification's "zero-copy" and "zero-allocation" are not provided: an `IoSlice` is a view over one byte list, copied only when asked. Clocks and random sources are injected, so retries, caches, masks and identifiers repeat for the same inputs.

**TCP.** A call waits for its result; in a program with tasks the waiting task parks and the others run, and cancelling its scope ends the wait with kind `Cancelled` ([Concurrency](Concurrency#104-host-io)). Needs P2 and T1.

- `TcpStream`: the module functions `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>` and `with_connection<T>(host, port, body)`, the scoped helper of [section 5.5](#55-resources). Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), the field `read_timeout: Duration` (zero, the initial value, for none; an expired timeout fails with kind `TimedOut`; assigning a negative duration traps with `invalid argument: negative timeout`), and `close()`. A write that is cancelled while it waits fails with kind `Cancelled` and the message `stream write cancelled after N bytes`, where N is the number of bytes already sent.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, the read-only field `local_port: int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- `Std.Net.Http.Fetch` (0.3.0) sends one HTTP request through the host's `neura.http.fetch` service: `request(method, url)` builds a `Request` (no headers or body, a 30-second timeout, same-origin credentials) whose `with_header`, `with_body`, `with_text`, `with_timeout` and `with_credentials` return changed copies; `send()` returns `Result<Response>`, and `get` and `post_text` are shorthands. Any status is `Ok`; `Err` means no response, and `failure_of` classifies it as a `FetchFailure` (`TooLarge`, `Busy` and `OutcomeUnknown` are custom errors, the rest I/O kinds). In 0.3.0 only the browser host serves the service ([Providers](Providers#http-fetch)); a native run of an image that calls it fails to bind.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

**Contracts.** One interface per module at the root of `Std.Net`:

- `Endpoint<Addr>`: `address() → Addr`. `Listener<Addr, Conn>` is an endpoint and `Closeable` with `accept() → Result<Conn>`; `Connector<Addr, Conn>` is an endpoint with `connect() → Result<Conn>`.
- `Transport<In, Out>` is `Closeable` with `receive() → Result<Optional<In>>`, `None` once the peer has finished, and `send(Out) → Result<Unit>`, all or an error. `FramedTransport<Frame>` is `Transport<Frame, Frame>`. `MultiplexedTransport<StreamId, Frame>` adds `open_stream(bidirectional)`, `accept_stream`, `send_on`, `receive_on` and `finish_stream`.
- `Service<Req, Resp>`: `call(Req) → Result<Resp>`; `Service.of(func)` wraps a function. `Filter<Req, Resp, In, Out>`: `apply(Req, next: Service<In, Out>) → Result<Resp>`; `SimpleFilter<Req, Resp>` keeps the types. A filter is not a service: `Filter.bind(filter, service)` gives one, `Filter.compose(outer, inner)` nests two filters, and a `FilterChain<Req, Resp>` runs its filters in the order added around the service given to `then`.

**Packages.**

- `Std.Net.Core`: `Ipv4Addr` and `Ipv6Addr` are immutable addresses with strict parsers (dotted decimal; the RFC 4291 forms with an optional `%zone`, which takes part in equality), canonical text (RFC 5952) and range predicates; `IpAddr` is an enum over the two, a mapped `::ffff:a.b.c.d` stays IPv6, and `SocketAddr` adds a port (`[2001:db8::1]:443`). `UnixAddr` holds a path or a Linux abstract name (`@name`), at most 107 bytes. `CidrBlock` tests membership, and `PrefixTrie<V>` gives the longest-prefix match. `IoSlice` and `IoSliceList` gather byte views and `write_gather` writes them through a writer that may take fewer bytes than offered; `ByteRing` is a fixed-capacity ring. `Uri` parses, resolves, normalizes and builds RFC 3986 references, `Urn` parses RFC 8141 names, `Query` reads query strings as pairs or an `a[b]` tree, `PercentEncoding` encodes components and form data, `Punycode` implements RFC 3492, and `Idna` converts names already mapped by UTS 46 (the mapping, `map_uts46`, is a stub).
- `Std.Net.Socket`: `StreamSocket<Addr>` (a byte `Transport` with addresses, half-close and a read timeout), `DatagramSocket<Addr>` and `SeqPacketSocket<Addr>`. `TcpSocket`, `TcpServer` (a `Listener`) and `TcpConnector` (a `Connector`) run over the TCP services; an accepted socket's peer address is not reported. `SocketOptions` builds and checks `SO_REUSEADDR`, `SO_REUSEPORT`, `TCP_NODELAY`, `TCP_FASTOPEN`, `TCP_USER_TIMEOUT`, keepalive timers, buffer sizes, broadcast and Multipath TCP per socket kind. `UdpSocket`, the Unix-domain `UnixStream`, `UnixListener` and `UnixDatagram` (paths and the Linux abstract namespace), applying options and `send_file` run over the host's socket services. Raw sockets, options that must be set before bind, `splice`, `MSG_ZEROCOPY`, UDP GSO and GRO, and event-loop backend selection have their API, and their host calls are stubs.
- `Std.Net.Dns`: a stub resolver in Neura. `Name`, `Wire` and `Message` encode and decode RFC 1035 messages with compression (forward pointers and loops rejected) and the A, AAAA, NS, CNAME, PTR, MX, TXT, SOA, SRV, NAPTR and OPT records, other types as raw bytes. `HostsFile` and `ResolvConf` parse their files. `DnsCache` caches by TTL with RFC 2308 negative caching. `StubResolver` consults hosts, then the cache, then a `DnsTransport`: it expands the search list, follows CNAMEs, matches IDs and questions, falls back to TCP on truncation, retries, rotates servers and routes name suffixes to their own servers. `TcpDnsTransport`, `UdpDnsTransport` and `SystemResolver` (the operating system's resolver, which blocks every task while it waits) run over host services, and `DotTransport` over the host's TLS. `DohTransport` works only with an injected `Http.Transport`: the default transport's fetch is a stub. `DoqTransport` and mDNS networking reach stubs. `SrvLookup` and `NaptrLookup` produce `SocketAddr` lists for the load balancers.
- `Std.Net.Tls`: `TlsConfig` validates client and server configurations (TLS 1.2 and 1.3, ALPN, SNI, CA file, mutual TLS). `TlsStream`, `TlsConnector` and `TlsAcceptor` are a stream, a connector and a listener over the host's TLS services, which a build configured with `NEURA_ENABLE_SERVER_TLS` has (OpenSSL 3.5); in another build they fail with `Unsupported`. A client certificate or a version range narrower than TLS 1.2 to 1.3 is a stub. `DynamicCertResolver` swaps certificate bundles atomically from an injected loader. `Acme` computes the RFC 8555 challenge values (thumbprints, key authorizations, HTTP-01, DNS-01, TLS-ALPN-01); its client flows are stubs. `Spiffe` parses SPIFFE IDs; verifying SVIDs is a stub. There is no Neura TLS or DTLS engine (`Handshake` is a stub).
- `Std.Net.Quic`: RFC 9000 variable-length integers and stream ID rules, RFC 9221 DATAGRAM frames, the `CongestionController` interface with `NewReno`, `Cubic` and `Ledbat` (BBRv3 is a stub), a 0-RTT replay filter and session-ticket rules. `QuicConnection` (a `MultiplexedTransport<int64, uint8[list]>`) and `QuicEndpoint` (a `Listener`) have their API and no transport.
- `Std.Net.Http`: the server API of the backend design. A `Request` is immutable (`method`, `path`, `query`, `headers` as a copy, `client`, `scheme`, `request_id`), with a body read once, whole within `body_limit` (`body()`) or in chunks (`open_body()`). A `Response` is a status, a mutable `Headers` map, and bytes or a streamed producer; `text`, `binary`, `json`, `json_value`, `stream` and `envelope` build them, and `json` takes the codec's `encode` function. A `Router` (a `Service<Request, Response>`) holds `Routes` (`get`, `post`, `put`, `patch`, `delete`, `mount`, optionally bound to a host pattern) and middleware (`use`); `dispatch` never fails: no match is 404, another method's match 405 with `Allow`, HEAD uses GET without the body, and an `Err` is 500 with the envelope `{"error":{"code","message","request_id"}}`. `check` is the startup check. `require`, `bearer`, `same_origin`, `cors` and `request_log` are middleware. `Client` sends `ClientRequest`s over a `Transport`. `Headers`, `Method`, `Status`, `Message` (`HttpRequest<Body>`, `HttpResponse<Body>`), `Wire` (RFC 9112 heads and framing), `Chunked`, `Multipart`, `CacheControl`, `Cache` (RFC 9111), `Date`, `Pool` (per-host limits, idempotent retries), `Alpn` and `AltSvc` need no host. Reading or writing a body at the server host, `neura.http.fetch`, HPACK, QPACK and the HTTP/2, HTTP/3 and MASQUE connections are stubs; their frame and settings codecs are not. `AppHandler` adapts a router to `Std.App` and compiles only where `Std.App` exists.
- `Std.Net.Rpc`: gRPC in Neura. `ProtoWire` reads and writes protobuf fields; codecs (`RpcCodec<T>`: raw, JSON, mapped JSON, protobuf; FlatBuffers and Cap'n Proto are stubs) are separate from `Framing` (length-prefixed and varint-delimited messages with a size limit; gzip and deflate are stubs). `Status`, `Metadata`, `Timeout`, `Deadline` (with `DeadlineFilter`) and `GrpcWeb` follow the gRPC protocol documents. `GrpcServer` routes `/package.Service/Method` calls, and `GrpcChannel` is a `Service<RpcRequest, RpcResponse>`; every call kind runs in memory, bidirectional streams half-duplex, and the HTTP/2 and HTTP/3 transports are stubs. `Health` implements `grpc.health.v1`; `Reflection` lists services, and its file descriptors are stubs.
- `Std.Net.Realtime.Ws` implements RFC 6455: `Frame` and `FrameDecoder` (client masking, minimal lengths, control-frame rules, a size limit), `Message` reassembly with UTF-8 checks, `CloseStatus`, the opening `Handshake`, and `WebSocket`, a `FramedTransport<Message>` over any byte `Transport` that answers pings, completes the closing handshake and fails with the matching close status; `Heartbeat` supervises pings with an injected clock. `PermessageDeflate` negotiates RFC 7692 parameters (compression is a stub) and `ExtendedConnect` builds RFC 8441 and RFC 9220 requests (opening the streams is a stub). `Std.Net.Realtime.WebTransport` implements the wire rules of draft-ietf-webtrans-http3-14; its session I/O is a stub. `Std.Net.Realtime.Sse` parses `text/event-stream` incrementally per the WHATWG rules, writes events, and reconnects with `Last-Event-ID` over an injected fetch function.
- `Std.Net.Resilience`: filters and policies over injected clocks and random sources. `CircuitBreaker` (sliding-window failure rate, Closed, Open and HalfOpen) and `CircuitBreakerFilter`; adaptive concurrency limits (`VegasLimit`, `GradientLimit`) with `ConcurrencyLimitFilter`; `TokenBucketLimiter` and `GcraLimiter` behind `RateLimiter` and a keyed `RateLimitFilter`, the GCRA limiter over a `RateStore` hook with an in-memory store; `RetryFilter` with `Backoff` and `RetryBudget`; `HedgingFilter`, which sends a backup request when the primary passes a latency percentile and needs a `SpawnableService`; and the `LoadBalancer` implementations `RoundRobin`, `PeakEwma` and `PowerOfTwoChoices`.
- `Std.Net.Mesh`: `TraceParent`, `TraceState` and `Baggage` parse and write the W3C headers, `TraceContext` extracts and injects them on any request type, and `TracePropagationFilter` continues or starts a trace around a service. `ProxyProtocol` parses and writes PROXY protocol v1 and v2 headers with their TLVs, `ProxyProtocolListener` wraps any `Listener` to consume the header, and `ProxyProtocolConnector` writes one. `ShadowFilter` mirrors a share of requests to a bounded channel without waiting, and `ConcurrentShadowFilter` runs the copy beside the primary call. `Ebpf` describes and validates socket programs and maps; loading and attaching are stubs.

Raw-socket, QUIC, HTTP/2, HTTP/3 and some offload APIs have host calls that trap in 0.3.0. TCP, UDP and Unix-domain sockets, the system resolver, DNS over TCP and UDP, and WebSockets over any byte transport run over the host; TLS streams do in TLS builds. For HTTP the library provides HTTP/1.1 parsing, serialization and in-memory routing, and a server serves a router through `Std.App` (`AppHandler`); the client's network call is a stub. The protocol logic of every package works without the host.

## 12. Time and randomness

### 12.1 `Std.Time`

The value types are `#Immutable` classes with `compare_to`, `equals`, `hash_code`, and `to_string`. Their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Module functions: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; the constant `Zero`; `parse(text) → Result<Duration>`. Overflow traps (reason 1), here and in every member below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- Module functions `of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`, and `parse`. The read-only fields `epoch_seconds` and `nanos`, and the members `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface with `now() → Result<Instant>`, `monotonic() → Result<Duration>` (elapsed time from an arbitrary fixed origin, never decreasing), and `sleep(duration) → Result<Unit>`.

- `SystemClock` uses the clock host services (P2) and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock`, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, `elapsed() → Result<Duration>`, and the read-only field `is_running`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate` stores `year` (-999,999 through 999,999), `month` (1 through 12), and `day`, read-only fields. The module function `of(year, month, day)` traps for an invalid date. `try_of(year, month, day) → LocalDate` returns `nil` for invalid components. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days. Shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle using floor division so that negative years work. Members include `plus_days`, `plus_months`, `plus_years`, `days_until(other) → int64`, `day_of_week() → DayOfWeek`, `day_of_year()`, `is_leap_year()`, and `length_of_month()`. Month and year changes clamp the day to the end of the target month. `DayOfWeek` runs from `Monday` through `Sunday`, with epoch day 0 on Thursday. Text uses `YYYY-MM-DD`. Years outside 0000 through 9999 have a sign and at least four digits.
- `LocalTime`: `hour()`, `minute()`, `second()`, `nano()`, computed from the nanoseconds of the day that it stores (`to_nano_of_day()`). `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`, the read-only fields `date` and `time`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800, the read-only field `total_seconds`. The constant `Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.

A read-only field is an exported `last` field (an `is_running` with a get-only [accessor](Types#accessors)): code outside the class reads it as `date.year` and cannot assign it. The value classes are `#Final`, and `#NoDiscard`, so a call whose result is dropped, such as `instant.plus(span);`, warns ([Tags](Tags)).

There is no time zone database, no named zones, and no daylight-saving rules. Conversions between instants and local times take an explicit `ZoneOffset`. The ISO forms above are the only text forms.

### 12.2 `Std.Random`

**`Random`** implements the xoshiro256** generator using shifts, rotations, exclusive ors, and multiplication by 5 and 9. Use the `uint64` recipes in [section 7.2](#72-arithmetic-without-wrapping-intrinsics). `next_gaussian` depends on the library's `Math` module, and `from_entropy` requires a registered entropy provider.

- **State.** Four `uint64` words `s0` through `s3`.
- **Step.** The output is `rotate_left(s1 × 5, 7) × 9`, wrapping. Then, with `t` = `s1` shifted left by 17: `s2 ^= s0`; `s3 ^= s1`; `s1 ^= s2`; `s0 ^= s3`; `s2 ^= t`; `s3 = rotate_left(s3, 45)`.
- **Seeding.** The module function `seeded(seed: uint64)` fills the four words with successive outputs of SplitMix64 started at the seed: add 0x9E3779B97F4A7C15 to the state; take `z` as the new state; `z = (z ^ (z >> 30)) × 0xBF58476D1CE4E5B9`; `z = (z ^ (z >> 27)) × 0x94D049BB133111EB`; output `z ^ (z >> 31)`; all wrapping. The source writes these constants in decimal (A5). `from_entropy() → Result<Random>` requests 8 bytes from `neura.entropy.fill`, reads them as a little-endian `uint64`, and seeds with that.

| Member | Behavior |
| --- | --- |
| `next_uint64() → uint64` | One step. |
| `next_uint32() → uint32` | The high 32 bits of one step. |
| `next_bool() → bool` | The top bit of one step. |
| `next_int_below(bound: int) → int` | Uniform in 0 up to but excluding `bound`. Take the smallest all-ones mask covering `bound - 1`; draw `next_uint32` masked until the result is below `bound`. A bound below 1 traps. |
| `next_int_between(low: int, high: int) → int` | Uniform from `low` up to but excluding `high`. The span is computed in `int64`, sampled with the same mask-and-reject loop on `uint32`, and added to `low` in `int64`. `low` not below `high` traps. |
| `next_float64() → float64` | The top 53 bits of one step, converted to `float64` (exact) and multiplied by 2^-53: uniform in 0 up to but excluding 1. |
| `next_gaussian() → float64` | Marsaglia's polar method. Each accepted pair yields two values; the second is kept in the generator and returned by the next call. |
| `fill_bytes(count: int)` | A new built-in `uint8` list. Each step supplies 8 bytes, least significant first; a final partial group uses the low bytes. |
| `shuffle<T>(items: T[list])` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. Generic, with no callback. |
| `choose<T>(items: T[list]) → T` | Traps with `no such element` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets; use `Std.Crypto.Random` ([section 21.3](#213-stdcryptorandom)).

**`Uuid`** is an `#Immutable` value of 16 bytes. The module function `random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `to_string` writes the 36-character hyphenated form in lowercase. `parse(text) → Result<Uuid>` accepts either case and needs T1. It has `compare_to`, `equals`, and `hash_code` over its bytes.

## 13. Data formats

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone. The kernel `neura.encoding.utf8_validate` computes it (P2a).
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error). The kernels `neura.encoding.hex_encode` and `hex_decode` compute it (P2a).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero. The kernels `neura.encoding.base64_encode` and `base64_decode` compute every form (P2a).
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**`JsonValue` is a recursive payload enum.** It is read with `match` and walked with mutually recursive functions.

| Variant | Payload |
| --- | --- |
| `Null` | None |
| `Boolean` | `bool` |
| `Number` | `string`: the number's JSON text |
| `Text` | `string` |
| `Array` | A built-in list of `JsonValue` |
| `Object` | A built-in list of `[string, JsonValue]`, in member order |

- **Objects are member lists.** They keep insertion order in the serialized representation. Lookup is a linear scan. `Json.index(members)` builds a `StringMap<JsonValue>` when callers need repeated lookups.
- **Numbers keep their text**, so no precision is lost in a round trip. The payload must be valid JSON number text; the writer checks it and traps with `invalid argument: number text` otherwise.
- **Aliasing.** The payloads of `Array` and `Object` are built-in lists: they are mutable, and the same list can be reached from more than one value. A `JsonValue` is therefore not a frozen tree. Changing a list changes every value that holds it, and a list can be made to contain a value that holds the same list. `write`, `equals`, and `hash` do not detect such a cycle and end with the frame-limit fault. The parser always produces a tree with no sharing.

The module `Json`:

| Function | Behavior | Needs |
| --- | --- | --- |
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | None |
| `number_of_float64(v: float64) → JsonValue` | A `Number` with the float's text. NaN and infinities trap. | N1 |
| `member(members, name: string) → Optional<JsonValue>` | The first member with that name. | T1 |
| `number_to_int64(text: string) → Optional<int64>` | `Some` when the text has no fraction or exponent and fits. | T1 |
| `number_to_float64(text: string) → float64` | The nearest value. | T1, N1 |
| `number_to_big_decimal(text: string) → BigDecimal` | Exact. | T1 |
| `parse(text: string) → Result<JsonValue>` | Below. | T1 |
| `write(value) → string`, `write_pretty(value, indent: int) → string` | Below. | T1 |
| `equals(a, b) → bool`, `hash(value) → uint32` | Structural. Numbers are equal when their exact decimal values are. Objects are equal when they have the same number of members and each member of one has an equal value under the same name in the other, regardless of order. | T1 |

- **Parsing** is strict recursive descent: no comments, no trailing commas, no leading zeros, only the four JSON whitespace characters. String escapes include `\uXXXX`, with surrogate pairs combined; an unpaired surrogate fails. Nesting deeper than 512 fails. For a repeated member name, the last value wins and the name keeps its first position. Anything but whitespace after the value fails. Errors are `Parse` errors with the byte offset.
- **Writing.** `write` produces no whitespace. `write_pretty` puts each element and member on its own line, indented by `indent` spaces per level, with `: ` after names; empty arrays and objects are `[]` and `{}`. Strings escape `"`, `\`, and characters below 0x20 (using `\b`, `\f`, `\n`, `\r`, `\t`, and otherwise `\u00XX` with lowercase hex). Nothing else is escaped. Every member of an object is written, including repeated names.

### 13.2 `Std.Digest`

Both work on built-in `uint8` lists with the 32-bit recipes of [section 7.2](#72-arithmetic-without-wrapping-intrinsics).

- **`Crc32`:** the IEEE 802.3 checksum (reflected polynomial 0xEDB88320, initial value and final exclusive-or of all ones). `update(bytes)`, `value() → uint32`, `reset()`, and the module function `of(bytes) → uint32`. The kernels `neura.digest.crc32` and `crc32_update` compute it (P2a).
- **`Sha256`:** FIPS 180-4. `update(bytes)`, `finish()` (a built-in list of 32 bytes; a later `update` or `finish` traps with `invalid state: digest finished`), and the module function `of(bytes)`. The kernels `neura.digest.sha256`, `sha256_init`, `sha256_update`, and `sha256_finish` compute it (P2a); a running digest keeps the kernel's 112-byte state in a private list.

These are for integrity checks and content addressing. The library makes no claim about timing behavior.

## 14. Diagnostics

### 14.1 `Std.Log`

`Logger`: its sink is a concrete function value and its text is built by concatenation.

- `LogLevel` is a fieldless enum, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`. Levels compare by ordinal.
- **The sink is a function value** of type `(string) → void`, called once per accepted record with the finished line, without a line terminator. `Console.print_line` is a sink. A closure that appends to a captured list is a sink for tests; because a captured list is shared, the test reads the list afterwards. A sink that can fail handles its own failure, typically by counting it in a captured `Cell`.
- Module functions `create(name: string, sink, level: LogLevel)` and `create_timed(name, sink, level, clock: Clock)`.
- Each record is one line: with a clock, the instant's text and a space; then the level in uppercase, a space, the name, `: `, and the message. If the clock fails, the timestamp is `-`.
- Without a clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, make: () → string)` (the function runs only when the level is enabled), `is_enabled(level)`, and the field `level`, which a program assigns to change the logger's level.
- There is no global logger registry. A logger is passed to the code that uses it.

### 14.2 `Std.Test`

This module replaces the built-in `Test` as described in [section 4.6](#46-retired-built-ins). An assertion builds its message in Neura and calls `Failure.trap`. The runner reports the message from the trapping test.

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
- **There is no assertion that expects a trap.** A trap cannot be caught, so a test function cannot observe one and continue. Code that must trap is tested by a separate program with an expected diagnostic ([section 17](#17-verification)).
- A failed assertion ends its test. Tests are discovered and run as [Testing](Testing) describes; only the import changes.

## 15. Names

The lexer's keyword list includes `let`, `const`, `export`, `override`, `func`, `module`, `class`, `import`, `new`, `destroy`, `if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `true`, `false`, `as`, `is`, `nil`, `enum`, `match`, `and`, `or`, `interface`, and `implements`. A keyword cannot be a member name, including after a dot.

The names that changed because they collided with a keyword, and others replaced for other reasons, are listed in the [design notes](https://github.com/Kayyo321/neura/blob/main/docs/design/Stdlib.md#names-that-changed). No other name collides. `RegexMatch`, `is_match`, and `matches_fully` contain `match` but are not the keyword. `Fs.list` is allowed: `list` is not a keyword. Variant names such as `Error.Io` and `JsonValue.Null` are always qualified.

Contracts deliberately kept: insertion-ordered hash collections, separate read-only and mutable interfaces, `Ordering` instead of integers, byte offsets for string positions, a required comparator for sorted collections, numeric equality for `BigDecimal`, the AVL tree, the fixed half load factor, unbuffered console output, and the fdlibm port.


## 16. Not provided

| Area | Status | Reason |
| --- | --- | --- |
| Concurrent collections; threads and executors that outlive a scope; free-standing locks | Not provided | `Std.Concurrent` supplies scoped tasks, channels, worker groups, `AtomicInt64`, `Semaphore`, and `Guarded<T>` ([section 19](#19-stdconcurrent)). Build a concurrent collection on `Guarded<T>` or as an owner task. No other library class is transferable, so a collection belongs to one task. |
| Reflection, runtime class loading, serialization of arbitrary objects | Excluded | The library needs class and interface type tests, which are available without a general reflection model. |
| Finalizers, weak references | Excluded | Reclamation runs no code, by design. |
| Recovery from traps, cleanup when a trap ends the run | Excluded | The runtime has neither, and the library is designed not to need them. The scoped helpers of [section 5.5](#55-resources) cover every ordinary completion. |
| Time zones, locales, localized formatting, collation | Not provided | They need large versioned data tables. |
| Unicode normalization | Not provided | Same reason. [`Character`](#85-character) classifies, maps, and names every code point. |
| Signatures, key exchange | Not provided | They need security review. Hashes, MACs, key derivation, authenticated encryption and a cryptographic generator are in [`Std.Crypto`](#21-stdcrypto); public-key cryptography is planned there. |
| TLS in a build without `NEURA_ENABLE_SERVER_TLS` | Not provided | `Std.Net.Tls` streams fail with `Unsupported`; the release archives are built without the option ([section 11.4](#114-stdnet)). |
| `BigInteger` bitwise operations | Not provided | |
| Facade modules such as `Std.Collections` that re-export every collection type | Not provided | They need re-export of generic classes, which the language does not have. The type modules are the import paths. |

## 17. Verification

These requirements apply the repository's rules in [AGENTS.md](https://github.com/Kayyo321/neura/blob/main/AGENTS.md) to the library.

- **Where tests live.** Library tests are Neura test files under `tests/stdlib/<module>/`, run by `neura test` through CMake with the library installed in the trusted source tree. Migrate the built-in `Test` imports once `Std.Test` is installed.
- **Trap tests are separate programs.** A member that traps is tested by a program under `tests/stdlib/<module>/traps/` with a `.out` file holding the expected diagnostic, run and compared by the harness that already runs `tests/backend/traps`. The comparison includes the trap reason and any message.
- **Coverage rule.** Every member in a table on this page has at least one test of its normal result, one test of each documented `Err` or `None`, and one trap test for each documented trap.
- **Both build modes.** Every library test and trap test runs optimized and with `--no-optimize`, and the outputs must be identical.
- **Reference evaluator.** The test programs also run under the IR reference evaluator, and the results must match NVM's.
- **Determinism.** Each test program runs twice from the same image; output and step counts must be identical.
- **Compiler fixtures.** The library's tests include negative cases for unsupported specialization, untrusted host declarations, and malformed artifacts where those boundaries are exercised.
- **Model-based tests.** For every collection, a test drives the collection and a trivially correct model (a plain built-in list, or an association list of tuples) with the same sequence of at least 10,000 operations drawn from `Random.seeded` with a fixed seed, and compares size, contents, iteration order, and return values after every operation.
- **Invariant validation.** `HashCore`, the AVL tree, `ArrayDeque`, `LinkedList`, and `PriorityQueue` each have an exported `validate()` member that traps on a broken structural invariant (index slots agree with entries, dead entries within the retention bound, AVL balance and parent links, endpoint and reciprocal links, heap order). Model tests call it after every operation.
- **Reclamation.** For each collection, a test adds and removes large elements in a loop under a heap limit that the loop would exceed if removed elements stayed reachable. For hash collections the limit allows for the documented retention bound and no more.
- **Fail-fast.** Tests mutate a collection during iteration through both a wrapper and any live backing alias. Check a remove-then-append sequence that restores the old length, indexed replacement that preserves the version, and `ArrayList.clear` and `truncate` through a wrapped list. A `to_list()` copy iterates independently.
- **Arithmetic recipes.** Each row of [section 7.2](#72-arithmetic-without-wrapping-intrinsics) is tested at its boundaries: zero, one, the largest value, the sign boundary, and operands whose exact result needs the full double width.
- **Contract text.** `to_string` output, hash values, trap messages, and float formatting are compared against literal expected values.
- **Numeric vectors.** `Math` is tested against bit patterns generated once from a reference fdlibm build and checked in. Float parsing and formatting use a checked-in corpus that includes halfway values, subnormals, the largest finite values, and powers of two and ten, plus exhaustive round trips for `float16` and `bfloat16`.
- **Host services.** Each service has a test that replays a recorded trace and a test with the capability denied.
- **Task safety.** `concurrency-frontend` runs `check --task-safety` over a program that links the modules a service needs and allows only console input to be not task-safe ([section 5.8](#58-task-safety)).
- **Retirement.** The change that deletes `SysIo` and `Test` includes a rejection test for a program importing each, and every existing example and test passes through the new modules.
- **Decompiler.** A library-using image must round-trip through `disassemble` and `assemble` byte for byte, and `decompile` must run on it. No stronger recovery claim is made for library code.
- **Cost baseline.** A small benchmark set (insert and look up 1,000 map entries, sort 1,000 integers, format 100 integers, hash 10 KB with `Sha256`) records NVM steps, neuron count, and image size in a checked-in baseline. A change that raises any figure by more than 10% says why in its pull request. The layout choices of sections 9.3 through 9.7 are re-measured with this set when an acceleration ships.

## 18. Behavior details

These rules complete the contracts of the earlier sections. Where a rule here is more specific than a table above, this section governs. Each is covered by the module's tests in `tests/stdlib/<area>/`.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](ProjectStructure).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](Modules#initialization)).
- **Extra members.** `Error` and `Result` have `equals`, `hash`, and `to_string`, and `SimpleDetail` compares its code and message, so errors compare structurally. `Comparable<T>` is `Std.Core.Comparable`. The collection interfaces declare `add(T) → bool` for every mutable collection (lists return true), and a sorted map's `first`, `last`, `range`, and `descending` give entries while `floor`, `ceiling`, `lower`, and `higher` give keys, with `_entry` forms for entries.

### 18.2 Program, console, and testing

- **Entry.** `Std.Program` holds `ExitCode`, `Entry`, `Environment`, and `Process` as one module. A `Status` outside 0 through 255 traps with `invalid argument: exit status`.
- **Process exit.** `Process.exit` is available without a capability. A test that exits with a nonzero status fails.
- **Std.Test messages.** `assert_equal_float64` compares with float `equals` (NaN equals NaN, `0.0` differs from `-0.0`). `assert_none` and `assert_err` report `expected None, got Some` and `expected Err, got Ok` without the value's text, so they work for payload enums. `assert_not_equal` reports `expected a value other than <x>`.
- **Console.** Output is unbuffered. Input is read in 4,096-byte requests shared by `read_line` and `input()`. A write that makes no progress is an `Other` error.

### 18.3 Numbers and text building

- **Text of numbers.** Integer and float `to_string` use the NVM's scalar text, which implements [section 7.5](#75-float-text-conversion); `StringBuilder.append_int` and the other number appends append that text.
- **Integer modules.** Every module has `to_unsigned` and `to_signed` (the same-signedness one is the identity). `format` counts the sign in the width; a width at or below the text length pads nothing. `parse` reports a syntax error before a range error and accepts leading zeros. `floor_div` and `floor_mod` share one divider circuit.
- **Trap details.** `low exceeds high`, `negative exponent`, `radix outside 2 through 36`, `pad is not one scalar value`, `negative count`, `not a Unicode scalar value`, `byte length out of range`, `not a character boundary`, `zero step`, `index out of range`, `NaN bound`, and `digits outside 0 through 100`, each after `invalid argument: `.
- **Floats.** `min`, `max`, and the rounding functions return the canonical NaN for a NaN operand; `clamp` orders `-0.0` below `0.0`. `to_fixed` and `to_exponential` print `NaN` and the infinities as `to_string` does, keep the sign of a negative value that rounds to zero, and take 0 through 100 digits. `parse` accepts `e` and `E`, rejects `+NaN` and `-NaN`, and treats digits beyond 800 significant digits as a sticky bit. Fast paths (Clinger's for `float64`, a guarded `float64` path for narrower formats) are used only where they are exact; exhaustive tests check every `float16` and `bfloat16` pattern.
- **IntRange** exposes `start()`, `end()`, and `step()` as methods. Equality and hashing use those three values in that order. The text is `start..end:step`. `size()` traps with reason 1 beyond `Int32.Max`.

### 18.4 Text

- **Offsets.** Library offset checks trap with `invalid argument: offset out of range` or `invalid argument: offset is not a character boundary`; `byte_at`, `slice`, and `slice_from` use the language's own traps.
- **Strings.** `lines("")` is `[""]`. `last_index_of(s, "")` is the byte length. `pad_start` and `pad_end` return the text unchanged for a width at or below its scalar count. `from_bytes` reports the offset of the first ill-formed sequence with the message `invalid start byte`, `invalid continuation byte`, or `unexpected end of data`.
- **Scalars.** `utf8_length` traps for a value that is not a scalar value. `is_ascii_whitespace` includes vertical tab, matching `trim` and `\s`.
- **Regex syntax.** `(?<name>…)` and `(?P<name>…)` both name a group. Any ASCII punctuation may be escaped; other escapes are errors. A literal `{` must be escaped. A quantified assertion is `nothing to repeat`, a second quantifier `repeated quantifier`, and `+` after a quantifier `possessive quantifiers are not supported`.
- **Regex matching.** `$` without `multi_line` matches only at the end, and `.` excludes only LF. An iteration beyond a quantifier's minimum that matches empty fails, and a capture inside a repeated group keeps its last value. The Pike machine keeps one thread per instruction per position, so a loop whose body can match empty can merge iterations and differ from every backtracking order; for example `(?:a*?)*` on `aa` matches `[0, 1)`. Replacements validate before matching, accept `${number}`, and trap on a malformed `$`. `group`, `group_start`, and `named_group` trap for an unknown index or name.

### 18.5 Time, randomness, and logging

- **Duration text.** `parse` accepts an optional sign, `P`, an optional day count, and `T` with hours, minutes, and seconds (seconds with up to nine fraction digits); days are read but never written.
- **Instant** normalizes the nanoseconds of `of_epoch_seconds` by floor division and writes every `int64` second count; `parse` accepts years -999,999 through 999,999.
- **ZoneOffset** holds whole minutes; hours and minutes share a sign, and `compare_to` orders by total seconds.
- **Clocks.** `Clock.sleep` returns at once for a zero or negative duration. `FixedClock.of(instant)` makes a fixed clock whose monotonic reading is the total of its advances and sleeps; `advance` traps for a negative duration. `Stopwatch.create(clock)`; `start` and `stop` return `Result<Unit>` and trap when repeated.
- **Calendar arithmetic** splits off 400-year cycles with one 64-bit division and works within a cycle in 32-bit arithmetic, which keeps images that format dates several megabytes smaller.
- **Random.** `seeded` and `from_entropy` are the only constructors; `from_entropy` fails with `InvalidData` when the host returns other than 8 bytes. **Uuid** stores two big-endian `uint64` halves and has `to_bytes`.
- **Trap details.** `invalid date`, `date out of range`, `invalid time`, `invalid zone offset`, `day of week`, `negative duration`, `bound must be positive`, `low must be below high`, and `negative count` after `invalid argument: `; `stopwatch is running` and `stopwatch is stopped` after `invalid state: `.
- **Logger.** `is_enabled(Off)` is false.

### 18.6 Host services, files, and networking

- **Signatures.** `neura.fs.stat` takes a follow-links flag so that `remove_tree` never descends through a symbolic link, `neura.fs.remove` takes a directory flag so that `remove_file` and `remove_directory` need no separate stat, and `neura.net.local_port` reads a listener's port. `File.size` is three seeks.
- **Errors.** Other failures use code 100. `EADDRINUSE` is `AlreadyExists`; `EAGAIN` and `ETIMEDOUT` are `TimedOut`; `EPIPE`, `ECONNABORTED`, and `ENOTCONN` are `ConnectionReset`; a name that does not resolve is `NotFound`. A read or entropy request is at most 1 MiB. Network errors carry `host:port` in the error's path.
- **Path.** `components` starts with `/` for an absolute path; `.` has no components. `file_name` is `None` for `/`, `.`, and a path ending in `..`. `starts_with` and `relative_to` need both paths absolute or both relative.
- **Buffers.** `BufferedReader` passes a large read straight through when its buffer is empty; `BufferedWriter` drops its buffer when the sink fails. `Utf8Reader.read_scalar` leaves the byte that broke a sequence unread.
- **Trap details.** `maximum count below 1`, `capacity below 16`, `negative count`, `port`, and `negative timeout` after `invalid argument: `.
- **Lifetime.** File and socket IDs count from 1 in each run; the runner closes what a run left open when it ends, and after each test.

### 18.7 Encodings and digests

- **Additions.** `Crc32.create()` and `Sha256.create()` make fresh digests; `Base64.decode_unpadded` and `decode_url_unpadded` read the unpadded forms, which reject padding.
- **Utf8** delegates to `Strings`, so its errors match `Strings.from_bytes`; `validate` returns the length of the longest valid prefix, the offset `decode` reports.
- **Hex errors.** `invalid hex digit` at the byte; `odd number of hex digits` at the text length, reported only when every digit is valid.
- **Base64 errors.** `invalid character`, `invalid padding`, and `unexpected padding` at the byte; `unexpected end of data` at the text length; `nonzero trailing bits` at the last character. The first structural error wins.
- **Csv.** A CR that does not start a CRLF is data. An empty line is a record with one empty field, and a record with no fields is written as an empty line. Errors: `quote in unquoted field` and `text after closing quote` (Syntax), `unterminated quoted field` (UnexpectedEnd).
- **Json parse errors.** `Empty` for empty or all-whitespace text; `UnexpectedEnd` at the text length; `Syntax` for structure; `InvalidCharacter` inside a token, with escapes reported at the backslash; `OutOfRange` at the bracket that opens level 513. Top-level scalars are accepted.
- **Json values.** Numbers compare by a canonical decimal text, including exponents beyond `int64`. `hash` starts each variant from its position (Null 1 through Object 6), and an object's hash combines its member count with the wrapping sum of its distinct member hashes, so it agrees with order-insensitive equality even when names repeat. `number_to_int64` is `None` for anything but a plain integer. `index` keeps the last value of a repeated name. A `StringMap<JsonValue>` supports lookups; its `equals` and `to_string` are rejected at build time because `JsonValue` has no built-in value members.
- **Json traps.** `number is not finite`, `negative indent`, `number text`, and `number exponent out of range`, after `invalid argument: `.
- **Sha256** kept rolled loops while it was Neura code: unrolling cut steps by about four but grew the image by 1.5 to 3.6 MB. It now calls kernels (P2a), which cut the `sha256` benchmark from 163,795 steps and 861,597 image bytes to 31,043 steps (mostly building the input) and 600,765 bytes.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` where a collection's own text reaches it again, directly or through other collections. Each collection keeps the flag for this in the instance, so text conversion reads no module state and is task-safe ([Concurrency](Concurrency#52-task-safe-calls)).
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the searched value as the receiver.
- **OrdinalMap.** A key outside 0 through n - 1 is the language's bounds trap, also in `contains_key`; `equals` is false when the other map holds a key beyond this map's size; `compute` returns the callback's result.
- **Trap details.** `negative size`, `size above current size`, `range`, `negative count`, `index out of range`, and `negative element` after `invalid argument: `; `invalid state: ` for a failed `validate()`.

### 18.9 Hash collections, trees, and sequences

- **Compaction rule 1** also compacts when live entries plus tombstones fill `E`. Trailing releases leave tombstones behind, so without this addition the index could fill and a lookup for a missing key would not terminate.
- **Modification counts are `uint64`**, as [section 5.4](#54-iteration) states; compaction and `trim()` advance them, so an iterator traps after either. `clear()` returns to the table size chosen at construction.
- **Bulk removal.** `remove_if` and `retain_if` call the callback on every entry, then release the chosen entries and compact at most once. `compute` returns the result of its callback. A `with_keys` collection hashes with its stored function, so `hash_code` agrees with its equality.
- **IntMap and StringMap** are generated from `hash-map.nra` with the key operations called directly (`tests/stdlib/hash-map/generators/gen_maps.py`); `StringMap` uses the string's built-in `hash_code` and `==`, which equal `Strings.hash` and `Strings.equals`, because `Std.Text` is above `Std.Collections` in the dependency order.
- **Extra members.** `with_keys_and_capacity`; `trim` and `validate` on every hash collection; `TreeMap.key_iterator`, `key_range`, and `descending_keys`, which `TreeSet` uses to avoid a tuple per element; `Sequence.from_iterator`, a public `Sequence` constructor from two functions, and `Sequence.iterator()` so that a sequence works once in for-each. Constructors of these collections take a module-private token, so only their module functions build them.
- **Sequence choices.** `range(from, to)` on a sorted collection is empty when `to` is not above `from`. `windowed` emits only full windows. `zip` and `chain` mark the other sequence used. `from_list` fails fast on a structural change to its list. `sum_float64` starts from the first element, so `[-0.0]` sums to `-0.0`, and an empty sequence sums to `0.0`. Maps and sets have no `sequence()` member; `ArrayList` and `LinkedList` build theirs with `from_iterator`.
- **Trap details.** `duplicate key`, `negative count`, `chunk size`, and `window size` after `invalid argument: `; `invalid state: hash table …` and `invalid state: tree …` for a failed `validate()`.

### 18.10 Math

- **fdlibm port.** `Math` is a port of fdlibm 5.3 written without a reference copy of the sources; Sun's notice is kept in the Neura port (`tests/stdlib/math/support/reference-math.nra` and `reference-math-trig.nra`, the kernels' oracle since `Math` binds the kernels), in `src/kernels/`, and in the test transliteration. The Neura functions agree bit for bit with a C transliteration of the same port on 9,148 vector cases (`tests/stdlib/math/vectors`), and that transliteration stays within 1 ulp of a 70-digit reference for the functions fdlibm documents as such, with the same larger errors as glibc for `sinh`, `cosh`, `tanh`, and `log10`. Agreement with a reference fdlibm 5.3 build has not been checked. Expressions fdlibm evaluates only to raise the inexact flag are omitted. `sqrt` is the correctly rounded integer method of [section 10.1](#101-math). `log2`, which fdlibm 5.3 lacks, follows `e_log10.c` and returns exact results for powers of two. `pow(±1, ±∞)` and `pow(1, NaN)` are NaN, as in fdlibm.
- **BigInteger** keeps its fields private (`signum`, `magnitude`); its constructor normalizes the value and adopts the list. `mod_inverse` traps for a modulus that is not positive. `floor_mod` takes the modulus's sign.
- **BigDecimal** adds the read-only fields `unscaled` and `scale`. `round(0, mode)` returns the value unchanged; a rounding carry drops one zero, as Java does. `of_float64` gives the exact value at the smallest scale. `-0.00` parses as zero. `parse` accepts an optional sign, digits with an optional point, and an optional `e` or `E` exponent; a scale outside `int` fails with `OutOfRange` at offset 0. Zero with a negative scale prints as `0`.
- **Trap details.** `negative exponent`, `negative shift count`, `negative bit index`, `modulus not positive`, `not a finite value`, `negative precision`, `magnitude too large`, and `zero sign with a nonzero magnitude` after `invalid argument: `.
- **Cost.** A `Math` call is one kernel call of one to four steps. While `Math` was Neura code, every float64 operation in it compiled to a large circuit: the `elementary` test program, which calls every function, had an image of 14,719,526 bytes, and has one of 15,480 bytes since it binds the kernels. `Math` is not in the cost baseline.

### 18.11 Test layout

- **Layout.** Each area of `tests/stdlib` holds test files, `support/` modules, `traps/` programs with `// status:` and `// stderr:` headers, and `programs/` with expected output (`tests/stdlib/area.cmake`). Every area runs against a fresh installation (`stdlib-install`), its tests optimized twice and unoptimized, its programs and traps in both modes, and its program images through `disassemble`, `assemble`, and `decompile`.
- **Reference evaluator.** `backend-reference` runs every library test function under the NeuraIR reference evaluator and NVM and compares the outcome, the console output, and any trap message. A call of a kernel-bound function runs the runtime's own kernel through the kernel table, so those tests are compared. Tests that need host responses (files, sockets, console input), open a channel, or exceed the evaluator's budget or call depth are counted and not compared.
- **Host services** are tested with the capability denied, with real providers in temporary directories and on localhost, and by replaying checked-in traces.
- **Cost baseline.** `tests/benchmarks` holds the workloads of this section's cost baseline and a few more (ArrayList, TreeMap, strings, JSON, dates); `benchmarks` compares their steps, peak heap cells, neurons, and image bytes with `baseline.txt` and checks that steps grow as expected between two sizes.
- **Not established.** Agreement of `Math` with a reference fdlibm 5.3 build (no reference build was available); `with_capacity(2^29)` and other limits that need billions of elements were only inspected.

### 18.12 Concurrency

- **Lists, not arrays.** `Tasks.all`, `all_ok`, and `first_ok` take their calls as a list and return lists. The language cannot build an array whose length is known only at run time, and a function with type parameters cannot be overloaded for arrays as well.
- **Helper errors.** `tasks.cancelled` and `tasks.empty` are the codes of the two `Custom` errors the `Tasks` helpers make themselves.
- **A group needs a scope.** `WorkerGroup` has no `create`: a group exists only inside `with_workers`, which owns the scope its workers run in and drains the queue before it returns. The handle's constructor is exported only so that `with_workers` can be written in the library.
- **A semaphore is a channel.** Its permits are places in a channel of `bool`, so waiting tasks are served first come, first served and a wait can be cancelled, with no scheduler operation of its own.
- **Errors across tasks.** `Error` stays as it is and gains a transferable copy, `PortableError`: its `Custom` detail is an object of the open class `ErrorDetail`, which is not transferable.
- **Scoped helpers.** The two helpers word their combined error differently, as [section 5.5](#55-resources) gives. `Closeable.release` is exported so that a program can apply the rule to a resource of its own.
- **Trap details.** `workers` and `capacity` (`WorkerGroup`, `Channel`), `permits` (`Semaphore`), `steps` and `heap_cells` (`Parallel`) after `invalid argument: `; `released more permits than were created` and `guard already held` after `invalid state: `.

## 19. `Std.Concurrent`

The modules of `Std.Concurrent` are implemented. [Concurrency](Concurrency) specifies the language forms they serve, the memory model, the scheduler, and the limits; this section gives the library contracts. Source: `stdlib/std/concurrent/`, one module per file, importing only `Std.Core` and `Std.Time`.

| Module | Contract |
| --- | --- |
| `Task` | `Task<T>` is what `spawn` returns. `join()` waits until the task has finished and returns its result; joining again returns the same value, and a join is never cancelled. `is_finished()` never waits. A handle is scope-bound: it cannot leave the `scope` block that started its task. |
| `Scope` | The handle that `scope (name) { ... }` binds. `cancel()` requests cancellation of the scope's tasks and all their descendants; it is idempotent, cannot be undone, wakes their parked operations, and never interrupts a running task. `is_cancelled()` reports a request made by `cancel` or by the deadline. `cancel_after(duration)` and `cancel_at(instant)` set a deadline on the monotonic clock and return `Result<Unit>`, an `Err` of kind `CapabilityDenied` when the clock is denied. `deadline()` is the monotonic reading at which the tasks are cancelled, the earlier of the scope's own deadline and the one it inherited, as an `Optional<Duration>`. Scope-bound. |
| `Channel` | `open<T>(capacity)` returns `[Sender<T>, Receiver<T>, Closer<T>]`, the three ends of one bounded FIFO channel of a transferable `T`. Capacity 0 is a rendezvous; a negative capacity traps with `invalid argument: capacity`. `send(value)` parks while the buffer is full and returns `SendOutcome`: `Sent`, `Closed`, or `Cancelled`. `receive()` parks while the channel is empty and open and returns `Received<T>`: `Value(T)`, `Closed`, or `Cancelled`. `try_send` (`Sent`, `Full`, `Closed`) and `try_receive` (`Value(T)`, `Empty`, `Closed`) never park. `close()` is idempotent: later sends report `Closed`, parked senders wake with `Closed` and their values are not delivered, and receivers see `Closed` once the buffer is empty. |
| `Cancellation` | What the running task can ask about itself. `requested()` is whether cancellation was requested for the scope that started the task or for any scope above it; the root task is never cancelled. `remaining()` is the time to the task's deadline as an `Optional<Duration>`, negative once it has passed, and `None` when the task has no deadline or the clock cannot be read. |
| `Timer` | A one-shot timer on the monotonic clock. `after(duration)` and `at(instant)` return `Result<Timer>`, an `Err` when the clock is denied. `wait()` parks until the timer has fired and returns `TimerOutcome`: `Fired`, or `Cancelled` when the task's scope is cancelled first. A `wait` can be a `select` arm, which is how a select gives up on its other arms after a time. |
| `Tasks` | Helpers that run a list of calls as tasks and combine the results; a call is a function value of no arguments. `all<T>(calls) -> T[list]` returns the results in the order of the calls. `all_ok<T>(calls) -> Result<T[list]>` returns the values in call order when every call returns `Ok`, and otherwise the first `Err` in completion order. `first_ok<T>(calls) -> Result<T>` returns the first `Ok` in completion order, or the `Err` of the call that completed last. `all_ok` and `first_ok` cancel the calls still running, and every helper waits for all of its calls before it returns. An `Err` made by a helper has the code `tasks.cancelled` (the calling task was cancelled while it waited) or `tasks.empty` (`first_ok` of no calls). The helpers take and return lists, not arrays. |
| `WorkerGroup` | A fixed number of workers over a bounded queue. `with_workers<T>(workers, capacity, work, body)` starts the workers, runs `body` with the group's handle, then closes the queue, lets the workers finish every queued job, and returns once they have stopped. On the handle, `submit(job)` parks while the queue is full and returns `SubmitOutcome` (`Submitted`, `Closed`, `Cancelled`); `try_submit(job)` never parks and returns `TrySubmitOutcome` (`Submitted`, `Full`, `Closed`); `close_and_drain()` closes the queue and waits for the queued jobs. Fewer than one worker traps with `invalid argument: workers`, and a negative capacity with `invalid argument: capacity`. |
| `Parallel` | `map<T, U>(items: T[], worker: func(T) -> U, limits: ItemLimits) -> U[]` returns what `worker` returns for each item, in input order. Each item runs alone, in a heap of its own, under `limits(steps, heap_cells)`, both positive. The worker is task-safe, issues no host request, and uses no scheduler operation and no object identity; the compiler checks this. If items trap, the run reports the trap of the lowest index. `--parallel-workers` chooses how many threads evaluate items and changes only wall time and peak memory. |
| `AtomicInt64` | A shared signed 64-bit integer. `create(initial)`, `load()`, `store(value)`, `add(delta)`, which returns the sum and traps on overflow leaving the value unchanged, and `compare_exchange(expected, desired)`, which reports whether it stored. All operations on one atomic form a single order consistent with each task's program order. |
| `Semaphore` | A fixed number of permits. `create(permits)` traps with `invalid argument: permits` for a negative count. `acquire()` parks until a permit is free and returns `AcquireOutcome`: `Acquired` or `Cancelled`. `try_acquire()` returns a `bool` and never parks. `release()` returns a permit and wakes the task that has waited longest; releasing more permits than were created traps with `invalid state: released more permits than were created`. |
| `Guarded` | Mutable state that tasks share one at a time. `create<T>(initializer)` builds the state inside the guard. `with<U>(body)` runs `body` with exclusive access and returns its result; a second task that calls `with` parks until the first returns, and that wait is not cancellable. The captures of `initializer` and `body` and the result `U` are transferable, so no reference to the state escapes. Calling `with` on a guard the task already holds traps with `invalid state: guard already held`. |

- Every handle except `Task<T>` and `Scope` is transferable ([Concurrency](Concurrency#51-transferable-types)). The compiler recognizes the handle classes by the trusted file that declares them.
- The classes `Sender`, `Receiver`, `Task`, and `Timer` also export `select_handle()` and a `<member>_selected(code)` function. The `select` statement calls them; a program does not. They are tagged `#Hidden`, so `neura docs` and the API listings leave them out.
- `Channel.State` is the identity the three ends of one channel share. A program does not construct one.
- Tests: `stdlib-concurrent` for `Tasks` and `WorkerGroup`; the `concurrency-*` and `backend-concurrency` entries for the rest ([Concurrency](Concurrency#15-verification)).
- Task safety of the library as a whole is in [section 5.8](#58-task-safety).

## 20. `Std.Events`

`Std.Events.Event` is a library module in `stdlib/std/events/event.nra` ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Lifetime030.md), implemented, proposed). It needs no language feature of its own. An owner keeps an `Event<T>`, emits payloads to the handlers subscribed to it, and exports its `EventView<T>`, which can only subscribe.

| Declaration | Contract |
| --- | --- |
| `Event<T>` | The owner's side. `new Event<T>()`; `subscribe(handler: func(T) -> void) -> Subscription`; `emit(payload: T)`; `view() -> EventView<T>`; `subscriber_count() -> int`. `#Final`, `#NotThreadSafe`. |
| `EventView<T>` | What an owner exports: `subscribe` only. It cannot emit and gives no access to the `Event`. `#Final`, `#NotThreadSafe`. |
| `Subscription` | The token of one registration. `unsubscribe()` removes that registration and does nothing when called again; `active() -> bool`. Two subscriptions are equal only when they are the same object. `#Final`, `#NotThreadSafe`. |

- `subscribe` keeps its handler until `unsubscribe`; collection is not unsubscription, and an event keeps its handlers alive. A handler that captures is therefore written `new func(...)`, and a bound method `new bound(receiver.method)`; a bare capturing handler is [E0453](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0453.md). The same handler subscribed twice is two registrations with two tokens.
- `emit` delivers synchronously, in registration order, to the registrations that exist when the emission starts. A registration added or removed by a handler takes effect from the next emission, so a handler removed during an emission is still called by it. A nested `emit` uses the registrations at its own start.
- Handlers return nothing. A trap in a handler ends the run.
- `emit` compares nothing and suppresses nothing. An owner commits its state first and then emits the old and new values it accepted, as a tuple payload; it does not read them again through a getter.
- The registrations are an immutable list that `subscribe` and `unsubscribe` replace, so `emit` copies nothing. `subscribe` and `unsubscribe` allocate; `emit` does not, beyond what handlers do.
- The three classes are not transferable, so giving one to a task is [E0602](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0602.md).
- `emit` is not called under a monitor guard ([E0550](https://github.com/Kayyo321/neura/blob/main/docs/diagnostics/E0550.md)): in a `#Synchronized` method, in a lambda passed directly to `Guarded.with`, or in a function either of them reaches through calls the compiler resolves. A call through a function value is not followed, so the check is a check of the known call graph and not a proof.
- Tests: `stdlib-events`, and `lifetime-programs` for delivery in every execution mode.

## 21. `Std.Crypto`

`Std.Crypto` holds the cryptography of the library: contracts, memory hygiene and constant-time helpers, a policy object, digests, MACs, key derivation, authenticated encryption, and random generation. It is Neura source running on the interpreter. Only `Sha256` reaches a C kernel, through `Std.Digest.Sha256`. Every primitive is checked against known answers from an independent implementation or from a standard's text (`tests/stdlib/crypto-*`, generated by `scripts/crypto-vectors/`). [The design note](https://github.com/Kayyo321/neura/blob/main/docs/design/Crypto030.md) records the API shape, the implementation tier of each item, and every stub.

**What the runtime does not provide.** Interpreted code does not run in constant time. The constant-time helpers are branch-free in source, and the runtime does not guarantee constant time. The runtime cannot lock memory (`mlock`) or keep it out of swap, and it cannot erase copies of a secret that the collector made. A member that would need a missing host service has the doc line `@stub` and traps with `unsupported: <Module>.<member> is not implemented in Neura 0.3.0 (<reason>)`.

### 21.1 Contracts

| Module | Contract |
| --- | --- |
| `Algorithm` | The enum `Algorithm` names every algorithm that a primitive or a policy can mention, including ones that are only denied (`Sha1`, `Md5`, `Rsa1024`) and the public-key and post-quantum schemes of later packages. |
| `SecretMemory` | `SecretMemory<T>`: `expose()` returns the secret and traps with `invalid state: secret wiped` after `wipe()`; `wipe()` overwrites the held storage; `is_wiped()`; `lock()` asks for page locking (a stub). |
| `SymmetricKey`, `PrivateKey` | `SymmetricKey<Alg>` and `PrivateKey<Alg>` extend `SecretMemory<uint8[list]>` with `key_length()` and `public_key_bytes()`. `Alg` is the class of the primitive that uses the key. |
| `CryptographicPrimitive` | `CryptographicPrimitive<Alg>` with `algorithm() -> Algorithm`. |
| `StreamingDigest` | `StreamingDigest<D>`, where `D` is the implementing class: the static requirements `create()`, `output_length()` and `block_length()`, and `update(bytes)` and `finish()`. `finish` returns `output_length` bytes, and an `update` or `finish` after it traps with `invalid state: digest finished`. |
| `ExtendableOutput` | `ExtendableOutput<D>: StreamingDigest<D>` adds `squeeze(count)`, which continues the output stream, and `read(max)` and `reader()`, a source in the shape `func(int) -> Result<uint8[list]>` that `Std.Io.BufferedReader` accepts. |
| `AeadCipher` | `AeadCipher<Key, Nonce, Tag>`: `seal(nonce, plaintext, associated_data)` returns the ciphertext followed by the tag; `open(nonce, sealed, associated_data) -> Result<uint8[list]>` fails with a `Custom` error of code `crypto.authentication` and releases no plaintext. A nonce of the wrong length traps; `AesSiv` reports a nonce length of 0 and takes a nonce of any length, including none. |
| `Signer`, `Verifier` | `Signer<PrivKey, Sig>` has `sign(message) -> Sig`. `Verifier<PubKey, Sig>` has `public_key()` and `verify(message, signature) -> bool`. |
| `KeyEncapsulation` | `encapsulate(public_key) -> Encapsulation<SharedSecret, Ciphertext>` and `decapsulate(private_key, ciphertext) -> Result<SharedSecret>`. |
| `KeyStoreProvider` | `KeyStoreProvider<KeyHandle>`: `provider_name`, `generate_key(algorithm, label)`, `sign`, `wrap_key`, `unwrap_key` and `delete_key`, each returning a `Result`. |

### 21.2 `Std.Crypto.Core`

| Module | Contract |
| --- | --- |
| `SecretBytes` | Holds a private copy of the bytes it is given. `expose()` returns the held list, `wipe()` overwrites it with zeros, and `length()` stays readable. `ct_equals(other)` compares contents without a data-dependent branch. `equals` and `hash_code` keep the identity defaults, so contents are never compared or hashed by accident. `to_string` is `SecretBytes(<n> bytes, <redacted>)`. `lock()` is a stub. |
| `Secret` | `Secret<T>` wraps any value with an `eraser` function that `wipe()` calls once, because generic code cannot overwrite a value of unknown type. `zero_bytes` is the eraser for byte lists. `to_string` is `Secret(<redacted>)`, and `lock()` is a stub. |
| `SecretKey` | `SecretKey<Alg>` implements `SymmetricKey<Alg>` over a `SecretBytes`. A cipher constructor that takes a `SymmetricKey<AesGcm>` rejects a key typed for another algorithm at compile time. |
| `ConstantTime` | `ct_eq(a, b)` on byte lists (a length difference returns false at once, because lengths are public), `ct_eq_uint32`, `ct_less` and `ct_select` on `uint32` (results 0 or 1), and `ct_select_bytes`. Branch-free in source; the runtime does not guarantee constant time. |
| `CryptoPolicy` | One process-wide `Policy`, which is `Modern` (the default), `Fips140_3` or `Cnsa2_0`. `set_policy`, `policy`, `allows(algorithm)`, `allowed_by(policy, algorithm)`, and `require(algorithm)`, which traps with `denied: <Algorithm> is not allowed under the <Policy> policy`. Each primitive calls `require` when it is created. `Modern` denies `Md5`, `Sha1` and `Rsa1024`. `Fips140_3` allows the FIPS-approved algorithms: SHA-2, SHA-3 and SP 800-185, HMAC, KMAC, CMAC, AES, GCM, FF1, HKDF, PBKDF2, and the approved public-key and post-quantum schemes. `Cnsa2_0` allows AES-256, SHA-384 and SHA-512, ML-KEM-1024 and ML-DSA-87, and HMAC, HKDF, PBKDF2 and Merkle trees over those hashes. The policy is checked at run time only. It is held in a `Std.Concurrent.AtomicInt64`, so primitives can be created in spawned tasks; set it at startup, because a change reaches each task at its next creation of a primitive. |

### 21.3 `Std.Crypto.Random`

| Module | Contract |
| --- | --- |
| `Entropy` | `os_bytes(count) -> Result<uint8[list]>` reads the operating system's generator through the existing host service `neura.entropy.fill` (requests of at most 1 MiB each) and needs the entropy capability. A denied capability is an `Io` error of kind `CapabilityDenied` with the message `entropy request failed`. `hardware_bytes(count)` (RDSEED or RNDR) and `fork_generation()` (fork and VM-clone detection) are stubs. `fused_seed()` is SHA-512 over a label, 64 OS bytes and 64 hardware bytes, so it traps while `hardware_bytes` is a stub. |
| `ChaCha20Drbg` | A generator with fast key erasure. The key starts as SHA-256("Neura ChaCha20-DRBG seed" ‖ seed), for a seed of at least 32 bytes; `reseed(material)` sets it to SHA-256("Neura ChaCha20-DRBG reseed" ‖ key ‖ material). `fill(count)` takes the ChaCha20 keystream under the key with a zero nonce, uses its first 32 bytes as the next key, and returns the next `count` bytes. Also `next_uint32`, `next_uint64`, `next_below(bound)` (rejection sampling), `reseed_from_entropy() -> Result<Unit>`, and `wipe`. `from_entropy()` seeds from 32 OS bytes. `fork_safe()` also checks `fork_generation` before every output and reseeds after a change, so it traps until that stub is implemented. A seeded generator is reproducible, which makes it suitable for tests and for deterministic derivation; seed it from `os_bytes` for secrets. |

### 21.4 `Std.Crypto.Hash`

| Module | Contract |
| --- | --- |
| `Sha2` | `Sha224`, `Sha256`, `Sha384`, `Sha512` and `Sha512_256` (FIPS 180-4) implement `StreamingDigest`, with the functions `sha224(bytes)` through `sha512_256(bytes)`. `Sha256` runs the kernel of `Std.Digest.Sha256`, so the two modules share one implementation. The others are Neura code over the `uint32` and `uint64` recipes of [section 7.2](#72-arithmetic-without-wrapping-intrinsics). |
| `Sha3` | `Sha3_224`, `Sha3_256`, `Sha3_384` and `Sha3_512` (FIPS 202) implement `StreamingDigest`. `Shake128` and `Shake256` implement `ExtendableOutput`, and `finish` returns 32 or 64 bytes. `CShake128` and `CShake256` (SP 800-185) take a function name and a customization string; with both empty they equal SHAKE. `TurboShake128` and `TurboShake256` (RFC 9861) run 12 rounds with a domain byte from 0x01 to 0x7F, 0x1F by default, and trap on any other. The functions are `sha3_224` through `sha3_512`, `shake128(bytes, length)`, `shake256`, `cshake128(bytes, length, function_name, customization)` and `cshake256`. |
| `TupleHash` | `TupleHash128` and `TupleHash256` (SP 800-185, section 5) take the customization string, then `add(item)` for each item. `finish(length)` ends the hash, and `finish_xof(length)` with `squeeze` gives the XOF form. The functions are `tuple_hash128(items, length, customization)` and `tuple_hash128_xof`, and the same for 256. Item boundaries are part of the hash. |
| `Blake3` | `Blake3` implements `ExtendableOutput`, and a 32-byte key gives the keyed mode. `hash(bytes, length)`, `keyed_hash(key, bytes, length)` and `derive_key(context, material, length)`. It is the serial reference algorithm, so the result equals the specification's but no SIMD or multithreading is used. |
| `MerkleTree` | `MerkleTree<H: StreamingDigest<H>>` is append-only and hashes as RFC 6962 and RFC 9162, section 2.1 do: a leaf is H(0x00 ‖ data), a node is H(0x01 ‖ left ‖ right), and the empty tree is H(""). It has `append(data) -> index`, `size`, `leaf`, `root`, `root_at(size)`, `inclusion_proof(index, size)` and `consistency_proof(old_size, new_size)`. The module functions `leaf_hash<H>`, `node_hash<H>`, `verify_inclusion<H>` and `verify_consistency<H>` follow the algorithms of RFC 9162, sections 2.1.3.2 and 2.1.4.2. A root or proof costs time in proportion to the tree size. |

Every digest checks the policy when it is created. An `update` after the output was read, or a second `finish` of a fixed-length digest, traps with `invalid state: digest finished`. No hardware acceleration (SHA-NI, AVX-512) is used.

### 21.5 `Std.Crypto.Mac`

| Module | Contract |
| --- | --- |
| `Authenticator` | The interface of every MAC here: `algorithm`, `tag_length`, `update(bytes)`, `finish() -> tag` and `verify(tag) -> bool`. `verify` compares in constant time and accepts only a tag of the full length, never a truncated one. An `update` after `finish` or `verify`, or a second `finish`, traps with `invalid state: mac finished`. |
| `Hmac` | `Hmac<H: StreamingDigest<H>>(key)` (FIPS 198-1) over any digest, with `mac<H>(key, message)` and `verify<H>(key, message, tag)`. A key longer than the block is hashed first. The policy is checked for `Hmac` and for `H`. |
| `Kmac` | `Kmac128` and `Kmac256` (SP 800-185, section 4). `new Kmac128(key)` gives a 32-byte tag (64 for `Kmac256`); `new Kmac128(key, customization, length)` takes any length of at least 4 bytes. `finish_xof(count)` and `squeeze` give KMACXOF. The functions are `kmac128(key, data, length, customization)`, `kmac128_xof` and the same for 256. |
| `Poly1305` | The one-time authenticator of RFC 8439 under a 32-byte key, with `mac(key, message)`. The key must not be reused. The final reduction selects with a mask. |
| `AesCmac` | AES-CMAC (SP 800-38B, RFC 4493) with 16-, 24- or 32-byte keys, and `mac(key, message)`. |

### 21.6 `Std.Crypto.Kdf`

| Module | Contract |
| --- | --- |
| `Hkdf` | RFC 5869 over any digest: `extract<H>(salt, input_key)` (an empty salt is HashLen zero bytes), `expand<H>(prk, info, length)` (traps above 255 × HashLen), `derive<H>(salt, input_key, info, length)`, and `expand_label<H>(prk, label, length)`, which puts a domain-separation label in `info`. |
| `Pbkdf2` | `derive<H>(password, salt, iterations, length)` (SP 800-132, RFC 8018). Fewer than one iteration traps. |
| `Scrypt` | `derive(password, salt, n, r, p, length)` (RFC 7914). It traps when N is not a power of two above 1, when r·p ≥ 2^30, or when 128·r·N exceeds 1 GiB. |
| `Argon2` | Argon2id version 0x13 (RFC 9106). `Argon2Params(memory_kib, iterations, parallelism, tag_length)` is an immutable value that traps on out-of-range values. `argon2id(password, salt, params)` and `argon2id_with(password, salt, secret, associated_data, params)`. A salt under 8 bytes traps. Lanes are filled one after another, which gives the result of parallel filling. Argon2d and Argon2i are not offered. |
| `Balloon` | `derive<H>(password, salt, space_cost, time_cost)`, single-buffer Balloon hashing with delta = 3 over any digest. The paper leaves the encodings open. This library uses 8-byte little-endian counters and indices, concatenated hash inputs, and `to_int` as the first 8 digest bytes little-endian modulo `space_cost`. No standard test vectors exist. |
| `Phc` | `PhcString(id, version, names, values, salt, hash)` and `parse(text) -> Result<PhcString>` for the PHC string format (`$id$v=19$m=65536,t=3,p=4$salt$hash`, B64 without padding). Malformed text is a `Parse` error at the offending offset. `to_string` is the canonical encoding, which `equals` and `hash_code` use. |
| `PasswordHash` | `hash_password(password, salt, params)` gives an argon2id PHC string; `hash_password_scrypt(password, salt, log2_n, r, p, length)` gives a scrypt one. `verify_password(password, encoded) -> Result<bool>` handles argon2id v=19 and scrypt and compares in constant time; an unknown function or bad parameters is `Custom` code `crypto.unsupported`. `needs_rehash(encoded, wanted)` is true unless the string is argon2id v=19 with exactly `wanted`'s parameters. |
| `Calibration` | `calibrate_argon2id(clock, target, parallelism, max_memory_kib) -> Result<Argon2Params>` measures on the given `Std.Time` clock. It starts at 8 KiB per lane and one pass, doubles memory while a run is faster than the target and within the cap, then adds passes up to 10. Its result depends on the host and is not deterministic. |

### 21.7 `Std.Crypto.Symmetric`

Raw ECB and CBC modes are not exposed. Every AEAD implements `AeadCipher<SymmetricKey<C>, uint8[list], uint8[list]>`, where `C` is its own class. A constructor takes a `SymmetricKey<C>` (such as `SecretKey<AesGcm>`) or raw key bytes, and a key of the wrong length traps. `seal` returns the ciphertext and then the tag, except for `AesSiv`, which puts its synthetic IV first. `open` compares tags in constant time. A failed `open` is `Custom` code `crypto.authentication` and releases no plaintext. Each cipher checks the policy for its variant, such as `Aes128Gcm` or `Aes256Gcm`, when it is created.

| Module | Contract |
| --- | --- |
| `AesBlock` | `AesBlock(key)`: the AES-128, AES-192 or AES-256 block cipher (FIPS 197), with `encrypt_block` and `decrypt_block` on 16-byte blocks. It is a building block, not an encryption mode. |
| `AesGcm` | AES-GCM (SP 800-38D) with 16-, 24- or 32-byte keys, 12-byte nonces only, and 16-byte tags. A nonce must never repeat under one key. |
| `AesGcmSiv` | AES-GCM-SIV (RFC 8452) with 16- or 32-byte keys, 12-byte nonces and 16-byte tags. A repeated nonce reveals only whether two messages were equal. |
| `AesSiv` | Deterministic AEAD (RFC 5297) with 32-, 48- or 64-byte keys. Through `AeadCipher`, the associated-data items are `[data]`, or `[data, nonce]` when the nonce is not empty. `seal_items(items, plaintext)` and `open_items` take RFC 5297's vector of up to 126 items. The output is the 16-byte synthetic IV followed by the ciphertext. Equal inputs give equal outputs, which suits exact-match lookups on encrypted columns. |
| `ChaCha20` | Hazmat functions without authentication: `xor(key, nonce12, counter, bytes)` (RFC 8439), `xchacha20_xor(key, nonce24, counter, bytes)`, and `hchacha20(key, nonce16)`. A block counter that would pass 2^32 - 1 traps. |
| `ChaCha20Poly1305`, `XChaCha20Poly1305` | RFC 8439 with 12-byte nonces, and the 24-byte-nonce variant of draft-irtf-cfrg-xchacha-03, which derives a subkey with HChaCha20. Random 24-byte nonces do not collide in practice. |
| `OnlineCipher` | STREAM over any AEAD whose nonce is at least 12 bytes. Segment *i* uses the nonce prefix ‖ *i* as a big-endian `uint32` ‖ a last-segment byte (1 on the last segment, otherwise 0). The prefix is 5 bytes shorter than the AEAD's nonce. `StreamEncryptor<K>(aead, prefix, data)` has `seal_segment(plaintext, last_segment)` and `is_ended`. `StreamDecryptor<K>` has `open_segment(sealed, last_segment) -> Result` and `finish() -> Result<Unit>`, which fails when the last segment was never seen. Truncated, reordered, altered or appended segments fail. `seal_segments` and `open_segments` work on a whole message. Each segment is processed in memory proportional to its own length. |
| `Ff1` | FF1 format-preserving encryption (SP 800-38G) over AES. `Ff1(key, radix)` takes a radix from 2 to 65536 and has the fields `radix` and `min_length`, the shortest length with radix^length ≥ 10^6. `encrypt(numerals, tweak)` and `decrypt` take numerals as `int[list]`. `encrypt_text` and `decrypt_text` take text in the digits and lowercase letters 0-9a-z cut to the radix (radix 36 at most) and return `Result<string>`; a bad character or a short input is an `Encoding` error. |
