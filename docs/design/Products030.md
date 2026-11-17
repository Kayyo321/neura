# Neura 0.3.0: SDK tools and the direct runtime

This note fixes how the single `neura` executable becomes a set of sibling tools and a small dispatcher, before the split is made. It covers the binaries and what each links, how `neura <command>` finds the tool that does the work, the install components, the size and dependency budget, and the order of the work. The resident runtime service (`neurad`) and the component-aware archives, links and updater are later milestones; this note leaves a seam for each and says where.

Status: *proposed* on 2026-11-17. The owner was away and had asked for autonomous work toward 0.3.0, so the rows await the owner's confirmation; "proposed" stays until then. A row the owner changes is changed here and in the implementation in one later change.

The split changes no compiler semantics, no image byte and no runtime behavior. Every command and option that `neura` 0.2 accepts keeps its meaning, its output and its exit status, whether it is typed as `neura <command>` or as the direct tool.

## Products and components

The two products are **Neura-sdk** and **Neura-runtime**; archives and packages use the lowercase identifiers `neura-sdk` and `neura-runtime`. Both are built from this repository as install components of one CMake project.

| Component | CMake component | Holds | In Neura-sdk | In Neura-runtime |
| --- | --- | --- | --- | --- |
| Shared execution and delivery core | `core` | `neura`, `neura-run`, `neura-inspect`, `neura-doctor`, `neura-update`, `neura-uninstall`, `share/doc/neura/LICENSE` | yes | yes |
| SDK tools | `sdk` | `neurac`, `neura-test`, `neura-debug`, `neura-lsp`, `neura-fmt`, `neura-doc`, `neura-cbind`, `neura-decompile`, `neura-asm`, `neura-disasm`, `neura-explain`, `neura-tags`, the standard library sources and the native launchers | yes | no |
| Runtime service | `service` | `neurad` and service-manager files; empty in this milestone | no | yes, from the service milestone |

`cmake --install build` installs every component, which is the SDK layout of 0.2 plus the new tools. `cmake --install build --component core` installs the runtime-only set, and adding `--component sdk` to the same prefix completes the SDK. The option `NEURA_SDK` (default `ON`) can be turned off to configure a runtime-only build: the compiler, decompiler, editor and libclang targets are then not defined at all, so nothing in the `core` component can depend on them.

## Binary map

Each row is one executable in `bin/`. "Links" names the CMake libraries of this repository the executable is built from; every executable also links the C++ runtime and the C library. The unified command is what `neura` accepts; the direct form is what the tool accepts when it is run by itself.

| Executable | Component | Unified commands | Direct form | Links |
| --- | --- | --- | --- | --- |
| `neura` | core | `--version`, `version`, `--help`, `help`, `capabilities --json` (built in); every other command is forwarded | | `neura_tool` only (registry, version, process forwarding) |
| `neura-run` | core | `run` | `neura-run [run] ...` | `neura_cli_options`, `neura_cli_run`, `neura_runner`, `neura_nvm` (with `src/jit` and tier 3), `neura_lift`, `neura_circuit`, `neura_nvr`, `neura_kernels`, `neura_gpu` when built |
| `neura-inspect` | core | `inspect`, `inspect-image` | `neura-inspect [inspect] ...`, `neura-inspect inspect-image ...` | `neura_cli_options`, `neura_runner`, the container and descriptor readers, the update notice |
| `neura-doctor` | core | `doctor` | `neura-doctor [doctor]` | `neura_lifecycle_tools`, plus the libclang probe of the C import stage (see "Dependency budget") |
| `neura-update` | core | `update` | `neura-update [update] ...` | `neura_lifecycle_tools` |
| `neura-uninstall` | core | `uninstall` | `neura-uninstall [uninstall] ...` | `neura_lifecycle_tools` |
| `neurac` | sdk | `build`, `check`, `emit-ir` | `neurac [build] ...`, `neurac check ...`, `neurac emit-ir ...` | `neura_cli_compile`, `neura_compiler`, the container writer of `neura_runner` |
| `neura-test` | sdk | `test` | `neura-test [test] ...` | `neura_cli_compile` and `neura_cli_run`: it compiles and runs each test in one process, as 0.2 does |
| `neura-doc` | sdk | `docs` | `neura-doc [docs] ...` | `neura_cli_compile`, `neura_compiler` |
| `neura-cbind` | sdk | `c-bindings` | `neura-cbind [c-bindings] ...` | `neura_cli_compile`, `neura_compiler` |
| `neura-decompile` | sdk | `decompile` | `neura-decompile [decompile] ...` | `neura_decompiler` (which links `neura_compiler`) |
| `neura-asm` | sdk | `assemble` | `neura-asm [assemble] ...` | `neura_decompiler` |
| `neura-disasm` | sdk | `disassemble` | `neura-disasm [disassemble] ...` | `neura_decompiler` |
| `neura-explain` | sdk | `explain`, `--explain` | `neura-explain [explain] ...` | the embedded diagnostic pages |
| `neura-tags` | sdk | `tags` | `neura-tags [tags] ...` | the tag registry of NeuraIR |
| `neura-debug` | sdk | `debug --stdio` | `neura-debug [debug] --stdio` | `neura_compiler`, `neura_runner`; keeps tier 0 and JIT off for every debug session |
| `neura-lsp` | sdk | `lsp --stdio` | `neura-lsp [lsp] --stdio` | `neura_compiler` |
| `neura-fmt` | sdk | `fmt ...` | `neura-fmt [fmt] ...` | `neura_compiler` (the formatter and the parser) |
| `neurad` | service | `runtime ...` (seam) | | not built in this milestone |

