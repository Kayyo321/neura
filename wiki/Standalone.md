<!-- Generated from docs/lang/Standalone.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Native executables

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

`neura build --native` packages an NVR image with a launcher. The executable carries the program and the CPU runtime that executes it, so the file can run on a machine without an installed Neura toolchain. Both launcher flavors are verified on Linux x86-64.

## 1. Terminology

- "Must" states a requirement on the implementation. An option name, message, or record layout given here is fixed.
- Two terms distinguish the image from the executable and from foreign code:
  - **Standalone image.** [Overview](Overview) and [Status](Status) call a `.nvr` image standalone because it runs without its source. It still needs a runner.
  - **Native code.** `--deny-native`, `Std.DynamicForeign`, and the `NATV` section concern C code that a program calls ([Foreign interop](ForeignInterop)). `--native` compiles nothing to machine code when it builds the executable. At run time the launcher's faster tiers run lifted blocks through its own fixed native routines, and regions of them as machine code that the launcher generates in memory for that run from the image's graphs and its `LIFT` hints, where its native code generator is enabled (by default on x86-64 Linux), or else through a portable interpreter ([native compilation](NVM#native-compilation)). Nothing generated is written to the executable or to disk, and generated code calls no C code a program could name; whether the packaged program may call native code is the policy's native capability, which is granted unless `--deny-native` or `--deny-all` is given.

| Term | Meaning |
| --- | --- |
| Image | The `.nvr` bytes `neura build` writes ([NVR](NVR)). |
| Launcher | A prebuilt executable that contains the NVM library and the standard host providers, and no compiler. |
| Native executable | The output of `neura build --native`: a launcher, a payload, and a footer in one file. |
| Policy | The capabilities, run limits, and task settings recorded in a native executable when it is built. |
| Payload | The policy followed by the image. |

**Stages affected.** Only the output stage's packaging and the CLI, plus one program, the launcher. The lexer, parser, NeuraIR, IR optimization, IRtoNeuronConversion, the NVR writer and loader, and the NVM's execution contract do not change. No image byte changes.

## 2. Summary

```sh
neura build --native examples/hello-world/hello-world.nra -o hello
./hello arg1 arg2
```

`neura build --native` builds the image exactly as `neura build` does, then writes one file: a copy of an installed launcher, the image, and a small footer that says where the image is. When the file runs, the launcher finds the image inside its own file, validates it with the same loader as `neura run`, binds its host imports under the capabilities chosen at build time, and runs it.

A native executable is a packaging of the existing execution model, not a new one. The neuron graphs stay data in the image, and the same reference evaluator computes them. It is not faster than `neura run`, and it must not behave differently. What it removes is the need for an installed toolchain, a separate image file, and a command line that names both.

Because the image is embedded unchanged, everything that inspects images keeps working: `disassemble`, `decompile`, and `run` read a native executable's image directly, and the bytes they read are the bytes `neura build` would have written.

**Not provided.**

- Ahead-of-time compilation. `--native` does not turn neuron graphs into machine code; only the run-time code generator of tier 2 does, in memory, for one run.
- Bundled data. Files a program reads at run time are not embedded. A program that needs them ships them beside the executable and is built with a file-system grant.
- Bundled shared libraries (section 6.5).
- Payload compression, self-update, and installers. A packaged program has no installer or updater. The `neura` tool's own install, update, and uninstall commands concern the toolchain ([Install](Install)).
- A library form. Embedding a program in another application is the [embedding API](Backend#running-an-image).
- A GPU launcher (section 6.4).
- Mach-O and PE containers (section 5.4).

## 3. Rules

- **Packaging.** `--native` packages an image with a launcher, and the NVM in the launcher evaluates the image's graphs (section 2).
- **Image bytes.** The embedded image is byte for byte the image `neura build` writes for the same inputs and options. Every compiler stage runs unchanged (section 2). Its optional `LIFT` hints travel with it, so the launcher's faster tiers prove and use them as `neura run` does; `--no-lift-hints` leaves them out of the embedded image, from sources or from a built `.nvr`.
- **Build requirements.** `neura build --native` needs no compiler, linker, or other external tool. It reads an installed launcher and writes the output itself (section 7.3). A program with C imports still needs the C compiler those imports need ([C imports](Tooling#c-imports)).
- **Container.** The container is the launcher's bytes, then the payload, then a fixed 32-byte footer at the end of the file. The launcher's bytes are not modified (section 5.1).
- **Arguments.** Every command-line argument of a native executable belongs to the program. The launcher interprets none (section 6.2).
- **Capabilities.** Capabilities are chosen when the executable is built and recorded in its policy. The default grants every group, as `neura run` does by default. `--deny-*` options at build time remove groups, and `--allow-fs=prefix,...` narrows files (section 4.2). Nothing at run time can widen the policy (section 6.3).
- **Run-time options.** Settings that only restrict or observe a run come from the environment variable `NEURA_RUN_OPTIONS`, unless the executable was built with `--sealed`. Restricting includes lowering a limit and denying a group with a `--deny-*` option (section 6.4).
- **Reproducibility.** The output bytes are a function of the launcher file, the image, and the policy. They contain no timestamps, paths, or host names. Building the same source label with the same options gives identical bytes from any working directory and from a copy at any absolute path.
- **Reading back.** `run`, `disassemble`, and `decompile` accept a native executable in place of a `.nvr` file, and `neura inspect` reports the launcher, the policy, and the image (section 8).
- **Launcher flavors.** There are two. The default links the platform C library dynamically and everything else statically. `--static` selects a fully static launcher without native interop (section 7.2).
- **Targets.** Linux x86-64 is the verified target. Other ELF targets are selected with `--target`, and the build accepts them when a launcher for that target is installed; they are not verified. Mach-O and PE targets are not provided (section 5.4).
- **Native library target.** An image whose embedded native library was linked for a different target than the launcher is a build error (section 4.3).

The reasons for these rules are in the [design notes](https://github.com/Kayyo321/neura/blob/main/docs/design/Standalone.md).

## 4. Command line

### 4.1 Forms

```sh
neura build --native [-I source-root] [--no-optimize] [--no-source-map] [policy] [launcher] <file.nra>... -o <file>
neura build --native [--no-optimize] [--no-source-map] [policy] [launcher] <file.nir> -o <file>
neura build --native [policy] [launcher] <file.nvr> -o <file>
neura inspect <file> [-o <file.nvr>]
```

The first two forms accept everything `neura build` accepts, including the [C import options](Tooling#c-imports), and produce the same image. The third packages an existing image without rebuilding it: a team can build an image once, test, disassemble, or decompile exactly those bytes, and then ship them. `--no-optimize` and `--no-source-map` are rejected with a `.nvr` input, because the image is already built.

`-o` is required. The output is written only when every step succeeds, with mode 0755 on POSIX hosts. An existing output file is replaced, and a symbolic link at the output path is replaced rather than followed. The bytes go to a uniquely named temporary file in the output directory, which is flushed and renamed into place, and removed if any step fails.

### 4.2 Options

These are accepted only with `--native`.

| Option | Meaning |
| --- | --- |
| `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, `--deny-all` | Remove the group (every group for `--deny-all`) from the policy. Without any capability option the policy grants every group (section 6.3). |
| `--allow-fs`, `--allow-fs=prefix,...`, `--allow-net`, `--allow-env`, `--allow-clock`, `--allow-entropy`, `--allow-native` | Grant the group in the policy, which matters after a `--deny` option. Same meaning as for [`neura run`](Tooling#build-and-run-executables). |
| `--max-steps N`, `--max-frames N`, `--max-heap-cells N` | Record a run limit in the policy. Without one, the policy records the default of `neura run`. |
| `--max-tasks N`, `--task-quantum N`, `--max-queued-messages N`, `--max-pending-requests N`, `--parallel-workers N` | Record a task setting in the policy ([Concurrency](Concurrency#103-limits)). Without one, the policy records the default of `neura run`: 65,536 tasks, a quantum of 1,024, 1,048,576 queued messages, 4,096 pending requests, and 1 parallel worker. `--parallel-workers` is at most 1,024. The settings matter only to an image with tasks or a parallel map; they are recorded for every image. |
| `--sealed` | The executable ignores `NEURA_RUN_OPTIONS` (section 6.4). |
| `--static` | Use the static launcher (section 7.2). |
| `--target <triple>` | Use the launcher for another ELF target, such as `aarch64-linux-gnu`. The default is the target of the `neura` that is running. Only Linux x86-64 is verified. |
| `--launcher <file>` | Use this launcher file instead of searching (section 7.3). |

Capability options apply in command-line order: `--deny-all --allow-net --allow-fs=data` records exactly the network and the files below `data`, and `--allow-net --deny-all` records nothing. `--allow-fs=prefix,...` narrows files to the prefixes, and several lists accumulate until `--deny-fs` or `--deny-all` clears them. A plain `--allow-fs` records every path and drops any list before it, so `--allow-fs=a --allow-fs` records every path and `--allow-fs --allow-fs=a` records only `a`.

A file-system prefix is recorded as written. A relative prefix is resolved when the executable starts, against its working directory, as `neura run` resolves one.

With `--static`, the default policy grants every group except native code, because the static launcher has no native interop. Asking for native code with `--allow-native` is the error in section 4.3, and a `--deny-native` after it cancels the request.

### 4.3 Errors and notes

Errors are printed as `neura: error: <text>` and exit with status 1. No output file is written.

| Case | Text |
| --- | --- |
| An option of section 4.2 without `--native` | `<option> applies only with --native` |
| A `.nvr` input without `--native` | `build takes sources or one .nir file; packaging a .nvr needs --native` |
| `--no-optimize` or `--no-source-map` with a `.nvr` input | `<option> cannot be applied to an image that is already built` |
| No launcher found | `--native: no launcher for <triple> in <directory>; pass --launcher or install one` |
| No launcher found, and the target is a macOS triple (`*-apple-darwin`) | `--native: Neura has no native launcher for macOS (<triple>); native executables are built for Linux targets only. Run the image with 'neura run' instead` |
| The file is not a launcher | `--native: <file> is not a Neura launcher` |
| The image needs a newer runtime | `--native: the image is NVR 1.<m>; this launcher runs up to 1.<n>` |
| The image's embedded native library was linked for a different target than the launcher | `--native: the image's native library is for <triple>; the launcher is for <triple>` |
| `--static` with an image that imports `neura.ffi.*` or has a `NATV` section, or with an `--allow-native` that no later `--deny-native` or `--deny-all` cancels | `--static: the image uses native interop, which the static launcher does not provide` |
| `--target` or `--launcher` given twice with different values | `conflicting <option> values` |
| `--target` names a different target than the launcher's descriptor | `--target: launcher is for <triple>, expected <triple>` |
| `--static` with a launcher whose descriptor is the default flavor | `--static: <file> is a default launcher` |
| `--target` is not a single directory name of letters, digits, periods, underscores, and hyphens | `--target expects an ELF target triple` |
| A `.nvr` input with other input files | `a .nvr input must be the only input file` |
| `-I` with a `.nvr` input | `-I applies only to source inputs` |
| A `.nvr` image of 4 GiB or more | `native executable: image must be smaller than 4 GiB` |
| `--native` without `-o` | `build --native requires -o <file>` |
| `--native` with a command other than `build` | `--native applies only to build` |
| An option without its value | `expected a value after <option>` |
| A limit that is not a positive integer | `expected a positive integer after <option>` |
| `--max-frames` above 4,294,967,295 | `--max-frames is too large` |
| `--parallel-workers` above 1,024 | `--parallel-workers is at most 1024` |
| `--allow-fs=` with an empty entry | `--allow-fs= expects a comma-separated list of path prefixes` |

One note is printed to standard error without failing the build: for each capability group whose services the image imports and the policy does not grant, `neura: note: the image uses <group> services, which are denied; they fail with CapabilityDenied`. Because every group is granted by default, a note appears only for a group that a `--deny` option or `--static` removed. The executable is still valid: a denied service binds and fails with error 14, as under `neura run`. Granting the group again removes exactly its note.

`neura inspect` reports a file that is not a native executable as `neura: error: <file>: not a native executable (no NEURAEXE footer)` and exits with status 1. A file that holds the launcher descriptor (`NEURA-LAUNCHER-1`) but no footer, such as a bare or stripped launcher, gets the message of section 6.1 instead: `this is a Neura launcher with no program; build one with neura build --native`. A damaged container is reported as `native executable: <reason>`.

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

All integers are fixed-width and little-endian, and `text`, `bytes`, and `vector<T>` are encoded as in [NVR](NVR#encoding-and-compatibility).

**Footer.**

| Field | Type | Value |
| --- | --- | --- |
| Payload offset | u64 | Absolute offset of the payload; equal to the launcher's size |
| Payload size | u64 | Bytes of policy and image together |
| Payload checksum | u32 | IEEE CRC-32 of the payload bytes |
| Container version | u32 | 2. Version 1 is the same layout without the task settings of the policy. |
| Magic | 8 bytes | ASCII `NEURAEXE` |

**Policy.**

| Field | Type | Value |
| --- | --- | --- |
| Flags | u32 | Bit 0: sealed. Other bits are zero. |
| Capabilities | u32 | Bit 0 files, 1 network, 2 environment, 3 clock, 4 entropy, 5 native. Other bits are zero. |
| File-system prefixes | `vector<text>` | Empty when files are not granted or every path is allowed. Each prefix is non-empty, valid UTF-8 text. |
| Maximum steps | u64 | |
| Maximum frames | u32 | |
| Maximum heap cells | u64 | |
| Maximum tasks | u64 | Version 2 only, as are the four fields below. Not zero. |
| Task quantum | u64 | Polls per turn. Not zero. |
| Maximum queued messages | u64 | Not zero. |
| Maximum pending requests | u64 | Not zero. |
| Parallel workers | u32 | From 1 to 1,024. |
| Image | `bytes` | The image. An image of 4 GiB or more is a build error. |

`neura build --native` writes version 2. The five task settings sit between the maximum heap cells and the image, 36 bytes in all, so the default policy, which has no file-system prefix, is 72 bytes with the image's length word. A version 1 policy ends its fixed fields at the maximum heap cells.

The checksum detects truncation and accidental damage. It is not a signature and gives no protection against deliberate modification; an operator who needs that signs the whole file with their platform's tools.

### 5.2 The launcher descriptor

A launcher carries one descriptor so that `neura build` can check it without running it, which a build for another target could not do. The descriptor is a constant in the launcher's data that begins with the 16 bytes `NEURA-LAUNCHER-1`, followed by `triple:text`, `flavor:u8` (0 default, 1 static), `nvr_major:u16`, `nvr_minor:u16` (the newest image version it loads), `container_version:u32` (the version it writes and the newest it reads), and `toolchain_version:text`. `neura build` finds it by searching the launcher file for those 16 bytes, and rejects a file in which they occur zero times or more than once, and a launcher whose container version is not the one this `neura` writes.

### 5.3 Reading a container

A reader takes the last 32 bytes, checks the magic and the container version, checks that the payload lies inside the file and ends exactly at the footer, and verifies the checksum, before it decodes the policy. It bounds every count by the bytes that remain before allocating, as the [NVR loader](NVR#writer-and-loader-requirements) does. Reserved bits that are set, bytes left over after the image, an image length that does not end at the footer, an empty or non-UTF-8 file-system prefix, and a payload that starts inside the launcher or is shorter than the fixed policy fields of its version are errors. The image then goes through the unchanged NVR loader, so an invalid image fails before any host import is invoked.

A reader accepts versions 1 and 2, so an executable with a version 1 container, which has no task settings, still runs:

| Container | Result |
| --- | --- |
| Version 2 | The policy's task settings are used. A setting of zero, or more than 1,024 parallel workers, is `native executable: invalid task settings`. |
| Version 1 | The policy has no task settings. They take the defaults of `neura run`, and `neura inspect` marks them `(defaults: container version 1 records none)`. |
| Version 0, or 3 and above | `native executable: unsupported container version <n>` |

The version decides how many policy bytes precede the image, so a version 1 policy under a version 2 footer, or the reverse, does not line up and is refused.

### 5.4 Consequences of the layout

ELF loaders map only the segments the program headers name and ignore bytes after them, so the appended payload does not disturb the launcher and the launcher's bytes stay identical to the installed file. Two consequences follow:

- `strip`, and any tool that rewrites the ELF file, discards the payload. The result is a bare launcher, which reports that it has no program (section 6.1). Launchers are installed already stripped; a native executable must not be stripped again.
- A tool that appends its own data after the footer hides the footer. Mach-O code signing and PE Authenticode both have rules about trailing data, so a native executable is not provided for those formats, which would need a different container.

## 6. Run-time behavior

### 6.1 Start-up

In order:

1. The launcher opens its own file and reads the container (section 5.3). It reads the footer and the payload, not the launcher's own bytes. On Linux it uses the first of these that can be opened: `/proc/self/exe`, then the path the kernel recorded when it started the executable (`AT_EXECFN`). A root file system without procfs therefore works. The program name is never used to find the file: whoever starts the process chooses it, and it could name a different executable with a different policy. When none can be opened, the start-up error is `could not open the executable file (tried <list>)`, naming each candidate.
2. Unless the policy is sealed, it reads `NEURA_RUN_OPTIONS` (section 6.4).
3. It loads and validates the image, binds the image's imports to the standard providers under the policy's capabilities, and provides the image's embedded native library, if any, to the native provider.
4. It runs the entry function with the program's arguments.

Container and load failures are printed as `<program name>: error: <text>` and exit with status 1, the status `neura run` uses for load and bind errors. A launcher with no payload prints `this is a Neura launcher with no program; build one with neura build --native`.

### 6.2 Arguments and exit status

- The arguments after the program name are passed to `main` as UTF-8 text, in order, exactly as the arguments after `--` are under `neura run`. `--help`, `--`, and anything that looks like a runner option reach the program untouched, because the launcher interprets no argument. An argument that is not valid UTF-8 is a start-up error.
- The exit status is the `ExitCode` that `main` returns, or the status given to `Std.Program.Process.exit`.
- A trap prints the diagnostic `neura run` prints, with a source location when the image has a source map, and exits with the status of its class: 136, 139, 134, 132, 137, or 152 ([Errors](Errors)).
- When the run ends, by completion, trap, or exit, the launcher closes every file and socket the program left open, as the runner does.

### 6.3 Capabilities and limits

The policy is the whole grant, and by default it grants every group. A service outside it still binds, and every call returns `CapabilityDenied` (error 14), so the same program behaves the same way under `neura run` with the matching `--deny` option.

A native executable built without a `--deny-*` option can do whatever the user who runs it can: read and write that user's files, use the network, read the environment, and call native libraries, which run outside Neura's memory-safety and determinism guarantees. Build with `--deny-*` options to give it less, or run it with `NEURA_RUN_OPTIONS` set to a `--deny-*` option (section 6.4) to give a particular run less.

Under `neura run`, the person running an image decides what it may touch. A native executable has no runner command line (section 6.2), so the builder decides, and `neura inspect` shows anyone what was decided. The operating system remains the security boundary for whoever starts the file, as for any program; the policy bounds what the Neura program inside can ask for, which contains a bug or a compromised dependency.

Run limits and task settings work as under `neura run` with the same values: exhaustion of a limit traps at the operation that would exceed it, and the recorded quantum gives the interleaving that `neura run --task-quantum` gives with that value.

### 6.4 `NEURA_RUN_OPTIONS`

An operator sometimes needs to bound or record a deployed program without rebuilding it. The variable holds options separated by spaces or tabs. There is no quoting, so a path with a space cannot be given.

| Option | Effect |
| --- | --- |
| `--max-steps N`, `--max-frames N`, `--max-heap-cells N` | The run uses the smaller of this value and the policy's. A limit can be lowered, never raised. |
| `--max-tasks N`, `--max-queued-messages N`, `--max-pending-requests N` | The run uses the smaller of this value and the policy's, as for the limits above. |
| `--parallel-workers N` | The run uses the smaller of this value and the policy's. Fewer workers change only wall time: a parallel map means the same under any number of them. |
| `--task-dump`, `--task-dump=text`, `--task-dump=json` | Prints every task when one traps, as for `neura run` ([Concurrency](Concurrency#141-task-dump)). |
| `--trace-events <file>`, `--trace-wall-time` | Writes one JSON line per scheduler event, with wall-clock times when asked ([Concurrency](Concurrency#142-profiling-and-trace-events)). |
| `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, `--deny-all` | Removes the group, or every group, from the policy for this run, as the same option does for `neura run`. A group the policy does not grant stays denied. Files with a prefix list lose the list and all files with it. |
| `--no-gpu`, `--gpu=off` | Accepted as requests for the launcher's CPU execution mode. The launcher has no GPU backend. |
| `--stats`, `--profile` | As for `neura run`, on standard error. |
| `--tier=0`, `--tier=1`, `--tier=2`, `--tier-stats` | Lower the execution tier, or report the tiers, as for `neura run` ([execution tiers](NVM#execution-tiers)). The variable cannot raise the tier: `--tier=2` keeps the default. The tier changes only speed, so a sealed executable that ignores the variable still honors `NEURA_TIER=0` or `NEURA_TIER=1` in the environment. |
| `--jit=off`, `--jit=on`, `--jit=auto` | Run tier 2 regions on the interpreter, as native code (an error at start-up where the platform cannot), or as the platform's default, as for `neura run` ([native compilation](NVM#native-compilation)). Native code changes only speed, so a sealed executable still honors `NEURA_JIT` in the environment. `--deny-native` does not turn it off. |
| `--record <file>` | Writes the run's host trace ([NVM](NVM#determinism-limits-and-observability)). |
| `--replay <file>` | Answers every host request from the trace. No service runs, so the policy's capabilities are not used. |

- An unset or empty variable has no effect.
- Any other option, including every `--allow-*` (the variable only restricts), `--task-quantum`, `--virtual-time`, `--gpu=auto`, and `--gpu=always`, is the start-up error `NEURA_RUN_OPTIONS: <option> is not accepted by a native executable`. The variable cannot widen the policy (section 6.3). `--task-quantum` is refused because the quantum changes how tasks interleave and so the order of their output, and `--virtual-time` because it changes what the program reads from the clock; the variable admits only settings that restrict or observe a run. The quantum of a native executable is the one its policy records. A missing or non-positive number after a limit is `NEURA_RUN_OPTIONS: expected a positive integer after <option>`, a missing path after `--record`, `--replay`, or `--trace-events` is `NEURA_RUN_OPTIONS: expected a value after <option>`, a task dump form other than `text` or `json` is refused like any unknown option, and giving both `--record` and `--replay` is `NEURA_RUN_OPTIONS: --record and --replay cannot be combined`.
- A sealed executable ignores the variable entirely. Sealing suits programs that run with more privilege than the user who sets their environment.
- A non-sealed executable trusts whoever sets its environment. `--record <file>` and `--trace-events <file>` create or replace any file the process may write, and `--replay <file>` answers host requests from a file of that person's choosing. A trace written by `--record` goes through a new, uniquely named temporary file in the destination directory (created exclusively, never through a symbolic link, and flushed) and renamed over the destination, so a symbolic link at the destination or at a guessed temporary name is replaced or ignored rather than followed. That protects other files from a planted link; it does not restrict where the trace goes. The events file of `--trace-events` is written as the run goes, so it is opened in place and truncated; a symbolic link at that path is refused with `cannot write <file>: it is a symbolic link` instead of followed. Build with `--sealed` when this trust is not acceptable.
- A trace recorded this way replays under `neura run --replay` with the extracted image, and a trace recorded by `neura run` replays in the executable, because both run the same image on the same NVM.

- The [concurrency](Concurrency#143-command-line) and [GPU](GpuExecution#14-command-line) pages describe further run options. A native executable accepts such an option through this variable only when it restricts or observes a run.
- The task limits and the worker count apply to images with tasks or a parallel map and are accepted for any image. `--no-gpu` and `--gpu=off` are accepted because they request the CPU evaluation the launcher always uses; `--gpu=auto` and `--gpu=always` are rejected, since launchers have no GPU backend. A sealed executable keeps exactly the settings its policy records, the quantum included.
- The launcher answers `SIGQUIT` with a task dump on standard error, as `neura run` does, whatever the variable holds. The dump shares the runner's code with `neura run`.

### 6.5 Native libraries and bundled files

An image with C imports carries its linked library in `NATV` ([Foreign interop](ForeignInterop#c-imports)). The executable loads it on the first native call, and only when the policy grants the native capability; otherwise that call fails as it does under `neura run --deny-native`. Libraries a program opens by path with `SymbolLookup.library_lookup` are not bundled. They must exist on the machine that runs the executable. Only the statically linked library of C imports travels in the image, and files a program reads at run time are not embedded.

## 7. Launchers

### 7.1 What a launcher is

A launcher is `src/launcher/main.cpp` linked with `neura_nvm` and `neura_nvr` and nothing from the compiler or decompiler. [CMakeLists.txt](https://github.com/Kayyo321/neura/blob/main/CMakeLists.txt) keeps the NVM free of compiler code for this purpose. Its `main` performs section 6.1 through the same public interfaces `neura run` uses: `nvr::load`, `nvm::bind`, `nvm::run`, `nvm::record`, and `nvm::replay` ([Backend](Backend#running-an-image)). The CLI's `Host` structure and its trap reporting live in a small library, `neura_runner`, that both `neura` and the launcher link, so the two cannot drift apart.

### 7.2 Flavors

| Flavor | Links | Use |
| --- | --- | --- |
| Default | The platform C library dynamically; libffi, the C++ runtime, and everything else statically | Any image. Runs wherever the target's C library is present. |
| Static (`--static`) | Everything statically. The native provider is compiled out, and every `neura.ffi.*` service is registered to fail with code 13 (`Unsupported`). Host names are not resolved. | Images without native interop, in minimal containers and images with no C library. |

Dynamic loading needs a dynamic C library, which is why native interop and a fully static file exclude each other. Name resolution is excluded for the same reason: in a static program the C library's resolver loads, at run time, the name-service libraries of the C library it was linked with, which the machine that runs the executable may not have. A static executable therefore connects and listens on numeric IPv4 and IPv6 addresses and on `localhost`, and any other host name fails with `Unsupported`. A program that must resolve names uses the default flavor.

### 7.3 Build, install, and search

| Target | Role |
| --- | --- |
| `neura_runner` | Static library: the runner's host set-up, trace and output files, and trap reporting. Links `neura_nvm`. |
| `neura_launcher` | Executable. Links `neura_runner`. Installed stripped. |
| `neura_launcher_static` | Executable, built when the CMake option `NEURA_STATIC_LAUNCHER` is `ON`, or `AUTO` and a static C library is found. |
| `neura` | Links `neura_runner`. Provides `build --native` and `inspect`. |

`cmake --install` places launchers at `<prefix>/lib/neura/launchers/<triple>/neura-launcher` and `neura-launcher-static`, where `<triple>` is the form `nvm::native_target_triple()` returns, such as `x86_64-linux-gnu`. A `neura` in a build directory directly inside the checkout finds them below that build directory, as it finds the standard library there.

`neura build --native` uses `--launcher <file>` when given. Otherwise it looks in the launcher directory of the `neura` that is running, under the triple from `--target` or its own. A launcher for another target comes from building Neura for that target and copying its launcher directory; the image needs no rebuilding, because an image moves between hosts unchanged ([NVM](NVM#toolchain-and-distribution)). With an explicit launcher and no `--target`, its descriptor determines the target and flavor. An explicit target must match the descriptor, and `--static` requires a static descriptor. Target strings must be single directory names made of letters, digits, periods, underscores, and hyphens.

**Measured sizes and start-up time.** A Release build with GCC 14.2.0 on Linux x86-64 produced a stripped default launcher of 2,269,680 bytes and a stripped static launcher of 3,024,208 bytes. The packaged `examples/hello-world/hello-world.nra` is 2,282,455 bytes with the default launcher and 3,036,983 bytes with the static one. On an AMD Ryzen 7 9800X3D pinned to CPU 7, the median of 21 paired launches after warming both modes was 0.66 ms for the default-launcher executable and 0.89 ms for `neura run --no-gpu` on the same image. The static Hello World ran in an empty root file system, one holding only its executable and no procfs, and in a root with procfs mounted. The launcher descriptor carries the version of the `neura` that built the launcher: `0.1.0-dev` for a build from a checkout and `0.1.0` for a release build (see [Install](Install)).

## 8. Reading an executable back

- `neura run <executable> [-- args]`, `neura disassemble <executable>`, and `neura decompile <executable>` accept a native executable wherever they accept a `.nvr` file. They recognize it by the footer and read the embedded image. `neura run` applies its own command line's capabilities and limits, not the executable's policy: it is `neura run` on that image.
- `neura inspect <executable>` prints the launcher descriptor, the policy, and a summary of the image. With `-o <file.nvr>` it also writes the image.

```text
native executable: hello
launcher: neura <version>, x86_64-linux-gnu, default, runs NVR up to 1.6
capabilities: console, process exit, files (/var/lib/hello), network
limits: steps 100000000, frames 10000, heap cells 67108864
tasks: at most 65536, quantum 1024, queued messages 1048576, pending requests 4096, parallel workers 1
run options: accepted
image: 48211 bytes, NVR 1.2, 31 graphs, source map present, native library absent
```

The `tasks:` line gives the policy's task settings in the order the container stores them. For a version 1 container it ends with ` (defaults: container version 1 records none)`.

The decompiler's evidence rules are unchanged. A reconstruction of an executable's image rests on that image's graphs and tables, and the launcher contributes nothing to it ([Decompiler](Decompiler#evidence)).

## 9. Contract

**What must be identical.** For the same image, arguments, standard input, capabilities, limits, and host responses, a native executable and `neura run <image> -- <arguments>` must agree on:

- every byte written to standard output, and the trap diagnostic on standard error;
- the exit status;
- every host request, in order, with the same arguments, and so a trace written by `--record`, byte for byte;
- the step count and peak heap cells that `--stats` reports.

This follows from construction: the image is the same bytes (section 2) and both programs drive the same NVM library with the same providers (section 7.1).

**What may differ.** Start-up time; the text of container errors, which `neura run` cannot have; and the program name in runner messages.

**What stays in the circuit and what crosses the boundary.** Nothing moves. The circuit is the image's graphs, the launcher is an NVM, and host effects cross the same boundary through the same imports. The policy is runner configuration, as the options of `neura run` are; no image section records it, and an image's behavior does not depend on which container holds it.

## 10. Verification

CTest entries, with the evidence each gives:

| Test | Check | Kind |
| --- | --- | --- |
| `native-build` | Sources, a `.nir`, and a `.nvr` give the same executable for the same options, from another working directory and from a copy at another path. Building twice gives identical bytes. `inspect -o` returns the bytes `neura build` writes. `inspect` of a file that is not a native executable. Every error and note of section 4.3. The default launcher is CPU-only (no Vulkan dependency or text). For an image with tasks: the default settings, recorded task settings read back by `inspect`, a recorded quantum of 1 giving the interleaving of `neura run --task-quantum 1`, `NEURA_RUN_OPTIONS` lowering a recorded limit and never raising one, a sealed executable keeping its recorded limit, a JSON task dump on a trap, trace events with wall-clock times, and the refusal of `--task-quantum`, `--virtual-time`, and an unknown dump form. | Structural, reproducibility, end to end |
| `native-run` | Every runnable example, every driver in `tests/backend/examples`, and every trap program (the test prints how many): standard output, standard error, and status equal those of `neura run`. Arguments such as `--help`, `--`, `--max-steps`, an empty string, and one with spaces reach `main`. | End to end, differential |
| `native-policy` | Every capability granted with no option, each denied by its `--deny-*` option and by `--deny-all`, and its denial note appearing only then; `--allow-*` after `--deny-all`; the order of options, including both files corner cases, read back with `inspect`; path prefixes narrowing the default; recorded limits; `NEURA_RUN_OPTIONS` lowering a limit, denying each group as an executable built with that `--deny-*` behaves, refusing every `--allow-*` and `--task-quantum`, and rejecting malformed values; a sealed executable ignoring the variable, `--deny-*` options included. | End to end |
| `native-replay` | A trace recorded by the executable replays under `neura run`, and the reverse, with identical trace bytes. A symbolic link planted at the destination and at the old temporary name is not followed, and the trace still replays. | Replay, hardening |
| `native-container` | Every truncation of the payload and footer, a wrong checksum, reserved bits, trailing bytes, container versions 0 and 3, a bare launcher, a stripped executable, and crafted payloads (empty or non-UTF-8 file-system prefix, a prefix count or length beyond the payload, an image length that lies, a zero-length payload, a payload offset inside the launcher, a zero task setting, 1,025 parallel workers, a version 1 policy under a version 2 footer and the reverse) each fail at start-up with status 1 and before any host import is invoked. A version 1 container runs, and so does one that records 1,024 workers. | Exhaustive (truncations), structural |
| `native-install` | From an installed toolchain, an executable is built, copied to an empty directory with no toolchain on the path, and run. With `--static`, it runs with no shared library available, in a root holding only the executable (no procfs) and in a root with procfs. Those two runs need an unprivileged user namespace; on a host that forbids one the test runs the static executable alone in its directory and prints `empty-root checks NOT RUN` as a warning, or fails when `NEURA_USERNS_REQUIRED=1` is set, as [continuous integration](ContinuousIntegration) sets it. When no static launcher was built, the test prints `static launcher: NOT BUILT, static checks not run` as a warning. | End to end |
| `native-c-import` | An image with a `NATV` section runs with the default policy and fails as documented under `--deny-native`; the static launcher's default policy omits native code, and a static executable refuses a host name while connecting to a numeric address and to `localhost`; a library for another target is a build error. | End to end on Linux x86-64 |

On a platform without a launcher, CMake registers none of these except `native-replay`, without its launcher comparison, and prints a status line naming them.

The existing image tests are the evidence for the image itself. These tests establish only that packaging preserves it.

No test sends `SIGQUIT` to a native executable.
