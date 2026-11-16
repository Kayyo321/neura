# Neura 0.3.0: semantic facts and representations

This note records the design of the 0.3.0 milestone that gives NeuraIR the facts later stages need: finite aggregate shapes, effect and retention summaries, and storage and owner facts. It also records the order in which those land. It is a design note in the sense of [the 0.3.0 baseline](Release030.md): the language pages, [NeuraIR format](../NirFormat.md) and [IR optimization](../IrOptimization.md) stay authoritative, and a row becomes a rule only when the page that owns it says so. The owner signed off every row marked *frozen* on 2026-11-16.

The milestone changes no executable image. Its acceptance evidence is that every image built from the repository's programs is byte for byte the image rc.3 builds, except where a row below says otherwise and explains the difference in steps and heap cells.

## What exists at the baseline

| Area | Baseline |
| --- | --- |
| Value types | NeuraIR already types tuples (`TypeKind::Tuple`) and payload enums as values. The converter gives each of them an NVR `Object` layout, so every tuple and payload-enum value is a heap allocation at run time. |
| Facts on declarations | NeuraIR 1.10 symbol attributes: a registry of typed facts with stable identifiers. A new identifier needs no format version, and the linker already renumbers attribute payloads. |
| Effects | Two analyses. `PurityAnalysis` (`src/neura-ir/purity.cpp`) computes a fixed-point summary per function, including what each parameter's object graph and the result may reach, for `#Pure` and the optimizer. `EffectAnalysis` (`src/ir-optimization/effects.cpp`) summarizes reads, writes, allocation, traps, calls, host requests and scheduler use for the optimizer's rewrites. |
| Prebuilt modules | A `.nir` artifact carries the bodies of everything it exports and links. The standard library ships as source, not as `.nir`. |

## Order of work

Each step is one pull request. A step that changes the standard library or an image says so in its own description, with the steps and heap cells it changes.

| Step | Content | Changes images | Status |
| --- | --- | --- | --- |
| 1 | This note | no | frozen |
| 2 | Containment analysis after specialization, with the cycle diagnostic tested on IR and not yet enforced | no | frozen |
| 3 | `ErrorLink` and `PortableErrorLink` replace the recursive `cause` payloads of `Error` and `PortableError` | yes, for programs that build error contexts | frozen |
| 4 | Effect and retention summaries, computed on the linked program | no | frozen |
| 5 | Storage, owner and transfer facts, the verifier rules that check them, and their NeuraIR encoding | no | frozen |

Inline layouts, frame and static regions, and the NVR revision belong to the storage milestone that follows and are not part of these steps.

## Finite aggregates

| Rule | Status |
| --- | --- |
| After specialization, the compiler builds the containment graph of value types. A tuple contains each of its element types. A payload enum contains each payload type of each variant. A class, list, array, string, function value or type parameter contains nothing: it is a reference, or its storage is separate. | frozen |
| A cycle in the graph is an error, `E0448`, at the declaration that closes it. The message lists the path, for example `Tree -> Tree.Branch.left: Tree`, and suggests an explicit owner class for the link. | frozen |
| The cycle error is enforced in the same change that gives tuples and payload enums inline layouts, not earlier. Until then the analysis runs in tests only, because today's heap layout executes a recursive enum correctly. | frozen |
| An inline layout holds at most 64 value words. A larger shape is an error that names the shape and its size; it is never moved to the heap silently. | frozen |
| The analysis runs on the specialized program, so `Optional<Tree>` and a generic enum instantiated with itself are checked per instance. | frozen |

Six enums in the repository close a cycle today:

- three in the standard library: `Error`, `PortableError`, and `Node` in `std/text/internal/regex-parser.nra`;
- two `Tree` enums in `tests/backend/programs`;
- the `Tree` example of [Types](../lang/Types.md).

`JsonValue` recurses only through lists, which are references, so it closes none. Step 3 handles the two error types. The others migrate in the change that enforces the rule.

## Error chains

| Rule | Status |
| --- | --- |
| `Std.Core.Error.ErrorLink` is a class marked `#Final` with one `last` field, `cause: Error`. `Error.Context` becomes `Context { message: string, cause: ErrorLink }`. | frozen |
| `PortableErrorLink` is the same, also marked `#Immutable`, so that `PortableError` stays transferable. `PortableError.Context` holds a `PortableErrorLink`. | frozen |
| The helpers that build a context construct the link with `new`. `message`, `to_portable`, `from_portable`, equality and the close-error helpers in `std/io/closeable.nra` keep their results, their categories and the order of causes. | frozen |
| Code that matches `Context { message, cause }` and uses `cause` as an `Error` reads `cause.cause` instead. The `Changed` entry of the changelog and the migration guide say so. | frozen |
| The change lands in this milestone, before inline layouts, and it costs one extra allocation per context while the enum itself is still a heap object. | frozen |

