
# Standard library implementation plan

This page defines the Neura standard library's source modules, contracts, and algorithms. The language and runtime already supply the facilities in [section 3](#3-language-baseline-and-gaps). Every module and host service on this page is implemented in `stdlib/` and the runner, and tested as [section 17](#17-verification) describes; [Status](Status.md) and [Standard library](StandardLibrary.md) list what shipped. [Section 18](#18-implementation-decisions) records the decisions made where this plan left a choice open, and where the implementation departs from it.

## 1. How to read this plan

- **Syntax is not specified here.** This page describes behavior, data layout, and algorithms in prose and tables. It contains no Neura source.
- **Member notation.** Tables write a member as `name(parameter: Type, ...) → Result`. A function type is written `(A, B) → R`. A tuple type is written `[A, B]`, as in the language. This is documentation notation.
- **Names are binding.** Module, type, and member names on this page are the names to implement. They follow the [coding style](../style/CodingStyle.md). No member is named with a keyword; [section 15.2](#152-name-audit) lists the names that changed for that reason.
- **Dependency labels.** Labels such as G1 and T1 point to compiler facilities used by an API. Their presence in an API table records what the implementation relies on. Host bindings also require registered providers.
- **"Decision"** marks a choice this plan makes. Decisions are final for the first version. Changing one requires editing this page first.
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
7. **No overloading and no class-level members.** Every member has a distinct name. A type lives in a module of the same name, and that module's functions are its factories and helpers ([section 4.2](#42-source-layout)).
8. **Storage is reclaimed; resources are closed.** Garbage collection has shipped, so no API has an owner that must `destroy` it. A host resource is released only by an explicit `close`. Reclamation never runs `deinit` and never closes anything.
9. **Collection type arguments are invariant.** `List<Alert>` and `List<Renderable>` are distinct types. Interfaces define extension points. A library class is meant to be subclassed only where its contract says so.
10. **Pay only for what is reachable.** An image contains only what the entry point can reach.

## 3. Language baseline and gaps


### 3.1 What the probes established

The former frontend rejections for generic callbacks, generic enums, interface declarations, type tests, and string operations no longer describe the current compiler. These fixtures exercise the corresponding behavior through source, optimized and unoptimized NVR builds, and execution. The supported API contracts and remaining restrictions are in [Status](Status.md) and the language pages.

| Capability | Evidence and boundary |
| --- | --- |
| Tuples and recursive payload enums | [Aggregates](../../tests/backend/programs/aggregates.nra) exercises tuple returns and destructuring, recursive values, and exhaustive matching. The source-level JSON model can use its own enum and built-in lists. |
| Closures with generic types | [Generic closures](../../tests/backend/programs/generic-closures.nra) exercises specialization. Captures still snapshot bindings and share references to objects. |
| Generic enum payloads and constructors | [Generic enums](../../tests/backend/programs/generic-enums.nra) covers explicit specialization and matching. Nested patterns and match guards remain outside the design. |
| Class defaults, nil, and structural value members | [Classes and nil](../../tests/backend/programs/class-nil.nra) exercises identity, override dispatch, casts, and operations on primitive and composite values. |
| Interfaces, multiple bounds, and for-each | [Interfaces](../../tests/backend/programs/interfaces.nra) exercises generic requirements, inherited defaults, multiple interface bounds, and the iterator protocol. |
| Text, raw float bits, and message traps | [Primitives](../../tests/backend/programs/stdlib-primitives.nra) covers UTF-8, IEEE bit access, and scalar value members. [Message traps](../../tests/backend/traps/message-trap.nra) checks the Unicode message and source location. |
| Reserved library roots, source host bindings, and the entry point | [Backend library test](../../tests/backend/stdlib.cmake) checks trusted imports, both entry forms, reachable host bindings, and host error continuations. Its installed fixture does not establish availability of every proposed host provider. |
| Runtime-sized collections and GC | Built-in lists grow at runtime and expose their structural `version`. Arrays still originate from literals. The [VM tests](../../tests/backend/vm_tests.cpp) check collection under heap pressure and tracing through cycles. Public `insert_at` and `truncate` remain optional. |


### 3.2 Status of the first plan's blockers

| First plan | Current design |
| --- | --- |
| L1 Interfaces, L12 iterable for-each | [G4](#g4-interfaces) and [G5](#g5-for-each-over-library-collections) supply the language machinery. The collection interfaces and adapters still need source implementations. |
| L2 Universal members and an `Object` root | [G3](#g3-universal-members) supplies operations for supported values and an explicit class-reference root spelled `class`. Scalar values are not boxed into it. |
| L3 Function types and closures | [G1](#g1-function-types-over-type-parameters) permits concrete and generic callback types. |
| L4 Automatic reclamation | [NVM](../runtime/NVM.md#garbage-collection) supplies deterministic tracing and reclaims cycles. Host resources still require explicit close. |
| L5 Vacant storage and storage operations | Runtime-sized arrays, clearing vacant slots, and bulk copy are unnecessary for the chosen layouts. Lists supply growing storage and `nil` supplies absent node links. [Optional accelerations](#34-optional-accelerations) |
| L6 Bitwise operators and wrapping arithmetic | Checked shifts and bitwise operators are available. [Arithmetic recipes](#72-arithmetic-without-wrapping-intrinsics) implement modular operations without changing ordinary checked arithmetic. [N1](#n1-float-bit-access) supplies exact float bit access. |
| L7 `char` and string semantics | [T1](#t1-text-primitives) supplies immutable UTF-8 text primitives. [Scalars](#83-scalars) represent code points as `uint32`. |
| L8 Class-level members and closed classes | Type modules supply factories. No class-level members are required. |
| L9 Type tests and downcasts | Class and interface references support `is` and checked `as`. Error outcomes still use exhaustive matching, and collection APIs compare their typed values directly. |
| L10 Enum intrinsics | Fieldless ordinals and variant text are available. Ordinal collections use integer keys. A payload enum's discriminant does not enumerate its values, and no reverse ordinal lookup or reflection is required. |
| L11 Packaging | [P1](#p1-library-root) reserves the installed standard-library namespace. Imports stay explicit and facade re-exports are optional. |
| L13 Host and intrinsic declarations | [P2](#p2-host-bindings-in-source) supplies named and versioned host bindings in trusted source. Text, float bit access, and message traps are ordinary language primitives. |
| E1–E6 Error handling | Returned failures are `Result<T>`, with one active case. Runtime traps keep their reason codes and cannot be caught. Message traps carry reason 3. Unwinding, defer, and recovery are outside the design. |


### 3.3 Compiler and runtime dependencies

The identifiers below describe dependencies of the source implementations. A compiler facility can be implemented before the corresponding library module or host provider is ready.

#### G1. Function types over type parameters

Function types may mention enclosing type parameters in signatures, locals, fields, tuples, collections, and type arguments. Generic bodies may create closures that capture such values. Specialization resolves their types and generates the concrete callable and capture layouts. Captured local bindings are copied. Mutating a captured object's fields mutates the shared object.

This supports generic predicates, comparators, collection callbacks, `Sequence` adapters, and `Optional` and `Result` mapping helpers.

#### G2. Generic enums

Generic declarations use `enum<T>(None, Some(T))` and specialize per concrete argument list. Variant construction supplies explicit type arguments, as in `Optional<T>.Some(value)`. A pattern uses the enum qualifier, as in `Optional.Some(value)`, or a concrete type overlay. The subject's type fixes the pattern's payload types.

A specialized enum can hold tuples, collection elements, callbacks, class references, or nested enum values. Its variants remain exhaustive. This permits direct `Optional<T>` and `Result<T>` source implementations. Nested patterns, guards, and inferred type arguments remain outside this design.

#### G3. Universal members

- **G3a. Value members.** Supported scalars, strings, fieldless enums, tuples, arrays, and lists have `equals`, `hash_code`, and `to_string`. Generic calls resolve after specialization. Tuples and collections recurse into supported element types. Payload enums require functions supplied by their authors. Calling a generic value operation with an unsupported specialized type is diagnosed.
- **G3b. Class members.** The root type `class` holds class and interface references. Classes inherit `equals(other: class)`, `hash_code()`, and `to_string()`, with ordinary override dispatch. An override can use `other is MyClass` before a checked `other as MyClass` to compare fields. `==` and `!=` on class references compare identity and never call an override. Default hashing and text allocate an identity number on the first request; the number follows request order rather than allocation order. Variant and class names used by these operations are execution data.
- **Nil.** A class or interface reference can be explicitly `nil`. An uninitialized field remains a separate runtime fault. Direct member dispatch on a nil receiver traps. Nil participates in identity comparison. Generic and composite value operations treat two nil references as equal, hash nil to 0, and print `nil`.

These contracts support default key operations in hash collections. A type may still supply custom comparison, equality, and hash callbacks where a collection constructor accepts them.

#### G4. Interfaces

Interfaces can be generic, inherit other interfaces, and declare required and default methods. A class can implement several interfaces. Multiple bounds are expressible on a type parameter. Inherited default conflicts require an explicit override. Interface values use the existing class dispatch machinery and can hold `nil`.

The standard collection and stream interface declarations are source work. Existing callback-based adapters can remain where a function type accurately describes a single operation.

#### G5. For-each over library collections

For-each over an interface iterable calls `iterator()` once, tests `has_next()`, and calls `next()` for each element. Collection iterators own their structural-change checks. For-each over a built-in list uses the existing NVM structural-version rule. A library adapter should keep its order and modification contract the same through either entry point.

#### T1. Text primitives

Strings are immutable UTF-8 with byte length bounded by `int`. The language supplies:

| Primitive | Contract |
| --- | --- |
| `text.byte_length` | Byte count as `int`. |
| `text.byte_at(index)` | One `uint8` at the checked byte index. |
| `text.slice(start, end)` | Half-open byte range. Both endpoints must be UTF-8 boundaries; the endpoint at the end of the string is allowed. |
| `string.from_utf8(bytes)` | Copies a `uint8[]` or `uint8[list]` and returns `[bool, string]`. Invalid UTF-8 returns `[false, ""]`; empty valid input returns `[true, ""]`. The library maps validation failure to its documented encoding error. |
| `==`, `!=` | Compare UTF-8 bytes without normalization. |

These operations supply the substrate for `Std.Text`, parsing, encodings, and string-keyed collections.

#### N1. Float bit access

A float value's `to_bits()` returns the unsigned integer of the same width. `float16.from_bits`, `bfloat16.from_bits`, `float32.from_bits`, and `float64.from_bits` preserve every bit, including NaN payloads and the sign of zero. The `float` alias has the `float32` operation. Reinterpreting bits does not round or perform a numeric `as` conversion. Circuit conversion connects the same value lanes to a differently typed result.

#### F1. Trap with a message

The built-in statement `trap_with_message(message)` ends execution with trap reason 3 and includes its string in the diagnostic. It does not need an assertion host service. `Failure.trap` wraps this primitive; its callers do not catch it.

Use the primitive directly in a value-returning function's trap-only branch so that return analysis sees a terminator. Calls to a void wrapper follow the ordinary function typing rules.

#### P1. Library root

The installed tool finds its standard-library source root relative to the executable. Reserved `Std` imports resolve in that trusted tree, separately from ordinary `-I` roots. A user file cannot declare or shadow the `Std` namespace. Imports containing an `Internal` component are reserved for trusted library source. No implicit prelude is added.

#### P2. Host bindings in source

Trusted library source can bind a bodyless function with the suffix `host("service.name", version)`. The return type is a payload enum with `Ok` and `Error(uint32)` variants, with an optional success payload on `Ok`. Both continuations return to the caller. The emitted import records the signature and version and includes only reachable bindings. A service still needs a matching runtime provider and a capability policy. A declaration alone cannot create either.

#### P3. Entry point from the library

An override of `Std.Program.Entry.main` can be the image entry point. Its `ExitCode` maps `Ok` to 0, `Failure` to 1, and `Status(int)` to the validated process status. The compiler still accepts the legacy `SysIo.Entry` route for migration and rejects an image with two entry points.

### 3.4 Optional accelerations

None of these blocks anything. Each replaces a loop of storage operations with one, or saves typing. They are listed so that nobody mistakes them for prerequisites.

| ID | Addition | Replaces | Benefit |
| --- | --- | --- | --- |
| A1 | Allocate a list of a runtime length filled with one value | A loop of `append` | Hash index tables and sort buffers are filled in one operation. |
| A2 | List insert at an index, and truncate to a length | `append` then shifting by element writes; repeated `remove_at` of the last element | `add_at`, `clear`, and `truncate` cost one operation instead of one per element. |
| A3 | Bulk copy of a range between lists | Element-by-element reads and writes | Sorting merges and deque rebalancing. |
| A4 | Read of a list's structural version, implemented as `list.version` | A modification count kept by the wrapper class | Iterator objects detect modification made through any backing-list alias. [ArrayList](#93-arraylistt) |
| A5 | Hexadecimal integer literals | Decimal constants | Readability of masks and published constants. |
| A6 | Re-export of generic classes, generic functions, and payload enums | Importing each type's own module | One import per library area ([section 4.2](#42-source-layout)). |
| A7 | Inference of type arguments | Explicit type arguments at every generic call | Shorter call sites. |

## 4. Architecture

### 4.1 Modules

| Module | Contents | Section |
| --- | --- | --- |
| `Std.Core` | `Failure`, `Ordering`, `Hash`, `Error`, `Optional`, `Result`, `Unit`, `IntRange`, the primitive companion modules, `StringBuilder` | [5](#5-core-contracts), [6](#6-stdcore-types), [7](#7-primitive-companion-modules) |
| `Std.Text` | `Strings`, `Scalars`, `Regex` | [8](#8-stdtext) |
| `Std.Collections` | `ArrayList`, `ArrayDeque`, `LinkedList`, `HashMap`, `HashSet`, `TreeMap`, `TreeSet`, `PriorityQueue`, `BitSet`, `OrdinalMap`, `Sequence`, `Sorting`, the interfaces | [9](#9-stdcollections) |
| `Std.Math` | `Math`, `BigInteger`, `BigDecimal` | [10](#10-stdmath) |
| `Std.Io` | `Console`, byte and text streams, buffers | [11.1](#111-stdio) |
| `Std.Fs` | `Path`, `File`, `Fs` | [11.2](#112-stdfs) |
| `Std.Program` | `Entry`, `ExitCode`, `Environment`, `Process` | [11.3](#113-stdprogram) |
| `Std.Net` | TCP streams and listeners | [11.4](#114-stdnet) |
| `Std.Time` | `Duration`, `Instant`, `Clock`, calendar types | [12.1](#121-stdtime) |
| `Std.Random` | `Random`, `Uuid` | [12.2](#122-stdrandom) |
| `Std.Encoding` | `Utf8`, `Hex`, `Base64`, `Json`, `Csv` | [13.1](#131-stdencoding) |
| `Std.Digest` | `Crc32`, `Sha256` | [13.2](#132-stddigest) |
| `Std.Log` | `Logger` | [14.1](#141-stdlog) |
| `Std.Test` | Assertions | [14.2](#142-stdtest) |
| `Std.Concurrent` | Reserved; not designed | [15.1](#151-deferred-and-excluded) |

`Std.Program`, `Std.Io.Console`, and `Std.Test` replace the built-in `SysIo` and `Test` modules, which are deleted ([section 4.6](#46-retiring-sysio-and-test)).

### 4.2 Source layout

- The library source tree is `stdlib/`, laid out by the rules in [Project structure](ProjectStructure.md). The installed tool discovers its trusted copy automatically. Ordinary `-I` roots cannot provide `Std` modules.
- **Decision: one module per type, named after the type.** `stdlib/std/collections/array-list.nra` declares the module `ArrayList`, imported as `Std.Collections.ArrayList`, which exports the class `ArrayList`. The probes confirmed that a module can export a class of its own name and that the importer can construct it and use the module's enums in `match`.
- **Factories are module functions.** Where Java writes a static method on a class, this library has an exported function in the type's module. This is why the first plan's class-level members are not needed. In the tables below, a type's "module functions" are these.
- **Internal helpers** are unexported members of the module that uses them, or separate modules under an `Internal` component (`Std.Collections.Internal.HashCore`) when two types share them. Nothing outside `Std` imports an `Internal` module; P1's namespace rule extends to enforce that.
- **Facades are deferred.** A module such as `Std.Collections` that re-exports every collection type needs acceleration A6. It is a convenience: the type modules are the stable import paths and remain valid afterwards.
- **There is no prelude.** Every file imports what it uses. The first plan's implicit import of `Std.Core` saved a line per file at the cost of a compiler feature and an invisible dependency.
- The library is compiled together with the program. There is no separately compiled library image.

### 4.3 Dependency order

A module may import only modules above it. Cycles are forbidden.

1. `Std.Core`
2. `Std.Collections`
3. `Std.Text`
4. `Std.Math`
5. `Std.Text.Regex`, `Std.Encoding`, `Std.Digest`, `Std.Random`, `Std.Time`
6. `Std.Io`, `Std.Program`
7. `Std.Fs`, `Std.Net`
8. `Std.Log`, `Std.Test`

`StringBuilder` is in `Std.Core` because number formatting needs it. `Std.Core` never names a collection class: where a core function returns several values it returns a tuple or a built-in list. `Std.Test` is last in the import order but early in the schedule: its assertions for built-in types import only `Std.Core`.


### 4.4 Built-in floor

The library relies on text inspection and UTF-8 construction (T1), float bit access (N1), message traps (F1), typed class identity and scalar formatting (G3), and explicit host bindings (P2). These are compiler and NVM operations, not library services.

Wrapping arithmetic, integer sign reinterpretation, vacant storage, bulk copy, and enum reflection are unnecessary for the chosen implementations.

### 4.5 Host services

Every interaction with the outside world is a host service with a name, a version, typed arguments, and typed success and error results ([NVM](../runtime/NVM.md#host-calls-and-standard-output)). An error result is a `uint32` code that the library maps to an `IoErrorKind` ([section 6.3](#63-error)): 1 `NotFound`, 2 `PermissionDenied`, 3 `AlreadyExists`, 4 `InvalidInput`, 5 `Interrupted`, 6 `UnexpectedEnd`, 7 `NotADirectory`, 8 `IsADirectory`, 9 `DirectoryNotEmpty`, 10 `ConnectionRefused`, 11 `ConnectionReset`, 12 `TimedOut`, 13 `Unsupported`, 14 `CapabilityDenied`, and every other code `Other` (the providers use 100).

| Service | Version | Arguments | Success | Used by |
| --- | --- | --- | --- | --- |
| `neura.console.write_line` | 1 | text | none | `Console.print_line`; its error code 1 maps to `Other` |
| `neura.console.write` | 1 | stream (1 output, 2 error), bytes | count written | `Console` |
| `neura.console.read` | 1 | maximum count | bytes; empty at end of input | `Console` |
| `neura.fs.open`, `read`, `write`, `seek`, `close` | 1 | path and mode; file ID and count, bytes, or offset and origin | file ID, bytes, count, or position | `File` |
| `neura.fs.stat`, `list`, `make_directory`, `remove`, `rename` | 1 | path text (two for rename); `stat` also takes a follow-links flag and `remove` a directory flag | kind, size, and modified time; list of names; none | `Fs` |
| `neura.clock.wall`, `monotonic`, `sleep` | 1 | none, none, nanoseconds | seconds and nanoseconds; nanoseconds; none | `SystemClock` |
| `neura.entropy.fill` | 1 | count | bytes | `Random.from_entropy` |
| `neura.env.get` | 1 | name text | found flag and value text | `Environment` |
| `neura.process.exit` | 1 | status | does not return | `Process.exit` |
| `neura.net.connect`, `listen`, `local_port`, `accept`, `read`, `write`, `close` | 1 | [section 11.4](#114-stdnet) | socket ID, port, bytes, or count | `Std.Net` |

The exact signatures are in [NVM](../runtime/NVM.md#host-services). The retired `neura.test.assert` and `neura.test.assert_equal_int32` services are no longer registered.

- **Capabilities.** `neura run` and `neura test` allow only the console group and `neura.process.exit` by default. `--allow-fs`, `--allow-net`, `--allow-env`, `--allow-clock`, and `--allow-entropy` enable groups; `--allow-fs` takes an optional list of path prefixes. A denied service still binds, and each call returns error 14.
- **Replay.** The NVM library already records and replays host interactions (`nvm::record`, `nvm::replay`). `neura run --record file` and `--replay file` expose that. Tests of I/O, time, and entropy run under replay.
- **Ordering.** `neura.fs.list` returns names sorted by their UTF-8 bytes. The host sorts.
- Host IDs are opaque `int64` values that the library wraps in classes and never exposes.

### 4.6 Retiring `SysIo` and `Test`

This migration is complete: every example and test uses the new modules, and importing `SysIo` or `Test` is an error that names `Std.Io.Console` and `Std.Program`, or `Std.Test`. Images built earlier whose only import is `neura.console.write_line` still run; images that call the retired assertion services fail to bind (`tests/backend/legacy`). The rest of this section is the plan as written.

`SysIo` and `Test` are development scaffolding: the compiler resolves both names during lowering, the converter recognizes `SysIo.println` and the entry override by name, and NVM registers two test services. Library development ends with all of that deleted. Nothing forwards from the old names, and no alias is kept.

| Today | Replacement | Needs |
| --- | --- | --- |
| `SysIo.println(text)`; a failed write traps with reason 3 | `Console.print_line(text)` in `Std.Io.Console`: a function bound to `neura.console.write_line` version 1, wrapped so that a failed write calls `Failure.trap` | P2, F1 |
| `SysIo.Entry`, a module with the requirement `main` | `Std.Program.Entry`, a library module with the same requirement | P3 |
| `SysIo.Entry.ExitCode`, with the single value `Ok` | `Std.Program.ExitCode`, a payload enum: `Ok` (status 0), `Failure` (status 1), `Status(int)` (0 through 255; anything else traps) | P3 |
| `Test.assert(condition)` and `Test.assert_equal_int32(expected, actual)`, host services that record a message and return an error that traps | `Std.Test` ([section 14.2](#142-stdtest)): Neura functions that build the message and call `Failure.trap` | F1 |
| The runner prints the recorded assertion message | The runner prints the trap's message | F1 |


Migration proceeds in source order:

1. Supply `Std.Program`, `Std.Io.Console.print_line`, `Failure.trap`, and the typed assertions in `Std.Test`. Use `trap_with_message` for the library failure path.
2. Migrate examples and tests to the new imports. Compare exit status, text, and failure diagnostics.
3. Retire built-in name resolution and the legacy assertion host services once no shipped source depends on them. Keep the console provider for the standard module's declared import. Document what earlier executable images need from the runner.

The compiler accepts the old and new entry routes during this migration. A program with both is rejected.

### 4.7 Versioning

The library has one version, equal to the compiler's. `Std.Core.Version` exports `Major`, `Minor`, and `Patch`. The text produced by `to_string` functions, the hash algorithms, the iteration orders, the trap messages, and the float formatting rules on this page are part of the contract and change only with a major version.

## 5. Core contracts

### 5.1 Failure model

There are two kinds of failure and they never mix.

**Returned errors.** A function whose failure depends on input text, input bytes, or the outside world returns `Result<T>` ([section 6.2](#62-resultt)). The caller reads it with `match`. There is no propagation shorthand and no exception; passing an error up is an explicit `match` arm that returns it.

**Traps.** A function whose precondition is violated ends the run. The run stops at once with process status 70; no library or user code runs afterwards; nothing can observe or recover it ([Errors](Errors.md)). The module `Failure` in `Std.Core` has one function:

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

**Decision: the library does not re-check what the language already checks.** An index outside a built-in list, an overflow, a zero divisor, a shift count out of range, and an out-of-range conversion trap in the language with their own reasons (1 and 2) and locations. A library member that would fail the same way lets the built-in operation trap and documents that it does. `ArrayList.get` with a bad index is a bounds trap from the list read, not a library trap.

**What the absence of unwinding changes.** The first plan assumed panics that unwind, deferred actions, and recovery. None exists, and this plan asks for none.

| First plan's promise | Now |
| --- | --- |
| `Closeables.use` closes the resource even when the body panics | Removed. A body cannot fail and return; it either returns or the run ends. Callers call `close` on each return path. When a run ends, by return or by trap, the host releases every file and socket. |
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
| `string` | Same bytes | Start with 0; for each byte, 31 times the hash plus the byte, wrapping | The string itself |
| Tuple | Same element types and each pair of elements equal, in order | Start with 1; for each element, 31 times the hash plus the element's hash, wrapping | `[`, the elements' text separated by `, `, `]` |
| Array, list | Same kind, same length, and each pair of elements equal in index order | As for a tuple | As for a tuple |
| Class (default, G3b) | Identity | The identity number times 2,654,435,761, wrapping | The qualified class name, `#`, and the identity number, for example `Acme.Shapes.Circle#3` |
| Function value | Not comparable | None | None |
| Payload enum | Not provided | Not provided | Not provided |

Float equality here deliberately differs from `==`: it is an equivalence relation consistent with the hash, which hash tables need, while `==` stays IEEE 754. The multiplier 31 is chosen because multiplying by 31 is a shift and a subtraction.

The module **`Hash`** has the building blocks, all ordinary functions that work today:

| Member | Behavior |
| --- | --- |
| `combine(seed: uint32, value: uint32) → uint32` | 31 times `seed` plus `value`, wrapping. Computed in `uint64`: the product is below 2^37, so nothing overflows before the mask. |
| `spread(hash: uint32) → uint32` | `hash` exclusive-or `hash` shifted right by 16. Hash tables apply this before masking. |
| `of_bool`, `of_int64`, `of_uint64` | The table's results for those types. |

A class's hash starts from 17 and combines each field's hash in declaration order.

**The contract for a class that overrides the G3b members:** `equals` is an equivalence relation; equal values have equal hashes; neither traps nor modifies the value; and while an object is a key in a hash collection its hash and equality do not change. The library does not detect violations.

**Self-containing collections.** A collection's text prints `(this collection)` for itself as a direct element. Deeper cycles end with the frame-limit fault.

### 5.3 Ordering

- **`Ordering`** is a fieldless enum with variants `Less`, `Equal`, and `Greater`. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would trap on overflow) cannot be written. The module `Ordering` also exports `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`. All of this works today.
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
| For-each over the built-in list from `ArrayList.items()`, or from any collection's `to_list()` | Today | The built-in loop compares the list's structural version before each advance and traps with reason 5 on a change. `items()` is live; `to_list()` is a copy. |
| An index loop over `size()` and `get(index)` | Today | For lists and deques. No modification check. |
| The collection's iterator object: `iterator()` returns a class with `has_next() → bool` and `next() → T` | Today, as one concrete iterator class per collection | `has_next` has no effect and can be repeated. `next` returns the next element and advances, and traps with `no such element` at the end. An iterator is used once. |
| For-each directly over the collection | G4 and G5 | Lowers to the iterator protocol when the source implements `Iterable<T>`. |

- **Decision: iteration does not allocate per element.** `has_next` and `next` stay two calls. A single `next` returning `Optional<T>` would read well with `match`, but it would allocate an enum value for every element.
- **Map iteration yields tuples.** A map's iterator returns `[K, V]`. The tuple is an immutable snapshot of the entry, which is the contract the first plan's `Entry` class existed to state.
- **Decision: no removal through iterators.** Collections remove in bulk with `remove_if` and `retain_if`.
- **Decision: fail-fast iterator objects.** A wrapper over a single built-in list uses that list's `uint64` structural version. Other mutable collections use a `uint64` modification count and increment it after each structural change. An iterator records its version when created. `has_next` and `next` compare first and trap on a difference. Replacing an element by index or replacing the value for an existing key is not structural.
- The iteration order of every collection is specified in section 9.
- `Iterator<T>` and `Iterable<T>` are the interfaces for this protocol. Concrete iterator classes and collections implement these member signatures.

### 5.5 Resources

- A host resource (`File`, `TcpStream`, `TcpListener`) has `close() → Result<Unit>`. Closing again does nothing and returns `Ok`. Any other operation on a closed resource returns an `Io` error of kind `Closed`. If `close` reports an error the resource is still closed.
- **Closing is always explicit.** Reclaiming an unreachable resource object frees its storage and does nothing else: `deinit` does not run and the host ID stays open until the run ends. No library class declares `deinit`. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns an `Io` error of kind `Other`.
- There is no scoped helper ([section 5.1](#51-failure-model)).
- Where a wrapper owns a closeable resource, it can use the `Closeable` interface. A wrapper over a bare function value does not own or close the function's receiver.
- **Frame slots keep storage alive.** A reference in a local stays reachable until the local is overwritten or the function returns ([NVM](../runtime/NVM.md#garbage-collection)). The rule for library code: an operation that replaces a large structure (a hash rebuild, a sort buffer, a deque rebalance) does the work in a helper function and returns the new structure, so the old one's last frame reference dies when the helper returns and not when the caller's loop ends.

### 5.6 Callbacks

- A callback parameter is written as a native function type. The library declares no named function shapes: a type overlay can be neither generic nor a function type, and the native spelling says everything a name would.
- **Concrete callbacks work today.** `Logger` takes a sink of type `(string) → void`, and any module can use function values whose types name no type parameter.
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
| `Optional<T>`, `Result<T>`, `Error`, `JsonValue` | Module functions; payload enums have no built-in equality | No | No | |
| `ArrayList`, `LinkedList` | As lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `BitSet` | As sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `OrdinalMap` | As maps | No | Yes, of `[K, V]` | `MutableMap`; `TreeMap` also `SortedMap` |
| `BigInteger`, `BigDecimal`, `Duration`, `Instant`, the calendar types, `Path`, `Uuid` | Yes, by value | `compare_to` member | No | `Comparable` |
| `Sequence`, `StringBuilder`, `Regex`, `Random`, `Logger`, streams, files, sockets | Identity | No | `Sequence` once | Streams: [section 11.1](#111-stdio) |

## 6. `Std.Core` types

### 6.1 `Optional<T>`

**Recommendation: a generic payload enum with the variants `None` and `Some(T)`, read with `match`.** The first plan's class (a `present` flag and a late field) is dropped. The enum cannot be read without handling both cases, has no trapping accessor to misuse, and needs no late field.

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

**Recommendation: a generic payload enum with the variants `Ok(T)` and `Err(Error)`.** It is the one representation of a fallible outcome. An operation with no value returns `Result<Unit>`. The error type is fixed to `Error` so that every fallible function composes with every other.

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

**Decision: `Error` is a payload enum.** Errors are ordinary values read with `match`. A custom error can hold an `ErrorDetail` reference, which is required to be non-nil.

| Variant | Fields | Meaning |
| --- | --- | --- |
| `Io` | `kind: IoErrorKind`, `message: string`, `path: string` | A host operation failed. `path` is empty when no path applies. |
| `Parse` | `kind: ParseErrorKind`, `offset: int`, `message: string` | Text does not match the expected grammar. `offset` is the byte offset of the first offending byte, or the input's length for an unexpected end. |
| `Encoding` | `offset: int`, `message: string` | Bytes are not valid in the expected encoding. |
| `Custom` | One `ErrorDetail` | An error defined outside the library. |
| `Context` | `message: string`, `cause: Error` | Another error with a description of what was being attempted. |

- `IoErrorKind` is a fieldless enum, in order: `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidInput`, `Interrupted`, `UnexpectedEnd`, `NotADirectory`, `IsADirectory`, `DirectoryNotEmpty`, `ConnectionRefused`, `ConnectionReset`, `TimedOut`, `Unsupported`, `CapabilityDenied`, `Closed`, `InvalidData`, `Other`. The first fourteen map from host error codes 1 through 14. `Closed` and `InvalidData` are produced by the library.
- `ParseErrorKind`, in order: `Empty`, `InvalidCharacter`, `OutOfRange`, `UnexpectedEnd`, `Syntax`.
- **`ErrorDetail`** is an abstract class with two requirements, `code() → string` (a stable identifier such as `billing.card_declined`) and `message() → string`. User code defines its own error types by deriving from it; dynamic dispatch on a non-generic class works today. It is the library's one class meant to be a base. `SimpleDetail` is the concrete subclass that stores both strings.
- Adding a variant to `Error` breaks every `match` without a wildcard arm, so the variant list changes only with a major version. Code that only reports errors should end its `match` with a wildcard arm that calls `Errors.message`.
- Errors are immutable. A cause is set at construction, so chains are finite and acyclic.

The module `Error` exports the enum, the two kind enums, `ErrorDetail`, `SimpleDetail`, and:

| Function | Behavior |
| --- | --- |
| `custom(code: string, message: string) → Error` | A `Custom` error holding a `SimpleDetail`. |
| `context(cause: Error, message: string) → Error` | A `Context` error. |
| `message(e: Error) → string` | `Io`: the kind's name, `: `, the message, and ` (` path `)` when the path is not empty. `Parse`: `parse error at ` offset `: ` message. `Encoding`: `invalid encoding at ` offset `: ` message. `Custom`: the detail's message. `Context`: the message, `: `, and the cause's message. |
| `root_cause(e: Error) → Error` | Follows `Context` causes to the end. |
| `chain(e: Error)` | A built-in list of `Error`: the error, then each cause. |
| `io_kind_name(kind) → string`, `parse_kind_name(kind) → string` | The variant's name, written as a `match`. |

`message` uses only concatenation and integer text, so it works before T1.


### 6.4 Concrete aliases and absence

Define `Optional<T>` and `Result<T>` directly as generic payload enums. A concrete public name such as `File.Result` can be a type overlay of `Result<File>`. Reuse the same `Ok` and `Err` variants and payload types through the overlay.

Use `nil` for an internal absent reference when it cannot be confused with a real value, such as a missing parent link or an unused iterator reference. Do not replace `Optional<T>` with a nil convention on a generic return, or on an API where a present nil reference must be distinguishable from absence. JSON `Null` remains its own payload enum variant.

### 6.5 Small types

| Type | Definition | Status |
| --- | --- | --- |
| `Unit` | A fieldless enum with the single variant `Value`. | Today |
| `Cell<T>` | A class with one mutable field `value`, and the module function `of<T>(value)`. It exists so that closures, which capture by value, can share mutable state. | Today |
| `IntRange` | An immutable class: `start` (inclusive), `end` (exclusive), `step` (nonzero). Module functions `of(start, end)` and `stepped(start, end, step)`; a zero step traps. A negative step counts down while the value is greater than `end`. Members `size()`, `is_empty()`, `contains(value)`, `get(index)`, and `to_list()`. `size` is computed in `int64` so that no intermediate overflows. | Today |

Removed from the first plan:

- **`Pair<A, B>`** is the native tuple `[A, B]`. `partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`.
- **`Entry<K, V>`** is the tuple `[K, V]`. Its contract was "an immutable snapshot of one association", which a tuple already is. Its text (`key: value`) is produced by the map's `to_string`.
- **`Boxed<T>`** existed to pass non-class values as `Object`. No API on this page takes an `Object`.

Named types are kept where the name carries a contract a tuple cannot: `RegexMatch` (group lookup by name), `FileMetadata` (three fields of unrelated types that will grow), and the value classes of `Std.Time`.

## 7. Primitive companion modules

`Std.Core` has one module per primitive type: `Int8`, `Int16`, `Int32`, `Int64`, `UInt8`, `UInt16`, `UInt32`, `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, and `Bool`. There is no overloading, so an operation is spelled with its type's module. The integer modules have identical member lists; so do the float modules. Each is a separate hand-written source file.

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
| `to_string_radix(v, radix: int) → string` | Lowercase digits. |
| `format(v, radix: int, min_width: int, pad: string) → string` | As `to_string_radix`, then left-padded with copies of `pad` to `min_width` digits and sign. When `pad` is `0`, padding goes after the sign. |
| `parse(text) → Result<X>`, `parse_radix(text, radix: int) → Result<X>` | **T1.** Grammar below. |

**Parsing grammar.** An optional `+` or `-`, then one or more digits of the radix, letters in either case. No whitespace, separators, or radix prefix. An empty input fails with kind `Empty`; a sign with no digits with `UnexpectedEnd`; any other byte with `InvalidCharacter` at its offset; a value outside the type with `OutOfRange` at offset 0. A `-` on an unsigned type is accepted only for zero. The parser accumulates in the negative range for signed types, so `Min` parses without overflow.

### 7.2 Arithmetic without wrapping intrinsics

The first plan asked for wrapping arithmetic and sign reinterpretation as intrinsics. The language's checked operators, shifts that discard high bits, and masks are enough. Each recipe below says why no intermediate value leaves its type. The `uint32` and `uint64` multiplication recipes were probed in both build modes.

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

**What the recipes do not cover.** A float's bit pattern (N1). Everything else that the first plan routed through intrinsics is on this table: hashing uses the 32-bit rows, `Random` and `Sha256` use the `uint64` and `uint32` rows, `BitSet` uses plain masks and shifts on `uint32`, and `BigInteger` uses the limb row.

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

### 7.4 `Bool`

`to_int(v) → int` gives 1 or 0; `xor(a, b)`; `compare(a, b) → Ordering`; `hash(v)`; `to_string(v)`. `parse(text) → Result<bool>` accepts exactly `true` and `false` and needs T1.

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

All of `Strings` needs T1. String operations are functions that take the string first. **Decision:** all positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `scalar_count` say which unit they count. An offset that is out of range, or not on a character boundary where one is required, traps.

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

**Search algorithm.** `index_of` scans for the needle's first byte and then compares the remaining bytes in place. Its worst case is the product of the two lengths. The simple scan has the smallest circuit and a predictable step count.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator is Neura code in `Std.Text` and must agree with the validation NVM performs when constructing text.

**Unicode scope. Decision:** the first version handles encoding, decoding, and iteration for all of Unicode, and classifies and case-maps ASCII only. Member names say `ascii` so that a later `Std.Text.Unicode`, pinned to one Unicode version, can be added without changing existing behavior.

### 8.2 `StringBuilder`

A class in `Std.Core` that accumulates text. Needs T1. The module function `create()` makes a builder: a method cannot name its own receiver, so the builder keeps a reference to itself that the factory sets, and `new StringBuilder()` is not available outside the module. Every `append` member returns that same builder.

- **Layout.** One built-in `uint8` list, always valid UTF-8 because every append adds whole characters.

| Member | Behavior |
| --- | --- |
| `append(s: string) → StringBuilder` | Returns the builder for chaining. |
| `append_scalar(scalar: uint32)`, `append_line(s)`, `append_repeated(s, count: int)` | `append_line` appends `s` and one LF. |
| `append_int(v: int64)`, `append_uint(v: uint64)`, `append_float(v: float64)`, `append_bool(v: bool)` | Append the value's text without building an intermediate string. |
| `append_value<T>(value: T)` | Appends `value.to_string()`. **G3.** |
| `byte_length() → int`, `is_empty() → bool` | |
| `truncate(byte_length: int)`, `clear()` | `truncate` requires a character boundary. |
| `to_string() → string` | Constructs a string from the bytes. The builder stays usable. |

### 8.3 `Scalars`

**Decision: there is no `char` type.** A code point is a `uint32` holding a Unicode scalar value. The first plan made `char` a language blocker so that a character would print as itself; `StringBuilder.append_scalar` and `Strings.from_scalars` do that explicitly, and the library needs no new primitive type or literal form.

`Scalars` is a module of functions on `uint32` that work today: `is_valid` (at most 0x10FFFF and not a surrogate), `utf8_length` (1 through 4), `is_ascii`, `is_ascii_digit`, `is_ascii_hex_digit`, `is_ascii_alphabetic`, `is_ascii_alphanumeric`, `is_ascii_uppercase`, `is_ascii_lowercase`, `is_ascii_whitespace`, `is_ascii_punctuation`, `is_ascii_control`, `to_ascii_upper`, `to_ascii_lower`, `digit_value(scalar, radix: int) → int` (-1 when it is not a digit of the radix), and `from_digit(value: int, radix: int) → uint32` (lowercase; traps when out of range). Each is a range comparison; none uses a table. The constant `Replacement` is 0xFFFD.

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
| `group_count() → int`, `pattern() → string` | |

`RegexMatch` is an immutable class: `start()`, `end()`, `text()`, `group(index) → Optional<string>`, `group_start(index) → int` (-1 when the group did not participate), and `named_group(name) → Optional<string>`. Group 0 is the whole match.

## 9. `Std.Collections`

### 9.1 Contracts

This section fixes the member sets that the concrete classes and the interfaces in [section 9.10](#910-interfaces-and-read-only-views) share.

The **Needs** column names the compiler facilities an implementation relies on.

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

**Decision:** `ArrayList` is a thin class over one built-in list. Each built-in list operation is a single NVM storage operation; a library-managed backing array would spend graph evaluations on capacity checks for every append and would need runtime-length arrays, which do not exist.

- **Layout.** One built-in list `storage`. Its `version` is the modification counter.
- **Module functions.** `create<T>()`, `of<T>(items: T[list])` (copies), `filled<T>(count, value)`, and `wrap<T>(items: T[list])` (adopts the list without copying).
- **`items() → T[list]`** returns the live built-in list. A built-in for-each over it traps with reason 5 on structural modification. For-each over the wrapper calls its iterator through `Iterable<T>`.
- **Aliasing.** `wrap` and `items` share the same backing list. The wrapper preserves the identity of that list for its lifetime. Its iterators compare the backing version, so modifications through a direct alias are detected as well.

| Operation | Cost | How |
| --- | --- | --- |
| `get`, `set`, `size`, `add`, `remove_last` | One storage operation | Built-in index, length, `append`, and `remove_at` of the last index. |
| `remove_at` | One storage operation | Built-in `remove_at`. |
| `add_at` | One operation per element after the index | At the end, append the new value directly. Otherwise append the last element again, shift the suffix right by one with indexed writes, and store the new value. A2 would make this one operation. |
| `truncate`, `clear` | One operation per removed element | Repeatedly remove the last index from the same backing list. A2 would shorten this to one in-place operation. |
| `remove_if` | Linear | Check the version after each callback, compact the kept elements by indexed writes, then remove the tail from the end. The backing version advances on each actual removal. |
| `reverse`, `fill`, `slice`, `to_list` | Linear | Element reads and writes. |

No removal leaves a stale reference: the built-in list drops the slot, and the probes showed the removed element is reclaimed.

### 9.4 `ArrayDeque<T>`

**Redesign.** The first plan used a ring buffer in an array with vacant slots. Without vacant slots a ring would keep a reference to every removed element until its slot was reused. The deque is instead two built-in lists used as stacks.

- **Layout.** `front` holds the first part of the deque in reverse order, so that its last element is the deque's first. `back` holds the rest in order. Plus the modification count.
- `add_first` appends to `front`; `add_last` appends to `back`. `remove_first` removes the last element of `front`; `remove_last` removes the last element of `back`. Each is one storage operation, and the removed slot is gone, so nothing is retained.
- **Rebalancing.** When `remove_first` finds `front` empty and `back` not empty, it moves the first half of `back` (rounded up) into `front` in reverse order and shifts the rest of `back` down, in a helper function. `remove_last` on an empty `back` mirrors it. A rebalance of `n` elements is followed by at least `n / 2` cheap removals from that end, so every end operation is amortized constant time. A3 would make the move two bulk operations.
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

**Decisions kept from the first plan:** hash collections iterate in **insertion order**, so output never depends on hash values, and the load factor is fixed at one half.

The index and entry tables are built-in lists grown by `append`. The shared probing core uses indices and hash values. Constructors can use specialized default value members or accept caller-supplied key operations.

#### Layout comparison

| Layout | Storage operations per successful lookup | Objects per entry | Removed entry released | Verdict |
| --- | --- | --- | --- | --- |
| Parallel lists: an integer index plus dense lists of hashes, keys, and values | Index read, hash read, key read, value read | None | At the next compaction, which is bounded (below) | **Chosen.** Probing touches only integers, insertion order is free, and nothing is allocated per entry. |
| One list of entry objects, with a shared empty object in removed slots | Index read, entry read, then a field read for each of hash, key, and value | One | Immediately | One more operation on every probe that reaches an entry, and an allocation per insertion. |
| Chained bucket nodes | A bucket read, then per node a field read for hash, key, and next | One | Immediately | Needs a second structure to keep insertion order. |

#### `HashCore`

`Std.Collections.Internal.HashCore` is a **non-generic** class. It knows hashes and entry numbers. It never sees a key. The owning collection keeps the keys and values in its own dense lists, indexed by entry number, and does the key comparison itself. One core therefore serves every hash collection, including the ones that can be written today.

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
| `IntMap<V>` | `int` | `Int32.hash` and `==`, called directly | Nothing: it can be written today |
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

Sorted collections ordered by a comparator given at construction. They need G1, because the comparator's type mentions `K`. **Decision:** a comparator is always required; for a built-in type pass its `compare` function. Two keys are the same key when the comparator returns `Equal`; `equals` is not consulted.

**Decision kept:** the balanced tree is an **AVL tree**. Lookups dominate, each comparison is a call through a function value, and an AVL tree is at most about 1.44 log2 of the size tall against about 2 log2 for a red-black tree.

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

Both can be written today.

- **`BitSet`** is a set of non-negative `int` values following the set contract. Layout: a built-in `uint32` list of words (bit `i` is in word `i / 32` at position `i % 32`) and a cached `count`. A negative element traps. Setting a bit beyond the last word appends zero words. Besides the contract it has `flip(i)`, the in-place `union_with`, `intersect_with`, `subtract`, and `symmetric_difference_with`, and `next_set(from) → int` (the next member at or after `from`, or -1). Every operation is a mask, a shift by 0 through 31, or a comparison on `uint32`; nothing wraps. Iteration is ascending. Equality is set equality, so trailing zero words do not matter.
- **`OrdinalMap<V>`** is a map from the integers 0 through `n - 1` to values, with `n` fixed at construction. Layout: an `int` list `position` of length `n` (the key's place in the dense lists, or -1), a dense `int` list of keys, and a dense list of values. `set` appends to the dense lists; `delete` moves the last dense entry into the removed place and removes the last slot, so nothing is retained. Every operation is constant time. Iteration is in key order, by scanning `position`.

**Enum keys.** The first plan had `EnumSet<E>` and `EnumMap<E, V>` built on intrinsics for an enum's variant count and for constructing a variant from an ordinal. Neither intrinsic is needed. A fieldless enum already converts to its ordinal with `as`, so a set of variants is a `BitSet` of ordinals and a map keyed by variants is an `OrdinalMap<V>` sized by the caller with the number of variants. The caller converts at the call. Going back from an ordinal to a variant is a `match`-style function the enum's author writes. This applies to fieldless enums only: a payload enum has no ordinal conversion, and nothing here constructs one.

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

- **Decision kept:** read-only and mutable interfaces are separate. A function that only reads takes the read-only interface, and no member simulates immutability by trapping.
- Members that need only other members (`is_empty`, `first`, `contains_all`, `for_each`) become default methods written once in the interface.
- `ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are classes that copy their contents at construction and implement only the read-only interfaces. Their module functions `of` take a built-in list (for the map, a list of `[K, V]`; a duplicate key traps).
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

**`binary_search<T>(items: T[list], value: T, c) → int`** requires `items` sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index, the first of any equal run. Otherwise it returns `-(insertion point) - 1`.

The module **`Lists`** has helpers for built-in lists that need no callback and work today: `copy<T>(items)`, `concat<T>(a, b)`, `reverse<T>(items)`, `swap<T>(items, i, j)`, `fill<T>(items, value)`, `insert_at<T>(items, index, value)`, `truncate<T>(items, length)`, `clear<T>(items)`, `filled<T>(count, value)`, and `from_array<T>(items: T[])`. There is no conversion from a list to an array, because an array's length must come from a literal.

### 9.12 `Sequence<T>`

A class for lazy pipelines, the counterpart of Java's streams. Needs G1; the members that return `Optional` also need G2.

**Redesign.** The first plan wrapped an `Iterator<T>` interface and gave each operation an adapter class implementing it. A sequence is now two native function values, and each operation is a closure over the previous two.

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

**Decision:** these functions are a port of fdlibm 5.3, operation for operation, including its argument reduction, its polynomial coefficients (written as exact bit patterns through `Float64.from_bits`), and its special cases. fdlibm relies only on IEEE 754 double arithmetic with round-to-nearest and no fused or reassociated operations, which is what [Numbers](Numbers.md) guarantees, and on reading and writing the high and low 32-bit words of a double, which is N1. The functions are therefore defined by their source: every runtime produces bit-identical results. Every NaN result is the canonical NaN. fdlibm's copyright notice must be kept in the ported files, following the third-party rule in [CONTRIBUTING](../../CONTRIBUTING.md).

### 10.2 `BigInteger`

An immutable arbitrary-precision integer. Parsing uses T1 and conversion to `float64` uses N1.

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

Bitwise and, or, and exclusive or on big integers are not in the first version.

### 10.3 `BigDecimal`

An immutable decimal number stores an unscaled `BigInteger` and an `int` `scale`, representing unscaled × 10^(-scale). Parsing uses T1 and float conversion uses N1.

- **Decision:** `equals` and `compare_to` are numeric, so `2.0` equals `2.00`. `hash_code` is computed on the form with trailing zeros stripped.
- `add` and `subtract` give the larger scale; `multiply` gives the sum of the scales; all are exact.
- **Decision:** division always takes an explicit result scale and `RoundingMode`: `divide(other, scale, mode)`. There is no exact division that could fail on a non-terminating quotient.
- `RoundingMode` is a fieldless enum, in order: `Up`, `Down`, `Ceiling`, `Floor`, `HalfUp`, `HalfDown`, `HalfEven`, with Java's meanings.
- Other members: `set_scale(scale, mode)`, `round(precision, mode)`, `strip_trailing_zeros`, `negate`, `abs`, `sign`, `pow(exponent: int)`, `to_big_integer(mode)`, `to_float64()`, and the module functions `of_int64`, `of_big_integer(value, scale)`, `of_float64(v)` (the exact binary value; NaN and infinities trap), and `parse(text) → Result<BigDecimal>`.
- `to_string` always uses plain notation with exactly `scale` fraction digits when the scale is positive, and appends zeros for a negative scale (zero with a negative scale prints as `0`).
- A scale that leaves the `int` range traps (reason 1).

## 11. Input and output

### 11.1 `Std.Io`

**`Console`** is the module that replaces `SysIo.println`. It needs P2.

| Member | Behavior | Also needs |
| --- | --- | --- |
| `print_line(s: string)` | Writes the text and one LF through `neura.console.write_line`. A failed write traps with `console write failed`, as `SysIo.println` does today. | F1 for the message |
| `try_print_line(s: string) → Result<Unit>` | The same write, returning the failure. | |
| `print(s)`, `error_line(s)`, and their `try_` forms | Through `neura.console.write`, to output or to the error stream. | T1 (text to bytes) |
| `read_line() → Result<Optional<string>>` | A line without its terminator; `None` at the end of input. | T1 |
| `output() → ByteSink`, `error() → ByteSink`, `input() → ByteSource` | The console as streams. | |

**Decision:** console output is unbuffered in the library. Each write is one host request, so output order is program order and nothing is lost when a run ends by trap.

**Streams are function values.** The first plan declared interfaces (`ByteReader`, `ByteWriter`, `TextReader`, `TextWriter`) so that a buffer could wrap any reader. Each of those is one operation with a concrete signature, which is what a native function value is, and a bound method supplies one from any object. No interface is needed and nothing here waits for G4.

| Shape | Type | Contract |
| --- | --- | --- |
| `ByteSource` | `(int) → Result<uint8[list]>` | Called with a maximum count of at least 1. Returns a new list of 1 through that many bytes, or an empty list at the end of the stream. |
| `ByteSink` | `(uint8[list]) → Result<Unit>` | Writes every byte, repeating partial host writes. Does not keep the list. |
| `TextSink` | `(string) → Result<Unit>` | Writes the string. |

The three names are this page's shorthand. A type overlay cannot name a function type today (probed), so library source spells each type out. Reading the method `read` of a `File` as a value binds the file and gives a `ByteSource`; a closure over anything else gives one too. The probe passed a bound method of a class to a function expecting a byte source and drained it, in both build modes. Closing is not part of a stream: the code that opened a resource closes it.

| Class | Behavior | Needs |
| --- | --- | --- |
| `ByteBuffer` | An in-memory accumulator over a built-in `uint8` list. `write` (a `ByteSink` when bound), `to_list()`, `size()`, `clear()`, and `reader()`, a `ByteSource` over a copy. | Today |
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

**`File`** is a class wrapping a host file ID. The module function `open(path, mode: OpenMode) → Result<File>`. `OpenMode` is a fieldless enum: `Read` (must exist), `Write` (create or truncate), `Append`, `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). Members: `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `seek(offset: int64, origin: SeekOrigin) → Result<int64>` (`SeekOrigin`: `Start`, `Current`, `End`), `size() → Result<int64>`, and `close() → Result<Unit>` ([section 5.5](#55-resources)). A `File` is unbuffered.

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

### 11.3 `Std.Program`

This module replaces `SysIo.Entry` ([section 4.6](#46-retiring-sysio-and-test)). It needs P3.

- **`Entry`** is a module with one requirement, `main(args: string[]) → ExitCode`. A program's entry module inherits it and overrides `main`, exactly as programs inherit `SysIo.Entry` today. Program arguments arrive only through this parameter.
- **`ExitCode`** is a payload enum: `Ok`, `Failure`, and `Status(int)`. They map to process statuses 0, 1, and the given value. A `Status` outside 0 through 255 traps with `invalid argument: exit status`. The first plan kept the old one-variant enum; a payload enum lets a program return any status without a host call.
- **`Environment.get(name: string) → Lookup`**, where `Lookup` is a payload enum: `Found(string)`, `Missing`, and `Failed(Error)`. A purpose-built enum reads better here than a `Result` of an `Optional`. Needs P2.
- **`Process.exit(status: int)`** ends the run with that status through `neura.process.exit` and does not return. A status outside 0 through 255 traps. Needs P2. Returning from `main` is preferred; `exit` is for leaving from deep inside a program.

### 11.4 `Std.Net`

Blocking TCP only. Needs P2 and T1.

- `TcpStream`: the module function `connect(host: string, port: int, timeout: Duration) → Result<TcpStream>`. Members `read` (a `ByteSource` when bound), `write` (a `ByteSink` when bound), `set_read_timeout(timeout: Duration)` (zero for none; an expired timeout fails with kind `TimedOut`), and `close()`.
- `TcpListener`: the module function `bind(host: string, port: int) → Result<TcpListener>`. Members `accept() → Result<TcpStream>`, `local_port() → int`, and `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 traps.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

UDP, TLS, HTTP, and name-resolution APIs are not in the first version.

## 12. Time and randomness

### 12.1 `Std.Time`

The value types are immutable classes with `compare_to`, `equals`, `hash_code`, and `to_string`. They can be written today: their arithmetic is checked integer arithmetic and their text is digits and punctuation joined by concatenation. Each `parse` needs T1.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Module functions: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; the constant `Zero`; `parse(text) → Result<Duration>`. Overflow traps (reason 1), here and in every member below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- Module functions `of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`, and `parse`. Members `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `epoch_seconds()`, `nanos()`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface with `now() → Result<Instant>`, `monotonic() → Result<Duration>` (elapsed time from an arbitrary fixed origin, never decreasing), and `sleep(duration) → Result<Unit>`.

- `SystemClock` uses the clock host services (P2) and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock`, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, and `elapsed() → Result<Duration>`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate` stores `year` (-999,999 through 999,999), `month` (1 through 12), and `day`. The module function `of(year, month, day)` traps for an invalid date. `try_of(year, month, day) → LocalDate` returns `nil` for invalid components. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days. Shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle using floor division so that negative years work. Members include `plus_days`, `plus_months`, `plus_years`, `days_until(other) → int64`, `day_of_week() → DayOfWeek`, `day_of_year()`, `is_leap_year()`, and `length_of_month()`. Month and year changes clamp the day to the end of the target month. `DayOfWeek` runs from `Monday` through `Sunday`, with epoch day 0 on Thursday. Text uses `YYYY-MM-DD`. Years outside 0000 through 9999 have a sign and at least four digits.
- `LocalTime`: `hour`, `minute`, `second`, `nano`, stored as nanoseconds of the day. `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800. The constant `Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.

**Decision:** there is no time zone database, no named zones, and no daylight-saving rules in the first version. Conversions between instants and local times take an explicit `ZoneOffset`. The ISO forms above are the only text forms.

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
| `shuffle<T>(items: T[list])` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. Generic, with no callback, so it works today. |
| `choose<T>(items: T[list]) → T` | Traps with `no such element` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets, and the library offers no cryptographic generator.

**`Uuid`** is an immutable value of 16 bytes. The module function `random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `to_string` writes the 36-character hyphenated form in lowercase. `parse(text) → Result<Uuid>` accepts either case and needs T1. It has `compare_to`, `equals`, and `hash_code` over its bytes.

## 13. Data formats

### 13.1 `Std.Encoding`

Everything here needs T1 except the `Json` value type itself.

- **`Utf8`:** `encode(s)` (a built-in `uint8` list); `decode(bytes) → Result<string>`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1). `validate` works on bytes alone and can be written today.
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → Result<uint8[list]>` (either case; an odd length or a non-hex character fails with an `Encoding` error).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero.
- **`Csv`** (RFC 4180): `parse(text)` returns a `Result` of a built-in list of built-in lists of strings, and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with a `Parse` error. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

**Recommendation: `JsonValue` is a recursive payload enum.** The first plan had an interface, six implementing classes, a kind enum, and five accessors returning `Optional`. All of that is one declaration and `match`, and it compiles today: the probe declared this shape and walked it with mutually recursive functions in both build modes.

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
| `number_of_int64(v: int64) → JsonValue` | A `Number` with the integer's text. | Today |
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

Both can be written today: they work on built-in `uint8` lists with the 32-bit recipes of [section 7.2](#72-arithmetic-without-wrapping-intrinsics).

- **`Crc32`:** the IEEE 802.3 checksum (reflected polynomial 0xEDB88320, initial value and final exclusive-or of all ones). A 256-entry `uint32` list is computed on first use. `update(bytes)`, `value() → uint32`, `reset()`, and the module function `of(bytes) → uint32`.
- **`Sha256`:** FIPS 180-4. `update(bytes)`, `finish()` (a built-in list of 32 bytes; a later `update` or `finish` traps with `invalid state: digest finished`), and the module function `of(bytes)`.

These are for integrity checks and content addressing. The library makes no claim about timing behavior.

## 14. Diagnostics

### 14.1 `Std.Log`

`Logger` can be written today: its sink is a concrete function value and its text is built by concatenation.

- `LogLevel` is a fieldless enum, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`. Levels compare by ordinal.
- **The sink is a function value** of type `(string) → void`, called once per accepted record with the finished line, without a line terminator. `Console.print_line` is a sink. A closure that appends to a captured list is a sink for tests; because a captured list is shared, the test reads the list afterwards. A sink that can fail handles its own failure, typically by counting it in a captured `Cell`.
- Module functions `create(name: string, sink, level: LogLevel)` and `create_timed(name, sink, level, clock: Clock)`.
- Each record is one line: with a clock, the instant's text and a space; then the level in uppercase, a space, the name, `: `, and the message. If the clock fails, the timestamp is `-`.
- **Decision:** without a clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, make: () → string)` (the function runs only when the level is enabled), `is_enabled(level)`, and `set_level(level)`.
- **Decision:** there is no global logger registry. A logger is passed to the code that uses it.

### 14.2 `Std.Test`

This module replaces the built-in `Test` as described in [section 4.6](#46-retiring-sysio-and-test). An assertion builds its message in Neura and calls `Failure.trap`. The runner reports the message from the trapping test.

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
- A failed assertion ends its test, as today. Tests are discovered and run as [Testing](Testing.md) describes; only the import changes.

## 15. Scope and names

### 15.1 Deferred and excluded

| Area | Status | Reason |
| --- | --- | --- |
| Concurrency: threads, executors, futures, locks, atomics, concurrent collections | Deferred. `Std.Concurrent` is reserved. | The language defers concurrency ([Resources and concurrency](ResourcesConcurrency.md)). No library type is safe for concurrent use, and none claims to be. |
| Reflection, runtime class loading, serialization of arbitrary objects | Excluded | The library needs class and interface type tests, which are available without a general reflection model. |
| Finalizers, weak references | Excluded | Reclamation runs no code, by design. |
| Scoped cleanup, recovery from traps | Excluded | The runtime has neither, and the library is designed not to need them. |
| Time zones, locales, localized formatting, collation | Deferred | They need large versioned data tables. |
| Unicode normalization, non-ASCII case mapping and classification | Deferred to `Std.Text.Unicode` | Same reason. |
| Pattern formatting (`printf`-style) | Excluded | Needs variadic or heterogeneous arguments. `StringBuilder` covers the use. |
| Ciphers, signatures, TLS, secure random, HTTP | Deferred | They need review beyond this plan. |
| `BigInteger` bitwise operations, UDP | Deferred | Not needed for the first version. |

### 15.2 Name audit

The lexer's keyword list includes `let`, `const`, `export`, `override`, `func`, `module`, `class`, `import`, `new`, `destroy`, `if`, `else`, `while`, `for`, `in`, `break`, `continue`, `return`, `true`, `false`, `as`, `is`, `nil`, `enum`, `match`, `and`, `or`, `interface`, and `implements`. A keyword cannot be a member name, including after a dot.

| First plan | Problem | Now |
| --- | --- | --- |
| `Optional.or(supplier)` | `or` is a keyword | `Optional.or_else(o, other)` |
| `Optional.or_else(fallback)`, `or_else_get(supplier)` | Freed the name above and matched `Result` | `value_or`, `value_or_else` |
| `Optional.flat_map` | Named differently from `Result.and_then` for the same operation | `and_then` on both |
| `Optional.get()`, `Result.get()`, `Result.error()` | Trapping accessors on a sum type | Removed; `match`, or `expect` with a message |
| `Result.unpack()` | Returned a value and an error together | Removed |
| `Errors.of`, `Errors.wrap` | | `Error.custom`, `Error.context` |
| `Char`, `Chars`, `char_count`, `char_at`, `chars`, `append_char` | There is no `char` type | `Scalars`, `scalar_count`, `scalar_at`, `scalars`, `append_scalar` |
| `Map.get_or_panic`, `or_panic` | There are no panics | `require`, `expect` |
| `Closeables.use`, `Test.assert_panics` | Depended on unwinding | Removed |
| `Pair`, `Entry`, `Boxed`, `EnumSet`, `EnumMap`, the named function shapes | Replaced by tuples, `BitSet`, `OrdinalMap`, and native function types | Removed |
| `SysIo`, `Test` | Compiler built-ins | `Std.Io.Console`, `Std.Program`, `Std.Test` |

No other name collides. `RegexMatch`, `is_match`, and `matches_fully` contain `match` but are not the keyword. `Fs.list` is allowed: `list` is not a keyword (probed). Variant names such as `Error.Io` and `JsonValue.Null` are always qualified.

Contracts deliberately kept: insertion-ordered hash collections, separate read-only and mutable interfaces, `Ordering` instead of integers, byte offsets for string positions, a required comparator for sorted collections, numeric equality for `BigDecimal`, the AVL tree, the fixed half load factor, unbuffered console output, and the fdlibm port.


## 16. Roadmap

The library was implemented in these independent source tracks, on the compiler support of [section 3](#3-language-baseline-and-gaps). Every track is complete: its modules are installed, tested through the installed compiler and NVM, and listed in [Standard library](StandardLibrary.md).

| Track | Modules and acceptance criteria |
| --- | --- |
| Program, console, and testing | Implement `Std.Program`, `Std.Io.Console.print_line`, `Failure`, and `Std.Test` over the existing console provider and message trap. Migrate examples and verify both entry forms before removing the legacy built-ins. |
| Core values | Implement `Unit`, `Cell`, `Ordering`, `Hash`, `Error`, `Optional`, `Result`, and the integer and Boolean companion modules. Exercise matching, nil versus `Some(nil)`, and generic callback specializations. |
| Text and number conversion | Implement `Strings`, `StringBuilder`, `Scalars`, `Regex`, parsing and formatting companions, and `BigInteger` and `BigDecimal` conversions. Reuse T1 and N1, and validate all documented failure and boundary cases. |
| Collections and iteration | Implement list, deque, linked list, hash, ordinal, tree, priority, and sequence modules against their interfaces. Check deterministic order, key equality, list aliasing, nil links, and structural modifications. |
| Pure algorithms and data formats | Implement math, seeded randomness, calendar arithmetic, JSON and other encodings, and digests. Compare the outputs with independent reference models and published vectors where applicable. |
| Host providers and capabilities | Register concrete providers for console input, files, environment, process, time, entropy, and networking. Specify denial, lifetime, error codes, and replay before considering the source modules complete. |

Within a track, establish the public contract and failure behavior, then implement, test, and document the module. A source module is supported only when the complete path through the installed compiler and NVM is verified.

### 16.2 Dependencies by module

| Module or API | Source dependencies or remaining runtime work |
| --- | --- |
| `Optional<T>`, `Result<T>`, `Error`, `Ordering`, `Hash`, `Unit`, `Cell` | Generic enums, generic callbacks, and value members are available. Implement the source declarations and adapters. |
| Primitive companions, `Strings`, `StringBuilder`, `Regex`, `Scalars` | T1 and N1 are available. Implement parsing, Unicode scalar handling, formatting variants, and the documented algorithms. |
| List, deque, tree, hash, ordinal, priority, sequence, and immutable collections | G1–G5, nil references, and the built-in list version are available. Implement the source modules and model-based tests. |
| `Math`, `BigInteger`, `BigDecimal`, time value types, seeded `Random`, `Uuid`, encodings, and digests | Implement and verify the pure Neura algorithms. Host-sourced time and entropy require the named providers. |
| `Std.Program`, `Console.print_line`, and `Std.Test` | P1–P3 and F1 are available. Install source modules, run migration tests, and retire legacy names only after their replacements are usable. |
| Other `Console` methods, `File`, `Fs`, `Environment`, `Process`, `SystemClock`, entropy, `Std.Net` | P2 supplies the binding mechanism. The named providers, permission checks, and user-facing replay tools are separate runtime work. |

## 17. Verification

These requirements apply the repository's rules in [AGENTS.md](../../AGENTS.md) to the library.

- **Where tests live.** Library tests are Neura test files under `tests/stdlib/<module>/`, run by `neura test` through CMake with the library installed in the trusted source tree. Migrate the built-in `Test` imports once `Std.Test` is installed.
- **Trap tests are separate programs.** A member that traps is tested by a program under `tests/stdlib/<module>/traps/` with a `.out` file holding the expected diagnostic, run and compared by the harness that already runs `tests/backend/traps`. The comparison includes the trap reason and any message.
- **Coverage rule.** Every member in a table on this page has at least one test of its normal result, one test of each documented `Err` or `None`, and one trap test for each documented trap.
- **Both build modes.** Every library test and trap test runs optimized and with `--no-optimize`, and the outputs must be identical.
- **Reference evaluator.** The test programs also run under the IR reference evaluator, and the results must match NVM's.
- **Determinism.** Each test program runs twice from the same image; output and step counts must be identical.
- **Probes become tests.** Keep the compiler fixtures linked in [section 3.1](#31-what-the-probes-established) and add source-module tests as each track ships. Include negative cases for unsupported specialization, untrusted host declarations, and malformed artifacts where those boundaries are exercised.
- **Model-based tests.** For every collection, a test drives the collection and a trivially correct model (a plain built-in list, or an association list of tuples) with the same sequence of at least 10,000 operations drawn from `Random.seeded` with a fixed seed, and compares size, contents, iteration order, and return values after every operation.
- **Invariant validation.** `HashCore`, the AVL tree, `ArrayDeque`, `LinkedList`, and `PriorityQueue` each have an exported `validate()` member that traps on a broken structural invariant (index slots agree with entries, dead entries within the retention bound, AVL balance and parent links, endpoint and reciprocal links, heap order). Model tests call it after every operation.
- **Reclamation.** For each collection, a test adds and removes large elements in a loop under a heap limit that the loop would exceed if removed elements stayed reachable. For hash collections the limit allows for the documented retention bound and no more.
- **Fail-fast.** Tests mutate a collection during iteration through both a wrapper and any live backing alias. Check a remove-then-append sequence that restores the old length, indexed replacement that preserves the version, and `ArrayList.clear` and `truncate` through a wrapped list. A `to_list()` copy iterates independently.
- **Arithmetic recipes.** Each row of [section 7.2](#72-arithmetic-without-wrapping-intrinsics) is tested at its boundaries: zero, one, the largest value, the sign boundary, and operands whose exact result needs the full double width.
- **Contract text.** `to_string` output, hash values, trap messages, and float formatting are compared against literal expected values.
- **Numeric vectors.** `Math` is tested against bit patterns generated once from a reference fdlibm build and checked in. Float parsing and formatting use a checked-in corpus that includes halfway values, subnormals, the largest finite values, and powers of two and ten, plus exhaustive round trips for `float16` and `bfloat16`.
- **Host services.** Each service has a test that replays a recorded trace and a test with the capability denied.
- **Retirement.** The change that deletes `SysIo` and `Test` includes a rejection test for a program importing each, and every existing example and test passes through the new modules.
- **Decompiler.** A library-using image must round-trip through `disassemble` and `assemble` byte for byte, and `decompile` must run on it. No stronger recovery claim is made for library code.
- **Cost baseline.** A small benchmark set (insert and look up 1,000 map entries, sort 1,000 integers, format 100 integers, hash 10 KB with `Sha256`) records NVM steps, neuron count, and image size in a checked-in baseline. A change that raises any figure by more than 10% says why in its pull request. The layout choices of sections 9.3 through 9.7 are re-measured with this set when an acceleration ships.

A completion summary for any track names the stages touched (for a pure library change: none beyond the library source; for a gap: each stage), lists the commands that ran, and separates tested behavior from behavior that was only inspected.

## 18. Implementation decisions

The library follows this plan. Where the plan left a choice open, or where the language required a different shape, the implementation decided as below. Each decision is covered by the module's tests in `tests/stdlib/<area>/`.

### 18.1 Language and layout

- **Receivers.** A method cannot name its own receiver, so a member that returns "the same object" needs a stored self reference. `StringBuilder` keeps one, set by its factory `create()`. Classes whose invariants need a factory (`StringBuilder`, `Random`, `File`, `TcpStream`, `TcpListener`) take a module-private token or an internal handle in their constructors.
- **Constructor visibility.** An unexported `init` is private to its class, not to its module, so a module function cannot call it. `Regex` exports its constructor for that reason; an object built directly with it traps on use.
- **Module file names.** `Std.Core.UInt8` and `Std.Core.BFloat16` live in `u-int8.nra` and `b-float16.nra`, following the PascalCase to kebab-case mapping of [Project structure](ProjectStructure.md).
- **Optional and Result** import each other (`to_result` and `to_optional`); both contain only declarations. Files that import each other initialize as one cycle after the files the cycle imports ([Modules](Modules.md#initialization)).
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
- **IntRange** exposes `start()`, `end()`, and `step()` as methods; `size()` traps (reason 1) beyond `Int32.Max`.

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
- **Sha256** keeps rolled loops: unrolling cut steps by about four but grew the image by 1.5 to 3.6 MB.

### 18.8 Lists, deques, queues, and sorting

- **Modification counts.** `ArrayList` uses its backing list's `version`; `ArrayDeque`, `LinkedList`, `PriorityQueue`, `BitSet`, and `OrdinalMap` share a `Cell<uint64>` count with their iterators. A callback or comparator that changes the collection traps with `collection modified during iteration`, and `Sorting` checks the list's version after every comparator call, also when it sorts a built-in list directly.
- **Construction.** `new ArrayList<T>(items)` adopts the list, as `wrap` does. `PriorityQueue`'s constructor takes the items and the comparator and heapifies a copy. `ArrayDeque`, `LinkedList`, and `BitSet` also have `create` and `of`.
- **Self-containment.** Text prints `(this collection)` for a collection that directly contains itself, through a shared depth counter (`Std.Collections.Internal.TextDepth`); deeper cycles still end with the frame-limit fault.
- **LinkedList.** `sort` and `binary_search` work on a copy; `sort` and `reverse` move values between existing nodes, which is not a structural change; `clear` unlinks every node. Positional access outside the list traps with `invalid argument: index out of range`.
- **ArrayList.add_all** reads the argument up to its starting length, so `add_all(items())` doubles the list instead of trapping.
- **Equality direction.** Search members call `value.equals(element)` with the probe as the receiver.
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

- **fdlibm port.** `Math` is a port of fdlibm 5.3 written without a reference copy of the sources; Sun's notice is kept in `math.nra`, `internal/trig.nra`, and the test transliteration. The Neura functions agree bit for bit with a C transliteration of the same port on 9,148 vector cases (`tests/stdlib/math/vectors`), and that transliteration stays within 1 ulp of a 70-digit reference for the functions fdlibm documents as such, with the same larger errors as glibc for `sinh`, `cosh`, `tanh`, and `log10`. Agreement with a reference fdlibm 5.3 build is not yet established. Expressions fdlibm evaluates only to raise the inexact flag are omitted. `sqrt` is the correctly rounded integer method of [section 10.1](#101-math). `log2`, which fdlibm 5.3 lacks, follows `e_log10.c` and returns exact results for powers of two. `pow(±1, ±∞)` and `pow(1, NaN)` are NaN, as in fdlibm.
- **BigInteger** keeps its fields private (`signum`, `magnitude`); its constructor normalizes the value and adopts the list. `mod_inverse` traps for a modulus that is not positive. `floor_mod` takes the modulus's sign.
- **BigDecimal** adds `unscaled()` and `scale()`. `round(0, mode)` returns the value unchanged; a rounding carry drops one zero, as Java does. `of_float64` gives the exact value at the smallest scale. `-0.00` parses as zero. `parse` accepts an optional sign, digits with an optional point, and an optional `e` or `E` exponent; a scale outside `int` fails with `OutOfRange` at offset 0. Zero with a negative scale prints as `0`.
- **Trap details.** `negative exponent`, `negative shift count`, `negative bit index`, `modulus not positive`, `not a finite value`, `negative precision`, `magnitude too large`, and `zero sign with a nonzero magnitude` after `invalid argument: `.
- **Cost.** A float64 operation compiles to a large circuit, so a program that calls several `Math` functions has an image of several megabytes; `Math` is not in the cost baseline for that reason.

### 18.11 Verification as built

- **Layout.** Each area of `tests/stdlib` holds test files, `support/` modules, `traps/` programs with `// status:` and `// stderr:` headers, and `programs/` with expected output (`tests/stdlib/area.cmake`). Every area runs against a fresh installation (`stdlib-install`), its tests optimized twice and unoptimized, its programs and traps in both modes, and its program images through `disassemble`, `assemble`, and `decompile`.
- **Reference evaluator.** `backend-reference` runs every library test function under the NeuraIR reference evaluator and NVM and compares the outcome, the console output, and any trap message. Tests that need host responses (files, sockets, console input) or exceed the evaluator's budget are counted and not compared.
- **Host services** are tested with the capability denied, with real providers in temporary directories and on localhost, and by replaying checked-in traces.
- **Cost baseline.** `tests/benchmarks` holds the workloads of this section's cost baseline and a few more (ArrayList, TreeMap, strings, JSON, dates); `benchmarks` compares their steps, peak heap cells, neurons, and image bytes with `baseline.txt` and checks that steps grow as expected between two sizes.
- **Not established.** Agreement of `Math` with a reference fdlibm 5.3 build (no reference build was available); `with_capacity(2^29)` and other limits that need billions of elements were only inspected.
