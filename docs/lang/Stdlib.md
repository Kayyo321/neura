# Standard library implementation plan

This page is the implementation plan for the Neura standard library. It fixes the library's shape, its contracts, its algorithms, and the order of work, so that an implementer does not have to choose any of them. Nothing on this page is implemented. [Status](Status.md) records what exists today (`SysIo.println`, `SysIo.Entry`, and `Test`), and [Standard library and I/O](StandardLibrary.md) holds the earlier requirements list that this plan replaces once work starts.

## 1. How to read this plan

- **Syntax is not specified here.** Neura's surface syntax can change. This page describes behavior, data layout, and algorithms in prose and tables. It contains no Neura source.
- **Member notation.** Tables write a member as `name(parameter: Type, ...) → Result`. This is documentation notation, not Neura syntax. `→ T or Error` marks a fallible operation under the error model in [section 3.1](#31-assumed-error-model). "Panics with X" names the error value carried by a panic.
- **Names are binding.** Module, type, and member names on this page are the names to implement. They follow the [coding style](../style/CodingStyle.md): `PascalCase` modules, classes, interfaces, enums, variants, and constants; `snake_case` functions, methods, parameters, and fields.
- **"Decision"** marks a choice this plan makes where Java or another library offers several. Decisions are final for the first version. Changing one requires editing this page first.
- **Sizes and indices** are `int` (`int32`) everywhere, matching the built-in `length`. A collection holds at most 2,147,483,647 elements. Hash codes are `uint32`.
- **Complexities** are worst case unless a table says "amortized" or "expected".

## 2. Principles

These rules apply to every module below.

1. **Computation stays in circuits.** The library is written in Neura and compiles to neuron graphs like user code. The runtime contributes storage operations and host services only. No library function is implemented as a native routine that makes arithmetic, comparison, or control decisions. Equality, hashing, ordering, parsing, formatting, and mathematics are Neura code.
2. **A small intrinsic floor.** The library may call only the intrinsics in [section 4.4](#44-intrinsic-floor) and the host services in [section 4.5](#45-host-services). Everything else is built from them.
3. **Deterministic by construction.** For the same image, arguments, and host responses, every library operation produces the same result, the same iteration order, the same hash codes, and the same text. There is no per-run hash seed and no dependence on handle values or host integer widths.
4. **No null.** No library API returns or accepts an absent reference. Absence is `Optional<T>`. Internal linked structures use integer indices with -1 for "none". A search that returns a position (`index_of` and its relatives) returns -1 when nothing is found, so that a search allocates nothing.
5. **Programmer errors panic; environmental failures are values.** [Section 6.4](#64-panic-or-error) gives the rule and the full list.
6. **No overloading.** The language has no function overloading, so every member has a distinct name. Variants use suffixes (`add` and `add_at`, `index_of` and `index_of_from`).
7. **One constructor per class.** A class declares at most one `init`. Every alternative way to build a value is a class-level factory function ([L8](#l8-class-level-members-and-closed-classes)).
8. **No generic class inheritance.** Library classes are closed. Shared behavior comes from interface default methods and from internal helper classes used by composition. This matches the existing rule that a generic class cannot be a base class.
9. **No variance.** `List<Alert>` is not a `List<Renderable>`. APIs take exactly `Iterable<T>` where Java would take a wildcard.
10. **Pay only for what is reachable.** Only functions, classes, and constants reachable from the entry point are converted and emitted ([L11](#l11-library-packaging)).

## 3. Prerequisites

Work on the library cannot start until the hard blockers are done. Each blocker is a language or runtime change that must be designed on its own language page, implemented through the pipeline, and recorded in [Status](Status.md).

### 3.1 Assumed error model

The library assumes Go-style errors. These assumptions are inputs to this plan, to be specified in [Errors](Errors.md):

| ID | Assumption |
| --- | --- |
| E1 | Errors are ordinary runtime values. `Error` is an interface ([section 6.3](#63-error)), and every error is an object that implements it. |
| E2 | A fallible function returns its value and an error together. Exactly one is meaningful. The error position holds the distinguished "no error" value on success; that value exists only for the type `Error` and is not a general null. On failure the value position is vacant, and reading it panics with `StateError`. |
| E3 | `panic(error)` stops normal execution and unwinds frames. An unrecovered panic ends the run with status 70 and prints the error's `to_string()` and, when the image has a source map, the source location of the panic. |
| E4 | A deferred action runs when its function exits, by return or by panic, in reverse order of registration. A deferred action can recover a panic and obtain its `Error`. |
| E5 | The runtime failures that trap today become panics with library error classes: arithmetic failure (trap reason 1) panics with `ArithmeticError`, bounds failure (reason 2 and the NVM bounds fault) with `IndexError`, modification during for-each (reason 5) with `ConcurrentModificationError`, an uninitialized or vacant read with `StateError`, and a failed checked downcast with `CastError`. |
| E6 | Exhausted run limits (steps, frames, heap cells), loader faults, and NVM internal faults are not panics. They end the run immediately and cannot be recovered. |

If the final error design differs, `Result<T>` ([section 6.2](#62-resultt)) is the canonical carrier: every `→ T or Error` in this plan converts mechanically to `→ Result<T>`, and every `→ void or Error` to `→ Optional<Error>`. No other part of the plan depends on E2's return shape.

### 3.2 Hard blockers

#### L1. Interfaces

Add an `interface` declaration alongside `class`.

- An interface declares method requirements (no body) and default methods (with a body). It has no fields, no constructor, and no destructor.
- An interface can be generic, with the same type parameter rules as a generic class. A class implements a generic interface at specific type arguments, which may mention the class's own type parameters (`ArrayList<T>` implements `MutableList<T>`). This is the one permitted relationship between a generic class and a generic supertype.
- A class implements any number of interfaces in addition to its optional single base class. An interface extends any number of interfaces.
- A class is concrete only when every requirement of every implemented interface has an implementation, either its own, an inherited one, or a default.
- **Diamonds.** When two inherited interfaces supply a default for the same method, the implementing class or extending interface must override it, or the declaration is rejected. A default in a more derived interface replaces the default of an interface it extends without conflict. A class's own or inherited class method always wins over an interface default.
- When two interfaces declare a requirement with the same name, parameter types, and result type, they are one requirement, and one method satisfies both. The same name with a different signature is a compile-time error at the implementing declaration.
- A value of interface type refers to an object of some implementing class. Calls dispatch on the runtime class. An interface type can be a field, parameter, result, collection element, type argument, or bound.
- A generic bound can name an interface, including a generic one that mentions the bounded parameter (`T` bounded by `Comparable<T>`).
- An interface method cannot have its own type parameters, because a generic method cannot be overridden. This plan never relies on generic interface methods.
- **Lowering.** Interface dispatch uses the existing mechanism for class dispatch: read the type tag, then select among the instantiated classes that implement the interface and can reach the call site. A call whose receiver has a concrete static class type is a direct call. No new runtime structure is needed.

#### L2. Universal members and the root type

- Every class without a declared base derives from the prelude class `Object`. `Object` is also the supertype of every interface type and every function value.
- `Object` declares three overridable methods: `to_string() → string`, `hash_code() → uint32`, and `equals(other: Object) → bool`. [Section 5.1](#51-universal-members) fixes their defaults.
- The same three members are callable on a value of **every** type: primitives, `char`, `string`, enums, arrays, lists, and unconstrained type parameters. For non-class types the compiler supplies the built-in implementations of section 5.1 and resolves the call statically. Inside a generic body, a call on a value of type `T` resolves when the body is specialized. No boxing occurs.
- Strings, primitives, enums, arrays, and lists are not `Object` values and cannot be assigned to `Object`. `Boxed<T>` ([section 6.5](#65-small-value-types)) carries them where an `Object` is required.
- `equals` on a non-class type takes a value of the same type.
- **Operator `==`.** On class, interface, and function values, arrays, and lists, `==` and `!=` compare identity: both operands refer to the same object. On `string`, they compare content, byte for byte. On primitives and enums they keep today's meaning. `equals` is the value-equality operation, and library code never uses `==` on a type parameter.
- **Identity number.** Each object can be asked for its identity number through an intrinsic. A run holds a counter that starts at 0. The first request for a given object increments the counter and stores the new value in the object; later requests return the stored value. The number therefore depends only on the order of first requests, not on handle values or on how many objects exist. The request is an ordered effect: the optimizer may not remove, duplicate, or reorder identity requests relative to each other.
- **Class names are execution data.** The default `to_string` prints the class's qualified name, and enum `to_string` prints the variant name. The compiler emits these names as text constants in the constant pool for every class and enum whose default `to_string` is reachable. They are required for execution and are separate from the optional name and source map sections. The decompiler reads them as constants, not as metadata.

#### L3. Function types and closures

- A function type is written from its parameter types and result type. Two function types are the same when those agree. It can be used wherever a type can.
- A function value is created from a named function, from a method bound to a receiver, or from an anonymous function expression (a closure).
- **Capture.** A closure can read bindings of enclosing functions. Captured values are copied into the closure when it is created. The closure body cannot assign to a captured binding. Shared mutable state goes through an object reference.
- A closure can capture the enclosing method's receiver.
- **Lowering.** Each function type is a compiler-generated interface with one method. Each closure expression is a compiler-generated closed class whose fields are the captured values and whose single method is the body. Calling a function value is an interface dispatch (L1). A function value is an `Object`: its `equals` and `hash_code` are identity-based and its `to_string` is the text `function`.
- [Section 5.6](#56-function-shapes) names the function shapes the library uses.

#### L4. Automatic memory reclamation

A Java-style library cannot be written with only manual `destroy`: an `Optional`, an iterator, or a string builder has no single owner to free it.

- NVM reclaims objects, arrays, lists, and texts that are unreachable from frames, globals, pending continuations, and the constant pool.
- **Determinism.** The NVM collects at deterministic allocation and growth safepoints. Before exceeding `--max-heap-cells`, it traces reachability and continues if the request fits after reclamation. Otherwise the run ends with the heap-limit fault. [Neura Virtual Machine](../runtime/NVM.md)
- Handles are never reused, as today.
- **No finalization.** Reclaiming an object does not run `deinit`. `deinit` runs only for an explicit `destroy`. The library never requires `destroy` and releases host resources through `Closeable` ([section 5.5](#55-closeable)).
- A reclaimed object's identity number is never reissued.

#### L5. Vacant storage and storage operations

Hash tables, ring buffers, and arenas need storage whose length is chosen at run time and whose slots can hold nothing.

- **Runtime-length arrays.** An intrinsic allocates an array of a given element type and a length known only at run time. Every slot starts vacant. A negative length panics with `ArgumentError`.
- **Filled arrays.** A second intrinsic allocates an array of a run-time length with every slot set to one given value.
- **Vacant slots.** A vacant array slot or field holds no value. Reading it panics with `StateError` (E5), exactly like today's uninitialized read. An intrinsic makes an array slot vacant again, so a removed element no longer keeps its referent alive.
- **Late fields.** A field can be declared late: it may be vacant after construction and can be made vacant again. Definite-initialization rules do not apply to late fields.
- **Bulk copy.** An intrinsic copies `count` consecutive slots from one array or list to another of the same element type, with source and destination start indices. Ranges are bounds checked before any slot changes. Overlapping ranges in the same collection behave as if the source range were read completely first. Copying a vacant slot makes the destination vacant.
- **List operations.** The built-in list gains: insert at an index (shifting later elements right; index equal to the length appends), truncate to a shorter length, and read of its structural version as `uint64`. Insert and a truncate that changes the length each advance the version by one.
- These are storage operations in the sense of [NVM](../runtime/NVM.md) and need an NVR minor revision. They move and clear values; they compute nothing.

#### L6. Bitwise operators and wrapping arithmetic

Hashing, random number generation, encoding, and big-number arithmetic need them.

- Bitwise and, or, exclusive or, and complement on every integer type.
- Left shift, and right shift that is arithmetic for signed types and logical for unsigned types. A shift count that is negative or at least the operand's width panics with `ArithmeticError`.
- Wrapping add, subtract, multiply, and negate for every integer type, as intrinsics separate from the checked operators. They return the mathematical result modulo 2^width.
- A reinterpreting conversion between a signed integer type and the unsigned type of the same width, and between each float type and the unsigned integer of the same width (bit pattern in, bit pattern out).
- Widening multiply for `uint32` (result `uint64`) is ordinary: convert both operands to `uint64` first.

#### L7. `char` and string semantics

- A built-in type `char` holds one Unicode scalar value: 0 through 0x10FFFF, excluding 0xD800 through 0xDFFF. It supports `==`, `!=`, and the four ordering comparisons by scalar value. `char as uint32` is exact. `uint32 as char` panics with `ArithmeticError` when the value is not a scalar value. The language provides a character literal form.
- `string` is an immutable sequence of bytes that is always valid UTF-8. Its length is at most 2,147,483,647 bytes.
- The language defines no ordering operators on `string`. Ordering is `compare_to` ([section 8.1](#81-strings)).
- String intrinsics: byte length; byte at a byte index (bounds checked); construction from a `uint8` array or list (validates UTF-8); and slicing by byte range, which copies. A slice is bounds checked, and both ends must fall on character boundaries.
- Concatenation with `+` keeps its present meaning.

#### L8. Class-level members and closed classes

- A class or interface can declare class-level functions and constants, which need no instance. A class-level function of a generic class can use the class's type parameters and is called with the class's type arguments.
- A class bound with `const` is closed: it cannot be a base class. Every concrete library class is closed unless this plan says otherwise.

#### L9. Type tests and checked downcasts

- A type test asks whether an `Object`, class, or interface value is an instance of a given class or interface.
- A checked downcast converts to that type and panics with `CastError` when the test would be false.
- Both use the type tag that dispatch already reads. The target type can mention type parameters of the enclosing generic declaration. The test is resolved after specialization, when every type argument is concrete, so `ArrayList<int32>` can test whether an `Object` is a `List<int32>`. A generic instance is an instance of an interface only at exactly the same type arguments.

#### L10. Enum intrinsics

For an enum type `E`: the number of variants, the variant for a given ordinal (panicking with `IndexError` when out of range), and an array of all variants in declaration order. `to_string` on an enum value returns the variant's name without the enum's name. A built-in bound `Enum` is satisfied by every enum type and by nothing else. Inside a generic body whose parameter has that bound, the enum intrinsics and the conversion to an ordinal are available.

#### L11. Library packaging

- The namespace `Std` is reserved. User projects cannot declare a module whose first component is `Std`.
- The library's source lives in the repository under `stdlib/`, laid out by the rules in [Project structure](ProjectStructure.md) (`stdlib/std/collections.nra` is `Std.Collections`). The compiler adds this root after every `-I` root. An installed compiler locates it relative to its executable.
- **Re-exports.** A module can export a class, interface, enum, function, or constant that it imported, including an uninstantiated generic. Each library module is a facade that re-exports declarations from child modules, so one import of `Std.Collections` provides every collection type while each type lives in its own file.
- **Prelude.** Every source file implicitly imports `Std.Core`, and its exports are usable without qualification. A declaration in the file with the same name shadows the prelude name.
- **Reachability.** The compiler converts and emits only declarations reachable from the entry point, from program initialization, or, under `neura test`, from the tests. An unreachable class contributes no layout, dispatch arm, or name constant.
- The library is compiled together with the program. There is no separately compiled library image.

#### L12. For-each over `Iterable`

`for`-each accepts any value whose static type implements `Iterable<T>`. The loop calls `iterator()` once, then repeats `has_next()` and `next()`. Arrays and lists keep their existing lowering.

#### L13. Intrinsic and host declarations

- A bodyless function in a `Std` module can be marked as an intrinsic. The compiler maps each to one NeuraIR operation. Only `Std` modules can declare intrinsics.
- A bodyless function in a `Std` module can be bound to a host service by name and version, with typed success and error results, generalizing how `SysIo.println` binds today. The image's import table lists exactly the reachable services.

### 3.3 Soft prerequisites

These make the library pleasant but do not block it. The API is designed to work without them.

| ID | Feature | Without it |
| --- | --- | --- |
| S1 | Inference of type arguments | Callers write type arguments at every generic call. |
| S2 | String interpolation, lowered to `StringBuilder` appends | Callers chain `StringBuilder.append`. |
| S3 | Index syntax on library types, lowered to `get` and `set` | Callers call `get`, `set`, and `put`. |
| S4 | Hexadecimal and binary integer literals | Constants are written in decimal. |
| S5 | A propagation shorthand for `T or Error` | Callers test the error and return it. |
| S6 | Multiple bounds on a type parameter | Not needed: universal members cover hashing and equality, and ordering takes a `Comparator`. |

## 4. Architecture

### 4.1 Modules

| Module | Contents | Section |
| --- | --- | --- |
| `Std.Core` (prelude) | `Object`, core interfaces (including the stream interfaces of section 11.1), `Optional`, `Result`, `Error` and the error classes, `Ordering`, `Pair`, `Unit`, `Boxed`, `Entry`, `IntRange`, `Hash`, `StringBuilder`, the primitive companion modules | [5](#5-core-contracts), [6](#6-stdcore-types), [7](#7-primitive-companion-modules) |
| `Std.Text` | `Strings`, `Chars`, `Regex`; re-exports `StringBuilder` | [8](#8-stdtext) |
| `Std.Collections` | Collection interfaces, implementations, `Sequence`, algorithms | [9](#9-stdcollections) |
| `Std.Math` | `Math`, `BigInteger`, `BigDecimal` | [10](#10-stdmath) |
| `Std.Io` | Byte and text streams, buffers, console | [11.1](#111-stdio) |
| `Std.Fs` | `Path`, files, directories | [11.2](#112-stdfs) |
| `Std.System` | Arguments, environment, exit | [11.3](#113-stdsystem) |
| `Std.Net` | TCP streams and listeners | [11.4](#114-stdnet) |
| `Std.Time` | `Duration`, `Instant`, clocks, calendar types | [12.1](#121-stdtime) |
| `Std.Random` | `Random`, `Uuid` | [12.2](#122-stdrandom) |
| `Std.Encoding` | `Utf8`, `Hex`, `Base64`, `Json`, `Csv` | [13.1](#131-stdencoding) |
| `Std.Digest` | `Crc32`, `Sha256` | [13.2](#132-stddigest) |
| `Std.Log` | `Logger` | [14.1](#141-stdlog) |
| `Std.Test` | Assertions | [14.2](#142-stdtest) |
| `Std.Concurrent` | Reserved; not designed | [15](#15-deferred-and-excluded) |

`SysIo` and `Test` remain as they are. `SysIo.println` becomes a thin call to `Console.print_line` that panics with `IoError` on failure, and `SysIo.Entry` is unchanged. `Test.assert` and `Test.assert_equal_int32` forward to `Std.Test`.

### 4.2 Dependency order

A module may import only modules above it in this list. Cycles between library modules are forbidden.

1. `Std.Core`
2. `Std.Collections`
3. `Std.Text` (except `Regex`)
4. `Std.Math`
5. `Std.Text.Regex`, `Std.Encoding`, `Std.Digest`, `Std.Random`, `Std.Time`
6. `Std.Io`
7. `Std.Fs`, `Std.System`, `Std.Net`
8. `Std.Log`, `Std.Test`

Three consequences of this order are deliberate:

- `StringBuilder` ([section 8.2](#82-stringbuilder)) is declared in `Std.Core`, because number formatting and every collection's `to_string` need it. `Std.Text` re-exports it.
- `Std.Core` never names a collection class. Where a core member returns several values it returns a built-in array or list.
- `Std.Core` contains the unexported class `WideUint` ([section 7.4](#74-float-text-conversion)) for float formatting. `BigInteger` has its own limb routines with the same algorithms, because an unexported class is not visible outside its module.

### 4.3 Visibility

A member that is not exported is visible throughout its declaring module. Each data structure's internal classes (arena, hash core, iterators) are unexported members of the child module that defines it. Facade modules re-export only the names listed in this plan.

### 4.4 Intrinsic floor

The complete list of compiler-provided operations the library may use, beyond ordinary language operations:

| Group | Intrinsics | Blocker |
| --- | --- | --- |
| Identity | Identity number of an object | L2 |
| Storage | Allocate vacant array; clear slot; bulk copy; list insert, truncate, version | L5 |
| Integer | Bitwise operations, shifts, wrapping arithmetic, sign reinterpretation | L6 |
| Float | Bit pattern to and from the same-width unsigned integer | L6 |
| Text | Byte length, byte at, construct from bytes, slice | L7 |
| Type | Type test, checked downcast | L9 |
| Enum | Variant count, variant from ordinal, all variants, variant name | L10 |
| Failure | Panic, recover, defer | E3, E4 |

Adding an intrinsic requires editing this table first.

### 4.5 Host services

Every interaction with the outside world is a host service with a name, a version, typed arguments, and typed success and error results ([NVM](../runtime/NVM.md#host-calls-and-standard-output)). All start at version 1. An error result is a `uint32` code that the library maps to an `IoErrorKind` ([section 6.3](#63-error)): 1 `NotFound`, 2 `PermissionDenied`, 3 `AlreadyExists`, 4 `InvalidInput`, 5 `Interrupted`, 6 `UnexpectedEnd`, 7 `NotADirectory`, 8 `IsADirectory`, 9 `DirectoryNotEmpty`, 10 `ConnectionRefused`, 11 `ConnectionReset`, 12 `TimedOut`, 13 `Unsupported`, 14 `CapabilityDenied`, and every other code `Other`.

| Service | Arguments | Success | Used by |
| --- | --- | --- | --- |
| `neura.console.write_line` | text | none | existing |
| `neura.console.write` | stream (1 output, 2 error), bytes | count written | `Console` |
| `neura.console.read` | maximum count | bytes; empty at end of input | `Console` |
| `neura.fs.open` | path text, mode | file ID | `File` |
| `neura.fs.read` | file ID, maximum count | bytes; empty at end | `File` |
| `neura.fs.write` | file ID, bytes | count written | `File` |
| `neura.fs.seek` | file ID, offset, origin | new position | `File` |
| `neura.fs.close` | file ID | none | `File` |
| `neura.fs.stat` | path text | kind, size, modified time | `Fs` |
| `neura.fs.list` | path text | array of entry names | `Fs` |
| `neura.fs.make_directory`, `neura.fs.remove`, `neura.fs.rename` | path text (two for rename) | none | `Fs` |
| `neura.clock.wall` | none | seconds and nanoseconds since the Unix epoch | `SystemClock` |
| `neura.clock.monotonic` | none | nanoseconds from an arbitrary origin | `SystemClock` |
| `neura.clock.sleep` | nanoseconds | none | `Clock.sleep` |
| `neura.entropy.fill` | count | bytes of that count | `Random.from_entropy` |
| `neura.env.get` | name text | found flag, value text | `Environment` |
| `neura.process.exit` | status | does not return | `Process.exit` |
| `neura.net.connect`, `neura.net.listen`, `neura.net.accept`, `neura.net.read`, `neura.net.write`, `neura.net.close` | see [11.4](#114-stdnet) | socket ID, bytes, or count | `Std.Net` |

- **Capabilities.** `neura run` denies every service group except console by default. `--allow-fs`, `--allow-net`, `--allow-env`, `--allow-clock`, and `--allow-entropy` enable groups. A denied service still binds, and each call returns error 14 (`CapabilityDenied`). `--allow-fs` takes an optional list of path prefixes.
- **Replay.** `neura run --record file` writes every host request and response in order. `--replay file` answers requests from that file and fails the run if a request differs from the recording. The existing host trace is the basis for this. Replay is how tests of I/O, time, and entropy stay deterministic.
- **Ordering.** `neura.fs.list` returns names sorted by their UTF-8 bytes, ascending. The host sorts; the library does not depend on directory order.
- Host IDs (files, sockets) are opaque `int64` values that the library wraps in closed classes and never exposes.

### 4.6 Versioning

The library has one version, equal to the compiler's version. The module `Std.Core` exports the constants `Version.Major`, `Version.Minor`, and `Version.Patch`. The text produced by `to_string`, the hash algorithms, the iteration orders, and the float formatting rules on this page are part of the contract and change only with a major version.

## 5. Core contracts

### 5.1 Universal members

Every value has `to_string`, `hash_code`, and `equals`. The defaults:

| Type | `equals` | `hash_code` | `to_string` |
| --- | --- | --- | --- |
| `bool` | Same value | 1231 for true, 1237 for false | `true` or `false` |
| Integers up to 32 bits | Same value | The value extended to 32 bits (sign-extended for signed types) and reinterpreted as `uint32` | Decimal, with `-` for negatives, no leading zeros |
| `int64`, `uint64` | Same value | Low 32 bits exclusive-or high 32 bits | Decimal, as above |
| `char` | Same scalar value | The scalar value | The character encoded as UTF-8 |
| Floats | Same bit pattern, except that every NaN equals every NaN. `0.0` and `-0.0` are **not** equal. | The bit pattern, with every NaN replaced by the canonical NaN; 16-bit patterns are zero-extended and the 64-bit pattern is folded like `uint64` | [Section 7.4](#74-float-text-conversion) |
| Enum | Same variant | The ordinal | The variant name |
| `string` | Same bytes | Start with 0; for each byte in order, set the hash to 31 times the hash plus the byte, wrapping | The string itself |
| Array, list | Same kind (array or list), same length, and each pair of elements `equals` in index order | Start with 1; for each element in order, set the hash to 31 times the hash plus the element's `hash_code`, wrapping | `[`, the elements' `to_string` separated by `, `, then `]` |
| Class (default) | Identity | The identity number multiplied by 2,654,435,761, wrapping | The qualified class name, `#`, and the identity number in decimal, for example `Acme.Shapes.Circle#3` |
| Function value | Identity | As for a class | `function` |

Float `equals` deliberately differs from `==`: `equals` is an equivalence relation consistent with `hash_code`, which hash tables need, while `==` stays IEEE 754. The multiplier 31 is chosen because multiplying by 31 is a shift and a subtraction, which costs only adder neurons.

**The contract every override must keep:**

1. `equals` is reflexive, symmetric, and transitive, and returns false for a value of an unrelated class.
2. Values that are `equals` have the same `hash_code`.
3. `hash_code` and `equals` do not panic and do not modify the value.
4. While an object is a key in a hash-based collection or an element of a hash-based set, its `hash_code` and `equals` results do not change. The library does not detect violations; lookups for such a key can fail.
5. `to_string` does not modify the value.

An override of `equals` uses a type test (L9) to reject other classes, then compares fields. A class that overrides `equals` must override `hash_code`; the compiler warns when only one is overridden.

**Self-containing collections.** When a collection's `to_string` meets the collection itself as a direct element, it prints `(this collection)` for that element. Deeper cycles are not detected and end with the frame-limit fault. `hash_code` and `equals` of a collection that contains itself are not supported.

### 5.2 `Hash`

`Hash` is a module of helpers for writing `hash_code` overrides.

| Member | Behavior |
| --- | --- |
| `combine(seed: uint32, value: uint32) → uint32` | 31 times `seed` plus `value`, wrapping. |
| `of<T>(value: T) → uint32` | The value's `hash_code`. |
| `spread(hash: uint32) → uint32` | `hash` exclusive-or `hash` shifted right by 16. Hash tables apply this before masking so that high bits affect the slot. |

A class with fields `a`, `b`, `c` computes its hash by starting from 17 and combining each field's hash in declaration order. Library classes all follow this pattern, so their hash codes are specified by their field order.

### 5.3 Ordering

- **`Ordering`** is an enum with variants `Less`, `Equal`, and `Greater`, in that order. Comparison results are `Ordering` values, never integers, so the subtraction idiom (which would panic on overflow) cannot be written. The module `Orderings` provides `reverse(o)`, `then(first, second)` (returns `first` unless it is `Equal`), `is_less(o)`, `is_at_most(o)`, `is_greater(o)`, and `is_at_least(o)`.
- **`Comparable<T>`** is an interface with one requirement, `compare_to(other: T) → Ordering`. It must define a total order, and `compare_to` returning `Equal` must agree with `equals`. The compiler provides `compare_to` for built-in types the way it provides universal members, and those types satisfy a `Comparable` bound:

| Type | Natural order |
| --- | --- |
| Integers, `char` | Numeric value |
| `bool` | `false` before `true` |
| Floats | A total order: negative infinity, negative finite values, `-0.0`, `0.0`, positive finite values, positive infinity, then NaN. All NaNs are equal. This agrees with float `equals`. |
| `string` | Byte-wise lexicographic comparison of the UTF-8 bytes; a proper prefix sorts first. This equals ordering by Unicode scalar value. |
| Enum | Ordinal |

  Arrays and lists have no natural order.

- **`Comparator<T>`** is the function shape `(T, T) → Ordering`. The module `Comparators` builds them:

| Member | Behavior |
| --- | --- |
| `natural<T: Comparable<T>>() → Comparator<T>` | Calls `compare_to`. |
| `reversed<T>(c) → Comparator<T>` | Swaps the arguments to `c`. |
| `by_key<T, K: Comparable<K>>(key: (T) → K) → Comparator<T>` | Compares the extracted keys naturally. `key` is called once per argument per comparison. |
| `by_key_with<T, K>(key, c: Comparator<K>) → Comparator<T>` | Compares extracted keys with `c`. |
| `then<T>(first, second) → Comparator<T>` | Uses `second` only when `first` returns `Equal`. |

A comparator that is not a total order never causes memory unsafety or nontermination in the library; the resulting order or lookup result is unspecified.

### 5.4 Iteration

- **`Iterator<T>`** requires `has_next() → bool` and `next() → T`. `has_next` has no observable effect and can be called repeatedly. `next` returns the next element and advances; when no element remains it panics with `NoSuchElementError`. An iterator is used once and cannot be reset.
- **`Iterable<T>`** requires `iterator() → Iterator<T>`. Each call returns a new, independent iterator positioned at the start.
- **Decision: no removal through iterators.** `Iterator` has no `remove`. Collections remove in bulk with `remove_if` and `retain_if`.
- **Decision: guaranteed fail-fast.** Every mutable library collection keeps a modification count, a `uint32` that advances by one, wrapping, on each structural modification (any change to the number of elements or, for maps, to the set of keys). An iterator records the count when created. `has_next` and `next` compare the counts first and panic with `ConcurrentModificationError` when they differ. Replacing a list element by index or a map value for an existing key is not structural. Execution is single threaded, so this check is exact, not best effort.
- The iteration order of every collection is specified in section 9. No library collection has an unspecified order.

### 5.5 `Closeable`

`Closeable` requires `close() → void or Error`.

- `close` releases the host resource. Calling it again has no effect and returns no error.
- Any other operation on a closed resource returns an `IoError` of kind `Closed`.
- If `close` reports an error, the resource is still closed.
- A reclaimed (L4) but unclosed resource is not closed by reclamation. The host closes every open file and socket when the run ends. `Fs` and `Net` each allow at most 1,024 simultaneously open IDs per run; exceeding that returns `IoError` of kind `Other`.
- `Closeables.use<R: Closeable, T>(resource: R, body: (R) → T) → T` calls `body` and closes the resource in a deferred action, whether `body` returns or panics. An error from `close` after a normal return is discarded by `use`; code that must see it calls `close` itself.

### 5.6 Function shapes

The library names these function types as type overlays in `Std.Core`:

| Name | Shape |
| --- | --- |
| `Action` | `() → void` |
| `Supplier<T>` | `() → T` |
| `Consumer<T>` | `(T) → void` |
| `BiConsumer<A, B>` | `(A, B) → void` |
| `Predicate<T>` | `(T) → bool` |
| `BiPredicate<A, B>` | `(A, B) → bool` |
| `Function<T, R>` | `(T) → R` |
| `BiFunction<A, B, R>` | `(A, B) → R` |
| `UnaryOperator<T>` | `(T) → T` |
| `BinaryOperator<T>` | `(T, T) → T` |
| `Comparator<T>` | `(T, T) → Ordering` |

Unless a member's description says otherwise, a function argument may be called any number of times, in the order the description implies, and a panic inside it propagates to the caller, leaving the collection in a valid state that the member documents.

### 5.7 Conformance matrix

This is the interlinking the library is built around. "Built in" means the compiler supplies the behavior (L2 and section 5.3).

| Type | Value `equals` and `hash_code` | `Comparable` | `Iterable` | Other interfaces |
| --- | --- | --- | --- | --- |
| Integers, floats, `bool`, `char`, enums, `string` | Built in | Built in | No (`Strings.chars` gives an `Iterable<char>`) | |
| Array, list | Built in, structural | No | Built-in for-each | |
| `Optional<T>`, `Result<T>` | Yes | No | `Optional` only | |
| `Pair<A, B>`, `Unit`, `Boxed<T>` | Yes | `Boxed` when `T` is | No | |
| `IntRange` | Yes | No | Yes | |
| `ArrayList`, `LinkedList` | Yes, as lists | No | Yes | `MutableList`; `LinkedList` also `Deque` |
| `ArrayDeque`, `PriorityQueue` | Identity | No | Yes | `Deque` or `Queue` |
| `HashSet`, `TreeSet`, `EnumSet`, `BitSet` | Yes, as sets | No | Yes | `MutableSet`; `TreeSet` also `SortedSet` |
| `HashMap`, `TreeMap`, `EnumMap` | Yes, as maps | No | Yes, of entries | `MutableMap`; `TreeMap` also `SortedMap` |
| `Sequence<T>` | Identity | No | Yes, once | |
| `StringBuilder` | Identity | No | No | `TextWriter` |
| `BigInteger`, `BigDecimal` | Yes | Yes | No | |
| `Duration`, `Instant`, `LocalDate`, `LocalTime`, `LocalDateTime`, `ZoneOffset` | Yes | Yes | No | |
| `Path` | Yes | Yes | No | |
| `Regex`, `Random`, streams, files, sockets, `Logger` | Identity | No | No | Streams: `ByteReader`, `ByteWriter`, `Closeable` as listed |
| `JsonValue` and subclasses | Yes, structural | No | No | |
| Error classes | Identity | No | No | `Error` |

"As lists", "as sets", and "as maps" are defined in [section 9.2](#92-equality-of-collections): two lists of different classes with equal elements in the same order are equal.

Every exported class on this page overrides `to_string`; each section gives the exact text.

## 6. `Std.Core` types

### 6.1 `Optional<T>`

A closed, immutable class that holds one value or nothing.

- **Layout.** A `bool` field `present` and a late field `value` (L5), vacant when `present` is false.
- **Construction.** `Optional<T>.of(value)` and `Optional<T>.empty()`. `empty()` returns one shared instance per type argument, created on first use. Code must not depend on the identity of any `Optional`.

| Member | Behavior |
| --- | --- |
| `is_present() → bool`, `is_empty() → bool` | Test for a value. |
| `get() → T` | The value. Panics with `NoSuchElementError` when empty. |
| `or_else(fallback: T) → T` | The value, or `fallback`. |
| `or_else_get(supplier: Supplier<T>) → T` | The value, or the supplier's result. The supplier is called only when empty. |
| `or_panic(message: string) → T` | The value, or a panic with `NoSuchElementError` carrying `message`. |
| `or(other: Supplier<Optional<T>>) → Optional<T>` | This optional when present, otherwise the supplier's result. |
| `map<U>(f: Function<T, U>) → Optional<U>` | Applies `f` to the value; empty stays empty. |
| `flat_map<U>(f: Function<T, Optional<U>>) → Optional<U>` | Applies `f` and returns its result directly. |
| `filter(p: Predicate<T>) → Optional<T>` | This optional when present and `p` holds, otherwise empty. |
| `if_present(action: Consumer<T>) → void` | Calls `action` with the value when present. |
| `to_result(error: Error) → Result<T>` | `Ok` of the value, or `Err` of `error`. |
| `iterator() → Iterator<T>` | Yields the value once, or nothing. |

`equals`: both empty, or both present with `equals` values. `hash_code`: 0 when empty, otherwise `Hash.combine(1, hash of value)`. `to_string`: `Optional.empty` or `Optional(` value `)`.

### 6.2 `Result<T>`

A closed, immutable class that holds a value or an `Error`. It is the storable form of `T or Error`: use it to keep outcomes in collections and to pass them through function values.

- **Layout.** A `bool` field `ok`, a late field `value`, and a late field `error`. Exactly one late field is filled.
- **Construction.** `Result<T>.ok(value)`, `Result<T>.err(error)`. An operation with no value uses `Result<Unit>`.

| Member | Behavior |
| --- | --- |
| `is_ok() → bool`, `is_err() → bool` | Test the outcome. |
| `get() → T` | The value. On `Err`, panics with the contained error itself. |
| `error() → Error` | The error. On `Ok`, panics with `StateError`. |
| `value_or(fallback: T) → T` | The value, or `fallback`. |
| `unpack() → T or Error` | Converts back to the two-result form (E2). |
| `map<U>(f: Function<T, U>) → Result<U>` | Applies `f` to an `Ok` value; `Err` passes through with the same error object. |
| `and_then<U>(f: Function<T, Result<U>>) → Result<U>` | Chains a fallible step. |
| `map_error(f: Function<Error, Error>) → Result<T>` | Replaces the error of an `Err`. |
| `to_optional() → Optional<T>` | The value, discarding any error. |

`equals`: both `Ok` with `equals` values, or both `Err` with `equals` errors (identity, unless the error class overrides). `hash_code`: `Hash.combine(1, hash of value)` or `Hash.combine(2, hash of error)`. `to_string`: `Ok(` value `)` or `Err(` error `)`.

### 6.3 `Error`

`Error` is an interface with two requirements:

- `message() → string`: a human-readable description, without a trailing period or newline.
- `cause() → Optional<Error>`: the error this one wraps, if any.

Errors are immutable. A cause is set only at construction, so cause chains are finite and acyclic.

**`BaseError`** is the one open (inheritable) class in the library. It implements `Error`, stores the message and the optional cause, and takes both in its constructor. Its `to_string` is the class's unqualified name, `: `, and the message; when there is a cause it appends `; caused by ` and the cause's `to_string`. Every library error class derives from `BaseError` and inherits this text. User code declares its own error classes by deriving from `BaseError` or by implementing `Error` directly.

The module **`Errors`** provides `of(message) → Error` (a plain `BaseError`), `wrap(cause, message) → Error`, `root_cause(error) → Error` (follows causes to the end), and `chain(error)`, which returns a built-in list of `Error` (the error, then each cause in order). Code identifies an error's class with a type test (L9) on each member of the chain.

Library error classes. "Use" says whether the class is carried by panics or returned as a value.

| Class | Extra fields | Use | Meaning |
| --- | --- | --- | --- |
| `ArgumentError` | | Panic | An argument violates a documented precondition. |
| `IndexError` | `index: int64`, `length: int64` | Panic | An index or range is outside a collection or string. |
| `ArithmeticError` | | Panic | Overflow, division by zero, invalid shift, out-of-range conversion. |
| `StateError` | | Panic | The receiver is in a state that forbids the call: a vacant read, a consumed `Sequence`, `error()` on `Ok`. |
| `NoSuchElementError` | | Panic | An element was required and none exists: `next()` at the end, `get()` on empty, `first()` of an empty list. |
| `ConcurrentModificationError` | | Panic | A collection changed structurally during iteration. |
| `CastError` | | Panic | A checked downcast failed. |
| `UnsupportedError` | | Panic | The operation is not available for this receiver. |
| `AssertionError` | | Panic | A `Std.Test` assertion failed. |
| `IoError` | `kind: IoErrorKind`, `path: Optional<string>` | Value | A host operation failed. |
| `ParseError` | `kind: ParseErrorKind`, `offset: int` | Value | Text does not match the expected grammar. `offset` is the byte offset of the first offending byte, or the input's length for an unexpected end. |
| `EncodingError` | `offset: int` | Value | Bytes are not valid in the expected encoding. |

- `IoErrorKind`, in order: `NotFound`, `PermissionDenied`, `AlreadyExists`, `InvalidInput`, `Interrupted`, `UnexpectedEnd`, `NotADirectory`, `IsADirectory`, `DirectoryNotEmpty`, `ConnectionRefused`, `ConnectionReset`, `TimedOut`, `Unsupported`, `CapabilityDenied`, `Closed`, `InvalidData`, `Other`. The first fourteen map from host error codes 1 through 14 ([section 4.5](#45-host-services)). `Closed` and `InvalidData` are produced by the library.
- `ParseErrorKind`, in order: `Empty`, `InvalidCharacter`, `OutOfRange`, `UnexpectedEnd`, `Syntax`.

### 6.4 Panic or error

| Situation | Outcome |
| --- | --- |
| The caller could have checked the condition from values it already holds: an index against a size, emptiness, a negative count, a zero step | Panic |
| A documented precondition on arguments is violated | Panic with `ArgumentError` |
| The failure depends on the outside world: files, network, console, environment, clock | Error value (`IoError`) |
| The failure depends on the content of text or bytes that typically come from outside: parsing numbers, dates, JSON, regular expressions, UTF-8, Base64 | Error value (`ParseError` or `EncodingError`) |
| Absence is an ordinary answer: a map lookup, a search, the first element of something that may be empty | `Optional`, or -1 for positions |

Each member that can be reached both ways offers both forms with fixed naming: the plain name panics (`get`, `first`, `dequeue`), and the `try_` or `_or_none` form returns `Optional` (`first_or_none`, `try_dequeue`). No library function recovers a panic except `Std.Test` ([section 14.2](#142-stdtest)) and `Closeables.use`, which re-raises it after closing.

### 6.5 Small value types

| Type | Definition |
| --- | --- |
| `Pair<A, B>` | Closed, immutable, fields `first` and `second`. `equals` and `hash_code` combine both fields in order. `to_string`: `(` first `, ` second `)`. |
| `Unit` | A closed class with exactly one instance, the class-level constant `Unit.Value`. `to_string`: `()`. `hash_code`: 0. |
| `Boxed<T>` | Closed, immutable, one field `value`. `equals`, `hash_code`, and `to_string` delegate to the value. It exists so that primitives, strings, enums, arrays, and lists can be passed as `Object`. |
| `Entry<K, V>` | Closed, immutable, fields `key` and `value`. A snapshot: changing the map afterwards does not change the entry. `hash_code`: key hash exclusive-or value hash. `to_string`: key `: ` value. |
| `IntRange` | Closed, immutable: `start` (inclusive), `end` (exclusive), `step` (nonzero). `IntRange.of(start, end)` uses step 1; `IntRange.stepped(start, end, step)` panics with `ArgumentError` for step 0. A negative step counts down while the value is greater than `end`. Implements `Iterable<int>` and has its own `size() → int`, `is_empty()`, and `contains(value: int) → bool`. `size` is computed in `int64` so that no intermediate overflows, and `contains` is arithmetic, not a scan. `to_string`: start `..` end, with ` step ` step appended when the step is not 1. |

## 7. Primitive companion modules

`Std.Core` exports one module per primitive type: `Int8`, `Int16`, `Int32`, `Int64`, `UInt8`, `UInt16`, `UInt32`, `UInt64`, `Float16`, `BFloat16`, `Float32`, `Float64`, `Bool`, and `Char`. There is no overloading, so an operation is spelled with its type's module (`Int64.abs`). The integer modules have identical member lists; so do the float modules. Each is a separate source file, written out per type, not generated at build time.

### 7.1 Integer modules

`X` stands for the module's type.

| Member | Behavior |
| --- | --- |
| Constants `Min`, `Max`, `Bits` | The type's range and width. |
| `abs(v) → X` | Signed types only. Panics with `ArithmeticError` for `Min`. |
| `sign(v) → int` | -1, 0, or 1. |
| `min(a, b)`, `max(a, b)`, `clamp(v, low, high)` | `clamp` panics with `ArgumentError` when `low` is greater than `high`. |
| `pow(base, exponent: int) → X` | Square-and-multiply with checked multiplication. A negative exponent panics with `ArgumentError`. `pow(0, 0)` is 1. |
| `gcd(a, b) → X` | Euclid's algorithm on magnitudes. `gcd(0, 0)` is 0. Panics with `ArithmeticError` when the result does not fit (`gcd(Min, 0)` for signed types). |
| `checked_add`, `checked_sub`, `checked_mul`, `checked_div`, `checked_rem` `→ Optional<X>` | Empty where the operator would panic. |
| `wrapping_add`, `wrapping_sub`, `wrapping_mul`, `wrapping_neg` | The L6 intrinsics. |
| `saturating_add`, `saturating_sub`, `saturating_mul` | Clamp to `Min` or `Max`. |
| `floor_div(a, b)`, `floor_mod(a, b)` | Quotient rounded toward negative infinity, and the remainder with the divisor's sign. Same panics as `/` and `%`. |
| `count_ones`, `leading_zeros`, `trailing_zeros` `→ int` | Bit counts over `Bits` bits. |
| `rotate_left(v, n: int)`, `rotate_right(v, n: int)` | `n` is reduced modulo `Bits` with a non-negative result first. |
| `parse(text) → X or Error` | Radix 10. See below. |
| `parse_radix(text, radix: int) → X or Error` | Radix 2 through 36; another radix panics with `ArgumentError`. |
| `to_string_radix(v, radix: int) → string` | Lowercase digits, `-` for negatives. |
| `format(v, radix: int, min_width: int, pad: char) → string` | As `to_string_radix`, then left-padded to `min_width` characters. When `pad` is `0`, padding goes after the sign. |

**Parsing grammar.** An optional `+` or `-`, then one or more digits of the radix, with letters accepted in either case. Nothing else: no whitespace, no separators, no radix prefix. An empty input fails with kind `Empty`; a sign with no digits with `UnexpectedEnd`; any other byte with `InvalidCharacter` at its offset; a value outside the type with `OutOfRange` at offset 0. A `-` on an unsigned type is accepted only for a value of zero. The parser accumulates in the negative range for signed types, so `Min` parses without overflow.

### 7.2 Float modules

| Member | Behavior |
| --- | --- |
| Constants `Max`, `MinPositive` (smallest subnormal), `MinNormal`, `Epsilon`, `Infinity`, `NegativeInfinity`, `NaN` | `NaN` is the canonical NaN. |
| `is_nan`, `is_infinite`, `is_finite`, `is_negative` | `is_negative` reads the sign bit, so it is true for `-0.0`. |
| `abs`, `copy_sign(magnitude, sign)` | Sign-bit operations; never round. |
| `min(a, b)`, `max(a, b)` | NaN if either operand is NaN. `-0.0` is less than `0.0`. |
| `clamp(v, low, high)` | Panics with `ArgumentError` when `low` is greater than `high` or either is NaN. |
| `floor`, `ceil`, `trunc`, `round`, `round_even` | `round` rounds halves away from zero; `round_even` rounds halves to even. All return the float type and preserve the sign of zero. |
| `to_bits(v)`, `from_bits(bits)` | The L6 reinterpretation. |
| `total_compare(a, b) → Ordering` | The total order of [section 5.3](#53-ordering). |
| `parse(text) → X or Error` | [Section 7.4](#74-float-text-conversion). |
| `to_fixed(v, digits: int) → string` | Exactly `digits` fraction digits (0 through 100), correctly rounded, ties to even. |
| `to_exponential(v, digits: int) → string` | One digit, `.`, `digits` digits, `e`, and the exponent. |

Mathematical functions (`sqrt`, `exp`, `sin`, and so on) are in `Std.Math` ([section 10.1](#101-math)).

### 7.3 `Bool` and `Char`

- `Bool`: `parse(text) → bool or Error` accepts exactly `true` and `false`; `to_int(v) → int` gives 1 or 0; `xor(a, b)`.
- `Char`: constants `Min` (scalar 0), `Max` (scalar 0x10FFFF), and `Replacement` (0xFFFD); `from_scalar(value: uint32) → Optional<char>`; `utf8_length(c) → int` (1 through 4). Classification lives in `Std.Text` ([section 8.3](#83-chars)).

### 7.4 Float text conversion

**`to_string`.** The output is the shortest decimal digit string that parses back to the same value, chosen as follows:

1. NaN is `NaN`; infinities are `Infinity` and `-Infinity`; zeros are `0.0` and `-0.0`.
2. Otherwise find the decimal number with the fewest significant digits that lies in the interval of values rounding to this float under ties-to-even parsing. If several have that many digits, take the one closest to the exact value; if still tied, the one with an even last digit.
3. Let the digits be `d1 d2 ... dn` with decimal exponent `e`, so that the value is `d1.d2...dn × 10^e`. If `e` is at least -4 and less than 16, print positional notation with at least one digit on each side of the point (`1.0`, `0.001`, `123456.7`). Otherwise print `d1`, a point, the remaining digits or `0` if none, `e`, and the exponent in decimal with `-` when negative and no `+` (`1.0e16`, `2.5e-7`).
4. Prefix `-` for negative values.

Every finite output is a valid Neura float literal for its type. The interval and the digits are computed for the value's own format, so a `float16` prints the digits that distinguish it among `float16` values.

**`parse`.** Accepts `NaN`, `Infinity`, `+Infinity`, `-Infinity`, or: an optional sign, digits with an optional point (at least one digit overall), and an optional exponent of `e` or `E`, an optional sign, and digits. The decimal value is converted exactly and rounded once, ties to even, to the target format. A magnitude too large becomes an infinity, and one too small becomes a zero with the sign; neither is an error, unlike a source literal. Failures use `ParseError` with the kinds of section 7.1.

**Reference arithmetic.** Both directions are defined by exact integer arithmetic: the float is an integer significand times a power of two, and the comparison of candidate decimals is done on integers scaled by powers of 2 and 10. `Std.Core` contains an unexported class `WideUint`, a non-negative integer stored as a `uint32` list of limbs, least significant first, with add, subtract, compare, multiply by a `uint32`, shift, and division with remainder by a `uint32`. Float conversion is written on `WideUint`. A faster algorithm may replace it only if its output is identical for every value of `float16` and `bfloat16` (checked exhaustively) and for the test corpus of [section 17](#17-verification).

## 8. `Std.Text`

### 8.1 `Strings`

String operations are functions in the module `Strings` that take the string first. **Decision:** all positions are byte offsets into the UTF-8 encoding, of type `int`. No member is named `length`: `byte_length` and `char_count` say which unit they count. An offset that is out of range, or that is not on a character boundary where one is required, panics with `IndexError`.

| Member | Behavior |
| --- | --- |
| `byte_length(s) → int` | Constant time. |
| `char_count(s) → int` | Counts scalar values by counting bytes that are not continuation bytes. Linear. |
| `is_empty(s) → bool` | |
| `byte_at(s, offset) → uint8` | |
| `char_at(s, offset) → char` | Decodes the character starting at `offset`. |
| `is_char_boundary(s, offset) → bool` | True for 0, for the byte length, and for any offset whose byte is not a continuation byte. False out of range. |
| `chars(s) → Iterable<char>` | The scalar values in order. |
| `char_offsets(s) → Iterable<Pair<int, char>>` | Each character with its starting offset. |
| `slice(s, start, end) → string` | Bytes from `start` up to `end`. Both must be boundaries and `start` must not exceed `end`. |
| `slice_from(s, start) → string` | To the end. |
| `starts_with(s, prefix)`, `ends_with(s, suffix)`, `contains(s, needle)` `→ bool` | Byte comparison. The empty string is a prefix, suffix, and substring of every string. |
| `index_of(s, needle) → int`, `index_of_from(s, needle, from) → int` | Offset of the first occurrence at or after `from`, or -1. An empty needle matches at `from`. |
| `last_index_of(s, needle) → int` | Offset of the last occurrence, or -1. An empty needle matches at the byte length. |
| `split(s, separator) → ArrayList<string>` | Every piece between occurrences, scanning left to right without overlap. Empty pieces are kept, including leading and trailing ones, so `a,,b,` splits into four pieces and the empty string into one empty piece. An empty separator panics with `ArgumentError`. |
| `split_limit(s, separator, limit) → ArrayList<string>` | At most `limit` pieces; the last holds the unsplit remainder. `limit` below 1 panics with `ArgumentError`. |
| `lines(s) → ArrayList<string>` | Splits at LF; a CR immediately before an LF is removed; a final empty piece after a trailing LF is dropped. |
| `join(parts: Iterable<string>, separator) → string` | |
| `repeat(s, count) → string` | A negative count panics with `ArgumentError`; a result longer than the maximum string length panics with `ArgumentError`. |
| `trim(s)`, `trim_start(s)`, `trim_end(s)` | Remove whitespace: space, tab, LF, vertical tab, form feed, CR. |
| `strip_prefix(s, prefix)`, `strip_suffix(s, suffix)` `→ Optional<string>` | The remainder when the affix is present. |
| `replace(s, target, replacement) → string` | Replaces every non-overlapping occurrence, left to right. An empty target panics with `ArgumentError`. |
| `replace_first(s, target, replacement) → string` | |
| `to_ascii_upper(s)`, `to_ascii_lower(s)` | Change only the letters A through Z and a through z. |
| `equals_ignore_ascii_case(a, b) → bool` | |
| `compare(a, b) → Ordering` | The natural order of [section 5.3](#53-ordering). |
| `pad_start(s, width, pad: char)`, `pad_end(s, width, pad: char)` | Pads to `width` characters, not bytes. |
| `reverse(s) → string` | Reverses the order of scalar values. |
| `to_bytes(s) → uint8[]` | A copy of the UTF-8 bytes. |
| `from_bytes(bytes: uint8[]) → string or Error` | `EncodingError` with the offset of the first invalid byte. |
| `from_bytes_lossy(bytes: uint8[]) → string` | Each maximal invalid sequence becomes one U+FFFD. |
| `from_chars(chars: Iterable<char>) → string` | |

**Search algorithm.** `index_of` scans for the needle's first byte and then compares the remaining bytes in place. Its worst case is the product of the two lengths. This is a decision: the simple scan has the smallest circuit, and its step count is easy to predict.

**UTF-8 validity** follows the Unicode definition: shortest form only, no surrogates, nothing above 0x10FFFF. The validator used by `from_bytes` is unexported Neura code in `Std.Text`, which `Std.Encoding.Utf8` ([section 13.1](#131-stdencoding)) calls. It must agree with the validation NVM performs when constructing text.

**Unicode scope. Decision:** the first version handles encoding, decoding, and iteration for all of Unicode, and classifies and case-maps ASCII only. Member names say `ascii` so that a later Unicode-aware module (`Std.Text.Unicode`, pinned to one Unicode version, with normalization and full case mapping) can be added without changing existing behavior.

### 8.2 `StringBuilder`

A closed class that accumulates text. It is declared in `Std.Core` and re-exported by `Std.Text` ([section 4.2](#42-dependency-order)).

- **Layout.** One built-in `uint8` list. It always holds valid UTF-8, because every append adds whole characters.
- `StringBuilder` implements `TextWriter` ([section 11.1](#111-stdio)); its writes never fail.

| Member | Behavior |
| --- | --- |
| `append<T>(value: T) → StringBuilder` | Appends `value.to_string()`. Returns the builder for chaining. |
| `append_string(s)`, `append_char(c)`, `append_line(s)` | `append_line` appends `s` and one LF. |
| `append_repeated(c: char, count: int)` | |
| `byte_length() → int`, `is_empty() → bool` | |
| `truncate(byte_length: int)` | Shortens to the given length, which must be a character boundary. |
| `clear()` | |
| `to_string() → string` | Constructs a string from a copy of the bytes. The builder stays usable. |

### 8.3 `Chars`

Functions on `char`: `is_ascii`, `is_ascii_digit`, `is_ascii_hex_digit`, `is_ascii_alphabetic`, `is_ascii_alphanumeric`, `is_ascii_uppercase`, `is_ascii_lowercase`, `is_ascii_whitespace` (the six characters of `trim`), `is_ascii_punctuation`, `is_ascii_control`, `to_ascii_upper`, `to_ascii_lower`, `digit_value(c, radix: int) → Optional<int>` (radix 2 through 36, either letter case), and `from_digit(value: int, radix: int) → char` (lowercase; panics with `ArgumentError` when out of range). Each classification is a range comparison on the scalar value; none uses a table.

### 8.4 `Regex`

A closed, immutable class holding a compiled regular expression. Matching runs in time proportional to the pattern size times the input length and never backtracks.

**Syntax.** Literals; `.` (any character except LF, or any character under the dot-all flag); character classes `[...]` with ranges and negation; the escapes `\d`, `\w`, `\s` and their negations (ASCII definitions); `\t`, `\n`, `\r`, `\\`, escaped metacharacters, and `\u{...}` with one to six hex digits; anchors `^` and `$` (input boundaries, or line boundaries under the multi-line flag); `\b` and `\B` (ASCII word boundaries); grouping `( )`, non-capturing `(?: )`, and named `(?<name> )`; alternation `|`; quantifiers `*`, `+`, `?`, `{n}`, `{n,}`, `{n,m}`, each with a lazy form using a trailing `?`. **Not supported:** backreferences, lookahead, lookbehind, possessive quantifiers, and Unicode property classes. A pattern that uses them fails to compile.

**Compilation.** `Regex.compile(pattern) → Regex or Error` and `Regex.compile_with(pattern, flags: RegexFlags) → Regex or Error`. `RegexFlags` is an immutable class with three `bool` fields: `ignore_ascii_case`, `multi_line`, `dot_all`. A recursive-descent parser builds a syntax tree; a compiler turns it into a program for a virtual machine with the instructions character, any, class, split (two targets in priority order), jump, save (capture position), assert (anchor or boundary), and match. A counted repetition is expanded into copies. Limits: a repetition count of at most 1,000, at most 10,000 instructions, and group nesting of at most 250. Exceeding a limit, or a syntax error, returns `ParseError` with the pattern offset.

**Matching.** The matcher is a Pike machine: it advances a list of threads over the input one character at a time, in priority order, with at most one thread per instruction per position. Each thread carries its capture positions. The result is the leftmost match, and among matches at that start the one a backtracking matcher would find first (greedy quantifiers prefer more, lazy prefer fewer, alternation prefers the left branch). The matcher works on decoded characters and reports byte offsets.

| Member | Behavior |
| --- | --- |
| `is_match(input) → bool` | Whether a match exists anywhere. |
| `matches_fully(input) → bool` | Whether the whole input matches. |
| `find(input) → Optional<RegexMatch>`, `find_from(input, offset)` | The first match at or after the offset. |
| `find_all(input) → ArrayList<RegexMatch>` | Successive non-overlapping matches. After an empty match the search resumes one character later. |
| `replace_all(input, replacement) → string`, `replace_first(...)` | In the replacement, `$0` through `$99` and `${name}` insert groups, and `$$` is a dollar sign. A reference to a group that does not exist panics with `ArgumentError`; a group that did not participate inserts nothing. |
| `split(input) → ArrayList<string>` | Pieces between matches, with the rules of `Strings.split`. |
| `group_count() → int`, `pattern() → string` | |

`RegexMatch` is immutable: `start()`, `end()`, `text()`, `group(index) → Optional<string>`, `group_start(index) → int` (-1 when the group did not participate), and `named_group(name) → Optional<string>`. Group 0 is the whole match.

## 9. `Std.Collections`

### 9.1 Interfaces

Read-only and mutable interfaces are separate. A function that only reads takes the read-only interface. **Decision:** there are no methods that panic with `UnsupportedError` to simulate immutability; a type either has the method or does not.

```text
Iterable<T>
└── Collection<T>
    ├── MutableCollection<T>
    ├── List<T> ──────────── MutableList<T>   (also MutableCollection<T>)
    ├── Set<T> ───────────── MutableSet<T>    (also MutableCollection<T>)
    │   └── SortedSet<T>
    └── (via MutableCollection<T>) Queue<T> ── Deque<T>

Iterable<Entry<K, V>>
└── Map<K, V> ── MutableMap<K, V>
    └── SortedMap<K, V>
```

Members marked (d) are default methods written once in the interface, in terms of the other members. A class overrides a default only when this plan says so.

**`Collection<T>`**

| Member | Behavior |
| --- | --- |
| `size() → int` | |
| `is_empty() → bool` (d) | |
| `contains(value: T) → bool` (d) | Linear search with `value.equals(element)`. Hash and tree classes override it. |
| `contains_all(values: Iterable<T>) → bool` (d) | |
| `for_each(action: Consumer<T>)` (d) | In iteration order. |
| `to_array() → T[]` (d) | A new array in iteration order. |
| `to_list() → ArrayList<T>` (d) | A new list in iteration order. |
| `sequence() → Sequence<T>` (d) | A lazy pipeline over a new iterator. |

**`MutableCollection<T>`**

| Member | Behavior |
| --- | --- |
| `add(value: T) → bool` | Adds the value. Returns whether the collection changed (a set returns false for a duplicate). |
| `add_all(values: Iterable<T>) → bool` (d) | Adds each in order. Passing the collection itself panics with `ConcurrentModificationError`, by the fail-fast rule. |
| `remove(value: T) → bool` | Removes the first element `equals` to the value. |
| `remove_if(p: Predicate<T>) → int` | Removes every element for which `p` holds; returns how many. `p` is called exactly once per element, in iteration order. |
| `retain_if(p: Predicate<T>) → int` (d) | `remove_if` with the predicate negated. |
| `clear()` | |

If the predicate of `remove_if` panics, the collection is left valid: elements already rejected are removed, and every other element remains in its original relative order.

**`List<T>`** adds: `get(index) → T`; `first() → T` and `last() → T` (panic with `NoSuchElementError` when empty); `first_or_none()` and `last_or_none() → Optional<T>`; `index_of(value) → int` and `last_index_of(value) → int`; `slice(start, end) → ArrayList<T>`, which **copies** (decision: there are no sublist views); and `binary_search(value, c: Comparator<T>) → int` ([section 9.11](#911-algorithms)). All are defaults over `get` and `size` except `get`.

**`MutableList<T>`** adds: `set(index, value) → T` (returns the old element; not structural); `add_at(index, value)` (index may equal the size); `add_all_at(index, values: Iterable<T>)`; `remove_at(index) → T`; `remove_first() → T` and `remove_last() → T` (panic with `NoSuchElementError` when empty); `swap(i, j)`; `reverse()`; `fill(value)`; `truncate(size)` (a size above the current size panics with `ArgumentError`); and `sort(c: Comparator<T>)` (stable; [section 9.11](#911-algorithms)). `add` appends.

**`Set<T>`** adds: `is_subset_of(other: Set<T>) → bool`, `is_disjoint_from(other: Set<T>) → bool`, and `union`, `intersection`, `difference` with another `Set<T>`, each returning a new `HashSet<T>` whose order is this set's elements first, then (for `union`) the other's new elements, each in its own iteration order. **`MutableSet<T>`** adds nothing beyond `MutableCollection<T>`.

**`SortedSet<T>`** adds: `first()`, `last()`, and their `_or_none` forms; `floor(value)` (greatest element not greater), `ceiling(value)` (least not less), `lower(value)` (greatest strictly less), `higher(value)` (least strictly greater), each `→ Optional<T>`; `range(from, to) → Iterable<T>` (from inclusive, to exclusive, ascending); and `descending() → Iterable<T>`.

**`Queue<T>`** extends `MutableCollection<T>`: `enqueue(value)`, `dequeue() → T` (panics with `NoSuchElementError` when empty), `try_dequeue() → Optional<T>`, and `peek() → Optional<T>`. `add` is `enqueue`.

**`Deque<T>`** extends `Queue<T>`: `add_first`, `add_last`, `remove_first() → T`, `remove_last() → T`, `peek_first()`, `peek_last() → Optional<T>`. `enqueue` is `add_last`; `dequeue` is `remove_first`.

**`Map<K, V>`**

| Member | Behavior |
| --- | --- |
| `size() → int`, `is_empty() → bool` (d) | |
| `contains_key(key: K) → bool` | |
| `contains_value(value: V) → bool` (d) | Linear. |
| `get(key: K) → Optional<V>` | |
| `get_or(key: K, fallback: V) → V` (d) | |
| `get_or_panic(key: K) → V` (d) | Panics with `NoSuchElementError`. |
| `keys() → Iterable<K>`, `values() → Iterable<V>` | Live views: each `iterator()` call walks the map's current contents, fail-fast. They allocate no copy. |
| `for_each(action: BiConsumer<K, V>)` | In iteration order, without allocating entries. |
| `iterator() → Iterator<Entry<K, V>>` | Allocates one `Entry` snapshot per element. |

**`MutableMap<K, V>`**

| Member | Behavior |
| --- | --- |
| `put(key, value) → Optional<V>` | Associates the value; returns the previous value if the key was present. |
| `put_if_absent(key, value) → Optional<V>` | Stores only when absent; returns the existing value otherwise. |
| `get_or_put(key, supplier: Supplier<V>) → V` | Returns the existing value, or stores and returns the supplier's result. |
| `compute(key, f: BiFunction<K, Optional<V>, Optional<V>>) → Optional<V>` | Calls `f` with the current value; stores a present result, removes the key for an empty one; returns the result. |
| `merge(key, value, f: BinaryOperator<V>) → V` | Stores `value` when absent, otherwise stores `f(old, value)`; returns what was stored. |
| `remove(key) → Optional<V>` | |
| `put_all(other: Map<K, V>)` (d) | In the other map's iteration order. |
| `remove_if(p: BiPredicate<K, V>) → int` | |
| `clear()` | |

A function passed to `get_or_put`, `compute`, or `merge` must not modify the map. The map records its modification count before the call and panics with `ConcurrentModificationError` afterwards if it changed.

**`SortedMap<K, V>`** adds `first_entry`, `last_entry`, `floor_entry(key)`, `ceiling_entry(key)`, `lower_entry(key)`, `higher_entry(key)`, each `→ Optional<Entry<K, V>>`; `range(from, to) → Iterable<Entry<K, V>>`; and `descending() → Iterable<Entry<K, V>>`.

### 9.2 Equality of collections

| Kind | `equals(other)` | `hash_code` | `to_string` |
| --- | --- | --- | --- |
| Any `List<T>` | `other` is a `List<T>` of the same size with `equals` elements at each index | The array formula of [section 5.1](#51-universal-members) | `[a, b, c]` |
| Any `Set<T>` | `other` is a `Set<T>` of the same size and this set contains each of its elements | The wrapping sum of the element hashes | `{a, b, c}` in iteration order |
| Any `Map<K, V>` | `other` is a `Map<K, V>` of the same size and, for each of its entries, this map holds an `equals` value for the key | The wrapping sum of the entry hashes | `{k: v, k2: v2}` in iteration order |
| `ArrayDeque`, `PriorityQueue`, `Sequence` | Identity | Identity | `[a, b, c]` in iteration order; `Sequence` prints `Sequence` |

An `ArrayList<int>` therefore equals a `LinkedList<int>` with the same elements, and a `HashSet<T>` equals a `TreeSet<T>` with the same elements. A sorted collection's `equals` uses the other collection's elements with its own lookup, so it is consistent only when its comparator agrees with `equals`.

### 9.3 `ArrayList<T>`

**Decision:** `ArrayList` is backed by the built-in list, not by a library-managed array. Each built-in list operation is one NVM storage operation, while a library-managed backing array would spend graph evaluations on capacity checks and growth for every append. The built-in list's capacity is not observable, and the heap limit counts its length.

- **Layout.** One field: a built-in list of `T`.
- **Modification count.** The built-in list's structural version (L5). Iterators record and compare it, so modification through an alias of the wrapped list is detected too.
- **Factories.** The constructor makes an empty list. `ArrayList<T>.of(items: T[])` and `ArrayList<T>.from(items: Iterable<T>)` copy. `ArrayList<T>.filled(count, value)` repeats a value. `ArrayList<T>.wrap(list)` adopts a built-in list without copying, so the two share storage; `unwrap()` returns the underlying built-in list.

| Operation | Cost | Implementation |
| --- | --- | --- |
| `get`, `set`, `size` | Constant | Built-in index and length. |
| `add`, `remove_last` | Amortized constant | Built-in append; built-in remove at the last index. |
| `add_at`, `remove_at` | Linear in the elements after the index | Built-in insert and remove. |
| `add_all_at` | Linear in the list plus the added count | Append the new elements, then rotate the tail into place with three in-place reversals of the affected range. |
| `remove_if` | Linear | One pass with a write position; kept elements are copied down; then truncate. A deferred action moves the unvisited tail down and truncates if the predicate panics. The version advances once if anything was removed. |
| `clear`, `truncate` | Constant storage operations | Built-in truncate. |
| `contains`, `index_of` | Linear | |
| `sort` | [Section 9.11](#911-algorithms) | |

### 9.4 `ArrayDeque<T>`

A ring buffer.

- **Layout.** A vacant-capable array `slots` whose length is always a power of two, an `int` `head` (index of the first element), an `int` `count`, and the modification count.
- **Capacity.** Starts at 8. `ArrayDeque<T>.with_capacity(n)` starts at the smallest power of two that is at least `max(8, n)`. When an insertion finds `count` equal to the capacity, the capacity doubles: allocate a new vacant array, copy the elements in logical order to indices 0 through `count - 1` with at most two bulk copies, and set `head` to 0. A capacity above 2^30 panics with `ArgumentError`. The capacity never shrinks.
- **Indexing.** Logical position `i` is at `(head + i)` masked with capacity minus one. `add_first` decrements `head` with the same mask.
- **Removal** makes the freed slot vacant so the element can be reclaimed.
- `remove(value)` and `remove_if` compact toward the front in one pass and vacate the freed tail slots.
- **Iteration** is first to last. `ArrayDeque` also offers `get(index) → T` by logical position. All end operations are amortized constant time.

### 9.5 `LinkedList<T>`

A doubly linked list stored in an arena, implementing `MutableList<T>` and `Deque<T>`. Use it when elements are frequently added or removed at both ends and also accessed as a list; otherwise prefer `ArrayList` or `ArrayDeque`.

- **Layout.** Three parallel arrays of equal length: `values` (vacant-capable), `next`, and `previous` (`int`), plus `head`, `tail`, `free` (each a node index or -1), `count`, `used` (the number of node indices ever handed out), and the modification count.
- **Nodes.** A node is an index. Allocation takes the head of the free list if there is one (the free list is threaded through `next`), otherwise index `used`, growing all three arrays by doubling from 8 when `used` equals their length. Freeing a node vacates its value and pushes its index on the free list.
- **Positional access** walks from the head when the index is in the first half and from the tail otherwise.
- **Iteration** follows `next` from `head`.

### 9.6 `HashMap<K, V>` and `HashSet<T>`

**Decision:** hash collections iterate in **insertion order**. A program's output never depends on hash values, which keeps behavior stable if a hash function changes and makes results independent of identity numbers. There is no separate linked hash map. **Decision:** the load factor is fixed and not configurable.

Both classes are built on one unexported class, `HashCore<K>`, which owns the keys and the index. `HashMap` adds a values array; `HashSet` adds nothing.

**Layout of `HashCore<K>`**

| Field | Type | Meaning |
| --- | --- | --- |
| `index` | `int` array of length `T` | Open-addressed table. Each slot holds an entry number, -1 (`Empty`), or -2 (`Tombstone`). `T` is a power of two, at least 8. |
| `keys` | Vacant-capable array of `K`, length `E` | Entry keys in insertion order. `E` is `T / 2`. |
| `hashes` | `uint32` array, length `E` | Each entry's spread hash, cached. |
| `live` | `bool` array, length `E` | False for a removed entry. |
| `used` | `int` | Entries appended so far, live and dead. |
| `count` | `int` | Live entries. |
| `modifications` | `uint32` | The modification count. |

`HashMap` holds a `HashCore<K>` and a vacant-capable array `values` of length `E`, indexed by entry number.

**Load factor.** At most half of the index slots are ever non-empty, because `E` is `T / 2` and each appended entry occupies exactly one slot. One half is chosen because lookups use linear probing: at half load an unsuccessful search inspects about 2.5 slots on average and a successful one about 1.5, and every inspected slot costs a comparison circuit evaluation. The index holds only integers, so the sparse table is cheap.

**Hashing.** The stored hash is `Hash.spread(key.hash_code())`. The home slot is that value masked with `T - 1`.

**Lookup.** Start at the home slot and inspect slots in increasing order, wrapping around:

1. `Empty`: the key is absent. Stop.
2. `Tombstone`: continue.
3. An entry number `e`: if `hashes[e]` equals the probe's hash and `key.equals(keys[e])`, the key is found at entry `e`. Otherwise continue.

`equals` is called on the **probe** key with the stored key as argument, and only when the cached hashes match. The loop terminates because at least half the slots are `Empty`.

**Insertion of a new key.** Run the lookup, remembering the first `Tombstone` slot passed. If the key is absent:

1. If `used` equals `E`, rebuild (below) and repeat the lookup.
2. Write the key, hash, `true`, and (for a map) the value at entry `used`.
3. Store `used` in the remembered tombstone slot, or in the `Empty` slot where the lookup stopped if there was none.
4. Increment `used`, `count`, and the modification count.

Putting a value for an existing key overwrites `values[e]` only: the entry keeps its position, and the modification count does not change.

**Removal.** Find entry `e` and its slot. Set the slot to `Tombstone`, set `live[e]` to false, vacate `keys[e]` and `values[e]`, decrement `count`, and advance the modification count. `used` does not change. A key removed and inserted again goes to the end of the order.

**Rebuild.** Triggered when a new key arrives and `used` equals `E`. If `count` is greater than `E / 2`, double `T`; otherwise keep `T` (the table is mostly dead entries, and rebuilding reclaims them). Allocate new arrays, copy live entries in order to entry numbers 0 upward, and place each in the new index by probing from its cached hash for the first `Empty` slot. No `hash_code` or `equals` call is made during a rebuild. A `T` above 2^30 panics with `ArgumentError`. The table never shrinks on its own; `trim()` rebuilds at the smallest `T` whose `E` holds `count`.

**Construction.** The constructor allocates `T` = 8. `with_capacity(n)` chooses the smallest power-of-two `T` that is at least `max(8, 2n)`, so `n` insertions cause no rebuild; a negative `n` or one above 2^29 panics with `ArgumentError`. `HashMap<K, V>.from(other: Map<K, V>)` and `HashSet<T>.from(items: Iterable<T>)` copy in iteration order.

**Iteration** visits entry numbers 0 through `used - 1` and skips dead entries, checking the modification count first.

**`clear`** refills the index with `Empty`, vacates the key and value slots below `used`, resets `used` and `count` to zero, keeps the capacity, and advances the modification count if the collection was not empty.

**Costs.** Lookup, insertion, and removal are expected constant time with well-distributed hashes and linear in `used` in the worst case. There is no conversion of long probe sequences into trees. Code that hashes adversarial keys uses `TreeMap`.

### 9.7 `TreeMap<K, V>` and `TreeSet<T>`

Sorted collections ordered by a `Comparator` given to the constructor. **Decision:** a comparator is always required; for a naturally ordered type pass `Comparators.natural`. Two keys are the same key when the comparator returns `Equal`; `equals` is not consulted.

**Decision:** the balanced tree is an **AVL tree**. Lookups dominate, each comparison is a call through a function value, and an AVL tree's height is at most about 1.44 log2 of the size against about 2 log2 for a red-black tree.

Both classes are built on one unexported class, `AvlCore<K>`; `TreeMap` adds a values array indexed by node.

**Layout of `AvlCore<K>`.** Parallel arrays of equal length: `keys` (vacant-capable), `left`, `right`, `parent` (`int`, -1 for none), and `height` (`int8`; a leaf has height 1 and a missing child counts as 0). Plus `root`, `free` (head of a free list threaded through `left`), `used`, `count`, the comparator, and the modification count. Nodes are allocated and freed as in `LinkedList`, with arrays doubling from 8.

- **Lookup** descends from the root, calling `comparator(probe, node key)`: `Less` goes left, `Greater` goes right, `Equal` stops.
- **Insertion** descends the same way. On `Equal`, a map overwrites the value (not structural) and a set does nothing. Otherwise it attaches a new leaf, then walks up through `parent`: recompute each ancestor's height; if its children's heights differ by 2, rebalance with a single rotation when the taller child leans the same way and a double rotation when it leans the other way; stop as soon as an ancestor's height is unchanged.
- **Removal.** A node with two children first takes the key and value of its in-order successor (the leftmost node of its right subtree), and the successor is removed instead. A node with at most one child is replaced by that child. Then walk up from the removed position, recomputing heights and rebalancing as above, until a height is unchanged. The node is freed.
- **Rotations** update `left`, `right`, and `parent` of the three nodes involved and the child link of the subtree's parent, or `root`.
- **Iteration** is ascending: start at the leftmost node and step to the in-order successor through `right` and `parent` links, with no stack. `descending` mirrors it. `range(from, to)` starts at the ceiling of `from` and stops at the first key not `Less` than `to`.
- `floor`, `ceiling`, `lower`, and `higher` are single descents that remember the best candidate.

`TreeMap` and `TreeSet` add `remove_first()` and `remove_last()`, returning `Optional` of the removed entry or element. Lookup, insertion, and removal take logarithmic time; a full iteration is linear.

### 9.8 `PriorityQueue<T>`

A binary min-heap implementing `Queue<T>`: `dequeue` returns a least element under the comparator given to the constructor.

- **Layout.** A built-in list holding the heap in array order (children of index `i` at `2i + 1` and `2i + 2`), the comparator, and the modification count.
- **Sift up** (after appending at the end): while the item is not at the root and `comparator(item, parent)` is `Less`, move the parent down.
- **Sift down:** pick the left child, or the right child when it exists and `comparator(right, left)` is `Less`; if `comparator(child, item)` is `Less`, move the child up and continue; otherwise stop.
- **`dequeue`** takes the root, moves the last element to the root, and sifts down.
- **`remove(value)`** finds the first `equals` element in array order, replaces it with the last element, sifts that down, and sifts it up if it did not move.
- **`PriorityQueue<T>.from(items, comparator)`** appends everything and then sifts down each index from `size / 2 - 1` to 0.
- **Iteration** is in heap array order, which is not sorted. `drain_sorted() → ArrayList<T>` dequeues everything in order.

These rules fix which of several equal elements comes out first, so results are deterministic; they are not first-in first-out among equals.

### 9.9 `BitSet`, `EnumSet<E>`, and `EnumMap<E, V>`

- **`BitSet`** implements `MutableSet<int>` for non-negative integers. Layout: a built-in `uint32` list of words (bit `i` is in word `i / 32`, at position `i % 32`) and a cached `count`. A negative element panics with `IndexError`. Setting a bit beyond the last word appends zero words. `add`, `remove`, `contains`, and `flip(i)` are constant time apart from growth. `union_with`, `intersect_with`, `subtract`, and `symmetric_difference_with` take another `BitSet` and work in place, word by word. `next_set(from) → int` returns the next member at or after `from`, or -1. Iteration is ascending. `count` is maintained with `count_ones`. Equality is set equality, so trailing zero words do not matter.
- **`EnumSet<E>`** (with `E` bounded by `Enum`) implements `MutableSet<E>` over the variants' ordinals with a fixed `uint32` array of `ceil(n / 32)` words, where `n` is the variant count. Iteration is in ordinal order. `EnumSet<E>.all()` and `EnumSet<E>.none()` are its factories; `complement()` returns a new set.
- **`EnumMap<E, V>`** implements `MutableMap<E, V>` with a vacant-capable array of `n` values and a `bool` array of presence flags, both indexed by ordinal. Every operation is constant time. Iteration is in ordinal order.

### 9.10 Immutable collections and array views

`ImmutableList<T>`, `ImmutableSet<T>`, and `ImmutableMap<K, V>` are closed classes that implement only the read-only interfaces and copy their contents at construction. They have no mutating members, so a downcast cannot reveal any. Immutable sets and maps use `HashCore` and keep insertion order.

The module `Collections` provides the factories:

| Member | Behavior |
| --- | --- |
| `list_of<T>(items: T[]) → List<T>` | An `ImmutableList` copy. |
| `set_of<T>(items: T[]) → Set<T>` | An `ImmutableSet`; later duplicates are ignored. |
| `map_of<K, V>(entries: Pair<K, V>[]) → Map<K, V>` | An `ImmutableMap`; a duplicate key panics with `ArgumentError`. |
| `empty_list<T>()`, `empty_set<T>()`, `empty_map<K, V>()` | |
| `copy_of_list<T>(items: Iterable<T>) → List<T>`, `copy_of_set`, `copy_of_map` | Immutable copies. |
| `array_view<T>(items: T[]) → List<T>` | A read-only `List` over the array without copying. Later writes to the array show through. |

Immutability is shallow: the elements themselves can still be mutable objects.

### 9.11 Algorithms

**Sorting.** `MutableList.sort` and `Arrays.sort` are stable and use the same algorithm:

1. Copy the elements into a working array (`Arrays.sort` works in the given array).
2. Sort each consecutive block of 16 elements (the last may be shorter) with binary insertion sort: for each element, binary-search the sorted prefix for the position after the last element not `Greater` than it, and shift the tail right by one with a bulk copy.
3. Merge bottom-up with one auxiliary vacant array of the same length. For widths 16, 32, 64, and so on while the width is less than the length, merge each adjacent pair of runs from the source array into the destination, then swap the roles of the two arrays. If the last element of the left run is not `Greater` than the first of the right run, the pair is copied without comparisons. When merging, take from the right run only when its head is `Less` than the left run's head; this is what makes the sort stable.
4. Write the result back to the list in index order (one `set` per element, so the list's modification count does not change).

The sort makes at most about `n log2 n` comparator calls and uses storage for `n` additional elements. If the comparator panics, the list keeps its original contents, since the list is written only in step 4; an array sorted in place is left as some permutation of its elements.

**Binary search.** `binary_search(value, c)` requires the list to be sorted by `c`. It finds the lowest index whose element is not `Less` than `value`. If that element compares `Equal`, it returns the index (the first of any equal run). Otherwise it returns `-(insertion point) - 1`.

**`Arrays`** module, for built-in arrays: `sort(array, c)`, `sort_range(array, start, end, c)`, `binary_search(array, value, c)`, `fill(array, value)`, `copy(array) → T[]`, `copy_range(array, start, end) → T[]`, `concat(a, b) → T[]`, `index_of(array, value) → int`, `contains`, `reverse(array)`, `swap(array, i, j)`, `to_list(array) → ArrayList<T>`, and `filled<T>(count, value) → T[]`.

### 9.12 `Sequence<T>`

A closed class for lazy pipelines, the counterpart of Java's streams. It is a class, not an interface, because several operations have their own type parameters and a generic method cannot be an interface requirement.

- **Layout.** One `Iterator<T>` and a `bool` `consumed`.
- **Single use.** Every operation, intermediate or terminal, marks the sequence consumed. A second operation on the same sequence panics with `StateError`. An intermediate operation returns a new `Sequence` wrapping an unexported adapter iterator.
- **Evaluation.** Pull-based and sequential. Nothing runs until a terminal operation, and each element passes through the whole pipeline before the next is requested. Function arguments are called in that order, exactly as often as needed.
- **Sources.** `Sequence<T>.from(items: Iterable<T>)`, `of(items: T[])`, `empty()`, `generate(supplier)` (infinite), `iterate(seed, step: UnaryOperator<T>)` (infinite: seed, then repeated application), and `Collection.sequence()`. The module `Sequences` adds `range(start, end) → Sequence<int>`.

| Intermediate | Behavior |
| --- | --- |
| `map<U>(f)` | |
| `filter(p)` | |
| `flat_map<U>(f: Function<T, Iterable<U>>)` | Concatenates the results. |
| `take(n)`, `skip(n)` | A negative `n` panics with `ArgumentError`. `take` stops pulling after `n` elements. |
| `take_while(p)`, `skip_while(p)` | |
| `enumerate() → Sequence<Pair<int, T>>` | Pairs each element with its position from 0. |
| `zip<U>(other: Iterable<U>) → Sequence<Pair<T, U>>` | Ends with the shorter input. |
| `chain(other: Iterable<T>)` | This sequence, then the other. |
| `peek(action)` | Calls `action` as each element passes. |
| `chunked(n) → Sequence<ArrayList<T>>` | Consecutive groups of `n`; the last may be shorter. `n` below 1 panics with `ArgumentError`. |
| `windowed(n) → Sequence<ArrayList<T>>` | Every run of `n` consecutive elements, each a fresh list. |
| `distinct()` | Keeps the first of each group of `equals` elements, using a `HashSet`. |
| `sorted(c)`, `reversed()` | Buffer the whole input when first pulled. `sorted` is stable. |

| Terminal | Behavior |
| --- | --- |
| `to_list() → ArrayList<T>`, `to_array() → T[]`, `to_set() → HashSet<T>` | |
| `to_map<K, V>(key, value) → HashMap<K, V>` | A duplicate key panics with `ArgumentError`. |
| `group_by<K>(key) → HashMap<K, ArrayList<T>>` | Groups in first-seen key order; elements keep their order. |
| `partition(p) → Pair<ArrayList<T>, ArrayList<T>>` | Matching elements first. |
| `fold<A>(initial: A, f: BiFunction<A, T, A>) → A` | Left to right. |
| `reduce(f: BinaryOperator<T>) → Optional<T>` | Empty for an empty sequence. |
| `count() → int` | Panics with `ArithmeticError` beyond the `int` range. |
| `any(p)`, `all(p)`, `none(p)` `→ bool` | Stop at the first deciding element. |
| `first()`, `last()`, `find(p)` `→ Optional<T>` | |
| `min(c)`, `max(c)` `→ Optional<T>` | The first of several equal extremes. |
| `for_each(action)` | |
| `join(separator) → string` | Joins the elements' `to_string` results. |
| `iterator() → Iterator<T>` | Hands out the underlying iterator; counts as the terminal operation. |

`Sequences` also provides `sum_int32`, `sum_int64` (checked; overflow panics), `sum_float64` (left-to-right addition, no compensation), and `average_float64 → Optional<float64>`, each taking a `Sequence` of that element type.

## 10. `Std.Math`

### 10.1 `Math`

`Math` works on `float64` only. Code using another float type converts to `float64`, calls `Math`, and converts back; this plan provides no per-format copies.

- **Constants:** `Pi`, `Tau`, `E`, `Ln2`, `Ln10`, `Sqrt2`, each the nearest `float64`.
- **`sqrt`** is correctly rounded. It unpacks the operand, computes the integer square root of the significand shifted to an even exponent, one result bit per step with a running remainder, and rounds to nearest, ties to even, using the remainder to decide. Negative operands other than `-0.0` give NaN; `sqrt(-0.0)` is `-0.0`.
- **Other functions:** `cbrt`, `hypot`, `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `to_radians`, and `to_degrees`.

**Decision:** these functions are a port of fdlibm 5.3, operation for operation, including its argument reduction, its polynomial coefficients (written as exact bit patterns through `Float64.from_bits`), and its special-case handling. fdlibm relies only on IEEE 754 double arithmetic with round-to-nearest and no fused or reassociated operations, which is exactly what [Numbers](Numbers.md) guarantees. The functions are therefore defined by their source: every runtime produces bit-identical results, with an error below one unit in the last place. Every NaN result is the canonical NaN. fdlibm's copyright notice must be kept in the ported files, following the third-party rule in [CONTRIBUTING](../../CONTRIBUTING.md).

### 10.2 `BigInteger`

A closed, immutable arbitrary-precision integer.

- **Layout.** `sign` (`int`: -1, 0, or 1) and `magnitude`, a `uint32` array of limbs, least significant first, with no zero limb at the top. Zero has sign 0 and an empty magnitude. This form is unique, so equality compares the fields directly.
- **Limits.** A magnitude has at most 2^26 limbs; a larger result panics with `ArithmeticError`.
- **Constants and factories.** `Zero`, `One`, `Ten`; `of_int64`, `of_uint64`; `parse(text)` and `parse_radix(text, radix)` with the grammar of [section 7.1](#71-integer-modules) and no range failure.

| Operation | Algorithm |
| --- | --- |
| `add`, `subtract` | Limb-wise with carry or borrow in `uint64`; signs handled by comparing magnitudes. |
| `multiply` | Schoolbook with `uint64` partial products. When both operands have at least 48 limbs, Karatsuba: split at half of the larger length and recurse. |
| `divide`, `remainder`, `divide_and_remainder → Pair` | Knuth's Algorithm D in base 2^32. The quotient truncates toward zero and the remainder takes the dividend's sign, matching the integer operators. A single-limb divisor uses the short division loop. A zero divisor panics with `ArithmeticError`. |
| `floor_mod(m)` | Non-negative result for a positive modulus. |
| `pow(exponent: int)` | Square and multiply. A negative exponent panics with `ArgumentError`. |
| `gcd(other)` | Euclid's algorithm with `remainder`; the result is non-negative. |
| `mod_pow(exponent, modulus)` | Left-to-right binary exponentiation, reducing after each step. Requires a non-negative exponent and a positive modulus. |
| `mod_inverse(modulus) → Optional<BigInteger>` | Extended Euclid; empty when no inverse exists. |
| `negate`, `abs`, `sign`, `is_zero`, `is_even` | |
| `shift_left(n)`, `shift_right(n)` | `shift_right` rounds toward negative infinity. A negative `n` panics with `ArgumentError`. |
| `bit_length() → int`, `test_bit(n) → bool` | Of the magnitude. |
| `compare_to`, `equals`, `hash_code` | Hash: start at 0, combine each limb from most to least significant, then negate (wrapping) for a negative value. |
| `to_int64() → Optional<int64>`, `to_float64() → float64` | `to_float64` rounds to nearest, ties to even, and gives an infinity beyond the range. |
| `to_string`, `to_string_radix(radix)` | Radix 10 divides repeatedly by 10^9 and prints nine-digit groups. |

The limb loops follow the same algorithms as `WideUint` ([section 7.4](#74-float-text-conversion)). Bitwise and, or, and exclusive or on big integers are not in the first version.

### 10.3 `BigDecimal`

A closed, immutable decimal number: an unscaled `BigInteger` and an `int` `scale`, representing unscaled × 10^(-scale).

- **Decision:** `equals` and `compare_to` are numeric, so `2.0` equals `2.00`. `hash_code` is computed on the form with trailing zeros stripped. This avoids Java's inconsistency between the two.
- `add` and `subtract` give the larger scale; `multiply` gives the sum of the scales; all are exact.
- **Decision:** division always takes an explicit result scale and `RoundingMode`: `divide(other, scale, mode)`. There is no exact division that could fail on a non-terminating quotient.
- `RoundingMode`, in order: `Up`, `Down`, `Ceiling`, `Floor`, `HalfUp`, `HalfDown`, `HalfEven`, with Java's meanings.
- Other members: `set_scale(scale, mode)`, `round(precision, mode)` (significant digits), `strip_trailing_zeros`, `negate`, `abs`, `sign`, `pow(exponent: int)` (non-negative), `to_big_integer(mode)`, `to_float64()` (correctly rounded), `of_float64(v)` (the exact binary value; NaN and infinities panic with `ArgumentError`), `of_int64`, `of_big_integer(value, scale)`, and `parse(text)` (the float grammar of section 7.4 without the special names).
- `to_string` always uses plain notation with exactly `scale` fraction digits when the scale is positive, and appends zeros for a negative scale. It never uses an exponent.
- A scale that leaves the `int` range panics with `ArithmeticError`.

## 11. Input and output

### 11.1 `Std.Io`

The stream interfaces are declared in `Std.Core`, so that `StringBuilder` can implement `TextWriter`, and are re-exported by `Std.Io`.

| Interface | Requirements | Defaults |
| --- | --- | --- |
| `ByteReader` | `read(buffer: uint8[], offset: int, count: int) → int or Error`: reads at least 1 and at most `count` bytes and returns how many, or returns 0 at the end of the stream. `count` below 1 or a range outside the buffer panics. | `read_exact(buffer, offset, count) → void or Error` (fails with kind `UnexpectedEnd`); `read_all() → uint8[] or Error`; `skip(count: int64) → int64 or Error` |
| `ByteWriter` | `write(buffer: uint8[], offset: int, count: int) → void or Error`: writes every byte, repeating partial host writes. `flush() → void or Error`. | `write_all(bytes: uint8[])`; `write_byte(value: uint8)` |
| `Seekable` | `seek(offset: int64, origin: SeekOrigin) → int64 or Error`, returning the new position. `SeekOrigin`: `Start`, `Current`, `End`. | `position() → int64 or Error` |
| `TextReader` | `read_char() → Optional<char> or Error`; empty at the end. | `read_line() → Optional<string> or Error`; `read_to_end() → string or Error` |
| `TextWriter` | `write_string(s) → void or Error`; `flush() → void or Error`. | `write_char(c)`; `write_line(s)` (the text, then one LF) |

`read_line` ends a line at LF and removes it, along with a CR immediately before it. A final line without an LF is returned; after that the result is empty.

| Class | Behavior |
| --- | --- |
| `ByteBuffer` | An in-memory `ByteWriter` over a built-in `uint8` list. `to_array()`, `size()`, `clear()`, and `reader() → ByteReader` over a snapshot. Never fails. |
| `ByteArrayReader` | A `ByteReader` and `Seekable` over a `uint8` array. |
| `BufferedReader` | Wraps a `ByteReader` with a buffer of 8,192 bytes (configurable, at least 16). A request at least as large as the buffer bypasses it when the buffer is empty. |
| `BufferedWriter` | Wraps a `ByteWriter` with a buffer of 8,192 bytes. Writes to the underlying writer when the buffer is full and on `flush`. `close` flushes, then closes the underlying writer if it is `Closeable`. |
| `Utf8Reader` | A `TextReader` over a `ByteReader`, with its own 8,192-byte buffer. Invalid UTF-8 fails with `IoError` of kind `InvalidData`; there is no lossy mode. |
| `Utf8Writer` | A `TextWriter` over a `ByteWriter`. |
| `StringReader` | A `TextReader` over a string. |

**`Console`** is a module:

- `out() → TextWriter`, `error() → TextWriter`, and `input() → TextReader` return shared stream objects.
- **Decision:** console output is unbuffered in the library. Each write is one host request, so output order is exactly program order and nothing is lost when a run ends by fault. Programs that print heavily build text with `StringBuilder` first.
- `print(s)`, `print_line(s)`, and `error_line(s)` panic with `IoError` if the host write fails. Code that must handle a failed write uses `out()` and checks the error.
- `read_line() → Optional<string> or Error` reads from `input()`.

### 11.2 `Std.Fs`

**`Path`** is a closed, immutable value holding normalized text.

- The separator is `/` on every host. The host service translates to the platform's form.
- **Normalization at construction:** repeated separators collapse; `.` components are removed; a trailing separator is removed except for the root; an empty result is `.`. `..` components are kept, because removing them is not correct in the presence of links.
- `is_absolute()` is true exactly when the text starts with `/`. Platform-specific prefixes such as drive letters are passed to the host unchanged and are not interpreted by `Path`.
- Members: `Path.of(text)`, `join(other: string) → Path` (an absolute `other` replaces the path), `parent() → Optional<Path>`, `file_name() → Optional<string>`, `stem() → Optional<string>`, `extension() → Optional<string>` (the text after the last `.` of the file name, when that `.` is not the first character), `components() → ArrayList<string>`, `resolve_lexically() → Path` (removes `..` against preceding components), `starts_with(other: Path) → bool`, `relative_to(base: Path) → Optional<Path>`, and `to_string()`. Equality, hashing, and ordering use the normalized text.

**`File`** is a closed class implementing `ByteReader`, `ByteWriter`, `Seekable`, and `Closeable`. `File.open(path, mode: OpenMode) → File or Error`. `OpenMode`: `Read` (must exist), `Write` (create or truncate), `Append` (create; writes go to the end), `ReadWrite` (must exist), `CreateNew` (fails with `AlreadyExists`). A `File` is unbuffered; wrap it for small reads and writes.

**`Fs`** module:

| Member | Behavior |
| --- | --- |
| `read_bytes(path) → uint8[] or Error`, `read_text(path) → string or Error` | `read_text` fails with `InvalidData` for invalid UTF-8. |
| `write_bytes(path, bytes)`, `write_text(path, text)`, `append_text(path, text)` | Create the file if needed; `write_` forms truncate. |
| `exists(path) → bool or Error` | False for `NotFound`; other failures are errors. |
| `metadata(path) → FileMetadata or Error` | `kind` (`FileKind`: `File`, `Directory`, `Other`), `size: int64`, `modified: Instant`. |
| `list(path) → ArrayList<Path> or Error` | The directory's entries joined to `path`, sorted by name bytes. |
| `create_directory(path)`, `create_directories(path)` | The second creates missing parents and succeeds when the directory exists. |
| `remove_file(path)`, `remove_directory(path)` | `remove_directory` requires an empty directory. |
| `remove_tree(path)` | Removes depth first, in sorted order. It removes an entry of kind `Other` without descending into it. Stops at the first error. |
| `rename(from, to)`, `copy_file(from, to)` | `copy_file` streams through a 65,536-byte buffer and replaces an existing target. |

### 11.3 `Std.System`

- Program arguments arrive only through the entry function's parameter. The library adds no global accessor.
- `Environment.get(name) → Optional<string> or Error`.
- `Process.exit(status: int)` ends the run with that status. It does not return, and it runs no deferred actions. A status outside 0 through 255 panics with `ArgumentError`.

### 11.4 `Std.Net`

Blocking TCP only.

- `TcpStream.connect(host: string, port: int, timeout: Duration) → TcpStream or Error`. The host resolves names. `TcpStream` implements `ByteReader`, `ByteWriter`, and `Closeable`, and has `set_read_timeout(timeout: Optional<Duration>)`; an expired timeout fails with kind `TimedOut`.
- `TcpListener.bind(host: string, port: int) → TcpListener or Error`; `accept() → TcpStream or Error`; `local_port() → int`; `close()`. Port 0 asks the host for a free port.
- A port outside 0 through 65,535 panics with `ArgumentError`.
- Service arguments: `connect` takes the host text, the port, and the timeout in nanoseconds; `listen` takes the host text and port; `accept` takes a listener ID; `read` takes a socket ID, a maximum count, and a timeout in nanoseconds (0 for none); `write` takes a socket ID and bytes; `close` takes either kind of ID.

UDP, TLS, HTTP, and name-resolution APIs are not in the first version.

## 12. Time and randomness

### 12.1 `Std.Time`

All types are closed, immutable, and `Comparable`.

**`Duration`** is a signed count of nanoseconds in one `int64` (about ±292 years).

- Factories: `of_nanos`, `of_micros`, `of_millis`, `of_seconds`, `of_minutes`, `of_hours`, `of_days` (24 hours), each taking `int64`; and the constant `Zero`. Overflow panics with `ArithmeticError`, as it does for every operation below.
- Members: `plus`, `minus`, `multiplied_by(int64)`, `divided_by(int64)` (truncating), `negated`, `abs`, `is_zero`, `is_negative`, `to_nanos`, `to_micros`, `to_millis`, `to_seconds` (each truncating toward zero), and `to_seconds_float64`.
- `to_string` is ISO 8601: `PT`, then hours, minutes, and seconds with their unit letters, omitting zero units, with a fractional second trimmed of trailing zeros (`PT1H30M`, `PT0.5S`). Zero is `PT0S`. A negative duration is `-` followed by the text of its absolute value. `Duration.parse(text) → Duration or Error` accepts exactly this form.

**`Instant`** is a point on the UTC time line: `int64` seconds since 1970-01-01T00:00:00Z and an `int` nanosecond adjustment from 0 through 999,999,999. Leap seconds are ignored, as in POSIX time.

- `Instant.of_epoch_seconds(seconds, nanos)`, `of_epoch_millis`, the constant `Epoch`; `plus(Duration)`, `minus(Duration)`, `duration_since(other) → Duration`, `epoch_seconds()`, `nanos()`, `to_epoch_millis()`, `to_local(offset: ZoneOffset) → LocalDateTime`.
- `to_string` is `YYYY-MM-DDTHH:MM:SS` with a fraction of 3, 6, or 9 digits when the nanoseconds are not zero (the shortest of those that is exact), then `Z`. `Instant.parse` accepts that form and a numeric offset in place of `Z`.

**`Clock`** is an interface: `now() → Instant or Error`, `monotonic() → Duration or Error` (elapsed time from an arbitrary fixed origin; never decreases), and `sleep(duration) → void or Error`.

- `SystemClock` uses the clock host services and fails with `CapabilityDenied` when they are not allowed.
- `FixedClock` holds an instant set at construction; `set(instant)` and `advance(duration)` change it; `sleep` advances it and returns at once; it never fails. Library code that needs time takes a `Clock` parameter, so tests pass a `FixedClock`.
- `Stopwatch` takes a `Clock` and offers `start`, `stop`, `reset`, and `elapsed() → Duration or Error`.

**Calendar types** use the proleptic Gregorian calendar.

- `LocalDate`: `year` (-999,999 through 999,999), `month` (1 through 12), `day`. `LocalDate.of(year, month, day)` panics with `ArgumentError` for an invalid date; `try_of` returns `Optional`. Conversion to and from a day count since 1970-01-01 (`to_epoch_day`, `of_epoch_day`) uses the 400-year cycle of 146,097 days: shift the year so that it starts in March, split it into the cycle and the year within the cycle, and compute the day of the cycle from the year and the day of the March-based year, using floor division throughout so that negative years work. Members: `plus_days`, `plus_months` and `plus_years` (which clamp the day to the end of the target month), `days_until(other) → int64`, `day_of_week() → DayOfWeek` (`Monday` through `Sunday`; epoch day 0 is a Thursday), `day_of_year()`, `is_leap_year()`, `length_of_month()`. Text form `YYYY-MM-DD`; years outside 0000 through 9999 are written with a sign and at least four digits.
- `LocalTime`: `hour`, `minute`, `second`, `nano`, stored as nanoseconds of the day. `plus(Duration)` wraps around midnight. Text form `HH:MM:SS` with the same fraction rule as `Instant`.
- `LocalDateTime`: a `LocalDate` and a `LocalTime`. `plus(Duration)` carries into the date. `at_offset(offset) → Instant`. Text form: the date, `T`, the time.
- `ZoneOffset`: total seconds from -64,800 through 64,800. `ZoneOffset.Utc`, `of_hours_minutes`, text form `Z` or `±HH:MM`.
- Each type has `parse(text) → T or Error` for its text form.

**Decision:** there is no time zone database, no named zones, and no daylight-saving rules in the first version. Conversions between instants and local times take an explicit `ZoneOffset`. Pattern-based and localized formatting is also out; the ISO forms above are the only text forms.

### 12.2 `Std.Random`

**`Random`** is a closed class implementing the xoshiro256** generator, chosen because each step needs only shifts, rotations, exclusive ors, and multiplications by 5 and 9, which are short adder chains in a circuit.

- **State.** Four `uint64` words `s0` through `s3`.
- **Step.** The output is `rotate_left(s1 × 5, 7) × 9`, wrapping. Then, with `t` = `s1` shifted left by 17: `s2 ^= s0`; `s3 ^= s1`; `s1 ^= s2`; `s0 ^= s3`; `s2 ^= t`; `s3 = rotate_left(s3, 45)`.
- **Seeding.** `Random.seeded(seed: uint64)` fills the four words with successive outputs of SplitMix64 started at the seed: add 0x9E3779B97F4A7C15 to the state; take `z` as the new state; `z = (z ^ (z >> 30)) × 0xBF58476D1CE4E5B9`; `z = (z ^ (z >> 27)) × 0x94D049BB133111EB`; output `z ^ (z >> 31)`; all wrapping. `Random.from_entropy() → Random or Error` requests 8 bytes from `neura.entropy.fill`, reads them as a little-endian `uint64`, and seeds with that.

| Member | Behavior |
| --- | --- |
| `next_uint64() → uint64` | One step. |
| `next_uint32() → uint32` | The high 32 bits of one step. |
| `next_bool() → bool` | The top bit of one step. |
| `next_int_below(bound: int) → int` | Uniform in 0 up to but excluding `bound`. Take the smallest all-ones mask covering `bound - 1`; draw `next_uint32` masked until the result is below `bound`. A bound below 1 panics with `ArgumentError`. |
| `next_int_between(low: int, high: int) → int` | Uniform from `low` up to but excluding `high`. The span is computed in `int64`, sampled with the same mask-and-reject loop on `uint32`, and added to `low` in `int64`. `low` not below `high` panics with `ArgumentError`. |
| `next_float64() → float64` | The top 53 bits of one step times 2^-53: uniform in 0 up to but excluding 1. |
| `next_gaussian() → float64` | Marsaglia's polar method. Each accepted pair yields two values; the second is cached in the generator and returned by the next call. |
| `fill_bytes(buffer: uint8[])` | Each step supplies 8 bytes, least significant first; a final partial group uses the low bytes. |
| `shuffle<T>(list: MutableList<T>)` | Fisher–Yates: for `i` from the last index down to 1, swap `i` with `next_int_below(i + 1)`. |
| `choose<T>(list: List<T>) → T` | Panics with `NoSuchElementError` for an empty list. |

The same seed gives the same sequence of results from every member, on every runtime, in every library version with the same major number. `Random` is not suitable for secrets, and the library offers no cryptographic generator.

**`Uuid`** is a closed, immutable value of 16 bytes. `Uuid.random(random: Random)` makes a version 4 identifier from `fill_bytes`, then sets the version and variant bits. `Uuid.parse(text) → Uuid or Error` accepts the 36-character hyphenated form in either case; `to_string` writes it in lowercase. It is `Comparable` by its bytes.

## 13. Data formats

### 13.1 `Std.Encoding`

- **`Utf8`:** `encode(s) → uint8[]`; `decode(bytes) → string or Error`; `decode_lossy(bytes) → string`; `validate(bytes) → int` (the offset of the first invalid byte, or -1).
- **`Hex`:** `encode(bytes) → string` (lowercase); `decode(text) → uint8[] or Error` (either case; an odd length or a non-hex character fails with `EncodingError`).
- **`Base64`:** the standard and URL-safe alphabets of RFC 4648. `encode`, `encode_url`, and their `_unpadded` forms; `decode` and `decode_url`. Decoding is strict: padded forms require correct padding, unpadded forms reject padding, whitespace is rejected, and unused trailing bits must be zero.
- **`Csv`** (RFC 4180): `parse(text) → ArrayList<ArrayList<string>> or Error` and `write(rows) → string`. Fields are separated by commas and records by LF or CRLF. A quoted field may contain commas, line breaks, and doubled quotes. A quote inside an unquoted field, or text after a closing quote, fails with `ParseError`. A final line break does not add an empty record. The writer quotes a field only when it contains a comma, a quote, a CR, or an LF, and ends each record with LF.

**`Json`** (RFC 8259).

- **Model.** `JsonValue` is an interface with `kind() → JsonKind` (`Null`, `Bool`, `Number`, `String`, `Array`, `Object`) and the accessors `as_bool`, `as_number`, `as_string`, `as_array`, `as_object`, each returning `Optional`. Six closed classes implement it: `JsonNull` (one instance), `JsonBool`, `JsonNumber`, `JsonString`, `JsonArray` (wrapping an `ArrayList<JsonValue>`), and `JsonObject` (wrapping a `HashMap<string, JsonValue>`, so members keep insertion order). Arrays and objects are mutable through their wrapped collections.
- **Numbers.** `JsonNumber` keeps the number's validated text. `as_int64() → Optional<int64>` is present when the text has no fraction or exponent and fits. `as_float64() → float64` is the nearest value. `to_big_decimal()` is exact. `JsonNumber.of_int64` and `of_float64` build numbers; `of_float64` panics with `ArgumentError` for NaN and infinities. Two numbers are `equals` when their exact decimal values are equal.
- **Parsing.** `Json.parse(text) → JsonValue or Error` is a strict recursive-descent parser: no comments, no trailing commas, no leading zeros, only the four JSON whitespace characters. String escapes include `\uXXXX`, with surrogate pairs combined; an unpaired surrogate fails. Nesting deeper than 512 fails. For a repeated member name, the last value wins and the name keeps its first position. Anything but whitespace after the value fails. Errors are `ParseError` with the byte offset.
- **Writing.** `Json.write(value) → string` produces no whitespace. `Json.write_pretty(value, indent: int)` puts each element and member on its own line, indented by `indent` spaces per level, with `: ` after names; empty arrays and objects are `[]` and `{}`. Strings escape `"`, `\`, and characters below 0x20 (using `\b`, `\f`, `\n`, `\r`, `\t`, and otherwise `\u00XX` with lowercase hex). Nothing else is escaped. Numbers are written as their stored text.
- `equals` and `hash_code` are structural for all six classes. `to_string` is `Json.write`.

### 13.2 `Std.Digest`

- **`Crc32`:** the IEEE 802.3 checksum (reflected polynomial 0xEDB88320, initial value and final exclusive-or of all ones). A 256-entry table is computed on first use. `update(bytes, offset, count)`, `value() → uint32`, `reset()`, and `Crc32.of(bytes) → uint32`.
- **`Sha256`:** FIPS 180-4. `update(bytes, offset, count)`, `finish() → uint8[]` (32 bytes; the object cannot be updated afterwards and a later call panics with `StateError`), and `Sha256.of(bytes) → uint8[]`.

These are for integrity checks and content addressing. The library makes no claim about timing behavior.

## 14. Diagnostics

### 14.1 `Std.Log`

- `LogLevel`, in order: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Off`.
- `Logger` is a closed class built from a name, a `TextWriter` sink, a minimum level, and an `Optional<Clock>`.
- Each accepted record is written as one line: the instant's `to_string` and a space when there is a clock, then the level in uppercase, a space, the name, `: `, and the message.
- **Decision:** with no clock there is no timestamp, so log output is deterministic by default.
- Members: `trace`, `debug`, `info`, `warn`, `error` (each taking a message), `log(level, message)`, `log_lazy(level, supplier: Supplier<string>)` (the supplier runs only when the level is enabled), `is_enabled(level)`, `set_level(level)`, and `dropped_count() → int`.
- **Decision:** a failed write to the sink, or a failed clock read, never fails the caller. The record is dropped and counted.
- **Decision:** there is no global logger registry. A logger is passed to the code that uses it.

### 14.2 `Std.Test`

Assertions are library functions that panic with `AssertionError`. The test runner already reports a failing test's diagnostic; with E3 that diagnostic is the error's `to_string`.

| Member | Failure message |
| --- | --- |
| `assert(condition: bool)` | `assertion failed` |
| `assert_with(condition: bool, message: string)` | The message |
| `assert_equal<T>(expected: T, actual: T)` | `expected <` expected `>, got <` actual `>`, using `equals` and `to_string` |
| `assert_not_equal<T>(unexpected: T, actual: T)` | `expected a value other than <` value `>` |
| `assert_same(expected: Object, actual: Object)` | Identity comparison |
| `assert_float_near(expected: float64, actual: float64, tolerance: float64)` | Fails when the absolute difference exceeds the tolerance or either value is NaN |
| `assert_present<T>(value: Optional<T>) → T`, `assert_empty<T>(value: Optional<T>)` | |
| `assert_ok<T>(result: Result<T>) → T`, `assert_err<T>(result: Result<T>) → Error` | |
| `assert_panics(action: Action) → Error` | Runs the action, recovers its panic, and returns the error. Fails when the action returns normally. |
| `fail(message: string)` | The message |

`assert_panics` cannot observe run-limit faults (E6). The existing `Test.assert` and `Test.assert_equal_int32` keep their host services until [Testing](Testing.md) is revised to retire them.

## 15. Deferred and excluded

| Area | Status | Reason |
| --- | --- | --- |
| Concurrency: threads, executors, futures, locks, atomics, concurrent collections | Deferred. `Std.Concurrent` is reserved. | The language defers concurrency ([Resources and concurrency](ResourcesConcurrency.md)). No library type on this page is safe for concurrent use, and none claims to be. |
| Reflection, runtime class loading, serialization of arbitrary objects | Excluded | Specialization and reachability-based emission leave no general runtime type model. |
| Finalizers, weak references | Excluded | Reclamation is unobservable by design (L4). |
| Time zones, locales, localized formatting, collation | Deferred | They need large versioned data tables. |
| Unicode normalization, non-ASCII case mapping and classification | Deferred to `Std.Text.Unicode` | Same reason. |
| Pattern formatting (`printf`-style) | Excluded | Needs variadic or heterogeneous arguments. `StringBuilder` and S2 cover the use. |
| Ciphers, signatures, TLS, secure random, HTTP | Deferred | They need review beyond this plan. |
| `BigInteger` bitwise operations, UDP, named groups of host capabilities beyond section 4.5 | Deferred | Not needed for the first version. |

## 16. Implementation phases

Each phase lands in order. A phase is finished when its exit criteria hold and [Status](Status.md) lists its modules as supported.

| Phase | Work | Exit criteria |
| --- | --- | --- |
| 0. Language | The blockers, in this order: L11, L13, L6, L7, L5, L4, L8, L1, L2, L9, L10, L3, L12, then the error model (E1 through E6). | Each has a language page, accepted and rejected examples, and end-to-end tests. `stdlib/` exists with an empty `Std.Core` that every program imports. |
| 1. Core | `Object` and universal members (float `to_string` arrives in phase 2), `Hash`, `Ordering`, `Comparable`, `Comparators`, iteration interfaces, function shapes, `Optional`, `Result`, `Error` and all error classes, the small value types, the integer modules, `Bool`, `Char`, and the `assert`, `assert_with`, `assert_equal`, and `fail` members of `Std.Test`. | Every table row of sections 5 through 7.1 and 7.3 has a test. Hello-world's image is no larger than before this phase (reachability). |
| 2. Numbers as text | `StringBuilder`, `WideUint`, float text conversion, the float modules. | Exhaustive `float16` and `bfloat16` round trips pass. |
| 3. Collections A | Interfaces, `ArrayList`, `Arrays`, sorting and binary search, `ArrayDeque`, `HashCore`, `HashMap`, `HashSet`, immutable collections, `Sequence`. | Model-based tests ([section 17](#17-verification)) pass for each structure. |
| 4. Text and collections B | `Strings`, `Chars`, `AvlCore`, `TreeMap`, `TreeSet`, `PriorityQueue`, `LinkedList`, `BitSet`, `EnumSet`, `EnumMap`. | Same, plus invariant validation after every operation in the model tests. |
| 5. Math | `Math`, `BigInteger`, `BigDecimal`. | `Math` matches the reference vectors bit for bit. |
| 6. Streams | Stream interfaces and classes, `Console`, capability flags, record and replay, the rest of `Std.Test`; `SysIo.println` and `Test` forwarded. | Existing examples and tests pass unchanged. |
| 7. System | `Std.Time`, `Std.Random`, `Std.Fs`, `Std.System`. | File tests pass under replay and against a temporary directory. |
| 8. Formats | `Std.Encoding`, `Std.Digest`, `Regex`, `Std.Log`. | Published test vectors pass (RFC 4648, FIPS 180-4, the JSON conformance cases). |
| 9. Network | `Std.Net`. | Loopback tests pass under replay. |

Within a phase, a module is written in this order: interfaces and error cases, then the implementation, then tests, then the documentation page under `docs/lang/` and an example under `examples/`.

## 17. Verification

These requirements apply the repository's rules in [AGENTS.md](../../AGENTS.md) to the library.

- **Where tests live.** Library tests are Neura test files under `tests/stdlib/<module>/`, run by `neura test` through CMake. Programs with expected output go beside them as `.nra` and `.out` pairs, like `tests/backend/programs`.
- **Coverage rule.** Every member in a table on this page has at least one test of its normal result and one test of each documented panic or error. A test of a panic uses `assert_panics` and checks the error's class with a type test.
- **Both build modes.** Every library test runs optimized and with `--no-optimize`, and the outputs must be identical. This is the check that identity numbers, iteration order, and hash-dependent behavior survive optimization.
- **Reference evaluator.** The test programs also run under the IR reference evaluator, and the results must match NVM's.
- **Determinism.** Each test program runs twice from the same image; output and step counts must be identical.
- **Model-based tests.** For every collection, a test drives the collection and a trivially correct model (an association list or a plain built-in list) with the same sequence of at least 10,000 operations drawn from `Random.seeded` with a fixed seed, and compares size, contents, iteration order, and return values after every operation.
- **Invariant validation.** `HashCore`, `AvlCore`, `ArrayDeque`, `LinkedList`, and `PriorityQueue` each export a `validate()` member from their child module that checks every structural invariant in this plan (index slots agree with entries, AVL balance and parent links, heap order, free-list integrity). The facade modules do not re-export it. Tests import the child module and call it after every operation.
- **Fail-fast.** For each collection, a test modifies it during for-each and during a `Sequence` pipeline and expects `ConcurrentModificationError`.
- **Contract text.** `to_string` output, hash codes of built-in types, and float formatting are compared against literal expected values, because they are part of the contract.
- **Numeric vectors.** `Math` is tested against bit patterns generated once from a reference fdlibm build and checked in. Float parsing and formatting use a checked-in corpus that includes boundary cases: halfway values, subnormals, the largest finite values, and powers of two and ten.
- **Host services.** Each service has a test that replays a recorded trace and a test with the capability denied.
- **Decompiler.** A library-using image must round-trip through `disassemble` and `assemble` byte for byte, and `decompile` must run on it. No stronger recovery claim is made for library code.
- **Cost baseline.** A small benchmark set (insert and look up 1,000 map entries, sort 1,000 integers, format 100 floats, parse a 10 KB JSON text) records NVM steps, neuron count, and image size in a checked-in baseline. A change that raises any figure by more than 10% must say why in its pull request.

A completion summary for any phase names the stages touched (for a pure library change: none beyond the library source; for a blocker: each stage), lists the commands that ran, and separates tested behavior from behavior that was only inspected.
