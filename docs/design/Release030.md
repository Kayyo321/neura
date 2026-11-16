# Neura 0.3.0: baseline and frozen contracts

This page records the starting point of the 0.3.0 work and the contracts that are fixed before any of it changes behavior. Contributors need them in order to tell a regression from an intended change. It is a design note: the language pages, [Project](../Project.md), and the format pages stay authoritative for behavior. A decision recorded here becomes a rule only when the page that owns it says so. The owner signed off every row marked *frozen* on 2026-11-16. A frozen row changes only through a later change to this page that the owner approves, and that change says why.

## Baseline

| Item | Value |
| --- | --- |
| Base | `v0.2.1-rc.3`, commit `3ffc212`, the head of `main` on 2026-11-16. 0.3.0 supersedes 0.2.1, and no 0.2.1 final release is planned. |
| Toolchain version | `VERSION` 0.2.1; `Std.Core.Version` 0.2.1 |
| Formats | NeuraIR reader 1.0 to 1.10 ([NeuraIR format](../NirFormat.md)); NVR reader 1.0 to 1.6 ([NVR](../runtime/NVR.md)). rc.3 changed neither. |
| Tier 1 suite | 182 of 182 CTest entries pass, Release build with gcc 14, no compiler warnings in Neura's sources (local run, 2026-11-16; the CI minimum is 177) |
| Tier 2 | Run 43931171051 on the rc.3 sources: sanitizer shards 1 and 2, Linux arm64, macOS arm64 and macOS x86-64 all pass |
| Wall-clock speed | [Benchmarks](../Benchmarks.md#021-rc3-at-the-long-sizes): 17 programs at the long sizes, geometric mean 3.87 times Java on the stated machine |

## Execution modes

rc.3 made native tier 3 the default on x86-64 Linux and left it off on every other platform ([execution tiers](../runtime/NVM.md#execution-tiers)), so one image now runs on several paths that must agree. A change that can alter what an image does at run time is checked in each of the modes below with `scripts/runtime-identity.py`, `scripts/tier-boundary.py` and `scripts/program-fuzz.py`. Each mode must give the same exit status, output, trap, steps, peak heap cells and profile as `--tier=0`.

| Mode | Settings | Covers |
| --- | --- | --- |
| Reference | `--tier=0` | The reference evaluator, against which every other mode is compared |
| Default | none | Native tier 3 on x86-64 Linux; tiers 1 and 2 elsewhere |
| No native code | `--jit=off` | Regions on the region interpreter, tier 3 off |
| Tier 3 off | `NEURA_TIER3=off` | Every platform without native tier 3, which includes macOS, AArch64, Windows and a WebAssembly build |
| Tier 3 interpreted | `NEURA_TIER3=interp` | Tier 3's IR and passes, without machine code |
| Function programs | `NEURA_TIER_PROGRAMS=1` | Tier 2 function programs on the interpreter |
| Eager | `NEURA_TIER_EAGER=1` | Every block lifted and every region or program formed when it first runs |
| Checked | `NEURA_TIER_CHECK=1`, `NEURA_TIER_CHECK=2` | Region re-execution and the tier 1 shadow run |

On rc.3, `scripts/runtime-identity.py` gives identical captures in every mode of the table for its 159 cases. `--extended` (4,902 cases) at `--tier=0` and in the default mode with native code is identical too. The captures equal those of the earlier rc.3 development head `44fa6a9` in every file; the only difference is the image path that appears in trap messages. These captures, built from one shared set of images, are the identity baseline for the 0.3.0 work. They are kept with the gate evidence and are not part of the repository.

## Performance gate

The gate for 0.3.0 is that it is no slower than rc.3. Speed equal to Java's is reported but does not block the release.

| Rule | Status |
| --- | --- |
| `scripts/bench-wall.py --long --runs 5` on the machine of the rc.3 table, with the same command and sizes. Every output matches Java, and every program's steps and peak heap cells match the table, or the change explains why they differ. | frozen |
| Each program's median time and the geometric mean are within 5% of the rc.3 figures | frozen |
| The same run with `NEURA_TIER3=off` is within 5% of rc.3 with `NEURA_TIER3=off` | frozen |
| `tests/benchmarks/baseline.txt` keeps its existing rule ([CONTRIBUTING](../../CONTRIBUTING.md)) | in force |
| The gate runs after each change to storage, hooks or inline values, and before every 0.3.0 candidate | frozen |

## Size and dependency baseline

Measured on the rc.3 Release build for x86-64 Linux:

| Item | Size |
| --- | ---: |
| `neura` | 9,773,816 bytes |
| `neura`, stripped | 8,734,864 bytes |
| Native executable from `neura build --native` | 4,859,366 to 4,871,368 bytes (7,301,759 with a C import) |
| `neura-0.2.1-rc.3-x86_64-linux-gnu.tar.gz` | 10,008,390 bytes |
| `neura-0.2.1-rc.3-aarch64-linux-gnu.tar.gz` | 8,576,321 bytes |
| `neura-0.2.1-rc.3-x86_64-apple-darwin.tar.gz` | 3,542,041 bytes |
| `neura-0.2.1-rc.3-aarch64-apple-darwin.tar.gz` | 3,293,854 bytes |

Launch time of a whole process, measured on the machine of the benchmark table with 200 runs after 5 warm-up runs, with output discarded and the page cache warm:

| Command | p50 | p95 |
| --- | ---: | ---: |
| `neura --version` | 0.52 ms | 0.60 ms |
| `neura run hello-world.nvr -- a b` | 0.68 ms | 0.78 ms |
| The same with `--jit=off` | 0.68 ms | 0.77 ms |
| `hello-world` built with `--native`, `a b` | 0.47 ms | 0.55 ms |

`neura` links dynamically against `libffi.so.8`, `libstdc++.so.6`, `libm.so.6`, `libgcc_s.so.1` and `libc.so.6`, and nothing else. libclang is loaded only when a C import needs it.

| Budget for the split products | Status |
| --- | --- |
| Every shipped executable depends on no library beyond the five above, and on libclang only where `neura` uses it today | frozen |
| The `neura` dispatcher, stripped, is at most 1 MiB | frozen |
| The execution core, the executables a runtime-only installation contains, contains no compiler, parser, lowering or decompiler code | frozen |
| Each archive is at most 25% larger than the rc.3 archive for its target, excluding application target components (browser, desktop and database providers), which are measured separately | frozen |
| `neura --version` and a direct run of a hello-world image take no longer than with rc.3, within 5% | frozen |

## Format revisions

Each change to an artifact format takes the next minor version. The writer keeps emitting the earliest version that can encode the program, so a program that uses no new feature keeps its exact bytes. Every reader accepts all earlier versions. The ordering below is fixed. The contents of each revision are fixed by the change that introduces it and recorded on its format page.

| Revision | First user | Status |
| --- | --- | --- |
| NeuraIR 1.11 | Owner, borrow, effect and policy facts; multiword typed values | frozen |
| NeuraIR 1.12 and later | Later language features, in the order they land | frozen |
| NVR 1.7 | Frame and static regions, inline aggregates, direct construction | frozen |
| NVR 1.8 and later | Later runtime features, in the order they land | frozen |

An image of NVR 1.6 or earlier keeps its current storage accounting under every tier.

## Products and executables

0.3.0 ships two products, **Neura-sdk** and **Neura-runtime**. Their packages are named `neura-sdk` and `neura-runtime`. Every public tool is a separate executable in the same `bin/` directory, and `neura` dispatches to them. The paired forms in each row behave the same: same options, image bytes, diagnostics, exit status and streams.

| `neura` form | Direct executable | Product | Status |
| --- | --- | --- | --- |
| `build`, `check`, `emit-ir` | `neurac` (default action `build`) | SDK | frozen |
| `run` | `neura-run` | both | frozen |
| `runtime ...` | `neura-run runtime ...` | both; needs `neurad` for service control | frozen |
| (service) | `neurad` | runtime, optional | frozen |
| `test` | `neura-test` | SDK | frozen |
| `debug` | `neura-debug` | SDK | frozen |
| `lsp` | `neura-lsp` | SDK | frozen |
| `fmt` | `neura-fmt` | SDK | frozen |
| `decompile` | `neura-decompile` | SDK | frozen |
| `assemble`, `disassemble` | `neura-asm`, `neura-disasm` | SDK | frozen |
| `inspect`, `inspect-image` | `neura-inspect` | both | frozen |
| `docs` | `neura-doc` | SDK | frozen |
| `c-bindings` | `neura-cbind` | SDK | frozen |
| `explain`, `tags` | `neura-explain`, `neura-tags` | SDK | frozen |
| `doctor` | `neura-doctor` | both | frozen |
| `update`, `uninstall` | `neura-update`, `neura-uninstall` | both | frozen |
| `new`, `dev`, `contracts`, `package` | `neura-new`, `neura-dev`, `neura-contracts`, `neura-package` | SDK | frozen |
| `--version`, `help`, `capabilities` | built into `neura` | both | frozen |

## Run and service defaults

| Rule | Status |
| --- | --- |
| `neura run`, `neura-run`, `neura test` and editor Run and Debug execute directly (`--runtime=off`). They do not contact, start or probe a service, even when one is running. | frozen |
| `--runtime=auto` uses or starts a compatible service and may fall back to direct execution only before the launch is committed. `--runtime=required` never falls back. | frozen |
| `--detach` requires `--runtime=auto` or `--runtime=required` | frozen |
| Ready workers that have never run an application: 1 | frozen |
| Active applications per service: 64 | frozen |
| Pending launches, prepared and not yet committed: 16; a launch beyond this fails with an admission error before commit | frozen |
| Launch reservation lifetime: 30 seconds | frozen |
| Image cache: 256 MiB | frozen |
| Terminal status records: 1,024, each kept for 24 hours | frozen |
| Control message size: 1 MiB, excluding the image bytes, which are sent by handle or content identity | frozen |
| Grace period between `stop` and forced termination: 10 seconds | frozen |

## Migration inventory

Counts of the sites in today's syntax that each planned breaking rule would touch, taken at `3ffc212` over `stdlib/`, `examples/`, `tests/` and the `neura` code fences of the documentation. Rejected-program fixtures under `tests/ui` are excluded. A site is one matching line, or one construct found by a token-level scan, which is an approximation of the compiler. Each change that introduces a rule recounts its rows with the compiler itself before the default changes.

| Rule | Sites | Most in | Confidence |
| --- | ---: | --- | --- |
| A list literal or range with no target type needs a target type or an explicit `new` | 60 | tests (36) | medium |
| A reference to a local primitive no longer moves the local to the heap | 19, and 34 `&T` declarations; none in `stdlib/` | tests (14) | medium |
| A function cannot return an array that aliases a parameter or field | 6 confirmed (2 in `stdlib/`), 262 candidates | tests | low |
| An array cannot be kept in a heap field, iterator or closure | 8 fields | `tests/bench-wall` (7) | medium |
| A recursive enum payload needs an explicit reference | 9 enums, 4 in `stdlib/` (`Error`, `PortableError`, a `Node`, `JsonValue`) | stdlib | high |
| A capturing lambda or bound method that escapes needs `new func` or `new bound` | about 87 lambdas and about 35 bound methods | tests (46) | low |
| A field or static whose default would be admitted by a class hook | 522, of which 96 have accessors | tests (199) | medium |
| A class match that relied on boxing a scalar | 0; the language has no such boxing | none | high |
| A user of recursive `Error` contexts | 67, 13 of them in `stdlib/std/core/error.nra` | tests (43) | medium |
| A script, workflow or document that assumes one `neura` executable holds every tool | 63 | `scripts/` (24) | low |

## Fixed when each feature starts

The exact grammar of each new form, and the table of class hook events, are fixed on their language pages when work on that feature starts and before its first implementation change. They are not fixed here. A form is fixed with accepted and rejected examples and with its diagnostics. No feature may leave a source-level rule to the runtime or the converter.
