# Feature status

This release carries the language below from source through NeuraIR, optimization, neuron conversion, and the `.nvr` image to execution in the Neura Virtual Machine. Every feature marked supported compiles to a standalone image, runs under `neura run`, and has end-to-end tests; the [release checklist](../Release.md) maps each one to its tests. See [Tooling](Tooling.md) for the commands.

## Supported in this release

| Area | Supported | Specification |
| --- | --- | --- |
| Core | Bindings (`let`, `const`, and assign-once `last` with definite assignment) and destructuring (with a tuple annotation and a value per name), functions and closures, fixed-arity function overloads, variadic parameters, expression bodies, `if`/`else`, conditional expressions, comparison chains, `do`, `do`/`while`, `while`, three-part `for`, for-each, `break`, `continue`, `return` | [Core language](CoreLanguage.md) |
| Numbers | Sized integers and floats, aliases, checked arithmetic and conversions, update and compound assignment operators, bitwise operations and shifts, exact float bit reinterpretation | [Numbers](Numbers.md) |
| Types | Classes and interfaces with first-use static initialization, getters and setters of exported fields, startup singletons, umbrella `class`, explicit `nil`, reference identity and checked casts, object casts, non-null primitive references, primitive companion calls, value members and `===`, UTF-8 text, tuples, payload enums with exhaustive matching, and anonymous classes in type positions | [Types](Types.md) |
| Generics | Generic functions, classes, enums, interfaces, methods and closures, with explicit type arguments, intersection bounds, and specialization | [Polymorphism](Polymorphism.md) |
| Modules | Source and prebuilt NeuraIR imports, promoted and wildcard imports, discard bindings, exports, module inheritance, initialization, trusted `Std` sources with host and kernel bindings, and the library-defined or legacy entry point | [Modules](Modules.md) |
| Lifecycle | Constructors and constructor overloads, target-typed `new`, field initializers, `last` fields assigned once by each constructor, `destroy`, destructors, deterministic tracing garbage collection | [Object lifecycle](ObjectLifecycle.md) |
| Collections | Arrays, lists, stepped ranges, `length`, indexing, `append`, `remove_at`, structural versions, and for-each over built-ins or `Iterable<T>` | [Collections](Collections.md) |
| Errors | Expected failures as returned values, plus runtime traps and explicit message traps | [Errors](Errors.md) |
| Concurrency | Scoped tasks with `scope`, `spawn`, and `join`; isolation checked by the compiler (scope-bound handles, transferable values, task-safe calls); bounded channels and `select`; cooperative cancellation, deadlines, and timers; host I/O that overlaps and can be cancelled; the `Std.Concurrent` modules, with `Tasks` and `WorkerGroup`; shared state through `AtomicInt64`, `Semaphore`, and `Guarded<T>`; `Parallel.map` over isolated work on several cores; record and replay of concurrent runs; task dumps, profiles, and trace events | [Concurrency](Concurrency.md), [Core language](CoreLanguage.md#tasks-scopes-and-select) |
| Testing | `neura test` with the `Std.Test` assertions; for concurrent code, virtual time, schedule exploration from a seed, and reproduction of a saved schedule | [Testing](Testing.md) |
| Foreign interop | `Std.DynamicForeign`: native libraries, symbol lookup, arena-owned native memory, layouts, downcalls through the platform C ABI, and upcalls, granted by default and denied with `--deny-native`. `import:c(...)` C header imports that link static libraries into the image | [Foreign interop](ForeignInterop.md) |
| Tags | `#` tags before declarations, members, enum variants, and parameters, validated against a registry of 85 tags. Implemented: `Deprecated`, `Experimental`, `Internal`, `Hidden`, `NoDiscard`, `Final`, `Sealed`, `Inline`, `NoInline`, `Allow`, and `Deny`, with warnings `W0001` to `W0004`; 17 informational tags are recorded and shown by `neura docs`; the other 57 are reserved and rejected until the compiler keeps their promise. Tags persist in `.nir` files | [Tags](Tags.md) |
| Tooling | `check` (with `--task-safety`), `docs` for `///` comments, `tags`, `emit-ir`, `build`, `build --native` and `inspect` on Linux x86-64, `run` (every host service granted by default, `--deny-*` and `--allow-*` capability options, `--record`, `--replay`, `--stats`, `--profile`, `--each`, `--trace-events`), `test` (`--schedules`, `--schedule-seed`, `--save-schedule`, `--schedule`; both with the task limits, `--task-quantum`, `--parallel-workers`, `--task-dump`, `--virtual-time`, `--gpu=auto\|always\|off`, `--no-gpu`, `--require-gpu`, `--gpu-software`, `--gpu-verify`, and `NEURA_GPU`), `disassemble`, `assemble`, `decompile`, `c-bindings`, `--version`, and on Linux and macOS `doctor`, `update`, and `uninstall` for an archive install | [Install](../Install.md), [Tooling](Tooling.md), [Decompiler](../Decompiler.md) |
| Standard library | The `Std` source modules of the [standard library specification](Stdlib.md), installed with the tool, the host providers they bind, and the built-in kernels behind `Std.Digest` ([Kernels](../runtime/Kernels.md)); see [Standard library](StandardLibrary.md) for the module list | [Standard library](StandardLibrary.md) |

## Outside this release

| Area | Status |
| --- | --- |
| Standard library extensions | Time zones, locale-sensitive sorting and formatting, general Unicode classification, ciphers and TLS, HTTP, UDP, `BigInteger` bitwise operations, and concurrent collections ([specification, section 16](Stdlib.md#16-not-provided)). Collection facades that re-export every type need generic re-exports. |
| Concurrency | Tasks in a program that uses `Std.DynamicForeign` or a C import (the compiler rejects the combination); running tasks on several cores (only `Parallel.map` uses more than one); overlapping file I/O; concurrent collections. Tasks are tested on Linux; Windows is unverified. The limits are listed in [Concurrency, section 16](Concurrency.md#16-limits). |
| Foreign interop | Interop with Java and other managed runtimes. Native code runs only on Linux x86-64 in this release's tests; other platforms build the same provider but are unverified. |
| Packages | Manifests, dependency management, and package distribution are not specified ([Project structure](ProjectStructure.md)). |
| Tags | The reserved tags of the [catalogue](Tags.md#catalogue), among them `Pure`, `Immutable`, `ThreadSafe`, `NotThreadSafe`, and `Synchronized`, which a later release checks. Tags before statements such as loops, and on destructuring declarations. `#Hot`, `#Cold`, `#Likely`, and `#Unlikely` have no effect. |

## Open language decisions

These are not decided. For most of them the compiler rejects or does not offer the construct, so no program depends on a choice. The exception is shadowing: the compiler accepts a nested declaration of a name and hides the outer binding, so a program that does this depends on the current behavior.

| Area | Open decisions |
| --- | --- |
| Core | Whether shadowing stays allowed, Unicode identifiers and escapes |
| Types | Type inference beyond local bindings, static non-nil types, nested enum patterns, match guards, overridable accessors and accessor requirements in interfaces |
| Generics | Inference of type arguments, generic class inheritance, variance |
| Modules | Package-level resolution, and with it the boundary of `#Internal`, which is a directory tree until packages exist |
| Lifecycle | Ownership, alias safety, definite initialization of `let` fields (`last` fields are checked), failure cleanup |

## Frontend conventions

- Identifiers use ASCII letters, digits, and underscores. Source strings and comments support UTF-8. String literals support escaped quotes, backslashes, tabs, carriage returns, and newlines.
- Decimal integer literals default to `int32` and decimal floating-point literals default to `float64`. A numeric context selects a literal of a different width; [Numbers](Numbers.md) specifies ranges and rounding.
- Keywords such as `for`, `in`, `as`, `is`, `nil`, `enum`, `match`, `interface`, `implements`, `and`, and `or` cannot name bindings. `last`, like `scope`, `spawn`, and `select`, is a contextual word and stays a valid name: it declares only when an identifier follows it ([Core language](CoreLanguage.md#source-and-bindings)).
- Every declaration ends with `;`, including a function, overload group, class, interface, or module member whose body ends at `}` or `)`; statement blocks take none. A missing `;` is error `E0201`, and `neura check --fix` inserts it.
- An exported instance field may have `| get:` and `| set:` accessor clauses ([Types](Types.md#accessors)). Inside the declaring class a bare field name or `instance.field` is the storage; any other receiver, a subclass, and code outside the class call the accessors, and a read-modify-write evaluates its receiver once, then gets, computes, and sets. A get-only field is read-only outside its class (`E0510`) and a set-only field write-only (`E0515`). Accessors resolve statically, cannot be overridden, and cannot be required by interfaces.
- A `let` or `const` local binding requires an initializer. A `last` local or instance field may omit it and is then assigned exactly once on every path before it is read, checked by the definite-assignment rules of [Core language](CoreLanguage.md#deferred-assignment). Decision for 0.2.0: `last` is the only deferred form, `const` stays for module, file, and static constants, and a deferred `last` binding outside a function body or instance field is an error. A nonempty collection literal without an expected type infers an array, and an empty literal requires an expected collection type.
- The examples initialize some fields after construction. The frontend accepts that pattern for `let` fields; reading a field before any store is a runtime fault. A `last` field without an initializer must be assigned by every constructor ([Object lifecycle](ObjectLifecycle.md#last-fields)).
- A target-typed `new` takes the expected type of its value: a binding or field annotation (including an element of a typed destructuring declaration), the declared type of an assignment's destination, a function's declared result type for `return`, a parameter for a call argument once the callee is known (among overloads it is a contextual argument), and the element type of an expected collection or the type of an expected conditional. There is no other inference: `let a = new;` is E0420 ([Object lifecycle](ObjectLifecycle.md#target-typed-new)).
- A destructuring list after `=` has one value per name, evaluated left to right before any name is bound. The typed and listed forms are allowed where destructuring already was, in blocks and at file level, not as module or class members ([Types](Types.md#typed-and-listed-destructuring)).
- Anonymous class identity: a fields-only body, whose members are all exported, typed `let` instance fields without accessor clauses, is one class wherever it is written, identified by its fields' order, names, and types; field initializers are not part of its identity. Any body with a method, lifecycle function, static member, cast, base, or interface, or with a `const` or `last` field or an accessor clause, is a class of its own per occurrence and accepts every member a declared class accepts. A fields-only class runs the field initializers of the body in the annotation of the binding that `new` initializes, or of the body written after `new`; initializers anywhere else are rejected. A fields-only class is the same class across separately compiled NeuraIR modules ([Types](Types.md#anonymous-classes)).

## Backend conventions

[Neuron conversion and execution](../Backend.md#semantic-decisions) records how conversion implements each rule.

- Integer overflow, a zero divisor, `MIN / -1`, and an out-of-range conversion trap with reason 1. Division truncates toward zero. Each class of trap ends the run with its own process status ([Errors](Errors.md)).
- A negative index traps with reason 2; a list modified during for-each traps with reason 5.
- The entry point is the unique override of `Std.Program.Entry.main`. `Ok` is status 0, `Failure` 1, and `Status(n)` is `n` for 0 through 255; any other `n` traps. The named variants from `SeriousFailure` (2) to `FileSizeExceeded` (153) have fixed statuses ([Modules](Modules.md#program-entry)). The built-in `SysIo` and `Test` modules are retired, and importing either is an error that names its replacement.
- An instance field without an initializer starts uninitialized, and reading it before a store faults. Static fields follow the preparation and first-use rules in [Static initialization](Types.md#static-initialization). Construction and `destroy` follow the order in [Object lifecycle](ObjectLifecycle.md).
- In an NVR 1.5 or later image with a `TRNS` table, live frames of every task count toward `--max-heap-cells` (one cell per frame plus one per slot), as do task records and open scopes; `--max-frames` bounds each task's depth. Images without tasks keep their accounting.
- `Console.print_line` traps with reason 3 when the write fails, and a failed `Std.Test` assertion is a message trap with reason 3.

## Examples

| Example | Status |
| --- | --- |
| `hello-world`, `modules/module-interface.nra`, `modules/multi-file` | Compile and run; they use `Std.Io.Console` and `Std.Program`. |
| `guide/core.nra`, `types/overlay.nra`, `types/classes.nra`, `types/collections.nra`, `types/polymorphism.nra` | Libraries. Test drivers in `tests/backend/examples` import them, compile, and run. |
| `types/overloads.nra` | Runnable example of function and constructor overloads, object casts, and a primitive companion call. |
| `types/accessors.nra` | Runnable example of a getter and a setter, a read-only field, and a setter that traps on an invalid value; `backend-cli` builds and runs it. |
| `types/construction.nra` | Runnable example of target-typed `new`, typed destructuring, and an anonymous class; `backend-cli` builds and runs it. |
| `types/anonymous-accessors.nra` | Runnable example of accessors on the fields of an anonymous class; `backend-cli` builds and runs it. |
| `types/primitive-references.nra` | Library example of a constructor-bound reference field and a reference parameter. |
| `guide/errors.nra` | A library that reads a count from a file with `Std.Fs` and `Std.Core.Int32.parse` and returns a `Result`; `tests/backend/examples/errors-driver.nra` runs it with `--deny-fs`. |
| `guide/resources.nra` | Top-level statements; runs with the test driver `tests/backend/examples/resources-driver.nra`, built together with it. |
| `foreign/native.nra` | Compiles and runs with native code granted, which is the default: `strlen` through `Std.DynamicForeign` and `qsort` with an upcall through `import:c("stdlib.h")`. Not run by the test suite, which covers the same calls in `tests/stdlib/dynamic-foreign` and `tests/c-import`. |
| `concurrency/service.nra` | Compiles and runs with the default capabilities: state owned by a task behind a channel, a cancellable search, and a table behind a `Guarded` handle. `backend-cli` builds and runs it. |
| `testing/math-tests.nra` | Tests with `Std.Test`; `neura test -I examples/guide examples/testing/math-tests.nra` passes. |
| `tags/tags.nra` | Compiles and runs; it uses implemented and informational [tags](Tags.md). `ir-optimization-cli` builds it with and without optimization and compares the output and the step counts. |
