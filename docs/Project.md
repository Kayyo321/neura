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

The stages should be inspectable independently. A diagnostic or test should be able to identify whether a discrepancy came from source analysis, an IR transformation, lowering, serialization, or circuit execution.

## NeuraIR and optimization

NeuraIR is the semantic boundary shared by the compiler, optimization passes, and verification tools. It should represent typed values and operation widths, function signatures and calls, control-flow edges, reads and writes, and the operations that can fail or request an external effect. Module names and object layouts should be resolved before the backend relies on them. The IR verifier should catch invalid operands, incompatible control-flow edges, and malformed state or effect operations before lowering.

A transformation preserves observable behavior for the inputs, initial state, and execution model defined by the language and target. Observable behavior includes return values, visible state changes, the order and payload of external effects, and specified failures. Termination is part of program behavior. A bounded comparison only establishes behavior within the stated bound.

Constant folding, dead code elimination, and branch simplification are useful when their preconditions hold. Folding arithmetic requires the defined width, overflow, rounding, and failure rules. Removing an unused value is safe only when computing it has no observable effect, including a possible failure or divergence. Speculating both sides of a branch can be valid for total, effect-free computations. A branch that might divide by zero or write to memory requires gating that preserves the language's evaluation order.

Measure an optimization using relevant circuit costs such as neuron and edge count, graph depth, state size, execution steps, or artifact size. State the target and workload so a reduction in one cost can be evaluated alongside changes in the others.

## Neuron representation

A circuit consists of neurons with a defined activation, weighted connections, and biases. The target defines the numeric domain, evaluation rules, and supported connectivity. For Boolean inputs in `{0, 1}`, a ReLU neuron with weights `1, 1` and bias `-1` computes `max(0, a + b - 1)`. It yields `1` for two true inputs and `0` otherwise. The compiler can compose primitives like this into circuits for logical operations, comparisons, arithmetic, and selection.

An exact integer target can encode a fixed-width value as a vector of bits and implement carries, comparisons, and selection as circuits. A different encoding is viable if it preserves the same behavior over the declared domain. The backend must account for intermediate numeric values as well as the encoded inputs and outputs. A correct circuit under exact arithmetic can behave differently if its executor rounds intermediate sums or changes the reduction order. Floating-point and low-precision types need explicit semantics and a compatible target before lowering.

### Example from source to circuit

```neura
let both = func(bool left, bool right) -> bool {
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
| Loops and recursion | A transition circuit can update control state and revisit a region. Recursive calls require storage for active frames and return continuations, with a defined limit for finite targets. |
| Classes and objects | Classes define layouts and method circuits. Each object has its own state, and method calls pass a receiver reference to shared circuits. |
| Dynamic dispatch | A method call resolves from the runtime type of the receiver using the declared class interface. A backend can lower this to a type tag and circuit selection. |
| Collections and lifecycle | Indexing, allocation, initialization, and destruction require explicit bounds, lifetime rules, and failure behavior before their lowering is defined. |

### Runtime boundary

The executor evaluates the neuron graph and provides the declared state and capability interfaces. A target may implement state transitions entirely in a recurrent graph or pair the graph with a small runtime for storage, scheduling, and effects. In either case, the compiled arithmetic, comparisons, and control decisions are represented by the circuit. Runtime services such as memory access and console I/O have explicit operations and documented failure behavior. An executor should be able to replay effect requests with recorded responses when deterministic comparison requires it.

## Artifact and inspection

The output format should identify the format version, neuron semantics, graph topology, weights and biases, and input and output encodings. Stateful artifacts also need state layout, initialization, transition and halt conventions, limits, and the signatures of required runtime capabilities. The loader should validate references, shapes, versions, and declared capabilities before execution. Canonical ordering and encoding should make repeated builds with identical inputs, compiler version, options, and target reproducible.

Source maps, names, type names, and optimization provenance can be included as optional metadata. Inspection tools should show the structure and parameters of a circuit, the memory or effect ports it uses, and source or IR locations when available. One source operation can map to several neurons, and optimization can combine or remove source operations. The inspector should reflect those relationships. Editing weights produces a changed artifact whose behavior can be evaluated and checked again.

## Decompilation

Decompilation starts with the serialized circuit. It validates the artifact and builds a graph representation from the executable structure and execution interface. For supported circuits it can recognize primitive patterns, recover typed operations where the encoding is known, and reconstruct data flow and control flow where there is sufficient evidence. A readable circuit representation is the fallback for regions that cannot be lifted to NeuraIR. Optional source-like output can build on the recovered representation.

Original names, comments, and source structure may be absent, and several different programs can produce equivalent circuits. Provenance can improve readability. The recovered program's behavior still needs to be checked against the graph and weights, especially when the artifact was optimized or edited. A decompiler should identify opaque regions and report assumptions used to recover types, control flow, or state. A partial recovery can still be inspected and compared within its supported domain.

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

## Implementation order

1. Define Boolean and selected fixed-width integer semantics. Compile a small pure function through the full pipeline, emit a circuit, and run it from the artifact.
2. Add a reference evaluator and verify lowering and optimization against it. Show an optimized circuit's weights and trace their relationship to NeuraIR.
3. Validate the artifact contract at load time and recover supported combinational circuits into readable operations. Check equivalence for both compiler output and deliberately modified artifacts.
4. Extend the execution model to explicit state, bounded resources, and defined failure paths. Add functions with state, loops, and recursion as their semantics and target contracts are specified.
5. Extend the same approach to modules, object state, dispatch, and collections. Introduce external effects through named capabilities with replayable test inputs.

The numeric rules, artifact format, runtime boundary, and decompiler's supported input class need concrete specifications as each implementation step selects them. Language decisions outside the compiler contract are tracked in [Feature status and implementation decisions](lang/Status.md).
