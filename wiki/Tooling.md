<!-- Generated from docs/lang/Tooling.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Tooling and project workflow

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

## Build the compiler

Build with CMake 3.20 or newer and C and C++20 compilers, from a checkout that includes the `third_party/neura-libclang` submodule. [Contributing](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md#set-up-a-checkout) lists the commands for fetching it, the libffi development package the build needs for native calls, and the libclang library that C header imports load.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

With a single-configuration build, the executable is `build/neura`. For multi-configuration builds, use the executable in the selected configuration directory and pass that configuration to CTest with `-C`.

```sh
build/neura check examples/guide/core.nra
build/neura emit-ir examples/types/polymorphism.nra
build/neura check examples/modules/multi-file/main.nra
build/neura check -I examples/guide examples/testing/math-tests.nra
```

`Std` imports resolve in the trusted standard library: below `share/neura/stdlib` of an installed tool, or the checkout's `stdlib` directory for a build directory directly inside the checkout ([Modules](Modules#standard-library-imports)).

Both commands accept multiple input paths and resolve imports across the source roots. `check` validates the source and succeeds silently on valid input. It does not run the optimizer. `emit-ir` writes a typed NeuraIR listing with resolved symbol references and explicit operations for control flow, state, and calls. The listing is for inspection. A source diagnostic reports the path, line, and column and returns a nonzero exit status.

`check --task-safety` prints a report instead of only checking: for every function of the linked program, whether a spawned task may call it, the reason when it may not, and whether it could also be the worker of a parallel map ([Concurrency](Concurrency#52-task-safe-calls)). Functions are grouped by source file, and a generic function is listed once for each specialization the program uses. The report ends with a count, and the command exits with status 0 whatever the report says. The option applies only to `check`.

`emit-ir` prints optimized IR by default. Pass `--no-optimize` to print the IR exactly as lowered. To see what the optimizer changed, compare the two listings:

```sh
build/neura emit-ir --no-optimize tests/ir-optimization/fixtures/folding.nra > before.ir
build/neura emit-ir tests/ir-optimization/fixtures/folding.nra > after.ir
diff before.ir after.ir
```

The two listings for this fixture share a symbol table, so the diff shows rewritten instructions. For programs that take primitive references or specialize generics, the optimized listing can also show new or rewritten symbols from those compilation stages, and inlining adds a local named `$inline.<callee>.<name>` to the caller for each inlined parameter and local. The listing is separate from the versioned `.nir` file described below, which holds NeuraIR before those transformations. See [NeuraIR optimization](IrOptimization) for the passes and their preconditions.

## Build and run executables

```sh
build/neura emit-ir examples/hello-world/hello-world.nra -o hello.nir
build/neura build hello.nir -o hello.nvr
build/neura build examples/hello-world/hello-world.nra -o hello.nvr
build/neura run hello.nvr -- arg1 arg2
```

`emit-ir -o` writes the program as lowered, before optimization, to a versioned [`.nir` file](NirFormat). `emit-ir` also accepts one `.nir` file in place of sources and prints its listing. `build` takes sources or one `.nir` file. It verifies and optimizes the IR, converts it to neuron graphs, and writes the `.nvr` image only when every step succeeds. Both routes produce the same bytes for the same program and options, and repeating a build reproduces them. `--no-optimize` converts the verified IR without optimizing it. `--no-source-map` omits the optional source map, which otherwise labels blocks with paths relative to the sources' common directory. `--no-lift-hints` omits the optional `LIFT` section, which records the typed operations of each block for the runner's faster tier ([NVR](NVR#lift-hints)); it changes nothing a program does.

Sources can [import prebuilt modules](Modules#prebuilt-neurair-modules) with `import:"library.nir".Module`. `check`, `emit-ir`, `build`, and `test` resolve and link those imports during source compilation.

`neura docs <file.nra>...` exports declaration documentation and source ranges as JSON for IDE tooling. Pass `-o <file.json>` to save the index. `neura docs --doxygen <file.nra>` emits a declaration view for Doxygen's input filter. [Documentation comments](CoreLanguage#documentation-comments)

`run` loads and validates an image, binds its host imports to the runner's services ([NVM](NVM#host-services)), and passes the arguments after `--` to the entry function as UTF-8 text. The image needs no source files. The exit status is the `ExitCode` that `main` returns, or the status given to `Std.Program.Process.exit`. A runtime trap prints its reason and source location, when the image has a source map, to standard error and exits with the status of its class, such as 136 for an arithmetic trap or 134 for a message trap ([Errors](Errors)). Load, bind, and usage errors exit with status 1. `--max-steps`, `--max-frames`, and `--max-heap-cells` bound a run. A program with tasks has further limits and options ([Run programs with tasks](#run-programs-with-tasks)). `--stats` prints the run's step count, peak heap cells, host requests, graph, neuron, and edge counts, and image bytes to standard error. `run` evaluates a block whose graph is proven to compute typed operations as native operations ([execution tiers](NVM#execution-tiers)) and every other block by reference; the result, the steps, the traps, the output, and a recorded trace are the same either way. It also runs regions of proven blocks of one function together, without returning to its dispatcher between them, with the same results. `--tier=0` evaluates every block by reference, `--tier=1` allows lifted blocks, and `--tier=2`, the default, allows regions too; `NEURA_TIER=0` or `NEURA_TIER=1` in the environment has the effect of that option for every run. At tier 2 a region runs as native code that the runner generates for it, where that is enabled ([native compilation](NVM#native-compilation)), with the same results again. `--jit=auto`, the default, generates code on x86-64 Linux and interprets regions elsewhere; `--jit=on` generates code wherever the runner has a code generator (x86-64 and AArch64, on Linux and macOS; experimental where `auto` does not enable it) and fails the run at its start where it cannot; `--jit=off` interprets every region. `NEURA_JIT=off`, `on`, or `auto` in the environment applies to every run: `off` from the option or the variable wins, then `on`. Generating code for regions is not the native capability: `--deny-native` does not turn it off. `--tier-stats` prints, after the run, the blocks lifted, the regions formed and how they ended, the regions compiled to native code, the steps each tier ran, and why the busiest blocks stayed on the reference tier, and ends with a line that divides the wall time of the command among loading the image, proving and searching for lifted forms, tier 0, tier 1, regions, garbage collection, host services, and the rest ([controls](NVM#controls)); a run with a graph device or more than one parallel worker omits that line. These lines are diagnostics: they depend on timing, unlike `--stats`. `cmake --install build --prefix <dir>` installs the `neura` executable, which contains both the compiler and the runner, and the standard library below `<dir>/share/neura/stdlib`, where the installed tool finds it without any `-I` option.

Console input and output and `Std.Program.Process.exit` are always available. Every other host service is granted by default: with no capability option, `run`, `test`, and the policy that `build --native` records grant files, the network, environment variables, clocks, entropy, and native code. A `--deny-*` option turns a group off. A denied service still binds, and every call returns the `CapabilityDenied` error (error 14), exactly as if the program had no access:

| Group | Services | Turn it off | Grant it again |
| --- | --- | --- | --- |
| Files | Files and directories ([`Std.Fs`](Stdlib-Design#112-stdfs)) | `--deny-fs` | `--allow-fs`, or `--allow-fs=prefix,...` for only the paths at or below the listed prefixes, after resolving symbolic links |
| Network | TCP connections and listeners ([`Std.Net`](Stdlib-Design#114-stdnet)) | `--deny-net` | `--allow-net` |
| Environment | Environment variables (`Std.Program.Environment`) | `--deny-env` | `--allow-env` |
| Clock | The wall and monotonic clocks and sleeping (`Std.Time.SystemClock`) | `--deny-clock` | `--allow-clock` |
| Entropy | Operating-system entropy (`Std.Random.Random.from_entropy`) | `--deny-entropy` | `--allow-entropy` |
| Native | Native libraries, memory, and calls ([`Std.DynamicForeign`](ForeignInterop)); native code runs outside Neura's memory-safety and determinism guarantees. The machine code the runner generates for tier 2 regions is not covered: `--deny-native` leaves it on, and `--jit=off` turns it off | `--deny-native` | `--allow-native` |
| Every group above | | `--deny-all` | |

Capability options apply in command-line order, and a later option for a group overrides an earlier one. `--deny-all --allow-net --allow-fs=data` grants exactly the network and the files at or below `data`; `--allow-net --deny-all` grants nothing. The `--allow-*` options matter after a `--deny-*` option, and they keep their old meaning for files: `--allow-fs=prefix,...` narrows the default, so only the listed prefixes are reachable. Several lists accumulate (`--allow-fs=a --allow-fs=b` reaches `a` and `b`) until `--deny-fs` or `--deny-all` clears them. A plain `--allow-fs` grants every path again and replaces any list before it, so `--allow-fs=a --allow-fs` reaches every path. A list after it narrows it, so `--allow-fs --allow-fs=a` reaches only `a`.

> **Security.** Nothing is denied by default. An image runs with everything the user who starts it can do: it can read, write, and delete their files, open connections, read their environment, and load native libraries that run outside Neura's guarantees. To run an image you do not trust, pass `--deny-all` and add only the `--allow-*` options it needs, for example `neura run --deny-all --allow-fs=data image.nvr`. The same holds for `neura test` and for the policy of an executable built with `neura build --native`.

`neura run --record trace.txt` writes every host request and response, and how the run ended, to a text trace. `neura run --replay trace.txt` runs the image answering each request from the trace instead of calling any service, so it needs no capability, and reproduces the console output of the recorded run. A replay whose requests differ from the trace stops with status 1 and names the first difference. The trace of a program with tasks also records which task issued each request, when each overlapping request was answered, and the clock readings that expired a timer or a deadline, so its replay reproduces the schedule; such a trace carries the quantum and the task limits of the recorded run, and a replay uses them in place of its own options. [NVM](NVM#determinism-limits-and-observability) describes the trace format.

[Neuron conversion and execution](Backend) lists the supported operations and the build diagnostics for unsupported ones. The status of each example is in [Feature status](Status).

### Run programs with tasks

A program that uses [tasks](Concurrency) builds like any other, into an NVR 1.5 image (1.6 when it also calls a kernel), and `run` and `test` accept these options for it. Each limit ends the run with status 137 at the operation that would exceed it.

| Option | Commands | Meaning |
| --- | --- | --- |
| `--max-tasks N` | `run`, `test` | Live tasks. Default 65,536. |
| `--max-queued-messages N` | `run`, `test` | Values buffered in all channels. Default 1,048,576. |
| `--max-pending-requests N` | `run`, `test` | Overlapping host requests issued and not yet answered. Default 4,096. |
| `--task-quantum N` | `run`, `test` | Polls a task may count before its turn ends. Default 1,024. A setting, not a limit: it changes how tasks interleave, and so the order of their output. |
| `--parallel-workers N` | `run`, `test` | Threads that evaluate the items of `Parallel.map`, from 1 to 1,024. Default 1. More workers change only wall time and peak process memory. |
| `--task-dump`, `--task-dump=text`, `--task-dump=json` | `run`, `test` | Print every task, with its stack and what it waits for, before the diagnostic of a trap. Sending `SIGQUIT` to a run prints a dump at any time and the run continues. |
| `--trace-events <file>` | `run` | Write one JSON object per scheduler event, one per line. `--trace-wall-time` adds wall-clock times, which differ between runs. |
| `--virtual-time` | `run`, `test` | Timers, deadlines, and the clock follow a clock that starts at zero and moves only when every task is parked, so a timeout takes no real time. It cannot be combined with `--replay` or `--each`. |

For an image with tasks, `--stats` adds the peak numbers of live tasks, queued messages, and pending requests, and `run --profile` adds, for each spawn site, the tasks started there, the steps they executed, and how long they waited for a turn, on a channel or timer, and on host I/O. [Concurrency, section 14](Concurrency#14-tooling) describes the dump, the events, and virtual time. A program that uses tasks cannot use `Std.DynamicForeign` or a C import ([Concurrency, section 16](Concurrency#16-limits)).

### Package a native executable

```sh
neura build --native --deny-all --allow-fs=data --max-steps 1000000 hello.nra -o hello
./hello --help
neura inspect hello -o hello.nvr
```

`build --native` packages sources, a `.nir`, or a validated `.nvr` with a CPU launcher. The image and launcher bytes are preserved. Every argument to the executable reaches the program, including `--help` and `--`. The build records the capability policy, the three run limits above, and the task settings: `--max-tasks`, `--task-quantum`, `--max-queued-messages`, `--max-pending-requests`, and `--parallel-workers` are accepted with `--native` and recorded, each with the default of `neura run` when it is not given. With no capability option the policy grants every group, so the executable can do whatever the user who runs it can; build with `--deny-*` options (the example denies everything but the files below `data`) to give it less. Relative file-system prefixes are resolved from the executable's working directory.

Set `NEURA_RUN_OPTIONS` to space- or tab-separated options to lower a recorded limit (`--max-steps`, `--max-frames`, `--max-heap-cells`, `--max-tasks`, `--max-queued-messages`, `--max-pending-requests`) or the recorded `--parallel-workers` count, to deny a group with `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, or `--deny-all`, or to select `--stats`, `--profile`, `--tier=0`, `--tier=1`, `--jit=off|on|auto`, `--tier-stats`, `--task-dump[=text|json]`, `--trace-events <file>`, `--trace-wall-time`, `--record <file>`, or `--replay <file>`. It also accepts `--no-gpu` and `--gpu=off`, which name the CPU evaluation the launcher always uses. `--task-quantum` is rejected, because it changes the interleaving and so the output order of tasks, and `--virtual-time` because it changes what the program reads from the clock; a native executable uses the quantum its policy records. The variable has no quoting and cannot widen the build policy: every `--allow-*` option is rejected. A build with `--sealed` ignores it, `--deny-*` options included. Without `--sealed`, an executable trusts whoever sets the variable: `--record` and `--trace-events` can create or replace any file the process may write, and `--replay` answers host requests from a file of their choosing.

`--static` selects the fully static flavor, which excludes native interop, so the policy it records grants every group except native code and `--static --allow-native` is an error. Set the CMake option `NEURA_STATIC_LAUNCHER` to `ON` to require that flavor or `OFF` to disable it. `AUTO` builds it when a static C library is available. Linux launchers install below `lib/neura/launchers/<triple>/`. Use `--target <triple>` to choose an installed ELF target or `--launcher <file>` to supply the launcher. Linux x86-64 is the verified target.

`inspect` reports the launcher, the recorded policy with its limits and task settings, and the image. A file that is not a native executable is reported as `not a native executable (no NEURAEXE footer)` with status 1. Its `-o` option extracts the original image bytes. `run`, `disassemble`, and `decompile` also accept a packaged executable. `neura run` uses the supplied command-line policy. Run the file directly to apply the recorded policy. [Native executable format and contract](Standalone)

### Choose graph execution

`run` and `test` choose graph placement automatically by default. With `--gpu=auto`, Neura opens Vulkan only when the observed workload can repay setup and calibration. It sends a batch only when the calibrated device is predicted to beat the CPU by the configured margin. `--gpu=always` sends eligible graphs to Vulkan, with CPU fallback when no device is usable or a request is declined. `--gpu=off` and `--no-gpu` select the CPU. `--require-gpu` selects `always` and turns an unavailable or failed device into an error. `--gpu-software` also accepts software Vulkan devices. `--gpu-verify` checks every device result against the CPU. When a device is in use, `test` and `run --each` advance their members as a fleet, preserving serial output and host effect order.

The CMake setting `NEURA_GPU=AUTO` builds the GPU backend when the Vulkan headers and `glslangValidator` are available on a supported platform. `ON` requires those tools and `OFF` builds the CPU runner. The GPU backend opens the Vulkan loader only when a run requests a device.

`NEURA_GPU` accepts `auto`, `always`, and `off` for `run` and `test`. An unset or empty value uses the default. An explicit mode option, `--no-gpu`, or `--require-gpu` takes precedence. Other values are usage errors. The effective off mode also applies the option checks for software devices and verification.

With `--stats`, a final `gpu:` line reports the selected mode, whether a device was built or used, and the reason when no device was used. A device report includes the device name, eligible graphs, evaluated and declined jobs, escapes, failures, and dispatches. Auto mode also reports the jobs kept on the CPU, the timing correction, and any rejection reason. An opened device that is later rejected ends the line with `rejected (<reason>)`. A device that kept declining most of its jobs is retired, and the line ends with `retired (the device stopped gaining)`; its counters are those accumulated before it was retired, because a retired device does no accounting. Retirement is not a failure and does not affect `--require-gpu`.

### Run an image over argument lists

```sh
neura run --each lists.txt image.nvr -- common arguments
```

`--each` loads and binds the image once. Each member runs with fresh state, the same limits and capabilities, and the common arguments followed by that line’s arguments. Host effects and member reports follow file order. Members share standard input, and the runner closes any files and sockets a member leaves open before it serves the next member.

The file is read and validated in full before a member runs. LF separates lines. A final LF ends the preceding line. An empty line gives a member no extra arguments, and a line beginning with `#` is a comment. A line ending in CR is invalid, including a comment line. Spaces and tabs separate arguments and are ignored at either end. Bare arguments cannot contain quotes or backslashes. Double quotes allow spaces, tabs, and empty arguments. Inside quotes, the escapes are `\"`, `\\`, `\n`, `\r`, `\t`, and `\xHH`, where `HH` is two hexadecimal digits. A backslash without a complete escape is invalid. Each resulting argument must be UTF-8. The file must yield between 1 and 1,048,576 argument lists and fit in 64 MiB.

A member with status 0 has no report. For other outcomes, standard error shows `<image>: <lists>:<line>: exit status <status>` or `<image>: <lists>:<line>: trap: <diagnostic>`. The image path is canonical and the list path retains its command-line spelling. Later members still run. The command returns the first nonzero status in file order, including the status assigned to a trap, or 0 if all members succeed. `--stats` reports the number of members, exact totals for steps and host requests, the largest member heap peak, and the costs and byte size of the loaded image, followed by the `gpu:` line. `--each` applies only to `run` and may appear once. It conflicts with `--record`, `--replay`, `--profile`, and `--virtual-time`, checked in that order when more than one is present.

## C imports

`check`, `emit-ir`, `build`, and `test` read the headers of [`import:c(...)`](ForeignInterop#c-imports) with libclang. The tool loads libclang when the first C import is compiled, from `--libclang <file>`, else the `NEURA_LIBCLANG` environment variable, else the platform's usual names, such as `libclang-19.so.1` and `/usr/lib/llvm-19/lib`. Builds without C imports never load it. libclang also needs its builtin headers, such as `stddef.h` and `stdarg.h`, which C library headers include. The tool looks for them in `/usr/lib/llvm-<major>/lib/clang/<major>`, `/usr/lib/clang/<major>`, `/usr/local/lib/clang/<major>`, and `clang/<major>` beside the loaded library, where `<major>` is libclang's major version, and names the first that exists as libclang's resource directory. On macOS, where Apple's toolchains version the directory themselves, it falls back to the highest `clang/<version>` beside the loaded library that has `include/stddef.h`; the usual names include `libclang.dylib` in the Command Line Tools and Xcode toolchains and in Homebrew's `llvm`. The C library headers are in the macOS SDK, which the tool finds as [ForeignInterop](ForeignInterop#c-imports) describes and passes to libclang as `-isysroot`. When libclang cannot find them, a header that includes `<stddef.h>` fails with `'stddef.h' file not found`. `--c-include <dir>` adds a header search directory, and `--c-define <name[=value]>` defines a macro while reading headers.

`emit-ir`, `build`, and `test` link the libraries of C imports with the system C compiler: `--cc <compiler>`, else `$CC`, else `cc`. The result is a shared object on Linux (`-shared`, each archive inside `--whole-archive`) and a Mach-O dynamic library on macOS (`-dynamiclib`, each archive loaded with `-force_load`, with a fixed install name so that builds stay reproducible). `--c-link-arg <arg>` passes an argument to that link. `check` reads headers but does not link.

`neura c-bindings [--linked] [c-options] <header.h>` prints the module that a C import of the header generates. `--linked` generates it as an import that names libraries does.

## Test

```sh
build/neura test -I examples/guide examples/testing/math-tests.nra
```

`test` builds the listed files, runs every test they declare in a fresh execution, prints `PASS` or `FAIL` with the failure and its location for each, and a summary line. It exits with status 0 only when every test passes. It accepts the capability options of `run`, `--stats`, `--tier=0|1|2`, `--jit=off|on|auto`, `--tier-stats`, the run limits, and the task options of [Run programs with tasks](#run-programs-with-tasks) other than `--trace-events`. The tests of one run share the lifted blocks and regions of their image. [Testing](Testing) defines tests and the `Std.Test` assertions.

These options test concurrent code ([Testing](Testing#tests-of-concurrent-code)):

| Option | Meaning |
| --- | --- |
| `--virtual-time` | Each test runs on its own virtual clock, which starts at zero. |
| `--schedules N` | Run each test under up to `N` legal task schedules. `--schedule-seed S` chooses the schedules (default 0), and `--save-schedule <file>` writes the first failing one. |
| `--schedule <file>` | Run the one test a saved schedule names, under exactly that schedule. It cannot be combined with the three options of the row above. |

## Inspect and decompile images

```sh
build/neura disassemble hello.nvr -o hello.nvra
build/neura assemble hello.nvra -o copy.nvr
build/neura decompile hello.nvr
```

`disassemble` writes the lossless text form of an image: every table, graph, neuron, weight, block, exit, import, the source map, and any optional section, in order. `assemble` turns that text back into an image, validates it with the same loader as `run`, and writes it; assembling an unedited disassembly reproduces the original bytes. Edited assembly is how tests and tools construct images by hand.

`decompile` writes a typed reconstruction of the image: functions, blocks, state operations, calls, host effects, and the operations each block's graph computes, with every block labeled by how its meaning was established. `--no-provenance` omits names and source locations from the optional NAME section and source map. [Decompiler](Decompiler) specifies both forms and the evidence behind them.

## Version, install, and update

`neura --version` and `neura version` print one line to standard output, `neura <version> (<target>, commit <commit>)`, and exit with status 0. A build from a checkout reports a `-dev` version and commit `unknown`. `neura help` and `neura --help` print the usage text and exit with status 0; `neura` with no arguments prints it to standard error and exits with status 1.

Three commands manage the installed tool; [Install](Install) describes the install layout, the verification an update performs, and what each command removes:

| Command | Does |
| --- | --- |
| `neura doctor` | Prints `ok`, `warn`, or `FAIL` for each check of the install and its optional tools (the standard library, native launchers, libclang, a C compiler, the GPU backend, `curl`, `tar`, `gh`). Exits with status 1 if a check failed. Uses no network. |
| `neura update [--check] [--source <https-url-or-directory>] [--require-attestation]` | Replaces an archive install with a newer release after checking the archive's size and SHA-256 and, where `gh` is available, its build provenance. `--check` reports without changing anything. |
| `neura uninstall [--dry-run] [--yes]` | Removes an archive install and nothing else. |

`update` and `uninstall` apply only to an install made by the release archive's `install.sh`. A source build, a copy, or a package manager's install exits with status 3.

## Diagnostics

Diagnostics go to standard error, and a command that reports an error exits with status 1. Warnings are printed before the errors, or at the end of a command that succeeds, sorted by file and byte offset. A diagnostic has a primary location, a message, and optionally a code, labels at other locations (which may be in other files), notes, and suggested edits. A line is a line of the source as the lexer counts lines (LF, CRLF, or CR ends one), and a column counts Unicode code points from 1.

How many errors a run reports depends on the stage that finds them:

- **Syntax.** The lexer and the parser recover and go on. The lexer skips an invalid character, a malformed number, or an unterminated string and reads the next token. The parser skips the rest of a statement or member that fails to parse: to the `;` that ends it at the same depth, to the closing delimiter of the enclosing block or member list, or to a keyword that begins the next statement. They report up to `--max-errors N` errors (20 by default; `check`, `emit-ir`, `build`, and `test` accept the option), in source order, and any syntax error stops compilation before NeuraIR lowering.
- **Everything after parsing.** Name resolution, type checking, the later NeuraIR checks, and circuit conversion stop at the first error. Lowering resolves declarations on demand and abandons speculative work, such as ranking an overload, by catching errors, so continuing after an error would report errors that the first one caused.

Every error has a code: `E` and four digits, whose hundreds digit names the area. `E01xx` is the lexer, `E02xx` the parser, `E03xx` names, `E04xx` types and bindings, `E05xx` classes and lifecycle, `E06xx` concurrency, `E07xx` modules and imports, `E08xx` the backend, and `E09xx` [tags](Tags). A warning has a code, `W` and four digits, and a name. Errors that report the same mistake share a code, and `E0899` is the one code for errors that only a compiler defect can cause. A code is never reused for another error. [Diagnostic codes](Diagnostics) lists them. `neura explain <CODE>` (or `neura --explain <CODE>`) prints a code's page: the rule, a program that breaks it, and the program fixed. `neura explain --list` prints each code and its title, with the name of each warning; `neura explain <name>` also takes a warning's name. `neura tags` prints the catalogue of [tags](Tags), and `neura tags <Name>` one tag's signature, targets, status, and meaning. The pages are part of the `neura` binary, so `explain` reads no files. `explain` exits with status 0; a code that is not registered is an error that names the closest registered code.

`--error-format` chooses the form. Every command accepts it.

| Form | Output |
| --- | --- |
| `human` (default) | The message, its location, and the source lines with the primary span underlined by `^` and other labels by `-`, then notes and suggested edits. |
| `short` | One line per diagnostic: `file:line:column: error: message`, or `file:line:column: error[E0201]: message` when the diagnostic has a code, and `warning` in place of `error` for a warning. |
| `json` | One JSON object per line. |

The `short` form is the stable machine-readable form: tools and tests that match diagnostics use it, and its layout changes only with a release note. The `human` form may change in any release. This is a missing semicolon in both forms:

```text
error[E0201]: expected ';'
 --> /work/limits.nra:2:15
  |
2 | let limit = 10
  |               ^ expected ';' here
3 | let half = limit / 2;
  | --- found 'let'
  |
help: add the missing ';'
  |
2 | let limit = 10;
  |               +

For more information about this error, try `neura explain E0201`.
```

```text
/work/limits.nra:2:15: error[E0201]: expected ';'
```

The `human` form ends, after errors with codes, with one line that points to `neura explain`: for one code as above, and for several, `Some errors have detailed explanations:` and the codes in order. The `short` and `json` forms have no such line.

In the `human` form a tab shows as four spaces, a column is as wide as the character's East Asian width (two for wide characters, none for combining marks, from a table in the tool rather than the system locale), a span over several lines is underlined on its first line and followed by `...`, and a line wider than the terminal is cut around the span with `...`. A suggested edit shows the edited line with `+` under inserted text and `~` under replaced text, or the original line with `-` under removed text. A diagnostic in the module that a [C import](ForeignInterop#c-imports) generates has a note that names the header.

Each `json` object has `code` (a string, or `null`), `severity` (`error` or `warning`), `message`, `spans`, `notes`, `help`, `suggestions`, `args`, and `rendered`. Each span has `file`, `offset` and `length` in bytes, `line`, `column`, `primary`, and `label` (a string, or `null`). Each suggestion has `message`, `applicability` (`MachineApplicable`, `MaybeIncorrect`, `HasPlaceholders`, or `Unspecified`), and `edits`, each a span and its `replacement`. `args` holds facts about the error as strings, such as the `kind`, `name`, and `owner` of a name that did not resolve. `rendered` is the `human` form without color.

`--color=auto|always|never` colors the `human` form. `auto`, the default, colors it when standard error is a terminal, `NO_COLOR` is unset or empty, and `TERM` is not `dumb`; `always` and `never` ignore all three. The width that long lines are cut to is the terminal's, else the `COLUMNS` environment variable, else 100.

`check`, `emit-ir`, `build`, and `test` accept the warning options:

| Option | Meaning |
| --- | --- |
| `-W<name>`, `-Wno-<name>` | Enable or disable the warning `<name>`. Warnings are enabled by default; the last option for a name wins. A name that is not a warning's is an error. |
| `-Werror` | Report every warning as an error; the command then exits with status 1. |
| `--warn-library` | Also report warnings in the standard library, which are otherwise hidden. A warning about a use is reported where the use is, so a program that uses a deprecated standard library declaration gets the warning without this option. |

The warnings come from [tags](Tags) and [accessors](Types#accessors):

| Code | Name | Warning | Reported by |
| --- | --- | --- | --- |
| `W0001` | `deprecated` | A use of a `#Deprecated` declaration, or an override of a deprecated method | `check`, `emit-ir`, `build`, `test` |
| `W0002` | `experimental` | A use of an `#Experimental` declaration | `check`, `emit-ir`, `build`, `test` |
| `W0003` | `unused-result` | An expression statement that discards the result of a `#NoDiscard` function or type, or a `new` of a `#NoDiscard` class | `check`, `emit-ir`, `build`, `test` |
| `W0004` | `inline` | An `#Inline(always: true)` function that the `inline-calls` pass cannot inline | `emit-ir`, `build`, and `test` when they optimize |
| `W0005` | `setter-never-stores` | A setter that never assigns its field | `check`, `emit-ir`, `build`, `test` |

`#Allow(<name or code>)` on a declaration silences a warning inside it, and `#Deny(...)` makes it an error there; neither is affected by `-Wno-<name>`, and `-Werror` applies to the warnings that remain.

`neura check --fix <file.nra>...` applies the suggestions marked `MachineApplicable` to the source files, saving each file in place. It checks the program, applies the edits of what it reports from the end of each file to the start, and repeats until no edit applies, then reports `neura: check --fix applied <n> edits to <m> files` on standard error and checks the result as `check` does. It never edits a standard library file or a module generated from a C header, and says so when a suggestion would. `--fix` cannot be combined with `--task-safety`.

## Exit status

Each status below is the tool's own. The exit status of a program that `neura run` executes, and of a native executable, is the program's: the `ExitCode` that `main` returns, or the status of its trap class.

| Status | Meaning | Defined in |
| --- | --- | --- |
| 0 | The command succeeded. For `test`, every test passed. For `update`, the install is up to date or was updated. | [Test](#test), [Install](Install) |
| 1 | A usage error, an unknown command or option, a diagnostic in the source, an image that fails to load or bind, a failed test, or a failed `update` or `uninstall` | [Errors](Errors) |
| 3 | `update` or `uninstall` on an install that `neura update` does not manage | [Install](Install) |
| 4 | `update --check` found a newer version | [Install](Install) |
| 128 and above from `run` | The program's trap class, such as 136 for an arithmetic trap or 134 for a message trap, or the program's own `ExitCode` | [Errors](Errors), [Program entry](Modules#program-entry) |

## Execution contract

The [NVM specification](NVM#toolchain-and-distribution) defines how `neura build` produces an executable `.nvr` image from source or `.nir` and how `neura run` executes that image. Package configuration is not part of this release.

Diagnostics should identify the source file, line and column, the offending span, a clear explanation and, where possible, a correction. For example, passing a `string` to a function expecting `int32` should report both types and the argument location. [Diagnostics](#diagnostics) defines their formats, columns, and warning options.

A formatter should produce stable, idempotent output consistent with [Coding style](CodingStyle). Editor integration should eventually expose diagnostics, navigation and completion; debugger support should include source locations, stepping and local variables. These are tooling goals, not promises of first-release support.
