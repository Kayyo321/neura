
## C imports

`check`, `emit-ir`, `build`, and `test` read the headers of [`import:c(...)`](https://github.com/Kayyo321/neura/wiki/ForeignInterop#c-imports) with libclang. The tool loads libclang when the first C import is compiled, from `--libclang <file>`, else the `NEURA_LIBCLANG` environment variable, else the platform's usual names, such as `libclang-19.so.1` and `/usr/lib/llvm-19/lib`. Builds without C imports never load it. `--c-include <dir>` adds a header search directory, and `--c-define <name[=value]>` defines a macro while reading headers.

`emit-ir`, `build`, and `test` link the libraries of C imports with the system C compiler: `--cc <compiler>`, else `$CC`, else `cc`. `--c-link-arg <arg>` passes an argument to that link. `check` reads headers but does not link.

`neura c-bindings [--linked] [c-options] <header.h>` prints the module that a C import of the header generates. `--linked` generates it as an import that names libraries does.
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

`Std` imports resolve in the trusted standard library: below `share/neura/stdlib` of an installed tool, or the checkout's `stdlib` directory for a build directory directly inside the checkout ([Modules](https://github.com/Kayyo321/neura/wiki/Modules#standard-library-imports)).

Both commands accept multiple input paths and resolve imports across the source roots. `check` validates the source and succeeds silently on valid input. It does not run the optimizer. `emit-ir` writes a typed NeuraIR listing with resolved symbol references and explicit operations for control flow, state, and calls. The listing is for inspection. A source diagnostic reports the path, line, and column and returns a nonzero exit status.

`emit-ir` prints optimized IR by default. Pass `--no-optimize` to print the IR exactly as lowered. To see what the optimizer changed, compare the two listings:

```sh
build/neura emit-ir --no-optimize tests/ir-optimization/fixtures/folding.nra > before.ir
build/neura emit-ir tests/ir-optimization/fixtures/folding.nra > after.ir
diff before.ir after.ir
```

The two listings for this fixture share a symbol table, so the diff shows rewritten instructions. For programs that take primitive references or specialize generics, the optimized listing can also show new or rewritten symbols from those compilation stages. The listing is separate from the versioned `.nir` file described below, which holds NeuraIR before those transformations. See [NeuraIR optimization](https://github.com/Kayyo321/neura/wiki/IrOptimization) for the passes and their preconditions.

## Build and run executables

```sh
build/neura emit-ir examples/hello-world/hello-world.nra -o hello.nir
build/neura build hello.nir -o hello.nvr
build/neura build examples/hello-world/hello-world.nra -o hello.nvr
build/neura run hello.nvr -- arg1 arg2
```

`emit-ir -o` writes the program as lowered, before optimization, to a versioned [`.nir` file](https://github.com/Kayyo321/neura/wiki/NirFormat). `emit-ir` also accepts one `.nir` file in place of sources and prints its listing. `build` takes sources or one `.nir` file. It verifies and optimizes the IR, converts it to neuron graphs, and writes the `.nvr` image only when every step succeeds. Both routes produce the same bytes for the same program and options, and repeating a build reproduces them. `--no-optimize` converts the verified IR without optimizing it. `--no-source-map` omits the optional source map, which otherwise labels blocks with paths relative to the sources' common directory.

Sources can [import prebuilt modules](https://github.com/Kayyo321/neura/wiki/Modules#prebuilt-neurair-modules) with `import:"library.nir".Module`. `check`, `emit-ir`, `build`, and `test` resolve and link those imports during source compilation.

`neura docs <file.nra>...` exports declaration documentation and source ranges as JSON for IDE tooling. Pass `-o <file.json>` to save the index. `neura docs --doxygen <file.nra>` emits a declaration view for Doxygen's input filter. [Documentation comments](https://github.com/Kayyo321/neura/wiki/CoreLanguage#documentation-comments)

`run` loads and validates an image, binds its host imports to the runner's services ([NVM](https://github.com/Kayyo321/neura/wiki/NVM#host-services)), and passes the arguments after `--` to the entry function as UTF-8 text. The image needs no source files. The exit status is the `ExitCode` that `main` returns, or the status given to `Std.Program.Process.exit`. A runtime trap prints its reason and source location, when the image has a source map, to standard error and exits with the status of its class, such as 136 for an arithmetic trap or 134 for a message trap ([Errors](https://github.com/Kayyo321/neura/wiki/Errors)). Load, bind, and usage errors exit with status 1. `--max-steps`, `--max-frames`, and `--max-heap-cells` bound a run. `--stats` prints the run's step count, peak heap cells, host requests, graph, neuron, and edge counts, and image bytes to standard error. `cmake --install build --prefix <dir>` installs the `neura` executable, which contains both the compiler and the runner, and the standard library below `<dir>/share/neura/stdlib`, where the installed tool finds it without any `-I` option.

Console input and output and `Std.Program.Process.exit` are always available. Other host services need a capability option; without it, every call returns the `CapabilityDenied` error:

| Option | Grants |
| --- | --- |
| `--allow-fs` | Files and directories ([`Std.Fs`](https://github.com/Kayyo321/neura/wiki/Stdlib-Design#112-stdfs)) |
| `--allow-fs=prefix,...` | Files and directories at or below the listed paths, after resolving symbolic links |
| `--allow-net` | TCP connections and listeners ([`Std.Net`](https://github.com/Kayyo321/neura/wiki/Stdlib-Design#114-stdnet)) |
| `--allow-env` | Environment variables (`Std.Program.Environment`) |
| `--allow-clock` | The wall and monotonic clocks and sleeping (`Std.Time.SystemClock`) |
| `--allow-entropy` | Operating-system entropy (`Std.Random.Random.from_entropy`) |
| `--allow-native` | Native libraries, memory, and calls ([`Std.DynamicForeign`](https://github.com/Kayyo321/neura/wiki/ForeignInterop)); native code runs outside Neura's safety guarantees |

`neura run --record trace.txt` writes every host request and response, and how the run ended, to a text trace. `neura run --replay trace.txt` runs the image answering each request from the trace instead of calling any service, so it needs no capability, and reproduces the console output of the recorded run. A replay whose requests differ from the trace stops with status 1 and names the first difference. [NVM](https://github.com/Kayyo321/neura/wiki/NVM#determinism-limits-and-observability) describes the trace format.

[Neuron conversion and execution](https://github.com/Kayyo321/neura/wiki/Backend) lists the supported operations and the build diagnostics for unsupported ones. The status of each example is in [Feature status](https://github.com/Kayyo321/neura/wiki/Status).

### Choose graph execution

`run` and `test` choose graph placement automatically by default. With `--gpu=auto`, Neura opens Vulkan only when the observed workload can repay setup and calibration. It sends a batch only when the calibrated device is predicted to beat the CPU by the configured margin. `--gpu=always` sends eligible graphs to Vulkan, with CPU fallback when no device is usable or a request is declined. `--gpu=off` and `--no-gpu` select the CPU. `--require-gpu` selects `always` and turns an unavailable or failed device into an error. `--gpu-software` also accepts software Vulkan devices. `--gpu-verify` checks every device result against the CPU. When a device is in use, `test` and `run --each` advance their members as a fleet, preserving serial output and host effect order.

The CMake setting `NEURA_GPU=AUTO` builds the GPU backend when the Vulkan headers and `glslangValidator` are available on a supported platform. `ON` requires those tools and `OFF` builds the CPU runner. The GPU backend opens the Vulkan loader only when a run requests a device.

`NEURA_GPU` accepts `auto`, `always`, and `off` for `run` and `test`. An unset or empty value uses the default. An explicit mode option, `--no-gpu`, or `--require-gpu` takes precedence. Other values are usage errors. The effective off mode also applies the option checks for software devices and verification.

With `--stats`, a final `gpu:` line reports the selected mode, whether a device was built or used, and the reason when no device was used. A device report includes the device name, eligible graphs, evaluated and declined jobs, escapes, failures, and dispatches. Auto mode also reports the jobs kept on the CPU, the timing correction, and any rejection reason.

### Run an image over argument lists

```sh
neura run --each lists.txt image.nvr -- common arguments
```

`--each` loads and binds the image once. Each member runs with fresh state, the same limits and capabilities, and the common arguments followed by that line’s arguments. Host effects and member reports follow file order. Members share standard input, and the runner closes any files and sockets a member leaves open before it serves the next member.

The file is read and validated in full before a member runs. LF separates lines. A final LF ends the preceding line. An empty line gives a member no extra arguments, and a line beginning with `#` is a comment. A line ending in CR is invalid, including a comment line. Spaces and tabs separate arguments and are ignored at either end. Bare arguments cannot contain quotes or backslashes. Double quotes allow spaces, tabs, and empty arguments. Inside quotes, the escapes are `\"`, `\\`, `\n`, `\r`, `\t`, and `\xHH`, where `HH` is two hexadecimal digits. A backslash without a complete escape is invalid. Each resulting argument must be UTF-8. The file must yield between 1 and 1,048,576 argument lists and fit in 64 MiB.

A member with status 0 has no report. For other outcomes, standard error shows `<image>: <lists>:<line>: exit status <status>` or `<image>: <lists>:<line>: trap: <diagnostic>`. The image path is canonical and the list path retains its command-line spelling. Later members still run. The command returns the first nonzero status in file order, including the status assigned to a trap, or 0 if all members succeed. `--stats` reports the number of members, exact totals for steps and host requests, the largest member heap peak, and the costs and byte size of the loaded image, followed by the `gpu:` line. `--each` applies only to `run` and may appear once. It conflicts with `--record`, `--replay`, and `--profile`, checked in that order when more than one is present.

## Test

```sh
build/neura test -I examples/guide examples/testing/math-tests.nra
```

`test` builds the listed files, runs every test they declare in a fresh execution, prints `PASS` or `FAIL` with the failure and its location for each, and a summary line. It exits with status 0 only when every test passes. It accepts the capability options of `run`, `--stats`, and the run limits. [Testing](https://github.com/Kayyo321/neura/wiki/Testing) defines tests and the `Std.Test` assertions.

## Inspect and decompile images

```sh
build/neura disassemble hello.nvr -o hello.nvra
build/neura assemble hello.nvra -o copy.nvr
build/neura decompile hello.nvr
```

`disassemble` writes the lossless text form of an image: every table, graph, neuron, weight, block, exit, import, the source map, and any optional section, in order. `assemble` turns that text back into an image, validates it with the same loader as `run`, and writes it; assembling an unedited disassembly reproduces the original bytes. Edited assembly is how tests and tools construct images by hand.

`decompile` writes a typed reconstruction of the image: functions, blocks, state operations, calls, host effects, and the operations each block's graph computes, with every block labeled by how its meaning was established. `--no-provenance` omits names and source locations from the optional NAME section and source map. [Decompiler](https://github.com/Kayyo321/neura/wiki/Decompiler) specifies both forms and the evidence behind them.

## Execution contract

The [NVM specification](https://github.com/Kayyo321/neura/wiki/NVM#toolchain-and-distribution) defines how `neura build` produces an executable `.nvr` image from source or `.nir` and how `neura run` executes that image. Package configuration is not part of this release.

Diagnostics should identify the source file, line and column, the offending span, a clear explanation and, where possible, a correction. For example, passing a `string` to a function expecting `int32` should report both types and the argument location. Diagnostics go to standard error and compilation fails on errors. Exact format, encoding of column offsets and warning policy remain open.

A formatter should produce stable, idempotent output consistent with [Coding style](https://github.com/Kayyo321/neura/blob/main/docs/style/CodingStyle.md). Editor integration should eventually expose diagnostics, navigation and completion; debugger support should include source locations, stepping and local variables. These are tooling goals, not promises of first-release support.
