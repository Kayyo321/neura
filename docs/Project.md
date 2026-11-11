# Neura project design

Neura is a statically typed, high-level language with a compiler and decompiler for deterministic neuron circuits. A Neura program defines a computation. The compiler translates that computation into a graph of neuron primitives, with weights and biases assigned directly from the program. An executor evaluates the graph with its declared inputs and state. A decompiler reads the generated artifact and reconstructs a representation that a developer can inspect and verify.

The [language specification](lang/Overview.md) describes the surface language, including modules, functions, classes, and collections. This document defines how the toolchain should preserve those semantics and separates the execution model from choices about individual syntax and features.

## Compilation pipeline

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

| Stage | Input and output | Contract |
| --- | --- | --- |
| Lexer | Source text → tokens with source spans | Recognize keywords, identifiers, literals, operators, and delimiters. Report invalid input with a span. Preserve enough location information to point back to source in later stages. |
| Parser | Tokens → syntax tree | Resolve grammar, precedence, and associativity. Preserve declaration and expression spans. Keep syntax errors distinct from errors that depend on names or types. |
| NeuraIR | Syntax tree → validated, typed intermediate representation | Resolve names and imports, check types and declared interfaces, and lower source constructs into explicit control flow, operations, and state access. Reject unsupported semantics with a diagnostic at the relevant source location. |
| IR optimization | Validated NeuraIR → validated NeuraIR | Transform code under the same observable behavior contract. Keep the result valid for the selected target and maintain meaningful source provenance. |
| IRtoNeuronConversion | Optimized NeuraIR → neuron graph and execution interface | Map supported IR operations and control flow to neuron circuits. Define input, output, and state encodings. Diagnose unsupported operations and target limits before emitting an executable artifact. |
| output | Graph and execution interface → serialized artifact | Record the graph, weights, biases, ports, and any required state and capabilities in a versioned format. The executor needs enough information in the artifact and its declared dependencies to run it independently of the source. |

Semantic analysis belongs at the boundary between the syntax tree and valid NeuraIR. By the time optimization starts, operations should have explicit types and effects. For example, the parser can represent `left && right` as a Boolean expression. NeuraIR must express that the right-hand side is evaluated only when the left-hand side is true if evaluating it could change state or fail.

Lowering resolves function overloads and object cast targets from static types. Each call names a single function, and each explicit construction or base initialization names its selected constructor when one is declared. Conversion uses that constructor's symbol and emits its normal field initialization and body. Object casts and primitive companion calls use ordinary typed call operations, including their effects and evaluation order.

When source imports prebuilt NeuraIR, the linker verifies each artifact and remaps its symbol and type references before source lowering. Compatible shared dependency files are merged. Source calls, initializers, and type references then resolve against the combined program. Linked classes keep their compiled layouts and initialization guards. Optimization, circuit conversion, and execution consume the same resolved operations as an all-source build.

Primitive reference bindings name a mutable location in typed NeuraIR. After linkage and before specialization, the compiler moves address-taken storage into heap cells and rewrites ordinary accesses and aliases to use the same cell. Constructors prepare addressed fields and startup prepares addressed global storage. Calls and layout access use the existing object operations. The NVM traces the cell for as long as any alias can reach it.

Range bounds, stride, checked length arithmetic, and element generation lower to explicit NeuraIR before conversion. The executable uses circuits for the arithmetic and control flow and a deterministic NVM state operation to allocate a collection of a computed length. Allocation checks the heap budget before reserving the backing storage.

Class static fields lower to global slots in a class-owned scope. NeuraIR
expresses first-use initialization with a guarded function and a shared
global state slot. Active uses reach the initializer through ordinary calls
and preserve the evaluation order of qualifiers, arguments, and operands.
The guarded function invokes initialization dependencies before executing its
own field initializers. A singleton lowers to one object allocation during
startup and a global reference shared by every use. Both use the existing
circuit state interfaces for global and object access.

Tasks ([Concurrency](lang/Concurrency.md)) change each stage's contract as follows, and the language rules stay in the stage that owns them.