The bracketed word is optional. When the first argument of a tool is one of its own command names, it selects that command; otherwise the tool runs its first command. `neurac app.nra -o app.nvr` is therefore `neura build app.nra -o app.nvr`. A file whose name is exactly a command name is passed after the command, as in `neurac build check`. As its only argument, `--help` prints the tool's commands and `--version` prints the same line as `neura --version`; after a command word both keep their 0.2 meaning, which for most commands is an unknown option.

Every tool reports errors with the prefix `neura:`, as 0.2 does, so a command prints the same bytes in both forms. Usage text that names a command keeps the unified form.

The commands `new`, `dev`, `contracts` and `package` of the application platform, and the separate tools that will serve them, are not part of this milestone. `neura` rejects them as unknown commands until they exist.

## Dispatch rules

`neura` holds a fixed registry: each unified command, the executable that serves it, the command word it passes, and the component it belongs to. The same registry generates `neura --help` for the tools, the component checks of `neura doctor`, and the parity tests.

| Rule | Status |
| --- | --- |
| `neura` resolves its own real executable once, following symbolic links (`/proc/self/exe` on Linux, `_NSGetExecutablePath` on macOS, else `argv[0]` and `PATH` as in 0.2), and takes that file's directory as the installation's `bin/`. | proposed |
| The tool for a command is looked up in that directory only. `neura` never searches the working directory or `PATH` for a tool, never runs one through a shell, and never forwards to another `neura`. | proposed |
| The tool is started with an argument vector, not a command line: its absolute path as `argv[0]`, the dispatch token, the command word, then the user's arguments unchanged, `--` included. On Linux and macOS `neura` replaces itself with the tool (`execv`), so the process, its standard streams, signals, environment, working directory and exit status are the tool's. On Windows it starts the tool, waits, and returns the tool's status. | proposed |
| The dispatch token is the argument `--neura-dispatch=<version line>`, where the version line is what `neura --version` prints. A tool that receives it compares it with its own version line before it reads any other argument, and stops with the mixed-version error below when they differ. The token is not passed to a program, so `run` and `test` see the same environment and arguments in both forms. | proposed |
| A missing tool is reported before any work starts, with the component that supplies it. | proposed |
| Without arguments, `neura` prints the usage to standard error and exits with status 1. An unknown command prints the 0.2 message, which lists the commands. Neither looks for a tool. | proposed |
| `neura --version`, `neura help` and `neura capabilities --json` never start a tool, a service or the network. `capabilities --json` prints the 0.2 object, byte for byte, when every SDK tool is present; a feature whose tool is missing is reported as `false`. | proposed |
| `neura runtime ...` belongs to the service component. Until `neurad` is installed next to `neura`, it reports the missing component; with it, it is forwarded to `neura-run runtime ...`. | proposed (seam) |

The rule that the tool is looked up only next to the real `neura` is stricter than looking next to `neura` first and on `PATH` second. A `PATH` fallback would let a `neura` from one installation run the compiler of another, which is the mixed state the version check exists to stop, so the stricter rule is chosen. A shell still finds a tool on `PATH` when the user types its name.

The version line contains the commit only for release and CI builds. Two local builds of one version from different commits both report the commit `unknown`, so the check cannot tell them apart; the parity tests build every tool in one tree.

### Messages

These are the exact texts. `<bin>` is the directory of the real `neura`; each is printed to standard error, and the exit status is 1.

| Case | Message |
| --- | --- |
| The tool of an SDK command is missing | `neura: error: 'neura <command>' needs <tool>, which is not in <bin>; it is part of the Neura-sdk component. Install the SDK, or reinstall Neura` |
| A core tool is missing | `neura: error: 'neura <command>' needs <tool>, which is not in <bin>; it is part of the Neura core, so this installation is incomplete. Reinstall Neura` |
| The service component is missing | `neura: error: 'neura runtime' needs the Neura-runtime service component (neurad), which is not in <bin>. 'neura run' runs images directly without it` |
| The tool cannot be started | `neura: error: cannot run <bin>/<tool>: <reason>` |
| The tool is from another build | `neura: error: <bin>/<tool> is from <its version line>, but the neura that started it is <neura's version line>; every tool in <bin> must come from one build. Reinstall Neura` |

## Doctor and components

