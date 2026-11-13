# Function programs: design (work order 2G, phase 1)

Base: commit b9ef537. Every file:line below is at that commit. "Reference
code" means the code tier 1 runs today: `commit_exit` (machine.cpp:3181) and
everything it calls. "The executor" is `Execution::State` (machine.cpp:323).
Where this document says *must*, the implementer has no choice; where a
choice was open, the decision is stated with its reason. The census that
motivates the design is in `census.md` next to this file.

Contents: 1 What exists · 2 Unit and entries · 3 Formation · 4 New operations
· 5 Checkpoints and deoptimization · 6 The interpreter · 7 What native code
needs · 8 Interactions · 9 Order of implementation · 10 Risks and open questions.

---

## 1. What exists

**Data.** `tier2::Region` (tier2.hpp:137) is a flat register program over
`tier2::Op` (tier2.hpp:111; codes tier2.hpp:64: Budget, Lift, Mask, Move,
Gather, Store, Branch, Switch, Jump, CheckRef, State, Poll, Complete, Leave,
Deopt). Register `r < slots.size()` caches frame slot `slots[r]`; the rest are
temporaries, constants (`constants`, loaded from the run's `const_values_` at
entry) and `immediates`. `written` are the slot registers written back;
`entry_reads` the slot registers read before any write on some path from pc 0;
`entry_canonical` the slot registers whose value must fit the type's width at
entry; `checkpoints` (tier2.hpp:131) hold, per checkpoint, the most blocks
and polls any path takes before the next checkpoint or the region's end.

**Formation.** `TierCache::region` (tier2.cpp:1267) is called by `region_for`
(tier2_steps.inc:54) once a block has had `region_after` = 1000 dispatcher
arrivals (`count_region`, tier1.hpp:216; 1 under `NEURA_TIER_EAGER`).
`form_region` (tier2.cpp:1153) takes the entry and the blocks reachable
through continue exits and `inline_state_operation` exits (tier2.cpp:1129)
whose tier-1 form is lifted, wired or empty (`BlockForm`, tier2.hpp:184), up
to 128, refusing an entry that is not admissible ("the entry is not lifted or
cannot continue in a region") or a region with no internal edge ("no exit
stays in the region"). A dispatch block (`Selection::Dispatch`) is not
admissible (tier2.cpp:1164). Pending blocks delay formation up to
`region_patience` × the threshold and cause up to four refreshes
(tier2.cpp:1319-1351). `Compiler::compile` (tier2.cpp:609) runs
`collect_slots` (648), `analyse_assignments` (674; a must-analysis rooted at
local block 0 that gives `written`, `entry_reads`), `find_checkpoints` (738;
DFS from block 0, back-edge targets, longest paths), `emit_block` (839) and
`emit_exit` (1024), then `Optimizer::run` (147): `fold_completes`,
`hoist_constants`, `entry_canonical` (all slot registers narrower than 64),
`analyse_widths` (270, rooted at pc 0), `drop_masks`, `eliminate_checks` (367,
facts rooted at pc 0), `drop_dead`, `write_directly`, `compact`.

**Running.** `advance` (machine.cpp:433) loops: `run_regions(max)` then one
`step` (machine.cpp:3157 → `tier_step`, tier1_steps.inc:64). `run_regions`
(tier2_steps.inc:107) enters regions while the current block has one, up to
`allowance` blocks (the fleet's rounds, machine.cpp:4446). `run_region`
(357) loads every slot register and flag from the frame, refuses entry
(`refused`) if an `entry_reads` register is uninitialized, an
`entry_canonical` register is too wide, or floats meet a non-default
environment; loads constants and immediates; runs `run_native` (200) or
`interpret_region` (236); writes back `written` with flags; sets
`frame.block`; adds `done` to `steps_`, under `--profile` to
`function_steps_[region.function]` and the task's steps, and to
`tier_steps_.region`. Inside, `done` counts completed blocks (Complete, or
the `completes` flag on Jump/Leave); nothing reads `steps_` except Budget,
which checks `steps_ + done` against `max_steps`, `allowance - done`, and
`scheduler_->polls + checkpoint.polls < task_quantum` (interpreter 250-262,
native compiler.cpp:282). Inline State calls the reference `state()`
(machine.cpp:3448) and deoptimizes on a `Fault`; Poll does what `poll()`
(1669) does with the quantum out of reach. **Deoptimization** sets
`next_block` to the op's block (a block commits only after every op that can
fail, so the slot registers hold the block-entry state), writes back, and
tier 1 runs that block. Counts are in `RegionCounts` (tier1.hpp:95); per-block
ends in `BlockPlan::region_deopts/leaves` (machine.cpp:2915) flushed by
`flush_region_events` (414).

**Checks.** `NEURA_TIER_CHECK=1` runs `checked_region` (440): the region runs
with `heap_log_` set (writes of WriteField/WriteElement/WriteGlobal are
logged in `state()`), then everything is undone (`undo_write`, 522), the same
number of blocks run at tier 1, and both are compared; "tier 1 left the
frame" if the depth changed. `NEURA_TIER_CHECK=2` (tier_shadow.inc) syncs a
tier-1 shadow after every `run_regions` that ran a block (`shadow_returned`,
tier_shadow.inc:234, called at 151) and at every event.

**Native code.** `TierCache::native` (tier2.cpp:1375) compiles a region once
(`jit::compile`); `Generator::emit` (compiler.cpp:173) translates op by op;
`jit::Context` (jit.hpp:70) carries registers, flags, `steps`, `max_steps`,
`allowance`, `polls`, `quantum`, `done` and helper pointers (lift,
check_reference, state, poll). Up to 7 (x86-64) / 13 (AArch64) registers are
pinned (`choose_pinned`, compiler.cpp:85) and flushed before helpers and at
the one exit; deoptimization stubs store `next_block` and an `Outcome`
(jit.hpp:112). `done` is a machine register stored at the epilogue.

---

## 2. Unit and entries

**Unit.** A *function program* is a `tier2` program (the `Region` structure
extended as below) that covers every block of one function. Blocks that tier
1 cannot run lifted, wired, or empty are *stubs*: their code is one `Deopt`
op (section 3).

**Decision 1: slot registers are the canonical frame.** In a function
program, slot register `r` *is* frame slot `r` of the running frame: the
interpreter and the native code read and write `frames_.back().slots[r]` and
`.initialized[r]` directly (`FrameStack::operator[]`, machine.cpp:771). There
is no load at entry and no write-back at exit. Temporaries, constants and
immediates live in a separate per-(run, program) register image (section 6).
Reason: a Call must leave the caller canonical and a Return must find it
canonical; with a copy model each Call writes back and each Return reloads
every slot register of the caller (census: binary-trees makes 45 million
calls at full size). It also makes every deoptimization map the identity, and
the collector sees slots without any write-back. It is exact because the
existing rule already makes every commit (Store, `defines` Lift, State
result) happen after every op that can fail in the block (tier2.hpp:36-44),
so at a deoptimization point the frame holds the block-entry state. The
`frame.block` field and the step count are *not* kept current per block;
they are written at every point where code can leave or where reference code
can observe them (sections 4, 5). Today's regions (functions without a
program) keep their copy model and `interpret_region`/`run_native`
unchanged in this order; every function program runs aliased, in the new
`interpret_program` (section 6), even under `NEURA_TIER_CHECK=1` where it
contains no Call or Return (`checked_region` snapshots and restores the
frame itself, so it checks either model). An exception other than a
`Fault` (for example `std::bad_alloc` inside a helper) leaves the frame as
far as the program had committed it and propagates as today; it ends the run
and is not one of the invariants' outcomes.

