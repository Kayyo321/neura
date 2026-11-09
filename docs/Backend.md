# Neuron conversion and execution

This page describes the implemented back half of the toolchain: `IRtoNeuronConversion`, the NVR writer and loader, and the Neura Virtual Machine. [NVM](runtime/NVM.md) and [NVR](runtime/NVR.md) define the contracts. This page records how the implementation meets them, the decisions it takes where the language is still open, and what the tests establish.

## Pipeline

```text
hello.nra --lower--------+
                         +--> verify --> optimize --> convert --> encode --> load (self-check) --> hello.nvr
hello.nir --read_nir-----+

hello.nvr --> load --> bind imports --> Execution
```

`neura build` takes either route into the same function, `backend::build`, so both routes produce identical bytes from identical NeuraIR. `--no-optimize` replaces optimization with verification alone. Before writing the image, the build loads the encoded bytes through the full validating loader, so the converter or writer cannot publish an image that the runtime rejects. The output is written to a temporary file and renamed, so a failed build leaves no output file.

| Directory | Library | Contents |
| --- | --- | --- |
| `src/neura-ir/nir_format.*` | `neura_compiler` | The `.nir` reader and writer. [NeuraIR file format](NirFormat.md) specifies the encoding. |
| `src/backend/circuit.*` | `neura_compiler` | Neuron circuits for bits and fixed-width integers. |
| `src/backend/converter.*` | `neura_compiler` | `IRtoNeuronConversion` and the build pipeline shared by both routes. |
| `src/nvr/` | `neura_nvr` | The image model, the writer, and the validating loader. |
| `src/nvm/` | `neura_nvm` | Execution, host services, and the console sink. |

`neura_nvm` links only `neura_nvr`, so a runtime-only distribution needs no compiler code. The `neura` executable links both and provides `neura run`.

## Supported NeuraIR

Conversion lowers an operation only where the language defines its result. Where a result depends on data, such as overflow, the graph checks the operands and takes a trap exit when the result is undefined.

| NeuraIR | Lowering |
| --- | --- |
| `boolean`, `integer` literals | Constant lanes folded into neuron biases, or constant-pool entries when an exit needs the value. |
| `string` literal | A UTF-8 text constant. |
| `load`, `store`, `initialize` of locals and parameters | Typed frame slots. Assignments become parallel slot assignments on the block's exits. |
| `load`, `store` of module globals | `ReadGlobal` and `WriteGlobal` state operations. |
| `unary !`, `binary == !=` on `bool` | Graph gadgets. |
| `unary -`, `binary + - * /` on `int8` to `uint64` | Checked integer circuits with trap exits for undefined results. |
| Comparisons on `int8` to `uint64` and on `Entry.ExitCode` | Graph circuits. |
| `binary +` on `string` | A call to a generated concatenation function. |
| `short_circuit` | One graph neuron when the right operand is pure and total. Otherwise the left operand selects a block that evaluates the right operand or a block that skips it. |
| `branch`, `loop`, `break`, `continue`, `block`, `return` | Blocks connected by `Continue` exits that the graph selects, and `Return` exits. |
| `call` | A `Call` exit. Module functions are specialized for their module. |
| `virtual_call` on a module | Resolved statically: the most derived implementation in the concrete module, specialized for that module. |
| `virtual_call` on an object | `ReadTag`, then a dispatch block whose graph compares the tag with each instantiated class that can reach the call site. |
| `external_call` to `SysIo.println` | A `Host` exit to `neura.console.write_line` version 1. |
| `allocate`, `base_initialize` | `AllocateObject`, then a generated constructor function. |
| `field` | `ReadField` and `WriteField` with the field's index in its declaring class's layout. |
| `collection`, `index`, `append`, `remove_at` | `AllocateCollection`, `ReadElement`, `WriteElement`, `AppendElement`, and `RemoveElement`. |
| `destroy` | `ReadTag`, dispatch to a generated destructor for the runtime class, then `FreeObject`. |
| `discard` | Evaluates the operand, including its checks, and drops the value. |

The build stops with a source diagnostic, before any output is written, for:

- `int` and `byte`, whose width and signedness are open. This includes `length`, whose type is `int`.
- Floating-point types and literals.
- Type parameters, generic functions, and generic classes. The converter does not specialize generics.
- Enums other than `SysIo.Entry.ExitCode`, whose representation is open.
- External functions other than `SysIo.println`, including the `Test` module, which needs the unspecified test runner.
- Top-level statements, and module or file initializers that are not literals.
- Programs with no function overriding `SysIo.Entry.main`, or more than one.
- A module call that resolves to an interface function without a body.

