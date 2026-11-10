# Neuron conversion and execution

This page describes the implemented back half of the toolchain: `IRtoNeuronConversion`, the NVR writer and loader, and the Neura Virtual Machine. [NVM](runtime/NVM.md) and [NVR](runtime/NVR.md) define the contracts. This page records how the implementation meets them, the decisions it takes where the language is still open, and what the tests establish.

## Pipeline

```text
hello.nra --lower--------+
                         +--> verify --> specialize --> optimize --> convert --> encode --> load (self-check) --> hello.nvr
hello.nir --read_nir-----+

hello.nvr --> load --> bind imports --> Execution
```

`neura build` takes either route into the same function, `backend::build`, so both routes produce identical bytes from identical NeuraIR. Specialization ([generics](#generics)) runs on verified IR and is always applied. `--no-optimize` replaces optimization with a second verification. Before writing the image, the build loads the encoded bytes through the full validating loader, so the converter or writer cannot publish an image that the runtime rejects. The output is written to a temporary file and renamed, so a failed build leaves no output file.

| Directory | Library | Contents |
| --- | --- | --- |
| `src/neura-ir/nir_format.*` | `neura_compiler` | The `.nir` reader and writer. [NeuraIR file format](NirFormat.md) specifies the encoding. |
| `src/neura-ir/specialize.*` | `neura_compiler` | Generic specialization. |
| `src/neura-ir/float.*` | `neura_compiler` | Float formats and exact decimal literal conversion. |
| `src/backend/circuit.*` | `neura_compiler` | Neuron circuits for bits and fixed-width integers. |
| `src/backend/float_circuit.*` | `neura_compiler` | Neuron circuits for IEEE floating point in any exponent and fraction width. |
| `src/backend/block_synth.*` | `neura_compiler` | Block assembly: inputs, checks, pending assignments, exits, and the selector. The converter and the decompiler share it. |
| `src/backend/converter.*` | `neura_compiler` | `IRtoNeuronConversion` and the build pipeline shared by both routes. |
| `src/nvr/` | `neura_nvr` | The image model, the writer, and the validating loader. |
| `src/nvm/` | `neura_nvm` | Execution, graph devices and layered tables, fleets, host services, the console sink, and host traces. |
| `src/gpu/` | `neura_gpu` | Optional Vulkan graph evaluator with a dynamically loaded Vulkan loader. |
| `src/runner/` | `neura_runner` | Shared host setup, trace handling, and run diagnostics. |
| `src/launcher/` | `neura_launcher`, `neura_launcher_static` | CPU launchers and the executable container. |
| `src/decompiler/` | `neura_decompiler` | Assembly, lifting, and the reconstruction form ([Decompiler](Decompiler.md)). |

`neura_nvm` links only `neura_nvr`, so a runtime-only distribution needs no compiler code. The `neura` executable links both and provides `neura run`.

## Supported NeuraIR

Conversion lowers an operation only where the language defines its result. Where a result depends on data, such as overflow, the graph checks the operands and takes a trap exit when the result is undefined.

| NeuraIR | Lowering |
| --- | --- |
| `boolean`, `integer` literals | Constant lanes folded into neuron biases, or constant-pool entries when an exit needs the value. |
| `string` literal | A UTF-8 text constant. |
| `nil`, class identity, type tests and casts | A zero reference constant, lane comparison for reference identity, and typed layout checks in NVM. |
| `load`, `store`, `initialize` of locals and parameters | Typed frame slots. Assignments become parallel slot assignments on the block's exits. |
| `load`, `store` of module globals | `ReadGlobal` and `WriteGlobal` state operations. |
| `unary !`, `binary == !=` on `bool` | Graph gadgets. |
| `unary -`, `binary + - * / %` on `int8` to `uint64` | Checked integer circuits with trap exits for overflow, a zero divisor, and `MIN / -1`. |
| `unary ~`, `binary & ^ \| << >>` on integers | Per-lane Boolean circuits and fixed-width shift circuits with a trap exit for out-of-range counts. |
| `float` literals, `unary -`, `binary + - * /` on the four float formats | Float circuits; negation only flips the sign lane. |
| Comparisons on integers, floats, enums, and `bool` | Graph circuits. |
| `convert` | Integer range checks, float rounding, and truncation circuits, with trap exits where [Numbers](lang/Numbers.md#conversions) specifies them. |
| `length` | `ReadLength`, then a checked narrowing of the `uint64` count to `int32`. |
| `collection_version`, `fail` | `ReadVersion` and a reason-5 `Trap` exit, for for-each loops. |
| `binary +` on `string` | A call to a generated concatenation function. |
| `short_circuit` | One graph neuron when the right operand is pure and total. Otherwise the left operand selects a block that evaluates the right operand or a block that skips it. |
| `comparison_chain` | Initialize the hidden subject slot once, then lower the comparisons and lazy connectors using the ordinary comparison and short-circuit rules. |
| `branch`, `loop`, `break`, `continue`, `block`, `return` | Blocks connected by `Continue` exits that the graph selects, and `Return` exits. A loop's latch gets its own block, which `continue` reaches. |
| `call` | A `Call` exit. Module functions are specialized for their module. |
| `virtual_call` on a module | Resolved statically: the most derived implementation in the concrete module, specialized for that module. |
| `virtual_call` on an object | `ReadTag`, then a dispatch block whose graph compares the tag with each instantiated class that can reach the call site. |
| Interface dispatch | The same receiver and tag as class dispatch. Layout links identify interface supertypes. A concrete method wins over a default, and inherited defaults follow the specificity rules. |
| `external_call` to a declared host binding | A versioned `Host` exit whose typed continuations construct the declared success or error variant. |
| Value members, text primitives, and float bit access | Scalar comparisons and hashes use graph circuits, text uses checked state operations, and structural aggregates use generated typed functions. Raw float reinterpretation reuses the input lanes. |
| `trap_message` | A reason-3 message trap carrying one text value. |
| `allocate`, `base_initialize` | `AllocateObject`, then a generated constructor function. |
| `field` | `ReadField` and `WriteField` with the field's index in its declaring class's layout. |
| `collection`, `index`, `append`, `remove_at` | `AllocateCollection`, `ReadElement`, `WriteElement`, `AppendElement`, and `RemoveElement`. |
| `destroy` | `ReadTag`, dispatch to a generated destructor for the runtime class, then `FreeObject`. |
| `discard` | Evaluates the operand, including its checks, and drops the value. |

The build stops with a source diagnostic, before any output is written, for:

- Programs without a unique entry override from `Std.Program.Entry`.
- A module call that resolves to an interface function without a body.
- Generic specialization that does not terminate ([Polymorphism](lang/Polymorphism.md#type-arguments-and-specialization)).

## Generics

Specialization (`src/neura-ir/specialize.cpp`) runs on verified IR in both build routes. Each concrete application of a generic class, interface, enum, function, or method gets one copy, keyed by declaration and canonical arguments. A copy retains its declaration's span and substitutes types in members, fields, interface edges, variant payloads, and bodies. The rewriter remaps variant and member references to their instances. A structural callable becomes a signature layout with an invoke method, and each closure's capture layout implements that method. After specialization the reachable program has concrete types. The verifier rechecks bounds because a `.nir` file reaches the backend without lowering.

## Loops

A post-test loop with a trailing condition uses a private first-iteration flag. Its first trip through the loop head bypasses the condition. The body clears the flag so that fallthrough and `continue` test the condition before another iteration. An unconditional `do` loop uses the existing constant-true loop.

A three-part `for` lowers to a block holding the initializer and a NeuraIR loop whose latch region holds the step. The converter gives the latch its own block: the body's end and every `continue` jump to it, and it jumps to the loop head. A for-each lowers to hidden locals for the collection, its version (for a list), and an `int32` index, and a `while (true)` loop whose body checks the version, compares the index with the length, binds the element, advances the index, and then runs the user's statements. `continue` therefore re-enters the check and the advance, and `break` and `return` leave without them. NVM keeps each list's version and `ReadVersion` reads it.

## Initialization

The image's entry is a generated start function. It runs each initialization statement in the [documented order](lang/Modules.md#initialization), writing globals with `WriteGlobal`, then calls `main` with the arguments and maps the `Std.Program.ExitCode` it returns to the process status: one block per payload-free variant compares the tag with that variant's ordinal and returns its fixed status (`Ok` 0, `Failure` 1, then `SeriousFailure` through `FileSizeExceeded`), and `Status(n)` returns `n` after two blocks check `n < 0` and then `n > 255`, each a comparison with one constant, and trap with `invalid argument: exit status` otherwise. Literal initializers stay image constants, so their globals are initialized at load. Under `neura test`, each test gets a start function that runs the same initialization, calls the test, and returns 0. Files that import each other form a cycle that starts once the files it imports from outside have run.

## Semantic decisions

These choices follow the existing contracts where they apply. Where the language is open, the backend takes the narrowest choice that agrees with every value the language could later define. A program that never reaches an undefined case behaves the same under any future decision.

| Area | Language status | Backend behavior |
| --- | --- | --- |
| Integer overflow | [Specified](lang/Numbers.md) | `+`, `-`, `*`, and unary `-` trap with reason 1 when the exact result is outside the type. Nothing wraps or saturates. |
| Division | Specified | `/` truncates toward zero and `%` takes the dividend's sign. A zero divisor and `MIN / -1` trap with reason 1; `MIN % -1` is 0. Before this release an inexact quotient trapped; it now truncates. |
| Floating point | Specified | IEEE 754 with ties-to-even rounding, subnormals, and no traps. Every NaN result is the canonical quiet NaN. The optimizer does not fold float arithmetic. |
| Conversions | Specified | Checked: out-of-range integer results and float-to-integer conversions of NaN, infinities, and out-of-range values trap with reason 1. |
| Negative index | Bounds error | A signed index is checked for a negative value in the graph, which traps with reason 2 before the value is converted to the `uint64` index of the state operation. NVM reports an index at or beyond the length as a bounds fault. |
| Entry point | [Specified](lang/Modules.md) | The unique function that overrides `Std.Program.Entry.main`. The entry maps `Ok`, `Failure`, and checked `Status(int)` to a process status. The built-in `SysIo` entry is retired. |
| Module receivers | Dispatch by override chain | Static. A module function is converted once for each concrete module through which it is called. Inside it, calls and global accesses on the module receiver resolve in that module's chain. |
| Module globals | Storage is open | One storage location per declared global. An inherited global that is not overridden is the same storage through the base and the derived module. An override has its own storage. The IR reference evaluator uses the same rule. |
| Initialization order | [Specified](lang/Modules.md#initialization) | Literal initializers become NVR global initializers. Other initializers and top-level statements run in a generated start function, files after their imports and each file in source order. A read before initialization is an NVM fault. Optimization keeps the root form of every global initializer, so `2 * 3` is not folded into a literal that would change when its global is set. |
| Enums | [Specified](lang/Types.md#enums) | Fieldless enums use `uint32` ordinals. Payload enums use typed layouts with an ordinal tag and the active fields. The legacy exit enum uses `int32`. |
| Construction | Proposed in [Object lifecycle](lang/ObjectLifecycle.md) | The base constructor runs, then this class's field initializers, then the rest of the declared `init`. Fields without an initializer start uninitialized; reading one before a store traps. |
| Destruction | Proposed | Cleanup dispatches on the runtime class and runs each `deinit` from the most derived class toward the bases. Then the object is freed. A later use of the handle traps. |
| Console write failure | [Specified](lang/Stdlib.md#111-stdio) | `Console.print_line` traps with reason 3 and `console write failed`; `try_print_line` and the other console forms return an `Io` error. |
| Assertions | [Specified](lang/Testing.md) | A failed `Std.Test` assertion is a message trap (reason 3) whose message the test runner reports. |
| For-each modification | [Specified](lang/CoreLanguage.md#loops) | A changed list version traps with reason 5 at the `for` statement. |
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
- Division uses restoring division, one quotient bit per column; the remainder comes out of the same circuit. Signed division divides magnitudes and applies the result's sign; the remainder takes the dividend's sign.
- Integer conversions extend or truncate lanes and check that every dropped lane equals the sign the target needs.
- Integer shifts use one mux stage per power of two below the word width. The left shift fills with zeros, and the right shift fills with zeros or the sign bit. Any set lane above the count lanes used by the stages selects a reason-1 trap.

Float circuits (`src/backend/float_circuit.cpp`) are written once for any exponent and fraction width and instantiated for the four formats. An operand is unpacked into sign, exponent, and significand with an explicit leading bit, and flags for NaN, infinity, and zero. Addition aligns the smaller operand with a barrel shifter that keeps a sticky bit, adds or subtracts with three extra lanes (guard, round, sticky), and normalizes. Multiplication forms the full product of the significands with the column multiplier. Division normalizes both significands and runs restoring division for `fraction + 3` quotient bits, with the remainder as sticky. Every operation and conversion ends in one shared round-and-pack step: a leading-zero normalization limited by the exponent, a right shift into the subnormal range, round to nearest even, and overflow to infinity. Special cases select the canonical NaN, an infinity, or a signed zero last.

Measured cost of one signed operation on two variable operands, in neurons and edges:

| Operation | int8 | int32 | int64 |
| --- | --- | --- | --- |
| `a + b`, `a - b` | 25 / 109 | 97 / 445 | 193 / 893 |
| `a * b` | 174 / 635 | 1,226 / 4,791 | 3,610 / 14,087 |
| `a / b` | 376 / 1,522 | 5,344 / 22,954 | 20,928 / 90,954 |
| `a < b` | 17 / 58 | 65 / 250 | 129 / 506 |
| `a == b` | 10 / 40 | 34 / 160 | 66 / 320 |

Float operations on two variable operands, in neurons and edges:

| Operation | float16 | bfloat16 | float32 | float64 |
| --- | --- | --- | --- | --- |
| `a + b` | 755 / 2,595 | 732 / 2,598 | 1,536 / 5,330 | 3,287 / 11,293 |
| `a * b` | 958 / 3,161 | 731 / 2,440 | 2,558 / 8,838 | 7,591 / 26,945 |
| `a / b` | 1,455 / 5,846 | 1,124 / 4,384 | 4,689 / 20,403 | 17,994 / 83,128 |
| `a < b` | 78 / 295 | 78 / 301 | 142 / 589 | 270 / 1,171 |
| `a == b` | 31 / 136 | 31 / 142 | 47 / 254 | 79 / 484 |
| `int32 as` the format | 857 / 2,583 | 832 / 2,589 | 880 / 2,710 | 1,200 / 3,547 |
| the format `as int32` | 314 / 1,191 | 301 / 1,212 | 493 / 2,108 | 898 / 3,429 |

Signed `%` costs about as much as `/`: 363 / 1,490 neurons and edges for `int8`, 5,307 / 22,874 for `int32`, and 20,859 / 90,810 for `int64`.

Integer `*`, `/`, and `%` and float arithmetic between two variables get a block of their own whose graph reads only the two operands. Every site with the same operation and type then shares one graph record. Graphs are deduplicated by content in general. An operand computed in the graph before such an operation is first assigned to a temporary slot, because the isolated operation ends the block. [`docs/Project.md`](Project.md#example-from-source-to-circuit) gives `left && right` on two Boolean parameters as an example. It converts to a block whose graph is that single neuron, `relu(left + right - 1)`, and the conversion tests check the neuron and its truth table.

## Blocks, frames, and exits

A block's graph computes every pure operation of a straight-line region, the selector, and its checks. The block ends at the first effect, call, state operation, or control-flow decision. Statements record slot assignments as pending and do not write them. Every non-trap exit commits them in parallel, so a later read in the same block uses the computed lanes, and a swap through a temporary is correct. A graph value that must outlive its block is assigned to a temporary slot by that block's exits.

A block that evaluates nothing and only continues to another block, such as the header of `while (true)` or the join after an `if`, would cost a step and do no work. After every function is converted, exits that resume at such a block resume at the end of its chain instead, and the bypassed blocks are dropped with the other unreachable blocks; a chain that cycles, such as an empty endless loop, is kept. This removed 2 to 16 percent of the steps of the standard library benchmarks (`tests/benchmarks`).

Checks are kept in evaluation order. The first failing check selects a trap exit with its reason, and each normal exit's indicator also requires that no check failed. The selector is the sum of the indicators of the exits whose index has that bit set, so exactly one exit is selected and every selector lane is 0 or 1.

Each source-map entry gives a block's first failing check, else the operation that ends it, else its first statement. A trap diagnostic therefore points at the expression that failed. Paths of program files are relative to the deepest directory that contains every program file, and standard library files are labeled by their path below the library root (`std/core/optional.nra`), so images carry no build-machine directories. `--no-source-map` omits the optional `SMAP` section without changing any other byte.

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
- **Minimal versions.** The writer emits NVR 1.0 unless the image uses a 1.1 feature: a float type, `ReadVersion`, or trap reason 5. Images of programs that use none of them are 1.0.
- **Optional sections.** The loader keeps unknown optional sections as raw bytes and the writer writes them back in tag order, so decoding and re-encoding any valid image reproduces its bytes. With the source map, the compiler writes the optional `NAME` section for tools.

The loader validates every rule in NVR's "Writer and loader requirements" before execution. Each count is bounded by the bytes that remain, using the smallest encoding of one element, and by `LoadLimits`, before storage is reserved.

## Running an image

`neura run image.nvr -- args...` loads the image, binds `neura.console.write_line` to standard output and the test assertions to services that record failures, and runs it. The process status is the entry function's `int32` result; POSIX keeps its low 8 bits. A trap prints a diagnostic and exits with the status `nvm::exit_status` assigns its class: 136 for arithmetic, 139 for bounds, uninitialized reads, nil, and frame depth, 134 for message traps, iteration changes, and failed casts, 132 for invalid dispatch and malformed graphs, 137 for heap and handle exhaustion, and 152 for the step limit. A load, bind, or usage error exits with status 1. `--max-steps`, `--max-frames`, and `--max-heap-cells` set run limits. The defaults are 100,000,000 steps, 10,000 frames, and 2²⁶ heap cells. A heap cell is one heap entry, object field, collection element, or text byte.

The embedding API is `nvm::Execution`. `advance()` runs until the run completes, traps, or issues a host request. `complete()` supplies the response to a pending request, so a host can suspend the run between them. `nvm::run` drives a run with bound services synchronously. `nvm::bind` matches imports to an `nvm::HostRegistry` by name, version, and signature. Console output goes through an injectable `nvm::ConsoleSink`. `nvm::StringSink` captures exact bytes and can be given a capacity to simulate a failed or partial write. Traps report a kind, such as a program trap with its reason, a bounds fault, an invalid handle, or a limit, along with the function and block, and the source location when the image has a source map.

The NVM library defaults to the reference evaluator. `nvm::Execution`, `nvm::run`, `nvm::record`, `nvm::replay`, and `nvm::Fleet` use a graph device only when the caller supplies one. Declined jobs use the reference evaluator. With no usable device, CLI auto and always modes fall back to the reference evaluator without a diagnostic unless `--require-gpu` is set. In that case startup fails before entry. `nvm::Fleet` advances independent executions of one validated image and offers each round as a batch to an optional device. `nvm::run_fleet` drives host requests and callbacks in member order. `neura run --each` takes argument lists from a file. The CLI supports automatic placement, an explicit Vulkan mode, and CPU-only evaluation. Its `--stats` report includes placement and fallback information. [NVM fleets](runtime/NVM.md#fleets) [Tooling](lang/Tooling.md#run-an-image-over-argument-lists)

A packaged executable starts the CPU runner with its recorded limits and capabilities. The default launcher provides native interop under its recorded capability. The static launcher supplies Unsupported for those imports, and the packager rejects images that use them. [Native executables](lang/Standalone.md)

## Tests and the limits of the evidence

The [release checklist](Release.md) maps each feature to its tests and states which checks are exhaustive, sampled, or structural. In summary:

| CTest | Evidence |
| --- | --- |
| `backend-circuits` | Integer circuits, including `%` and conversions between every pair of formats, are evaluated by NVM's checked graph evaluator: exhaustively over 8-bit operands (and every 8- and 16-bit source value for conversions), on boundary and pseudo-random operands for wider types. |
| `backend-floats` | Float circuits against two independent references: exhaustively over every pair of the 6- and 8-lane formats e3m2 and e4m3 against a brute-force exact rational reference and a hardware double reference; exhaustively over every float16 and bfloat16 value for negation, conversions to and from wider formats and integers; on special and pseudo-random operands for float16, bfloat16, float32, and float64 arithmetic and comparisons against hardware float and double; literal conversion against `strtod` and `strtof`. |
| `backend-format` | Hand-assembled 1.0 and 1.1 images decode to their models and the writer reproduces their bytes; malformed images, version mismatches, and 1.1 features in 1.0 images are rejected. |
| `backend-vm` | Execution semantics of NVM. |
| `backend-layered` | Packed graph tables, exact CPU model evaluation over the integer circuit domains, and traps and results across the image corpus. |
| `backend-fleet` | Step transitions and member-order equivalence across the image corpus, fleet state and limits, and all 65,536 pairs for one 8-bit function. |
| `backend-each` | Argument-list parsing, CLI diagnostics, and serial output, statistics, and status. |
| `backend-placement` | Cost model fits, deterministic gate accounting, placement, timing correction, and fleet equivalence with fixed models. |
| `gpu-device` | Vulkan graph outputs over the integer circuit domains, mixed and split batches, buffer growth, and calibration. |
| `gpu-cli` | CPU and Vulkan runs, tests, fleets, and trace replay compared through the CLI. |
| `backend-nir` | `.nir` round trips, deterministic encoding, both build routes give identical images, corrupt files are rejected. |
| `backend-conversion` | Compiled functions against the IR reference evaluator: integer, structure, and float fixtures, exhaustively over 8-bit domains where the parameters allow and on sampled float operands; whole-program stdout and initialization order against the evaluator. |
| `backend-generics` | Instance caching, determinism, the specialization limits, and specialized functions against the evaluator exhaustively over 8-bit inputs. |
| `backend-cli` | End-to-end builds and runs, reproducibility, isolated execution, every runnable example and driver, trap programs, unsupported builds, the test runner, and option errors. |
| `backend-install` | The installed toolchain builds and runs Hello World from a directory without sources. |
| `native-build`, `native-run`, `native-container` | Byte-preserving packaging, differential execution, and malformed container rejection. |
| `native-policy`, `native-replay` | Recorded grants and limits, environment overrides, sealing, and cross-runner traces. |
| `native-install`, `native-c-import` | Relocated launchers, static execution in a root with only the executable and procfs, and C-import target checks. |
| `decompiler-*` | See [Decompiler](Decompiler.md#evidence). |

These are observed agreements over the stated domains; the exhaustive checks are exhaustive only over the stated domains, and nothing here is a proof beyond them. The IR evaluator models objects, collections, floats, and initialization independently of the converter, but it shares the language rules as written in these pages, so a misreading common to both would not be caught. MSVC builds have not been verified.

## Example status

[Feature status](lang/Status.md#examples) lists every example. All runnable examples run, and every library example has a driver in `tests/backend/examples/` that builds and runs it.
