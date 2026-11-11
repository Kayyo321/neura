# The `neura` command line

One executable compiles, runs, tests, packages, and decompiles Neura programs. Specification: [Tooling](https://github.com/Kayyo321/neura/blob/main/docs/lang/Tooling.md) and [Install](https://github.com/Kayyo321/neura/blob/main/docs/Install.md). `neura help` prints the full usage.

## Contents

- [Install and check the tool](#install-and-check-the-tool)
- [Everyday workflow](#everyday-workflow)
- [check](#check)
- [build and run](#build-and-run)
- [Capabilities](#capabilities)
- [Run limits and diagnostics](#run-limits-and-diagnostics)
- [test](#test)
- [Native executables](#native-executables)
- [NeuraIR, images, and decompiling](#neurair-images-and-decompiling)
- [Documentation export](#documentation-export)
- [Exit statuses](#exit-statuses)

## Install and check the tool

```sh
curl --proto '=https' --tlsv1.2 -fsSL https://github.com/Kayyo321/neura/releases/latest/download/install.sh | sh
neura --version        # neura 0.1.0 (x86_64-linux-gnu, commit ...)
neura doctor           # ok / warn / FAIL for the install and optional tools
neura update --check   # status 4 when a newer release exists
```

Releases exist for Linux x86-64 and arm64 (glibc 2.39+), macOS 15+ (Apple silicon and Intel), and Windows through WSL 2. The installer writes under your home directory without `sudo`. A source build (`cmake -S . -B build && cmake --build build`, from a recursive clone) produces `build/neura` and reports a `-dev` version. The standard library is found automatically next to the tool; no `-I` is needed for `Std.*`.

## Everyday workflow

```sh
neura check app.nra                           # fast type check, silent on success
neura build app.nra -o app.nvr                # compile to an image
neura run app.nvr -- --name Ada               # run; arguments after -- go to main
neura test tests/app-tests.nra -I src         # run tests
neura build --native app.nra -o app           # single native executable (Linux)
```

Every command that takes sources accepts several `.nra` files and `-I <source-root>` (repeatable) for project imports. Diagnostics go to standard error as `path:line:column: error: message`, and the command exits with status 1.

## check

`neura check [-I root] file.nra...` parses, resolves, and type-checks without optimizing or building. Use it after every edit.

`neura check --task-safety file.nra` prints, for every function of the linked program, whether a spawned task may call it and why not, then exits 0.

## build and run

```sh
neura build app.nra -o app.nvr                # sources -> image
neura build lib.nir -o app.nvr                # prebuilt NeuraIR -> image
neura build --no-optimize app.nra -o app.nvr  # skip the optimizer
neura build --no-source-map app.nra -o app.nvr  # omit source locations
neura run app.nvr -- a b c
neura run --stats app.nvr                     # steps, heap peak, host requests, graph sizes
neura run --profile app.nvr                   # steps per function
neura run --each lists.txt app.nvr -- common  # run once per line of argument lists
neura run --record trace.txt app.nvr          # record every host interaction
neura run --replay trace.txt app.nvr          # replay them without touching the outside world
```

- A build is deterministic: the same program and options give the same bytes.
- The image needs no sources. Traps print their source location when the image has a source map.
- `build` fails with `the program has no entry point` when no module overrides `Std.Program.Entry.main`. Library files are checked with `check` and exercised with `test`.

## Capabilities

Everything a program does outside itself is a host service. `run`, `test`, and the policy of `build --native` **grant every group by default**; deny groups to sandbox a run. Console I/O and `Process.exit` are always available. A denied call returns an `Io` error of kind `CapabilityDenied` (error 14); native calls trap instead.

| Group | Deny | Grant again |
| --- | --- | --- |
| Files (`Std.Fs`) | `--deny-fs` | `--allow-fs`, or `--allow-fs=prefix,...` for paths at or below the prefixes |
| Network (`Std.Net`) | `--deny-net` | `--allow-net` |
| Environment variables | `--deny-env` | `--allow-env` |
| Clocks and sleeping (`Std.Time.SystemClock`, timers) | `--deny-clock` | `--allow-clock` |
| Entropy (`Random.from_entropy`) | `--deny-entropy` | `--allow-entropy` |
| Native code (`Std.DynamicForeign`, `import:c`) | `--deny-native` | `--allow-native` |
| All of the above | `--deny-all` | |

Options apply left to right: `neura run --deny-all --allow-fs=data app.nvr` grants only files below `data`. Run untrusted images with `--deny-all` plus what they need.

## Run limits and diagnostics

| Option | Default | Exceeding it |
| --- | --- | --- |
| `--max-steps N` | 100,000,000 block evaluations | status 152, `step limit exceeded` |
| `--max-frames N` | 10,000 active frames (recursion depth) | status 139 |
| `--max-heap-cells N` | 67,108,864 cells | status 137 |

Raise `--max-steps` for long computations: a loop that runs past the default ends with `trap: step limit exceeded: 100000000 steps`. Concurrency options (`--max-tasks`, `--task-quantum`, `--parallel-workers`, `--task-dump`, `--trace-events`, `--virtual-time`) are in [concurrency.md](concurrency.md#running-and-testing-concurrent-programs).

GPU execution: `--gpu=auto` (default) uses a Vulkan device only when it is predicted to be faster; `--gpu=off` or `--no-gpu` forces the CPU; `--require-gpu` fails without a device. Release archives are built without the GPU backend. Results never depend on the device.

## test

```sh
neura test -I src tests/math-tests.nra tests/text-tests.nra
```

- Runs every exported zero-argument `void` function named `test_*` in a module of each listed file (imported files contribute no tests), each in a fresh execution after normal program initialization.
- Prints `PASS Module.test_name` or `FAIL Module.test_name: <message> at file.nra:line:col`, then `N test(s), P passed, F failed`. Exits 0 only when every test passes, and 1 when any fails, none is found, or the build fails.
- Accepts the capability options, `--max-steps`/`--max-frames`/`--max-heap-cells` (per test), `--stats`, `--no-optimize`, and the concurrency options, plus `--virtual-time`, `--schedules N`, `--schedule-seed S`, `--save-schedule FILE`, and `--schedule FILE`.
- No fixtures, mocks, timeouts, or parallel test execution. A test file does not need an entry module.

## Native executables

```sh
neura build --native --deny-all --allow-fs=data --max-steps 1000000 app.nra -o app
./app --help                         # every argument reaches main
neura inspect app                    # launcher, recorded policy and limits, image
neura inspect app -o app.nvr         # extract the image
neura build --native --static app.nra -o app   # fully static, no native interop
```

The executable embeds the image, a CPU launcher, and the capability policy and limits given at build time (all groups granted unless you deny them). `NEURA_RUN_OPTIONS` can lower limits or deny more at run time but never widen the policy; `--sealed` makes the executable ignore it. Linux x86-64 is the verified target; macOS releases have no launcher.

## NeuraIR, images, and decompiling

```sh
neura emit-ir app.nra                     # print optimized, typed NeuraIR
neura emit-ir --no-optimize app.nra       # print IR as lowered
neura emit-ir lib.nra -o lib.nir          # write a versioned .nir artifact (importable)
neura disassemble app.nvr -o app.nvra     # lossless text form of the image
neura assemble app.nvra -o app.nvr        # back to an image (validated)
neura decompile app.nvr                   # typed reconstruction of functions and blocks
neura decompile --no-provenance app.nvr   # ignore optional names and source map
neura c-bindings header.h                 # show the module a C import would generate
```

## Documentation export

`neura docs file.nra -o index.json` exports `///` documentation as JSON for editors; `neura docs --doxygen file.nra` is a Doxygen input filter (`INPUT_FILTER = "neura docs --doxygen"`, `EXTENSION_MAPPING = nra=C++`).

## Exit statuses

Of the tool itself:

| Status | Meaning |
| --- | --- |
| 0 | Success; for `test`, every test passed |
| 1 | Usage error, a source diagnostic, an image that fails to load, a failed test |
| 3 | `update`/`uninstall` on an install `neura` did not make |
| 4 | `update --check` found a newer version |

Of a program under `neura run` or a native executable: the `ExitCode` that `main` returns, the status given to `Process.exit`, or the status of a trap's class (136 arithmetic, 139 bounds or nil, 134 explicit failure or assertion, 137 memory, 152 steps, 132 invalid operation). See [errors-and-diagnostics.md](errors-and-diagnostics.md).
