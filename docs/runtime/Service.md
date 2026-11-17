# The runtime service

`neurad` is an optional, per-user service that runs images on request. A command opts in with `neura run --runtime=auto` or `--runtime=required`; without that option `neura run`, `neura test`, `neura debug` and the editor run directly, as before, and never look for the service. The [design note](../design/Service030.md) holds the rules, the protocol and the edge cases; this page describes the service as it is built.

The service changes no compiler stage and no image. It is a way to launch an image: an image runs under it as it runs directly, with the same output, status, traps, step counts and trace events. It is not a sandbox and has no network listener. Linux is the qualified platform; the macOS code paths (login start through `launchctl`, a thread that watches the supervisor in place of the Linux parent-death signal, `_NSGetEnviron`) are written but not tested.

## Using it

```sh
neura run --runtime=auto app.nvr -- arguments      # through the service, or directly when it cannot
neura run --runtime=required app.nvr               # through the service, or status 5
neura run --runtime=required --detach --stdout=out.log --stderr=err.log app.nvr
neura runtime ps
neura runtime stop <id>
```

`--runtime=auto` starts the service when it is not running, and runs directly, with one line `neura: runtime: running directly: <reason>` on standard error, when it is not installed, cannot start, refuses the launch, or does not support an option of the command. `--runtime=required` never runs directly: it exits with status 5 instead. `--detach` prints the application's ID and exits once the launch is committed; it never runs directly. `--runtime-report=FILE` writes one JSON object about the launch in every mode: `schema` 1, the `path` taken (`direct`, `service-warm`, `service-fresh`, `direct-fallback`), the `reason` of a fallback or refusal, the `application` ID, the terminal `state`, the `status`, and the `times` of the launch's phases in nanoseconds from the client's start.

The service does not support `--each`, `--record`, `--replay` or a graph device in 0.3.0. `neura test --runtime=...` is a usage error, and a debug launch whose `runtime` setting is not `off` is refused before anything starts: tests and debugging are direct.

| Command | Does |
| --- | --- |
| `neura runtime start` | Starts the supervisor of this installation's active generation if it is not running. It stays when idle. |
| `neura runtime status [--json]` | Whether the component is installed, whether login start is registered, and each live generation's supervisor: state, epoch, applications, ready workers, cache bytes. Starts nothing. |
| `neura runtime ps [--json] [--all]` | The applications that are not terminal; `--all` adds the terminal records still kept. Starts nothing. |
| `neura runtime inspect ID [--json]` | One application's record: state, status, trap class or signal, image path and digest, argument count, mode, limits, lifetime, path and times. Starts nothing; reads the records of a supervisor that is gone. |
| `neura runtime stop ID [--grace=SECONDS]` | Ends the application's process group, `SIGTERM` then `SIGKILL` after the grace period (10 seconds), and prints its terminal state. An application that already ended prints its state; an expired ID exits with status 1. |
| `neura runtime shutdown [--generation=G] [--timeout=SECONDS] [--terminate]` | The supervisors refuse new launches and exit when their applications have ended. After the timeout (60 seconds) the command lists what still runs and exits with status 1. `--terminate` stops the applications first. |
| `neura runtime enable`, `disable` | Register or remove the start of the supervisor at login (`systemctl --user`; `launchctl` on macOS). Where no user service manager answers, `enable` changes nothing and exits with status 1. `disable` stops nothing. |
| `neura runtime cache clear` | Drops every cached image that no running application uses. |

`neura runtime ...` is `neura-run runtime ...`, and needs `neurad` next to `neura`: `cmake --install build --component service` installs it (a plain `cmake --install` does not, so an SDK prefix holds no service).

`neura capabilities --json` has a `service` member read from the files of the installation, without connecting to anything: `{"installed":false,"state":"missing"}`, or `{"installed":true,"protocol":"1.0","state":"ready"}`, or the state `unmigrated` on an installation that a 0.2 updater produced and that is not migrated, which refuses `start`, `enable`, `auto` and `required` (status 5) until `neura update --migrate-install`.

## How a launch runs

```text
client (neura-run)                  supervisor (neurad --foreground)          worker (neurad --worker)
  read image, SHA-256  -- Prepare -->  admit; reserve; warm or fresh
                       <-- Prepared --
  [image descriptor]   -- Image ---->  copy into its own sealed memory file
  cwd, stdin/out/err   -- Commit ---->  give the launch to a worker  -- Assign -->  environment, umask, limits,
                       <-- Committed --                                            nice, directory, process group,
  forward signals      -- Signal ---->  killpg(application)                       streams 0-2, signals, map image,
                       <-- Outcome ---  record the terminal state   <-- Finished -- run_image() as a direct run
```

The client parses the options with the code of a direct run and sends the result: the capability policy resolved against its directory, every limit an option sets, the runner's options, its whole environment, its umask, its soft and hard resource limits, its nice value, its ignored signals and signal mask, and the reading of its clock when it started, so that `--tier-stats` covers the whole command. The working directory and the three streams travel as descriptors, so the program writes to the caller's own terminal, pipe or file; the service relays no byte. A worker replaces its environment with the caller's before any runtime code reads `NEURA_*`: the readers that keep their first answer abort a worker that reads them earlier ([NVM, controls](NVM.md#controls)).

