# Work order 2D — the heap's storage: one block per entry, elements at their own width

Read `handoff/WORK_RULES.md` first; it binds you. In your worktree: `git reset --hard ccr-e433a631-4hzo4n-m2` (the lead's message names the commit to expect; it contains `src/nvm/heap.hpp`). Commit work in progress on your worktree branch before any run longer than ten minutes, and write long runs' output to files under `handoff/wo2d/` as they go.

## Why, and how much it is worth

Stage: NVM runtime, the heap only (`src/nvm/heap.hpp`; `src/nvm/machine.cpp` only where this order says).

`Heap` (order 2C) hides how an entry is stored. Behind it an entry is still a pooled struct holding a `std::vector<std::uint64_t>` of cells, a `std::vector<std::uint8_t>` of flags and a `std::string`: up to three allocations per entry, 8 bytes per element whatever its type, and two pointer hops to reach a field.

Be clear about the size of the prize. An instruction profile of `binary-trees` at its quick size (callgrind) puts `malloc`+`free` at about 4.5%, `typed_entry`+`entry` at 4%, collection and marking at 3.5%, and the rest in the dispatcher between blocks, which other orders remove. So this order is worth a few percent of today's time on allocation-heavy programs, and more memory than time. It is done now for two reasons that matter later: native code will read and write fields and elements inline, which needs a **published, fixed layout**; and once steps stop paying for the dispatcher, heap costs are what is left.

A measurement taken with the 2C work found no stored element with bits above its declared width in 15,166,236 stores (identity corpus and three standard-library test areas), so narrow storage will hold in practice. Correctness must not depend on that.

## What to build

Only the private part of `Heap` and of `Heap::Builder` changes, plus the comments that describe storage. **The public interface of `heap.hpp` does not change**, except where this order names an addition. If you find you need another change to it, stop and report.

1. **One block per entry.** A fixed header followed by the entry's data, allocated as one block:
   - **Object**: header, then the initialized bits (one bit per field, in 64-bit words), then the fields as 8-byte words. The identity number lives in the header.
   - **Array**: header (with length), then the elements at the storage width, inline.
   - **Text and bytes**: header (with length), then the bytes, inline.
   - **List**: header (with length, capacity, version) and a separate, growable element buffer the header points to. Growth is geometric. `append` and `remove` never move the header.
   - A `Ref` stays what its comment says: valid while the entry is live, never moved. So an array that must widen (below) gets a separate buffer and its header points to it; the header records whether the data is inline.
2. **Elements at their own width.** An array or list is created at its declared width (`ElementWidth` 1, 2, 4 or 8, which the creating paths already pass). `element()` returns the stored bits zero-extended. `set_element`, `append`, `copy_in` and the creating functions test each value: if it has a bit above the storage width, **that collection is converted to 8-byte elements first** (once; it stays wide), so that every element still reads back exactly the 64 bits that were stored. `ElementView::width` and the view's data reflect the storage as it is at that moment. References and 64-bit scalars are always 8 bytes.
3. **A size-class allocator** for the blocks and list buffers: chunks carved into blocks of a small set of sizes with a free list per class; larger blocks go to `operator new`. Freed blocks are reused. Under AddressSanitizer (`__SANITIZE_ADDRESS__`, or `__has_feature(address_sanitizer)`) every block comes from and returns to `operator new`/`delete` directly, so that the sanitizer still sees every use after free and overflow of an entry. No memory is returned to the system while the heap lives unless a whole chunk is free; say what you chose and why.
4. **The handle table stays as it is** (two-level pages). So do the mark bit (in the header), the sweep order (handle order), `max_elements()` (its exact value), the cell formula, handle numbering, and identity numbering. Do not add side bitmaps, a flat table, or address-based references: those are separate decisions that need their own measurements.
5. **The layout is published.** A section of `heap.hpp` (comment plus `static constexpr` offsets and `static_assert`s on the header struct) states, for native code to rely on later: how a `Ref` is represented, the offset of the kind, type, flags, the object's initialized words and first field, the array's and text's length and inline data, the list's length and buffer pointer, and which header bit says "data is inline" and which field gives the storage width. Add public `static constexpr` accessors for these offsets (this is the one allowed interface addition); nothing uses them yet.
6. **`Heap::Builder`** may keep building in ordinary containers; `create(Builder&&)` copies into a block. `create_object`, `create_filled`, `create_collection`, `create_text` allocate the block directly without a temporary.
7. **Kernels** (`machine.cpp`, the argument loop of the kernel call): the in-place test becomes "the view's storage width equals the width the kernel parameter needs" for read-only views (`View*`, `Bytes`, `Text`); writable parameters (`Mut*`, `State`) keep copying out and back exactly as today (a kernel that fails must leave the list unchanged). An `int32` array is then read in place by a `ViewI32` kernel. A collection that was widened is packed with `copy_out` as today. This is the only change outside `heap.hpp` apart from comments; if a second one seems needed, stop and report.

## What must not change

Everything observable: outputs, traps (kind, reason, detail, function, block), `steps`, `function_steps`, `peak_heap_cells` (the cell formula counts one cell per element or byte, whatever the storage), host requests, traces, the order in which handles and identity numbers are given, which objects a collection frees. Byte-identical captures against the base binary are the test.

## Tests (extend `tests/backend/heap_tests.cpp` and `heap_executor_tests.cpp`)

- Every operation on every kind at every declared width, at the boundaries: empty; one element; lengths around each size class boundary and around the list growth steps; an object with 0, 1, 63, 64, 65 and 200 fields (initialized bits across words).
- Widening: for each narrow width, storing a value with one bit above the width through each writer (`set_element`, `append`, `copy_in`, each creating function, the builder) converts the collection and every element reads back exactly, including earlier ones; `0xFFFFFFFFFFFFFFFF` into an `int8`, `int16`, `int32` and `bool` collection; a `Ref` and the handle stay valid; `ElementView::width` becomes 8; a later narrow value stays readable; `same()` treats a narrow and a widened collection with equal elements as the same; the cell count does not change.
- Views: an `ElementView` over each width reads the right bytes in host order; a view taken before a widening store is not used after it (document, and test the fresh view).
- Reuse: allocate, free and collect in patterns that reuse blocks of each class; contents of a reused block never leak into a new entry (fields uninitialized, elements as created).
- Executor level: a kernel with a read-only narrow view runs in place (observable only through a test hook or a counter you add for tests) and gives the same result as before; a kernel with a writable view that fails leaves the list unchanged; the widening cases through real state operations (hand-built image, as the existing executor tests do).
- Each test must fail under a deliberate fault in what it covers. List the faults you tried.

## Acceptance

```
cmake --build build --parallel 2 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"        # only the known passes.cpp:166 line may appear
ctest --test-dir build --parallel 2 --output-on-failure                 # all; native-c-import may fail for lack of DNS
```

Identity, with captures of the **base binary** (build it once from the base commit in a second directory, or keep a copy of `build/neura` before you edit) and of yours from the same images: `--run-option=--tier=0`; `--jit on`; `NEURA_TIER_EAGER=1 --jit on`; `NEURA_TIER_EAGER=1 NEURA_TIER_CHECK=1`; `NEURA_TIER_CHECK=2`; `NEURA_TIER_EAGER=1 NEURA_TIER_CHECK=2`. Every capture of yours byte-identical (`diff -r`) to the base binary's, and `scripts/runtime-identity.py compare` of your tier 0 against each of your others identical. Also `--extended` once for tier 0 and default. `python3 scripts/tier-boundary.py --neura build/neura --out build/tier-boundary --jit off,on`: 0 differences.

Sanitizer build (configuration of the `sanitizers` job in `.github/workflows/tier2.yml`, `-DNEURA_STATIC_LAUNCHER=OFF`): `backend-(vm|heap|host|conversion|generics|concurrency|tier|jit|tier-interpreter|cli|each|fleet)`, `concurrency-.*`, `kernels-.*`, `tooling-.*`, `stdlib-(hash-map|json|big-integer|array-list|strings|sorting)`. Zero reports. Then, once, the same suites with the size-class allocator forced on under the sanitizers (a build option you add for this, off by default), to exercise the allocator's own code.

Speed and memory, against the base binary, interleaved (the machine is shared; timing noise here is ±15%, so the bound is on instruction counts):
- `valgrind --tool=callgrind` instruction counts at quick size for every `tests/bench-wall` program: none more than 0.5% above the base; `binary-trees`, `object-churn`, `hash-map`, `string-building`, `text-split` and `sort` each below the base, and say by how much and where the instructions went (callgrind_annotate, top functions before and after).
- Peak resident memory (`ru_maxrss` from `os.wait4`) at full size for every program: report the table. It must not rise by more than 2% on any program and should fall where collections are narrow.
- CPU time medians of 7 at full size, for the record.

## Files

`src/nvm/heap.hpp`; `src/nvm/machine.cpp` (kernel argument loop, comments); `tests/backend/heap_tests.cpp`, `tests/backend/heap_executor_tests.cpp`; `CMakeLists.txt` only for the build option of the forced allocator; `docs/runtime/NVM.md` ("Heap and objects": one paragraph on how entries are stored and that storage is not observable; then `python3 scripts/generate-wiki.py` and `python3 scripts/check-docs.py`). Nothing else.

## Report

As WORK_RULES.md says, plus: the layout as you defined it (a table of offsets per kind); the size classes and why; the instruction-count and memory tables; the list of deliberate faults; every place where you were unsure that a `Ref`, a view, or the cell accounting behaves exactly as before (file:line).
