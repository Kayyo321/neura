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

No version has been published. The [release checklist](docs/Release.md) lists the language, toolchain, standard library, and decompiler features on `main`, each with the tests that establish it.

### Added

- `VERSION` is the one source of the tool's version. `neura --version` and `neura version` print `neura <version> (<target>, commit <commit>)`; `neura help` and `neura --help` print the usage. A local build reports `<version>-dev`, and the native launcher descriptor carries that version in place of `development` ([Install](docs/Install.md), [Tooling](docs/lang/Tooling.md#version-install-and-update)).
- `neura doctor` checks an install and its optional tools. `neura update [--check] [--source] [--require-attestation]` replaces an archive install with a newer release after verifying its size, SHA-256, and, with `gh`, its build provenance, and rolls back if activation fails. `neura uninstall [--dry-run] [--yes]` removes an archive install. They exit with status 3 for an install that `neura update` does not manage, and `update --check` exits with status 4 when a newer version exists ([Install](docs/Install.md)).
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

### Security

- A native executable finds its own file through `/proc/self/exe` or the path the kernel recorded, never through its program name, and writes traces through an exclusively created temporary file. `--trace-events` refuses a symbolic link at its path.
