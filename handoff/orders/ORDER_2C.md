# Work order 2C — the heap behind one interface (no behaviour change)

Read `handoff/WORK_RULES.md` first; it binds you. Then, in your worktree: `git reset --hard ccr-e433a631-4hzo4n-m2` if that branch exists locally, else `git reset --hard ccr-e433a631-4hzo4n` (the lead's message names the commit to expect). Commit work in progress on your worktree branch before any run longer than ten minutes, and send long runs' output to files in the scratchpad as they go.

## Why

Stage: NVM runtime only (`src/nvm`, and the helpers of `src/jit` that touch heap entries). Read `docs/runtime/NVM.md`: "State operations", "Garbage collection", "Execution tiers", "Controls".

Today a heap entry is `Execution::State::HeapEntry` in `src/nvm/machine.cpp`: a kind, a type, `std::vector<std::uint64_t> cells`, `std::vector<std::uint8_t> initialized`, `std::string bytes`, a list version, an identity number, a mark bit. Code all over the runtime reads and writes those members directly: state operations, the kernels' argument views, host value conversion, parallel export and import, the collector, the tier 2 undo log, text building, the shadow oracle (`tier_shadow.inc`, if it is on your base), and the debugger's inspection (`debug_value`, `debug_children`).

The next order (2D, not yours) replaces the representation: one contiguous allocation per entry with a small header; object fields as 8-byte words with an initialized bitmap; array, list, text and bytes elements stored at their own width (1, 2, 4 or 8 bytes) with no per-element flags; lists with a separate growable buffer; a flat growable handle table; side mark bitmaps. That is only possible if nothing outside the heap knows how an entry is stored. This order makes that true and changes nothing else.

## Phase 1 — survey and interface (report, then stop)

Do not change any existing code in phase 1. Produce:

1. **The survey.** Every place outside the definition of `HeapEntry`/`HeapTable` that names a member of a heap entry or holds a pointer, reference, span or view into one, in `src/nvm/**` and `src/jit/**` (and anywhere else the compiler tells you). Group by what the code does: field read/write, element read/write, length, append/insert/remove/resize, text or bytes read, text or bytes build, bulk view for a kernel, copy of a whole entry, traversal of references (collector, export, reachability), identity and version, allocation, free, comparison/inspection (oracle, debugger, task dump). Give counts per group and file, and list the sites that hold a pointer or view across a call that can allocate, grow a list, or collect (these are the dangerous ones for 2D).
2. **The interface.** A new header (propose the name; `src/nvm/heap.hpp` unless you find a reason) that declares the heap as a class with the operations the survey needs, with a comment on each. Requirements:
   - One inline function turns a reference word into the object it names (or "not live"). No caller computes that any other way.
   - No operation returns a `std::vector`, `std::string`, or a span of 8-byte cells that a caller may keep as "the elements". Element access is by index with an explicit element type or width, or by a bulk operation the heap performs (copy out, copy in, fill, compare, hash, visit references), or by a **view object whose validity rule is stated in its type's comment** (valid until the next call that may allocate, grow, or collect) and which exposes element width, so that 2D can make it a typed in-place view. Say for every kernel argument kind which of these it uses.
   - Text and bytes: read as `std::string_view`-like views under the same validity rule; building goes through heap operations (reserve, append, set), never through a caller-held `std::string&`.
   - Lists: append, insert, remove, clear, resize and the version counter are heap operations.
   - Objects: field read, field write, initialized test, as separate inline operations (the JIT and tier 2 will later inline their fast paths).
   - The collector's needs: mark, visit the references of an entry (by layout or element type), sweep with a callback or iterator, without exposing storage.
   - The accounting the heap owns versus what stays in `State` (`live_cells_`, `peak_cells_`, `next_collection_`, `last_handle_`, the collection schedule): propose the split. The cell counts reported to programs (`peak_heap_cells`) must stay exactly as today: one cell per object field, collection element, and text or bytes byte, whatever the storage.
   - Everything hot is defined inline in the header. No virtual functions.
3. **A mapping table**: survey group → interface operation(s).
4. **Risks**: every place where the interface cannot express what the code does today without a behaviour or speed change, with file:line.

Commit the header (unused, compiling) and the survey as a Markdown file under `handoff/wo2c/` (not in the repository). Then stop and hand back the phase 1 report. The lead reviews the interface before any call site changes. **Do not start phase 2 until told.**

## Phase 2 — move every site (after the lead's go-ahead, which may change the interface)

- The heap class is implemented with today's representation behind the interface.
- Every site of the survey goes through the interface. After the change, no file other than the heap's own names `cells`, `initialized`, `bytes`, `version`, `identity`, `marked` of a heap entry, and `HeapEntry` is private to the heap. The lead checks this by making those members private and by grep.
- Nothing observable changes: see "Acceptance".
- No speed regression: CPU time (user + system, median of 5, `os.wait4`) of every `tests/bench-wall` program at quick and full size within 2% of the base commit's binary, measured interleaved. Report the table. A program that regresses more needs a fix or an explanation with a profile.
- The tier 2 undo log, the region interpreter and the JIT helpers keep their exact write order and trap order.
- If your base has `tier_shadow.inc`: its heap section says it reads the heap only through the functions of that section; make those functions use the interface, and keep every comparison it makes.
- The debugger (`debug_value`, `debug_children`) and coverage are another team's code in `machine.cpp`: change only the lines that touch heap entries, keep their output byte for byte (the `tooling-*` tests check it).

## Files

`src/nvm/**`, `src/jit/**` (helpers that touch heap entries only), tests under `tests/backend/**` for new unit tests of the heap class. Docs: `docs/runtime/NVM.md` only if a sentence describes the storage of an entry; then regenerate the wiki. Nothing under `src/lift`, `src/backend`, `src/circuit`, `src/tooling`, `src/cli`, `scripts`.

## Tests to add (phase 2)

Unit tests of the heap class through its interface only (a new suite file in `tests/backend/`, registered like its neighbours in `CMakeLists.txt` if a new CTest entry is needed; say so, the lead updates the `--minimum` counts): every operation on every entry kind, at boundary sizes (empty, one element, the growth boundaries of a list), views invalidated correctly in a debug check if you add one, reference traversal over an object with nil, live and freed references, the cell accounting of each kind. Each test must fail under a deliberate fault in the operation it covers; list the faults you tried.

## Acceptance (phase 2)

```
cmake --build build --parallel 2 2>&1 | tee build/build.log
grep -F "warning:" build/build.log | grep -v -F "/third_party/"        # only the known passes.cpp:166 line may appear
ctest --test-dir build --parallel 2 --output-on-failure                 # all; native-c-import may fail for lack of DNS
python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/t0 --run-option=--tier=0
python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/jit --jit on
NEURA_TIER_EAGER=1 python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/eager --jit on
NEURA_TIER_EAGER=1 NEURA_TIER_CHECK=1 python3 scripts/runtime-identity.py capture --neura build/neura --images build/identity/images --out build/identity/check1 --jit on
python3 scripts/runtime-identity.py compare build/identity/t0 build/identity/jit      # and eager, check1
python3 scripts/tier-boundary.py --neura build/neura --out build/tier-boundary --jit off,on
```

plus, against captures made with the base commit's binary from the same images: every capture file byte-identical (this is the check that `peak_heap_cells`, traps and traces did not move). If the oracle is on your base, also `NEURA_TIER_CHECK=2` captures, plain and eager.

Sanitizer build (configuration of the `sanitizers` job in `.github/workflows/tier2.yml`, `-DNEURA_STATIC_LAUNCHER=OFF`): `backend-(vm|host|conversion|generics|concurrency|tier|jit|tier-interpreter|cli|each)`, `concurrency-.*`, `kernels-.*`, `tooling-.*`, your new suite. Zero reports.

## Report

Phase 1: the four items above. Phase 2: as WORK_RULES.md says, plus the before/after CPU-time table, the list of deliberate faults, and every site where you were unsure the write order, trap order or cell accounting is unchanged (file:line).
