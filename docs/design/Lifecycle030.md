# Neura 0.3.0: products, installation and updates

This note fixes the design of the 0.3.0 lifecycle milestone: the archives of the two products, the component manifests inside them, the install marker and release metadata that know about components, installing, updating, rolling back and uninstalling by component, a public link for every tool, runtime generations that stay on disk while a process uses them, and the bridge that brings a 0.2 installation to 0.3.0. It builds on the products and executables that [the 0.3.0 baseline](Release030.md#products-and-executables) froze and on today's [install layout](../Install.md#layout), and assumes that the tools are already sibling executables in one `bin/` directory with `neura` as their dispatcher. The service itself, `neurad`, is the subject of the service note of the same release (`docs/design/Service030.md`); this note covers only how its component is installed, kept and removed. Rows move to [Install](../Install.md), [Release process](../ReleaseProcess.md) and [Tooling](../lang/Tooling.md) in the change that implements them.

Status: *proposed* on 2026-11-17, and *implemented* as written in the changes that followed this note, except where a row or [the decisions](#decisions-taken-without-the-owner) say otherwise. The owner was away and had asked for autonomous work toward 0.3.0, so the rows await the owner's confirmation; "proposed" stays until then. Later changes to a row are made here, with the reason. The service itself is the service milestone's: where a row needs `neurad`, this milestone implements the lifecycle's side with `neurad` absent or a stand-in, and the row says so.

Nothing here touches the language, NeuraIR, NVR or an image. The milestone changes what is packed, where it is put, and when it is removed.

## Principles

| Rule | Status |
| --- | --- |
| An SDK installation builds, runs, tests and debugs with no service installed, registered or running. Installing or updating it never adds the service. | frozen by the baseline |
| A runtime installation runs images with no compiler, no standard library sources and no SDK. It never downloads the SDK to satisfy a command; it names the product that has the command. | proposed; implemented |
| An installation is a set of *components*. `core` is the execution core and the lifecycle tools, `sdk` the development tools, standard library and native launchers, `service` is `neurad`. Neura-sdk is `core` and `sdk`. Neura-runtime is `core` and `service`. Both together are one installation with one copy of `core`. | proposed; implemented |
| A *generation* is one complete tree of one build for one set of components, under `versions/`. It is never written after it is placed. Every change of version or of components makes a new generation and switches to it. | proposed; implemented |
| Every public tool an installation has is callable from the one directory that holds the `neura` link. | proposed; implemented |
| A change either completes or leaves the installation as it was. Where several file system entries must change, a journal makes the change recoverable; the note does not claim that they change at one instant. | proposed; implemented |
| A generation that a running process uses is not removed, by an update, a rollback, a component change or `--force`. An update stops, restarts and kills nothing. | proposed; implemented |
| A 0.2 client is offered only files it can read, and reaches 0.3.0 in two documented steps. | proposed; implemented |
| Only files and links the installation owns are changed or removed. A package manager's installation is left to the package manager, as today. | proposed; implemented |

## Commands, protocol and files

### Archives

Each release publishes, for each target:

| File | Top directory | Holds | For | Status |
| --- | --- | --- | --- | --- |
| `neura-sdk-<version>-<triple>.tar.gz` | `neura-sdk-<version>-<triple>/` | `core` and `sdk` | New clients and the bootstrap script | proposed; implemented |
| `neura-runtime-<version>-<triple>.tar.gz` | `neura-runtime-<version>-<triple>/` | `core` and `service` | New clients and the bootstrap script | proposed; implemented |
| `neura-<version>-<triple>.tar.gz` | `neura-<version>-<triple>/` | Exactly the tree of the SDK archive | 0.2 clients, whose updater requires this name, this top directory, `bin/neura`, `share/neura/stdlib/std` and a version line that starts `neura <version> ` | proposed; implemented |

Beside the component files, each top directory holds `install.sh` and `VERSION`, as today. An archive holds regular files and directories only, is reproducible as [`make-archive.sh`](../../packaging/make-archive.sh) makes it today, and its `core` files are byte-identical in the three archives of one target. A path in an archive uses only letters, digits, `.`, `_`, `+`, `-` and `/`.

### Component manifests

Each component has one manifest, `share/neura/components/<name>.txt`, written by the archive script and parsed strictly, like the other install files: the lines below in this order, single spaces, a final newline, nothing else.

```text
neura-component 1
name sdk
version 0.3.0
build 3ffc2120c0ffee00000000000000000000000000
target x86_64-linux-gnu
requires core
tool neurac
tool neura-test
file 755 <sha256> <size> bin/neurac
file 644 <sha256> <size> share/neura/stdlib/std/core/version.nra
```

| Rule | Status |
| --- | --- |
| `build` is the commit the release was built from. All components of a generation have the same `version`, `build` and `target`; a tree that mixes them is refused. | proposed; implemented |
| `requires` appears once for each component this one needs: `sdk` and `service` require `core`. `core` has none. | proposed; implemented |
| `tool <name>` declares the public executable `bin/<name>`, which gets a public link. Tool lines are sorted. | proposed; implemented |
| `file <mode> <sha256> <size> <path>` lists every file of the component except the manifest itself, sorted by path. The mode is 755 or 644. A file belongs to one component. | proposed; implemented |
| `core`: `neura`, `neura-run`, `neura-inspect`, `neura-doctor`, `neura-update`, `neura-uninstall`, and the license. `sdk`: every other tool of the baseline's table except `neurad`, the standard library, and the native launchers. `service`: `neurad`, with an extra line `protocol service 1.0` after `requires`. | proposed; implemented |
| The dispatcher reads the `tool` lines of the manifests in its own generation to know which commands are installed. A command whose tool is absent names the component that has it. | proposed; implemented when a tool is missing: the dispatcher reads the manifests only then, to name the product that has it (decision 13) |
| A component name the reader does not know is an error for an install or an update, and is reported and otherwise ignored by `doctor`. Application target components, which later milestones define, use the same format. | proposed; implemented |

### The install root

```text
<root>/neura-install.txt                 the marker, schema 2
<root>/versions/<generation>/            bin/, share/, lib/ of one generation
<root>/current                           symbolic link: versions/<generation>
<root>/leases/<generation>               an empty file that users of the generation lock
<root>/.lock                             the installation lock
<root>/.journal                          present only during a change
<bin dir>/<tool>                         symbolic link: <root>/current/bin/<tool>, one for each tool
```

| Rule | Status |
| --- | --- |
| A generation's name is `<version>+<id>`, where `<id>` is the first 12 hexadecimal digits of the SHA-256 of its manifests' bytes, concatenated in the order `core`, `sdk`, `service`. The same build with the same components has the same name everywhere; a different build of the same version, or a different component set, has another. | proposed; implemented |
| A generation that a 0.2 updater placed is named `<version>` alone and keeps that name. | proposed; implemented |
| Every public link points through `current`, so one rename of `current` moves every tool to the new generation at once. Links are added or removed only when the set of tools changes. | proposed; implemented |
| A tool finds its root from the real path of its own executable, `<root>/versions/<generation>/bin/<tool>`, never from the working directory, `PATH`, or an environment variable. `detect_install` accepts both forms of generation name and either marker schema. | proposed; implemented |
| The service's socket, records and log, and any cache, are outside the root, in the user's runtime and state directories. | proposed; implemented |

The marker, schema 2:

```text
neura-install 2
method archive
bin /home/user/.local/bin
components core sdk
generation 0.3.0+1a2b3c4d5e6f
generation 0.2.1-rc.3
link neura
link neura-doctor
link neurac
```

`components` is the set an update preserves. There is one `generation` line for each directory in `versions/`, and one `link` line, sorted, for each link in `bin` the installation owns. The active generation is what `current` names; the marker does not repeat it. A schema 1 marker is still read: it describes an installation with the one link `neura`.

### Release metadata

| File | Content | Read by | Status |
| --- | --- | --- | --- |
| `neura-release.txt` | Schema 1, unchanged: `neura-release 1`, the version, and one `asset` line for each target, naming the legacy archive. No other line, since a 0.2 client rejects a line it does not know and a second asset for a target. | 0.2 clients, and a bootstrap script saved from a 0.2 release | proposed; implemented |
| `neura-components.txt` | `neura-components 1`, `version <version>`, `build <commit>`, then `archive <product> <triple> <file> <sha256> <size>` lines, sorted, where product is `sdk` or `runtime`. Parsed strictly. | 0.3 clients and the 0.3 bootstrap script | proposed; implemented |
| `SHA256SUMS` | Every archive | People and the release workflow | proposed; implemented |

A 0.3 client asks a release source for `neura-components.txt`. If the source has none, it reads `neura-release.txt`: when that names a version that is not newer, the installation is up to date; when it names a newer one, the update fails with `release <version> has no component metadata`.

### Commands

| Command | Does | Status |
| --- | --- | --- |
| `install.sh [--force]` in an unpacked archive | Checks `VERSION` and that `bin/neura-update` runs, then runs `bin/neura-update --install-from <dir>` with its options. `NEURA_INSTALL_ROOT` and `NEURA_BIN_DIR` keep their meaning. All checking, locking and journaling is in the one implementation, in `src/cli/lifecycle.cpp`. | proposed; implemented |
| `install.sh --add <dir>` | Also takes the components of a second unpacked archive of the same build, so that the SDK and runtime archives install as one generation. | proposed; implemented |
| The bootstrap script, `--components sdk`, `runtime` or `sdk,service` | Downloads and verifies the one or two archives the set needs and runs the installer. The default is `sdk`. `--version`, `--source` and `--force` are as today. | proposed; implemented |
| `neura update` | Updates to the newest release, keeping the installed set of components. `--check`, `--nightly`, `--source` and `--require-attestation` are as today, and apply to every archive fetched. | proposed; implemented |
| `neura update --components <set>` | Changes the set: `sdk`, `runtime` (`core` and `service`) or `sdk,service`. Removing a component fetches nothing. Adding one fetches the archive of the installed version from that version's own release (`releases/download/v<version>`, or `--source`), not from the latest release, and requires its `build` to be the installed one. | proposed; implemented |
| `neura update --migrate-install` | Finishes an installation that a 0.2 updater produced. See [the 0.2 bridge](#the-02-bridge). | proposed; implemented |
| `neura update --rollback` | Makes the previous generation active again, when it is still on disk and is a 0.3 generation. Fetches nothing. | proposed; implemented, for a previous generation that has manifests, whatever its name (decision 16) |
| `neura update --repair` | Completes or undoes an interrupted change, restores owned links, and removes generations no one uses beyond the previous one. Fetches nothing. | proposed; implemented |
| `neura uninstall [--dry-run] [--yes]` | Removes the installation, as today, with the new files of the root, the links of the marker, and the service's registration and state. | proposed; implemented |
| `neura uninstall --component sdk` or `service` | Removes one component, by a new generation without it. | proposed; implemented |
| `--terminate`, with either uninstall or with `update --components` that removes `service` | First stops the applications of this installation's service, as `neura runtime shutdown --terminate` does. Without it, a removal that affects running applications is refused. | proposed; implemented through the `ServiceHooks` seam, whose default runs the active generation's `neura runtime shutdown --terminate` (decision 17) |
| `neura doctor` | See [doctor](#doctor). Offline and read-only, as today. | proposed; implemented |

`neura update` and `neura uninstall` are `neura-update` and `neura-uninstall`; both forms behave the same. An installation whose marker names a package manager, and an unmanaged build, exit with status 3 for every command above except `doctor`, as today.

### A change, step by step

Every command that changes the root runs these steps. The fault hook `NEURA_TEST_UPDATE_FAULT=<step>` fails at the named step as today, and `NEURA_TEST_UPDATE_CRASH=<step>` ends the process there without cleanup; both exist for the tests only.

| Step | Name | What happens | Status |
| --- | --- | --- | --- |
| 1 | `lock` | An exclusive `flock` on `<root>/.lock`, not waited for: a second command fails at once with `another install, update or uninstall is running`. On a root with a schema 1 marker, where a 0.2 updater may run, the command also creates the 0.2 lock file `.update-lock` by the 0.2 rules and keeps it until it ends: a file that is already there means a 0.2 update is running, or is reported as stale after an hour for the user to delete, and the command changes nothing. On a schema 2 root no 0.2 client can run, so a `.update-lock` found there is left over from an interrupted migration and is removed. | proposed; implemented |
| 2 | `recover` | A journal that is present is from a command that ended early. If `current` already names the journal's new generation the change is completed from step 9; otherwise it is undone: removed links are restored, and the new generation's directory is deleted only when the journal says this change placed it. A generation that was on disk before, as in a rollback or `--force` on a whole generation, is never deleted by recovery. | proposed; implemented |
| 3 | `download`, `checksum` | As today, for each archive: size, SHA-256, and build provenance when `gh` can check it | proposed; implemented |
| 4 | `extract` | As today: the member names and kinds are checked before anything is unpacked, into a staging directory in the root | proposed; implemented |
| 5 | `verify` | Each manifest parses; every listed file is there with its size, digest and mode; nothing else is there but the manifests, `install.sh` and `VERSION`; all manifests agree on version, build and target; `core` is byte-identical where two archives bring it; the staged `bin/neura --version` prints a line that starts `neura <version> ` | proposed; implemented |
| 6 | `compose` | The components selected are copied into one tree, the generation's name is computed, and every public link is checked: a path that is absent, or a symbolic link into this root, is ours to set; anything else is a conflict. Any conflict ends the command here with every conflicting path listed and nothing changed. | proposed; implemented |
| 7 | `journal`, `place` | The journal is written and synced: the operation, the old and new generation, whether this change places the new generation or finds it on disk, the links to remove and to add, and the old marker. A new tree is renamed to `versions/<generation>` and `leases/<generation>` is created. | proposed; implemented |
| 8 | `links-remove` | Links of tools the new set does not have are removed | proposed; implemented |
| 9 | `activate-link` | `current` is replaced by one rename | proposed; implemented |
| 10 | `links-add` | Links of tools the old set did not have are created | proposed; implemented |
| 11 | `activate-marker` | The marker is replaced by one rename and the journal is removed | proposed; implemented |
| 12 | `retire`, `prune` | A running supervisor of the old generation is told it is retired, with a wait of at most 250 ms. Generations other than the active and the previous one are removed when no process holds their lease. A failure here is a warning; the update has succeeded. | proposed; implemented: prune as written; retire through the `ServiceHooks` seam, whose control client the service milestone supplies (decision 17) |

A failure at steps 3 to 7 removes the staging directory and leaves the installation untouched. A failure at steps 8 to 11 is undone in reverse order, by the rule of step 2, and the command reports `rolled back`. The installation works in every intermediate state: before step 9 every link resolves in the old generation, and after it every link that exists resolves in the new one.

### Generations in use

| Rule | Status |
| --- | --- |
| Every tool of an installed generation takes a shared `flock` on `leases/<generation>` when it starts, before it opens another file of the generation, and holds it until it exits. A supervisor and its workers hold it too, so an application that is running holds its generation. The descriptor is close-on-exec: a program a tool starts does not hold a lease. | proposed; implemented |
| A generation is removed only by a command that holds the installation lock and has taken an exclusive `flock` on its lease without waiting. The lock is released by the kernel when its holder ends, so a process that died holds nothing and no process ID is trusted. | proposed; implemented |
| On a root with a schema 2 marker, a tool that finds its lease file gone ends with `this generation of neura was removed; run the command again`, status 1. On a root with a schema 1 marker, which a 0.2 client made and which has no `leases/`, a tool runs without a lease. | proposed; implemented |
| Where the file system cannot lock, tools run without a lease and no generation is removed automatically; `doctor` says so, and `uninstall` still works. | proposed; implemented |
| The previous generation is kept after an update, in use or not, so that `--rollback` needs no download. The next update removes it. | proposed; implemented |
| `install.sh --force` on a generation that is already there verifies it against its manifests. If it is whole, nothing is copied and the installation is switched to it and its links repaired. If it is damaged and unleased it is set aside and replaced; if it is damaged and leased the command is refused. | proposed; implemented |
| A rollback, or an update to a generation still on disk, verifies that generation first and then runs steps 6 to 12. The generation rolled away from becomes the previous one. | proposed; implemented |
| An update while applications run leaves them, their supervisor and their generation alone. New service launches go to a supervisor of the new generation. The old supervisor retires and exits when it is empty, and its generation is then removable. | proposed; implemented for the lifecycle's side (leases, retire hook); new launches and the old supervisor's exit are the service milestone's |

### The 0.2 bridge

A 0.2 client (0.2.0, and the 0.2.1 candidates through rc.3) reads `neura-release.txt`, downloads the legacy archive, places it at `versions/<version>`, switches `current`, writes a schema 1 marker, and deletes the old version at once. It does not run the new installer and makes no link but `neura`. The installation is then *unmigrated*: schema 1 marker, and a `core` manifest in the running generation.

| Rule | Status |
| --- | --- |
| Unmigrated, `neura --version`, `help`, `capabilities` and every command through the dispatcher work, and run directly. The other tools are not on `PATH`. | proposed; implemented |
| Unmigrated, `neura doctor` reports `FAIL  migration: run "neura update --migrate-install" to finish the install`; `neura update`, `--components` and `--rollback` refuse with the same instruction; service start and service launches are refused; `neura uninstall` works by the schema 1 rules. Where an update notice would be printed, the instruction is printed instead. | proposed; implemented, except service start and launches, which the service milestone refuses (its case 60); `update --check` and `--repair` refuse too (decision 18) |
| `neura update --migrate-install` takes the lock; verifies the running generation against its manifests (allowing `install.sh` and `VERSION`); takes the bin directory from the directory of the marker's `link` line, whose last component must be `neura`; checks every tool's link as step 6 does; writes the journal; creates the lease files and the links; writes the schema 2 marker with `components core sdk`. It moves no file of the generation, fetches nothing, and starts and registers nothing. | proposed; implemented |
| Run again, or on an installation made by the 0.3 installer, it verifies the links, restores any that are missing, and exits 0 with `already migrated`. Interrupted, it is completed by the next run. | proposed; implemented |
| The 0.3 installer run on a root with a schema 1 marker (a 0.2 installation, or an unmigrated one) migrates the marker as part of its own change. The 0.2 generation becomes the previous one. `--rollback` refuses to make a generation without manifests active. | proposed; implemented |
| A 0.2 `install.sh` run on a schema 2 root stops at the marker's first line and changes nothing; a 0.2 `neura` reading a schema 2 marker reports it as not valid. Going back to 0.2 is an uninstall and an install. | proposed; implemented |
| The first candidate's upgrade check starts from `v0.2.1-rc.3`, the newest published release. Every candidate and the final release run the bridge from `v0.2.1-rc.3` and from `v0.2.0`, and later candidates also update from the candidate before them with the 0.3 client. | proposed; implemented |

### Doctor

`doctor` keeps its `ok`, `warn` and `FAIL` lines and exits with status 1 when any line is `FAIL`. It starts no service, runs no project code and uses no network.

| Check | Result | Status |
| --- | --- | --- |
| `components` | `ok` with the set and the build | proposed; implemented |
| `integrity` | Every manifest file exists with its size; every executable has its digest. `FAIL` names the first three that do not. | proposed; implemented |
| `sdk` | With `sdk`: the standard library and launcher checks of today. Without: `ok  sdk: not installed; source commands need Neura-sdk`. | proposed; implemented |
| `service` | With `service`: `ok` with the protocol version, whether a supervisor answers, and whether login start is registered. Without: `ok  service: not installed (optional)`. | proposed; implemented, with the supervisor found by the lock it holds rather than by an answer, since doctor links no control client |
| `links` | `ok` when every tool has its link; `warn` for each link that is missing or is not ours, as `bin-link` does today | proposed; implemented |
| `path` | As today | proposed; implemented |
| `migration`, `transaction` | `FAIL` for an unmigrated installation, and for a journal left by an interrupted change, each with the command to run | proposed; implemented |
| `generations` | `ok` with the active and previous generation; `warn` for others on disk, saying whether each is in use | proposed; implemented |
| libclang, the C compiler, the GPU backend | With `sdk` only | proposed; implemented |

## Semantics

| Rule | Status |
| --- | --- |
| A command through the dispatcher and the same command through its own executable resolve the same generation: the one the executable is in. A command that spans tools, such as `neura test`, uses the siblings of its own generation throughout, although `current` may move meanwhile. | proposed; implemented |
| Two installations on one `PATH` do not mix: each tool uses its own root. Which `neura` the shell finds is the shell's affair. | proposed; implemented |
| An installation is in one of these states, and `doctor` names it: complete; unmigrated; interrupted (a journal is present); damaged (a file fails its manifest). Only a complete installation can be updated, rolled back or changed in components; the others are first repaired or migrated. | proposed; implemented |
| The component set after any command is one of `core sdk`, `core service`, `core sdk service`. Removing `sdk` from an SDK, or `service` from a runtime, is refused with the advice to uninstall. | proposed; implemented |
| Removing `service` keeps `core`, so the SDK still runs, tests and debugs. It removes the login registration and the service's state for this installation, and nothing of another installation. | proposed; implemented |
| Removing `sdk` leaves the runtime and its applications. Applications that are running continue on the old generation, which stays until they end. | proposed; implemented |
| A full uninstall is refused while applications of this installation's service run, and while another process holds a lease, with the list of generations in use. `--terminate` stops the applications and the supervisor; other holders, such as an editor session or a build, must end first. | proposed; implemented |
| `--dry-run` prints every path, link, registration and application the command would affect, and changes nothing. | proposed; implemented |

## Lowering and artifacts

None in the compiler: no stage, NeuraIR or NVR revision, tier, collector or ABI changes, and every image keeps its bytes. The milestone adds four versioned text formats, each independent of the Neura version: the component manifest (1), the install marker (2, reading 1), the component release metadata (1), and the journal (1, private to one build and never read by another). The legacy release metadata stays at 1.

The lifecycle tools link the lifecycle library and the service's control client only, and stay inside the baseline's dependency and size rows: the legacy and SDK archives are each at most 25% larger than the rc.3 archive of their target, and the runtime archive is smaller than the SDK archive. The release workflow applies its library and version checks to every executable of every archive, not to `bin/neura` alone.

## Diagnostics

No source diagnostic is added, so none of the codes `E0711` to `E0719` held for these milestones is used (`E0710` is already taken). Errors are lines on standard error that begin `neura: update: `, `neura: uninstall: ` or `install.sh: `, with the exit statuses of today: 0, 1 for a failure, 3 for an installation the tool does not manage, 4 for `update --check` that found a newer version.

| Condition | Message | Status |
| --- | --- | --- |
| A second lifecycle command | `another install, update or uninstall is running (lock <path>)` | proposed; implemented |
| A link conflict | `<path> exists and is not a link made by neura` for each, then `nothing was changed` | proposed; implemented |
| Components of two builds | `<archive> is build <a>; the installation is build <b>. Update first, then add the component` | proposed; implemented |
| A file that fails its manifest | `<path>: expected sha256 <a>, found <b>` | proposed; implemented |
| Unmigrated | `this installation was updated by a 0.2 client; run "neura update --migrate-install" first` | proposed; implemented |
| A source without component metadata | `release <version> has no component metadata` | proposed; implemented |
| A component that cannot be removed | `removing <name> would leave no product; use "neura uninstall"` | proposed; implemented |
| Applications running | `<n> applications of this installation are running (neura runtime ps); pass --terminate to stop them` | proposed; implemented |
| A generation in use | `generation <g> is in use by a running neura process; end it and run the command again` | proposed; implemented |
| Rollback with nothing to return to | `no previous 0.3 generation is installed` | proposed; implemented |
| A runtime installation asked for an SDK command | `neura: "build" needs Neura-sdk, which is not installed here` (from the dispatcher, status 1) | proposed; implemented |
| A failure after activation began | `update failed and was rolled back; <generation> is still active (<reason>)` | proposed; implemented |

## Edge cases

Each case is a test of the order of work. *Accept* means the command succeeds with the stated result; *reject* means status 1 (or the stated status) and an unchanged installation.

1. A clean SDK install: every `core` and `sdk` tool has a link, no `neurad`, no unit, no socket; `doctor` exits 0 and says the service is not installed (accept).
2. A clean runtime install: no `neurac`, no standard library; `neura run` of an image works; `neura build` names Neura-sdk; `doctor` exits 0 (accept).
3. A clean combined install from two archives: one generation, one `core`, every link (accept).
4. Two archives of different builds given to `install.sh --add`: refused before any change (reject).
5. A regular file named `neurac` in the bin directory: every conflict listed, nothing installed, no `neura` link either (reject).
6. A `neura-fmt` link into another installation's root: a conflict (reject).
7. A dangling `neurac` link into this root: ours; repointed (accept).
8. A bin directory with spaces and non-ASCII characters, and a root reached through a symbolic link: installs; tools resolve the real root (accept).
9. `install.sh` twice for one build without `--force`: refused as today (reject).
10. `--force` on a whole generation while a tool of it runs: nothing is overwritten; links repaired (accept).
11. `--force` on a generation with a truncated file and no lease: set aside and replaced (accept).
12. `--force` on a damaged generation that a running process holds: refused (reject).
13. A rebuilt release of the same version: another build, so another generation name; both can be on disk (accept).
14. Two `neura update` at once: the second fails at the lock (reject).
15. On a schema 1 root, a 0.2 `neura update` started while `--migrate-install` or the 0.3 installer runs: it refuses on `.update-lock`. The 0.3 installer or `--migrate-install` started while a 0.2 update runs: it refuses on the same file and changes nothing (reject).
16. `.update-lock` left on a schema 2 root by an interrupted migration: removed by the next command, which proceeds. The same file on a schema 1 root, more than an hour old: reported as stale, not removed (accept, reject).
17. A fault at each of `download`, `checksum`, `extract`, `verify`, `compose`, `journal`, `place`: unchanged root, no staging directory (reject).
18. A fault at each of `links-remove`, `activate-link`, `links-add`, `activate-marker`: rolled back; old links, `current` and marker as before (reject).
19. A crash at each of steps 7 to 11: the installation works as left; `doctor` reports the interrupted change; the next lifecycle command completes or undoes it by the rule of step 2. A crash during `--rollback` before step 9 leaves both generations on disk after recovery (accept).
20. A crash, then a plain `neura build`: works, before and after the recovery (accept).
21. An archive member that is a symbolic link, a hard link, absolute, or outside the top directory: refused before unpacking, as today (reject).
22. An archive with a file its manifest does not list, a listed file missing, a wrong digest, a wrong mode: refused at `verify` (reject).
23. A manifest with an unknown line, a repeated file, a path with `..`, a missing final newline: refused (reject).
24. Metadata with an unknown product, two archives for one product and target, a size of 0: refused (reject).
25. A release source that has only `neura-release.txt` with the installed version: `up to date` (accept).
26. The same with a newer version: `has no component metadata` (reject).
27. An SDK installation updates: the set stays `core sdk`; the runtime archive is not fetched (accept).
28. A combined installation updates: both archives are fetched and verified; one new generation (accept).
29. A combined update where the runtime archive fails its checksum: nothing changes; the SDK part is not installed alone (reject).
30. An update while `neura lsp` and a debug session run: both keep working on their generation; it stays on disk; `doctor` shows it in use (accept).
31. Two updates later with the editor session still open: its generation is still there; it is removed by the first update after the session ends (accept).
32. An update while a detached application runs: it continues; `neura runtime ps` and `stop` from the new generation reach it; the old supervisor exits when it ends (accept).
33. An update that is rolled back at step 10 while an application runs: the application is untouched (accept).
34. `--rollback` after an update: the previous generation is active, links follow, the newer one becomes the previous (accept).
35. `--rollback` twice: returns to the newer one (accept).
36. `--rollback` when the previous generation is a 0.2 one, or there is none: refused (reject).
37. `--rollback` to a generation whose file was changed on disk: refused at verification (reject).
38. `--components sdk,service` on an SDK: the runtime archive of the installed build is fetched; a new generation; the `neurad` link appears; nothing is started or registered (accept).
39. The same on a candidate while a newer release is the latest: the archive comes from the installed version's own release, so it succeeds; with `--source` naming a release of another build, refused with the advice to update first (accept, reject).
40. `--components sdk` on a combined installation with no application running: a new generation without `neurad`; its link, unit and state are removed; `neura run` still works (accept).
41. The same with an application running: refused; with `--terminate`: the application is stopped, then the change (reject, accept).
42. `uninstall --component service` on a runtime installation; `--component sdk` on an SDK: refused (reject).
43. `uninstall --component sdk` on a combined installation while a build runs: the build finishes on the old generation; new commands find no `neurac` (accept).
44. `uninstall --dry-run`: lists generations, links, the unit, the state directory and running applications; removes nothing (accept).
45. `uninstall --yes` with an editor session open: refused, naming the generation in use (reject).
46. `uninstall --yes` of a combined installation with login start enabled and nothing running: every link, the root, the unit and the state directory are gone; a `PATH` line and files the user put in the root are left and listed (accept).
47. `uninstall` when one owned link was replaced by a regular file: that path is left and listed; the rest is removed (accept).
48. A second installation's unit and socket: untouched by the first's uninstall (accept).
49. The bridge from rc.3 and from 0.2.0: after the old `neura update`, `neura --version` reports 0.3.0, `neura build`, `neura run` and `neura test` work with no `leases/` directory, `neurac` is not on `PATH`, and `doctor` fails on `migration` (accept).
50. `--migrate-install` then: every link, schema 2, `doctor` exits 0, `neurac --version` works from `PATH` (accept).
51. `--migrate-install` again, and on a fresh 0.3 installation: `already migrated`, status 0 (accept).
52. `--migrate-install` with a conflicting file at one tool's path: nothing changed, still unmigrated (reject).
53. `--migrate-install` interrupted after some links: the next run completes it (accept).
54. `--migrate-install` when a file of the generation was altered: refused at verification (reject).
55. `neura update` on an unmigrated installation: refused with the instruction (reject).
56. `neura uninstall --yes` on an unmigrated installation: removes it by the schema 1 rules (accept).
57. `--runtime=required` on an unmigrated installation: refused with the instruction; a plain run works (reject).
58. A 0.2 `install.sh` on a schema 2 root; a 0.3 `install.sh` on a 0.2 root: the first refuses, the second migrates and installs (reject, accept).
59. A tool copied out of its generation and run alone: unmanaged, status 3 for `update`, and it does not dispatch to siblings it does not have (reject).
60. `neurac` replaced by one from another build inside a generation: `doctor` fails on `integrity`; the dispatcher's own check of its siblings is the earlier milestone's (reject).
61. A root on a file system without `flock`: tools run; nothing is pruned; `doctor` warns (accept).
62. A package manager's installation: `update`, `uninstall` and `--migrate-install` exit with status 3 (reject).
63. `doctor`, `--version`, `capabilities` and `update --check` on every state above: no file under the root changes (accept).
64. Without terminals: no update notice, no migration notice, and no state file, as today (accept).

## Order of work

Each step is one commit on the milestone branch. No step changes an image.

| Step | Content | Targeted tests |
| --- | --- | --- |
| 0 | Merge the split into sibling executables and the service milestone. If the dispatcher already reads a registry file of its own, replace it by the manifests' `tool` lines here. | Their tests |
| 1 | This note | Documentation checks |
| 2 | `src/cli/install_layout.{hpp,cpp}`: the component manifest, marker schema 2, `neura-components.txt` and the journal, parsed and formatted; generation names; `detect_install` for both name forms and both schemas | `lifecycle-unit-manifest`, `-marker`, `-release-metadata`, `-journal`, `-generation` in `tests/lifecycle/unit_tests.cpp` (cases 23, 24) |
| 3 | CMake install components `core`, `sdk`, `service`; `make-archive.sh` takes a product and the component prefixes and writes the manifests; `make-release-metadata.sh` sorts the three archive names and writes both metadata files | `lifecycle-install`: contents of the three archives, identical `core`, reproducible bytes (cases 1 to 3 as archives) |
| 4 | The change procedure in `src/cli/lifecycle.cpp`: lock, journal, recovery, link checks, `--install-from`, `--add`, `--force`; `install.sh` reduced to its wrapper; the bootstrap script's `--components` | `lifecycle-install`, `lifecycle-bootstrap` (cases 4 to 13, 21, 22) |
| 5 | Leases in every tool's start; `neura update` for components; pruning; `--rollback`; `--repair`; the fault and crash steps | `lifecycle-update` (cases 14 to 20, 25 to 37, 61) |
| 6 | The legacy archive; the unmigrated state; `--migrate-install`. The CTest builds the state a 0.2 updater leaves from the legacy archive, by hand. | `lifecycle-migrate` (cases 49 to 58, with the real 0.2 clients in the release workflow) |
| 7 | `--components`, `uninstall --component`, `--terminate`, the full uninstall with service state | `lifecycle-components`, `lifecycle-uninstall` (cases 38 to 48, 59, 62) |
| 8 | `doctor` | `lifecycle-doctor` (cases 60, 63, 64, and the states of the other cases) |
| 9 | With the service: retire on update, applications across update and rollback, removal of the unit | `lifecycle-service` (cases 30 to 33, 40, 41) |
| 10 | `qualify.sh` and `release.yml`: three archives for each target, every executable's dependencies, the size report, the bridge from `v0.2.1-rc.3` and `v0.2.0`, the 0.3 update from the previous candidate; a trial run of the workflow that publishes nothing | The workflow trial |
| 11 | Install, Release process, Tooling, the README quick start, the changelog and the migration guide | Documentation checks |

## Gate and acceptance

- **Images unchanged.** Every image the compiler builds is byte-identical before and after the milestone; the runtime identity captures are identical. The milestone has no other way to change execution.
- **Three clean installs** on each target, from the packed archives, in a new home directory with an empty environment: SDK without service, runtime only, and both. In each, `doctor` exits 0, every tool of the set runs from the bin directory by its own name and through `neura`, and no process, socket or login registration exists that was not asked for.
- **The SDK without a service** builds, runs and tests the examples and passes the debugger's tests with `neurad` absent from the tree.
- **The runtime alone** runs an image the SDK built, on a machine image with no compiler and no standard library, directly and through its service.
- **The bridge.** Starting from the published `v0.2.1-rc.3` and `v0.2.0` archives: the old `neura update`, then `--migrate-install`, then cases 49 to 53, then an update with the 0.3 client. This runs in the release workflow for every candidate, against the real 0.2 executables.
- **A candidate update.** From the second candidate on, an installation of the previous candidate updates with running applications, an editor session and a debug session open (cases 30 to 33), rolls back, updates again, and uninstalls.
- **Every edge case** above passes on Linux and macOS, x86-64 and arm64. Login registration is checked with stand-in service managers in CTest and with the real ones in the manual qualification on macOS, WSL 2 and a Linux machine outside the hosted runners; where a platform has no user service manager, the qualification says so and checks on-demand start.
- **Budgets.** The baseline's size and dependency rows hold for every archive and executable, and the report lists each executable's stripped size and each archive's size.

These are observed results on the stated platforms and cases. The note claims no property of file systems beyond the atomic rename of one entry.

## Migration

For the migration guide and the pages named in the introduction:

- A script that copies `bin/neura` alone no longer has a compiler or a runner: copy the generation, or install the runtime archive for deployment.
- Add nothing to `PATH`: the directory that held `neura` now holds every tool.
- From 0.2: `neura update` (with `--nightly` to reach a candidate from rc.3), then `neura update --migrate-install`. The 0.2 updater removes the old version at once, so close editor sessions first; from 0.3 on they may stay open.
- `install.sh` takes the same options and environment variables. `neura-install.txt` is schema 2; tools that read it should read the `link` lines instead of the one `link` path.
- Release automation that expects one archive for each target finds three, and `neura-components.txt` beside `neura-release.txt`. [`make-release-metadata.sh`](../../packaging/make-release-metadata.sh), [`qualify.sh`](../../packaging/qualify.sh) and [`release.yml`](../../.github/workflows/release.yml), which today match `neura-*-<triple>.tar.gz`, match each product's exact name.
- Old generations are no longer removed at once. `neura update --repair` removes those no one uses.

## Decisions taken without the owner

1. Components are described by one strict line-oriented manifest each, under `share/neura/components/`, not by one JSON registry. It matches the install formats the tools already parse strictly, the archive scripts can write it with POSIX tools, and a combined installation is a union of files.
2. The installer's logic is in the lifecycle code, and `install.sh` calls it. One implementation of locking, journaling and link checks is safer than a second one in shell.
3. A generation's name is the version and a digest of its manifests, so the same content has the same name and a rebuild or another component set never overwrites a generation in place.
4. Leases are shared `flock`s that the kernel releases, not process ID files.
5. The previous generation is always kept, and `--rollback` and `--repair` exist. Rollback then needs no network, at the cost of one generation on disk.
6. Changing components always makes a new generation, even to add one file, so that no generation is ever written twice.
7. The legacy archive is the SDK without the service, under the old name; the legacy metadata lists nothing else.
8. An unmigrated installation fails `doctor` and refuses updates and service use until migrated; direct commands work. Failing is the stricter reading of an installation whose tools are not on `PATH`.
9. A full uninstall is refused while any process holds a lease, and `--terminate` stops only the service's applications. The tool does not kill editor sessions or builds.
10. The bridge is qualified from both `v0.2.1-rc.3` and `v0.2.0` on every candidate, since both are in use and only the first is the workflow's "previous release".
11. Removing the only product component is refused in favour of `neura uninstall`; an installation of `core` alone is not a state.
12. No diagnostic code is used.
13. The dispatcher reads the component manifests only when the tool of a command is missing, to name the product that has it, and otherwise forwards on the tool's presence as the earlier milestone did. Reading three manifests on every command would cost more than the launch budget leaves: `neura run` of a hello-world image measured 0.601 ms before this milestone and 0.611 ms after it, with two leases taken (the dispatcher's and the tool's), against a budget of 5% over rc.3's 0.660 ms. The earlier milestone's dispatcher reads no registry file, so step 0 had nothing to replace.
14. A `.update-lock` that this tool creates holds `neura-lifecycle <pid>`. One found while the installation lock is held is left over from an interrupted command of this tool, not a running 0.2 update, and is removed; any other one is the 0.2 rule's.
15. The previous generation is the most recently activated other one; a generation's lease file is touched at each activation, and its time orders them. The marker does not repeat it, as the marker rules say.
16. `--rollback` returns to a previous generation that has manifests, whatever its name, so the generation a 0.2 updater placed from the legacy archive (named by its version alone) is a 0.3 generation for this rule. A generation without manifests is a 0.2 one and is refused.
17. The lifecycle reaches the service through `ServiceHooks` (`src/cli/lifecycle.hpp`): the applications that run, terminating them, and retiring a supervisor. The service milestone's control client fills them. Without it, a supervisor is found by the exclusive `flock` it holds on its `lock` file, a running supervisor counts as applications that may run (the refusal then says `a supervisor of this installation is running (neura runtime status); pass --terminate to stop it and its applications`), `--terminate` runs the active generation's `neura runtime shutdown --terminate`, and retire does nothing, since a supervisor also retires when `current` moves. The login unit and the state directory are removed by their paths in the service note, after `systemctl --user disable` where `systemctl` exists. `NEURA_TEST_SERVICE_CONTROL` names a stand-in for the tests.
18. On an unmigrated installation `update --check` and `--repair` refuse with the instruction too, the stricter reading of "only a complete installation can be updated".
19. `doctor` names the state in its `install` line and adds a `leases` line, which warns on a file system that cannot lock (edge case 61).
20. A generation holds the files and manifests of its components only; `install.sh` and `VERSION` stay in the archive. A crash point after each added link (`links-add-<n>`) tests an interrupted migration, and a crash ends the process with status 70.
21. The scenario tests are Python suites (`tests/lifecycle/scenarios.py`), as the tools' tests are, under the ctest names of the order of work; step 3's archive checks are `lifecycle-archives`. They replace the 0.2 CMake scripts and keep their assertions where the rule still holds.
22. Step 8 (`doctor`) was committed before step 7, whose tests use it, and the commands of steps 5 to 7 were written in step 4's change procedure and committed with their tests.
23. The installer refuses to drop a component the installation has (an SDK archive alone over a combined installation): a removal goes through `--components` or `uninstall --component`, which check the service's applications and remove its state.
24. A tool that finds its lease held exclusively waits for it and then decides by the lease file's link count, since `doctor` and `uninstall --dry-run` take the exclusive lock for an instant to see whether a generation is in use. A command converting its own shared lease to test it takes the shared lock again afterwards, as a failed conversion drops it on Linux.
25. `<run>` is `XDG_RUNTIME_DIR` only when it is a directory of this user with mode 0700, else `${TMPDIR:-/tmp}/neura-<uid>`, as the service computes it.

## Results

Measured on x86-64 Linux, gcc 14, `NEURA_PORTABLE_RELEASE`, under a load average of about 4 on 8 cores, pinned to one processor, interleaved, six rounds of the median of 50 runs:

| Launch | Before (the earlier milestone, schema 1 install) | After (schema 2 install, leases) |
| --- | ---: | ---: |
| `neura run hello-world.nvr -- a b` | 0.601 ms | 0.611 ms (+1.6%) |
| `neura-run hello-world.nvr -- a b` | 0.532 ms | 0.540 ms (+1.5%) |
| `neura --version` | 0.081 ms | 0.083 ms |

| Archive, x86-64 Linux | Bytes | Against rc.3 (10,008,390) |
| --- | ---: | ---: |
| `neura-sdk-…tar.gz` | 11,663,386 | +16.5% |
| `neura-…tar.gz` (legacy) | 11,663,456 | +16.5% |
| `neura-runtime-…tar.gz` | 4,026,482 | −60% |

`packaging/qualify.sh` on these archives, with the published `v0.2.1-rc.3` and `v0.2.0` archives as the previous release and the bridge, passed its 14 checks. The archives were built with the version line `0.2.1`, since `VERSION` holds 0.2.1 until the release step sets 0.3.0; the bridge needs only a version newer than the 0.2 client's. The aarch64 Linux and macOS archives are built only by the release workflow and were not measured here, and the edge cases ran on Linux only.

