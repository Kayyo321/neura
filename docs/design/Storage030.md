# Neura 0.3.0: storage and construction

This note fixes the design of the 0.3.0 storage milestone: explicit construction with `new`, tuples and payload enums stored inline, arrays owned by the block that creates them, primitive references as checked borrows, and closures whose environment does not escape. It builds on [semantic facts and representations](Representation030.md) and is a design note in the sense of [the 0.3.0 baseline](Release030.md). Rows move to the language pages, [NVR](../runtime/NVR.md) and [NVM](../runtime/NVM.md) in the change that implements them.

The storage design is approved. Steps 2–10 are implemented on the storage milestone branch. Step 11 records performance acceptance. When a rule changes, record the reason here and update the corresponding language or artifact contract.

## Principles

| Rule | Status |
| --- | --- |
| "Stack" storage means storage owned by an activation and released when the block, loop iteration or function that owns it ends. The runtime may take that storage from the host allocator; it is not counted as heap. | proposed |
| Nothing is moved to the heap to make a program legal. A value that cannot be stored where the program puts it is an error with a fix the author applies: an explicit `new`, an owner class, or a smaller shape. | proposed |
| The milestone changes executable images, `peak_heap_cells` and step counts. Output, exit status, traps and their messages stay those of rc.3 for every program that compiles under both. | proposed |

## Construction

| Rule | Status |
| --- | --- |
| A list is constructed with `new`: `new([1, 2, 3])` where the list type is known from context, `new int[list]([1, 2, 3])` with the type written, `new` alone for an empty list of a known type, and `new(0 .. count)` or `new int[list](0 .. count)` from a range. The elements, or the range's bounds, are evaluated once, in written order, straight into the list; no array is built and copied. | proposed |
| A bare literal or range where a list is expected is an error, `E0450`, with a fix that inserts `new` and keeps the literal, its comments and its order. A bare literal where an array is expected stays an array. A nested list needs its own `new`. | proposed |
| A function value that captures nothing needs no `new`. One that captures and is stored, returned, passed to a parameter that retains it, or captured by another escaping function value is written `new func(...) ...`. A bound method that escapes is written `new bound(receiver.method)`. Whether a capture escapes follows from the retention summaries of M2. | implemented (step 10) |
| An ordinary call that allocates inside, such as a factory or a list that grows, needs no `new`; its summary says it allocates. | proposed |

The record form `new with(original) { ... }` belongs to the records milestone.

## Inline tuples and payload enums

A tuple or payload-enum value occupies a fixed sequence of *value words* wherever it is stored: a frame slot, a parameter, a result, a field, a global, or an element of an aggregate collection. Each word has one NVR type, so the collector and every tier see only words whose type they already know.

| Rule | Status |
| --- | --- |
| A tuple's words are its elements' words, in order. A nested tuple or enum is flattened in place. | proposed |
| A payload enum's words are a `uint32` tag, then its payload words. The payload words are pooled by NVR type: for each type, the enum has as many words of that type as the variant that needs the most of them, and each variant uses the first words of each type in payload order. A word a variant does not use holds zero or nil and is initialized. A fieldless enum stays one `uint32` word. | proposed |
| The containment check of M2 is enforced here: `E0448` is reported at the declaration that closes a cycle, and the five enums that close one today are migrated in the same change. | proposed |
| A shape of more than 64 value words is an error, `E0449`, that names the shape and its size. | proposed |
| A value is copied word by word. Copying never calls a class hook or accessor, and a reference word keeps the identity of the object it names. | proposed |
| A function's result may be several words (NVR 1.7). A call writes them to consecutive caller slots. | proposed |
| An array or list whose element is a tuple or payload enum stores each element's words inline, one after the other (NVR 1.7 aggregate elements). Reading or writing a component of an element is one operation; `length` counts elements, not words. | proposed |
| Where the runtime carries one word today, an aggregate is boxed into a runtime-owned transport object at the boundary and unboxed on the other side: a channel message, a task's result, a scheduler outcome, a host call's arguments and results, a kernel's arguments, a parallel item's argument and result, a callback, and an object constant. The box is a heap allocation charged as today. | proposed |
| Value equality, hashing and text of an aggregate read its words; their results are those of rc.3. | proposed |

