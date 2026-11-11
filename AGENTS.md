# Repository instructions

## Start with the relevant contract

- Read [README.md](README.md) for project scope and [docs/Project.md](docs/Project.md) for the compiler, circuit runtime, and decompiler contracts.
- Read the relevant page in [the language design](docs/lang/Overview.md) before changing syntax or behavior. Use [language status](docs/lang/Status.md) to identify unresolved decisions. Treat `.nra` files under [examples](examples/) as illustrations unless a conformance test establishes the behavior.
- Follow [the Neura coding style](docs/style/CodingStyle.md) for `.nra` code, including examples and fixtures. Follow [CONTRIBUTING.md](CONTRIBUTING.md) for change and verification expectations.
- When updating wiki content, follow [wiki maintenance](docs/Wiki.md) to keep the repository sources and the separately hosted GitHub wiki in sync.
- When changing the website, follow [website maintenance](docs/Site.md) and keep to [its design](docs/SiteDesign.md). The site is the pages in [`site/`](site/) and the repository documents that [`site/contents.toml`](site/contents.toml) lists, which the build renders, so a change to a listed document changes the site. Merging into `main` publishes it. Correct a document in the document, not on the site. When a document links to another Markdown file, list that file in `contents.toml`. Preview the change and run `python3 scripts/build-site.py --check`.
- Preserve existing work. Keep edits within the requested feature and avoid unrelated formatting, renames, dependencies, or generated files.

## Keep compiler stages separate

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

| Stage | Agent rule |
| --- | --- |
| C import | Before parsing, turn an `import:c(...)` header into ordinary Neura source that calls `Std.DynamicForeign`. Add no language rules of its own, and name the reason when a program uses a C declaration it could not map. |
| Lexer | Preserve source locations while recognizing tokens. Report lexical failures with the offending span. |
| Parser | Build syntax from tokens. Keep grammar, precedence, and syntax recovery separate from typing and name resolution. |
| NeuraIR | Resolve and check language semantics during lowering. Make the types, control flow, state, and effects needed for later stages explicit. Diagnose unsupported constructs before they reach circuit conversion. |
| IR optimization | Preserve the behavior of valid NeuraIR, including evaluation order where observable, effects, and specified failures. Compare optimized and unoptimized behavior before claiming equivalence. |
| IRtoNeuronConversion | Lower supported operations to deterministic neuron primitives and explicit state interfaces. Keep source parsing and source-level policy out of the converter. |
| Output | Emit enough information for a compatible runtime to execute the circuit independently of the original source. Keep optional debugging and source mapping data separate from execution requirements. |

Make the smallest change at the stage that owns the behavior. Keep enough provenance through lowering and optimization to diagnose failures and inspect generated circuits. Validate the boundaries affected by the change, including the normal source-to-circuit path when its behavior changes.

## Preserve language and execution semantics

- Treat documented syntax and language semantics as the contract. When a necessary behavior is unresolved, document the decision with the implementation. Avoid silently choosing overflow, coercion, failure, or evaluation rules in a backend.
- Keep execution deterministic for the same artifact, inputs, initial state, runtime configuration, and declared external interactions. Make external state and capabilities explicit. Avoid relying on iteration order, scheduling, host integer widths, or floating-point behavior unless the contract specifies them.
- Preserve the specified value representation and numeric behavior. Exercise widths, signedness, boundary values, and failure cases when lowering arithmetic. If a target cannot preserve the contract, report that limitation at the appropriate boundary.
- Preserve independent instance state and shared method behavior when lowering classes. Keep receiver handling, lifecycle, and dispatch consistent with the language rules. Where those rules are open, record the dependency on a language decision.
- Keep circuit behavior in the emitted representation and its defined primitives. Any host services or runtime responsibilities must be explicit. Describe what stays in the circuit and what crosses the runtime boundary when adding a stateful operation.
- Keep decompilation grounded in the artifact. Use metadata when available and identify inferred structure as such. Compare recovered behavior against the artifact under stated inputs, state, and execution bounds. Describe exact recovery or formal equivalence only when the evidence supports that claim.

## Verify at the boundary that changed

- For grammar or typing, cover accepted input and a representative rejection. Check the diagnostic span and the reason for the error when they form part of the contract.
- For NeuraIR changes, inspect the resulting types, control flow, effects, and invariants relevant to the feature. Use the same language examples and rules across the frontend and the backend.
- For optimization, compare results, specified failures, and observable effects before and after the pass. Include cases where a rewrite must leave an operation in place.
- For circuit conversion or execution, check the emitted artifact with the runtime. Use repeated runs with identical starting state and include edge values and state transitions relevant to the change.
- For decompilation, distinguish tests that use optional provenance from tests that recover behavior from the circuit. State whether a result is an observed match, an exhaustive check over a stated domain, or a proof under stated assumptions.
- Use commands and checks available in the repository. Identify what ran and what could only be inspected. Keep design examples, proposed commands, and implemented guarantees distinct.

Update the relevant language page and example when changing a user-visible rule. Update the project specification when changing a stage contract, artifact boundary, or verification model. Keep the README focused on what Neura is and its repository-wide agent rules. Record implementation status in the dedicated status documentation.
