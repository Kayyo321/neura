# Neura 0.3.0: storage and construction

This note fixes the design of the 0.3.0 storage milestone: explicit construction with `new`, tuples and payload enums stored inline, arrays owned by the block that creates them, primitive references as checked borrows, and closures whose environment does not escape. It builds on [semantic facts and representations](Representation030.md) and is a design note in the sense of [the 0.3.0 baseline](Release030.md). Rows move to the language pages, [NVR](../runtime/NVR.md) and [NVM](../runtime/NVM.md) in the change that implements them.

Status: *proposed* on 2026-11-16. The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written and await the owner's confirmation. Later changes to a row are made here, with the reason.

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
| A function value that captures nothing needs no `new`. One that captures and is stored, returned, passed to a parameter that retains it, or captured by another escaping function value is written `new func(...) ...`. A bound method that escapes is written `new bound(receiver.method)`. Whether a capture escapes follows from the retention summaries of M2. | proposed |
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

`Optional<T>` and `Result<T>` built over a payload that needs no allocation then need none either. `Result<T>` over `Error` takes at most 1 + 7 words more than `T` alone, by the pooling rule.

## Regions

Arrays, primitive-reference storage and non-escaping closure environments that belong to an activation are *region entries*: entries in the runtime's existing entry format, so every tier reads and writes them as it does a heap entry, but owned by a region instead of the collector.

| Rule | Status |
| --- | --- |
| A region belongs to a block, a loop iteration, or a function. It is released on every exit from its owner: the end of the block, `break`, `continue`, `return`, a trap, cancellation, and task teardown. A loop releases each iteration's region before the next. | proposed |
| A fresh array belongs to the region of the block that creates it. Returning a fresh array moves it, and every entry it owns, to the caller's region. A function that returns an array it did not create, borrowed from a parameter, global or field, is an error. | proposed |
| Assigning an array created in an inner block to a binding of an outer block is an error. An inner binding may borrow an outer array. | proposed |
| An array may not be stored in a class field, a list element, a channel message, an escaping closure, or a task argument. A tuple or enum that contains an array has the array's lifetime. | proposed |
| A primitive reference to a local or parameter is a borrow of a region box that lives as long as the binding's block. It may not be returned, stored in a field, or captured by an escaping closure. Returning `&T` is allowed only for global storage or a field of an owner the result summary names. `reference_storage.cpp` no longer promotes storage to the heap. | proposed |
| A closure environment that does not escape is a region entry of the block that creates it; `new func` and `new bound` allocate on the heap. | proposed |
| Escapes are checked on lowering and again on the linked, specialized program, with the storage and transfer facts of M2. | proposed |
| Region cells are counted as stack cells, not heap cells. A run reports `peak stack cells` beside `peak heap cells`, and `--max-stack-cells` limits them (default 2^24). Exceeding it is the trap `stack limit exceeded`. | proposed |
| A handle is never reused, so an access to a released entry finds no entry and traps, in every tier, with `released storage accessed`. The checks above make that unreachable in a verified program. The trap is the runtime's backstop for a malformed image. | proposed |
| The collector does not sweep a region entry. It traces the references a live region entry holds. | proposed |

## Images and runtime

| Rule | Status |
| --- | --- |
| The changes are NVR 1.7: multi-word results, aggregate element types and their component operations, region allocation and release, and the move of an array to the caller's region. The writer emits 1.7 only for an image that uses one of them, so an image that uses none keeps its bytes. | proposed |
| An image of NVR 1.6 or earlier runs as it does in rc.3, with the accounting of rc.3. | proposed |
| Tier 2 and tier 3 first decline a block that uses a new operation; it runs at tier 1. Native lowering follows in its own change, and the performance gate decides when it is required. | proposed |
| The decompiler and assembler read and write every new form. A region entry and a flattened aggregate are shown with their source names when the image carries names. | proposed |

## Order of work

Each step is one pull request.

| Step | Content | Changes images |
| --- | --- | --- |
| 1 | This note | no |
| 2 | Construction with `new`, `E0450`, and the migration of the repository's sources | no |
| 3 | M2 step 5: storage, owner and transfer facts and the verifier rules that check them | no |
| 4 | NVR 1.7 and the runtime: multi-word results, aggregate elements, region entries and release, stack accounting; assembler tests only | no |
| 5 | Inline tuples and payload enums in the converter; `E0448` enforced; `E0449`; the recursive enums migrated | yes |
| 6 | Arrays in regions, with the escape checks | yes |
| 7 | Primitive references as checked borrows, and closures in regions | yes |
| 8 | Native lowering of the new operations in tier 3, and the performance gate | yes |

## Gate

From step 5 on, an image no longer matches rc.3 byte for byte. A step is accepted when:

- every program of the identity set, the extended set and the fuzzer gives the output, exit status and trap of rc.3;
- on the new binary, every execution mode gives exactly what tier 0 gives, including steps, `peak_heap_cells` and peak stack cells;
- the change in steps and peak cells is listed for each program it changes, with the reason;
- the performance gate of the plan holds: no program and not the geometric mean more than 5% slower than rc.3.

## Acceptance

- Programs that use only scalars and arrays allocate nothing on the heap, measured apart from startup, arguments and host results.
- Dynamic, empty and ragged arrays; owned returns; each rejected escape; loops that create an array per iteration without growth; recursion to the stack limit; regions that grow while aliases are live; partial initialization; references held by region entries traced by the collector; a destroyed owner; a suspended task holding regions; a malformed image that accesses a released entry; and a run without optional metadata.