| Stage | Contract for tasks | Hands on |
| --- | --- | --- |
| C import | None. A program that uses a scheduler operation cannot use native bindings. | |
| Lexer | None. `scope`, `spawn`, and `select` remain identifiers. | |
| Parser | Reads `scope`, `spawn`, and `select` as forms only in the positions the grammar gives, with spans for the keyword, the operand, and each arm. It does no typing or name resolution. | Syntax nodes for the three forms |
| NeuraIR | Resolves `Task<T>` and `Scope`, checks that scope-bound values do not leave their scope, and, on the specialized linked program, that spawn arguments and channel messages are transferable and spawned calls are task-safe. Makes scope entry and exit, spawn, every other scheduler operation, and polls explicit (`task_scope`, `spawn`, `scheduler_call`, and a poll flag). Written as `.nir` 1.9. | No operation after this point needs a source-level rule |
| IR optimization | Preserves every executed call, loop back edge, spawn, scope, and scheduler call, and their order. | The same scheduler operations and polls as the unoptimized program |
| IRtoNeuronConversion | Turns each scheduler operation into a typed exit, makes `return`, `break`, and `continue` leave the scopes they cross, puts each poll on the exit that makes the call or takes the back edge, and marks transferable layouts. | NVR 1.5 with the `TRNS` table |
| Output | The scheduler exit, the poll bit, and `TRNS`; an image without tasks keeps its earlier version and bytes. | An image a compatible runtime runs without source |

