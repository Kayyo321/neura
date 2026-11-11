# Changelog

This file records the changes in each version of Neura that a user of the language, the `neura` command, the standard library, or the `.nir` and `.nvr` formats can observe. Its layout follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versions are numbered `MAJOR.MINOR.PATCH`. Before `1.0.0`, a minor version may change or remove existing behavior, and its entry lists each such change under **Changed** or **Removed**. The `.nir` and `.nvr` formats carry their own version numbers, which the [NeuraIR format](docs/NirFormat.md) and [NVR format](docs/runtime/NVR.md) pages define.

A pull request that changes observable behavior adds a line to **Unreleased** under one of these headings:

| Heading | For |
| --- | --- |
| Added | New syntax, library members, commands, options, or format features. |
| Changed | Existing behavior that now differs, including a diagnostic or exit status that programs or scripts may depend on. |
| Deprecated | Behavior that still works and is scheduled for removal. |
| Removed | Behavior that no longer exists. |
| Fixed | Behavior corrected to match its specification. |
| Security | Fixes for vulnerabilities reported under the [security policy](SECURITY.md). |

## Unreleased

Nothing yet.

## [0.1.0] - 2026-11-11

The first release. The [release checklist](docs/Release.md) lists the language, toolchain, standard library, and decompiler features in it, each with the tests that establish it. [Install and update](docs/Install.md) lists the supported platforms: Linux and macOS on x86-64 and arm64, and Windows through WSL 2.

### Added

