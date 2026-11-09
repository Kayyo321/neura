# Feature status and implementation decisions

The C++ frontend implements lexing, parsing, and semantic lowering to typed NeuraIR for the existing examples. It handles their functions, control flow, modules, classes, generic declarations and calls, overlays, and nested collections. The I/O and testing modules provide frontend signatures. The IR optimization stage verifies NeuraIR and applies the passes described in [NeuraIR optimization](../IrOptimization.md). It folds only operations that have defined semantics and leaves the open decisions below in place. IRtoNeuronConversion converts a supported subset into `.nvr` images, and the Neura Virtual Machine runs them; [Neuron conversion and execution](../Backend.md) lists that subset. The language-level test runner is a future stage. See [Tooling](Tooling.md) for the commands.

| Area | Current design | Open decisions |
| --- | --- | --- |
| Core | Bindings, functions, control flow and basic operators proposed | Complete grammar, literals, overflow, scoping and evaluation details |
| Types | Primitive vocabulary, classes, overlays and collections described | Numeric conversions, inference, equality, nullability, enum syntax and representation |
| Modules | Imports, exports and inheritance described | Resolution, cycles, entry-point discovery and interface checking |
| Lifecycle | Constructors and explicit destruction proposed | Ownership, alias safety, initialization compatibility and failure cleanup |
| Errors | Expected failures should be explicit | Result representation, propagation, runtime failure behavior |
| Testing | Explicit test modules and serial isolated runner proposed | Runner interface, assertion reporting and integration |
| Library | Capability requirements outlined | Concrete signatures and error contracts |
| Tooling | Workflow and diagnostic requirements outlined | Commands, targets, manifests, dependency management |
| Concurrency | Deferred | Memory model, synchronization, cancellation and ownership |

## Frontend conventions

- Identifiers use ASCII letters, digits, and underscores. Source strings and comments support UTF-8. String literals support escaped quotes, backslashes, tabs, carriage returns, and newlines.
- Decimal integer literals default to `int32` and decimal floating-point literals default to `float64`. A numeric context can select a matching literal kind with a different width. The frontend checks integer literal ranges and uses a 32-bit signed range for `int` literals. Arithmetic overflow and floating-point rounding still need execution rules.
- Local value bindings require an initializer. A nonempty collection literal without an expected type infers an array, and an empty literal requires an expected collection type.
- The examples initialize some fields after construction. The frontend accepts that pattern while [object lifecycle](ObjectLifecycle.md) rules for definite field initialization and alias safety remain open. Successful lowering establishes frontend acceptance. Runtime behavior requires separate verification.

## Backend conventions

Where a rule above is open, conversion takes the narrowest choice that agrees with every result the language could later define, or rejects the construct with a diagnostic. [Neuron conversion and execution](../Backend.md#semantic-decisions) gives the reasons.

- Integer overflow and division by zero trap at run time. Division returns a result only when it is exact; a nonzero remainder traps, because rounding is open.
- `int`, `byte`, floating-point types, generic functions and classes, enums other than `SysIo.Entry.ExitCode`, the `Test` module, top-level statements, and nonconstant module initializers stop the build with a diagnostic. `length` has type `int`, so it is rejected too.
- The entry point is the one function that overrides `SysIo.Entry.main`. `Entry.ExitCode.Ok` is the status 0.
- A field without an initializer starts uninitialized, and reading it before a store traps. Construction and `destroy` follow the order proposed in [Object lifecycle](ObjectLifecycle.md).
- A failed `println` write traps the run, because the language has no error result for it yet.

| Example | Status |
| --- | --- |
| `hello-world`, `modules/module-interface.nra`, `modules/multi-file` | Compile and run. |
| `guide/core.nra`, `types/overlay.nra`, `types/classes.nra` | Libraries. Test drivers that import them compile and run. |
| `types/collections.nra` | Rejected: `length` has type `int`. |
| `types/polymorphism.nra` | Rejected: generics are not converted. |
| `testing/math-tests.nra` | Rejected: the `Test` module needs the unspecified runner. |
| `guide/resources.nra` | Rejected: a top-level statement and a nonconstant initializer. |
| `guide/errors.nra` | No code. |
