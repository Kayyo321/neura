# Neura 0.3.0: SDK tools and the direct runtime

This note fixes how the single `neura` executable becomes a set of sibling tools and a small dispatcher, before the split is made. It covers the binaries and what each links, how `neura <command>` finds the tool that does the work, the install components, the size and dependency budget, and the order of the work. The resident runtime service (`neurad`) and the component-aware archives, links and updater are later milestones; this note leaves a seam for each and says where.

Status: *proposed* on 2026-11-17, and *implemented* as written in the changes that followed this note, except where a row says otherwise. The owner was away and had asked for autonomous work toward 0.3.0, so the rows await the owner's confirmation; "proposed" stays until then. A row the owner changes is changed here and in the implementation in one later change.

The split changes no compiler semantics, no image byte and no runtime behavior. Every command and option that `neura` 0.2 accepts keeps its meaning, its output and its exit status, whether it is typed as `neura <command>` or as the direct tool.

## Products and components

The two products are **Neura-sdk** and **Neura-runtime**; archives and packages use the lowercase identifiers `neura-sdk` and `neura-runtime`. Both are built from this repository as install components of one CMake project.

| Component | CMake component | Holds | In Neura-sdk | In Neura-runtime |
| --- | --- | --- | --- | --- |
| Shared execution and delivery core | `core` | `neura`, `neura-run`, `neura-inspect`, `neura-doctor`, `neura-update`, `neura-uninstall`, `lib/neura/libneura-core-<version>`, `share/doc/neura/LICENSE` | yes | yes |
| SDK tools | `sdk` | `neurac`, `neura-test`, `neura-debug`, `neura-lsp`, `neura-fmt`, `neura-doc`, `neura-cbind`, `neura-decompile`, `neura-asm`, `neura-disasm`, `neura-explain`, `neura-tags`, `lib/neura/libneura-sdk-<version>`, the standard library sources and the native launchers | yes | no |
| Runtime service | `service` | `neurad` and service-manager files; empty in this milestone | no | yes, from the service milestone |

`cmake --install build` installs every component, which is the SDK layout of 0.2 plus the new tools. `cmake --install build --component core` installs the runtime-only set, and adding `--component sdk` to the same prefix completes the SDK. The option `NEURA_SDK` (default `ON`) can be turned off to configure a runtime-only build: the compiler, decompiler, editor and libclang targets are then not defined at all, so nothing in the `core` component can depend on them.

## Binary map

Each row is one executable in `bin/`. A tool is a small `main` that loads the shared libraries of "Build structure" from `lib/neura` and calls one command; "Loads" names them. The unified command is what `neura` accepts; the direct form is what the tool accepts when it is run by itself.