`neura doctor` keeps its 0.2 checks and adds one line, `components`, that names the components it finds next to it: `core` when every core tool is present, `sdk` when every SDK tool is present. A component that is partly present fails the check and names the missing tools. An installation without the SDK is healthy without the standard library, the launchers, libclang or a C compiler: those lines say that the SDK is not installed instead of failing or warning. The absence of `neurad` is not reported in this milestone.

## Build structure

`src/cli/main.cpp` is replaced by libraries and one small `main` per tool. Files move only within `src/cli`; no file of the compiler, NeuraIR, optimizer, converter or NVM changes.

| Library | Sources | Depends on |
| --- | --- | --- |
| `neura_tool` | the registry, the dispatch token, version and help of a tool, and process forwarding | the generated version header only |
| `neura_cli_options` | the option reader and the usage text of 0.2, moved without change | headers of the image format, NVM limits and GPU options; no compiler object |
| `neura_cli_run` | `run`, `--each`, the tier report, the GPU choice | `neura_runner` |
| `neura_cli_compile` | `check`, `emit-ir`, `build`, `--native` packaging, `docs`, `c-bindings`, `test`, and the diagnostic printer | `neura_compiler`, `neura_cli_run` |
| `neura_lifecycle_tools` | `doctor`, `update`, `uninstall` and the update notice, from `lifecycle.cpp` | `neura_lifecycle` |

Each tool is a CMake executable with its install component. The build tree puts every tool next to `build/neura`, and the `neura` target depends on all of them, so a build of the target `neura` builds the tools it forwards to and every test that runs `build/neura` dispatches to real siblings. Options that apply to the shipped executables (`NEURA_PORTABLE_RELEASE`'s static C++ runtime, `NEURA_HAS_GPU`) apply to every tool.

## Dependency budget

Measured on x86-64 Linux, gcc 14, Release, GPU support compiled in, before the split:

| Item | Size |
| --- | --- |
| `bin/neura` | 10,037,488 bytes (8,976,528 stripped) |
| Install prefix (`bin`, `share`, `lib`) | 21,883,678 bytes |
| Dynamic dependencies of `bin/neura` | `libffi`, `libstdc++`, `libm`, `libgcc_s`, `libc` (a portable release build links `libffi` and the C++ runtime statically) |

| Rule | Status |
| --- | --- |
| `neura` links no compiler, NeuraIR, optimizer, converter, decompiler, NVM or libclang object, and is at most 5% of the size of the 0.2 `neura`. | proposed |
| `neura-run` and every other core tool link no object of the lexer, parser, NeuraIR, optimizer, converter, decompiler or editor tooling. `nm` of each core tool and a configure with `NEURA_SDK=OFF` are the evidence. | proposed |
| `neura-doctor` may contain the libclang probe (`c_import.cpp` and the libclang loader, which load libclang only when run). It is reported, and moving the probe behind the SDK is left to the lifecycle milestone. | proposed |
| Every shipped executable keeps the dynamic dependencies of the 0.2 `neura` or fewer. | proposed |
| Static linking duplicates shared code across the SDK tools. The size of every tool, of each component and of the whole prefix is reported against the table above; this milestone sets no limit on the total, and the lifecycle milestone sets one from these figures. | proposed |

## Archives and install

The release archive `neura-<version>-<triple>.tar.gz` keeps its name, its top-level directory, `bin/neura` and the standard library, and gains the tools in `bin/`. 0.2 clients update to it unchanged, because the updater copies the whole tree. `packaging/make-archive.sh --runtime` packs a runtime-only prefix as `neura-runtime-<version>-<triple>.tar.gz`. It is built and checked in CI but not published: release metadata for components, an archive per product, a public link for each tool and the updater bridge belong to the lifecycle milestone. Until then `install.sh` keeps one public link, `neura`, and every other tool is reached through it or through `<root>/current/bin`.

## Verification

| Evidence | Check |
| --- | --- |
| Every command works directly and through `neura` | A CTest entry runs each command of the registry both ways, with accepted and rejected arguments, and compares standard output, standard error and exit status |
| Unchanged images | Every image of `examples/` and `tests/` that the 0.2-layout binary at the base commit builds is built again with `neurac` and with `neura build`, and compared with `cmp`; the runtime identity capture of each binary at `--tier=0` is identical |
| Build tree and install | The tests run once against the build tree and once against an install prefix on `PATH` |
| Compiler-free core | `nm` and `ldd` of each core tool; a configure and build with `NEURA_SDK=OFF` |
| Missing and mixed components | A runtime-only prefix rejects every SDK command with its message; a missing core tool is reported; a `neura` built with another version line is refused by every tool |

## Order of work

1. This note.
2. Move the option reader, the usage text and the commands of `main.cpp` into the libraries above, with the single `neura` still built from them. Images and targeted tests are unchanged.
3. Add the tools and the dispatch token, with the 0.2 `neura` still in place.
4. Turn `neura` into the dispatcher.
5. Install components, `NEURA_SDK`, the runtime-only archive, CI and the tests that copy `neura`.
6. Evidence, documentation and the changelog.