**Entries.** A function program has *dispatcher entries*, each a block at
which the dispatcher may start the program:

1. the function's entry block 0;
2. every *loop head*: a block that is the target of a back edge of a
   depth-first search over the function's blocks started from block 0 and
   then from every other entry (3. below) not yet visited, in ascending block
   order (deterministic), following the in-program edges defined under
   "Per-entry analyses" below (exits in exit order);
3. every *resume block*: the `resume` of every Call exit, the `resume` and
   `error_resume` of every Host exit, the `resume` of every Scheduler exit,
   and the `resume` of every Kernel state exit (the last because a kernel
   charges extra steps, section 4.6).

Entries are numbered in ascending block order: `Program::entries[i] =
{block, label, entry_reads, entry_canonical, checkpoint}`. Every block has
one label (its first op). Entry checks are not ops: the executor runs them
before it jumps to an entry's label (section 6). A Call's resume block is
therefore reached two ways at the same label: from the dispatcher, after
the checks, and from an in-program Return (section 4.2), without them; the
latter is the *return continuation* of the call.

**Per-entry analyses.** Each analysis that `Compiler`/`Optimizer` roots at pc
0 today becomes:

- `analyse_assignments` (tier2.cpp:674). Run once per dispatcher entry `e`
  as today's must-analysis with `in[e] = ∅` and every other block starting
  at "all assigned"; edges are every exit with a resume in the function: a
  Continue or State exit assigns its assignments and result slot (as
  `assigned`, tier2.cpp:680), whether the state op is inline or a Helper
  (4.3-4.6, kernels included: the program continues to the kernel's resume);
  a **Call** exit edge to its `resume` assigns the exit's assignments and the
  call's `result_slot` (the Return writes it, machine.cpp:3247-3250); a
  Dispatch block's call exits likewise. Host, Scheduler, Trap and MessageTrap
  exits have no in-program edge (they deoptimize, 4.7; their resumes are
  entries with their own analyses). These are the *in-program edges*; the
  loop-head search above follows the same edges. The early reads from `e`
  become `entries[e].entry_reads`. Representation: one bitset
  of `frame_types.size()` bits per block; cost O(entries × blocks × ⌈slots/64⌉
  × iterations). `written` is no longer needed (aliasing).
- `entry_canonical` (tier2.cpp:153): per dispatcher entry, every slot
  narrower than 64 lanes that is in that entry's *live-in* set (initialized
  at entry and read on some path, i.e. its `entry_reads` plus any slot the
  analysis below assumes narrow). Simplest exact rule, chosen: all slots of
  the function narrower than 64 lanes whose `initialized` flag is set at
  entry (uninitialized slots are never read before written, so their
  contents do not matter). The check runs at dispatcher entry only.
- `analyse_widths` (tier2.cpp:270) and `eliminate_checks` (tier2.cpp:367):
  one dataflow over the whole program with every dispatcher-entry label as a
  root whose start state is today's (`slot_widths_` for slots, constant and
  immediate widths; no facts). The **Call** op's successor is the resume
  block's return continuation; across that edge every caller slot keeps its
  width and facts (no code but the Return writes a caller frame while the
  callee runs: frames are private to their function activation, a host
  completion writes only a frame that left compiled code, and the collector
  does not move or change slot values), **except** the call's `result_slot`,
  whose width becomes the slot type's width (justified by the Return op's
  width check, section 4.2) and whose facts are erased. Temporaries do not
  cross the edge (they are block-local, section 6).
  At a block that is both a dispatcher entry and reached by in-program
  edges, the dataflow merges the root state with the incoming edges as it
  merges any join, which is sound for both ways in.
- `find_checkpoints` (tier2.cpp:738): roots are all dispatcher entries; every
  dispatcher entry (resume blocks included) and every loop head is a
  checkpoint (section 5).

---

## 3. Formation

**When.** Arrivals are counted per block as today (`count_region` in
`region_for`, tier2_steps.inc:77). A function's program is formed the first
time any of its dispatcher-entry blocks reaches `program_after` arrivals
(new `TierOptions::program_after`, default 1000 = `region_after`; 1 under
`NEURA_TIER_EAGER=1`). Arrivals are counts over every run sharing the cache;
nothing depends on time. A block that is not an entry of a formed program
never forms a region of its own any more; a function whose program is
rejected (below) falls back to today's per-block regions.