| Executable | Component | Unified commands | Direct form | Loads |
| --- | --- | --- | --- | --- |
| `neura` | core | `--version`, `version`, `--help`, `help`, `capabilities --json` (built in); every other command is forwarded | | nothing: a static executable on Linux, holding the registry, the version line and the usage text |
| `neura-run` | core | `run`, `runtime` | `neura-run [run] ...`, `neura-run runtime ...` | `libneura-core` |
| `neura-inspect` | core | `inspect`, `inspect-image` | `neura-inspect [inspect] ...`, `neura-inspect inspect-image ...` | `libneura-core` |
| `neura-doctor` | core | `doctor` | `neura-doctor [doctor]` | `libneura-core`; in an SDK build it also holds the C import stage's libclang probe, linked statically, and does not load `libneura-sdk` |
| `neura-update` | core | `update` | `neura-update [update] ...` | `libneura-core` |
| `neura-uninstall` | core | `uninstall` | `neura-uninstall [uninstall] ...` | `libneura-core` |
| `neurac` | sdk | `build`, `check`, `emit-ir` | `neurac [build] ...`, `neurac check ...`, `neurac emit-ir ...` | `libneura-sdk`, `libneura-core` |
| `neura-test` | sdk | `test` | `neura-test [test] ...` | `libneura-sdk`, `libneura-core`: it compiles and runs each test in one process, as 0.2 does |
| `neura-doc` | sdk | `docs` | `neura-doc [docs] ...` | `libneura-sdk`, `libneura-core` |
| `neura-cbind` | sdk | `c-bindings` | `neura-cbind [c-bindings] ...` | `libneura-sdk`, `libneura-core` |
| `neura-decompile` | sdk | `decompile` | `neura-decompile [decompile] ...` | `libneura-sdk`, `libneura-core` |
| `neura-asm` | sdk | `assemble` | `neura-asm [assemble] ...` | `libneura-sdk`, `libneura-core` |
| `neura-disasm` | sdk | `disassemble` | `neura-disasm [disassemble] ...` | `libneura-sdk`, `libneura-core` |
| `neura-explain` | sdk | `explain`, `--explain` | `neura-explain [explain] ...` | `libneura-sdk`, `libneura-core` |
| `neura-tags` | sdk | `tags` | `neura-tags [tags] ...` | `libneura-sdk`, `libneura-core` |
| `neura-debug` | sdk | `debug --stdio` | `neura-debug [debug] --stdio` | `libneura-sdk`, `libneura-core`; every debug session runs at tier 0 with the JIT off |
| `neura-lsp` | sdk | `lsp --stdio` | `neura-lsp [lsp] --stdio` | `libneura-sdk`, `libneura-core` |
| `neura-fmt` | sdk | `fmt ...` | `neura-fmt [fmt] ...` | `libneura-sdk`, `libneura-core` |
| `neurad` | service | `runtime ...` is served by `neura-run` | `neurad --foreground --root ROOT`, and the private `neurad --worker` | `libneura-core`; built by the service milestone ([Service030.md](Service030.md)) and installed only with `--component service` |

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
| `neura --version`, `neura help` and `neura capabilities --json` never start a tool, a service or the network. `capabilities --json` prints the 0.2 object when every SDK tool is present, byte for byte except for one top-level member, `service`, that [the service milestone](Service030.md#editors) added and that a 0.2 reader ignores; a feature whose tool is missing is reported as `false`. | proposed |
| `neura runtime ...` belongs to the service component. Until `neurad` is installed next to `neura`, it reports the missing component; with it, it is forwarded to `neura-run runtime ...`. | proposed; implemented by [the service milestone](Service030.md) |

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

`src/cli/main.cpp` became the dispatcher. Its option reader, usage text and commands moved, unchanged, into files of `src/cli` (`options.cpp`, `commands_*.cpp`), each tool has a `main` in `src/cli/tools/`, and the static libraries below hold them. No file of the compiler, NeuraIR, optimizer, converter or NVM changed. `lifecycle.cpp` no longer calls the C import stage: the doctor tool passes it the libclang probe.

| Static library | Sources | Depends on |
| --- | --- | --- |
| `neura_cli_registry` | `tool_registry.cpp`: the registry, the version line and the usage text, with no stream or file-system code | the generated version header only |
| `neura_cli_tool` | `tool.cpp`: a tool's start, the dispatch token, `--help`, `--version`, the executable's real path | `neura_cli_registry` |
| `neura_cli_core` | the option reader, `run` and `--each`, `inspect`, `inspect-image`, `doctor`, `update`, `uninstall`, the update notice, and the driver without a diagnostic renderer | `neura_runner`, `neura_lifecycle`, `neura_gpu` when built |
| `neura_cli_sdk` | `check`, `emit-ir`, `build` and `--native` packaging, `docs`, `c-bindings`, `test`, and the driver with the diagnostic renderer | `neura_cli_core`, `neura_compiler` |
| `neura_cli_artifact`, `neura_cli_explain`, `neura_cli_tags`, `neura_cli_debug` | `disassemble`, `assemble`, `decompile`; `explain` with the embedded pages; `tags`; the debug adapter | `neura_cli_sdk`, and `neura_decompiler` for the first |

The tools do not link these archives. Two shared libraries hold every object of them, and of the libraries below them, once:

| Shared library | Holds | Loads |
| --- | --- | --- |
| `lib/neura/libneura-core-<version>` | `neura_cli_core`, `neura_cli_tool`, `neura_cli_registry`, `neura_runner`, `neura_nvm` (with `src/jit` and tier 3), `neura_lift`, `neura_circuit`, `neura_nvr`, `neura_kernels`, `neura_lifecycle`, `neura_gpu` when built | libffi and the C++ runtime from the host, or, in a portable release, both inside it |
| `lib/neura/libneura-sdk-<version>` | `neura_cli_sdk`, `neura_cli_artifact`, `neura_cli_explain`, `neura_cli_tags`, `neura_cli_debug`, `neura_compiler`, `neura_decompiler`, the libclang loader | `libneura-core` |

| Rule | Status |
| --- | --- |
| The file name carries the full version (`libneura-core-0.3.0-rc.1.so`, `.dylib` on macOS), with no `SONAME` alias, so two versions of Neura never share a library file and the archive holds no symbolic link. | proposed; implemented |
| A tool finds the libraries through its run path, relative to itself: `$ORIGIN/../lib/neura` installed (`@loader_path/../lib/neura` on macOS) and `$ORIGIN/lib/neura` in the build tree, where the libraries are in `build/lib/neura` beside the launchers. `libneura-sdk` finds `libneura-core` beside itself. A moved installation keeps working. | proposed; implemented |
| `libneura-sdk` holds no object of the core's archives: it takes every core symbol from `libneura-core`, so the two libraries share one copy of the core's state. The tools link the shared libraries only. | proposed; implemented and checked |
| Calls between a library's own functions bind inside it (`-Bsymbolic-functions`), and objects are compiled with `-fno-semantic-interposition`, so making the code position independent keeps calls direct and inlinable. Data stays exported and unified: the static variables of inline functions that both libraries and the tools use (the diagnostic sink, the error limit, the source map) must have one copy in a process. Thread-local variables use the initial-exec model, which is valid because the libraries are always loaded at start, never with `dlopen`. | proposed; implemented |
| Only the entry points that tools need would ideally be exported. That needs export annotations in the compiler's and the NVM's headers, which this milestone does not edit, because hiding the rest at link time would split the inline functions' static variables between the two libraries. Every symbol of the `neura` namespace is exported for now. | proposed; not implemented |
| In a `NEURA_PORTABLE_RELEASE` build `libneura-core` holds the whole static C++ runtime and exports it, and `libneura-sdk` and the tools take it from there, so a process has one C++ runtime and every binary needs only the C library, `libm` and the loader. libffi's static archive, which is often not position independent, is linked into `libneura-core` with its symbols kept private. | proposed; implemented |

Each tool is a CMake executable with its install component. The build tree puts every tool next to `build/neura`, and the `neura` target depends on all of them, so a build of the target `neura` builds the tools it forwards to and every test that runs `build/neura` dispatches to real siblings. A test that copies `neura` elsewhere copies the tools and `lib/neura` with it. The Windows branch of the dispatcher (start the tool and wait) is written but neither built nor tested: no workflow builds Neura on Windows, and the libraries would need a DLL layout there.

## Dependency budget

The budget is the one frozen in [the 0.3.0 baseline](Release030.md): the dispatcher at most 1 MiB stripped, each archive at most 25% larger than rc.3's for its target, `neura --version` and a direct run of a hello-world image no slower than rc.3 within 5%, no system library beyond rc.3's, and an execution core without compiler code.

Measured on x86-64 Linux, gcc 14, Release, GPU support compiled in. "Before" is the single binary of the base commit of this milestone.

| Item | Before | After, default build | After, `NEURA_PORTABLE_RELEASE` |
| --- | ---: | ---: | ---: |
| `bin/neura` (stripped) | 10,037,488 (8,976,528) | 1,002,152 (848,744), static | 1,002,152 (848,744), static |
| `bin/neura-run` and the other tools (stripped) | | 16,984 to 22,536 (14,600 to 18,800); `neura-doctor` 206,152 (175,048) | 55,040 to 60,392 (47,456 to 51,712); `neura-doctor` 243,264 (207,880) |
| `lib/neura/libneura-core-<version>.so` (stripped) | | 5,213,752 (4,603,896) | 8,518,656 (7,275,336) |
| `lib/neura/libneura-sdk-<version>.so` (stripped) | | 5,738,000 (5,163,616) | 5,777,648 (5,200,544) |
| `bin/` and the two libraries | 10,037,488 | 12,445,168 | 16,439,264 |
| Install prefix | 21,883,678 | 24,295,454 | 28,289,550; the `core` component alone 10,006,742 |
| Archive | rc.3: 10,008,390 | | `neura-…tar.gz` 11,519,327 (+15.1%); `neura-runtime-…tar.gz` 3,888,161 (−61%) |

| Launch, median of 3 × 50 runs, one processor, page cache warm | Before | After, portable |
| --- | ---: | ---: |
| `neura --version` | 0.533 ms | 0.151 ms |
| `neura run hello-world.nvr -- a b` | 0.660 ms | 0.683 ms (+3.5%) |
| `neura-run hello-world.nvr -- a b` | | 0.603 ms |

`neura run` starts two processes, `neura` and then `neura-run`. The static dispatcher costs about 0.08 ms of that, and loading `libneura-core` instead of one larger executable costs less than the single binary's relocation of the C++ runtime. In the default build, which loads the host's `libstdc++` into both libraries, `neura run` takes 0.819 ms against 0.663 ms; the budget applies to release archives.

| Rule | Status |
| --- | --- |
| `neura` links no compiler, NeuraIR, optimizer, converter, decompiler, NVM or libclang object, and is at most 1 MiB stripped. | proposed; holds (848,744 bytes) |
| `neura-run`, every other core tool and `libneura-core` hold no object of the lexer, parser, NeuraIR, optimizer, converter, decompiler or editor tooling, and none loads `libneura-sdk`. `scripts/check-core-closure.py` compares the global definitions of every object of the SDK's static libraries with each core binary, reads each one's `DT_NEEDED` (load commands on macOS), and checks that `libneura-sdk` holds no core object; a configure with `NEURA_SDK=OFF` builds the core without them. Weak definitions of header code, such as the inline members of `neura::Diagnostic`, are shared by design and not counted. | proposed; holds |
| `neura-doctor` may contain the libclang probe (`c_import.cpp` and the libclang loader, which load libclang only when run). It is reported, and moving the probe behind the SDK is left to the lifecycle milestone. | proposed; holds in SDK builds, absent with `NEURA_SDK=OFF` |
| Every shipped executable depends on no system library beyond rc.3's. | proposed; holds. The tools also load `libneura-core` and `libneura-sdk`, which ship with them; the frozen rule names the system libraries. A portable release needs only `libc`, `libm` and the loader, with glibc symbols no newer than 2.38 |
| Each archive is at most 25% larger than rc.3's. | proposed; holds on x86-64 Linux (+15.1%, runtime-only −61%); the aarch64 Linux and macOS archives are built only by the release workflow and are not measured here |
| `neura --version` and `neura run` of a hello-world image within 5% of rc.3. | proposed; holds for the portable build (−72%, +3.5%) |
| Position-independent code does not slow programs down. | proposed; holds: `scripts/bench-wall.py` at the full sizes, 5 runs, best median of two rounds each, gives a geometric mean of 0.998 against the single binary over 17 programs (0.945 to 1.017), with equal steps and peak heap cells |

Section garbage collection (`-ffunction-sections -fdata-sections` with `--gc-sections`) was tried before the libraries existed and reduced the statically linked tools from 103 MB to 35 MB. With shared libraries it would only remove unreferenced code from the two libraries, and it changes the code generation of the NVM and the JIT, so it is not used.

## Archives and install

The release archive `neura-<version>-<triple>.tar.gz` keeps its name, its top-level directory, `bin/neura` and the standard library, and gains the tools in `bin/` and their libraries in `lib/neura`. 0.2 clients update to it unchanged, because the updater copies the whole tree. `packaging/make-archive.sh --runtime` packs a runtime-only prefix as `neura-runtime-<version>-<triple>.tar.gz`. It is built and checked in CI but not published: release metadata for components, an archive per product, a public link for each tool and the updater bridge belong to the lifecycle milestone. Until then `install.sh` keeps one public link, `neura`, and every other tool is reached through it or through `<root>/current/bin`.

## Verification

| Evidence | Check |
| --- | --- |
| Every command works directly and through `neura` | A CTest entry runs each command of the registry both ways, with accepted and rejected arguments, and compares standard output, standard error and exit status |
| Unchanged images | Every image of `examples/` and `tests/` that the 0.2-layout binary at the base commit builds is built again with `neurac` and with `neura build`, and compared with `cmp`; the runtime identity captures of each binary at `--tier=0` and with the JIT are identical, since the NVM is now compiled position independent |
| Build tree and install | The tests run once against the build tree and once against an install prefix on `PATH` |
| Compiler-free core | `scripts/check-core-closure.py` (symbols and `DT_NEEDED`) on each core binary and `libneura-core`; a configure and build with `NEURA_SDK=OFF` |
| Missing and mixed components | A runtime-only prefix rejects every SDK command with its message; a missing core tool is reported; a `neura` built with another version line is refused by every tool |

## Order of work

1. This note.
2. Move the option reader, the usage text and the commands of `main.cpp` into the libraries above, with the single `neura` still built from them. Images and targeted tests are unchanged.
3. Add the tools and the dispatch token, with the 0.2 `neura` still in place.
4. Turn `neura` into the dispatcher.
5. Install components, `NEURA_SDK`, the runtime-only archive, CI and the tests that copy `neura`.
6. Evidence, documentation and the changelog.
7. Shared libraries in `lib/neura`, after the statically linked tools exceeded the archive budget of [the baseline](Release030.md) five times over, and the static dispatcher.
