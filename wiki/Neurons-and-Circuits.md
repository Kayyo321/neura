# Neurons and circuits

Neura compiles a program into graphs of simple mathematical units called **neurons**. Their connections and parameters are constructed from the program; executing an image evaluates those graphs under a specified runtime contract. This page explains the model and walks through a Boolean circuit. For the exact encoding and implementation, see [Neuron conversion and execution](Backend), [NVM](NVM), and [NVR](NVR).

## Neurons as mathematical functions

A neuron takes values from its incoming connections, multiplies each by a weight, and adds a bias. For inputs `x₁, …, xₙ`, weights `w₁, …, wₙ`, and bias `b`, its pre-activation is:

```text
z = b + w₁x₁ + ⋯ + wₙxₙ
```

An **activation** maps that sum to the neuron's output. The reference execution profile supports identity (`identity(z) = z`) and rectified linear activation (`ReLU(z) = max(0, z)`). Connections form a directed graph. A feedforward graph has no directed cycles, so neurons can be evaluated in topological order: each node's inputs are known before it runs.

The terms neuron, weight, and activation also appear in machine learning, but Neura's compiler constructs the topology and parameters to implement program behavior. The mechanism described here does not require training on examples. In particular, a circuit's correctness depends on its encoding and execution rules; a mathematical resemblance to a neural network alone does not establish correctness.

## Boolean logic from a ReLU

Encode `false` as `0` and `true` as `1`. A single ReLU computes conjunction:

```text
AND(a, b) = ReLU(a + b - 1)
```

Its weights are `1` and `1`, and its bias is `-1`. Evaluating all four valid inputs gives:

| `a` | `b` | Weighted sum | Output |
| --- | --- | --- | --- |
| 0 | 0 | -1 | 0 |
| 0 | 1 | 0 | 0 |
| 1 | 0 | 0 | 0 |
| 1 | 1 | 1 | 1 |

This truth table is an **exhaustive check** for this gate over its declared two-bit input domain. It does not say anything about arbitrary real-valued inputs: for example, changing the domain changes what must be verified. Negation can be expressed as `1 - a` and disjunction as `1 - ReLU(1 - a - b)` on Boolean inputs. The backend composes these and other gadgets into larger operations.

## From values to circuits

At graph boundaries, values use bit lanes. Lane zero is least significant; a Boolean has one lane, an integer has its declared width, and signed integers use two's-complement encoding. Floating-point lanes carry the bits of their IEEE encoding. A reference is an opaque 64-bit handle, not the object itself. Graph outputs used by an exit must be bits: each selected lane must be exactly `0` or `1`.

Integer addition can be assembled from bit-level gates and carry logic. Wider arithmetic composes more gates: the current backend uses ripple carry for addition and subtraction, partial products for multiplication, and restoring division. Floating-point arithmetic unpacks sign, exponent, and significand and implements alignment, arithmetic, normalization, rounding, and special cases with circuits. The [backend reference](Backend#circuits) describes those constructions and gives measured neuron and edge counts. The [numeric language rules](Numbers) remain the source-level contract for widths, rounding, conversions, and failures.

A graph may compute a failure check as well as a value. For example, the source language's checked integer overflow must be represented by a corresponding trap path. Separately, graph-profile weighted sums use checked signed 64-bit arithmetic. A circuit must stay within that target's arithmetic bounds for the inputs it claims to support. Target overflow and source-level arithmetic overflow are different concepts.

## From source to an image

```text
source → lexer → parser → typed NeuraIR → optimization
       → neuron conversion → NVR image → NVM execution
```

For `left && right`, the parser represents the expression; semantic lowering determines its types and short-circuit behavior. The compiler may use a single conjunction neuron when reading the right operand is pure and total. If evaluating it can have effects or fail, control flow must preserve the language's evaluation order. The circuit and its declared interface, rather than original source text, are what the runtime executes. See [Project design](Project) for the contracts between stages and [NeuraIR optimization](IrOptimization) for permitted transformations.

## State, loops, and effects

A feedforward graph computes a result from its inputs once; a complete program can also have loops, calls, mutable state, and external interactions. NVM evaluates a graph for a block and follows its selected exit. A continuation can enter another block or revisit a block, which gives the program control flow without requiring a cyclic feedforward graph. Calls have frames, and state operations access explicitly represented globals and heap objects. Method circuits can be shared while each instance retains independent fields.

Host effects cross a declared interface. An image names its host imports and the runtime binds compatible services; the graph selects an effect exit, while the service handles the external interaction. Determinism depends on the artifact, inputs, initial state, runtime configuration, limits, and declared external responses. Recorded host interactions can be replayed. The detailed transition, memory, and host rules are specified in [NVM](NVM).

## Inspecting and validating results

Use `neura emit-ir` to inspect typed operations, `neura disassemble` to inspect an image's graph and tables, and `neura decompile` to recover a readable representation. The [Decompiler](Decompiler) distinguishes lossless assembly from inferred or structurally recovered operations. Optional source metadata helps label output but is not proof of behavior.

Verification must state what is compared, over which inputs and initial states, with which runtime limits and external responses. A truth table can exhaust a small domain; testing selected values is evidence only for those cases; a formal proof needs an explicit model and assumptions. For stateful programs, bounded comparisons establish behavior only within the stated bound. See [Project design: testing and verification](Project#testing-and-verification) and the [release checklist](Release) for the repository's checks.