**Blocks.** For each block `b` of the function, in ascending order, the
formation asks the cache for its `BlockForm` as `TierCache::region` does
(tier2.cpp:1292-1314). If the block has a LIFT hint (`TierCache::hinted`,
tier1.cpp:212) and its entry is not settled and has never been attempted
(`Entry::next_attempt == 0` and `!attempted`), formation proves it now by
calling `TierCache::resolve(function, b, options.hint_after)`
(tier1.cpp:229), which gives it `minimum_proof_work` (tier1.cpp:296). At most
`program_proofs` (new option, default 32) such proofs are started per
formation, in ascending block order; the rest stay pending. Reason: a cold
block that is a stub makes every pass through it leave compiled code; a
proof costs well under a millisecond for most graphs and at most about 16 ms
of work (a binary64 multiply), so the bound is 32 × 16 ms in the worst case
and is counted in proofs, not time. A block whose proof ran out of work
(`exhausted`) keeps its `next_attempt` and stays pending.

**Stubs.** A block whose form is `None` (rejected, declined, pending, a
search-only block below `search_after`, or a block with a 16-bit float
operation) is a stub: `Budget` if it is a checkpoint, then `Deopt` to the
block. A program is formed only when its entry block 0 is not a stub and at
least one block is not a stub; otherwise "function has no program" with the
reason, and today's regions are used for the function.

**What the program contains.** Every non-stub block, emitted by
`emit_block`/`emit_exit` extended so that an exit is compiled as: Continue →
as today; inline state op → as today; **direct Call** → `Call` (4.1); a
**dispatch block** (`Selection::Dispatch`; every exit the table names is a
call, lowered_block.cpp:398-400) → `Dispatch` (4.1) then one `Call` per
call exit; **Return** → `Return` (4.2); non-inline State: AllocateObject,
AllocateCollection, RepeatCollection, AppendElement → `Helper` (4.3); other
non-inline State (FreeObject, RemoveElement, ConstructText, TextSlice,
TextFromUtf8, ClassHash, ClassText, ScalarText with one argument, Kernel) →
`Helper` (4.4-4.6); Host, Scheduler, Trap, MessageTrap → `Deopt` (4.7).
Under `NEURA_TIER_CHECK=1` every Call, Dispatch, Return and Helper is
compiled as `Deopt` (section 8).

**Bounds.** A function with more than 4096 blocks or more than 65535 frame
slots gets no program ("function too large"). Formation cost is linear in
the function's blocks and ops except the per-entry assignment analysis
(O(entries × blocks × slots/64 × iterations)) and at most `program_proofs`
proofs. It runs under the same `TierTimes` tier-1 scope as region formation
(tier2_steps.inc:80).

**Refresh and invalidation.** Programs are immutable once published. A
program formed while some hinted blocks were pending is formed again, as
regions are (tier2.cpp:1319-1332): at `refresh_at = arrivals × 4`, at most four
times; the replaced program moves to `retired_regions_` (it stays alive for
the cache's life, since frames may still return into it, 4.2). A run picks up
a newer program at its next dispatcher entry (`region_for`, Settled branch).
There is no other invalidation: nothing a run does can make a proven block
wrong.

---

## 4. New operations

Common rules for every op below.

- **R1 (block commit order).** Within a block, every op that can fail and
  deoptimize (Lift with a check, CheckRef, inline State, Switch, Dispatch,
  every precondition of the op) runs before the first effect of the block;
  a deoptimization therefore leaves the frame in its block-entry state and
  tier 1 re-runs the block. This is today's rule (tier2.hpp:36-44).
- **R2 (deoptimize).** Deopt at block B: `frames_.back().block = B`; flush
  `done` (R4) **without** counting B; leave with outcome "deoptimized"; the
  dispatcher runs B at tier 1 (`step`).
- **R3 (trap in place).** For an op that calls reference code which may
  `fault` after the block's effects began: (1) all R1 ops of the block have
  passed and the op's arguments have been read into the executor's argument
  buffer from the pre-transition registers (as `commit_exit` resolves them
  before assigning, machine.cpp:3188-3197); (2) the block's assignments are
  committed to the frame (Stores, in
  exit order, parallel as today); (3) `frames_.back().block = B`; (4) `done
  += 1` and flush `done` (R4) — the block's step is charged before the call,
  as `prepare_step` (machine.cpp:3087-3091) charges it before `commit_exit`;
  (5) call the reference function. If it throws `Fault`, the helper catches
  it and calls `trap(failure)` (machine.cpp:1142), which sets `finished_`;
  compiled code ends with outcome `Trapped`, and the block count it returns
  to `run_regions` **excludes** block B (so `advance_reference_rounds`,
  machine.cpp:4441-4453, adds B's round when `step` returns the finished
  event, and the fleet's rounds stay those of tier 1). Any other exception
  is kept and rethrown as today (`region_exception_`).
- **R4 (flush).** `steps_ += done`; under `--profile`
  `function_steps_[f] += done` with `f` the function of the frame that ran
  those blocks (resized as in `prepare_step`, machine.cpp:3094-3097) and, with
  a scheduler, `current_task().steps += done`; `tier_steps_.region += done`;
  `done = 0`. Flushes happen at every Call, Return, Helper, Leave, Deopt and
  Trapped end, so `done` always belongs to one function. A second counter,
  `activation_blocks`, counts every block completed since the dispatcher
  entered compiled code and is never reset by a flush: Budget checks
  `steps_ + done` against `max_steps` (as today; `steps_` now includes the
  flushed blocks) and `allowance - activation_blocks` against the
  checkpoint's steps, and `activation_blocks` (minus a trapped block, R3) is
  what `run_regions` receives.
- **R5 (frame.block).** `frames_.back().block` is written only at R2, R3, a
  Call (caller's block = the call block), a Return (caller's block =
  `return_block`, as machine.cpp:3251), and a Leave (the target). Nothing
  else observes it: inline `state()` and `lift::evaluate` do not read frames.
- **R6 (budget).** No op below reads or writes `steps_`, polls or the
  quantum except as stated; the Budget at the preceding checkpoint covers
  every block and poll up to and including the op's block (section 5).

### 4.1 Call (direct, through a dispatch block, closure)

`Call` op fields: `block` (the call block), `target` (callee function),
`index` (the exit), `other`/`width` (argument registers in `operands`),
`dst` = `result_slot` or absent, `a` = `resume`.

`Dispatch` op (for `Selection::Dispatch` blocks): `a` = the tag register
(the lowered block's input 0, masked to its input width as today's inputs
are), `index` = the block's dispatch table (copied into the program:
`LoweredBlock::dispatch` sorted pairs and `dispatch_default`, lowered_block.cpp:408-409),
operands = one label per exit. It computes the exit exactly as
`lift::execute` does (lowered_block.cpp:617-622: binary search; a missing tag
selects `dispatch_default`, the block's last exit). The default exit is
compiled as `Deopt` whatever its kind (the converter makes it the
`InvalidDispatch` trap; a Deopt is exact for any kind, since tier 1 re-runs
the block). The receiver argument of each call exit is a Lanes
value of reference type and gets today's `CheckRef` (tier2.cpp:1003-1018).
A closure call is a dispatch over the closure's class (`$closureN.$invoke`
targets in the census) and needs nothing else.

Preconditions (deoptimize at the call block when one fails, before any
effect): the R1 ops of the block (lifted checks, CheckRef of lane values).

Effects, in this order, each exactly as `commit_exit` does them
(machine.cpp:3181-3215):

1. Resolve the argument values (registers) into the executor's
   `argument_scratch_` (machine.cpp:3194-3197), against the pre-transition
   frame.
2. Commit the exit's assignments to the caller frame (value and
   `initialized = 1`), parallel as today (machine.cpp:3202-3205).