The NVM runs the tasks of an image on one deterministic scheduler. Scheduler state (task records, queues, channel buffers, timers, guards) is NVM state reached through exits, like the heap; graph profile 1 does not change, and no scheduler operation is a host service. The decompiler reads scheduler exits and polls as decoded records ([Decompiler](Decompiler.md#tasks-and-scheduler-operations)). Host services that declare it overlap: the calling task parks and the completion is delivered at a later scheduling point. A recorded run replays with every scheduling decision ([NVM](runtime/NVM.md#traces-of-images-with-tasks)). A test tool may substitute the choice among runnable tasks and the clock; every such choice is a legal schedule of the same image ([NVM](runtime/NVM.md#the-scheduler)).

A C header import (`import:c(...)`) runs before parsing for the importing file's imports. The C import stage reads the header with libclang and generates the source of an ordinary Neura module that calls the header's functions through `Std.DynamicForeign`; that module then enters the lexer and parser like any other. The tool links the imports' static libraries into one shared object after lowering. The shared object travels in NeuraIR and in the image's `NATV` section, and the circuit never contains native code ([Foreign interop](lang/ForeignInterop.md#c-imports)).

The stages should be inspectable independently. A diagnostic or test should be able to identify whether a discrepancy came from source analysis, an IR transformation, lowering, serialization, or circuit execution.

## NeuraIR and optimization

NeuraIR is the semantic boundary shared by the compiler, optimization passes, and verification tools. It should represent typed values and operation widths, function signatures and calls, control-flow edges, reads and writes, and the operations that can fail or request an external effect. Module names and object layouts should be resolved before the backend relies on them. The IR verifier should catch invalid operands, incompatible control-flow edges, and malformed state or effect operations before lowering.

A transformation preserves observable behavior for the inputs, initial state, and execution model defined by the language and target. Observable behavior includes return values, visible state changes, the order and payload of external effects, and specified failures. Termination is part of program behavior. A bounded comparison only establishes behavior within the stated bound.

Constant folding, dead code elimination, and branch simplification are useful when their preconditions hold. Folding arithmetic requires the defined width, overflow, rounding, and failure rules. Removing an unused value is safe only when computing it has no observable effect, including a possible failure or divergence. Speculating both sides of a branch can be valid for total, effect-free computations. A branch that might divide by zero or write to memory requires gating that preserves the language's evaluation order.

The implemented optimization stage verifies NeuraIR before its first pass and after each pass that changes the program, and runs a fixed sequence of passes until they reach a fixed point. It evaluates only operations whose results the language defines and that do not trap. An operation that traps, such as an overflowing addition or a division by zero, stays at its original position in the evaluation order. Before optimization, generic declarations are specialized for their type arguments. [NeuraIR optimization](IrOptimization.md) describes the passes, their preconditions, the verifier's invariants, and the scope of the tests.

Measure an optimization using relevant circuit costs such as neuron and edge count, graph depth, state size, execution steps, or artifact size. State the target and workload so a reduction in one cost can be evaluated alongside changes in the others.

## Neuron representation

A circuit consists of neurons with a defined activation, weighted connections, and biases. The target defines the numeric domain, evaluation rules, and supported connectivity. For Boolean inputs in `{0, 1}`, a ReLU neuron with weights `1, 1` and bias `-1` computes `max(0, a + b - 1)`. It yields `1` for two true inputs and `0` otherwise. The compiler can compose primitives like this into circuits for logical operations, comparisons, arithmetic, and selection.

An exact integer target can encode a fixed-width value as a vector of bits and implement carries, comparisons, and selection as circuits. A different encoding is viable if it preserves the same behavior over the declared domain. The backend must account for intermediate numeric values as well as the encoded inputs and outputs. A correct circuit under exact arithmetic can behave differently if its executor rounds intermediate sums or changes the reduction order. Floating-point and low-precision types need explicit semantics and a compatible target before lowering.

### Example from source to circuit

<!-- example: check -->
```neura
let both = func(left: bool, right: bool) -> bool {
    return left && right;
};
```

The lexer recognizes the declaration, parameter names, and operators. The parser builds a function with a return expression. NeuraIR resolves both parameters as `bool` and expresses the short-circuit rule. The optimizer can reduce this instance to a pure conjunction because the right operand is already a Boolean parameter and reading it has no effect. Conversion emits the single ReLU neuron described above. The artifact declares two Boolean inputs and one Boolean output. An exhaustive check evaluates the four input pairs and expects outputs `0, 0, 0, 1` in the order `00, 01, 10, 11`.

A decompiler can recognize the conjunction from the declared encoding and the neuron's parameters, and validate the recovered operation against the same truth table. Replacing the bias with `0` changes the result for inputs `1, 1` to `2`, outside the declared Boolean output encoding. The verifier should identify that violation even if the source map still points to `left && right`.

### State and control flow

Feedforward circuits suit computations whose inputs and outputs fit a fixed interface and whose evaluation needs no persistent execution state. Pure computations with statically bounded control flow can also be unrolled when the target can afford the resulting graph. Stateful programs can use a transition circuit. Each evaluation reads the current state and inputs and produces the next state, outputs, and a halt, failure, or effect signal as applicable.

```text
(next state, outputs, status) = step(current state, inputs)
```

The execution contract defines initial state, how and when state commits, how control moves between steps, and what happens at halt, failure, or a resource limit. Given the same artifact, target semantics, input sequence, initial state, limits, and external responses, execution should yield the same observable results. An unbounded source loop can be modeled by repeated steps. A finite artifact and executor have finite storage, and each target must state its resource limits and how exhaustion is reported.

| Source construct | Circuit or state representation |
| --- | --- |
| Bindings and assignment | Immutable values can travel on edges. Mutable values use explicit state slots or memory operations where their lifetime requires it. |
| Functions | A typed call interface passes arguments and returns values. Reusable function circuits can share topology and weights across calls, with independent locals for each active invocation. |
| Closures | A function value uses an object handle whose layout describes its captured values. The signature supplies a common interface and the environment layout selects the function body using ordinary virtual dispatch. |
| Tuples and enum payloads | Typed object layouts store tuple fields or an enum discriminant and variant payload. A match reads the discriminant and selects the region that can read the active payload. |
| Loops and recursion | A transition circuit can update control state and revisit a region. Recursive calls require storage for active frames and return continuations, with a defined limit for finite targets. |
| Classes and objects | Classes define layouts and method circuits. Each object has its own state, and method calls pass a receiver reference to shared circuits. The umbrella `class` type preserves identity and dynamic type. Nil uses the reserved zero reference. |
| Dynamic dispatch | The runtime reads the receiver's layout tag. A generated circuit selects the implementation using class and interface subtype links. A checked cast preserves the handle and checks the target layout relationship. |
| Collections and lifecycle | Indexing, allocation, initialization, and destruction require explicit bounds, lifetime rules, and failure behavior before their lowering is defined. |

### Runtime boundary

The [Neura Virtual Machine](runtime/NVM.md) evaluates feedforward neuron graphs and follows their selected exits. It owns frames, state storage, and host service dispatch. The compiled graph makes arithmetic, comparison, and branch decisions. Heap operations and host effects use explicit requests with typed inputs and continuations. Graph evaluation may run on a GPU, and independent executions may advance in rounds with the same results and host effect order as the reference executor running them serially.

The NVM traces initialized reference roots at allocation safepoints and reclaims unreachable objects, collections, text, and closure environments, including cycles. It skips nil and keeps live handles stable. Explicit destruction and host resource cleanup remain under program control. Trusted standard library sources declare host services with versioned signatures and typed continuations. The runner binds those imports to providers before execution. Native C code is one such provider: [`Std.DynamicForeign`](lang/ForeignInterop.md) reaches it through `neura.ffi.*` requests under an explicit capability, so native code never enters the circuit and its results replay like any other host response.

IRtoNeuronConversion, the NVR writer and loader, and a reference NVM are implemented for the whole language of this release: Booleans, integers, IEEE floating point, enums, control flow including loops, calls, generics after specialization, module dispatch, classes with dynamic dispatch, arrays, lists, text, program initialization, console output, and test assertions. [Neuron conversion and execution](Backend.md) lists the supported operations. It also describes the circuits and their measured costs, the semantics the compiler fixes where the language is open, and the tests that compare compiled graphs with the IR reference evaluator.

## Artifact and inspection

The [NVR format](runtime/NVR.md) defines the versioned executable image emitted by the output stage. It carries the graphs, weights, constants, layouts, execution metadata, and host imports required to run independently of the source. A versioned `.nir` file carries typed NeuraIR across the earlier compiler boundary. A native executable can carry an NVR image unchanged, and inspection reads the image from its container.

Source maps are optional. Inspection tools expose graph topology and parameters, declared memory and effect operations, and source locations when available. Optimization can combine or remove source operations, and inspection can show the relationship where provenance survives. Editing weights creates a changed image whose behavior can be evaluated and checked again.

## Decompilation

Decompilation starts with the serialized circuit. It validates the artifact and builds a graph representation from the executable structure and execution interface. For supported circuits it can recognize primitive patterns, recover typed operations where the encoding is known, and reconstruct data flow and control flow where there is sufficient evidence. A readable circuit representation is the fallback for regions that cannot be lifted to NeuraIR. Optional source-like output can build on the recovered representation.

Original names, comments, and source structure may be absent, and several different programs can produce equivalent circuits. Provenance can improve readability. The recovered program's behavior still needs to be checked against the graph and weights, especially when the artifact was optimized or edited. A decompiler should identify opaque regions and report assumptions used to recover types, control flow, or state. A partial recovery can still be inspected and compared within its supported domain.

The implemented decompiler ([Decompiler](Decompiler.md)) provides a lossless text form of every valid image that reassembles to the same bytes, and a typed reconstruction that labels each block as decoded from the tables, lifted with a structural proof (the block regenerated from the lifted operations is identical to the image's), lifted by exhaustive evaluation, or left as a circuit. It reads only executable content to decide behavior; the source map and the optional NAME section only label the output.

## Testing and verification

Verification is part of the compiler and decompiler workflow. Each check should name its reference semantics, domain, assumptions, and result.

| Check | What it establishes |
| --- | --- |
| Frontend conformance | Valid programs lower with the intended types and source semantics. Invalid programs produce diagnostics at the relevant stage and location. |
| IR transformation | Original and transformed IR agree on values, state, effects, and failures under the pass's preconditions. Include edge inputs and cases where the pass must decline a rewrite. |
| Lowering and execution | A source or IR reference evaluator and the circuit executor agree on identical inputs and initial state. Compare declared effects and failures as well as output values. |
| Artifact loading | A serialized circuit retains its execution semantics after loading. Invalid graphs or unsupported versions fail validation. |
| Decompilation | A recovered representation agrees with the circuit it was lifted from. Include optimized artifacts and artifacts with altered weights or omitted source metadata. |
| Determinism | Repeating a build or run under the same declared conditions produces the same artifact or observable result, respectively. |

Use exhaustive evaluation for small input spaces and targeted boundary and differential tests for larger ones. A solver can prove equivalence for a supported encoding, such as fixed-width bit vectors, provided the model covers the executor's semantics and the relevant state and effects. A bounded stateful proof needs an explicit step or memory bound. General stateful equivalence may require invariants or an inductive argument. Report test coverage, bounded proofs, and unresolved results with their respective scope.

Open language decisions are listed in [Feature status](lang/Status.md#open-language-decisions).
