# Feature status and implementation decisions

This release carries the language below from source through NeuraIR, optimization, neuron conversion, and the `.nvr` image to execution in the Neura Virtual Machine. Every feature marked supported compiles to a standalone image, runs under `neura run`, and has end-to-end tests; the [release checklist](../Release.md) maps each one to its tests. See [Tooling](Tooling.md) for the commands.

## Supported in this release

| Area | Supported | Specification |
| --- | --- | --- |
| Core | Bindings and destructuring, functions and closures, `if`/`else`, comparison chains, `while`, three-part `for`, for-each, `break`, `continue`, `return` | [Core language](CoreLanguage.md) |
| Numbers | Sized integers and floats, `int` (`int32`), `byte` (`uint8`), `float` (`float32`), checked arithmetic, integer bitwise operations and shifts, and `as` conversions | [Numbers](Numbers.md) |
| Types | Classes, inheritance, overlays, tuples, enums with named and positional payloads and exhaustive matching, collections | [Types](Types.md) |
| Generics | Generic functions, classes, and methods with explicit type arguments, bounds, nested instances; specialization per type arguments | [Polymorphism](Polymorphism.md) |
| Modules | Imports, exports, module inheritance, module interfaces, the entry point, program initialization order, top-level statements | [Modules](Modules.md) |
| Lifecycle | Constructors, field initializers, `destroy`, destructors, deterministic tracing garbage collection | [Object lifecycle](ObjectLifecycle.md) |
| Collections | Arrays, lists, `length`, indexing, `append`, `remove_at`, for-each with modification checks | [Collections](Collections.md) |
| Errors | Expected failures as returned values; runtime failures trap with documented reasons | [Errors](Errors.md) |
| Testing | `neura test`, `Test.assert`, `Test.assert_equal_int32` | [Testing](Testing.md) |
| Tooling | `check`, `emit-ir`, `build`, `run`, `test`, `disassemble`, `assemble`, `decompile` | [Tooling](Tooling.md), [Decompiler](../Decompiler.md) |

## Outside this release

| Area | Status |
| --- | --- |
| Standard library | Only `SysIo.println`, `SysIo.Entry`, and `Test` exist. Text and numeric formatting, collection operations beyond `length`, `append`, and `remove_at`, files and paths, a standard result type, and I/O APIs with explicit failure results are a separate topic ([Standard library](StandardLibrary.md)). The [implementation plan](Stdlib.md) specifies the planned library and lists the language features it is blocked on; none of it is implemented. |
| Concurrency | Deferred: memory model, synchronization, cancellation, and ownership ([Resources and concurrency](ResourcesConcurrency.md)). |
| Foreign interop | Java and other foreign-function interop are not part of the language. |
| Packages | Manifests, dependency management, and package distribution are not specified ([Project structure](ProjectStructure.md)). |

## Open language decisions

These remain open. The compiler rejects or does not offer the affected constructs, so no program depends on a choice.

| Area | Open decisions |
| --- | --- |
| Core | Shadowing, Unicode identifiers and escapes, compound assignment |
| Types | Type inference beyond local bindings, nullability, equality for classes, collections, and strings, nested enum patterns, match guards, generic enum declarations |
| Generics | Inference of type arguments, multiple bounds, generic inheritance, variance |
| Modules | Interface checking beyond overrides, package-level resolution |
| Lifecycle | Ownership, alias safety, definite field initialization, failure cleanup |

## Frontend conventions

- Identifiers use ASCII letters, digits, and underscores. Source strings and comments support UTF-8. String literals support escaped quotes, backslashes, tabs, carriage returns, and newlines.
- Decimal integer literals default to `int32` and decimal floating-point literals default to `float64`. A numeric context selects a literal of a different width; [Numbers](Numbers.md) specifies ranges and rounding.
- `for`, `in`, `as`, `enum`, `match`, `and`, and `or` are keywords, so they cannot name bindings.
- Local value bindings require an initializer. A nonempty collection literal without an expected type infers an array, and an empty literal requires an expected collection type.
- The examples initialize some fields after construction. The frontend accepts that pattern; reading a field before any store is a runtime fault.

## Backend conventions

[Neuron conversion and execution](../Backend.md#semantic-decisions) records how conversion implements each rule.

- Integer overflow, a zero divisor, `MIN / -1`, and an out-of-range conversion trap with reason 1. Division truncates toward zero.
- A negative index traps with reason 2; a list modified during for-each traps with reason 5.
- The entry point is the one function that overrides `SysIo.Entry.main`. `Entry.ExitCode.Ok` is the status 0.
- A field without an initializer starts uninitialized, and reading it before a store faults. Construction and `destroy` follow the order in [Object lifecycle](ObjectLifecycle.md).
- A failed `println` write and a failed test assertion trap the run with reason 3.

## Examples

| Example | Status |
| --- | --- |
| `hello-world`, `modules/module-interface.nra`, `modules/multi-file` | Compile and run. |
| `guide/core.nra`, `types/overlay.nra`, `types/classes.nra`, `types/collections.nra`, `types/polymorphism.nra` | Libraries. Test drivers in `tests/backend/examples` import them, compile, and run. |
| `guide/resources.nra` | Top-level statements; runs with the test driver `tests/backend/examples/resources-driver.nra`, built together with it. |
| `testing/math-tests.nra` | Tests; `neura test -I examples/guide examples/testing/math-tests.nra` passes. |
| `guide/errors.nra` | Commented pseudocode for file input, which belongs to the standard library. |
