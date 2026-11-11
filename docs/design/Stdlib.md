# Standard library: design notes

This page records why the standard library is designed as it is, for contributors: the probes that established the language support the library relies on, how the first design's blockers were resolved, the names that changed, and the order in which the modules were built. [Standard library specification](../lang/Stdlib.md) is authoritative for behavior: module contents, member contracts, and limits are defined there, and this page adds no rule to them. The identifiers (L1 to L13, G1 to G5, A1 to A7) are kept so that earlier discussion stays searchable.

## What the probes established

The compiler accepts generic callbacks, generic enums, interface declarations, type tests, and string operations. These fixtures exercise the corresponding behavior through source, optimized and unoptimized NVR builds, and execution. The supported API contracts and remaining restrictions are in [Status](../lang/Status.md) and the language pages.

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

## Resolution of the first design's blockers

| First design | Current design |
| --- | --- |
| L1 Interfaces, L12 iterable for-each | [G4](../lang/Stdlib.md#g4-interfaces) and [G5](../lang/Stdlib.md#g5-for-each-over-library-collections) supply the language machinery. The collection interfaces and adapters still need source implementations. |
| L2 Universal members and an `Object` root | [G3](../lang/Stdlib.md#g3-universal-members) supplies operations for supported values and an explicit class-reference root spelled `class`. Scalar values are not boxed into it. |
| L3 Function types and closures | [G1](../lang/Stdlib.md#g1-function-types-over-type-parameters) permits concrete and generic callback types. |
| L4 Automatic reclamation | [NVM](../runtime/NVM.md#garbage-collection) supplies deterministic tracing and reclaims cycles. Host resources still require explicit close. |
| L5 Vacant storage and storage operations | Runtime-sized arrays, clearing vacant slots, and bulk copy are unnecessary for the chosen layouts. Lists supply growing storage and `nil` supplies absent node links. Optional additions (below) |
| L6 Bitwise operators and wrapping arithmetic | Checked shifts and bitwise operators are available. [Arithmetic recipes](../lang/Stdlib.md#72-arithmetic-without-wrapping-intrinsics) implement modular operations without changing ordinary checked arithmetic. [N1](../lang/Stdlib.md#n1-float-bit-access) supplies exact float bit access. |
| L7 `char` and string semantics | [T1](../lang/Stdlib.md#t1-text-primitives) supplies immutable UTF-8 text primitives. [Scalars](../lang/Stdlib.md#83-scalars) represent code points as `uint32`. |
| L8 Class-level members and closed classes | Type modules supply factories. No class-level members are required. |
| L9 Type tests and downcasts | Class and interface references support `is` and checked `as`. Error outcomes still use exhaustive matching, and collection APIs compare their typed values directly. |
| L10 Enum intrinsics | Fieldless ordinals and variant text are available. Ordinal collections use integer keys. A payload enum's discriminant does not enumerate its values, and no reverse ordinal lookup or reflection is required. |
| L11 Packaging | [P1](../lang/Stdlib.md#p1-library-root) reserves the installed standard-library namespace. Imports stay explicit and facade re-exports are optional. |
| L13 Host and intrinsic declarations | [P2](../lang/Stdlib.md#p2-host-bindings-in-source) supplies named and versioned host bindings in trusted source. Text, float bit access, and message traps are ordinary language primitives. |
| E1–E6 Error handling | Returned failures are `Result<T>`, with one active case. Runtime traps keep their reason codes and cannot be caught. Message traps carry reason 3. Unwinding, defer, and recovery are outside the design. |

## Optional additions

None of these is needed by the library. Each would replace a loop of storage operations with one, or save typing. Hexadecimal integer literals (A5) and inference of type arguments (A7) are not part of the language.

| ID | Addition | Replaces | Benefit |
| --- | --- | --- | --- |
| A1 | Allocate a list of a runtime length filled with one value | A loop of `append` | Hash index tables and sort buffers are filled in one operation. |
| A2 | List insert at an index, and truncate to a length | `append` then shifting by element writes; repeated `remove_at` of the last element | `add_at`, `clear`, and `truncate` cost one operation instead of one per element. |
| A3 | Bulk copy of a range between lists | Element-by-element reads and writes | Sorting merges and deque rebalancing. |
| A4 | Read of a list's structural version, implemented as `list.version` | A modification count kept by the wrapper class | Iterator objects detect modification made through any backing-list alias. [ArrayList](../lang/Stdlib.md#93-arraylistt) |
| A5 | Hexadecimal integer literals | Decimal constants | Readability of masks and published constants. |
| A6 | Re-export of generic classes, generic functions, and payload enums | Importing each type's own module | One import per library area ([section 4.2](../lang/Stdlib.md#42-source-layout)). |
| A7 | Inference of type arguments | Explicit type arguments at every generic call | Shorter call sites. |

## Names that changed

A keyword cannot be a member name, so the names of the first design that collided with one were changed, along with names that depended on features the language does not have.

| First design | Problem | Now |
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

## Types removed from the first design


- **`Pair<A, B>`** is the native tuple `[A, B]`. `partition` returns `[ArrayList<T>, ArrayList<T>]`, `divide_and_remainder` returns `[BigInteger, BigInteger]`, `enumerate` yields `[int, T]`, and `zip` yields `[T, U]`.
- **`Entry<K, V>`** is the tuple `[K, V]`. Its contract was "an immutable snapshot of one association", which a tuple already is. Its text (`key: value`) is produced by the map's `to_string`.
- **`Boxed<T>`** existed to pass non-class values as `Object`. No API on this page takes an `Object`.

## Order of construction

The library was implemented in these independent source tracks, on the compiler support of [language facilities](../lang/Stdlib.md#3-language-facilities). Every track is complete: its modules are installed, tested through the installed compiler and NVM, and listed in [Standard library](../lang/StandardLibrary.md).

| Track | Modules and acceptance criteria |
| --- | --- |
| Program, console, and testing | Implement `Std.Program`, `Std.Io.Console.print_line`, `Failure`, and `Std.Test` over the existing console provider and message trap. Migrate examples and verify both entry forms before removing the legacy built-ins. |
| Core values | Implement `Unit`, `Cell`, `Ordering`, `Hash`, `Error`, `Optional`, `Result`, and the integer and Boolean companion modules. Exercise matching, nil versus `Some(nil)`, and generic callback specializations. |
| Text and number conversion | Implement `Strings`, `StringBuilder`, `Scalars`, `Regex`, parsing and formatting companions, and `BigInteger` and `BigDecimal` conversions. Reuse T1 and N1, and validate all documented failure and boundary cases. |
| Collections and iteration | Implement list, deque, linked list, hash, ordinal, tree, priority, and sequence modules against their interfaces. Check deterministic order, key equality, list aliasing, nil links, and structural modifications. |
| Pure algorithms and data formats | Implement math, seeded randomness, calendar arithmetic, JSON and other encodings, and digests. Compare the outputs with independent reference models and published vectors where applicable. |
| Host providers and capabilities | Register concrete providers for console input, files, environment, process, time, entropy, and networking. Specify denial, lifetime, error codes, and replay before considering the source modules complete. |
| Concurrency | Implement `Std.Concurrent` over the scheduler operations of [Concurrency](../lang/Concurrency.md), the scoped resource helpers, `PortableError`, and the overlapping, cancellable forms of the console, clock, and network calls. Check the library's task safety with `check --task-safety`. |

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