- The Neura language skill for AI coding agents in [`prompts/neura`](https://github.com/Kayyo321/neura/tree/main/prompts/neura): the syntax, modules and imports, the standard library with a generated signature index, concurrency, the command line, diagnostics, and style, with every example compiled or run by the `doc-examples` test. The README and the website's front page carry a prompt that installs it ([Write Neura with an AI agent](README.md#write-neura-with-an-ai-agent)).
- `VERSION` is the one source of the tool's version. `neura --version` and `neura version` print `neura <version> (<target>, commit <commit>)`; `neura help` and `neura --help` print the usage. A local build reports `<version>-dev`, and the native launcher descriptor carries that version in place of `development` ([Install](docs/Install.md), [Tooling](docs/lang/Tooling.md#version-install-and-update)).
- `neura doctor` checks an install and its optional tools. `neura update [--check] [--source] [--require-attestation]` replaces an archive install with a newer release after verifying its size, SHA-256, and, with `gh`, its build provenance, and rolls back if activation fails. `neura uninstall [--dry-run] [--yes]` removes an archive install. They exit with status 3 for an install that `neura update` does not manage, and `update --check` exits with status 4 when a newer version exists ([Install](docs/Install.md)).
- macOS support: Neura builds and passes its tests on macOS 15, on Apple silicon and Intel. C imports find the SDK's headers and link with Apple's linker, and a C import also takes declarations from the underscore-named headers a system header is made of, such as `_stdlib.h` ([Foreign interop](docs/lang/ForeignInterop.md)). There is no native launcher for macOS.
- The release workflow (`.github/workflows/release.yml`): a `v*` tag builds, tests, packs, and qualifies an archive for Linux and macOS on x86-64 and arm64, and publishes it with `SHA256SUMS`, `neura-release.txt`, and build provenance attestations ([release process](docs/ReleaseProcess.md), [code signing policy](docs/CodeSigning.md)). `NEURA_PORTABLE_RELEASE=ON` links libffi and the C++ runtime into `neura`.
- A one-line install: each release publishes `install.sh`, which downloads the archive for the machine, verifies its size and SHA-256 (and its build provenance when `gh` is installed), and installs it ([Install](docs/Install.md#install-with-one-command)).
- Release archives: `packaging/make-archive.sh`, `make-release-metadata.sh`, and the `install.sh` that each archive carries, with the install layout, marker, and `neura-release.txt` format described in [Install](docs/Install.md). The installed tree gains `share/doc/neura/LICENSE`.
- An update notice on standard error, off unless the install is an archive install and standard input and standard error are terminals, at most one network check every 7 days; `NEURA_NO_UPDATE_CHECK=1` or `CI` turns it off ([Install](docs/Install.md#update-notices-and-privacy)).
- Scoped tasks: `scope { }`, `scope (name) { }`, `spawn`, `join`, and `select`, with compile-time checks that values given to a task are transferable, that spawned calls are task-safe, and that task and scope handles do not leave their scope ([Concurrency](docs/lang/Concurrency.md)).
- `Std.Concurrent`: `Task`, `Scope`, `Channel`, `Cancellation`, `Timer`, `Tasks`, `WorkerGroup`, `Parallel`, `AtomicInt64`, `Semaphore`, and `Guarded`.
- Cancellation and deadlines for scopes, and host I/O that overlaps other tasks and can be cancelled: `TcpStream` and `TcpListener` operations, `Console` input, and `SystemClock.sleep`. `IoErrorKind.Cancelled` reports a cancelled operation.
- The scoped resource helpers `File.with_open`, `TcpStream.with_connection`, and `Closeable.release`, and `Error.to_portable` and `from_portable` for sending an error over a channel.
- `neura run` and `neura test` options for concurrent programs: `--max-tasks`, `--task-quantum`, `--max-queued-messages`, `--max-pending-requests`, `--parallel-workers`, `--task-dump[=text|json]` (also on `SIGQUIT`), `--virtual-time`, and for `run`, `--trace-events` and `--trace-wall-time`.
- `neura test --schedules N`, `--schedule-seed S`, `--save-schedule <file>`, and `--schedule <file>`: run tests under several legal task schedules and reproduce a failing one.
- `neura check --task-safety`: report, for every function, whether a spawned task may call it.
- Record and replay of programs with tasks (trace file version 3).
- `neura build --native`: package an image as a native executable with a recorded policy of capabilities, limits, and task settings; `neura inspect` reads it back; `NEURA_RUN_OPTIONS` restricts or observes a run ([Native executables](docs/lang/Standalone.md)). The fully static flavor (`--static`) has no native interop and resolves no host names.
- GPU execution of eligible graphs for `neura run` and `neura test`, with `--gpu=auto|always|off`, `--require-gpu`, `--gpu-verify`, and `run --each` for batched runs ([GPU execution](docs/lang/GpuExecution.md)).
- NVR 1.5 (scheduler operations, poll bits, and the transferable-layout table) and `.nir` 1.9 (`task_scope`, `spawn`, `scheduler_call`, and polls).
- `--deny-fs`, `--deny-net`, `--deny-env`, `--deny-clock`, `--deny-entropy`, `--deny-native`, and `--deny-all`.

### Changed

- `neura run`, `neura test`, and the policy that `neura build --native` records now grant every host service by default: files, network, environment, clock, entropy, and native code. Before, each needed its `--allow-*` option. Run an image you do not trust with `--deny-all` and the `--allow-*` options it needs. The capability options now apply in command-line order, so `--allow-fs --allow-fs=prefix` grants only the prefix.
- A collection's `to_string` prints `(this collection)` wherever its own text reaches it again, including through other collections. An indirect cycle used to end with the frame-limit fault.
- The trap for denied native access reads `native access denied: the native capability is not granted`.
- A program that uses a scope, a channel, or another scheduler operation cannot also use `Std.DynamicForeign` or `import:c(...)`.

### Fixed

- The install script and `neura update` treated a `gh` that is installed but not signed in as a failed provenance check and stopped. They now report that provenance was not verified and continue, as they do without `gh`; `--require-attestation` still stops. Found by the release workflow on the candidate `v0.1.0-rc.1`, whose published `install.sh` has this defect; `0.1.0` does not.

### Known issues

- `neura run --each <file> --gpu=always` does not finish for an image whose tasks wait on cancellable host I/O, such as `tests/concurrency/io/cancelled.nra`; the same fleet finishes with `--gpu=off` and with the default `--gpu=auto`. [GPU execution](docs/lang/GpuExecution.md) specifies that a fleet gives the results of running its members in order, so this is a defect, not a documented limit. The release archives are built without the GPU backend and are not affected; a build from source with the backend is.
- On Linux arm64, `neura-libclang-load`, a test of the `neura-libclang` submodule, fails because it expects `char` to be signed. C imports themselves map `char` as the platform does.

### Security

- A native executable finds its own file through `/proc/self/exe` or the path the kernel recorded, never through its program name, and writes traces through an exclusively created temporary file. `--trace-events` refuses a symbolic link at its path.
