# Contributing to Neura

Read the [project design](docs/Project.md) for the compiler and circuit model, the [language overview](docs/lang/Overview.md) for source-level behavior, and the [coding style guide](docs/style/CodingStyle.md) when writing `.nra` files. Check [language design decisions](docs/lang/Status.md) before treating a proposed feature or illustrative example as a conformance requirement. Follow the [repository rules](README.md#rules-for-agents) when using a coding agent.

## Define the change

- For a bug, provide the smallest input that reproduces it, the expected behavior, and the actual behavior. Include the first stage where the result diverges if you can identify it. For a runtime issue, include the circuit artifact or its build inputs, the initial state, and any external inputs needed to replay it.
- For a language feature, define the syntax, static rules, observable behavior, and failure cases. Show at least one valid example and one rejected or failing example. Record any unresolved rule that the implementation would otherwise have to decide implicitly.
- For a circuit or artifact change, describe which programs it applies to, how callers or tools observe the change, and how readers handle artifacts produced under earlier supported formats.
- For a documentation change, update the page that owns the behavior. Keep illustrative examples and verified test fixtures distinct. When a change establishes new semantics, keep the language documentation, examples, and applicable tests in agreement.

## Locate the change in the pipeline

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

| Stage | What to review |
| --- | --- |
| Lexer | Token boundaries, literal spelling, comments, source spans, and diagnostics for invalid characters or malformed tokens. |
| Parser | Grammar, precedence, associativity, and the location and quality of syntax errors. |
| NeuraIR | Name and type resolution, explicit control flow and effects, well-formed typed operations, and the source context needed by later diagnostics. |
| IR optimization | Preconditions for each rewrite, the resulting IR invariants, and preservation of observable behavior, including failures and effects. |
| IRtoNeuronConversion | Exact lowering of supported operations, circuit state and connections, and a diagnostic for unsupported operations or target constraints. |
| Output | Artifact validation, compatibility, stable serialization under the same build conditions, and execution from the serialized artifact. |

Place checks at the first stage with enough information to diagnose the issue. A parser can reject malformed syntax. Type checking and lowering must enforce semantic rules that require resolved names or types. Keep malformed input distinguishable from valid source that a target cannot lower.

When changing multiple stages, explain which invariant crosses each boundary and where it is checked. Keep backend layout choices out of the grammar and preserve enough source information for diagnostics after lowering.

## Verify behavior

Use the repository's applicable checks and report the exact commands and results in the pull request. Identify any behavior you assessed by review alone. For a fix, prefer a small fixture that isolates the failure and a check through the normal entry point when the issue crosses stages.

- **Front end.** Exercise accepted and rejected inputs. Check diagnostic spans and actionable error information. Match full diagnostic text only where the text itself is part of the contract.
- **IR and optimization.** Validate the resulting IR and compare original and rewritten behavior for the same inputs and initial state. Cover reachable branches, numeric boundaries, and effect or failure ordering where relevant. For rewrites with narrow preconditions, include cases that must bypass the rewrite.
- **Circuit execution.** Run the serialized circuit with known inputs. Compare results, relevant state changes, external events, and defined failures with expected source behavior. Use an independent reference evaluator or hand-derived expectation where feasible. For recurrent circuits, cover multiple steps, termination or bounded execution, and state reset. When testing object semantics, cover independent instances and dispatch through a base-typed reference if supported by the tested subset.
- **Standard library.** Put tests for a module in `tests/stdlib/<area>/` (layout in `tests/stdlib/area.cmake`): a normal result for each public member, each documented `Err` or `None`, and a trap program with its expected diagnostic for each documented trap. Collections need seeded model-based tests and their `validate()` checks; numeric code needs independent reference vectors, with the generator checked in. Host-backed code needs a test with the capability denied and a recorded trace replayed with `neura run --replay`. Run `ctest -R 'stdlib-|backend-reference'`, which also runs each library test under the reference evaluator.
- **Determinism and reproducibility.** Hold inputs, initial state, runtime limits, and external input traces fixed when comparing executions. Separately check serialized bytes when reproducible output is part of the change. Include the target and toolchain configuration in any reproducibility claim.
- **Decompilation.** Check recovered behavior by executing the recovered representation or by recompiling and comparing it with the original artifact within the supported domain. Include a circuit without optional debug information if the change claims to recover behavior from weights and topology alone. Report unsupported structures and distinguish evidence from sampled tests, exhaustive checks over a finite domain, and formal proofs. Source spelling and names are only meaningful comparison targets when the artifact explicitly preserves them.

Exercise failure paths that the change can affect. Examples include malformed artifacts, arithmetic boundaries, invalid memory access, exhausted execution limits, and unsupported circuit structures. Use the documented behavior of the affected type or feature to decide the expected result. Keep language-level tests independent of test order and follow the conventions in [Language testing](docs/lang/Testing.md).

## Review performance changes

Specify the workload, artifact configuration, execution environment, and baseline. For the standard library, `tests/benchmarks` records NVM steps, peak heap cells, neuron count, and image bytes for representative workloads in `tests/benchmarks/baseline.txt`; the `benchmarks` test fails when a figure rises more than 10%, and a change that raises one updates the baseline with the reason. `neura run --profile` shows the steps each function takes. Compare the same inputs and confirm equivalent results before measuring. Record the cost the change targets, such as compile time, circuit size or depth, serialized size, execution steps, runtime, or memory. Include the cost of any work moved to another stage or to the runtime. Explain which workloads benefit and which regress in the measurements.

## Submit a pull request

- Keep the patch focused and describe the affected stage and contract. Link the relevant design decision when behavior changes.
- Include a reproducer or example, test evidence, and any compatibility or migration details a reviewer needs.
- For changes to circuit encoding or runtime behavior, state the input domain, execution limits, and assumptions behind the verification. Include a small artifact or reproducible way to generate it when reviewers need one.
- For dependency, build, or runtime changes, describe how they affect repeatability and what a reviewer needs to reproduce the results.
- Follow the [Apache License 2.0](LICENSE) and preserve applicable notices when incorporating third-party material. Identify its origin and license in the pull request.
