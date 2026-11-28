# Native executables: design notes

This page records why the design of native executables is as it is, for contributors. [Native executables](../lang/Standalone.md) is authoritative for behavior: option names, messages, the container layout, and run-time rules are defined there, and this page adds no rule to them.

## Decisions

| ID | Decision | Rationale |
| --- | --- | --- |
| N1 | `--native` packages. The executable is a launcher plus an image; the NVM evaluates the image's graphs. | A second evaluator, such as one that compiles neuron graphs to machine code, would have to be shown equal to the reference evaluator, as the [GPU backend](../lang/GpuExecution.md) must for its device. Packaging reuses the execution model, so the executable does not behave differently from `neura run`. What it removes is the installed toolchain, a separate image file, and a command line that names both. Compiling circuits to machine code needs its own design. |
| N2 | The embedded image is byte for byte the image `neura build` writes for the same inputs and options. Every compiler stage runs unchanged. | Everything that inspects images keeps working on an executable, and the existing image tests are the evidence for the image. The packaging tests only have to establish that packaging preserves it. |
| N3 | `neura build --native` needs no compiler, linker, or other external tool. It reads an installed launcher and writes the output itself. | Building a program does not depend on the machine having a toolchain beyond Neura. A C import needs the C compiler that linking its library already needs. |
| N4 | Packaging copies the launcher and appends the payload and fixed footer. It clears any template Authenticode metadata before writing the PE overlay. | ELF and PE loaders map specified file ranges, and appending the payload after those ranges preserves an unsigned launcher's bytes. Authenticode includes the PE overlay in the image hash, so the output is signed after packaging. Its Security directory locates the certificate table after the footer. |
| N5 | Every command-line argument of a native executable belongs to the program. The launcher interprets none. | A deployed program has no runner command line. If the launcher took options, `--help` or `--` would reach the program changed. Run-time settings come from the environment (N7). |
| N6 | Capabilities are chosen when the executable is built and recorded in its policy. The default grants what `neura run` grants by default. Nothing at run time can widen the policy. | Under `neura run`, the person running an image decides what it may touch. A native executable has no runner command line, so the builder decides, and `neura inspect` shows anyone what was decided. The default matches `neura run` so the same program behaves the same under both. |
| N7 | Run-time settings that only restrict or observe a run come from one environment variable, `NEURA_RUN_OPTIONS`, unless the executable was built with `--sealed`. | An operator sometimes needs to bound or record a deployed program without rebuilding it. Restricting cannot widen the build-time policy. The quantum changes how tasks interleave and so the order of their output, and `--virtual-time` changes what the program reads from the clock, so neither is a restriction or an observation. Sealing suits programs that run with more privilege than the user who sets their environment. |
| N8 | Output bytes are a function of the launcher file, the image, and the policy. There are no timestamps, paths, or host names. | Execution and artifacts are deterministic, so the same inputs give the same file and a build can be checked by comparing bytes. |
| N9 | The toolchain reads the image back out. `run`, `disassemble`, and `decompile` accept a native executable in place of a `.nvr` file, and `neura inspect` reports the launcher, the policy, and the image. | Decompilation and verification rest on the image's graphs and tables. The container adds no evidence about behavior, so reading it back must return the original bytes. |
| N10 | The default launcher supports native interop. Linux uses the system C library, and Windows uses the static CRT and system DLLs. `--static` selects the Linux launcher without native interop. | The Linux static flavor can run in a minimal file system because it avoids the C library's dynamic loader and name-service libraries. On Windows, `LoadLibraryW` is a system service available to the default PE. |
| N11 | Linux x86-64 is verified, and Win64 PE qualification is pending. `--target` selects an installed launcher by triple. | Each supported host qualifies its own installed launch and image identity (N4). |
| N12 | An image whose embedded native library was linked for a different target than the launcher is a build error. | The library would not load in that launcher. Failing at build time reports it before the file is shipped. |

## Questions not settled

| Area | Question |
| --- | --- |
| Sealing by default | Whether accepting the variable should be the opt-in, with `--sealed` as the default. |
| `neura run <executable>` | Whether it should offer a way to apply the executable's policy instead of the runner's command line. |
| Descriptor location | Searching for the descriptor's 16 bytes is simple and format-independent. An ELF section would be more conventional and ties the reader to one format. |
| Launcher provenance | Whether an executable should record a digest of its launcher so `inspect` can confirm it is an unmodified, released one. |

## Alternatives not taken

- **Ahead-of-time compilation of neuron graphs.** It would add a second evaluator whose results must be shown equal to the reference evaluator's (N1).
- **A GPU launcher.** A flavor that links `neura_gpu` would need rules for a runtime-only distribution of the GPU backend; launchers are CPU-only.
