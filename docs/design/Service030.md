# Neura 0.3.0: the resident runtime service

This note fixes the design of the 0.3.0 runtime service milestone: the per-user service `neurad`, its local protocol, one worker process per application, foreground and detached launches, a pool of ready workers that are each used once, a bounded cache of image bytes, and the `--runtime=off|auto|required` modes of `neura run`. It builds on the products, executables and service defaults that [the 0.3.0 baseline](Release030.md#run-and-service-defaults) froze, and assumes the split into sibling executables of [that page's table](Release030.md#products-and-executables): `neura` dispatches, `neura-run` runs images, and `neurad` is an optional component. Installing, updating and removing the service component belong to the lifecycle note of the same release (`docs/design/Lifecycle030.md`). Rows move to [Tooling](../lang/Tooling.md), [NVM](../runtime/NVM.md), [Install](../Install.md) and a new page `docs/runtime/Service.md` in the change that implements them.

Status: *proposed* on 2026-11-17. The owner was away and had asked for autonomous work toward 0.3.0, so the rows are implemented as written and await the owner's confirmation. Later changes to a row are made here, with the reason.

The service is a way to launch an image. It adds no language rule, no NeuraIR or NVR revision, and no operation. An image runs the same under it as under a direct run, and every program that does not ask for it never meets it.

## Principles

| Rule | Status |
| --- | --- |
| The defaults are those of the baseline and stay direct: `neura run`, `neura-run`, `neura test`, `neura debug` and the editor run in a new process of their own and neither look for, contact nor start a service, whether or not one is running or registered to start at login. No environment variable and no configuration file changes this; only `--runtime=auto` or `--runtime=required` on the command does. | frozen by the baseline |
| An application is one execution of one image. Each application runs in its own worker process, never in the supervisor and never in a process that has run or will run another application. | proposed |
| Nothing that an application made is reused: no heap, global, static, startup result, class or module guard, handle, host response, open resource, loaded native library, lifted block, region, function program or machine code. Startup runs in full for every application. | proposed |
| What may be shared is the bytes of an image, read-only, and work the worker did before it knew which application it would run and that depends on nothing the caller supplies. | proposed |
| Whatever the caller's process would have given a direct run, the launch carries explicitly. The worker takes nothing from the supervisor's environment, working directory, streams or limits. | proposed |
| A failure of the service, of the transport or of the host is reported as that, with its own exit status. It is never reported as a trap, a step limit or the program's own status. | proposed |
| No application is started twice, restarted, replayed or adopted by another supervisor. When the service cannot say whether an application ran, it says so. | proposed |
| The service is local to one user and one installation. It has no network listener, needs no elevation, and is no sandbox: a worker has the authority of the user, bounded by the capability policy of the launch exactly as a direct run is. | proposed |

## Commands, protocol and files

### Commands

`neura runtime ...` is `neura-run runtime ...`. Every form below also works as `neura-run`.

| Command | Does | Status |
| --- | --- | --- |
| `neura run --runtime=off IMAGE [-- ARGS]` | The default, written out. A direct run. | frozen by the baseline |
| `neura run --runtime=required IMAGE [-- ARGS]` | Runs through the service of this installation, starting it when it is not running. Never runs directly. | proposed |
| `neura run --runtime=auto IMAGE [-- ARGS]` | As `required`, but runs directly when the service is not installed, cannot start, has no capacity or does not support an option of the command, and only while nothing has been committed. | proposed |
| `--detach --stdout=FILE --stderr=FILE [--stdin=FILE]` | With `auto` or `required` only. The command prints the application ID and a newline on standard output and exits with status 0 once the launch is committed. The files are opened by the client for appending, created with the caller's umask. Without `--stdin` the application reads an empty input. `--detach` never falls back to a direct run. | proposed |
| `--runtime-report=FILE` | Writes one JSON object when the command ends: `schema` 1, the `path` taken (`direct`, `service-warm`, `service-fresh`, `direct-fallback`), the fallback or failure `reason`, the application ID, the terminal state, and the times of the launch phases. Accepted in every mode. | proposed |
| `neura runtime start` | Starts the supervisor of the active generation if it is not running, and waits until it answers. | proposed |
| `neura runtime status [--json]` | Says whether the service component is installed, whether a supervisor answers for each live generation, its state, epoch, ready workers, applications, cache bytes and whether login start is registered. Starts nothing. Exits 0 also when nothing is installed or running. | proposed |
| `neura runtime ps [--json] [--all]` | Lists the applications of every live generation of this installation that are not terminal; `--all` adds the terminal records still kept. Starts nothing. | proposed |
| `neura runtime inspect ID [--json]` | One application: its state, image path and digest, argument count, mode settings, limits, times and terminal outcome. Routed to the generation named in the ID; if that supervisor is gone, read from its records. Starts nothing. | proposed |
| `neura runtime stop ID [--grace=SECONDS]` | Asks for the application's process group to be terminated: `SIGTERM`, then `SIGKILL` after the grace period (10 seconds by default, the baseline's row). Waits for the terminal state and prints it. | proposed |
| `neura runtime shutdown [--generation=G] [--timeout=SECONDS] [--terminate]` | Each supervisor refuses new launches and exits when its applications have ended. After the timeout (60 seconds by default) the command reports the applications still running and exits with status 1; they keep running and the supervisor stays draining. `--terminate` stops them first, as `stop` does. | proposed |
| `neura runtime enable`, `disable` | Register or remove the start of the supervisor at login. See [login start](#starting-retiring-and-login-start). `disable` stops nothing. | proposed |
| `neura runtime cache clear` | Drops every cached image no running application uses. Stops nothing. | proposed |
| `neurad --foreground --root ROOT` | The supervisor, in the foreground, for a service manager or a terminal. `neura runtime start` and an on-demand start run the same command, detached. | proposed |
| `neurad --worker` | The worker entry. Private: it works only when started by a supervisor of the same build, and exits with status 1 and a message otherwise. | proposed |

`--json` output is one object with `"schema": 1`. A reader ignores members it does not know; a member is never removed or given another meaning without a new schema number.

### Options of a service launch

The client parses the command with the code of a direct run (`read_options` and `parse_policy_option` in today's `src/cli/main.cpp`), so an option means the same in both. The capability options are resolved in the client, in written order, into one policy, and the policy is sent; the worker binds exactly it.

| Option or mode | Through the service | Status |
| --- | --- | --- |
| Arguments after `--`, capability options, `--max-steps`, `--max-frames`, `--max-heap-cells`, `--max-stack-cells`, the task limits, `--task-quantum`, `--parallel-workers`, `--tier`, `--jit`, `--stats`, `--profile`, `--tier-stats`, `--task-dump`, `--trace-events`, `--trace-wall-time`, `--virtual-time` | Supported, with the meaning and output of a direct run | proposed |
| An image inside a native executable | Supported as today: the client extracts the image, and the image is what runs | proposed |
| `--each` (a fleet), `--record`, `--replay`, a graph device (`NEURA_GPU`, a GPU build's device options) | Not supported in 0.3.0. With `auto` the command runs directly; with `required` it fails before anything starts. | proposed |
| `neura test --runtime=...`, `neura debug` with a service mode, and a debug launch from the editor with one | Rejected as a usage error. Tests and debugging are direct in 0.3.0. | proposed |

### Files

`<id>` is the first 16 hexadecimal digits of the SHA-256 of the real path of the install root, and `<generation>` is the directory name of the runtime generation under `versions/`. `<endpoint>` is the first 16 hexadecimal digits of the SHA-256 of that path, a NUL byte, and the generation name: the socket's directory is named by it so that the socket path stays under the 104 bytes macOS allows, whatever the version is called. A source build that is not installed uses the real path of its `bin` directory as the root and `dev` as the generation.

| Path | Holds | Status |
| --- | --- | --- |
| `<run>/neura/<endpoint>/` | Mode 0700, owned by the user. `s`, the control socket (a Unix stream socket, mode 0600); `lock`, on which the supervisor holds an exclusive `flock` for its whole life; `start`, locked by a client while it starts a supervisor. | proposed |
| `<run>` | `$XDG_RUNTIME_DIR` when it is set, absolute, owned by the user and mode 0700; otherwise `${TMPDIR:-/tmp}/neura-<uid>`, created mode 0700, and refused when it exists with another owner or mode or is a symbolic link. | proposed |
| `${XDG_STATE_HOME:-$HOME/.local/state}/neura/<id>/<generation>/` | `records/<application ID>`, one small file per application, written whole and renamed into place at each change of state; `epoch-<epoch>`, the highest application number the epoch has issued; `log`, the supervisor's own log, at most 1 MiB and one rotated copy. | proposed |
| Login start | `~/.config/systemd/user/neurad-<id>.service` on Linux; `~/Library/LaunchAgents/net.neura.neurad-<id>.plist` on macOS. | proposed |

A client finds the supervisors of its installation by computing `<endpoint>` for each generation its install marker lists, and the records of generations that are gone by listing the state directory. A socket path longer than the platform allows, which needs a run directory of more than about 75 bytes, makes the service unavailable on that machine, with a message that names the path. Nothing mutable is written under the install root. No file holds an argument, an environment value or anything a program wrote. The image cache is in the supervisor's memory and has no file.

A socket file proves nothing. A client trusts a supervisor only after it has connected, the peer's user ID is its own (`SO_PEERCRED`, `getpeereid`), and the supervisor's reply names the same installation. To launch, the generation and build must also be the client's own. To control (`status`, `ps`, `inspect`, `stop`, `shutdown`, `cache clear`, and an update's `Retire`), the same user, the same installation and a common protocol major are enough, so that a client of the active generation manages the applications of an older one. A supervisor accepts a connection only from its own user. A supervisor that starts takes `lock` first; if it gets the lock, any socket file already there is stale and is removed.

### Protocol

Protocol version 1.0. It is one of the release's independent version axes: it is not the Neura version, the NVR version or the editor protocol.

A message is `length:u32` (the bytes that follow, at most 1,048,576, the baseline's limit), `kind:u16`, `flags:u16` (zero), `sequence:u64` (chosen by the sender of a request and repeated in its reply), then fields. A field is `tag:u16`, `length:u32`, bytes. Integers are little-endian. Text is UTF-8 without NUL; arguments, environment values and paths are byte strings. A receiver rejects a message with an unknown kind, an unknown field whose tag is below 0x8000, a repeated field that is not a list, a length that passes the message, or a missing required field; it ignores an unknown field at or above 0x8000. A malformed message closes the connection after one `Rejected` with reason `protocol`. File descriptors travel as `SCM_RIGHTS` with the message that names them, and a message with a different number of descriptors than it declares is malformed.

| Kind | From | Carries | Status |
| --- | --- | --- | --- |
| `Hello` | client | Lowest and highest protocol major it speaks, its build, installation `<id>` and generation, whether it connects to launch or to control, the features it needs | proposed |
| `Welcome` | supervisor | The protocol chosen, the service epoch (128 random bits made when the supervisor starts), build, generation, features, limits, and its state: `accepting`, `retiring` or `draining` | proposed |
| `Prepare` | client | A request ID (128 random bits) and the [launch envelope](#launch-envelope) | proposed |
| `Prepared` | supervisor | A token (256 random bits), the time it stays valid, whether the image bytes are needed, and the path: `warm` or `fresh` | proposed |
| `Image` | client | The token and one descriptor from which the image bytes can be read | proposed |
| `Commit` | client | The token and four descriptors: the working directory, standard input, output and error | proposed |
| `Committed` | supervisor | The application ID | proposed |
| `Outcome` | supervisor | The application ID, the terminal state, the exit status, and the times of the launch phases | proposed |
| `Rejected` | supervisor | A reason from the table below, a sentence, and `not-started`: nothing of the application ran and nothing will | proposed |
| `Cancel` | client | A token: release the reservation | proposed |
| `Signal` | client | The application ID and one of `INT`, `TERM`, `HUP`, `QUIT`, `WINCH` | proposed |
| `Query`, `Status` | client, supervisor | A request ID or an application ID; the record, or `expired`, or `unknown` | proposed |
| `List`, `Listing` | client, supervisor | The supervisor's state and its applications | proposed |
| `Stop` | client | The application ID and the grace period | proposed |
| `Shutdown`, `Retire`, `CacheClear` | client | `Shutdown` carries the terminate flag; `Retire` is sent by an update that activated another generation | proposed |

Every request carries a deadline, and a client that gets no reply by then closes the connection. The defaults: 250 ms to connect and receive `Welcome` from a running supervisor; 2 seconds for an on-demand start; 1 second each for `Prepare` and `Commit`; 5 seconds plus 1 second for each 64 MiB to send an image.

| `Rejected` reason | Meaning | `auto` | Status |
| --- | --- | --- | --- |
| `protocol`, `identity` | A malformed message; a peer that is another user or installation, or a `Prepare` from a client of another generation or build | error | proposed |
| `unsupported-protocol`, `unsupported-feature`, `unsupported-option` | No common protocol major; a feature or an option of the table above the service does not have | direct | proposed |
| `not-reproducible` | The worker cannot be given a setting of the caller's process (see [the worker](#the-worker)) | direct | proposed |
| `admission-applications`, `admission-pending` | 64 applications, or 16 prepared launches, are already held (the baseline's limits). Nothing is queued. | direct | proposed |
| `image-too-large`, `admission-cache` | The image is larger than the cache limit; or it does not fit beside the images that running applications use | direct | proposed |
| `image-mismatch` | The bytes read are not of the size and digest the envelope names | error | proposed |
| `draining`, `retired` | The supervisor is shutting down; or it is retired and the client is not of its generation | direct | proposed |
| `token-unknown`, `token-expired` | `Commit`, `Image` or `Cancel` with a token this supervisor did not issue in this epoch, or issued more than 30 seconds ago | error | proposed |

### Launch envelope

| Field | Rule | Status |
| --- | --- | --- |
| Image identity | The SHA-256 and size of the exact image bytes the runner would load, after extraction from a native executable. Never a path or a modification time. The path is carried only to be shown by `ps` and in messages. | proposed |
| Arguments | The argument vector after `--`, as byte strings | proposed |
| Environment | The caller's whole environment, names and values. The worker replaces its own environment with it before any other step of the launch. | proposed |
| Mode summary | The values in that environment of `NEURA_TIER`, `NEURA_TIER3`, `NEURA_TIER3_AFTER`, `NEURA_TIER_PROGRAMS`, `NEURA_TIER_EAGER`, `NEURA_TIER_CHECK`, `NEURA_TIER_CHECK_HEAP` and `NEURA_JIT`, and the `--tier` and `--jit` options. Copied into the record so that `inspect` shows the mode an application ran in. No other environment value is recorded. | proposed |
| Capability policy | The resolved policy, with the prefixes of file grants already resolved against the caller's directory, as a direct run resolves them | proposed |
| Limits and options | Every member of `nvm::RunLimits` that an option sets, `--max-stack-cells` included, and the runner options of the table above | proposed |
| Process settings | The umask; the soft limits `RLIMIT_NOFILE`, `RLIMIT_STACK`, `RLIMIT_CORE`, `RLIMIT_FSIZE`, `RLIMIT_AS` and `RLIMIT_CPU`; the nice value | proposed |
| Lifetime | `foreground` or `detached` | proposed |
| Client clock | The client's monotonic clock reading when its process started, so the worker's `--tier-stats` total covers the whole command as it does in a direct run | proposed |

The working directory and the three streams are descriptors sent with `Commit`, not paths, so the worker has exactly the caller's objects. An envelope larger than a message is `Rejected` with `unsupported-option`; a command whose environment and arguments do not fit in 1 MiB runs directly under `auto`.

### Prepare and commit

1. The client connects, checks `Welcome`, and sends `Prepare`. The supervisor checks the envelope, reserves one application and one pending slot, and answers `Prepared` or `Rejected`. It runs nothing of the image.
2. If the supervisor does not hold the image bytes, the client sends `Image`. The supervisor copies the bytes into storage of its own, hashing as it copies, and rejects with `image-mismatch` unless size and digest are those of the envelope. It never uses the client's object after the copy.
3. The client sends `Commit`. Before it does, any failure leaves the application not started, and under `auto` the client may run directly. `Commit` is the last message a client may abandon safely.
4. The supervisor gives the application to a worker and answers `Committed`. A second `Commit` with the same token, while the token is valid, gets the same `Committed` and starts nothing.
5. A foreground client then waits for `Outcome`, forwarding signals. A detached client prints the application ID and exits.

A reservation that is not committed within 30 seconds, or whose connection closes, is released. A token is 256 random bits a supervisor issued and remembers for its epoch, so a token it does not know is rejected whatever a record says: a commit from an earlier epoch, or one whose record was evicted, is never a new launch.

If the client sent `Commit` and the connection ended before `Committed` or `Outcome`, the outcome is unknown to it. It does not run the image directly, does not send `Prepare` again, and exits with status 6. It first tries, within 1 second, to connect again and `Query` its request ID: an answer from the same epoch gives the record and the client continues to wait or reports it; another epoch or no answer leaves the outcome unknown.

### The worker

| Rule | Status |
| --- | --- |
| The supervisor starts a worker with `posix_spawn` of `neurad --worker`; it never forks itself. The worker shares one socket pair with the supervisor. | proposed |
| Before it is given an application, a worker may only load its executable and libraries, run static initializers that read no environment and open no file of the user, and report ready. It does not read `NEURA_*`, create a tier cache, generate code, open a graph device, load a native library, or read an image. | proposed |
| On assignment the worker, in this order: replaces its environment; applies the umask, the limits and the nice value; changes to the working directory descriptor; makes itself a process group leader; installs the three streams as descriptors 0, 1 and 2 and closes every other descriptor but the supervisor's and its lease on the runtime generation, which it holds until it exits (the lifecycle note); resets signal dispositions and the signal mask; maps the image bytes read-only; then loads, validates and binds the image and runs it with the code of a direct run (`runner::run`). | proposed |
| The environment readers that keep their first answer for the life of the process (`tier_ceiling`, `tier_check_mode`, `tier_eager`, `tier_programs_enabled`, `tier3_environment`, `tier3_after` in `src/nvm/tier1.cpp`, `environment_mode` in `src/jit/jit.cpp`, and those of `tier3_passes.cpp`, `tier3_build.cpp`, `tier2.cpp` and `gpu_run.cpp`) abort a worker that calls one before its environment is replaced. A test launches twice through one supervisor with different `NEURA_TIER3` and `NEURA_JIT` and requires each run's `--tier-stats` to show its own mode. | proposed |
| A ready worker is used when the caller's `LD_*`, `DYLD_*`, `GLIBC_TUNABLES` and `MALLOC_*` variables equal those the worker was started with and its limits can be set from inside. Otherwise the supervisor starts a worker for this launch with the caller's environment and limits at `posix_spawn` (the path `fresh`). A setting neither can give, such as a soft limit above the supervisor's hard limit, is `not-reproducible`. | proposed |
| A worker runs one application and exits. It is never returned to the pool, whatever the application did. | proposed |
| The pool holds one ready worker (the baseline's row). After a launch takes it the supervisor starts another, unless it is retired, draining or at the application limit. A launch that finds no ready worker starts one and waits for it; it is not refused for that. | proposed |
| A worker ends when its supervisor does: `PR_SET_PDEATHSIG` with `SIGKILL` on Linux, a thread watching the supervisor's process on macOS, and in both the end of the socket pair. Nothing adopts it. | proposed |
| The worker tells the supervisor how the run ended (exit status, or trap class and status) before it exits. A worker that ends without saying so is `worker-lost`. | proposed |

What a worker does not have of the caller's process, and a direct run does: the caller's session, process group, controlling terminal and parent, its descriptors above 2, and its control group or service scope. Reading and writing a terminal through the passed descriptors works; the terminal's own signals reach the client, which forwards them.

### Streams, signals and the client's end

| Rule | Status |
| --- | --- |
| The worker writes to the caller's own descriptors. The service relays no byte of a stream, so binary data, end of input, partial writes, a closed pipe and terminal modes are those of a direct run, and no order between output and error is invented. | proposed |
| A foreground client forwards `SIGINT`, `SIGTERM`, `SIGHUP`, `SIGQUIT` and `SIGWINCH` to its application's process group only. `SIGQUIT` asks the image for a task dump, as in a direct run. A stopped client (`SIGTSTP`) stops only itself. | proposed |
| A foreground client that ends or loses its connection before `Outcome` cancels its launch: the supervisor terminates the application's process group as `stop` does, and the terminal state is `client-lost`. End of input on standard input is not that: it reaches the program as end of input. | proposed |
| A foreground client exits with the application's exit status. When the worker was killed by signal N it exits with 128 + N after printing `neura: runtime: the application was ended by signal N`. | proposed |
| A detached application has no client. It ends by itself, by `stop`, or by `shutdown --terminate`. It is never restarted. | proposed |
| `stop` and a cancel end the process group from outside. No trap is raised, and no `defer`, `using` or task cleanup of the program is promised to run. | proposed |

### States and records

`prepared`, `starting` and `running` are live. The terminal states:

| State | Meaning |
| --- | --- |
| `exited` | The program returned or called `Std.Program.Process.exit`; the status is its own |
| `trapped` | A Neura trap; the trap class and its status |
| `load-failed` | The image did not load, validate or bind in the worker; status 1 and the message of a direct run on the application's standard error |
| `stopped`, `client-lost` | Ended by `stop` or `shutdown --terminate`; ended because its foreground client went away |
| `signaled` | Ended by a signal the service did not send, a forwarded one included |
| `worker-lost` | The worker ended without reporting |
| `unknown` | The record was live when its supervisor ended. Only a reader of the records of a dead supervisor reports this. |
| `expired` | No record is kept, and the application number is at or below the highest its epoch issued |

An application ID is `<generation>.<first 8 hexadecimal digits of the epoch>.<number>`, numbered from 1 in each epoch. It is never a process ID and is never issued twice. A supervisor keeps 1,024 terminal records for 24 hours each (the baseline's row), removing the oldest first; live records are never removed. `inspect` and `ps` read the records of a generation whose supervisor is gone and never start one.

### Starting, retiring and login start

| Rule | Status |
| --- | --- |
| A client of `auto` or `required` that finds no supervisor takes the `start` lock, looks again, starts `neurad --foreground --root ROOT` in a new session with its streams on `/dev/null` and the log, and waits up to 2 seconds for `Welcome`. Clients that arrive meanwhile wait on the lock and find the supervisor started. | proposed |
| A supervisor started on demand exits when it has had no application and no connection for 15 minutes. One started by `neura runtime start` or at login stays. | proposed |
| A supervisor is *retired* when `current` under its root no longer names its generation, which it checks at each `Prepare`, or when it receives `Retire`. A retired supervisor keeps no ready worker, serves the clients of its own generation, and exits as soon as it has no application and no connection. | proposed |
| A supervisor that begins when its generation is not the active one is retired from the start. | proposed |
| `enable` writes the unit of the table above, whose command is `<root>/current/bin/neurad --foreground --root <root>` and which is never restarted by the service manager, and enables it for the user (`systemctl --user enable`, `launchctl bootstrap`). Where no user service manager answers, as in WSL without systemd or in a container, it changes nothing and exits with status 1 and a message; `start`, `auto` and `required` still work there. `disable` removes the unit and exits 0 when there is none. | proposed |
| An installation that an 0.2 updater produced and that has not yet been migrated refuses `start`, `enable`, `auto` and `required` with the instruction to run `neura update --migrate-install`. `--runtime=off` works. | proposed |

### Editors

| Rule | Status |
| --- | --- |
| `neura capabilities` reports whether the service component is installed and its protocol version, from the files of the installation. It connects to nothing and starts nothing, and reports a missing component as missing. | proposed |
| The editor's Run uses a service mode only when the launch configuration sets it (`"runtime": "auto"` or `"required"`), and passes it as `--runtime`. It never infers the mode from a service being installed or running. The mode used, and a fallback, are shown in the run's output. | proposed |
| The editor's Test and Debug are direct. A configuration that sets a service mode for them is rejected before anything is built or started, with a message that names the setting; it is not dropped. | proposed |
| In an untrusted workspace the editor may read `neura capabilities`. It starts no supervisor, no application and no debug session. | proposed |
| The editor's tests run with the service component absent from the installation and the run directory unwritable, then with a supervisor running: build, run, test, a breakpoint at startup, step, restart and stop behave the same in both, and the supervisor sees no connection. | proposed |

### The image cache

| Rule | Status |
| --- | --- |
| The cache maps a SHA-256 to the bytes of one image, in memory the supervisor owns and never writes again, and gives a worker a read-only descriptor to it. Its key is the digest alone, since it lives and dies with one supervisor of one build. | proposed |
| It holds at most 256 MiB (the baseline's row), entries in use included: every image a live or prepared application uses is an entry. An entry in use is never evicted; the least recently used free entry goes first. A launch whose image does not fit after every free entry is evicted is refused at `Prepare` with `admission-cache`. | proposed |
| The cache holds bytes only. A worker decodes and validates the image every time, with the loader of a direct run. No decoded structure, validation result, source map, lifted block or machine code is cached, in memory or on disk, in 0.3.0. | proposed |
| A cached image authorizes nothing. Grants, native dependencies and limits come from each launch's envelope. | proposed |

## Semantics

The service changes no stage of the compiler and no image. Everything in this note is at the output boundary of [the project specification](../Project.md): what a runtime needs in order to execute an image. What stays in the circuit and the NVM is every step, value, trap, limit and count. What the service adds across the runtime boundary is transport of the launch inputs and of the terminal outcome.

| Rule | Status |
| --- | --- |
| For the same image, arguments, input, environment, working directory, policy and options, a service launch and a direct run give the same output on each stream, exit status, trap and message, `--stats`, `--profile` and `--tier-stats` counts, and trace events. Wall times differ. | proposed |
| Every application has its own heap, frames, regions, globals, statics, class and module guards, handles, scheduler, monitors, clocks, limits and host bindings. Startup, a `static scope` when that feature lands, and class statics run once per application. Language accounting charges constants, arguments and static state on every launch even when the image bytes are shared. | proposed |
| Applications run at the same time as separate processes. The service schedules no task and changes no quantum. Separate launches are not a fleet, and `run --each` keeps its meaning and its direct path. | proposed |
| Each worker makes its own tier cache for its one image. Lifted blocks, regions, function programs and machine code live and die in that worker. A second launch of the same image lifts and compiles again. | proposed |
| The time a launch takes is measured from the client's start to the application's first step. Proving, searching and tier 3 compilation happen after the first step and belong to the run: they are read from the `--tier-stats` time line and are never counted in, or subtracted from, a launch time. A claim about warm launches is a claim about the first interval only. | proposed |
| Service limits (ready workers, applications, pending launches, cache bytes, records, message size) are the baseline's and are apart from an application's limits. A launch over a service limit is refused; an application's limits are never lowered to admit it. | proposed |
| Memory is reported in three parts that are never added to the language counters: the supervisor's resident size without the cache, the cache bytes, and each ready worker's resident size. | proposed |

## Lowering and artifacts

None. NeuraIR, NVR, the graph profile, the kernel and host ABIs and the trace schema are unchanged, and no image changes by a byte. Tiers 1 to 3, the collector and the regions of [the storage milestone](Storage030.md) are untouched: the worker calls the runner a direct run calls. The new versioned interfaces are the service protocol (1.0) and the JSON schema of `status`, `ps`, `inspect` and `--runtime-report` (1). The channel between a supervisor and its workers is private to one build and has no version: a worker refuses a supervisor of another build.

`neurad`, and the client code in `neura-run`, link the execution core only. They contain no compiler, parser, lowering or decompiler code, and depend on no library beyond those the baseline lists.

## Diagnostics

The service reports no source diagnostic, so it takes none of the codes `E0711` to `E0719` held for these milestones (`E0710` is already *invalid host or scheduler binding*). Its errors are lines on standard error that begin `neura: runtime: `, written by the client, and a tool exit status. The program's own output is never prefixed or interleaved by the supervisor.

| Status | Meaning | Status |
| --- | --- | --- |
| the application's | A committed foreground launch that reached a terminal state | proposed |
| 1 | A usage error; `shutdown` that timed out; `enable` without a service manager | proposed |
| 5 | The service did not run the application and nothing was started: not installed, could not start, refused, or an option it does not support under `required` | proposed |
| 6 | The outcome is unknown: a commit was sent and no outcome arrived | proposed |

| Condition | Message after `neura: runtime: ` | Status |
| --- | --- | --- |
| `--detach` with `off` or no `--runtime` | `--detach needs --runtime=auto or --runtime=required` (status 1) | proposed |
| `--detach` without both output files | `--detach needs --stdout and --stderr` (status 1) | proposed |
| `required`, no `neurad` in the installation | `the service component is not installed; install Neura-runtime, or run without --runtime` (5) | proposed |
| The supervisor did not answer in time | `the service did not start within 2 seconds; see <log>` (5) | proposed |
| `Rejected`, under `required` | `the service refused the launch: <sentence> (<reason>)` (5) | proposed |
| `identity`, `protocol`, `image-mismatch`, in either mode | The sentence, and `nothing was run` (5) | proposed |
| An unsupported option under `required` | `--each is not supported by the service; run without --runtime` (5) | proposed |
| Fallback under `auto` | `running directly: <reason>`, one line before the program starts; the run's status is the program's | proposed |
| Commit sent, no outcome | `the outcome of application <ID or request ID> is unknown; it was not run again. Try "neura runtime inspect <ID>"` (6) | proposed |
| `inspect` or `stop` of an ID of another installation or a malformed ID | `<ID> is not an application of this installation` (1) | proposed |
| Unmigrated installation | `finish the 0.3.0 install first: run "neura update --migrate-install"` (5) | proposed |

## Edge cases

Each case is a test of the order of work. *Accept* means the launch runs; *reject* means the stated status and nothing started.

1. Plain `neura run` while a supervisor runs and login start is enabled: direct; the supervisor's `Listing` shows no application and no connection was made (accept).
2. Plain `neura run`, `neura test` and a debug launch with `neurad` removed from the installation and the run directory unwritable: unchanged (accept).
3. `--runtime=auto` with no `neurad` installed: direct, with the one `running directly` line (accept).
4. `--runtime=required` with no `neurad`: status 5 (reject).
5. `--runtime=bogus`, `--runtime` with no value: usage error, status 1 (reject).
6. `--detach` alone; `--detach --runtime=off`; `--detach --runtime=auto` without `--stderr`: status 1 (reject).
7. `--detach --runtime=auto` when the service cannot start: status 5, not a direct run (reject).
8. Two clients start the first launch at once with no supervisor: one supervisor, both run (accept).
9. A socket file and no supervisor (the supervisor was killed): the next client starts a supervisor, which removes the file (accept).
10. A socket owned by another user in a run directory with a wrong mode: `identity`, status 5 in both modes (reject).
11. A supervisor of another build of the same generation name answers (a rebuilt source tree): a launch is `identity`, and under `auto` this is an error, not a fallback; `ps` and `stop` still work (reject).
12. A process ID reused by an unrelated process: no decision rests on a process ID (accept).
13. Sixty-four applications running, a sixty-fifth under `required`: `admission-applications`, status 5; under `auto`: direct (reject, accept).
14. Seventeen simultaneous `Prepare` without `Commit`: the seventeenth is `admission-pending`; after 30 seconds the sixteen are released (reject).
15. `Commit` 31 seconds after `Prepared`: `token-expired`, status 5, nothing ran (reject).
16. `Commit` sent twice with one token: one application, the same `Committed` (accept).
17. `Commit` with a token from before the supervisor restarted: `token-unknown`; the client reports status 6 if it had sent that commit before, 5 if not (reject).
18. The connection drops after `Commit` and the application ran to its end: the client reconnects, `Query` returns the record, and it exits with the application's status (accept).
19. The same, and the supervisor is gone: status 6, and the image is not run again (reject).
20. The image file is rewritten between the client's read and the supervisor's copy: `image-mismatch`, status 5 in both modes (reject).
21. The image file is replaced after `Commit`: the application runs the bytes it was committed with (accept).
22. The same path launched twice with different contents: two digests, two entries; neither run sees the other's bytes (accept).
23. An image that fails validation: `load-failed`, status 1, the direct run's message (accept as a launch; the run fails as directly).
24. A cached entry evicted while its application runs: cannot happen; the entry is in use. `cache clear` during the run leaves it (accept).
25. A 300 MiB image: `image-too-large`. Three different 100 MiB images, the third while the first two run: `admission-cache`; it launches once one has ended. Both run directly under `auto` (reject).
26. Two launches of one image with `NEURA_TIER3=off` and `NEURA_TIER3=native`: each `--tier-stats` shows its mode; neither sees the other's programs (accept).
27. `NEURA_JIT=off` in the supervisor's own environment and unset in the caller's: the application runs with the caller's (accept).
28. `--max-stack-cells 1000` on an image that uses regions: `stack limit exceeded` with the direct run's message and status (accept).
29. The caller sets `LD_LIBRARY_PATH`: the path is `fresh`, and a native library is found as in a direct run (accept).
30. The caller's `RLIMIT_NOFILE` soft limit is above the supervisor's hard limit: `not-reproducible`; direct under `auto` (reject).
31. An environment of 2 MiB: direct under `auto`, status 5 under `required` (reject).
32. Relative paths in arguments and `--allow-fs=.`: resolved against the caller's directory, though the supervisor started elsewhere (accept).
33. The caller's directory is deleted before `Commit`: the worker's directory is the deleted directory, as a direct run's would be (accept).
34. Standard output is a closed pipe: the program gets the same write error or signal as directly (accept).
35. Standard input is a terminal: reads work; Ctrl-C ends only this application; Ctrl-D is end of input (accept).
36. Binary output of 64 MiB with NUL bytes to a pipe and to a file: identical bytes (accept).
37. The client is killed with `SIGKILL` during a foreground run: `client-lost`; the process group is gone within the grace period (accept).
38. The client is killed during a detached launch after `Committed`: the application continues (accept).
39. The worker is killed with `SIGKILL`: `worker-lost`; the foreground client exits 137 with the `ended by signal` line; no restart (accept).
40. The supervisor is killed with `SIGKILL` while three applications run: all three workers end; their foreground clients exit 6; `ps --all` shows them `unknown`; nothing is replayed (accept).
41. A program that starts a child process and exits: `stop` and cancel end the whole process group; a child that left the group is not chased, and `inspect` says the group was signaled (accept).
42. A trap in startup code: `trapped` with the direct run's message; the next launch of the same image runs startup again (accept).
43. Eight launches at once with one ready worker: one is `warm`, seven wait for workers started for them; none is refused (accept).
44. A worker that never reports ready within 2 seconds: it is killed, the launch is `Rejected` before commit, and the pool tries once more on the next launch (reject).
45. `neurad --worker` run by hand: status 1 and a message (reject).
46. `stop` of an application that already ended: prints its terminal state, status 0 (accept).
47. `stop` of an expired ID: `expired`, status 1 (reject).
48. `shutdown` with a detached application that never ends: status 1 after the timeout, the application runs on, later launches get `draining` (reject).
49. `shutdown`, then `--runtime=auto`: the draining supervisor refuses; the client runs directly. After the supervisor exits, the next `auto` starts a new one (accept).
50. An update activates another generation while an application runs: the old supervisor retires and drops its ready worker; the application finishes; `ps` from the new generation lists it; `stop` reaches it (accept).
51. `inspect` of an application of a generation whose supervisor is gone and whose directory was pruned: its record if kept, else `expired`; no supervisor is started (accept).
52. `status`, `ps`, `inspect` and `doctor` with nothing running: no process, socket or file appears (accept).
53. `enable` in WSL without systemd: status 1, the message, no file written; `--runtime=required` still works (reject).
54. `enable` twice; `disable` twice: both succeed with one unit and none (accept).
55. Two installations on one machine: two `<id>`, two sockets, two units; `ps` of one never lists the other's (accept).
56. An install root reached through a symbolic link and with spaces and non-ASCII characters: one `<id>`, from the real path (accept).
57. A message of 2 MiB, a field length past the end, an unknown kind, a `Commit` with three descriptors: one `Rejected` with `protocol`, then the connection closes; the supervisor keeps serving others (reject).
58. `--record`, `--replay`, `--each` with `required`: status 5; with `auto`: direct, with the line (reject, accept).
59. `neura test --runtime=required`; a debug launch that asks for a service mode; an editor Debug configuration with `"runtime": "required"`: rejected before anything starts (reject).
60. A launch on an installation not yet migrated from a 0.2 update: status 5 with the instruction; `--runtime=off` runs (reject).
61. `neura capabilities` with a stale socket, with no `neurad`, and with a supervisor running: the same answer about the component each time, no connection, no file written (accept).
62. An editor Run with `"runtime": "auto"` in an untrusted workspace: nothing is started (reject).
63. A release candidate's generation name under a macOS `TMPDIR` of 49 bytes: the socket path is under 104 bytes and the service starts (accept).

## Order of work

Each step is one commit on the milestone branch. No step changes an image.

| Step | Content | Targeted tests |
| --- | --- | --- |
| 0 | Merge the split into sibling executables. The steps below name today's files; after the merge the same code is in `neura-run` and the execution core. | The sibling parity tests of that milestone |
| 1 | This note | Documentation checks |
| 2 | `src/service/protocol.{hpp,cpp}`: messages, fields, limits, reasons, IDs; no I/O | `service-unit-protocol`: round trips, every malformed case of edge case 57, limits |
| 3 | `--runtime`, `--detach`, the stream options and `--runtime-report` in `read_options`; `off` only, and every other value rejected until step 7; the usage errors | `service-cli-options` (cases 5, 6), `runtime-off-default` (cases 1, 2 with no `neurad` built) |
| 4 | The assertion on environment readers; `runner::run` takes its streams, clock start and a mapped image | The existing tier and runner tests, unchanged output; `scripts/runtime-identity.py` identical |
| 5 | `neurad --worker` and a test driver that plays the supervisor: assignment order, process settings, one use, reporting, supervisor loss | `service-worker`: cases 26 to 36, 42, 45 |
| 6 | The supervisor: `lock`, socket, `Hello`, prepare and commit, the pool, records, admission, the cache | `service-protocol`: cases 8 to 25, 43, 44, 57 |
| 7 | The client in `run`: `required` and `auto`, on-demand start, signals, fallback, statuses 5 and 6 | `service-run`: cases 3, 4, 7, 37 to 41, 58; `service-parity`: the direct identity cases through `required` |
| 8 | `neura runtime start`, `status`, `ps`, `inspect`, `stop`, `shutdown`, `cache clear`; retiring; the idle exit | `service-control`: cases 46 to 52, 55, 56, 63 |
| 9 | `enable` and `disable`, with a stand-in `systemctl` and `launchctl` on the test `PATH`; the service line of `neura capabilities`; the `runtime` launch setting in the editor extension, which is versioned apart | `service-login`: cases 53, 54, 61; the extension's tests: cases 59, 62 |
| 10 | `scripts/bench-launch.py`, the measurements, the budgets, and the pages: `docs/runtime/Service.md`, Tooling, NVM, Install, the changelog | The documentation checks; the gate below |

## Gate and acceptance

- **Direct stays direct.** With `neurad` absent, and again with a supervisor running and login start enabled, `neura run`, `neura test` and a debug session behave as before the milestone, and no process, socket, record or unit appears. The baseline's rows for `neura --version` and a direct hello-world run still hold.
- **Images unchanged.** Every image the compiler builds is byte-identical before and after the milestone, and `scripts/runtime-identity.py` gives identical captures in every execution mode of the baseline.
- **Parity.** The identity cases, run through `--runtime=required`, give the captures of the direct run in each execution mode of the baseline: output, status, trap, steps, peak heap cells, peak stack cells and profile. This is an observed match over those cases, not a proof.
- **Isolation.** Simultaneous launches of programs that write globals, statics and files show independent state; a worker's process ID is never seen twice across applications; a launch after a trapped one starts clean.
- **Protocol.** Every edge case above passes, on Linux and macOS, x86-64 and arm64.
- **Launch measurements.** `scripts/bench-launch.py` records p50 and p95 of client start to request accepted, worker assigned, image bound, first step, entry function and exit, for: the baseline's monolithic figures, a direct run, the first launch with the service stopped, a running service with an image it does not hold, a ready worker with a cached image, and a burst of eight. The workloads are hello-world, the largest image of the benchmark set, a program with long startup, one with tasks, one with a native dependency, and one reading a terminal, each in the default mode, with `NEURA_TIER3=off` and with `--jit=off`. It also records the supervisor's and a ready worker's resident size, the cache bytes, the wakeups of an idle supervisor in 60 seconds, and the sizes of `neurad` and the runtime archive.
- **The claim follows the numbers.** The release notes state the measured difference between a warm launch and a direct run for those workloads and that machine, and nothing more. If a warm launch is not faster, the notes say so and present the service as process management. Neither result changes the direct default.
- **Budgets.** Step 10 records the idle supervisor's resident size, a ready worker's, and the warm and stopped-service launch times, and a later change may not exceed them by more than 10% without saying why.

## Migration

Nothing in a source file, the standard library or an image changes. For the migration guide:

- A script that runs `neura run` keeps working and keeps running directly.
- A deployment that wants a resident service installs Neura-runtime, or adds the service component to an SDK, and opts in per launch. The guide documents on-demand start, `neura runtime enable`, the idle exit, `ps`, `stop`, `shutdown`, and what a service launch does not inherit from the caller (descriptors above 2, the session, the control group).
- [Tooling](../lang/Tooling.md) gains the `--runtime` options, the `runtime` commands and statuses 5 and 6. [NVM](../runtime/NVM.md) says that the cached environment readers are read after a launch is bound. [Install](../Install.md) lists the files of the tables above.

## Decisions taken without the owner

1. The cache holds image bytes only, in the supervisor's memory; workers validate every time. A shared validated form would add a trust boundary between processes for a cost that has not been measured to matter.
2. Streams are passed as descriptors and never relayed, which keeps every stream property of a direct run and needs no ordering rule.
3. Tests, debugging, fleets, record, replay and graph devices are direct only in 0.3.0. Each needs a worker transport of its own, and refusing is the stricter choice.
4. The whole environment is carried and installed before any runtime code reads it, and the readers abort a worker that reads early, rather than each `NEURA_*` variable becoming an envelope field. One rule covers variables added later.
5. A worker that cannot be given the caller's loader variables or limits is replaced by a fresh one or the launch is refused; the service never runs an application under settings that differ from a direct run's.
6. Tool statuses 5 (not run) and 6 (outcome unknown), and `--runtime-report`, distinguish service failures from the program's status.
7. Terminal records are files, so that an application of a supervisor that is gone can still be inspected without starting anything.
8. A supervisor started on demand exits after 15 idle minutes, so one opted-in launch does not leave a process for the rest of the session.
9. A file changed while it is being launched is an error in both modes, not a reason to fall back.
10. No speed-up is promised. Budgets for memory and launch time are the first measured values plus 10%.
11. `neura runtime cache clear` exists, as the one way to release cache memory without a shutdown.
12. No diagnostic code is used.
