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

With a single-configuration build, the executable is `build/neura`, with the tools it starts beside it ([Tools and the `neura` dispatcher](#tools-and-the-neura-dispatcher)). For multi-configuration builds, use the executables in the selected configuration directory and pass that configuration to CTest with `-C`.

```sh
build/neura check examples/guide/core.nra
build/neura emit-ir examples/types/polymorphism.nra
build/neura check examples/modules/multi-file/main.nra
build/neura check -I examples/guide examples/testing/math-tests.nra
```

`Std` imports resolve in the trusted standard library: below `share/neura/stdlib` of an installed tool, or the checkout's `stdlib` directory for a build directory directly inside the checkout ([Modules](Modules#standard-library-imports)).

Both commands accept multiple input paths and resolve imports across the source roots. `check` validates the source and succeeds silently on valid input. It does not run the optimizer. `emit-ir` writes a typed NeuraIR listing with resolved symbol references and explicit operations for control flow, state, and calls. The listing is for inspection. A source diagnostic reports the path, line, and column and returns a nonzero exit status.

`check --task-safety` validates the linked program, including storage checks and class hook elaboration after specialization, before producing a report. For each function it reports whether a spawned task may call it, the reason when it may not, and whether it can serve as a parallel-map worker. Functions are grouped by source file, with one entry per specialization of a generic function. A completed report ends with totals and returns status 0. The option applies only to `check`. [Concurrency](Concurrency#52-task-safe-calls)

`emit-ir` prints optimized IR by default. Pass `--no-optimize` to print the IR exactly as lowered. To see what the optimizer changed, compare the two listings:

```sh
build/neura emit-ir --no-optimize tests/ir-optimization/fixtures/folding.nra > before.ir
build/neura emit-ir tests/ir-optimization/fixtures/folding.nra > after.ir
diff before.ir after.ir
```

The two listings for this fixture share a symbol table, so the diff shows rewritten instructions. For programs that take primitive references or specialize generics, the optimized listing can also show new or rewritten symbols from those compilation stages, and inlining adds a local named `$inline.<callee>.<name>` to the caller for each inlined parameter and local. The listing is separate from the versioned `.nir` file described below, which holds NeuraIR before those transformations. See [NeuraIR optimization](IrOptimization) for the passes and their preconditions.

## Tools and the `neura` dispatcher

`neura` starts a separate tool for each command, from the directory of its own executable ([design](https://github.com/Kayyo321/neura/blob/main/docs/design/Products030.md)). Each tool can also be run by itself, with the same options, output, and exit status:

| `neura` command | Tool | Component |
| --- | --- | --- |
| `build`, `check`, `emit-ir` | `neurac` | SDK |
| `run`, `serve` | `neura-run` | core |
| `test` | `neura-test` | SDK |
| `inspect`, `inspect-image` | `neura-inspect` | core |
| `docs` | `neura-doc` | SDK |
| `c-bindings` | `neura-cbind` | SDK |
| `decompile`, `assemble`, `disassemble` | `neura-decompile`, `neura-asm`, `neura-disasm` | SDK |
| `explain`, `tags` | `neura-explain`, `neura-tags` | SDK |
| `debug`, `lsp` and `rename`, `fmt` | `neura-debug`, `neura-lsp`, `neura-fmt` | SDK |
| `new`, `dev`, `package` | `neura-new`, `neura-dev`, `neura-package` | SDK |
| `doctor`, `update`, `uninstall` | `neura-doctor`, `neura-update`, `neura-uninstall` | core |
| `--version`, `version`, `help`, `--help`, `capabilities --json` | `neura` itself | core |

A tool's first argument may name one of its commands; otherwise it runs its first. `neurac app.nra -o app.nvr` is `neura build app.nra -o app.nvr`, `neurac check app.nra` is `neura check app.nra`, and `neura-run app.nvr -- one two` is `neura run app.nvr -- one two`. A file named exactly like a command follows the command: `neurac build check`. Given alone, `--help` prints a tool's commands and `--version` prints the line of `neura --version`. Every tool reports errors with the prefix `neura:`.

`neura` looks for a tool only in the directory of its own executable, after resolving symbolic links, and never in the working directory or on `PATH`, so it always runs the tools of its own installation. It starts the tool in its place, with the same streams, environment, and working directory, and the tool's exit status is the command's. Before it starts a tool, it reports a missing one and exits with status 1:

```text
neura: error: 'neura build' needs neurac, which is not in /opt/neura/bin; it is part of the Neura-sdk component. Install the SDK, or reinstall Neura
```

In an installed generation the component manifests say which components are there, so a runtime installation asked for an SDK command answers `neura: "build" needs Neura-sdk, which is not installed here`. `neura` reads the manifests only when a tool is missing, so a command pays nothing for them.

A tool started by a `neura` of another build refuses to run, so a directory of tools from different builds cannot mix them silently. `neura runtime` is `neura-run runtime` and belongs to the runtime service component, `neurad` ([Run through the runtime service](#run-through-the-runtime-service)); without `neurad` next to `neura` it reports that component missing, except that `neura runtime start` and `enable` on an installation that a 0.2 updater produced and that is not migrated exit with status 5 and the instruction to run `neura update --migrate-install`, as `run --runtime=auto|required` does there. `capabilities --json` reports a feature whose tool is missing as `false`, and has a `service` member that says whether `neurad` is installed.

The tools share their code through two libraries in `lib/neura`, named with the full version: `libneura-core-<version>` (the image format, the runner and every execution tier) and `libneura-sdk-<version>` (the compiler, the decompiler, and the editor tools), which needs the core library. Each tool finds them relative to its own executable, so an installation can be moved as a whole, and the build directory holds them in its own `lib/neura`. The tools form three install components. The **core** (`neura`, `neura-run`, `neura-inspect`, `neura-doctor`, `neura-update`, `neura-uninstall`, and `libneura-core`) runs images and loads no compiler code; on Linux and macOS it also holds `neura-server-host` ([Server host](Server-Host)), the core tool that `neura serve` starts from beside `neura-run`; it serves no `neura` command of its own, and an installation links it like the other tools. The **SDK** adds the compiler and the other tools, `libneura-sdk`, the standard library, and the native launchers. The **service** is `neurad`, the resident runtime service, which a build has only when its target exists. `cmake --install build --component core --prefix <dir>` installs only the core; without `--component`, the core and the SDK are installed, and `--component service` adds `neurad`. Configuring with `-DNEURA_SDK=OFF` builds only the core, without the libclang submodule. A release installs the products Neura-sdk (`core` and `sdk`) and Neura-runtime (`core` and `service`), alone or together, and links every tool in one bin directory ([Install](Install#layout)). Each tool of an installed generation holds a lease on it while it runs, so an update leaves a running tool's generation in place ([generations in use](Install#generations-in-use)).

## Build and run executables

```sh
build/neura emit-ir examples/hello-world/hello-world.nra -o hello.nir
build/neura build hello.nir -o hello.nvr
build/neura build examples/hello-world/hello-world.nra -o hello.nvr
build/neura run hello.nvr -- arg1 arg2
```

`emit-ir -o` writes the program as lowered, before optimization, to a versioned [`.nir` file](NirFormat). `emit-ir` also accepts one `.nir` file in place of sources and prints its listing. `build` takes sources or one `.nir` file. It verifies and optimizes the IR, converts it to neuron graphs, and writes the `.nvr` image only when every step succeeds. Both routes produce the same bytes for the same program and options, and repeating a build reproduces them. `--no-optimize` converts the verified IR without optimizing it. `--no-source-map` omits the optional source map, which otherwise labels blocks with paths relative to the sources' common directory. `--no-lift-hints` omits the optional `LIFT` section, which records the typed operations of each block for the runner's faster tier ([NVR](NVR#lift-hints)); it changes nothing a program does.

Sources can [import prebuilt modules](Modules#prebuilt-neurair-modules) with `import:"library.nir".Module`. `check`, `emit-ir`, `build`, and `test` resolve and link those imports during source compilation.

`neura docs <file.nra>...` exports declaration documentation and source ranges as JSON for IDE tooling. Pass `-o <file.json>` to save the index. `neura docs --doxygen <file.nra>` emits a declaration view for Doxygen's input filter. The index lists a [record](Types#records) as `record` with its components, followed by its generated `init`, and a [derived codec](Types#deriving-a-json-codec) with its generator and mappings as written, followed by its generated `to_json`, `encode`, `from_json` and `decode`; a field the mappings do not pin keeps its declared name on the wire. The generated members have no documentation of their own. A static function, including an interface's [static requirement](Polymorphism#static-interface-requirements), is listed as `static name = func...`. [Documentation comments](CoreLanguage#documentation-comments)

`run` loads and validates an image, binds its host imports to the runner's services ([NVM](NVM#host-services)), and passes the arguments after `--` to the entry function as UTF-8 text. The image needs no source files. The exit status is the `ExitCode` that `main` returns, or the status given to `Std.Program.Process.exit`. A runtime trap prints its reason and source location, when the image has a source map, to standard error and exits with the status of its class, such as 136 for an arithmetic trap or 134 for a message trap ([Errors](Errors)). Load, bind, and usage errors exit with status 1. `--max-steps`, `--max-frames`, and `--max-heap-cells` bound a run, and for an image of NVR 1.7 that uses storage regions `--max-stack-cells` (16,777,216 by default) bounds the cells of their entries; exceeding it traps `stack limit exceeded` ([regions](NVR#storage-regions-17)). A program with tasks has further limits and options ([Run programs with tasks](#run-programs-with-tasks)). `--stats` prints the run's step count, peak heap cells, `peak stack cells` (only for an image that uses regions, which the compiler does not yet emit, so the output of every other image is unchanged), host requests, graph, neuron, and edge counts, and image bytes to standard error. `run` evaluates a block whose graph is proven to compute typed operations as native operations ([execution tiers](NVM#execution-tiers)) and every other block by reference; the result, the steps, the traps, the output, and a recorded trace are the same either way. It also runs regions of proven blocks of one function together, without returning to its dispatcher between them, with the same results. `--tier=0` evaluates every block by reference, `--tier=1` allows lifted blocks, and `--tier=2`, the default, allows regions too; `NEURA_TIER=0` or `NEURA_TIER=1` in the environment has the effect of that option for every run. At tier 2 a region runs as native code that the runner generates for it, where that is enabled ([native compilation](NVM#native-compilation)), with the same results again. `--jit=auto`, the default, generates code on x86-64 Linux and interprets regions elsewhere; `--jit=on` generates code wherever the runner has a code generator (x86-64 and AArch64, on Linux and macOS; experimental where `auto` does not enable it) and fails the run at its start where it cannot; `--jit=off` interprets every region. `NEURA_JIT=off`, `on`, or `auto` in the environment applies to every run: `off` from the option or the variable wins, then `on`. Generating code for regions is not the native capability: `--deny-native` does not turn it off. `--tier-stats` prints, after the run, the blocks lifted, the regions formed and how they ended, the regions compiled to native code, the steps each tier ran, and why the busiest blocks stayed on the reference tier, and ends with a line that divides the wall time of the command among loading the image, proving and searching for lifted forms, tier 0, tier 1, regions, garbage collection, host services, and the rest ([controls](NVM#controls)); a run with a graph device or more than one parallel worker omits that line. These lines are diagnostics: they depend on timing, unlike `--stats`. `cmake --install build --prefix <dir>` installs `neura` and its tools in `<dir>/bin` and the standard library below `<dir>/share/neura/stdlib`, where the installed tools find it without any `-I` option. `neura run` is `neura-run`, which runs an image with no compiler code, from the shared library in `<dir>/lib/neura` ([Tools and the `neura` dispatcher](#tools-and-the-neura-dispatcher)).

Console input and output and `Std.Program.Process.exit` are always available. Every other host service is granted by default: with no capability option, `run`, `test`, and the policy that `build --native` records grant files, the network, environment variables, clocks, entropy, and native code. A `--deny-*` option turns a group off. A denied service still binds, and every call returns the `CapabilityDenied` error (error 14), exactly as if the program had no access:

| Group | Services | Turn it off | Grant it again |
| --- | --- | --- | --- |
| Files | Files and directories ([`Std.Fs`](Stdlib-Design#112-stdfs)) | `--deny-fs` | `--allow-fs`, or `--allow-fs=prefix,...` for only the paths at or below the listed prefixes, after resolving symbolic links |
| Network | TCP connections and listeners ([`Std.Net`](Stdlib-Design#114-stdnet)) | `--deny-net` | `--allow-net` |
| Environment | Environment variables (`Std.Program.Environment`) | `--deny-env` | `--allow-env` |
| Clock | The wall and monotonic clocks and sleeping (`Std.Time.SystemClock`) | `--deny-clock` | `--allow-clock` |
| Entropy | Operating-system entropy (`Std.Random.Random.from_entropy`) | `--deny-entropy` | `--allow-entropy` |
| Native | Native libraries, memory, and calls ([`Std.DynamicForeign`](ForeignInterop)); native code runs outside Neura's memory-safety and determinism guarantees. The machine code the runner generates for tier 2 regions is not covered: `--deny-native` leaves it on, and `--jit=off` turns it off | `--deny-native` | `--allow-native` |
| SQL | The pools of [`Std.Data.Sql`](Sql), served in process; denied unless a pool is named | `--deny-all` | `--sql-pool <name>=<url>`, with `<name>.migrations=<directory>` and `<name>.password_env=<variable>` |
| Every group above | | `--deny-all` | |

Capability options apply in command-line order, and a later option for a group overrides an earlier one. `--deny-all --allow-net --allow-fs=data` grants exactly the network and the files at or below `data`; `--allow-net --deny-all` grants nothing. The `--allow-*` options matter after a `--deny-*` option, and they keep their old meaning for files: `--allow-fs=prefix,...` narrows the default, so only the listed prefixes are reachable. Several lists accumulate (`--allow-fs=a --allow-fs=b` reaches `a` and `b`) until `--deny-fs` or `--deny-all` clears them. A plain `--allow-fs` grants every path again and replaces any list before it, so `--allow-fs=a --allow-fs` reaches every path. A list after it narrows it, so `--allow-fs --allow-fs=a` reaches only `a`.

> **Security.** Nothing is denied by default. An image runs with everything the user who starts it can do: it can read, write, and delete their files, open connections, read their environment, and load native libraries that run outside Neura's guarantees. To run an image you do not trust, pass `--deny-all` and add only the `--allow-*` options it needs, for example `neura run --deny-all --allow-fs=data image.nvr`. The same holds for `neura test` and for the policy of an executable built with `neura build --native`.

`neura serve app.nra` builds a server program (one that calls `Std.App.run`) to `app.nvr` and replaces itself with `neura-server-host` serving it in the foreground on `127.0.0.1:8080`; it also takes a built `.nvr`, and passes every other option to the host, such as `--listen 0.0.0.0:80`, `--admin-listen 127.0.0.1:9090` or `--max-steps 500000000`. The host is found beside `neura` or on `PATH`. [Server host](Server-Host) describes the host and its options.

`neura run --record trace.txt` writes every host request and response, and how the run ended, to a text trace. `neura run --replay trace.txt` runs the image answering each request from the trace instead of calling any service, so it needs no capability, and reproduces the console output of the recorded run. A replay whose requests differ from the trace stops with status 1 and names the first difference. The trace of a program with tasks also records which task issued each request, when each overlapping request was answered, and the clock readings that expired a timer or a deadline, so its replay reproduces the schedule; such a trace carries the quantum and the task limits of the recorded run, and a replay uses them in place of its own options. [NVM](NVM#determinism-limits-and-observability) describes the trace format.

[Neuron conversion and execution](Backend) lists the supported operations and the build diagnostics for unsupported ones. The status of each example is in [Feature status](Status).

### Run through the runtime service

`neura run` runs an image in a new process of its own, and nothing changes that default: no environment variable or configuration file makes it use a service. `--runtime=auto` or `--runtime=required` on the command runs the image through `neurad`, the optional per-user [runtime service](Service), which keeps a worker process ready and the image bytes cached, and starts on demand. The output, status, traps, `--stats`, `--profile` and `--tier-stats` counts are those of a direct run.

```sh
neura run --runtime=auto app.nvr -- one two        # through the service, or directly
neura run --runtime=required app.nvr               # through the service, or status 5
neura run --runtime=required --detach --stdout=out.log --stderr=err.log app.nvr
neura runtime ps
neura runtime stop <id>
neura runtime shutdown
```

`auto` runs directly, after one line `neura: runtime: running directly: <reason>` on standard error, when the service is not installed, cannot start within 2 seconds, refuses the launch for capacity or a setting it cannot reproduce, or does not support an option: `--each`, `--record`, `--replay` and a graph device are direct only. `required` exits with status 5 instead, and so does every launch when another user's or another build's supervisor answers or the image changes while it is launched. Once the launch is committed it is never run directly: when the connection ends before the outcome, the command asks the service for its record and, if no answer comes, exits with status 6. `--detach --stdout=FILE --stderr=FILE [--stdin=FILE]` prints the application's ID and exits once the launch is committed; the files are opened for appending, and the input is empty without `--stdin`. `--runtime-report=FILE` writes the path taken and the launch's times as JSON, also for a direct run. `neura test --runtime=...` is a usage error; tests and debugging are direct. The [runtime service page](Service) lists the `neura runtime` commands, the files, and what a service launch does not inherit from the caller (its session, controlling terminal, descriptors above 2, and control group).

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

### Build and run a project

A directory with `neura.toml` is a project ([Projects and bundles](Apps-Projects)). `neura build --project <manifest> --target <name>` builds one target into its bundle, `dist/<name>` unless `--out <directory>`; without `--project` the manifest is `neura.toml` of the current directory. `--update-lock` writes `neura.lock`, which the build otherwise only compares (`E1022`). A desktop target builds its web target first and embeds it. The command prints the bundle directory and its identity, the SHA-256 of `bundle.json`. Two builds of the same sources, manifest, lock and components give the same bytes, from any directory.

`neura new <directory> --template fullstack|web|server|desktop` writes a project from a template of the SDK into a directory that does not exist or is empty, with its `neura.lock`; `--name` and `--id` set the project's name and id ([Your first Neura project](FirstProject)).

`neura dev --target <name> [--port <n>]` builds a target, serves a web bundle on `127.0.0.1:4310` or runs a server or desktop bundle, and rebuilds when a source changes, reloading the page or restarting the host; it stops everything it started on an interrupt or the end of its input ([Developing](Apps-Projects#developing)).

`neura package --target <name> [--format <format>]` wraps a built server bundle with its hosts in a reproducible `.tar.gz`, or a desktop bundle in a `.tar.gz` or `.deb` through the desktop packager ([Packaging](Apps-Projects#packaging)).

`neura run <bundle directory> [-- arguments]` verifies the bundle and then runs a server bundle under `neura-server-host` or a desktop bundle under the desktop host, passing the arguments after `--` to the host. A web bundle is refused with the ways to serve it. A single-file `neura build app.nra -o app.nvr` is unchanged, in a project directory too.

### Package a native executable

```sh
neura build --native --deny-all --allow-fs=data --max-steps 1000000 hello.nra -o hello
./hello --help
neura inspect hello -o hello.nvr
```

`build --native` packages sources, a `.nir`, or a validated `.nvr` with a CPU launcher. The image and launcher bytes are preserved. Every argument to the executable reaches the program, including `--help` and `--`. The build records the capability policy, the run limits above (`--max-steps`, `--max-frames`, `--max-heap-cells`, and `--max-stack-cells`, which needs container version 3), and the task settings: `--max-tasks`, `--task-quantum`, `--max-queued-messages`, `--max-pending-requests`, and `--parallel-workers` are accepted with `--native` and recorded, each with the default of `neura run` when it is not given. With no capability option the policy grants every group, so the executable can do whatever the user who runs it can; build with `--deny-*` options (the example denies everything but the files below `data`) to give it less. Relative file-system prefixes are resolved from the executable's working directory.

Set `NEURA_RUN_OPTIONS` to space- or tab-separated options to lower a recorded limit (`--max-steps`, `--max-frames`, `--max-heap-cells`, `--max-stack-cells`, `--max-tasks`, `--max-queued-messages`, `--max-pending-requests`) or the recorded `--parallel-workers` count, to deny a group with `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, or `--deny-all`, or to select `--stats`, `--profile`, `--tier=0`, `--tier=1`, `--jit=off|on|auto`, `--tier-stats`, `--task-dump[=text|json]`, `--trace-events <file>`, `--trace-wall-time`, `--record <file>`, or `--replay <file>`. It also accepts `--no-gpu` and `--gpu=off`, which name the CPU evaluation the launcher always uses. `--task-quantum` is rejected, because it changes the interleaving and so the output order of tasks, and `--virtual-time` because it changes what the program reads from the clock; a native executable uses the quantum its policy records. The variable has no quoting and cannot widen the build policy: every `--allow-*` option is rejected. A build with `--sealed` ignores it, `--deny-*` options included. Without `--sealed`, an executable trusts whoever sets the variable: `--record` and `--trace-events` can create or replace any file the process may write, and `--replay` answers host requests from a file of their choosing.

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

`neura --version` and `neura version` print one line to standard output, `neura <version> (<target>, commit <commit>)`, and exit with status 0; each tool prints the same line for `--version`. A build from a checkout reports a `-dev` version and commit `unknown`. `neura help` and `neura --help` print the usage text and exit with status 0; `neura` with no arguments prints it to standard error and exits with status 1.

Three commands manage the installed tool; [Install](Install) describes the install layout, the verification an update performs, and what each command removes:

| Command | Does |
| --- | --- |
| `neura doctor` | Prints `ok`, `warn`, or `FAIL` for each check of the install: its state (complete, unmigrated, interrupted, damaged), its components from their manifests, the integrity of its files, the service, its links and generations, and its optional tools (the standard library, native launchers, libclang, a C compiler, the GPU backend, `curl`, `tar`, `gh`). Without the SDK, the SDK's checks are not printed. Exits with status 1 if a check failed. Uses no network and changes no file. |
| `neura update [--check] [--nightly] [--source <https-url-or-directory>] [--require-attestation]` | Replaces an archive install with a newer release of the same components after checking each archive's size and SHA-256, every file against the component manifests and, where `gh` is available, its build provenance. `--check` reports without changing anything. `--nightly` includes published GitHub prereleases, selecting the highest semantic version. |
| `neura update --components sdk\|runtime\|sdk,service [--terminate]` | Changes the installed components, fetching an added component's archive of the installed build. |
| `neura update --rollback`, `--repair`, `--migrate-install` | Returns to the previous generation; completes or undoes an interrupted change and removes unused generations; finishes an installation that a 0.2 updater produced. None fetches anything. |
| `neura uninstall [--dry-run] [--yes] [--terminate]` | Removes an archive install, its links and its service's registration and state, and nothing else. |
| `neura uninstall --component sdk\|service [--terminate]` | Removes one component, by a new generation without it. |

`update` and `uninstall` apply only to an install made by a release archive's installer. A source build, a copy, or a package manager's install exits with status 3.

## Diagnostics

Diagnostics go to standard error, and a command that reports an error exits with status 1. Warnings are printed before the errors, or at the end of a command that succeeds, sorted by file and byte offset. A diagnostic has a primary location, a message, and optionally a code, labels at other locations (which may be in other files), notes, and suggested edits. A line is a line of the source as the lexer counts lines (LF, CRLF, or CR ends one), and a column counts Unicode code points from 1.

How many errors a run reports depends on the stage that finds them:

- **Syntax.** The lexer and the parser recover and go on. The lexer skips an invalid character, a malformed number, or an unterminated string and reads the next token. The parser skips the rest of a statement or member that fails to parse: to the `;` that ends it at the same depth, to the closing delimiter of the enclosing block or member list, or to a keyword that begins the next statement. They report up to `--max-errors N` errors (20 by default; `check`, `emit-ir`, `build`, and `test` accept the option), in source order, and any syntax error stops compilation before NeuraIR lowering.
- **Everything after parsing.** Name resolution, type checking, the later NeuraIR checks, and circuit conversion stop at the first error. Lowering resolves declarations on demand and abandons speculative work, such as ranking an overload, by catching errors, so continuing after an error would report errors that the first one caused.

Every error has a code: `E` and four digits, whose hundreds digit names the area. `E01xx` is the lexer, `E02xx` the parser, `E03xx` names, `E04xx` types and bindings, `E05xx` classes and lifecycle, `E06xx` concurrency, `E07xx` modules and imports, `E08xx` the backend, and `E09xx` [tags](Tags). A warning has a code, `W` and four digits, and a name. Errors that report the same mistake share a code, and `E0899` is the one code for errors that only a compiler defect can cause. A code is never reused for another error. [Diagnostic codes](Diagnostics) lists them. `neura explain <CODE>` (or `neura --explain <CODE>`) prints a code's page: the rule, a program that breaks it, and the program fixed. `neura explain --list` prints each code and its title, with the name of each warning; `neura explain <name>` also takes a warning's name. `neura tags` prints the catalogue of [tags](Tags), and `neura tags <Name>` one tag's signature, targets, status, and meaning. The pages are part of the `neura` binary, so `explain` reads no files. `explain` exits with status 0; a code that is not registered is an error that names the closest registered code.

`--error-format` chooses the form for the compilation and artifact commands. Editor protocol commands reserve their output for their protocol.

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
| 5 | `run --runtime=...` or `neura runtime`: the service ran nothing and nothing was started | [Runtime service](Service#statuses-and-messages) |
| 6 | `run --runtime=...`: the launch was committed and its outcome is unknown; it was not run again | [Runtime service](Service#statuses-and-messages) |
| 128 and above from `run` | The program's trap class, such as 136 for an arithmetic trap or 134 for a message trap, or the program's own `ExitCode` | [Errors](Errors), [Program entry](Modules#program-entry) |

## Execution contract

The [NVM specification](NVM#toolchain-and-distribution) defines how `neura build` produces an executable `.nvr` image from source or `.nir` and how `neura run` executes that image. Package configuration is not part of this release.

Diagnostics should identify the source file, line and column, the offending span, a clear explanation and, where possible, a correction. For example, passing a `string` to a function expecting `int32` should report both types and the argument location. [Diagnostics](#diagnostics) defines their formats, columns, and warning options.

A formatter should produce stable, idempotent output consistent with [Coding style](CodingStyle). Editor integration should eventually expose diagnostics, navigation and completion; debugger support should include source locations, stepping and local variables. These are tooling goals, not promises of first-release support.


## Editor tooling

Neura 0.3.0 provides these interfaces for the VS Code extension, as the `0.2.1-rc.2` candidate first did. They are capability-negotiated: `neura capabilities --json` returns a versioned feature object.

- `neura lsp --stdio` speaks LSP with UTF-16 positions. It analyzes unsaved sources and imports using per-session overlays, recovers partial syntax, and provides diagnostics, tokens, completion, hover, symbols, navigation, rename, hierarchy, refactoring, and formatting. Editor requests never link or run a program. C header analysis requires a trusted session.
- `neura fmt [--check | --write] <file.nra>...` shares the editor formatter. `--stdin --stdin-filepath <file>` formats unsaved text. It preserves token spellings, comments and documentation attachment; malformed input is rejected before writes. `--check` reports files that need formatting without changing them.
- `neura fmt` lays out code as [the coding style](CodingStyle) does, and formatting its own output changes nothing. A `(` or `[` written against an operand opens a call, an index or a parameter list, `f(x)`, `values[0]`, `func(`, `new(`; after an operator or a keyword it groups and is spaced like any operand, `x == (a + b)`, `return (a)`, `[["a"], ["b"]]`. A tag group, `#[Final, Since("0.3.0")]`, stays on one line above its declaration. A blank line follows a member that spans more than one line and goes before the comments directly above the next member; one-line members, such as a one-line enum, need none. A line break written inside a statement is kept: the wrapped line is one level deeper than the line that opened the innermost open parenthesis, bracket or brace, or than the statement's first line; after `=` or `=>` it is one level deeper than the line before it, and an `or` alternative is one level deeper than its declaration. A closing delimiter written on a line of its own, after an opening delimiter that ends its line, stays there. An enum written over several lines keeps its variants on the lines they are written on. A block is written with its braces in K&R style and one statement per line, also when the source writes it on one line, and a trailing comment is one space after its code.
- `neura test --list --format=json --workspace-root <directory> <files>...` returns version 1 discovery records. IDs combine workspace-relative source identity and qualified function name. Discovery performs semantic checking but no native linking, initialization, or test execution.
- Repeatable `test --filter <exact-id>` selects tests. `--reporter=json` emits version 1 `runStart`, `testStart`, `output`, `testResult`, `coverage`, and `summary` events, with program output framed separately. Ordinary human output is unchanged. `--coverage <file.json>` observes actual CPU block entries and includes zero-count mapped lines; its declared granularity is `mapped-block-line`, not statement or branch coverage.
- `neura debug --stdio` speaks DAP and drives actual unoptimized NVM execution. It supports source/test launch, breakpoints, stepping, traps, typed frame/heap inspection and read-only expressions. Task state comes from the runtime. Source maps currently identify blocks, so stepping and visible frame slots have block/frame granularity rather than exact lexical live ranges. Calls/getters/assignments are rejected in watch evaluation; native execution, optimized attach, and reverse debugging are unavailable. Console input uses the `neura/input` request.
- `neura inspect-image --json <image.nvr> [--graph N] [--limit N]` validates an image and returns source/function/block/graph summaries. Selected graph details use wire IDs; exact integer biases/weights are strings. Graph output is bounded and explicitly reports truncation.

Named arguments, defaults, extension functions, interpolated strings and `nameof` ([Core language](CoreLanguage#named-arguments)) are part of the same analysis. Inside an argument list, completion offers the labels of the parameters not yet given, as `name:`, and signature help marks the parameter that the argument's label names, also in an unfinished `f(a: `. Hovers, signatures and the documentation index show each default as `name: Type = value`. Renaming a parameter renames the labels that name it at call sites; navigation and rename reach names inside the holes of an interpolated string and the operand of `nameof`. Member completion lists the extension functions the file enables that accept the receiver, with the set that enables them. `neura fmt` writes a label as `name: value` and a default as `name: Type = value`, never adds, removes or reorders labels, and copies an interpolated string byte for byte.

`neura fmt` writes a [record](Types#records) like an enum with named payloads: on one line when it was written on one line, and otherwise one component per line with a trailing comma, which is the one token the formatter may add. A [copy](Types#copy-construction) written on one line stays there, as `new with(source) { a: x }`; written over several lines, it has one replacement per line and a trailing comma. The words of an accessor clause are written joined, `| get.virtual:`, and a bodyless interface clause, `| get;`, is indented under its property. Record, property and static-requirement tooling is tracked in [the records note](https://github.com/Kayyo321/neura/blob/main/docs/design/Records030.md#tooling).

Use `neura check --show-derived file.nra` to print the checked JSON codec expansions. Select one module with `--show-derived=Name`. The printed module can replace its derivation in the same source scope. Hover on a derived module shows its expansion, and hover on a record its components and generated members. Inside the braces of a copy, completion offers the record's fields not yet replaced, also while the copy is unfinished. After `| get.` or `| set.`, it offers `override:` under a `let.override` property and `virtual:` under any other field. Renaming a record field renames its constructor labels and the replacement names of copies with it; go to definition on a replacement name reaches the field. Renaming a named payload field, from its declaration, a label of `Mode.Slow { speed: 3 }`, a braced pattern or a `field Mode.Slow.speed` mapping, renames all of them and leaves a field of the same name in another variant. Renaming a record field, enum variant or named payload field through the language server or `neura rename` preserves its wire name in reachable derivations. Existing wire strings stay in place and an unpinned derivation receives an explicit mapping. Clients that support change annotations can identify the schema-preserving edit. Renaming a property, from its interface requirement, a field that satisfies it, a `let.override` of that field or any use, renames all of them together, and is refused when one is declared outside the workspace. Go to definition on a `let.override` name, and on the property's name inside its clauses, reaches the overridden field. Completion after `export let.override ` in a class offers the base's properties that have a virtual accessor; after `T.`, the static requirements of the bounds of the type parameter `T`; and in a derivation's braces, after `field User.` or `variant Mode.`, a record's fields, an enum's variants (only those with named payloads after `field`), or a variant's named payload fields after `field Mode.Slow.`. A record whose component list runs to the end of the buffer, `let P = record (id: int64,`, is one `E0203`, and the editor knows `P` with the components read so far; a class, interface or module whose last member's error reaches the end of the buffer keeps its other members, and after a `|` in an interface completion offers `get;` and `set;`.

Generators and `using` bindings ([Core language](CoreLanguage#generators)) behave in the editor as the functions and locals they are written as: hover and signature help show a generator's function type, `squares(count: int32) -> Sequence<int32>`, rename reaches its calls, parameters and locals, and the outline lists it as a function without the members of its generated state class. In the debugger, a breakpoint condition or an evaluation in a generator's `$resume` frame reads a local that lives in the generator's state by its source name.

`neura rename [-I source-root] <file.nra> <line>:<column> <new-name>` applies the language server's rename to the files on disk; it is a command-line companion of the editor and not part of `neura capabilities`. The line and column count from 1, and the column counts bytes, as a diagnostic's location does. It reads the `.nra` files under the current directory, or under the file's own directory when the file is outside it, as an editor reads its workspace (skipping build and version-control directories), and changes every one the rename reaches. It refuses when it could not search that directory completely (more than 5000 files, or a directory it could not read). It moves a module file whose name follows the module, and refuses, changing nothing, what the editor refuses: a library or language-defined symbol, a name that is not an identifier, a module whose new file name is taken, or an edit after which the affected files no longer compile. When the renamed record field or enum variant is reachable from a JSON derivation that does not pin it, it inserts the mapping that keeps the old wire name and says so on standard error, as `neura: rename inserted 'field User.display: "name";' in model.nra so that the JSON wire name does not change`. It ends with `neura: rename applied <n> edits to <m> files`.

Editor formatting returns no edits for an incomplete or otherwise syntactically invalid buffer, leaving diagnostics to explain the source error. The CLI formatter still rejects invalid input. Completion works with partial declaration modifiers and import paths; promoted exports follow the scope and precedence rules in [Modules](Modules#imports), and override suggestions follow inherited member eligibility. Hover renders documentation commands such as `@brief` as readable Markdown. Outline symbols always have a name and a selection within their full range.

The compiler's strict build/check behavior and image execution contract are unchanged. The normal command line and editor share the parser, resolver, diagnostics, formatter, and runtime rather than maintaining a separate editor language implementation.
