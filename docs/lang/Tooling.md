# Tooling and project workflow

## Build the compiler

Build with CMake 3.20 or newer and a C++20 compiler.

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

Both commands accept multiple input paths and resolve imports across the source roots. `check` validates the source and succeeds silently on valid input. It does not run the optimizer. `emit-ir` writes a typed NeuraIR listing with resolved symbol references and explicit operations for control flow, state, and calls. The listing is for inspection. A source diagnostic reports the path, line, and column and returns a nonzero exit status.

`emit-ir` prints optimized IR by default. Pass `--no-optimize` to print the IR exactly as lowered. To see what the optimizer changed, compare the two listings:

```sh
build/neura emit-ir --no-optimize tests/ir-optimization/fixtures/folding.nra > before.ir
build/neura emit-ir tests/ir-optimization/fixtures/folding.nra > after.ir
diff before.ir after.ir
```

The two listings share the same format and symbol table, so the diff shows only rewritten instructions. The listing is for inspection and is separate from the versioned `.nir` file described below, which holds NeuraIR before optimization. See [NeuraIR optimization](../IrOptimization.md) for the passes and their preconditions.

## Build and run executables

```sh
build/neura emit-ir examples/hello-world/hello-world.nra -o hello.nir
build/neura build hello.nir -o hello.nvr
build/neura build examples/hello-world/hello-world.nra -o hello.nvr
build/neura run hello.nvr -- arg1 arg2
```

`emit-ir -o` writes the program as lowered, before optimization, to a versioned [`.nir` file](../NirFormat.md). `emit-ir` also accepts one `.nir` file in place of sources and prints its listing. `build` takes sources or one `.nir` file. It verifies and optimizes the IR, converts it to neuron graphs, and writes the `.nvr` image only when every step succeeds. Both routes produce the same bytes for the same program and options, and repeating a build reproduces them. `--no-optimize` converts the verified IR without optimizing it. `--no-source-map` omits the optional source map, which otherwise labels blocks with paths relative to the sources' common directory.

`run` loads and validates an image, binds `neura.console.write_line` to standard output, and passes the arguments after `--` to the entry function as UTF-8 text. The image needs no source files. The exit status is the entry function's result. A runtime trap prints its reason and source location, when the image has a source map, to standard error and exits with status 70. Load, bind, and usage errors exit with status 1. `--max-steps`, `--max-frames`, and `--max-heap-cells` bound a run. `cmake --install build --prefix <dir>` installs the `neura` executable, which contains both the compiler and the runner.

[Neuron conversion and execution](../Backend.md) lists the supported operations and the build diagnostics for unsupported ones. The status of each example is in [Feature status](Status.md).

## Execution contract

The [NVM specification](../runtime/NVM.md#toolchain-and-distribution) defines how `neura build` produces an executable `.nvr` image from source or `.nir` and how `neura run` executes that image. The language test runner and package configuration are specified separately.

Diagnostics should identify the source file, line and column, the offending span, a clear explanation and, where possible, a correction. For example, passing a `string` to a function expecting `int32` should report both types and the argument location. Diagnostics go to standard error and compilation fails on errors. Exact format, encoding of column offsets and warning policy remain open.

A formatter should produce stable, idempotent output consistent with [Coding style](../style/CodingStyle.md). Editor integration should eventually expose diagnostics, navigation and completion; debugger support should include source locations, stepping and local variables. These are tooling goals, not promises of first-release support.