A worker runs one application and exits. The supervisor keeps one ready worker, started with its own environment; a launch whose loader and allocator variables (`LD_*`, `DYLD_*`, `GLIBC_TUNABLES`, `MALLOC_*`) or stack limit differ from it, or that finds no ready worker, gets a worker started for it with its own environment and limits (the `fresh` path). A setting no worker can be given, such as a soft limit above the supervisor's hard limit or a nice value below the supervisor's, is refused as `not-reproducible`, and `auto` runs directly. A supervisor started on demand by a launch inherits that first caller's nice value and limits.

What a service launch does not have of the caller's process: its session, process group, controlling terminal and parent, its descriptors above 2, and its control group. Reading a terminal through the passed descriptors works; the terminal's signals reach the client, which forwards `INT`, `TERM`, `HUP`, `QUIT` and `WINCH` to the application's process group.

## Statuses and messages

Errors of the service are lines that begin `neura: runtime: `, printed by the client; the program's own output is never prefixed.

| Status | Meaning |
| --- | --- |
| the application's | A committed foreground launch that ended: its exit status, its trap's status, or 128 + N after `neura: runtime: the application was ended by signal N` |
| 1 | A usage error; `shutdown` that timed out; `enable` without a service manager; `inspect` or `stop` of an ID that is not of this installation, or expired |
| 5 | The service ran nothing and nothing was started: not installed, could not start, refused, an unsupported option under `required`, another build or user answering (`identity`), the image changed while it was launched (`image-mismatch`), an unmigrated installation |
| 6 | The outcome is unknown: the commit was sent and no outcome arrived; the image was not run again |

Terminal states: `exited`, `trapped`, `load-failed`, `stopped`, `client-lost` (the foreground client went away), `signaled` (a signal the service did not send, a forwarded one included), `worker-lost` (the worker ended without reporting, for example by `SIGKILL`), and, for a reader of a supervisor that is gone, `unknown` (the record was live) and `expired` (no record is kept for a number its epoch issued). An application ID is `<generation>.<8 hexadecimal digits of the epoch>.<number>`; it is never a process ID.

## Files

| Path | Holds |
| --- | --- |
| `<run>/neura/<endpoint>/` | Mode 0700: `s`, the control socket (0600); `lock`, held by the supervisor for its life; `start`, held by a client while it starts one. `<run>` is `$XDG_RUNTIME_DIR` when it is absolute, this user's and mode 0700, else `${TMPDIR:-/tmp}/neura-<uid>`. `<endpoint>` is 16 hexadecimal digits of the SHA-256 of the root's real path, a NUL byte and the generation. |
| `${XDG_STATE_HOME:-$HOME/.local/state}/neura/<id>/<generation>/` | `records/<application ID>` (JSON, written whole), `epoch-<epoch>` (the highest number the epoch issued), `log` (at most 1 MiB and one rotated copy). Terminal records are kept 24 hours, at most 1,024. |
| `~/.config/systemd/user/neurad-<id>.service`, `~/Library/LaunchAgents/net.neura.neurad-<id>.plist` | Login start |

`<id>` is 16 hexadecimal digits of the SHA-256 of the real path of the install root, or of a source build's `bin` directory, whose generation is `dev`. No record holds an argument, an environment value or program output, and nothing mutable is written under the install root. The image cache is in the supervisor's memory: at most 256 MiB, an image in use is never evicted, and a worker validates the bytes every time.

## Limits

The baseline's: one ready worker, 64 applications, 16 prepared launches, 256 MiB of cached images, 1,024 terminal records for 24 hours, messages of at most 1 MiB. A launch over a limit is refused; an application's own limits are never lowered to admit it. A supervisor started on demand exits after 15 idle minutes; one started by `neura runtime start` or at login stays.

## Measurements

`scripts/bench-launch.py` measures each launch end to end from outside the command, and the phases of a warm launch from `--runtime-report`. On the machine of the milestone's measurements ([Service030.md](../design/Service030.md#measurements)), a direct run of hello-world takes 0.86 ms (p50), a warm launch through the service 1.32 ms, and a launch that starts the service on demand 6.76 ms; a 4.4 MB image is about 12 ms slower warm than directly, because the client hashes it. A warm launch is not faster than a direct run there: the service is for process management (detached applications, `ps`, `stop`, shutdown, login start), not for speed. An idle supervisor holds about 6 MiB and a ready worker about 4 MiB, and neither wakes up while idle. These are the figures of one machine and the budgets of later changes, not promises.

## What the tests establish

`ctest -R service-` runs, on Linux:

| Test | Establishes |
| --- | --- |
| `service-unit-protocol` | The codec: round trips, every malformed message of the note, the limits, reasons and IDs |
| `service-unit-environment` | Each cached environment reader aborts while the environment is held, and reads the replaced value once it is bound |
| `service-cli-options`, `service-off-default` | The option errors, and that the defaults never touch a run or state directory |
| `service-worker` | A worker driven by a stand-in supervisor: its settings read from `/proc`, one use, supervisor loss, and the output of direct runs for limits, traps, modes, relative paths, a deleted directory, closed pipes, a terminal and 64 MiB of binary output |
| `service-protocol` | The supervisor through an independent client of the protocol: admission, tokens, the cache, malformed messages |
| `service-run`, `service-control`, `service-login`, `service-isolation` | The client, the `runtime` commands, login start and isolation, edge case by edge case |
| `service-parity` | `scripts/runtime-identity.py` captures, direct and through `--runtime=required`, are identical |

Each is an observed match over its cases, not a proof.