3. `caller.block = B` (R5). It already is B in the reference: `commit_exit`
   does not change the caller's block on a Call, and a trap's `callers`
   list (machine.cpp:1187-1193) reports `(function, B)` for each caller.
4. `done += 1`; flush (R4) to the caller's function.
5. Call `push_frame(target, arguments, result_slot, resume)`
   (machine.cpp:1517) under R3: it checks the frame limit
   (`FrameLimit`, "calling function N") and, in a task image, charges
   `frame_cells` to the heap with `ensure_heap_cells` and the argument roots
   (a collection may run: the caller's frame is canonical and its committed
   assignments are roots, exactly as in the reference, which commits them
   before `push_frame`), then `make_frame` and `add_live_cells`. A `Fault` is
   a trap in place (R3). After the push, every pointer into the frame arena
   is re-derived (`FrameStack::push` may reallocate, machine.cpp:792-793).
6. Set the new header's `called_from` (new `FrameHeader` field, default null,
   set to null by `FrameStack::push`) to the caller's program (section 4.2).
7. If the exit polls and there is a scheduler: `serve_dump_request(); ++polls`
   as the region's Poll does (tier2_steps.inc:333-339); the Budget covered
   it (R6), so `poll()`'s park (machine.cpp:1671-1678) cannot happen.
8. Callee: if the callee function has a program and its entry-0 checks pass
   (`entries[0].entry_reads` contains no slot at or above the parameter
   count — parameters are initialized by `push`, every other slot is zero
   and uninitialized, machine.cpp:790-795, so a non-parameter early read
   means the callee traps at tier 1 and the program must not be entered;
   `entries[0].entry_canonical` checked over the parameters only, since a
   zero slot fits every width; and, if the callee program uses floats, the
   activation's float-environment flag, set at dispatcher entry), continue
   at the callee's entry label (a checkpoint). Otherwise **leave** (outcome Leave, next block 0 of the
   callee; the call has completed, nothing to undo); the dispatcher runs the
   callee by tier 1 or enters its program with full checks.

`done`, `steps_`, `function_steps_`: the call block is one step, charged to
the caller (step 4). Budget: none read. Failure modes: R1 failures →
deoptimize before effect; `FrameLimit`, `HeapLimit` (task images) and a
collection's effects → trap in place / effects as in the reference. Must be
bit-for-bit equivalent to `commit_exit`'s Call case (machine.cpp:3212-3215)
preceded by `prepare_step`'s charge.

### 4.2 Return

`Return` op fields: `block`, `a` = the value register (or absent for a void
function: the result is 0, machine.cpp:3217).

Preconditions (deoptimize at the return block, before any effect, when one
fails): R1 ops; `frames_.size() >= 2` (the last frame of a task or of the
run is handled by tier 1: `finish_task`, machine.cpp:3230-3233, or the
Completed event, 3234-3246); the returning frame is not a callback
`boundary` (machine.cpp:3221; programs never run in a callback anyway, since
`invoke_callback`, machine.cpp:543-545, runs `step` only, but the check is
cheap and keeps the op total); with a scheduler, no open scope at this depth
(`scopes.back().frame_depth == frames_.size()` would fault, machine.cpp:3223-
3226); and, if the frame's `called_from` program is not null and the
caller's `return_slot` is not absent, the value fits that slot's type width
(`value >> width == 0` for widths below 64; this is what keeps the caller's
width facts sound across the call, section 2).

Effects, in this order (machine.cpp:3216-3253):

1. `done += 1`; flush (R4) to the returning function.
2. With a scheduler, `remove_live_cells(frame_cells(function))`.
3. Remember `return_slot`, `return_block`, `called_from`; `frames_.pop_back()`;
   re-derive frame pointers.
4. If `return_slot` is not absent: `caller.slots[return_slot] = value;
   caller.initialized[return_slot] = 1`.
5. `caller.block = return_block`.
6. Where to go on:
   - `called_from` is not null, and either that program does not use floats
     or the activation's `float_default` flag is set: continue in program
     `called_from` at the label of block `return_block`, without entry
     checks (the caller resumes in the same program version it called from,
     whose analyses produced the facts at that label). The flag is computed
     at every dispatcher entry; it matters because a callee may have left
     compiled code and been re-entered since the call.
   - `called_from` is not null but that program uses floats and the flag is
     clear: leave (outcome Leave, next block `return_block`); the Return has
     completed, the dispatcher's entry check refuses, tier 1 runs the block.
   - `called_from` is null (the caller frame was pushed by tier 1 or entered
     from the dispatcher): leave (outcome Leave, next block `return_block`);
     the dispatcher enters the caller's program at its dispatcher entry
     `return_block` with full checks.

Nothing can fail after the preconditions. Must be bit-for-bit equivalent to
`commit_exit`'s Return case for a frame that is neither the last nor a
boundary, preceded by the step charge. The result's initialized flag is set
exactly when the reference sets it; a void function writes no slot.