## Semantic decisions

These choices follow the existing contracts where they apply. Where the language is open, the backend takes the narrowest choice that agrees with every value the language could later define. A program that never reaches an undefined case behaves the same under any future decision.

| Area | Language status | Backend behavior |
| --- | --- | --- |
| Integer overflow | Open | `+`, `-`, `*`, and unary `-` trap with reason 1 when the exact result is outside the type. Nothing wraps or saturates. |
| Division | Division by zero and rounding are open | `/` returns the quotient only when the division is exact. A zero divisor, a nonzero remainder, or an unrepresentable quotient (`MIN / -1`) traps with reason 1. This matches the optimizer's folding rule. |
| Negative index | Bounds error | A signed index is checked for a negative value in the graph, which traps with reason 2 before the value is converted to the `uint64` index of the state operation. NVM reports an index at or beyond the length as a bounds fault. |
| Entry point | Discovery is open | The unique function that overrides `SysIo.Entry.main`. `Entry.ExitCode` is `int32`, and `Ok` is 0. |
| Module receivers | Dispatch by override chain | Static. A module function is converted once for each concrete module through which it is called. Inside it, calls and global accesses on the module receiver resolve in that module's chain. |
| Module globals | Storage is open | One storage location per declared global. An inherited global that is not overridden is the same storage through the base and the derived module. An override has its own storage. The IR reference evaluator uses the same rule. |
| Initialization order | Open across modules | Module and file initializers must be literals and become NVR global initializers, so their order cannot be observed. Other initializers and top-level statements are rejected. |
| Construction | Proposed in [Object lifecycle](lang/ObjectLifecycle.md) | The base constructor runs, then this class's field initializers, then the rest of the declared `init`. Fields without an initializer start uninitialized; reading one before a store traps. |
| Destruction | Proposed | Cleanup dispatches on the runtime class and runs each `deinit` from the most derived class toward the bases. Then the object is freed. A later use of the handle traps. |
| `println` failure | Open | After a failed or partial write, the run traps with reason 3. The language has no error result for `println` yet. |
| Short-circuit operands | Specified | The right operand is speculated in the graph only when the optimizer's `pure_and_total` predicate holds: it cannot fail, change state, or request an effect. Otherwise it gets a block of its own, reached only when the left operand does not decide the result. |

## Circuits

A graph value is a vector of bit lanes, least significant first, with two's-complement encoding for signed integers. During construction a signal is an affine combination of wires, `c + Σ wᵢ·xᵢ`. Linear combinations cost no neurons; only the ReLU and identity neurons that consume them are stored. Every gadget takes inputs known to be 0 or 1 and uses at most two neurons:

| Gadget | Neurons | Construction |
| --- | --- | --- |
| `a ∧ b` | 1 | `relu(a + b - 1)` |
| `a ∨ b` | 1 | `1 - relu(1 - a - b)` |
| `¬a`, `a ⊕ b` | 0, 1 | `1 - a`; `a + b - 2·(a ∧ b)` |
| `s ? x : y` | 2 | `(s ∧ x) + (¬s ∧ y)` |
| `[v ≥ k]` for integer `v` | 2 | `relu(v - k + 1) - relu(v - k)` |
| all, any of n bits | 1 | `relu(Σ - n + 1)`, `1 - relu(1 - Σ)` |

Constant lanes fold through every gadget, so `x + 1` and `x == 42` need no neurons for their constant operand. Every wire holds 0 or 1, except a multiplier's column sums, which stay below 128 for 64-bit operands. In the 64-bit circuits, weights and biases are at most 64 in magnitude and a neuron reads at most 128 wires. Every weighted sum is therefore below 2²⁰, far inside the checked signed 64-bit range of graph profile 1, whatever the inputs. The circuit tests also confirm that no neuron overflows on any input they evaluate.

Arithmetic uses these gadgets:

- Addition and subtraction use a ripple-carry adder: two ReLU neurons per bit for the carry, plus an identity neuron when the sum bit's terms are compacted. Signed overflow is `carry into the top bit ⊕ carry out`. Unsigned overflow is the carry out, or the borrow for subtraction.
- `a < b` is the borrow of `a - b`. It needs the carry chain only, with the sign bits inverted for signed order. Equality uses one neuron per bit pair plus one conjunction.
- Multiplication sums partial products by column. One identity neuron sums each column, and thresholds split the sum into the result bit and an integer carry. Overflow is any partial product at or above the type's width, or a carry out of the top column. Signed multiplication multiplies magnitudes and checks the result against `2^(w-1) - 1`, or against `2^(w-1)` for a negative result.
- Division uses restoring division, one quotient bit per column. Signed division divides magnitudes, and the remainder decides exactness.

