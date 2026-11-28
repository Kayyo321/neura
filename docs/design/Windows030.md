# Neura 0.3.0: native Windows

This note describes how Neura is built and runs natively on Windows (`x86_64-windows-msvc`) without WSL. It covers the toolchain, platform layer, Windows layout for the [SDK and runtime](Products030.md), host services, tools, installation and release. Decisions W01 to W35 also record the work deferred from 0.3.0 and its runtime refusals.

The initial design decisions were made on 2026-11-20. Windows 0.3.0 defers the JIT (W11), TLS, HTTP/2 and PostgreSQL (W28), native launchers and C-import linking (W19, W30), and desktop hosting (W31). The decision table describes the intended release behavior. [Results](#results) records measured evidence, and [Verification](#verification) lists the checks still required for release qualification.

The port adds no language rule or NVR format revision. The verification target in W13 is byte-identical images built on Linux, macOS and Windows from the same source and options. Windows-specific code belongs in `_WIN32` branches, in the platform layer or in the Windows build hooks. Changes visible to POSIX need review and tests on those targets.

## Scope

| Item | Value |
| --- | --- |
| Target | `x86_64-windows-msvc`: Windows 10 version 2004 (build 19041) or later, Windows 11, Windows Server 2022 or later, on x86-64 (W03). Windows on Arm64 is not a 0.3.0 target. |
| Products | Both. Neura-runtime: `neura`, `neura-run`, `neura-inspect`, `neura-doctor`, `neura-update`, `neura-uninstall`, `neura-server-host`, `neurad`. Neura-sdk adds `neurac`, `neura-test`, `neura-lsp`, `neura-fmt`, `neura-debug`, `neura-decompile`, `neura-doc`, `neura-cbind`, `neura-asm`, `neura-disasm`, `neura-explain`, `neura-tags`, `neura-new`, `neura-package`, `neura-dev`, and libclang-backed header reading. |
| Build hosts | Windows with Visual Studio 2022 (W01). MinGW, MSYS2 GCC and Cygwin are not build hosts; Git for Windows supplies `bash`, GNU tar and `gzip` to the packaging scripts and the tests, not a compiler. |
| Release | Windows ships in 0.3.0 for both products, unsigned, with build-provenance attestations (W35). |
| Not covered | The services and flavors under "Out of scope", each with its refusal. |

## Baseline

Measured on 2026-11-20 on a CRLF checkout of `windows-native` with cl 19.44, Ninja and a Release configure with `NEURA_SDK=ON`, before any port change to the build or the sources. Configuring succeeds and registers 298 CTest entries plus the three `neura-libclang-*` entries. The build stops at step 286 of 347 with 17 failures; the link steps of the shared libraries, whose flags are GNU `ld`'s, never run.

| Failure | Cause |
| --- | --- |
| `src/kernels/neura_math.c` | C4146 (unary minus on an unsigned value) under the kernels' `/WX` |
| `src/circuit/circuit.cpp` | C2280: a conditional operator copies the non-copyable `Circuit::Gadget` |
| `src/nvm/tier3_range.cpp`, `tests/lift/lift_tests.cpp` | `__int128`, which MSVC does not have |
| `src/jit/tier3_lower.cpp`, `tests/jit/regalloc_tests.cpp` | `__builtin_ctzll` and `__builtin_popcountll` |
| `src/cli/install_layout.cpp` | `fopen(path.c_str())` with a `wchar_t` path |
| `src/ir-optimization/inlining.cpp` | `std::back_inserter` without `<iterator>` |
| `src/cli/serve.cpp`, `src/app/dev.cpp` | POSIX code outside the `_WIN32` guard; `dev.cpp` also blocks the `neura` target through its dependency on `neura-dev` |
| `src/project/manifest.cpp` | Aggregate class template argument deduction of `Located`, which cl does not support |
| `src/cli/commands_test.cpp` | `_setmode` without `<io.h>` and `<fcntl.h>` |
| `tests/diagnostics/unit_tests.cpp` | C2015: multi-character literals without `/utf-8` |
| `tests/ir-optimization/summaries_tests.cpp`, `tests/lifecycle/unit_tests.cpp` | `<unistd.h>` |
| `tests/jit/x64_tests.cpp` | C1128: too many sections without `/bigobj` |
| `neura_test_mixed_dispatcher` | LNK2019: the target does not compile `src/cli/tool.cpp` |

About 104 warnings remain, mostly CRT deprecations (C4996), narrowing (C4244, C4267) and shadowing (C4456 to C4458). Without `/utf-8`, cl reads sources in the ANSI code page, so the typographic-character checks of the lexer never match (C4566).

## Toolchain and build

| Item | Rule |
| --- | --- |
| Reference compiler | cl.exe from Visual Studio 2022 17.14 or later (MSVC v143, toolset 14.44 or later) with Ninja and the Windows SDK 10.0.22621 or later. Releases are built with it. |
| Second compiler | clang-cl from the Visual Studio "C++ Clang tools" component (LLVM 19), built in CI. No fix and no JIT path may depend on one compiler. |
| CMake | 3.24 or later on Windows, checked by `cmake/NeuraWindows.cmake`; the POSIX minimum stays 3.20. |
| Dependencies | vcpkg in manifest mode: `vcpkg.json` at the root with `libffi` and an optional `gpu` feature (`vulkan-headers` and the `glslang` tools), a pinned `builtin-baseline`, triplet `x64-windows-static`. CI uses the runner's preinstalled vcpkg, developers the copy that comes with Visual Studio. OpenSSL, nghttp2 and libpq stay in the POSIX release recipe for 0.3.0. |
| Presets | `CMakePresets.json` adds `windows-msvc`, `windows-msvc-debug`, `windows-clang-cl` and `windows-release` (`NEURA_PORTABLE_RELEASE` with TLS, HTTP/2 and PostgreSQL set to `OFF`), each with build and test presets. The file stays at schema 2, which CMake 3.20 reads, so `wasm-runtime` keeps the POSIX minimum: the Windows presets therefore set vcpkg's toolchain as `CMAKE_TOOLCHAIN_FILE` and carry no host condition (a schema 3 feature). They are listed on every host and fail at configure on one without `cl` or `clang-cl`. Each builds in `build-<preset>` at the root, beside `stdlib/`, which is where a build-tree tool looks for the standard library (`<bin>/../stdlib`), as with `cmake -B build`. `wasm-runtime` is unchanged. |
| Build and test prerequisites | A Developer PowerShell for VS 2022 (or a shell after `vcvars64.bat`), CPython 3.11 or later (not the Microsoft Store alias), Git for Windows, and `NEURA_LIBCLANG` naming a libclang 14 to 21 for the C import tests. None of them is a runtime prerequisite. |
| Commands | `cmake --preset windows-msvc`, `cmake --build --preset windows-msvc`, `ctest --preset windows-msvc`. |

### Compiler and linker settings

| Setting | Value | Why |
| --- | --- | --- |
| Definitions | `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_WIN32_WINNT=0x0A00`, `WINVER=0x0A00`, `_CRT_SECURE_NO_WARNINGS`, `_CRT_NONSTDC_NO_WARNINGS`, `_WINSOCK_DEPRECATED_NO_WARNINGS`; `UNICODE` is not defined | Windows code calls the `W` functions by name, so existing explicit `A` calls (the Vulkan loader, the libclang loader) keep working |
| Source and language | `/utf-8` for C and C++; for C++ also `/bigobj`, `/Zc:__cplusplus`, `/Zc:preprocessor`, `/permissive-` (implied by `/std:c++20`) and `/EHsc` | W06, W07 |
| Warnings | `/W4` with `/wd4244 /wd4267 /wd4456 /wd4457 /wd4458 /wd4459 /wd4127 /wd4702 /wd4324`; no `/WX` for C++; CI fails on any `warning C` line from Neura's sources; the kernels keep `/W4 /WX` | W06 |
| Floating point | `/fp:precise` for the C++ code, stated explicitly for `neura_lift` and `neura_nvm`; `/fp:strict` for the kernels; clang-cl adds `-ffp-contract=off`; never `/fp:fast`, `/fp:contract` or `/arch:AVX*` | W13 |
| Runtime library | `CMAKE_MSVC_RUNTIME_LIBRARY` `MultiThreaded$<$<CONFIG:Debug>:Debug>` for every target; a mixed-CRT link (LNK4098) fails the release check | W05 |
| Executables | `/STACK:8388608`, an embedded manifest (`activeCodePage` UTF-8, `longPathAware`, `supportedOS` for Windows 10 and 11) and a `VERSIONINFO` resource | W03, W07, W12 |
| Release builds | `/Brepro` for compiling and linking, `/DEBUG:FULL` with `/OPT:REF` and `/OPT:ICF`; PDBs are CI artifacts and are not installed | W05, W29 |
| POSIX-only flags | `-fno-semantic-interposition` and `-ftls-model=initial-exec` are not given to clang-cl | They describe ELF symbol binding, which a DLL does not have |

### Build structure

`CMakeLists.txt` stays the single top-level file. The foundation package rewrites only conditions, so that each area that is POSIX-gated today has a Windows hook point; the text of every POSIX branch is unchanged.

| File | Holds |
| --- | --- |
| `cmake/NeuraWindows.cmake` | Every Windows-wide setting above, the resource and manifest functions, the product-layout functions, and `neura_windows_hook(name)`, which calls `neura_windows_<name>()` when an area file defines it |
| `cmake/windows/<area>.cmake` | One file per gated area, included by a sorted glob on `WIN32` and owned by one package. It may define only `neura_windows_<hook>` macros and `NEURA_WINDOWS_<AREA>` variables. |
| `cmake/neura.manifest`, `cmake/neura-version.rc.in`, `cmake/tool_entry.cpp.in` | The application manifest, the version resource and the generated tool entry of W04 |

| Area file | Package | Hooks and variables |
| --- | --- | --- |
| `service.cmake` | WP09 | `NEURA_WINDOWS_SERVICE`; `service_sources`, `service_tools`, `service_tests` |
| `server.cmake` | WP10 | `server_libraries`, `server_tools` |
| `server-components.cmake` | WP11 | `server_tls`, `server_http2`, `postgres` |
| `launchers.cmake` | WP12 | `launchers`, `launcher_tests` |
| `project-tools.cmake` | WP13 | `NEURA_WINDOWS_DEV` |

Until an area file exists, its area is absent on Windows, as it is today, so the build stays green while the packages work in parallel. The exceptions are the TLS, HTTP/2 and PostgreSQL options: asked for on Windows without their hook, they stop the configure with an error that names `cmake/windows/server-components.cmake`, in place of today's "requires POSIX sockets". The server test registrations test for the targets (`if(TARGET neura_server_http)`), so an area file that defines the targets also gets the existing tests.

### Platform layer

`src/platform/` (namespace `neura::platform`) is one static library, `neura_platform`, with a POSIX and a Windows implementation of each module, linked by `neura_nvm`, `neura_runner` and `neura_cli_tool` and so by every tool, the dispatcher and the launcher. `tests/platform/platform_tests.cpp` tests it as the CTest entries `platform-<suite>`.

| Module | Provides |
| --- | --- |
| `windows.hpp` | The only sanctioned include of `<winsock2.h>`, `<ws2tcpip.h>`, `<afunix.h>`, `<mswsock.h>` and `<windows.h>`, in that order; included from `.cpp` files only, never from a public header |
| `text.hpp` | Strict UTF-8 and UTF-16 conversion (invalid sequences and unpaired surrogates are errors), `path_from_utf8`, `utf8_path` (generic form, `/`) and `native_utf8_path` |
| `error.hpp` | `last_error()` and `error_message(code)`: `FormatMessageW` as UTF-8 on Windows, `strerror` elsewhere |
| `self.hpp` | `executable_path()`: `GetModuleFileNameW`, then `GetFinalPathNameByHandleW` to resolve junctions, links and 8.3 names; `/proc/self/exe` and `_NSGetExecutablePath` on POSIX |
| `process.hpp` | `Spawn` and `Process` (start, wait with a timeout, terminate, interrupt with a console event, kill the tree), `ExitStatus` (exited, signaled, crashed) with `describe()` and `shell_status()`, quoting by the `CommandLineToArgvW` rules, `run_capture`. Windows uses `CreateProcessW` with `STARTUPINFOEXW`, an explicit handle list and an optional job list, never `cmd.exe`. |
| `console.hpp` | Binary standard streams, `is_terminal` (by `GetConsoleMode`, since `_isatty` is true for `NUL`), terminal colors and width, UTF-8 console streams through `WriteConsoleW`, console-event handlers, and the unattended error mode |
| `socket.hpp` | A pointer-width socket type, one-time Winsock start, non-blocking and non-inheritable sockets, `poll` (`WSAPoll` on Windows), `socket_pair()` (an `AF_UNIX` pair through an owner-only directory, loopback TCP with a random token as the fallback) and `peer_pid()` |
| `file.hpp` | `FileLock` (`flock` or `LockFileEx`), `replace_file` and `write_file_atomically` with bounded retries on sharing violations, `remove_tree` that never follows links or junctions, `link_kind`, `open_no_follow`, private directories (mode 0700, or a protected DACL that grants only the user's SID) and `unique_name` |
| `dynlib.hpp` | `Library::open` with the search policy of W19, `symbol`, `close`, `loaded_modules` |
| `environment.hpp` | `get_env`, `set_env` and `unset_env` (`SetEnvironmentVariableW` and the CRT copy kept in step), `environment()` |
| `dirs.hpp` | `local_app_data()`, `home_directory()`, `temp_directory()` |

Three rules apply to every package. New code includes Windows headers only through `windows.hpp`; the existing direct includes in the NVM providers, pending requests, the native container and the Vulkan loader get `NOMINMAX` and `WIN32_LEAN_AND_MEAN` guards from their owners. Text that reaches the user, an artifact, a C API that takes `char*` or another process never comes from `std::filesystem::path::string()` or `c_str()`, which throw or lose characters for non-ASCII names under the ANSI code page; it comes from `utf8_path`, `native_utf8_path` or a `W` function. A package keeps existing POSIX code unchanged unless it also runs the POSIX tests that cover the change; new code that must work on both may use the platform layer on both.

## Decisions

| ID | Topic | Status | Recorded in |
| --- | --- | --- | --- |
| W01 | Toolchain | decided | this note, [CONTRIBUTING](../../CONTRIBUTING.md), [Continuous integration](../ContinuousIntegration.md) |
| W02 | Triple spelling | decided | this note, [Install](../Install.md) |
| W03 | Minimum Windows | decided | [Install](../Install.md), this note |
| W04 | Product layout | decided | [Products030](Products030.md), [Install](../Install.md) |
| W05 | CRT and portable release | decided | [Install](../Install.md), [Release process](../ReleaseProcess.md) |
| W06 | Compiler settings and warnings | decided | [CONTRIBUTING](../../CONTRIBUTING.md), [Continuous integration](../ContinuousIntegration.md) |
| W07 | Text encoding | decided | this note, [NVM](../runtime/NVM.md) |
| W08 | Line endings | decided | [Core language](../lang/CoreLanguage.md), [Tooling](../lang/Tooling.md), [Projects030](Projects030.md) |
| W09 | Paths | decided | [Standard library](../lang/Stdlib.md), [NVM](../runtime/NVM.md), [Projects030](Projects030.md) |
| W10 | Processes, exit statuses and console events | decided | [Tooling](../lang/Tooling.md), [Errors](../lang/Errors.md), [Server host](../runtime/ServerHost.md), [Service](../runtime/Service.md) |
| W11 | JIT | deferred to 0.3.1 | [NVM](../runtime/NVM.md) |
| W12 | Stack size | decided | this note |
| W13 | Floating point and image identity | decided | [NVM](../runtime/NVM.md), [Backend](../Backend.md) |
| W14 | Sockets and readiness | decided | [NVM](../runtime/NVM.md) |
| W15 | Error codes | decided | [NVM](../runtime/NVM.md) |
| W16 | File-system semantics | decided | [NVM](../runtime/NVM.md), [Standard library](../lang/Stdlib.md) |
| W17 | Console services | decided | [NVM](../runtime/NVM.md) |
| W18 | Environment | decided | [NVM](../runtime/NVM.md), [Standard library](../lang/Stdlib.md) |
| W19 | Dynamic libraries, DLL search and C import linking | DLL loading and header reading in 0.3.0. COFF linking in 0.3.1. | [Foreign interop](../lang/ForeignInterop.md), [NVM](../runtime/NVM.md), [Tooling](../lang/Tooling.md) |
| W20 | Locked memory | decided | [NVM](../runtime/NVM.md), [Standard library](../lang/Stdlib.md) |
| W21 | Process generation | decided | [NVM](../runtime/NVM.md) |
| W22 | Install layout | decided | [Install](../Install.md), [Lifecycle030](Lifecycle030.md) |
| W23 | Archives and release metadata | decided | [Install](../Install.md), [Lifecycle030](Lifecycle030.md), [Release process](../ReleaseProcess.md) |
| W24 | Installer, bootstrap and `PATH` | decided | [Install](../Install.md), [Release process](../ReleaseProcess.md) |
| W25 | Manifest file modes | decided | [Lifecycle030](Lifecycle030.md) |
| W26 | Update transport | decided | [Install](../Install.md) |
| W27 | Server host | decided | [Server host](../runtime/ServerHost.md) |
| W28 | TLS, HTTP/2, PostgreSQL and TLS trust | deferred to 0.3.1 | [Server host](../runtime/ServerHost.md), [Install](../Install.md), [SQL](../runtime/Sql.md) |
| W29 | `neurad` | decided | [Service030](Service030.md), [Service](../runtime/Service.md) |
| W30 | Native executables | Win64 PE in 0.3.1, qualification pending | [Standalone executables](../lang/Standalone.md), [Standalone design](Standalone.md) |
| W31 | Desktop build, run, dev and ZIP | Win64 in 0.3.1, desktop session qualification pending | [Desktop030](Desktop030.md), [Providers](../runtime/Providers.md) |
| W32 | Project tools | decided | [Projects](../apps/Projects.md), [Projects030](Projects030.md) |
| W33 | GPU | decided | [GPU execution](../lang/GpuExecution.md) |
| W34 | Testing | decided | [Continuous integration](../ContinuousIntegration.md), [CONTRIBUTING](../../CONTRIBUTING.md) |
| W35 | Release scope | decided | [Release030](Release030.md), [Release process](../ReleaseProcess.md), [Code signing](../CodeSigning.md), [CHANGELOG](../../CHANGELOG.md) |

### W01: Toolchain

cl.exe (Visual Studio 2022 17.14 or later, MSVC v143) with Ninja and the Windows SDK 10.0.22621 or later is the reference compiler for Windows builds and releases. clang-cl from the Visual Studio "C++ Clang tools" component (LLVM 19) is built in CI as a second compiler. CMake is 3.24 or later on Windows. Dependencies come from vcpkg in manifest mode with the triplet `x64-windows-static` and a pinned baseline. MinGW and Cygwin are not supported.

The triple is already `x86_64-windows-msvc` in `CMakeLists.txt`, and the cl baseline nearly builds. Keeping every fix, and the JIT, independent of the compiler avoids a product that only clang-cl can build. vcpkg comes with Visual Studio and is preinstalled on GitHub's Windows runners, so the workflows need no third-party Action, which the repository settings do not allow.

### W02: Triple spelling

`x86_64-windows-msvc` is the only spelling: in CMake, in the native interop provider's host triple, in `--version`, in archive names, in component manifests and in the keys of `neura-components.txt`. The spelling `x86_64-pc-windows-msvc` in the documentation of `stdlib/std/reflect/ffi/abi.nra` and in `tests/stdlib/reflect-ffi` is corrected to it.

The spelling follows the `<arch>-<os>-<abi>` pattern of the other targets, and the triple check of the install layout already accepts it. It becomes permanent in update metadata with the first release, so it is fixed now.

### W03: Minimum Windows

Windows 10 version 2004 (build 19041) or later, Windows 11, and Windows Server 2022 or later, on x86-64. The manifest declares `supportedOS`, and a tool started on an older system stops with `neura: Neura needs Windows 10 version 2004 or later`. Windows on Arm64 is not a 0.3.0 target.

The design depends on features of these versions: the UTF-8 active code page (1903), `AF_UNIX` stream sockets (1803), `WSAPoll` reporting failed connects (2004), POSIX-semantics delete and rename (1709), job lists for new processes (Windows 10), and the UDP segmentation options `UDP_SEND_MSG_SIZE` and `UDP_RECV_MAX_COALESCED_SIZE` (2004).

### W04: Product layout

An installation has two self-contained DLLs, `bin/neura-core-<version>.dll` and `bin/neura-sdk-<version>.dll`, beside the executables. Every tool executable is a stub whose `main` calls one exported C function, `neura_tool_<id>(int, char**)`, of one DLL, and the DLLs export nothing else. The SDK DLL does not import the core DLL: each holds its own copy of the core archives. `lib/neura` holds only the launchers on Windows. The core-closure rule of [Products030](Products030.md#build-structure), "`libneura-sdk` holds no object of the core's archives", is replaced on Windows by "each executable imports at most one Neura DLL".

The entry of each tool is generated from `cmake/tool_entry.cpp.in` into the build tree: it defines `main` as `neura_tool_main_<id>`, includes the tool's source, and adds the exported `extern "C"` wrapper, so the tool sources are not edited. No project header uses `main` as an identifier, and the helpers of the tool sources are in anonymous namespaces; a duplicate external symbol between two tool sources would fail the DLL's link (LNK2005) rather than go unnoticed. The dispatcher `neura.exe` imports no Neura DLL and stays within the budget of 1 MiB. The native launcher is one static executable. Tests keep linking the static archives. Import libraries are not installed, and the loader finds a DLL in its executable's directory first, so no run-path equivalent is needed. `scripts/check-core-closure.py` gets a Windows mode that checks these rules.

Statically linked tools were rejected for size when the products were split ([Products030](Products030.md#order-of-work), step 7). A pair of DLLs with a C++ interface would reach the limit of 65,535 exports, would need `dllimport` for data, and would duplicate the static variables of inline functions, such as the diagnostic sink, between modules, which breaks the single copy per process that the shared libraries provide on POSIX. With one Neura DLL per process and a C boundary, none of these arise, and no C++ object or allocation crosses a module boundary.

### W05: CRT and portable release

Every Windows binary links the static CRT (`/MT`, `/MTd` in Debug). libffi uses the static vcpkg triplet. `NEURA_PORTABLE_RELEASE` sets the portable Windows settings, including `/Brepro`. A released Windows binary may import only allowlisted Windows system DLLs and its product's Neura DLL. `scripts/check-windows-binaries.py` enforces the import and export boundaries in the release workflow.

[Install](../Install.md) promises that the archives need nothing else from the system. The static CRT removes the Visual C++ redistributable as a prerequisite, and the C-only boundary of W04 makes a CRT per module safe.

### W06: Compiler settings and warnings

`/utf-8`, `/Zc:__cplusplus`, `/Zc:preprocessor`, `/bigobj`, `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `_WIN32_WINNT=0x0A00` and the CRT deprecation switches apply to the whole build, and `UNICODE` is not defined. C++ builds at `/W4` with the suppressions `/wd4244 /wd4267 /wd4456 /wd4457 /wd4458 /wd4459 /wd4127 /wd4702 /wd4324`; C4100, C4189, C4505, C4018, C4146, C4706, C4005 and C4273 stay enabled. CI fails on any remaining `warning C` line from Neura's sources (`third_party`, `_deps` and vcpkg excluded). The kernels keep `/W4 /WX /fp:strict`.

The suppressions are the MSVC counterparts of warnings the POSIX gate does not enable (`-Wall -Wextra -Wpedantic` without `-Wconversion` or `-Wshadow`), so MSVC enforces the same set as the Tier 1 warning gate rather than a larger one that would touch many unrelated files. `/utf-8` fixes silent corruption of literals: under the ANSI code page the lexer's typographic-character checks never fire.

### W07: Text encoding

UTF-8 is the only narrow encoding. Every executable embeds the `activeCodePage` UTF-8 manifest, so `argv`, `getenv` and `fopen` are UTF-8. Paths cross into operating-system APIs as UTF-16, converted strictly: an invalid sequence is an error, and an unpaired surrogate in a file name answers 13 (`Unsupported`). A real console is written with `WriteConsoleW` and read with `ReadConsoleW`. The console code page is never changed.

Non-ASCII profile paths are common on Windows, and `path::string()` throws for them under the ANSI code page. Changing the console code page would leak into the user's shell after the tool ends.

### W08: Line endings

The repository is LF: `.gitattributes` says `* text=auto eol=lf`, with CRLF only for `*.ps1`, `*.cmd` and `*.bat`, and marks the binary files binary. Tools read and write their standard streams in binary mode. Source text is normalized from CR LF to LF when it is loaded, by one shared helper (`src/util/line_endings.hpp`) used by the source loader, the language server's document store, `fmt` and `check --stdin`; a lone CR keeps its current meaning. Spans, images and diagnostics are therefore identical for CRLF and LF files. `neura fmt` writes LF and treats differences of line endings alone as already formatted. Project and bundle digests stay digests of the bytes. `neura new` writes a `.gitattributes` into new projects.

Without this every byte-exact test fails on a default Git for Windows checkout, which converts to CRLF, and images built on Windows differ from those built on Linux. Normalizing when the text is loaded, as rustc does, keeps the lexer unchanged.

### W09: Paths

Tools print paths in generic form, with `/` and the drive letter as the file system reports it (`C:/Users/me/app.nra`). `Std.Fs.Path` keeps `/` as its only separator and gains one rule on every host: a leading ASCII letter, `:` and `/` is an absolute root, so `Path.of("C:/x").is_absolute` holds and `join`, `parent` and `relative_to` treat it as a root. A doubled `//` still collapses, and `\` is an ordinary character in a `Path`.

The Windows file-system host accepts `/` and `\`, absolute drive paths, root-relative paths (on the current drive) and UNC text with backslashes. It refuses with `invalid_input` (4) a drive-relative `X:name`, a `:` inside a component (alternate data streams), the reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1` to `COM9`, `LPT1` to `LPT9`, `CONIN$`, `CONOUT$`), and a component that ends in a dot or a space. `--allow-fs` prefixes are canonicalized with `GetFinalPathNameByHandleW` and compared component by component, ordinally and without case. The project manifest's local paths reject the same Windows-hostile names as bundle entries do, on every host.

Absolute path components start with `/` or the complete drive root such as `C:/`. Roots compare by their bytes, so `C:/` and `c:/` are distinct. A relative path whose first component is a drive designator keeps a leading `./` when more components follow, as in `./C:/x`. This keeps a relative path from becoming absolute through `join`, `relative_to` or `resolve_lexically`. The rule applies on every host and changes POSIX results for these drive-shaped components. See [the standard library contract](../lang/Stdlib.md#112-stdfs).

### W10: Processes, exit statuses and console events

Children are started with `CreateProcessW` from an argument vector, quoted by the `CommandLineToArgvW` rules, never through `cmd.exe`. The dispatcher returns the tool's exit code unchanged. Console events map to signals:

| Event | Signal | Unhandled exit status |
| --- | --- | --- |
| `CTRL_C_EVENT` | `SIGINT` | 130 |
| `CTRL_BREAK_EVENT` | `SIGQUIT` in `neura run` (task dump); `SIGTERM` in `neura-server-host`, `neura dev` and `neurad` | 131 |
| `CTRL_CLOSE_EVENT`, `CTRL_LOGOFF_EVENT`, `CTRL_SHUTDOWN_EVENT` | `SIGHUP` | 129 |

Trap statuses (132 to 153) are exit codes on every host. An unhandled exception passes its NTSTATUS through as the exit code, and `std::abort` exits with 3, as the MSVC CRT does. Where Neura describes a child (the lifecycle's `run_child`, service records, the server host's `worker_exit`, the test drivers), an exit code of 0xC0000000 or above is reported as `crashed 0x...`, and a termination that Neura itself performs uses the exit code 128+N and is described as signal N. Every tool and test runner sets an unattended error mode, so no error dialog blocks a run.

A process that waits for a child sharing its console (the dispatcher, `neura serve`) leaves every event to the child. For Ctrl+C and Ctrl+Break it returns at once. For close, logoff and shutdown its handler does not return, because the system ends a process once its handler returns, and the child's kill-on-close job would end with it: the waiting process ends when the child does, or both end at the system's timeout. A program named by a relative path runs from the child's working directory when one is given, as POSIX resolves it after `chdir`. A handle passed by value (`Spawn::inherit`) is made inheritable while any start that names it runs, counted across threads, and then restored.

Scripts and tests keep seeing the POSIX shell's statuses for interrupts, and real crash codes are not hidden behind them. Ctrl+Break is the key Windows consoles reserve for `SIGBREAK`, the counterpart of `SIGQUIT`'s Ctrl+\\.

### W11: JIT

Windows 0.3.0 uses portable execution for NVM regions. `--jit=auto` selects the interpreter, and `--jit=on` fails before execution with `--jit=on: Windows has no JIT in 0.3.0`. Windows CI uses a CTest time scale of 12. The Windows Debug build keeps the tier 3 context layout assertions and uses a pointer-owned value pool for compatibility with MSVC's checked iterators.

A future Win64 implementation needs qualified calling-convention adapters, unwind data, and identity comparisons with the interpreter before enabling native code.

### W12: Stack size

Every executable reserves an 8 MiB main stack (`/STACK:8388608`), which is also the default for `std::thread`. Test runners that need a 1 GiB stack keep it, on a thread started with `_beginthreadex` and `STACK_SIZE_PARAM_IS_A_RESERVATION`.

Windows' default of 1 MiB would make the recursive parser, the lowering, the NeuraIR formatter (depth 1024) and the reference evaluator overflow where Linux traps cleanly or succeeds.

### W13: Floating point and image identity

C++ is compiled with `/fp:precise` and the kernels with `/fp:strict`, never with `/fp:fast`, `/fp:contract` or `/arch:AVX*`; clang-cl adds `-ffp-contract=off`. The block lowering treats `_M_X64` without `_M_FP_FAST` as `FLT_EVAL_METHOD` 0, as on Linux. Images are byte-identical across hosts and compilers, which `lift-portable`'s fixed digest and a CI comparison of images built on Linux and on Windows check. A difference is a bug in the code to fix, such as an order that depends on argument evaluation or on an unstable sort, never an accepted platform difference.

Float lowering is bit-exact only without contraction. MSVC evaluates function arguments in a different order from GCC, and any dependence on that order must show up as a test failure rather than as a different image.

### W14: Sockets and readiness

Winsock 2.2 is started once per process before any socket is used. Sockets are held in pointer-width fields. Readiness uses `WSAPoll` for sockets and `WaitForMultipleObjects` for handles. `NEURA_IO_BACKEND` is ignored on Windows. Listeners use `SO_EXCLUSIVEADDRUSE` where POSIX uses `SO_REUSEADDR`. IPv6 sockets in the Windows host provider are dual-stack (`IPV6_V6ONLY=0`), including TCP and UDP binds to `::`. `send_file` uses `TransmitFile`.

| Socket option | Windows |
| --- | --- |
| 1 | `SO_EXCLUSIVEADDRUSE` semantics |
| 2, 5 | 13 (`Unsupported`) |
| 3 | `TCP_NODELAY` |
| 4 | `TCP_FASTOPEN` |
| 6 to 9 | `SO_KEEPALIVE`, `TCP_KEEPIDLE`, `TCP_KEEPINTVL`, `TCP_KEEPCNT` |
| 10 to 12 | as on POSIX |
| 13 | `UDP_SEND_MSG_SIZE` |
| 14 | `UDP_RECV_MAX_COALESCED_SIZE` |
| 15 | `SO_PROTOCOL_INFOW` |

`SO_REUSEADDR` on Windows lets another process take over a bound port. `WSAEventSelect` is edge-triggered and forces non-blocking mode, which breaks the level-triggered contract of the wait sources; IOCP is not used in 0.3.0.

### W15: Error codes

`host_error::from_win32` and `from_wsa` map Win32 and Winsock errors onto the existing codes 1 to 16 and 100, and new Windows code never maps through `errno`.

| Windows error | Code |
| --- | --- |
| `ERROR_FILE_NOT_FOUND`, `ERROR_PATH_NOT_FOUND` | 1 |
| `ERROR_ACCESS_DENIED`, `ERROR_SHARING_VIOLATION` | 2 |
| `ERROR_ALREADY_EXISTS` | 3 |
| `ERROR_DIRECTORY` | 7 |
| `ERROR_DIR_NOT_EMPTY` | 9 |
| `WSAECONNREFUSED` | 10 |
| `WSAECONNRESET` | 11 |
| `WAIT_TIMEOUT`, `WSAETIMEDOUT`, `WSAEWOULDBLOCK` | 12 |
| `ERROR_NOT_SUPPORTED` | 13 |
| any other | 100 |

The table lists representative rows; the complete mapping is recorded in [NVM](../runtime/NVM.md) in the change that implements it. Without it every Windows failure would be "other" (100), and the contract of the standard library's error codes would be lost.

### W16: File-system semantics

Files are opened with `FILE_SHARE_READ`, `FILE_SHARE_WRITE` and `FILE_SHARE_DELETE` and are not inherited. `Append` is `FILE_APPEND_DATA`. Remove and rename use POSIX semantics where the volume supports them (`FileDispositionInfoEx` with `POSIX_SEMANTICS` and `IGNORE_READONLY_ATTRIBUTE`, `FileRenameInfoEx` with `POSIX_SEMANTICS` and `REPLACE_IF_EXISTS`), falling back to `DeleteFileW` and `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`, so a read-only or open file can be removed as on POSIX. `stat` without following reports every reparse point as `Other`. The modification time is `LastWriteTime`. Long paths use the `\\?\` prefix. The providers do not retry sharing violations, which answer `permission_denied` (2); the lifecycle and the formatter retry a bounded number of times.

Write mode opens or creates the file before truncating its handle. Directory rename into its own subtree returns `InvalidInput`, including when a junction would lead into that subtree. Renaming a path to itself or changing only its case is allowed. If the volume lacks the rename operation needed to replace an empty directory, the fallback returns `Unsupported`.

`remove_tree` leaves junction targets untouched.

### W17: Console services

The byte contract is unchanged on Windows: there is no CRLF translation, and `write_line` writes one LF. A real console is read with `ReadConsoleW` and converted from UTF-16 to UTF-8, with surrogate pairs carried across reads; line-input mode delivers a line after Enter, Ctrl+Z at the start of a line is the end of input, and readiness means that an input record is pending. Pipes and files are read with `ReadFile`, and a broken pipe is the end of input.

`ReadFile` on a console returns bytes in the ANSI code page, and the descriptor source answers 13 for every read on Windows today, which breaks every console read of `Std`.

Tools put stdout and stderr in binary mode, and stdin too when it is a pipe or a file, so LSP and DAP framing and `fmt --stdin` see exact bytes. A console stdin stays in the C runtime's text mode: the console delivers a line as `line\r\n`, and text mode gives C++ readers (`std::getline`, such as the lifecycle's confirmation prompt) `line\n`, as a POSIX terminal does.

### W18: Environment

`neura.env.get` uses `GetEnvironmentVariableW`. Names match without case, as Windows matches them, and a value that is not valid UTF-16 answers 13.

The environment is an external input, so the host's rule applies. `std::getenv` returns bytes in the ANSI code page without the manifest, and is deprecated under `/W4`.

### W19: Dynamic libraries, DLL search and C import linking

Tools call `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` when they start, so implicit loads never search the current directory or `PATH`.

`Std.DynamicForeign`'s `library.open` treats text that contains `/` or `\` or starts with a drive as a path, loaded with `LoadLibraryExW` and `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` with the default directories. A bare name is searched in the application directory, `System32`, then each `PATH` entry by full path. Any extension is accepted, so the tests keep `libmathops.so`. Library 0 searches the executable, `ucrtbase.dll`, `kernel32.dll`, then the other loaded modules in `EnumProcessModulesEx` order. The ABI is Win64 through libffi's `FFI_WIN64`: `long` is 4 bytes, `wchar_t` is 2 bytes and unsigned, and aggregates whose size is not 1, 2, 4 or 8 bytes are passed by reference.

For an image with an embedded DLL, the runtime writes it into a private temporary directory and deletes it after unloading. A subsequent embedded load can clean up abandoned directories from processes that already exited. `NEURA_LIBCLANG` can select the supported DLL by full path. The loader searches installed LLVM, Visual Studio and then `PATH` entries by full path. The current directory is excluded from implicit DLL lookup.

Windows 0.3.0 reads C headers through libclang and refuses linking. In 0.3.1 the x86-64 SDK compiles the C shim with `cl.exe` or `clang-cl.exe` and links the static COFF inputs into a PE DLL with `link.exe` or `lld-link.exe`. It uses `/WHOLEARCHIVE` for archives, `/BREPRO` for reproducible linking, and a module definition file to export the symbols requested by the C imports, including data and inline wrappers. DLL import libraries and objects are refused. The shim uses the static CRT, and the SDK embeds the DLL in the existing `NATV` section. The runner loads it with the embedded DLL loader and calls it through statically linked libffi. Windows CI qualification is pending. [C imports](../lang/ForeignInterop.md#c-imports)

### W20: Locked memory

`neura.memory.lock.*` is implemented on Windows with `VirtualAlloc`, `SetProcessWorkingSetSizeEx`, `VirtualLock` and `WerRegisterExcludedMemoryBlock`, and released with `SecureZeroMemory`, `VirtualUnlock` and `VirtualFree`. `ERROR_WORKING_SET_QUOTA` and `ERROR_NOT_ENOUGH_MEMORY` answer `too_large` (16). Wipe-on-fork holds trivially, since there is no fork.

Locked pages are guaranteed not to reach the page file, and the exclusion from Windows Error Reporting is the counterpart of `MADV_DONTDUMP`'s exclusion from core dumps. Third-party dump tools are outside both guarantees.

### W21: Process generation

`neura.process.generation` is the constant 0 on Windows. A virtual machine restore is not detected, and `ChaCha20Drbg.fork_safe` behaves as on a host that never forks.

Windows has no fork and no public user-mode notification of a new virtual machine generation.

### W22: Install layout

The install root is `%LOCALAPPDATA%/Neura`; `NEURA_INSTALL_ROOT` overrides it, and `NEURA_BIN_DIR` is refused on Windows. `<root>/current` is an NTFS directory junction, retargeted in place by one `FSCTL_SET_REPARSE_POINT`. The public entry is the single `PATH` directory `<root>/current/bin`: the marker's bin line names that directory, and there are no per-tool links. The dispatcher's real path, from `GetFinalPathNameByHandleW`, names the generation. Leases and the install lock use `LockFileEx`. The marker's `bin` line and the journal's paths are absolute only as a drive path (`C:/...`) or a UNC path (`//server/share/...`); a single leading `/` names a directory of whichever drive is current and is refused. Generation removal retries a bounded number of times and is deferred while files are mapped. `neura uninstall` removes the files its own process still maps through a detached helper copied to `%TEMP%`, which waits for the uninstalling process to end.

Symbolic links need Developer Mode or a privilege and cannot be swapped atomically, and shims would add a third process start to every command. A junction needs no privilege and keeps the rule that one change of `current` moves every tool ([Install](../Install.md)).

### W23: Archives and release metadata

Windows archives are `.tar.gz`, as on every target: `neura-sdk-<version>-x86_64-windows-msvc.tar.gz` and `neura-runtime-<version>-x86_64-windows-msvc.tar.gz`, built by `packaging/make-archive.sh` under Git Bash with GNU tar's existing deterministic flags. There is no legacy `neura-<version>-<target>` archive for Windows. `neura-release.txt`, which 0.2 clients read, never lists Windows. `neura-components.txt` keeps schema 1. Extraction uses `System32\tar.exe`.

The component-metadata reader rejects the whole file unless every archive line names `neura-<product>-<version>-<triple>.tar.gz`, so a `.zip` row would break every 0.3 client already installed (`v0.3.0-rc.1`). `tar.exe` ships with Windows 10 1803 and later, and the Windows 11 Explorer opens `.tar.gz`.

### W24: Installer, bootstrap and `PATH`

The archives carry `install.ps1` and `install.cmd`, both a thin front for `bin\neura-update.exe update --install-from`; every check stays in `neura-update`. The release publishes a PowerShell bootstrap as `install.ps1`, wrapped in a function so that a partial download runs nothing, with `-Components`, `-Version`, `-Source`, `-Force` and `-NoPathUpdate`. It verifies the SHA-256 and, when `gh` is available, the attestation, as `bootstrap.sh` does. On Windows the installer adds `<root>/current/bin` to the `Path` value of `HKCU\Environment` and broadcasts `WM_SETTINGCHANGE` unless `-NoPathUpdate` is given, and uninstall removes the entry. `bootstrap.sh` run under MINGW, MSYS or Cygwin prints the PowerShell command instead of the WSL advice.

Windows has no per-user bin directory on `PATH` by default, and the user `Path` value is the platform's documented mechanism, not a shell startup file. The POSIX rule that the installer never edits shell startup files is kept in spirit and recorded in [Install](../Install.md) as a Windows difference.

### W25: Manifest file modes

On Windows, `make-archive.sh` records `file 755` for `.exe` and `.dll` files and `file 644` for every other file. Verification checks hashes and sizes; modes are not applied.

NTFS has no execute bit and the permissions MSYS emulates are not stable, but the manifest format must stay deterministic.

### W26: Update transport

`neura update` uses `System32\curl.exe` (Schannel) with the existing flags plus `--ssl-revoke-best-effort`, `System32\tar.exe`, and `gh` for attestations. `neura doctor` reports each.

Schannel's strict revocation checks fail offline and behind proxies, while the POSIX curl builds do not check revocation at all; best effort keeps the two in step.

### W27: Server host

`neura-server-host` is built on Windows. Workers are started by exec only, with `CREATE_NEW_PROCESS_GROUP`, and all workers are in one job object that kills them when the host's handle closes. The worker channel is an `AF_UNIX` socket pair; the worker's end is inherited through the handle list and named by `--channel`. One `WSAPoll` loop waits on every pollable, with socket pairs for wake-ups, and worker exits arrive through per-worker wait callbacks that post to the reaper's pair. Ctrl+C and Ctrl+Break drain. Reload is the named event `Local\neura-server-host-reload-<pid>`, set by `neura-server-host --reload <pid>`. Listeners use `SO_EXCLUSIVEADDRUSE`, every socket is non-inheritable, and `worker_exit` reports `exited N`, `signal N` or `crashed 0x...` (W10). Without the zygote, the bound on the host's processes becomes S-1 + S-2 + 1.

This keeps the single socket readiness model of the POSIX host, since `WSAPoll` cannot wait on pipes or events. The job object guarantees the documented rule that workers end with the host ([Server host](../runtime/ServerHost.md)). `--worker-start zygote` keeps its refusal (see "Out of scope").

### W28: TLS trust and OpenSSL configuration

Windows 0.3.0 serves HTTP/1.1 and supports SQLite. The server host refuses TLS listeners and HTTP/2 options that enable the protocol, and SQL pool setup refuses PostgreSQL. `Std.Net.Tls` reports that TLS is unavailable. The refusals name the Windows 0.3.0 limit. OpenSSL, nghttp2 and libpq integration and the Windows TLS trust policy are deferred to 0.3.1.

### W29: `neurad`

`neurad` is ported, not refused.

| Concern | Windows form |
| --- | --- |
| Transport | An `AF_UNIX` stream socket in an owner-only run directory under `%LOCALAPPDATA%/Neura/run`; a run-directory path longer than 107 bytes falls back to `%TEMP%/neura-<hash of the user's SID>` |
| Peer identity | `SIO_AF_UNIX_GETPEERPID` and the token's user SID, compared with the supervisor's |
| Passing handles | `DuplicateHandle` into the peer; client and supervisor run at the same integrity level |
| Images | Copied into a supervisor-owned, page-file-backed section that workers map read-only |
| Process groups | Job objects for application groups and for the supervisor's death |
| Launch envelope | The working directory is sent as a path; umask, resource limits and nice are not captured, and the priority class stands in for nice |
| Signals | Forwarded `Int`, `Term` and `Hup` end the application's job with exit code 128+N, recorded as that signal; `WINCH` and `QUIT` are not forwarded |
| Login start | The value `neurad-<id>` under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` |
| Build identity | The RSDS GUID and age of `neura-core-<version>.dll`, which `/Brepro` makes a content hash, falling back to the DLL's SHA-256 |

This keeps the supervisor's poll-based design: `AF_UNIX` sockets can be waited on with `WSAPoll`, and named pipes cannot. Windows cannot change directory to a handle or seal a mapping, and copying the image into a section that only the supervisor can write gives the same write protection as a sealed memory file.

### W30: Native executables

The 0.3.1 Win64 SDK builds and installs `neura-launcher.exe` under `lib/neura/launchers/x86_64-windows-msvc/`. MSVC and clang-cl link it with `link.exe`. It links the runner, NVM, CRT and pinned libffi statically, carries its launcher descriptor and UTF-8 manifest, and uses Windows system DLLs. `neura build --native` copies that template and appends the image container. A Neura-only build needs the SDK and operates without a linker. The resulting executable runs independently of the SDK.

The payload and footer stay in the PE overlay. The Security directory names the certificate table by file offset. The reader looks for the footer at EOF without a table, or before the table and its zero padding of up to seven bytes. Packaging clears the template's signing metadata and produces an unsigned `.exe`. Windows CI covers isolated execution, Unicode paths, inspection and replay, sealed policy, checksum, byte identity, and PE dependency closure. Portable tests exercise the synthetic certificate table, signed-template packaging and malformed directory bounds. [Native executable layout](../lang/Standalone.md#54-consequences-of-the-layout).

### W31: Desktop

In 0.3.1, `neura build --target desktop` includes the Win64 PE child (W30), the web bundle and the bridge registry. `neura run` and `neura dev` launch the pinned Electron 45.3.3 host, which starts the child without a shell, checks the registry digest and communicates over binary pipes. The data directory uses `%LOCALAPPDATA%`. Run and dev put the host in a Windows Job Object for cancellation and descendant cleanup. Dev replaces the job when a project edit changes the bundle.

The Windows desktop suite exercises the project commands, Unicode paths, bridge and window behavior, and child cleanup. The Electron cases require a Windows desktop session. Windows 0.3.0 reports that the desktop host is unavailable.

`neura package` builds the Windows desktop ZIP from the pinned Electron 45.3.3 x64 tree, the verified bundle, an ICO generated by Pillow, and the license and provider notices. The packager reports Python, Pillow and zlib versions for reproducibility. Windows CI compares repeated archives, extracts and launches the packaged host and PE child under a fresh Unicode profile, and checks the job ends after the window closes. [Desktop packages](../apps/Desktop.md#packages).

### W32: Project tools

`neura build --project` starts `neurac` with its working directory set and records the source path relative and in generic form. `neura run <bundle>`, `neura serve` and `neura dev` run hosts as children in kill-on-close jobs and return their status. `neura dev` stops the running host before it replaces `dist/dev/<name>`, because Windows locks running executables, and stops children with Ctrl+Break, then ends the job after 2 seconds. Server packages for Windows targets are `.zip` with `run.cmd`.

There is no `execv` on Windows, and the rebuild-while-running model of `neura dev` fails on locked files. Packages are not read by the updater, so `.zip` is the natural Windows container for them.

### W33: GPU

The policy is unchanged: release archives have no GPU backend on any target. Source builds with `NEURA_GPU=AUTO` find the Vulkan SDK through `VULKAN_SDK` or vcpkg's `vulkan-headers` and `glslang`. The runtime loads `System32\vulkan-1.dll`. A device loss from a Windows timeout detection and recovery is a GPU failure, handled by the existing fallback rules.

The loader's Windows branch already exists; only the build's discovery hints are missing.

### W34: Testing

The same CTest suite runs on Windows. An entry that cannot run there is not registered and is listed with its reason in [Continuous integration](../ContinuousIntegration.md). Cases inside an entry that need a privilege (symbolic links) use junctions or report themselves as not run within the entry's output. Test drivers stop process trees with `CREATE_NEW_PROCESS_GROUP` and Ctrl+Break, inspect processes without `/proc`, use `NUL` for `/dev/null`, and compare paths after normalizing separators. Python 3.11 or later and Git for Windows are build and test prerequisites, not runtime ones. `NEURA_LIBCLANG` names a libclang 14 to 21 in CI.

The CI report refuses skipped entries, and hosted runners may lack the symbolic-link privilege.

### W35: Release scope

Windows ships in 0.3.0 for both products. `release.yml` builds and qualifies `x86_64-windows-msvc`. `packaging/qualify.py` reports check 9 (`update` from the previous release) as `NOT RUN` when the previous release has no Windows archive and check 13 (the 0.2 bridge) as `NOT RUN` because there is no Windows 0.2 client. The [release process](../ReleaseProcess.md#go-or-no-go) records these exceptions. The Windows SDK and runtime archives may be at most 125% of the same build's `x86_64-linux-gnu` archives of the same product. Record measurements from the first green trial in [Release030](Release030.md). Windows binaries ship unsigned in 0.3.0 and carry build-provenance attestations. The owner handles tag creation and publication through the release process.

There is no rc.3 Windows archive for the existing rule of the budget to compare against, so the budget is relative to Linux. Moving a tag and publishing a release are irreversible steps that only the owner takes.

## Ownership rules

The port is divided into packages so that work can proceed in parallel without two packages editing one file.

| Rule |
| --- |
| Each file belongs to exactly one package. A package edits, creates or deletes only the files its list matches, minus its exclusions; globs cover new files created under them by that package. |
| WP00, the foundation, finishes before any other package starts. A package that needs a change in a file it does not own stops and reports the change to the owner instead of editing; a file that no package lists is not edited without amending the plan. |
| Windows build logic for a gated area goes only into that package's `cmake/windows/<area>.cmake`, which may define only `neura_windows_<hook>` macros and `NEURA_WINDOWS_<AREA>` variables. A change to `CMakeLists.txt` after WP00 is an amendment of the plan. |
| POSIX code paths and POSIX CMake text stay byte-identical unless the package also runs the POSIX tests that cover the change (Tier 1 on the pushed branch). |
| `third_party/neura-libclang` is a submodule: WP06 commits there first and then moves the pointer in this repository. |
| Every package records the user-visible behavior it implements as a note for WP18 (documentation) and keeps to this note's decisions. A package that cannot meet a decision stops and reports instead of choosing another behavior silently. |

The table summarizes each package's file list in the port's plan, which is exact: a file that no list names is not edited without amending the plan.

| Package | Area | Depends on | Owns |
| --- | --- | --- | --- |
| WP00 | Foundation: toolchain, build system, platform layer, product layout, dispatcher, the baseline's compile blockers | | `CMakeLists.txt`, `CMakePresets.json`, `.gitattributes`, `vcpkg.json`, `cmake/NeuraWindows.cmake` and the other `cmake/` templates, `src/platform/`, `src/util/int128.hpp`, `src/util/line_endings.hpp`, the dispatcher and tool start (`src/cli/main.cpp`, `tool*.cpp`, `serve.cpp`, `install_layout.cpp`, `commands_test.cpp`), the compile-blocker files in the kernels, circuit, lift, converter, tier 3 and inliner, `src/project/manifest.*`, the affected test files, `tests/platform/`, this note |
| WP01 | JIT tiers 2 and 3 on Windows x86-64 | WP00 | The JIT's entry, emitter, assembler and memory files (`src/jit/jit.*`, `x86_64.*`, `x64.*`, `tier3_emit.cpp`, `tier3_native.hpp`, `assembler.hpp`), the JIT tests, `scripts/runtime-identity.py`, `tier-boundary.py`, `tier3-budget.py` |
| WP02 | NVM provider core: handles, error mapping, fs sandbox, console, environment, waits, locked memory, runner | WP00 | `src/nvm/providers.*`, `provider_internal.hpp`, `host.*`, `pending_requests.cpp`, `secret_services.*`, `src/runner/` |
| WP03 | NVM file-system services | WP02 | `src/nvm/fs_provider.cpp` |
| WP04 | NVM network and socket services on Winsock 2 | WP02 | `src/nvm/net_provider.cpp`, `socket_provider.cpp` |
| WP05 | Native interop with libffi | WP02 | `src/nvm/ffi_provider.cpp` |
| WP06 | Frontend and C import: source loading, CRLF normalization, deterministic ordering, libclang | WP00 | `src/cli/source_loader.*`, `src/lexer/`, `src/parser/`, `src/c-import/`, `third_party/neura-libclang/`, the frontend and UI tests |
| WP07 | Editor and SDK tooling: LSP, DAP, fmt, test protocol, diagnostic colors | WP00 | `src/cli/driver_sdk.cpp`, `src/tooling/`, `tests/tooling/` |
| WP08 | Install lifecycle, archives, installer, bootstrap, qualification | WP00 | `src/cli/lifecycle.*`, `packaging/` scripts (new `install.ps1`, `install.cmd`, `bootstrap.ps1`, `qualify.py`), `tests/lifecycle/scenarios.py` |
| WP09 | `neurad`, `run --runtime` and the runtime commands | WP02 | `src/service/`, `cmake/windows/service.cmake`, the `neurad`, `neura-run`, `neura-update` and `neura-uninstall` tool sources, `src/cli/options.cpp`, `commands_run.cpp`, `tests/service/` |
| WP10 | Server host | WP02 | `src/server/` except TLS, `cmake/windows/server.cmake`, `tests/server/` except TLS |
| WP11 | Server components: OpenSSL, nghttp2, libpq, TLS, SQL | WP10, WP04 | `cmake/windows/server-components.cmake`, the server-component recipes and lock, `src/server/http_tls.*`, `src/nvm/tls_provider.cpp`, `src/sql/` |
| WP12 | Native executables and C import linking | WP02, WP05 | `src/launcher/`, `src/cli/commands_compile.cpp`, `cmake/windows/launchers.cmake`, `tests/native/`, `tests/c-import/` |
| WP13 | Project tools | WP00 | `src/app/`, `src/project/` except the manifest, `cmake/windows/project-tools.cmake`, `templates/`, `tests/project/` except the manifest tests |
| WP14 | Desktop host | WP13, WP12 | `hosts/desktop/`, `scripts/package-desktop.py`, `tests/desktop/` |
| WP15 | Standard library contracts and tests | WP00 | `stdlib/`, `tests/stdlib/` |
| WP16 | Remaining test portability | WP03, WP04, WP05, WP11 | The host-service, evaluator, app, wasm and benchmark drivers and their scripts |
| WP17 | CI and release automation | WP00, WP08 | `.github/`, `scripts/check-kernels.sh`, `check-core-closure.py`, `check-windows-binaries.py`, `gen-kernels.py`, `release-notes.py`, `test-checks.py` |
| WP18 | Documentation, changelog, wiki and site | every other package | `README.md`, `CONTRIBUTING.md`, `CHANGELOG.md`, `docs/` except this note, `wiki/`, `site/contents.toml`, `prompts/` |

The packages run in waves: WP00 alone; then WP01, WP02, WP06, WP07, WP08, WP13 and WP15; then WP03, WP04, WP05, WP09, WP10 and WP17; then WP11 and WP12; then WP14 and WP16; then WP18.

## Out of scope

Each case below has a defined refusal; none is left to fail in an unspecified way.

| Item | Reason | Refusal |
| --- | --- | --- |
| Native JIT | Win64 code generation and calling conventions await qualification | `--jit=auto` uses the interpreter. `--jit=on` fails with `Windows has no JIT in 0.3.0` |
| Native executable generation in the 0.3.0 archive | The PE launcher is a 0.3.1 addition (W30) | The 0.3.0 command reports `Windows has no native launcher in 0.3.0` |
| Server TLS, HTTP/2, `Std.Net.Tls` and PostgreSQL | OpenSSL, nghttp2 and libpq integration is deferred to 0.3.1 | TLS listener startup, HTTP/2 enablement and PostgreSQL pool setup name the Windows 0.3.0 limit. `Std.Net.Tls` reports that TLS is unavailable in the runtime |
| Desktop host and desktop project operations in the 0.3.0 archive | W31 records the 0.3.1 addition | The 0.3.0 commands report `the desktop host is unavailable on Windows in Neura 0.3.0` |
| The server host's fork-based worker zygote (`--worker-start zygote`) | Windows has no fork, and cloning a Win32 process that uses the CRT and Winsock is unsupported, so a pre-forked worker cannot be made safely | The existing `--worker-start zygote needs Linux`; the default on Windows is exec, as on macOS |
| The fully static launcher (`neura build --native --static`, `neura-launcher-static`, `NEURA_STATIC_LAUNCHER=ON`) | The fully static flavor requires Linux | The Windows launcher search reports `--static: the fully static launcher requires Linux`. CMake reports `NEURA_STATIC_LAUNCHER requires Linux` |
| Unix-domain datagram sockets and the abstract namespace (`neura.net.unix.datagram`, `unix.send_to`, `unix.receive_from`, `@` names) | Windows `AF_UNIX` implements only stream sockets and has no abstract namespace | 13 (`Unsupported`), as macOS answers for abstract names; the standard library's documentation and tests say so |
| Linux-only socket services: pipe, splice, MPTCP, `TCP_USER_TIMEOUT`, `SO_REUSEPORT` | No Windows equivalent; macOS answers the same way today | 13 (`Unsupported`), documented in [NVM](../runtime/NVM.md) |
| The epoll and io_uring backends (`NEURA_IO_BACKEND`, `nvm-wait-source-epoll`, `nvm-wait-source-io-uring`) | Linux kernel interfaces | `NEURA_IO_BACKEND` is ignored on Windows, as documented; the two CTest entries stay Linux-only |
| Detecting a virtual machine restore for `neura.process.generation` | Windows exposes the VM generation ID only to drivers, and without fork the generation never changes | The generation is the constant 0 (W21), documented in [NVM](../runtime/NVM.md) and [Standard library](../lang/Stdlib.md) |
| UNC roots in `Std.Fs.Path`'s lexical operations | `Path` collapses a leading `//` on every host, so a UNC root cannot be modeled without changing POSIX results; mapped drive letters work, and UNC text with backslashes passes through the Windows fs host as one `Path` component | Recorded in [Standard library](../lang/Stdlib.md) as a limitation pending a language decision; no silent rewrite |
| `.msix` desktop packages | An MSIX cannot be installed unsigned, and `neura package --sign` is refused on every host in 0.3.0 | E1033 naming the missing signing step, as for `.dmg` on macOS |
| Authenticode signing of release binaries | Not a platform limitation: the project has no code-signing certificate, and SignPath Foundation requires a published Windows binary first ([Code signing](../CodeSigning.md)) | Releases state that the binaries are unsigned and rely on build-provenance attestations; SmartScreen and Smart App Control warnings are documented |
| Windows on Arm64 (`aarch64-windows-msvc`) | Outside the x86-64 scope; it needs an AArch64 Windows variant of the JIT (`x18` reserved, other unwind codes), its own launcher and a qualifying runner | No archive is published; [Install](../Install.md) lists it as unsupported; the x86-64 build under emulation is not qualified |
| The POSIX-only fields of the service launch envelope (umask, resource limits, nice) | Windows has no umask, no resource limits of that kind and no nice value | Not captured or sent by Windows clients; the priority class stands in for nice; documented in [Service030](Service030.md) |
| Test-only Linux mechanisms: the Vulkan fault loader, the user-namespace cases of `native-install`, Xvfb desktop runs | They depend on Linux loader, namespace and X11 facilities | They do not run on Windows. See [Continuous integration](../ContinuousIntegration.md) |
| `server-sql-postgres` | Windows 0.3.0 defers the PostgreSQL provider | Windows tests exercise the unsupported provider response. Release qualification reports the PostgreSQL probe as `NOT RUN` |

## Verification

| Evidence | Check |
| --- | --- |
| Line endings | `git ls-files --eol` lists CRLF in the working tree only for `*.ps1`, `*.cmd` and `*.bat` |
| Builds | The `windows-msvc`, `windows-clang-cl` and `windows-msvc-debug` presets build with no errors, and a configure with `NEURA_SDK=OFF` builds the runtime-only set |
| Product layout | `dumpbin /exports` on both DLLs lists only `neura_tool_*`; `dumpbin /dependents` shows each tool importing exactly one Neura DLL and system DLLs, and `neura.exe` none; `dumpbin /directives` shows only the static CRT; the release check enforces the allowlist of W05 |
| Text and console | `neura --version` prints `neura <version> (x86_64-windows-msvc, commit ...)` followed by LF only; `neura run` of a hello-world image works from a directory with a non-ASCII name and from an install of the `core` component; Ctrl+C during a long program exits with 130 |
| Suites | The Windows CTest registrations pass with cl and clang-cl, and the MSVC warning gate is clean. The GitHub jobs run on `windows-2022` |
| Image identity | `lift-portable`'s fixed digest, and the comparison of the images built on Linux and Windows from one corpus (W13) |
| JIT | The Windows JIT test verifies that `--jit=auto` uses the interpreter and that an explicit JIT request names the 0.3.0 limitation |
| POSIX unchanged | Tier 1 and Tier 2 on Linux and macOS pass with unchanged test counts on the pushed branch |
| Release | A trial of the release workflow is green on all five targets, and qualification on `windows-2022` passes every applicable check (W35); a manual check on Windows 10 22H2 and Windows 11 follows [Release process](../ReleaseProcess.md) |

## Risks

| Risk | Handling |
| --- | --- |
| Differences in argument evaluation order or STL ordering under cl change graphs and so images | `lift-portable` and the cross-host image comparison stay green; a mismatch is fixed in the code, not accepted |
| The Win64 native ABI needs verification | Windows 0.3.0 uses the interpreter and refuses an explicit JIT request |
| Inheriting sockets through the handle list fails when a non-IFS layered service provider is installed | The server host and `neurad` report it clearly; a connect-back channel is the fallback if field reports show it |
| Antivirus and indexer handles cause sporadic sharing violations on rename and delete | Only the lifecycle, the formatter and the bundle writer retry, so provider-level tests must not depend on the immediate deletion of a file another process scans |
| Windows starts processes slowly (tens of milliseconds), and a run without the JIT is several times slower | CI timeouts use scale factors; the 1,000-request server load case may need the scaled limit |
| Unsigned binaries meet SmartScreen and, on clean Windows 11 installations with Smart App Control, may be blocked | Documented, not solved, in 0.3.0 (W35) |
| WP00 is large and on the critical path | Its compile-blocker and product-layout tasks land first, so the packages of the first wave start from a tree that builds |

## Results

Each package adds its measured evidence here, with the date and the commit, as its change lands; the archive sizes of W35 go to [Release030](Release030.md).

### WP00, foundation (2026-11-20, before its commit)

Measured on Windows 11 with cl 19.44, the Visual Studio clang-cl 19.1.5, the Windows SDK 10.0.26100, CMake 4.4 and vcpkg's libffi 3.8.0 (`x64-windows-static`). Linux and macOS were not built; Tier 1 and Tier 2 on the pushed branch remain to run.

| Check | Result |
| --- | --- |
| `windows-msvc` | Builds with no errors (425 steps). The only warning is C4100 in `src/cli/commands_compile.cpp`, which WP12 owns |
| `windows-clang-cl` | Builds with no errors; its `-Wunused` warnings are all in files that later packages own |
| `windows-msvc-debug` | Configures; the build stops at the `std::is_standard_layout_v<Context>` assertion in `src/nvm/tier3.hpp`, because a `std::vector` member is not standard layout under MSVC's debug iterators |
| Product layout | The core DLL exports the 5 core `neura_tool_*` entries and the SDK DLL the 14 SDK entries, nothing else. Each tool stub imports one Neura DLL and `KERNEL32.dll`, `neura.exe` imports only `KERNEL32.dll`, the DLLs import `bcrypt.dll` and `KERNEL32.dll`, and every object names `LIBCMT` and no DLL runtime |
| Text and console | `neura --version` prints `neura 0.3.0-dev (x86_64-windows-msvc, commit unknown)` and LF. A hello-world image built in a directory named `hello dé 中` runs from the build tree, from a full install and from an install of the `core` component. Ctrl+C during a long run exits with 130 and Ctrl+Break with 131 |
| `lift-portable` | 25,972 graph lines match the GCC digest and every hint lifts, with cl and with clang-cl |
| Platform layer | `platform-*` passes 8 of 8 with cl and with clang-cl. After the review fixes, `platform-*`, `util-int128`, `util-line-endings` and the `lifecycle-unit-*` entries, `lease` among them, pass with cl |

The entries checked were WP00's list plus the console-only `stdlib-*` areas. They ran on a copy of the tree with LF line endings, as `.gitattributes` makes the checkout, and 106 of the 122 passed. The 16 failures come from these causes:

| Entries | Cause |
| --- | --- |
| `frontend`, `ir-optimization-structure`, `-verifier`, `-behavior` and `-cli` | C import cannot find `stdlib.h`, and neither can 3 cases of `backend-nir` (WP06) |
| `neura-libclang-load` | Unloading libclang 19.1.5 with `FreeLibrary` makes the process crash at exit (WP06) |
| `backend-cli`, `concurrency-schedules`, `source-docs` | CMake's `file(WRITE)` writes CRLF on Windows, so the fixtures these scripts write are not what the tools are given on POSIX |
| `backend-nir` | Its golden case renames the source by `fs::canonical(...).string()`, a backslash path that the lowered program does not hold |
| 3 `stdlib-crypto-*` areas | They exceed their 1,500 s limit without the JIT (W11) |
| `tools-parity`, `tools-components`, `tools-install` | The host policy described below, and `neura doctor`'s refusal on Windows (WP08) |

The rerun after the review fixes was limited by that host policy: in the LF copy every newly linked executable, `neura.exe` among them, was blocked, so the entries that start them did not run there. In the CRLF checkout the same entries ran; the only failures beyond those in the table were `project-toml`, `-manifest` and `-lock`, which read CRLF fixtures there and pass in the LF copy, and the entries that start a blocked `neura-disasm.exe` or `neura_diagnostic_tests.exe`.

On this machine, Smart App Control blocks some newly linked or copied executables. An entry that starts such a file fails with "An Application Control policy has blocked this file", and that failure comes from the host, not the port.