### 4.3 AllocObject, AllocCollection, RepeatCollection (and AppendElement)

Ops: `Helper` with `index` = the exit (`AllocateObject`,
`AllocateCollection`, `RepeatCollection`, `AppendElement`), arguments as
today's State, `dst` = result register or absent.

Rule: R1 ops; then R3 (commit, `frame.block`, charge, call) with the call
being the reference `state(exit, arguments)` (machine.cpp:3448) itself;
then, on success, `frame.slots[result_slot] = result; initialized = 1`
(machine.cpp:3256-3260) and continue at the resume (a Jump, or a Leave to a
block outside the program never happens: every block of the function is in
the program).

Order of limit checks is the reference's, because it is the reference code:
`allocate` (machine.cpp:1368): text size, `HandleLimit`, `ensure_heap_cells`
(machine.cpp:1348: `HeapLimit` before collecting, the collection when the
request passes `next_collection_`, `HeapLimit` after it, the new growth
window), `insert_allocation` (handle = `last_handle_ + 1`, `add_live_cells`).
RepeatCollection checks `HandleLimit`, the storage limit, then
`ensure_heap_cells` with the element root (machine.cpp:3520-3539);
AppendElement roots the list and the element (machine.cpp:3560-3569).

Safepoint: a collection can run only inside the reference call. Roots are
`collect`'s (machine.cpp:1286): constants, globals, `mark_frames(frames_)`
(every frame, canonical by aliasing and R3), scheduler structures, the
temporary roots the reference passes, and the entry being allocated. A
reference held only in a program **temporary** is never live across this
call: temporaries are block-local (section 6), the op's own arguments are
passed by value and rooted by the reference exactly as at tier 1, and the
result is stored into its slot before any later op. So no register needs
enumerating.

Fast path: none in this order (the interpreter calls the reference). The
next order may add an inline fast path for AllocateObject and AppendElement
whose precondition is the reference's no-collection, no-limit path
(`last_handle_ < max_handles`, `cells <= next_collection_ - min(live_cells_,
next_collection_)`, `cells <= max_heap_cells - live_cells_`); when it does
not hold, the fast path must fall back to this helper, never deoptimize
after an effect.

### 4.4 State operations

Each `nvr::StateOp` (image.hpp:158), as the program compiles it:

| Operation | Program op | Native code (next order) |
| --- | --- | --- |
| ReadTag, ReadField, WriteField, ReadGlobal, WriteGlobal, ReadLength, ReadElement, WriteElement, ReadVersion | inline State (today) | inline check + load/store: `HeapTable::find` (two loads), kind/type check against `compatible`, index check, `initialized` check; any failure → deoptimize (the existing rule) |
| TextEqual, ClassIs, ClassCast, ScalarText with >1 argument | inline State (today) | stay helper calls (string compare, `derives` walk) |
| AllocateObject, AllocateCollection, RepeatCollection, AppendElement | Helper, R3 | helper; inline fast path later (4.3) |
| RemoveElement, FreeObject, ClassHash | Helper, R3 | helper |
| ConstructText, TextSlice, TextFromUtf8, ClassText, ScalarText with 1 argument | Helper, R3 (4.5) | helper |
| Kernel | Helper, R3 (4.6) | helper |

For helper calls the rule is R3: "commit, set `frame.block`, flush `done`,
charge the step, then call; a fault is the trap in place, never a re-run".
Inline ops keep today's rule: they run before the commit and a fault
deoptimizes (tier 1 re-runs the block and traps). The two must not be
mixed in one block: an inline op is the block's only state op, and so is a
helper.

### 4.5 Text building

ConstructText, TextSlice, TextFromUtf8, ClassText and one-argument
ScalarText are Helpers under R3 calling `state()`. ScalarText reads
`frames_.back().function` (machine.cpp:3652), which is the running frame
(aliasing, R5). They may allocate (safepoint as 4.3) and fault after
allocating (TextFromUtf8 allocates twice, machine.cpp:3614-3634): that is why they are
traps in place, not deoptimizations.

### 4.6 Kernel calls

Helper under R3 calling `state()` → `run_kernel` (machine.cpp:3753). The
kernel charges its declared steps with `charge_kernel_steps`
(machine.cpp:3741), which reads and checks `steps_` (so the flush of R3 step 4
is required before the call) and adds to `function_steps_[frames_.back().function]`
(the running function: R5). Because a kernel adds steps the Budget did not
count, **the kernel's resume block is a checkpoint** (section 2, entries;
section 5), so no block after it runs unchecked against `max_steps`. Kernel
steps are not blocks: they are not added to `done` nor to fleet rounds (as
today, where a kernel exit is one tier-1 step and one round).

### 4.7 Host requests, scheduling points, polls

- Host exits, Scheduler exits (including spawn, join, channel operations,
  scope enter/exit, parallel start/finish), Trap and MessageTrap exits
  compile as `Deopt` at their block: tier 1 runs the block and produces the
  event, the park, or the trap with reference code. Their resume blocks are
  dispatcher entries, so after the host's completion or the scheduler's
  decision the next `run_regions` re-enters the program there.
- Polls of continue exits and of call exits run inside the program as today
  (Budget keeps the quantum out of reach). A Return of a task's last frame
  deoptimizes (4.2), so `finish_task` and its `deciding = true` happen at
  tier 1.
- A scheduling point therefore never happens inside compiled code, and
  `run_regions` stops at `scheduler_->deciding` as today
  (tier2_steps.inc:111).

---

## 5. Checkpoints and deoptimization

**Checkpoints.** Every dispatcher entry (function entry, loop heads, every
resume block including kernel resumes) is a checkpoint; a return
continuation is a resume block, so it is one too. Each begins with `Budget`. Its
`steps`/`polls` are the longest path, in blocks and polls, from it to the
next checkpoint, a Call (the call block and its poll included; the callee's
entry is a checkpoint), a Return (included), a Helper whose kernel resume is
a checkpoint, a Deopt, or a stub — computed as `find_checkpoints` does today
(tier2.cpp:765-787) over the program's in-function edges, with a Call's edge
to its resume and a kernel helper's edge to its resume **cut** (they are
checkpoints). Every cycle within a function passes a checkpoint (back-edge
targets), and every cycle through calls passes the callee's entry
checkpoint, so `done` cannot grow without a Budget.

