# Deviations from design.md (order 2G, phase 2)

Each entry: the rule, what the code required, what I did, the evidence.

## D1. Return: no width check of the returned value (design 4.2 precondition, section 2 widths)

- Rule: across a Call's edge to its resume block the result slot's width
  becomes its type's width, justified by a Return precondition that checks
  the returned value against the caller's result slot width.
- What the code allowed: the optimizer's width analysis already treats a
  register defined by an op without a width rule as 64 bits wide
  (`Optimizer::width_of`, default case). Making the Call op the definition
  of its result slot (`Optimizer::definition`) gives the resume block a
  64-bit result slot from the call edge, so no mask is dropped and no check
  is eliminated on the strength of the returned value.
- What I did: no width check in Return; the Call edge carries every other
  caller slot's width and facts as designed, the result slot's as unknown.
  This is more conservative than the design, never less.
- Evidence: src/nvm/tier2.cpp `definition` (Code::Call) and `width_of`;
  tests/backend/tier_programs_tests.cpp compares every step limit.

## D2. Host, scheduler and helper-kind state exits are deoptimizations from step 1 (design 9, step 5)

- Rule: step 1 forms programs only for functions whose exits are continue,
  inline state, direct call or return; resume entries at host and scheduler
  exits come in step 5, helpers in steps 3 and 4.
- What the code required: almost every function of the benchmarks and the
  standard library has a trap exit, an allocation, a list append, a text
  operation or a host request; under the step-1 shape almost no function
  would have a program, and the tests would exercise little.
- What I did: every exit a program does not run (host, scheduler,
  non-inline state, trap, message trap) compiles as `Deopt` at its block
  (exact for any exit: tier 1 runs the block), and the resume (and a host's
  error resume) of each is an entry with its own entry checks (design 2,
  entries 3). No helper ops; no stubs (a function with a block that is not
  lifted, wired or empty gets no program).
- Evidence: `Compiler::takes`, `find_entries` in src/nvm/tier2.cpp; the
  smoke and identity comparisons below.

## D3. Proving at formation is in step 1 (design 3; section 9 put it in step 5)

- The coordinator fixed the cap at 32. Without it, a function with any
  block that has not run (a branch never taken yet) has no program, since
  stubs are not in this stage. `TierCache::program` proves at most
  `program_proofs` hinted, never-tried blocks with `resolve(..., hint_after)`.

## D4. Programs are never offered to the native code generator (design 9, step 1: "jit refuses programs ... a `jit fallback` reason")

- What I did: `run_regions` asks for native code only for a region that is
  not a program, so no `tier: jit fallback` line appears; the generator
  also refuses Call, Return and Dispatch ops ("a function program") in case
  one reaches it.
- Reason: a program's slot registers are the frame (decision 1), which the
  generated code cannot address; refusing every program at compile time
  would add a fallback count per program with no information.

## D5. `--tier-stats` attributes a program's steps to its function's block 0

- `region_steps` of the block plan of (function, entry) is per region
  entry; a program's steps are added to (program function, block 0) when
  they are flushed. Only the statistics' per-region map is affected.

## D6. The float environment is read at every entry of a program

- Design 6: `float_default` "from the dispatcher entry". I read it at every
  `run_program` entry, whether or not the entered program uses floats, so
  that a call into a float-using program is decided correctly (design 4.1
  step 8); it is a read of the floating-point control state.

## D7. The `tier: programs` line also gives the steps run in programs

- Design 8 specified `tier: programs P (B blocks, S stubs, E entries);
  calls C, returns R, helpers H, traps in place T`. The line ends with
  `; steps in programs N` as well (RegionCounts::program_steps), because
  "in regions" counts steps of regions and programs together and the
  coordinator asked for the share of steps in programs. No script parses
  the line.