Step 5 implements the tuple rows ([Backend](../Backend.md#tuples-and-enums-are-words)); payload enums stay heap objects until step 7, and a tuple payload of one is several consecutive fields of its object, not yet pooled words. Three rows differ from what the table expects, with the reason. A kernel's arguments are never tuples (the kernel table admits scalars, text and byte lists), so the kernel boundary is its result, the outcome tuple the runtime allocates. No source construct produces an object constant of a tuple type, so that boundary does not arise. And two boundaries the table does not list exist: the pair `string.from_utf8` returns, which the runtime allocates, and the array of results `parallel.finish` builds from boxed tuples, which a compiler-generated function copies into aggregate elements.

Step 7 implements the enum rows ([Backend](../Backend.md#tuples-and-enums-are-words)), and the four enums that closed a cycle then (the regular expression parser's `Node`, the `Tree` of `tests/backend/programs/aggregates.nra`, and the `Tree` examples of [Types](../lang/Types.md#enums) and of the agent reference `prompts/neura/references/syntax.md`) hold their links in classes. Four details the table leaves open are decided there, with the reason:

- A text word that the variant does not use holds the empty text, not nil: a reference read from graph lanes must name a live entry, and only an object reference may be nil there.
- An array or list word holds the nil collection, a constant that NVR 1.7 gains for this ([NVR](../runtime/NVR.md#types-constants-and-storage)). Allocating an empty collection instead would allocate for every value of such an enum, and the format had no other value that is no allocation.
- Words are pooled by NVR type, except that an array or list of inline values of two or more words is pooled by its element type: that element's aggregate can hold a collection of itself, as `Node` holds `Node[list]`, and two such collections of different element types rarely share a word.
- A boxed enum has one field for each of its words, where a boxed tuple has one for each element.

Every program image is NVR 1.7 from step 7 on: `main` returns `Std.Program.ExitCode`, a payload enum of two words because of `Status(int)`.

`Optional<T>` and `Result<T>` built over a payload that needs no allocation then need none either. `Result<T>` over `Error` takes at most 1 + 7 words more than `T` alone, by the pooling rule.

## Regions

Arrays, primitive-reference storage and non-escaping closure environments that belong to an activation are *region entries*: entries in the runtime's existing entry format, so every tier reads and writes them as it does a heap entry, but owned by a region instead of the collector.

| Rule | Status |
| --- | --- |
| A region belongs to a block, a loop iteration, or a function. It is released on every exit from its owner: the end of the block, `break`, `continue`, `return`, a trap, cancellation, and task teardown. A loop releases each iteration's region before the next. | proposed |
| A fresh array belongs to the region of the block that creates it. Returning a fresh array moves it, and every entry it owns, to the caller's region. A function that returns an array it did not create, borrowed from a parameter, global or field, is an error. | implemented (step 9) |
| Assigning an array created in an inner block to a binding of an outer block is an error. An inner binding may borrow an outer array. | implemented (step 9) |
| An array may not be stored in a class field, a list element, a channel message, an escaping closure, or a task argument. A tuple or enum that contains an array has the array's lifetime. | implemented (step 9) |
| A primitive reference to a local or parameter is a borrow of a region box that lives as long as the binding's block. It may not be returned, stored in a field, or captured by an escaping closure. Returning `&T` is allowed only for global storage or a field of an owner the result summary names. `reference_storage.cpp` no longer promotes storage to the heap. | implemented (step 10) |
| A nonescaping closure environment uses the lexical region of its construction. `new func` and `new bound` allocate on the heap. | implemented (step 10) |
| Escapes are checked on lowering and again on the linked, specialized program, with the storage and transfer facts of M2. | implemented (step 10) |
| Region cells are counted as stack cells, not heap cells. A run reports `peak stack cells` beside `peak heap cells`, and `--max-stack-cells` limits them (default 2^24). Exceeding it is the trap `stack limit exceeded`. | proposed |
| A handle is never reused, so an access to a released entry finds no entry and traps, in every tier, with `released storage accessed`. The checks above make that unreachable in a verified program. The trap is the runtime's backstop for a malformed image. | proposed |
| The collector does not sweep a region entry. It traces the references a live region entry holds. | proposed |

## Images and runtime

| Rule | Status |
| --- | --- |
| The changes are NVR 1.7: multi-word results, aggregate element types and their component operations, region allocation and release, and the move of an array to the caller's region. The writer emits 1.7 only for an image that uses one of them, so an image that uses none keeps its bytes. | proposed |
| An image of NVR 1.6 or earlier runs as it does in rc.3, with the accounting of rc.3. | proposed |
| Tiers 2 and 3 run every new operation: multi-word calls and returns and component accesses inline or as helpers, aggregate allocation and region operations as helpers. A function that uses a region operation is not inlined, and only a function that enters no region pops its frame in place. | proposed (steps 6 and 8) |
| The decompiler and assembler read and write every new form. A region entry and a flattened aggregate are shown with their source names when the image carries names. | proposed |

## Order of work

Amended on 2026-11-16: declining every new operation in tiers 2 and 3 would hold hot paths at tier 1, so the tiers learn the multi-word and aggregate operations before payload enums are inlined, and the region operations before arrays move into regions. Under the milestone process of 2026-11-17 each step is a commit on the milestone's integration branch, and the milestone is one pull request.

| Step | Content | Changes images |
| --- | --- | --- |
| 1 | This note | no |
| 2 | Construction with `new`, `E0450`, and the migration of the repository's sources | no |
| 3 | M2 step 5: storage, owner and transfer facts and the verifier rules that check them | no |
| 4 | NVR 1.7 and the runtime: multi-word results, aggregate elements, region entries and release, stack accounting; assembler tests only | no |
| 5 | Inline tuples in the converter; `E0449` | yes |
| 6 | Tiers 2 and 3 run multi-word calls and returns and aggregate operations | no |
| 7 | Inline payload enums; `E0448` enforced; the recursive enums migrated | yes |
| 8 | Tiers 2 and 3 run the region operations; native executables carry the stack limit | no (native containers: yes) |
| 9 | Arrays in regions, with the escape checks | yes |
| 10 | Primitive references as checked borrows, and closures in regions | yes |
| 11 | The performance gate | no |

Step 9 implements array placement, transfer, lexical release and E0451 ([Arrays and lists](../lang/Collections.md#array-lifetime)). The converter combines region release and task-scope joins in its existing exit path. At the lifetime milestone, the region token and release move to that milestone's existing exit-stack entry. A task-scope block joins its children before releasing its region. Startup and static arrays live for the execution, and pooled inactive enum words carry no ownership.

Step 10 implements a region box for each addressed local or value parameter. A primitive borrow of an instance field keeps the containing object reachable through an owner field in the borrow box. Conversion emits an owner liveness check before a read or write through that borrow. Unowned closure environments use the lexical region. Noncapturing callable descriptors are fieldless NVR constants in static storage. Explicit `new func` and `new bound` environments use the heap. The compiler keeps the written form in the existing NeuraIR operation text and derives escape and placement facts from the linked bodies.

## Gate

From step 5 on, an image no longer matches rc.3 byte for byte. A step is accepted when:

- every program of the identity set, the extended set and the fuzzer gives the output, exit status and trap of rc.3. Two parts of a trap's text depend on layout as counts do and are listed rather than compared: the handle number in `invalid reference handle: handle N`, which counts allocations, and the source position of a `--max-steps` trap, which depends on which operations a block runs. A run stopped by `--max-steps` is a count too, and so is how far it got: from step 7 on, every console write takes fewer steps because its outcome enum is no longer allocated, and a step-limit sweep's run prints more lines before the same limit (step 7: 362 of the 15,136 extended captures, every one a step-limit sweep, amended 2026-11-17);
- on the new binary, every execution mode gives exactly what tier 0 gives, including steps, `peak_heap_cells` and peak stack cells;
- the change in steps and peak cells is listed for each program it changes, with the reason;
- the performance gate of the plan holds: no program and not the geometric mean more than 5% slower than rc.3.

The storage milestone's tier-0 comparison found 488 shared baseline files with zero differences. The extended comparison covered 14,896 shared files and found 374 resource-limit differences. For the 371 step-limit differences, each pair returned the same status and hit the same numeric step bound. The trap moved to a different operation, or a run emitted a longer prefix of the identical completed output. Three frame-limit diagnostics named different calls in the console stack while preserving the trap kind, status and output. Tier 0 and every compared candidate mode agreed on these cases and their counters. The numeric boundary suite agreed on all 136,816 cases.

The combined storage fixtures cover arrays of explicitly allocated class instances, a list of aliases, an inline tuple inside `Result`, and ragged array ownership transfer over repeated loop exits. Each optimized and unoptimized build produced identical image bytes from source and serialized NeuraIR, with an exact assembly round trip. Mapped and stripped images matched in 11 execution modes. The compiled object-aggregate case contains two explicit object allocations, one array region allocation and no tuple or result box. The ragged-array case completes with a peak of 51 stack cells and traps when the limit is 50. Returning a borrowed array or borrowed row, and moving an inner row into an outer scope, each reports `E0451` before image emission.

## Acceptance

- Programs that use only scalars and arrays allocate nothing on the heap, measured apart from startup, arguments and host results.
- Dynamic, empty and ragged arrays; owned returns; each rejected escape; loops that create an array per iteration without growth; recursion to the stack limit; regions that grow while aliases are live; partial initialization; references held by region entries traced by the collector; a destroyed owner; a suspended task holding regions; a malformed image that accesses a released entry; and a run without optional metadata.