**What a deoptimization restores.** Nothing needs restoring: by R1 the frame
is at the block's entry state; R2 sets `frame.block` and flushes the
completed blocks. A deoptimization inside a callee leaves every caller frame
canonical because each caller was canonical at its Call (4.1, steps 2-3:
committed assignments, `block` = call block) and nothing writes a caller
frame while a callee runs (section 2). The trap's `callers` list built by
`fault` (machine.cpp:1187-1193) is therefore the reference's.

**Undo log.** The `heap_log_` undo log of `NEURA_TIER_CHECK=1` keeps its
scope: it covers the writes of inline state operations of one frame. Under
`NEURA_TIER_CHECK=1` programs contain no Call, Return, Dispatch or Helper
(they compile as Deopt), so a checked program never changes the depth and
`checked_region` (tier2_steps.inc:440) works unchanged.

---

## 6. The interpreter first

**Decision 2: one interpreter activation, a loop over frames.**
`interpret_program(entry)` runs ops until the program ends; Call and Return
switch the current program, `pc` and frame pointers inside the same loop.
There is no host recursion, so its depth is bounded by nothing but
`max_frames` as at tier 1, and "left" unwinds nothing: every frame is
canonical at every op boundary where the loop can end (aliasing, R2, R3, R5),
so the dispatcher simply continues with `frames_` as they are, possibly
deeper or shallower than at entry. `run_regions` then looks up the region
or program of `frames_.back()` as today.

