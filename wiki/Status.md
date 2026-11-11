<!-- Generated from docs/lang/Status.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Feature status and implementation decisions

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This release carries the language below from source through NeuraIR, optimization, neuron conversion, and the `.nvr` image to execution in the Neura Virtual Machine. Every feature marked supported compiles to a standalone image, runs under `neura run`, and has end-to-end tests; the [release checklist](Release) maps each one to its tests. See [Tooling](Tooling) for the commands.

## Supported in this release

| Area | Supported | Specification |
| --- | --- | --- |
| Core | Bindings and destructuring, functions and closures, fixed-arity function overloads, variadic parameters, expression bodies, `if`/`else`, conditional expressions, comparison chains, `do`, `do`/`while`, `while`, three-part `for`, for-each, `break`, `continue`, `return` | [Core language](CoreLanguage) |
| Numbers | Sized integers and floats, aliases, checked arithmetic and conversions, update and compound assignment operators, bitwise operations and shifts, exact float bit reinterpretation | [Numbers](Numbers) |
| Types | Classes and interfaces with first-use static initialization, startup singletons, umbrella `class`, explicit `nil`, reference identity and checked casts, object casts, non-null primitive references, primitive companion calls, value members and `===`, UTF-8 text, tuples, and payload enums with exhaustive matching | [Types](Types) |
| Generics | Generic functions, classes, enums, interfaces, methods and closures, with explicit type arguments, intersection bounds, and specialization | [Polymorphism](Polymorphism) |
| Modules | Source and prebuilt NeuraIR imports, promoted and wildcard imports, discard bindings, exports, module inheritance, initialization, trusted `Std` sources and host bindings, and the library-defined or legacy entry point | [Modules](Modules) |
| Lifecycle | Constructors and constructor overloads, field initializers, `destroy`, destructors, deterministic tracing garbage collection | [Object lifecycle](ObjectLifecycle) |
| Collections | Arrays, lists, stepped ranges, `length`, indexing, `append`, `remove_at`, structural versions, and for-each over built-ins or `Iterable<T>` | [Collections](Collections) |
| Errors | Expected failures as returned values, plus runtime traps and explicit message traps | [Errors](Errors) |
| Concurrency | Scoped tasks with `scope`, `spawn`, and `join`; isolation checked by the compiler (scope-bound handles, transferable values, task-safe calls); bounded channels and `select`; cooperative cancellation, deadlines, and timers; host I/O that overlaps and can be cancelled; the `Std.Concurrent` modules, with `Tasks` and `WorkerGroup`; shared state through `AtomicInt64`, `Semaphore`, and `Guarded<T>`; `Parallel.map` over isolated work on several cores; record and replay of concurrent runs; task dumps, profiles, and trace events | [Concurrency](Concurrency), [Core language](CoreLanguage#tasks-scopes-and-select) |
| Testing | `neura test` with the `Std.Test` assertions; for concurrent code, virtual time, schedule exploration from a seed, and reproduction of a saved schedule | [Testing](Testing) |
| Foreign interop | `Std.DynamicForeign`: native libraries, symbol lookup, arena-owned native memory, layouts, downcalls through the platform C ABI, and upcalls, granted by default and denied with `--deny-native`. `import:c(...)` C header imports that link static libraries into the image | [Foreign interop](ForeignInterop) |
| Tooling | `check` (with `--task-safety`), `docs` for `///` comments, `emit-ir`, `build`, `build --native` and `inspect` on Linux x86-64, `run` (every host service granted by default, `--deny-*` and `--allow-*` capability options, `--record`, `--replay`, `--stats`, `--profile`, `--each`, `--trace-events`), `test` (`--schedules`, `--schedule-seed`, `--save-schedule`, `--schedule`; both with the task limits, `--task-quantum`, `--parallel-workers`, `--task-dump`, `--virtual-time`, `--gpu=auto\|always\|off`, `--no-gpu`, `--require-gpu`, `--gpu-software`, `--gpu-verify`, and `NEURA_GPU`), `disassemble`, `assemble`, `decompile` | [Tooling](Tooling), [Decompiler](Decompiler) |
| Standard library | The `Std` source modules of the [implementation plan](Stdlib-Design), installed with the tool, and the host providers they bind; see [Standard library](StandardLibrary) for the module list and [section 18 of the plan](Stdlib-Design#18-implementation-decisions) for decisions made while implementing it | [Standard library](StandardLibrary) |

## Outside this release

| Area | Status |
| --- | --- |
| Standard library extensions | Time zones, locale-sensitive sorting and formatting, general Unicode classification, ciphers and TLS, HTTP, UDP, `BigInteger` bitwise operations, and concurrent collections ([plan, section 15.1](Stdlib-Design#151-deferred-and-excluded)). Collection facades that re-export every type need generic re-exports. |
| Concurrency | Tasks in a program that uses `Std.DynamicForeign` or a C import ([decision C15](Concurrency#3-decisions)); running tasks on several cores (only `Parallel.map` uses more than one); overlapping file I/O; concurrent collections. Tasks are tested on Linux; Windows is unverified. Open decisions are in [Concurrency, section 18](Concurrency#18-decisions-and-open-questions). |
| Foreign interop | Interop with Java and other managed runtimes. Native code runs only on Linux x86-64 in this release's tests; other platforms build the same provider but are unverified. |
| Packages | Manifests, dependency management, and package distribution are not specified ([Project structure](ProjectStructure)). |

## Open language decisions

These remain open. The compiler rejects or does not offer the affected constructs, so no program depends on a choice.

| Area | Open decisions |
| --- | --- |
| Core | Shadowing, Unicode identifiers and escapes |
| Types | Type inference beyond local bindings, static non-nil types, nested enum patterns, match guards |
| Generics | Inference of type arguments, generic class inheritance, variance |
| Modules | Package-level resolution |
| Lifecycle | Ownership, alias safety, definite field initialization, failure cleanup |

## Frontend conventions

- Identifiers use ASCII letters, digits, and underscores. Source strings and comments support UTF-8. String literals support escaped quotes, backslashes, tabs, carriage returns, and newlines.
- Decimal integer literals default to `int32` and decimal floating-point literals default to `float64`. A numeric context selects a literal of a different width; [Numbers](Numbers) specifies ranges and rounding.
- Keywords such as `for`, `in`, `as`, `is`, `nil`, `enum`, `match`, `interface`, `implements`, `and`, and `or` cannot name bindings.
- Local value bindings require an initializer. A nonempty collection literal without an expected type infers an array, and an empty literal requires an expected collection type.
- The examples initialize some fields after construction. The frontend accepts that pattern; reading a field before any store is a runtime fault.

## Backend conventions

[Neuron conversion and execution](Backend#semantic-decisions) records how conversion implements each rule.

- Integer overflow, a zero divisor, `MIN / -1`, and an out-of-range conversion trap with reason 1. Division truncates toward zero. Each class of trap ends the run with its own process status ([Errors](Errors)).
- A negative index traps with reason 2; a list modified during for-each traps with reason 5.
- The entry point is the unique override of `Std.Program.Entry.main`. `Ok` is status 0, `Failure` 1, and `Status(n)` is `n` for 0 through 255; any other `n` traps. The named variants from `SeriousFailure` (2) to `FileSizeExceeded` (153) have fixed statuses ([Modules](Modules#program-entry)). The built-in `SysIo` and `Test` modules are retired, and importing either is an error that names its replacement.
- An instance field without an initializer starts uninitialized, and reading it before a store faults. Static fields follow the preparation and first-use rules in [Static initialization](Types#static-initialization). Construction and `destroy` follow the order in [Object lifecycle](ObjectLifecycle).
- In an NVR 1.5 image with a `TRNS` table, live frames of every task count toward `--max-heap-cells` (one cell per frame plus one per slot), as do task records and open scopes; `--max-frames` bounds each task's depth. This settles the frame-storage decision by adding a bound rather than leaving task frames unbounded ([Concurrency, section 10.3](Concurrency#103-limits)). Images without tasks keep their accounting.
- `Console.print_line` traps with reason 3 when the write fails, and a failed `Std.Test` assertion is a message trap with reason 3.

## Examples

| Example | Status |
| --- | --- |
| `hello-world`, `modules/module-interface.nra`, `modules/multi-file` | Compile and run; they use `Std.Io.Console` and `Std.Program`. |
| `guide/core.nra`, `types/overlay.nra`, `types/classes.nra`, `types/collections.nra`, `types/polymorphism.nra` | Libraries. Test drivers in `tests/backend/examples` import them, compile, and run. |
| `types/overloads.nra` | Runnable example of function and constructor overloads, object casts, and a primitive companion call. |
| `types/primitive-references.nra` | Library example of a constructor-bound reference field and a reference parameter. |
| `guide/errors.nra` | A library that reads a count from a file with `Std.Fs` and `Std.Core.Int32.parse` and returns a `Result`; `tests/backend/examples/errors-driver.nra` runs it with `--deny-fs`. |
| `guide/resources.nra` | Top-level statements; runs with the test driver `tests/backend/examples/resources-driver.nra`, built together with it. |
| `foreign/native.nra` | Compiles and runs with native code granted, which is the default: `strlen` through `Std.DynamicForeign` and `qsort` with an upcall through `import:c("stdlib.h")`. Not run by the test suite, which covers the same calls in `tests/stdlib/dynamic-foreign` and `tests/c-import`. |
| `concurrency/service.nra` | Compiles and runs with the default capabilities: state owned by a task behind a channel, a cancellable search, and a table behind a `Guarded` handle. `backend-cli` builds and runs it. |
| `testing/math-tests.nra` | Tests with `Std.Test`; `neura test -I examples/guide examples/testing/math-tests.nra` passes. |
