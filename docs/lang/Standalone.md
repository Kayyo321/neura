# Native executables: implementation plan for `neura build --native`

Phase 1 implements `neura build --native` with the default launcher and policy on Linux x86-64. The remaining phases add build-time policy options, executable inspection, and launcher variants. A native executable contains the program's image and the runtime that executes it, so the file can run on a machine without an installed Neura toolchain.

## 1. How to read this plan

- "Must" states a requirement on the implementation. A row of section 3 is a decision; an option name, message, or record layout given here is fixed. Section 13 lists what is still open.
- Two words already have meanings in these pages, and this plan keeps them:
  - **Standalone image.** [Overview](Overview.md) and [Status](Status.md) call a `.nvr` image standalone because it runs without its source. It still needs a runner.
  - **Native code.** `--allow-native`, `Std.DynamicForeign`, and the `NATV` section concern C code that a program calls ([Foreign interop](ForeignInterop.md)). `--native` grants no native access and compiles nothing to machine code.

| Term | Meaning |
| --- | --- |
| Image | The `.nvr` bytes `neura build` writes today ([NVR](../runtime/NVR.md)). |
| Launcher | A prebuilt executable that contains the NVM library and the standard host providers, and no compiler. |
| Native executable | The output of `neura build --native`: a launcher, a payload, and a footer in one file. |
| Policy | The capabilities and run limits recorded in a native executable when it is built. |
| Payload | The policy followed by the image. |

**Stages affected.** Only the output stage's packaging and the CLI, plus one new program, the launcher. The lexer, parser, NeuraIR, IR optimization, IRtoNeuronConversion, the NVR writer and loader, and the NVM's execution contract do not change. No image byte changes.

## 2. Summary

```sh
neura build --native examples/hello-world/hello-world.nra -o hello
./hello arg1 arg2
```

`neura build --native` builds the image exactly as `neura build` does, then writes one file: a copy of an installed launcher, the image, and a small footer that says where the image is. When the file runs, the launcher finds the image inside its own file, validates it with the same loader as `neura run`, binds its host imports under the capabilities chosen at build time, and runs it.

A native executable is a packaging of the existing execution model, not a new one. The neuron graphs stay data in the image, and the same reference evaluator computes them. It is not faster than `neura run`, and it must not behave differently. What it removes is the need for an installed toolchain, a separate image file, and a command line that names both.

Because the image is embedded unchanged, everything that inspects images keeps working: `disassemble`, `decompile`, and `run` read a native executable's image directly, and the bytes they read are the bytes `neura build` would have written.

## 3. Decisions

| ID | Decision |
| --- | --- |
| N1 | `--native` packages. The executable is a launcher plus an image; the NVM evaluates the image's graphs. Compiling circuits to machine code is outside this plan. |
| N2 | The embedded image is byte for byte the image `neura build` writes for the same inputs and options. Every compiler stage runs unchanged. |
| N3 | `neura build --native` needs no compiler, linker, or other external tool. It reads an installed launcher and writes the output itself. A program with C imports still needs the C compiler those imports need today. |
| N4 | The container is the launcher's bytes, then the payload, then a fixed footer at the end of the file (section 5). The launcher's bytes are not modified. |
| N5 | Every command-line argument of a native executable belongs to the program. The launcher interprets none. |
| N6 | Capabilities are chosen when the executable is built and recorded in its policy. The default grants what `neura run` grants by default: console input and output and process exit. Nothing at run time can widen them. |
| N7 | Run-time settings that only restrict or observe a run come from one environment variable, `NEURA_RUN_OPTIONS`, unless the executable was built with `--sealed`. |
| N8 | Output bytes are a function of the launcher file, the image, and the policy. There are no timestamps, paths, or host names. |
| N9 | The toolchain reads the image back out. `run`, `disassemble`, and `decompile` accept a native executable in place of a `.nvr` file, and `neura inspect` reports the launcher, the policy, and the image. |
| N10 | There are two launcher flavors. The default links the platform C library dynamically and everything else statically. `--static` selects a fully static launcher without native interop. |
| N11 | The verified target is Linux x86-64, the platform of this release's native tests. Other ELF targets are selected with `--target` and are specified but unverified. Mach-O and PE are outside this plan. |
| N12 | An image whose embedded native library was linked for a different target than the launcher is a build error. |