**Interpreter state.** `program` (current), `pc`, `done`, `float_default`
(bool, from the dispatcher entry), `slots`/`flags` (pointers into the
current frame, re-derived after every push and pop), and `regs` (the current
program's register image). The register image of a program in a run is a
`std::vector<std::uint64_t>` of `register_count - slot_count` entries, kept
in the executor by program id (`program_registers_[id]`), filled once per run
with the program's `immediates` and its text/bytes `constants` from
`const_values_` (as `run_region` does, tier2_steps.inc:382-385), and never
reloaded. Temporaries are written in it; that is safe across recursion
because no temporary is live across a block boundary: every temporary is
defined and used inside one block (Lift results, input Masks, parallel-
assignment Moves, State results, Gathers; `drop_masks` renames only within
the block that defines the mask), and Call/Return end their block.
Register operand `r` reads `slots[r]` when `r < slot_count`, otherwise
`regs[r - slot_count]`.

**Lookup.** `region_for` (tier2_steps.inc:54) keeps counting arrivals per
block as today. When a block's count reaches the threshold it asks the cache
first for the function's program (new `TierCache::program(function,
arrivals)`, same locking and Forming/Settled states as `TierCache::region`,
tier2.cpp:1267-1352, keyed by function). If the function has a program, the
block's plan records `{program, entry index}` when the block is one of its
dispatcher entries and "no region" otherwise; if the function has none, the
block falls back to `form_region` as today. `BlockPlan` (machine.cpp:2892)
gains `program` and `program_entry`.

**Entry.** `run_regions` on a block that is a dispatcher entry of a program:
check `entries[e].entry_reads` initialized and `entries[e].entry_canonical`
widths against the frame, and the float environment if the program uses
floats; refuse (as today, `refused`) otherwise; else `pc = entries[e].label`.

**Ending.** Leave (target block), Deopt (block), Trapped, or a Budget
deoptimization. Each writes `frame.block` (R2/R5) and flushes (R4). The
returned `RegionOutcome::blocks` is the number of blocks completed in this
activation (excluding a trapped block, R3), so `run_regions`' allowance and
the fleet's rounds count every block once.

This interpreter is the reference for the next order's native code: every op
must do in native code what it does here, in the same order.

---

## 7. What the native code of the next order needs (interface only)

- **Register addressing.** Two bases: slot registers at the frame's slot and
  flag arrays (reloaded after every push/pop helper), temporaries at the
  program's register image. `jit::Context` gains `slots`, `slot_flags`,
  `temporaries` and a `float_default` byte; `registers`/`initialized` remain
  for plain regions.
- **One `done`.** A single `done` for the whole native activation chain,
  kept in a machine register and stored to `Context::done` before every
  helper; helpers flush it (R4) because they need `steps_` current.
- **Calls.** `Call` becomes: R1 checks inline; a `call` helper doing 4.1
  steps 1-7 (argument buffer from registers, commit, block, flush, push, poll)
  and returning status (0 continue, trapped, exception) and the callee
  program's entry (or none); then a direct native call to the callee
  program's code, with a nesting counter in the context bounded by
  `max_native_depth` (proposed 1024; beyond it, Leave at the callee's block
  0, so deep recursion continues on the dispatcher and tier 1). The native
  return address is the code at the resume block's label; `called_from` holds the program
  so a Return knows whether it can return natively.
- **Returns.** A `return` helper (4.2 steps 1-5) or, later, inline pops; an
  activation that ends (Leave, Deopt, Trapped, Exception) returns an outcome
  and every enclosing native activation returns at once without touching
  state.
- **Allocation fast path.** An inline path is possible only for the
  reference's no-collection, no-limit case (4.3) and still needs the
  `HeapTable` insert and a `HeapEntry` with heap-allocated `cells` and
  `initialized` vectors (machine.cpp:621-633, 641-653); a true inline bump
  allocation needs a different heap representation, which is outside
  function programs (section 10).
- **Helpers.** `state` helper (R3 variant, which must not report "deoptimize"
  after an effect), `kernel` the same, `check_reference`, `poll` unchanged.

---

## 8. Interactions

- **`NEURA_TIER_CHECK=1`.** Call, Dispatch, Return and Helper compile as
  `Deopt` when `tier_check_enabled()` (tier1.cpp) is set at the cache's
  construction (new `TierOptions::checkable`, so programs in a cache shared
  between checked and unchecked runs cannot exist: the variable is
  process-wide). Programs then never change the frame depth and
  `checked_region` checks them unchanged. These operations are covered by
  `NEURA_TIER_CHECK=2` and by the identity runs.
- **`NEURA_TIER_CHECK=2`.** `shadow_entering`/`shadow_returned` stay where
  they are (tier_shadow.inc:150-151), with one change: `shadow_returned` is
  not called when the run has `finished_` after a trap in place (R3); the
  next `step` returns the finished event and `shadow_event` syncs it
  (otherwise the shadow, stepping to the run's count, would produce the trap
  event at a sync point without an event and report a difference,
  tier_shadow.inc:251-256, 502-505). Additional sync points, because compiled
  code now returns rarely: under a shadow, `shadowed_advance` passes an
  allowance of `shadow_sync_blocks` (proposed 65536) to `run_regions`
  instead of the maximum; the Budget at the next checkpoint past that many
  blocks deoptimizes, tier 1 runs one block, the program is re-entered at
  the next dispatcher entry, and `shadow_returned` syncs. Cost without a
  shadow: zero (no new op). Cost with one: one deoptimization and a few tier-1
  blocks per 65536 blocks, plus the sync itself (frames and counters; the heap
  at the existing spacing), small next to the shadow's own tier-1 run.
- **`--profile`.** `function_steps_` exact by R4 (flush at every Call and
  Return, attributing to the function that ran the blocks); kernel steps by
  `charge_kernel_steps` as today. Task steps likewise.
- **Record/replay.** Host requests happen only at tier 1 (4.7); a trace is
  unchanged.
- **Debugger and coverage.** Both run at tier 0 (`limits.tier = 0` in
  debug.cpp:639 and main.cpp:788) and must stay there. Additionally,
  `setup_tier` (machine.cpp:373) must leave `tier2_` false when
  `limits_.block_observer` is set: today a region skips `prepare_step`'s
  observer call (machine.cpp:3093), and function programs would skip far
  more (risk 10.7).
- **Tasks and the scheduler quantum.** Polls only as in 4.1/4.7 under
  Budget; parks, decisions and `finish_task` at tier 1. Task images charge
  frame cells inside `push_frame` (4.1 step 5) and release them in Return
  (4.2 step 2). `current_task().steps` via R4.
- **Parallel items.** Each item's `State` has its own `program_registers_`;
  programs are immutable and shared. Nothing else changes.
- **Fleets and graph devices.** No tier 2 with a graph device (unchanged,
  machine.cpp:376). Fleet rounds: `run_regions(limit - count)`'s allowance is
  honored by Budget; R3's exclusion of the trapped block keeps the round
  count of tier 1.
- **`--tier-stats`.** The lines `scripts/bench-wall.py` parses
  (`tier: steps N: tier 2 X% (in regions N), tier 1 Y% ...` and `tier: jit
  MODE: N regions compiled (B bytes of code)`, bench-wall.py:276-277) and
  the lines `scripts/tier-boundary.py` takes (`tier: steps`, `tier: jit`,
  tier-boundary.py:269-270) keep their exact format. Meaning changes:
  "in regions" counts steps run in regions or programs; "regions N (B
  blocks ...)" counts regions plus programs; "entered" counts dispatcher
  entries; "left" counts ends by Leave (including a Call into a function
  without a program and a Return to a caller entered from the dispatcher);
  "deoptimized: ... exits" includes host, scheduler and trap exits; "left N
  times at function F block B" keeps its meaning. In the `tier: time:` line,
  "regions" now includes the helpers a program calls (calls, allocation,
  text building, kernels), which were "tier 1" before; a collection inside a
  helper stays "collection" (its own `TierTimes::Scope`, machine.cpp:1287). New line after the
  regions line: `tier: programs P (B blocks, S stubs, E entries); calls C,
  returns R, helpers H, traps in place T; no program N: reason`.

---

## 9. Order of implementation

Each step leaves every existing test passing, adds tests that fail without
it (confirmed by running them on the previous step's binary), and runs the
identity checks of WORK_RULES (runtime-identity t0 vs jit and jit-eager,
tier-boundary) plus the `tier`, `tier-interpreter`, `jit` and `concurrency`
backend suites.

1. **Call and Return for one shape (interpreter only).** Program structure
   (entries, per-entry `entry_reads`/`entry_canonical`, the call-to-resume
   edges of the analyses, checkpoints rule of section 5), aliasing interpreter
   `interpret_program`, `Call` (direct only) and `Return` ops, R1-R5, the
   `called_from` header field (excluded from the shadow's frame comparison,
   tier_shadow.inc:667-680, by name in its comment), programs formed only
   for functions whose every block is lifted/wired/empty and whose exits are
   Continue, inline State, direct Call, or Return; jit refuses programs with
   Call/Return ("calls are interpreted", a `jit fallback` reason) so they run
   on the interpreter; programs are formed only under `NEURA_TIER_PROGRAMS=1`
   (risk 10.11); `NEURA_TIER_CHECK=1` compiles them as Deopt; the R3
   `shadow_returned` rule. Tests: `tests/backend/tier/program-calls.nra` (a
   loop calling a two-block leaf and a recursive function) compared with tier
   0 under `NEURA_TIER_EAGER` for every `--max-steps` from 1 to the run's
   steps at a small size, for `max_frames` from 1 to the recursion depth + 2,
   with `--profile` (function steps), and with tasks (an image with a spawn,
   so frame cells are charged); a statistics check that calls ran in
   programs (fails before); a hand-built image test of entries, checkpoints
   and per-entry `entry_reads` (a loop head whose read slot is assigned
   before the loop is not in entry 0's set).
2. **Dispatch and closure calls.** `Dispatch` op; tests with the
   `dispatch.nra`-like interface calls, a closure call, and an invalid
   dispatch (trap at tier 1 through Deopt).
3. **Helpers for allocation and growth.** AllocateObject,
   AllocateCollection, RepeatCollection, AppendElement under R3; tests at
   every `max_heap_cells` around each allocation of a small program (HeapLimit
   trap in place, collection at every allocation), `max_handles`, and a
   check that `peak_heap_cells` and the trap's callers match tier 0.
4. **Other helpers.** Text building, RemoveElement, FreeObject, ClassHash,
   Kernel with resume checkpoints; tests: a kernel at every `--max-steps`
   around its charge (StepLimit inside the kernel and right after it),
   invalid UTF-8 traps in TextFromUtf8/ConstructText, profile function steps
   with kernels.
5. **Entries at host and scheduler resumes; stubs; proving at formation;
   refresh.** Tests: programs with a console write in a loop, a channel
   loop under quanta 1..4, a function with an unliftable block (stub) whose
   deoptimizations are counted.
6. **Sync points for `NEURA_TIER_CHECK=2` and the statistics line.** Tests:
   tier_shadow hook test that a long loop in a program syncs at least N
   times (fails before: one sync), the perturbation tests of
   tier_shadow_tests.cpp detect a perturbation inside a program; bench-wall
   and tier-boundary parse the output.
7. **Documentation** (NVM.md "Tier 2", "Controls"), status.

Native code for programs is the next order.

---

## 10. Risks and open questions

1. **Allocation is not made cheap by function programs.** Evidence: census
   binary-trees (object allocation 10.5% of outside steps, collection 28.2 s
   of 154.9 s at long size) and the reference `allocate` path (`HeapEntry`
   with two `std::vector`s, a `HeapTable` page insert). Recommendation:
   accept the floor in this milestone; a heap-layout order (cells in a slab,
   flags in a bitmap) is what removes it.
2. **The collector is a floor.** binary-trees 28.2 s collection at long size
   is untouched (see census); sha256 at long size holds a 300-million-cell
   list. Recommendation: separate order.
3. **Formation proving cost on short runs.** nbody at full size spends
   148 ms of 287 ms proving today; proving every hinted block at formation
   adds proofs of blocks that would never run. Bound: `program_proofs` = 32
   per formation. Open: whether the lead prefers proving only blocks that
   have run at least once (then cold blocks are stubs until they run).
   Recommendation: 32 with the census's proving lines as the acceptance
   measure.
4. **Kernel steps and Budget.** Kernels charge steps the Budget does not
   see; kernel resumes as checkpoints (4.6) is the fix. Evidence:
   `charge_kernel_steps` (machine.cpp:3741). A test at every step limit
   around a kernel is in step 4.
5. **Version skew on Return.** A program refreshed while frames of the old
   version are suspended: `called_from` (4.2) returns into the same version.
   Open: whether the lead prefers a per-depth side table in the executor to a
   new `FrameHeader` field (both are tier machinery not compared by the
   shadow). Recommendation: the header field (moves with the frame when a
   task parks).
6. **Frame arena reallocation.** Aliasing makes every pointer into
   `slots_`/`initialized_` invalid after a push (machine.cpp:792-795). Every
   op after a push/pop must re-derive. Recommendation: one accessor in the
   interpreter that returns `frames_.back()` pointers, called after Call and
   Return only; a sanitizer run of the tier suite in step 1.
7. **`block_observer` with tier 2.** Today regions already skip the observer
   (pre-existing, not observable through the CLI because coverage and the
   debugger force tier 0). Recommendation: make `setup_tier` turn tier 2 off
   when an observer is set, in step 1.
8. **`--tier-stats` semantics.** "regions" and "in regions" will mean
   regions or programs. Open: rename in a later change? Recommendation: keep
   the parsed formats, add the programs line.
9. **Programs never run inside callbacks.** `invoke_callback` steps at tier
   1 only (machine.cpp:543-545). This is unchanged and makes callbacks slow;
   not a correctness risk.
10. **What function programs do not buy.** Evidence: census.md, A3 and A4.
    sha256 is bound today by tier-1 dispatch of calls to two one- or
    two-block functions (`rotate`, `add`): function programs remove it.
    nbody at long size is bound by the inline state helper calls inside its
    regions (498 of 737 s) and by `run_kernel` for `sqrt`; function programs
    remove only the region entries the kernel forces, and estimate (b) needs
    the inline state operations of section 4.4's native column.
    text-split is bound by collection (192 of 330 s), binary-trees by
    allocation and collection. Recommendation: hold the implementation to
    estimate (a) for this milestone's native order, and treat inline state
    operations (4.4) as the first native item, since they are worth more than
    calls for nbody, fannkuch, sort and the array programs.
11. **The interpreter is slower than today's native regions.** In this order
    programs run only on the interpreter (section 9, step 1), so a function
    whose loop runs as a native region today would run interpreted once it
    has a program. Evidence: today's interpreted regions cost several times
    the native ones (the `backend-tier-interpreter` suite exists for that
    reason). Recommendation: form programs only when `NEURA_TIER_PROGRAMS=1`
    (process environment, like `NEURA_TIER_EAGER`) in this order, run the
    identity checks and the tier suites with it set and unset, and make it
    the default in the native order. Open for the lead.
12. **Collection cost grows with the number of allocations.** Evidence:
    text-split's collections cost 5.8 µs each at full size, 31 µs at ten
    times that, and about 200 µs at long size with a peak heap of 2,937
    cells; `HeapTable::sweep` (machine.cpp:675-689) visits every entry of
    `pages_`, which grows with the highest handle ever assigned
    (machine.cpp:651-653). Not part of function programs; a separate fix
    (keep a list of live pages) would remove most of text-split's 192 s
    and much of parse-results' 106 s (of 324 s at long size, with a peak
    heap of 23,067 cells; its collections cost 90 µs each at full size and
    about 740 µs at long size, derived from the collection time and the
    allocation rate).
13. **A pre-existing difference found while reading: a fault inside a region
    writes the `--task-dump` twice.** `fault` (machine.cpp:1150-1194; the dump at 1182-1184) writes
    the on-trap dump when a `Fault` is built, before the region catches it
    and deoptimizes; tier 1 then faults again and writes it again. Reproduced
    on b9ef537 with `probe/dump.nra` (`probe/probe.sh`): at tier 1 one dump,
    with `NEURA_TIER_EAGER=1` two, the first naming the stale block
    `dump.nra:6:5`. Function programs inherit it for every inline state
    operation (they deoptimize on a fault). Recommendation: write the dump
    where a fault becomes the run's trap (`trap`, machine.cpp:1142), in a
    separate change before step 1.
