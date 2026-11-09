# Feature status and implementation decisions

There is no compiler, runtime, package manager, test runner, or verified executable example in this repository. All language behavior is documented design, not implemented behavior.

| Area | Current design | Decisions needed before implementation |
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

Existing examples may predate proposed definite-initialization rules; see [Object lifecycle](ObjectLifecycle.md). Resolve inconsistencies before treating examples as conformance tests. A compiler plan should choose an initial feature subset and document each deferred decision rather than assuming all proposals are complete.