Measured cost of one signed operation on two variable operands, in neurons and edges:

| Operation | int8 | int32 | int64 |
| --- | --- | --- | --- |
| `a + b`, `a - b` | 25 / 109 | 97 / 445 | 193 / 893 |
| `a * b` | 174 / 635 | 1,226 / 4,791 | 3,610 / 14,087 |
| `a / b` | 376 / 1,522 | 5,344 / 22,954 | 20,928 / 90,954 |
| `a < b` | 17 / 58 | 65 / 250 | 129 / 506 |
| `a == b` | 10 / 40 | 34 / 160 | 66 / 320 |

`*` and `/` between two variables get a block of their own whose graph reads only the two operands. Every site with the same operation and type then shares one graph record. Graphs are deduplicated by content in general. [`docs/Project.md`](Project.md#example-from-source-to-circuit) gives `left && right` on two Boolean parameters as an example. It converts to a block whose graph is that single neuron, `relu(left + right - 1)`, and the conversion tests check the neuron and its truth table.

## Blocks, frames, and exits

A block's graph computes every pure operation of a straight-line region, the selector, and its checks. The block ends at the first effect, call, state operation, or control-flow decision. Statements record slot assignments as pending and do not write them. Every non-trap exit commits them in parallel, so a later read in the same block uses the computed lanes, and a swap through a temporary is correct. A graph value that must outlive its block is assigned to a temporary slot by that block's exits.

Checks are kept in evaluation order. The first failing check selects a trap exit with its reason, and each normal exit's indicator also requires that no check failed. The selector is the sum of the indicators of the exits whose index has that bit set, so exactly one exit is selected and every selector lane is 0 or 1.

Each source-map entry gives a block's first failing check, else the operation that ends it, else its first statement. A trap diagnostic therefore points at the expression that failed. Paths are relative to the deepest directory that contains every source file, so images carry no build-machine directories. `--no-source-map` omits the optional `SMAP` section without changing any other byte.

Classes become layouts whose inherited fields form a prefix. A method takes its receiver as parameter 0, typed with its declaring class. A dispatch block passes the receiver handle through 64 graph lanes, so each `Call` exit can pass it at the parameter type of its target. NVM checks the handle against that type when it decodes the lanes, which serves as a checked downcast. Dispatch targets are computed after every reachable function is converted, from the classes the program actually allocates. A tag that matches none of them selects a trap with reason 4.

## Format decisions

Each of these decisions holds in both the writer and the loader and is recorded in [NVR](runtime/NVR.md) where it changes or narrows the format text:

- **`ConstructText` input.** It accepts an array or a list of `uint8`. `AllocateCollection` takes its elements as exit arguments, so an array always has a length fixed at compile time. A list is the only way to build text whose length is known only at run time, which string concatenation needs.
- **Section order.** Sections are ordered by bytewise tag order: `CONS FUNC GLOB GRPH IMPT LAYO META SMAP TYPE`. The table in NVR lists `TYPE` before `SMAP`, but the text requires ascending order, and ascending order wins.
- **Section tags and flags.** Tags must be printable ASCII. A known section must carry exactly its documented required flag; a mismatch is a malformed image.
- **Result slots.** The rule for results applies to every exit with a result: calls, state operations, and both host continuations. A void result requires an absent slot, and any other result requires a slot of a compatible type. A discarded result still gets a temporary slot.
- **References in lanes.** A reference decoded from lanes must name a live handle whose runtime type is compatible with the descriptor type. Otherwise the run traps.
- **Handle order.** Handles are assigned to text and bytes constants in table order, then to each launch argument's text in order, then to the argument array, then to allocations in execution order.
- **Graph inputs.** A constant can supply graph inputs. A text constant supplies its 64-bit handle.

The loader validates every rule in NVR's "Writer and loader requirements" before execution. Each count is bounded by the bytes that remain, using the smallest encoding of one element, and by `LoadLimits`, before storage is reserved.

## Running an image

`neura run image.nvr -- args...` loads the image, binds `neura.console.write_line` to standard output, and runs it. The process status is the entry function's `int32` result; POSIX keeps its low 8 bits. A trap prints a diagnostic and exits with status 70. A load, bind, or usage error exits with status 1. `--max-steps`, `--max-frames`, and `--max-heap-cells` set run limits. The defaults are 100,000,000 steps, 10,000 frames, and 2²⁶ heap cells. A heap cell is one heap entry, object field, collection element, or text byte.

The embedding API is `nvm::Execution`. `advance()` runs until the run completes, traps, or issues a host request. `complete()` supplies the response to a pending request, so a host can suspend the run between them. `nvm::run` drives a run with bound services synchronously. `nvm::bind` matches imports to an `nvm::HostRegistry` by name, version, and signature. Console output goes through an injectable `nvm::ConsoleSink`. `nvm::StringSink` captures exact bytes and can be given a capacity to simulate a failed or partial write. Traps report a kind, such as a program trap with its reason, a bounds fault, an invalid handle, or a limit, along with the function and block, and the source location when the image has a source map.

## Tests and the limits of the evidence

| CTest | Evidence |
| --- | --- |
| `backend-circuits` | Every circuit for `+ - * /`, unary `-`, `==`, and `<` is evaluated by NVM's checked graph evaluator. The check is exhaustive over all 8-bit signed and unsigned operands, and covers boundary and pseudo-random operands for 16, 32, and 64 bits. Each result must equal a guarded C++ reference, a failure must be flagged exactly when the result is undefined, every output lane must be 0 or 1, and no neuron may overflow. |
| `backend-format` | Two hand-assembled images, written from the specification tables with a byte assembler that shares no code with the writer, must decode to hand-written models. The writer must reproduce their bytes exactly. Every prefix of each image is rejected, as is every truncated section, plus header, directory, flag, encoding, limit, and semantic validation errors (references, types, layouts, graphs, selectors, exits, state operations, imports, entry signature, and source map). |
| `backend-vm` | Selectors, non-binary lanes, graph overflow, uninitialized slots, fields, and globals, freed handles, import binding, host error continuations with partial output, suspension, request sequence numbers, ill-typed completions, the step, frame, heap, and handle limits, argument passing, and repeated runs. |
| `backend-nir` | Every example, optimization fixture, and backend program round-trips through `.nir` with every field and span intact, and encodes deterministically. Both build routes give byte-identical images. Every prefix of a `.nir` file is rejected, along with corrupt headers, references, operations, and nesting. |
| `backend-conversion` | Sixteen fixture functions are called directly in the image and compared with the IR reference evaluator: exhaustively over 8-bit operand pairs for six of them and on sampled operands for the rest. Each call must give an equal result, or a reason-1 trap where the evaluator reports an unspecified result. Whole-program stdout is compared with the evaluator's effect list. Optimized and unoptimized images must produce the same output. Further tests check the single-neuron conjunction, the shared multiplier, deterministic conversion, the optional source map, and a diagnostic and location for each unsupported case. |
| `backend-cli` | Hello World is built from `.nra` and from `.nir`, twice each, and compared byte for byte. The image is copied into a directory that holds only the image and run there, and stdout and the exit status are compared exactly. The test also checks that no build-machine path is in the image, that arguments reach the program, the runnable examples, example drivers, programs in three build variants, nine trap programs (stdout, status, and diagnostic), unsupported builds that leave no output, and option errors. |
| `backend-install` | `cmake --install` into a fresh prefix, then the installed `neura` builds Hello World and runs the image from a directory without sources. |

These are observed agreements over the stated domains. Only the 8-bit circuit and differential checks are exhaustive, and nothing here is a proof. The differential tests rely on the IR evaluator's assumptions about module globals, module dispatch, and `void` external responses. The evaluator does not model objects or collections, so their behavior is checked against hand-derived expected output, not against an independent evaluator. Runs that reach the evaluator's budget or depth limit are skipped. MSVC builds have not been verified.

## Example status

| Example | Status |
| --- | --- |
| `hello-world/hello-world.nra` | Compiles and runs: `Hello, World!`. |
| `modules/module-interface.nra` | Compiles and runs. It assigns fields after `new`, which relies on the open field-initialization rule; the runtime traps if a field is read before it is assigned. |
| `modules/multi-file/main.nra` | Compiles and runs with `-I examples/modules/multi-file`: `Hello, Neura!`. |
| `modules/multi-file/acme/greetings.nra`, `…/punctuation.nra` | Libraries, compiled and run as part of `main.nra`. |
| `guide/core.nra`, `types/overlay.nra`, `types/classes.nra` | Libraries without an entry point. Drivers in `tests/backend/examples/` import them, compile, and run with checked output. |
| `types/collections.nra` | Depends on an unresolved rule: `length` has type `int`, whose width is open. |
| `types/polymorphism.nra` | Generic functions and classes are not supported by the converter. |
| `testing/math-tests.nra` | Needs the `Test` module and its runner, which are not specified. |
| `guide/resources.nra` | Uses a top-level statement and a nonconstant module initializer, and program initialization order is open. |
| `guide/errors.nra` | Design sketch with no code. |