## 4. Command line

### 4.1 Forms

```sh
neura build --native [-I source-root] [--no-optimize] [--no-source-map] [policy] [launcher] <file.nra>... -o <file>
neura build --native [--no-optimize] [--no-source-map] [policy] [launcher] <file.nir> -o <file>
neura build --native [policy] [launcher] <file.nvr> -o <file>
neura inspect <file> [-o <file.nvr>]
```

The first two forms accept everything `neura build` accepts, including the [C import options](Tooling.md#c-imports), and produce the same image. The third packages an existing image without rebuilding it: a team can build an image once, test, disassemble, or decompile exactly those bytes, and then ship them. `--no-optimize` and `--no-source-map` are rejected with a `.nvr` input, because the image is already built.

`-o` is required. The output is written only when every step succeeds, with mode 0755 on POSIX hosts. An existing output file is replaced.

### 4.2 Options

These are accepted only with `--native`.

| Option | Meaning |
| --- | --- |
| `--allow-fs`, `--allow-fs=prefix,...`, `--allow-net`, `--allow-env`, `--allow-clock`, `--allow-entropy`, `--allow-native` | Record the capability in the policy. Same meaning as for [`neura run`](Tooling.md#build-and-run-executables). |
| `--max-steps N`, `--max-frames N`, `--max-heap-cells N` | Record a run limit in the policy. Without one, the policy records the default of `neura run`. |
| `--sealed` | The executable ignores `NEURA_RUN_OPTIONS` (section 6.4). |
| `--static` | Use the static launcher (section 7.2). |
| `--target <triple>` | Use the launcher for another target, such as `aarch64-linux-gnu`. The default is the target of the `neura` that is running. |
| `--launcher <file>` | Use this launcher file instead of searching (section 7.3). |

A file-system prefix is recorded as written. A relative prefix is resolved when the executable starts, against its working directory, as `neura run` resolves one.

### 4.3 Errors and notes

Errors are printed as `neura: error: <text>` and exit with status 1. No output file is written.

| Case | Text |
| --- | --- |
| An option of section 4.2 without `--native` | `<option> applies only with --native` |
| A `.nvr` input without `--native` | `build takes sources or one .nir file; packaging a .nvr needs --native` |
| `--no-optimize` or `--no-source-map` with a `.nvr` input | `<option> cannot be applied to an image that is already built` |
| No launcher found | `--native: no launcher for <triple> in <directory>; pass --launcher or install one` |
| The file is not a launcher | `--native: <file> is not a Neura launcher` |
| The image needs a newer runtime | `--native: the image is NVR 1.<m>; this launcher runs up to 1.<n>` |
| The image's native library is for another target (N12) | `--native: the image's native library is for <triple>; the launcher is for <triple>` |
| `--static` with an image that imports `neura.ffi.*` or has a `NATV` section, or with `--allow-native` | `--static: the image uses native interop, which the static launcher does not provide` |
| `--target` or `--launcher` given twice with different values | `conflicting <option> values` |

One note is printed to standard error without failing the build: for each capability group whose services the image imports and the policy does not grant, `neura: note: the image uses <group> services; without --allow-<group> they fail with CapabilityDenied`. The executable is still valid: a denied service binds and fails with error 14, as under `neura run`.

## 5. The container

### 5.1 Layout

```text
+------------------+  offset 0
| launcher         |  an ordinary executable for the target, unmodified
+------------------+  payload offset = launcher size
| policy           |
| image            |  the .nvr bytes, unmodified
+------------------+
| footer, 32 bytes |  the last bytes of the file
+------------------+
```

All integers are fixed-width and little-endian, and `text`, `bytes`, and `vector<T>` are encoded as in [NVR](../runtime/NVR.md#encoding-and-compatibility).

**Footer.**

| Field | Type | Value |
| --- | --- | --- |
| Payload offset | u64 | Absolute offset of the payload; equal to the launcher's size |
| Payload size | u64 | Bytes of policy and image together |
| Payload checksum | u32 | IEEE CRC-32 of the payload bytes |
| Container version | u32 | 1 |
| Magic | 8 bytes | ASCII `NEURAEXE` |

**Policy.**

| Field | Type | Value |
| --- | --- | --- |
| Flags | u32 | Bit 0: sealed. Other bits are zero in version 1. |
| Capabilities | u32 | Bit 0 files, 1 network, 2 environment, 3 clock, 4 entropy, 5 native. Other bits are zero. |
| File-system prefixes | `vector<text>` | Empty when files are not granted or every path is allowed |
| Maximum steps | u64 | |
| Maximum frames | u32 | |
| Maximum heap cells | u64 | |
| Image | `bytes` | The image. An image of 4 GiB or more is a build error. |

The checksum detects truncation and accidental damage. It is not a signature and gives no protection against deliberate modification; an operator who needs that signs the whole file with their platform's tools.

### 5.2 The launcher descriptor

A launcher carries one descriptor so that `neura build` can check it without running it, which a build for another target could not do. The descriptor is a constant in the launcher's data that begins with the 16 bytes `NEURA-LAUNCHER-1`, followed by `triple:text`, `flavor:u8` (0 default, 1 static), `nvr_major:u16`, `nvr_minor:u16` (the newest image version it loads), `container_version:u32`, and `toolchain_version:text`. `neura build` finds it by searching the launcher file for those 16 bytes, and rejects a file in which they occur zero times or more than once.

### 5.3 Reading a container

A reader takes the last 32 bytes, checks the magic and the container version, checks that the payload lies inside the file and ends exactly at the footer, and verifies the checksum, before it decodes the policy. It bounds every count by the bytes that remain before allocating, as the [NVR loader](../runtime/NVR.md#writer-and-loader-requirements) does. Reserved bits that are set, bytes left over after the image, and a newer container version are errors. The image then goes through the unchanged NVR loader, so an invalid image fails before any host import is invoked.

### 5.4 Why the end of the file

ELF loaders map only the segments the program headers name and ignore bytes after them, so appending does not disturb the launcher, needs no linker, and leaves the launcher's bytes identical to the installed file. Two consequences are stated here so they are not discovered later:

- `strip`, and any tool that rewrites the ELF file, discards the payload. The result is a bare launcher, which reports that it has no program (section 6.1). Launchers are installed already stripped; a native executable must not be stripped again.
- A tool that appends its own data after the footer hides the footer. Mach-O code signing and PE Authenticode both have rules about trailing data, which is why those formats need a different container and are outside this plan (N11).

## 6. Run-time behavior

### 6.1 Start-up

In order:

1. The launcher opens its own file (`/proc/self/exe` on Linux) and reads the container (section 5.3). It reads the footer and the payload, not the launcher's own bytes.
2. Unless the policy is sealed, it reads `NEURA_RUN_OPTIONS` (section 6.4).
3. It loads and validates the image, binds the image's imports to the standard providers under the policy's capabilities, and provides the image's embedded native library, if any, to the native provider.
4. It runs the entry function with the program's arguments.

Container and load failures are printed as `<program name>: error: <text>` and exit with status 1, the status `neura run` uses for load and bind errors. A launcher with no payload prints `this is a Neura launcher with no program; build one with neura build --native`.

### 6.2 Arguments and exit status

- The arguments after the program name are passed to `main` as UTF-8 text, in order, exactly as the arguments after `--` are under `neura run`. `--help`, `--`, and anything that looks like a runner option reach the program untouched (N5). An argument that is not valid UTF-8 is a start-up error.
- The exit status is the `ExitCode` that `main` returns, or the status given to `Std.Program.Process.exit`.
- A trap prints the diagnostic `neura run` prints, with a source location when the image has a source map, and exits with the status of its class: 136, 139, 134, 132, 137, or 152 ([Errors](Errors.md)).
- When the run ends, by completion, trap, or exit, the launcher closes every file and socket the program left open, as the runner does.

### 6.3 Capabilities and limits

The policy is the whole grant. A service outside it still binds, and every call returns `CapabilityDenied` (error 14), so the same program behaves the same way under `neura run` without the option.

Choosing capabilities at build time fits what a native executable is. Under `neura run`, the person running an image decides what it may touch. A native executable has no runner command line (N5), so the builder decides, and `neura inspect` shows anyone what was decided. The operating system remains the security boundary for whoever starts the file, as for any program; the policy bounds what the Neura program inside can ask for, which still contains a bug or a compromised dependency.

Run limits work as under `neura run`: exhaustion traps at the operation that would exceed the limit.

### 6.4 `NEURA_RUN_OPTIONS`

An operator sometimes needs to bound or record a deployed program without rebuilding it. The variable holds options separated by spaces or tabs. There is no quoting, so a path with a space cannot be given.

| Option | Effect |
| --- | --- |
| `--max-steps N`, `--max-frames N`, `--max-heap-cells N` | The run uses the smaller of this value and the policy's. A limit can be lowered, never raised. |
| `--stats`, `--profile` | As for `neura run`, on standard error. |
| `--record <file>` | Writes the run's host trace ([NVM](../runtime/NVM.md#determinism-limits-and-observability)). |
| `--replay <file>` | Answers every host request from the trace. No service runs, so the policy's capabilities are not used. |

- An unset or empty variable has no effect.
- Any other option, including every `--allow-*`, is the start-up error `NEURA_RUN_OPTIONS: <option> is not accepted by a native executable`. The variable cannot widen the policy (N6).
- A sealed executable ignores the variable entirely. Sealing suits programs that run with more privilege than the user who sets their environment.
- A trace recorded this way replays under `neura run --replay` with the extracted image, and a trace recorded by `neura run` replays in the executable, because both run the same image on the same NVM.

When the [concurrency plan](Concurrency.md#143-command-line) and the [GPU plan](GpuExecution.md#14-command-line) are implemented, their run options are accepted here on the same rule: settings that restrict or observe are allowed, and the launcher handles `SIGQUIT` task dumps as `neura run` does.

### 6.5 Native libraries

An image with C imports carries its linked library in `NATV` ([Foreign interop](ForeignInterop.md#c-imports)). The executable loads it on the first native call, and only when the policy grants the native capability; otherwise that call fails as it does under `neura run` without `--allow-native`. Libraries a program opens by path with `SymbolLookup.library_lookup` are not bundled. They must exist on the machine that runs the executable, and the page of a program that needs them should say so.

## 7. Launchers

### 7.1 What a launcher is

A launcher is `src/launcher/main.cpp` linked with `neura_nvm` and `neura_nvr` and nothing from the compiler or decompiler. [CMakeLists.txt](../../CMakeLists.txt) already keeps the NVM free of compiler code for this purpose. Its `main` performs section 6.1 through the same public interfaces `neura run` uses: `nvr::load`, `nvm::bind`, `nvm::run`, `nvm::record`, and `nvm::replay` ([Backend](../Backend.md#running-an-image)). The CLI's `Host` structure and its trap reporting move into a small library that both `neura` and the launcher link, so the two cannot drift apart.

### 7.2 Flavors

| Flavor | Links | Use |
| --- | --- | --- |
| Default | The platform C library dynamically; libffi, the C++ runtime, and everything else statically | Any image. Runs wherever the target's C library is present. |
| Static (`--static`) | Everything statically. The native provider is compiled out, and every `neura.ffi.*` service is registered to fail with code 13 (`Unsupported`). | Images without native interop, in minimal containers and images with no C library. |

Dynamic loading needs a dynamic C library, which is why native interop and a fully static file exclude each other.

### 7.3 Build, install, and search

| Target | Change |
| --- | --- |
| `neura_runner` | New static library: the runner's host set-up and trap reporting, moved from `src/cli/main.cpp`. Links `neura_nvm`. |
| `neura_launcher` | New executable. Links `neura_runner`. Installed stripped. |
| `neura_launcher_static` | New executable, built when the CMake option `NEURA_STATIC_LAUNCHER` is `ON`, or `AUTO` and a static C library is found. |
| `neura` | Links `neura_runner`. Gains `build --native` and `inspect`. |

`cmake --install` places launchers at `<prefix>/lib/neura/launchers/<triple>/neura-launcher` and `neura-launcher-static`, where `<triple>` is the form `nvm::native_target_triple()` returns, such as `x86_64-linux-gnu`. A `neura` in a build directory directly inside the checkout finds them below that build directory, as it finds the standard library there.

`neura build --native` uses `--launcher <file>` when given. Otherwise it looks in the launcher directory of the `neura` that is running, under the triple from `--target` or its own. A launcher for another target comes from building Neura for that target and copying its launcher directory; the image needs no rebuilding, because an image moves between hosts unchanged ([NVM](../runtime/NVM.md#toolchain-and-distribution)).

The phase 1 Release build on Linux x86-64 with GCC 14.2.0 produced a stripped default launcher of 1,999,344 bytes and a packaged Hello World of 2,012,899 bytes. On the Ryzen 7 9800X3D pinned to CPU 7, the median of 21 paired launches after warming both modes was 0.418 ms for the executable and 0.644 ms for `neura run --no-gpu`. The descriptor uses `development` as the toolchain version for this unversioned build.

## 8. Reading an executable back

- `neura run <executable> [-- args]`, `neura disassemble <executable>`, and `neura decompile <executable>` accept a native executable wherever they accept a `.nvr` file. They recognize it by the footer and read the embedded image. `neura run` applies its own command line's capabilities and limits, not the executable's policy: it is `neura run` on that image.
- `neura inspect <executable>` prints the launcher descriptor, the policy, and a summary of the image. With `-o <file.nvr>` it also writes the image.

```text
native executable: hello
launcher: neura <version>, x86_64-linux-gnu, default, runs NVR up to 1.4
capabilities: console, process exit, files (/var/lib/hello), network
limits: steps 100000000, frames 10000, heap cells 67108864
run options: accepted
image: 48211 bytes, NVR 1.2, 31 graphs, source map present, native library absent
```

The decompiler's evidence rules are unchanged. A reconstruction of an executable's image rests on that image's graphs and tables, and the launcher contributes nothing to it ([Decompiler](../Decompiler.md#evidence)).

## 9. Contract

**What must be identical.** For the same image, arguments, standard input, capabilities, limits, and host responses, a native executable and `neura run <image> -- <arguments>` must agree on:

- every byte written to standard output, and the trap diagnostic on standard error;
- the exit status;
- every host request, in order, with the same arguments, and so a trace written by `--record`, byte for byte;
- the step count and peak heap cells that `--stats` reports.

This follows from construction: the image is the same bytes (N2) and both programs drive the same NVM library with the same providers (section 7.1).

**What may differ.** Start-up time; the text of container errors, which `neura run` cannot have; and the program name in runner messages.

**What stays in the circuit and what crosses the boundary.** Nothing moves. The circuit is the image's graphs, the launcher is an NVM, and host effects cross the same boundary through the same imports. The policy is runner configuration, as the options of `neura run` are; no image section records it, and an image's behavior does not depend on which container holds it.

## 10. Verification

New CTest entries, with the evidence each gives:

| Test | Check | Kind |
| --- | --- | --- |
| `native-build` | Sources, a `.nir`, and a `.nvr` give the same executable for the same options. Building twice gives identical bytes. `inspect -o` returns the bytes `neura build` writes. Every error and note of section 4.3. | Structural, reproducibility |
| `native-run` | Every runnable example, every driver in `tests/backend/examples`, and every trap program: standard output, standard error, and status equal those of `neura run`. Arguments such as `--help`, `--`, and `--max-steps` reach `main`. | End to end, differential |
| `native-policy` | Each capability denied by default and allowed when recorded; path prefixes; recorded limits; `NEURA_RUN_OPTIONS` lowering a limit, refusing to raise one, refusing `--allow-fs`; a sealed executable ignoring the variable. | End to end |
| `native-replay` | A trace recorded by the executable replays under `neura run`, and the reverse, with identical trace bytes. | Replay |
| `native-container` | Every truncation of the payload and footer, a wrong checksum, reserved bits, trailing bytes, a newer container version, a bare launcher, and a stripped executable each fail at start-up with status 1 and before any host import is invoked. | Exhaustive (truncations), structural |
| `native-install` | From an installed toolchain, an executable is built, copied to an empty directory with no toolchain on the path, and run. With `--static`, it runs with no shared library available. | End to end |
| `native-c-import` | An image with a `NATV` section runs with the capability recorded and fails as documented without it; a library for another target is a build error. | End to end on Linux x86-64 |

The existing image tests are the evidence for the image itself. These tests establish only that packaging preserves it.

## 11. Documentation to update with the implementation

| Page | Change |
| --- | --- |
| [Tooling](Tooling.md) | `build --native` and its options, `inspect`, `NEURA_RUN_OPTIONS`, and the commands that accept an executable. |
| [NVM](../runtime/NVM.md) | In "Toolchain and distribution": the launcher as a runtime-only distribution, and the container as a carrier that does not alter the execution contract. |
| [Backend](../Backend.md) | The directory table gains `src/launcher/` and `neura_runner`; "Running an image" gains the launcher; the test table gains the new entries. |
| [Project](../Project.md) | One sentence in "Artifact and inspection": an image may be carried inside a native executable, and inspection reads it from there unchanged. |
| [Status](Status.md), [Release](../Release.md) | The Tooling row and the rows mapping native executables to their tests. |
| [Overview](Overview.md) | The first paragraph distinguishes standalone images from native executables. |
| `wiki/` | The matching pages, following [wiki maintenance](../Wiki.md). |
| This page | Becomes the specification: remove "plan" and replace the size indication with measurements. |

## 12. Outside this plan

- **Ahead-of-time compilation.** Turning neuron graphs into machine code would add a second evaluator whose results must be shown equal to the reference evaluator's, as the [GPU plan](GpuExecution.md) does for its device. It needs its own design.
- **Mach-O and PE.** Both need the payload in a place their signing rules allow.
- **Bundled data.** Files a program reads at run time are not embedded. A program that needs them ships them beside the executable and is built with a file-system grant.
- **Bundled shared libraries.** Only the statically linked library of C imports travels in the image.
- **Payload compression, self-update, and installers.**
- **A library form.** Embedding a program in another application is the [embedding API](../Backend.md#running-an-image), which exists.
- **A GPU launcher.** When the GPU plan lands, a launcher flavor that links `neura_gpu` follows that plan's rules for a runtime-only distribution.

## 13. Open decisions

| Area | Open |
| --- | --- |
| Narrowing at deployment | Whether `NEURA_RUN_OPTIONS` should also accept options that remove a recorded capability. |
| Sealing by default | Whether accepting the variable should be the opt-in, with `--sealed` as the default. |
| `neura run <executable>` | Whether it should offer a way to apply the executable's policy instead of the runner's command line. |
| Descriptor location | Searching for the descriptor's 16 bytes is simple and format-independent. An ELF section would be more conventional and ties the reader to one format. |
| Launcher provenance | Whether an executable should record a digest of its launcher so `inspect` can confirm it is an unmodified, released one. |

## 14. Phases

Each phase is one reviewable change and leaves every existing test passing. The handoff reports the commands that ran, their results, and anything that could only be inspected.

| Phase | Work | Done when |
| --- | --- | --- |
| 1 | `neura_runner`, `neura_launcher` with its descriptor, the container writer and reader, and `build --native` for sources, `.nir`, and `.nvr` with the default policy, on Linux x86-64. | `native-build`, `native-run`, and `native-container` pass. The launcher's size and the start-up time of Hello World, next to `neura run`, are measured and reported. |
| 2 | Recorded capabilities and limits, `NEURA_RUN_OPTIONS`, `--sealed`, `neura inspect`, and executables as inputs to `run`, `disassemble`, and `decompile`. | `native-policy` and `native-replay` pass. An image extracted with `inspect -o` equals the `neura build` output for every example. |
| 3 | `--static`, `--target`, `--launcher`, the install layout, and images with C imports. The documentation of section 11. | `native-install` and `native-c-import` pass. A static executable runs in an empty root file system. Any target other than Linux x86-64 is reported as verified under a named emulator or as unverified. |