## Effect and retention summaries

A summary says what a call of a function may do. It over-approximates: a fact that is absent is guaranteed.

| Fact | Values | Status |
| --- | --- | --- |
| Allocation | allocation-free, may allocate, unknown | frozen |
| Traps | cannot trap, may trap | frozen |
| Host | no host request, host requests | frozen |
| Scheduler and synchronization | none, uses tasks, guards or channels | frozen |
| Class initialization | none, may run a class or static initializer | frozen |
| Hooks and accessors | reserved; defined by the class-hook milestone | frozen |
| Each parameter's retention | not retained, retained by the result, retained elsewhere, unknown | frozen |
| Result ownership | fresh, borrowed from parameter *n*, static, unknown | frozen |
| Transfer | the existing task-transfer facts, unchanged | frozen |

| Rule | Status |
| --- | --- |
| One analysis computes summaries: `PurityAnalysis`, extended. It already tracks what each parameter and the result may reach. `EffectAnalysis` then reads its call facts from it instead of computing them again. | frozen |
| Summaries are computed on the linked program. A program built from source and the same program built from its `.nir` therefore get the same summaries and the same image bytes. | frozen |
| A function with a body is never trusted for a recorded summary. Its summary is always computed from the body. | frozen |
| A function without a body (a host binding or a kernel) carries its summary as a contract. Kernels take it from their existing signature (`kernel_signature.hpp`). Host bindings take it from a new registered attribute, which the standard library declares with a tag on each binding. A host binding without one is *unknown* in every fact. | frozen |
| An unknown fact is never shown as allocation-free, non-retaining, pure or task-safe, in the compiler or in editor responses. | frozen |
| A summary is attributed to the source operation that causes it, including an implicit initializer, so that a diagnostic can point at the cause. | frozen |

## Storage, owner and transfer facts

| Rule | Status |
| --- | --- |
| Every local, parameter, field and global has a storage class: frame, static, inside a heap object, or a borrow of storage owned elsewhere. The class follows from its declaration and type. | frozen |
| A fresh array belongs to the lexical block, or loop iteration, of the binding that creates it. Its owner follows from the tree of the body and needs no extra record. | frozen |
| A function's result is either a transfer of a fresh owned value or a borrow with a stated origin, and its summary records which. The verifier checks this against the body instead of inferring it from `return`. | frozen |
| These facts are derived and checked; they are not stored. Only what cannot be derived from the body and the types is encoded: a contract on a bodyless declaration, and the operations that the explicit-construction syntax adds. | frozen |
| Escapes are checked once on lowering and again on the specialized, linked program, where concrete types are known. | frozen |
| Class-reference policy provenance is reserved for the class-hook milestone. This milestone allocates no encoding for it. | frozen |

## Amendments to the baseline

Two frozen rows of [the 0.3.0 baseline](Release030.md) did not fit this design. The owner signed off both amendments on 2026-11-16, and the baseline note now carries them.

| Row | Amendment | Status |
| --- | --- | --- |
| NeuraIR 1.11, "Owner, borrow, effect and policy facts; multiword typed values" | Effect and retention facts are derived, or are symbol attributes, which need no format version. Multiword values are already NeuraIR types. NeuraIR 1.11 therefore goes to the first change that adds an operation or a node field, expected to be explicit construction (`new` for lists and function values). | frozen |
| "Fixed when each feature starts": the grammar is fixed on its language page | The language pages are published as fact when they are merged. The grammar and event tables of a feature are therefore fixed in a design note when work starts, and moved to the language page in the change that implements them. | frozen |

## Acceptance

- Every image of the identity set, and of every example and test program, rebuilt from source into a fresh directory, compares byte for byte with the rc.3 image, except the programs that step 3 changes. Those list their changed steps and peak heap cells.
- Source to `.nir` to image gives the same bytes as source to image.
- The cycle diagnostic has accepted and rejected cases on specialized programs, including a mutual cycle and a cycle through a generic instance.
- Summaries have cases for each fact, recursion, virtual calls, and a host binding with and without a contract.
- The verifier and the `.nir` reader reject an attribute payload that breaks its schema, and a contract on a function that has a body.
