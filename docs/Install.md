# Install, update, and uninstall

Install Neura from a release. A release has prebuilt archives for each supported platform, and [one command](#install-with-one-command) downloads, verifies, and installs the ones for your machine. Nothing is compiled on your machine.

Neura comes as two products. **Neura-sdk** is everything Neura does: compiling, running, testing, debugging and decompiling programs, the standard library, and packaging native executables. **Neura-runtime** runs images that an SDK built, with no compiler and no standard library sources, and adds the resident runtime service `neurad`. An installation is a set of *components*: `core` (the execution core and the lifecycle tools), `sdk`, and `service`. Neura-sdk is `core` and `sdk`, Neura-runtime is `core` and `service`, and both together are one installation with one copy of `core`.

Each archive holds the tools of its components, a manifest for each component, and an `install.sh` that installs them for the current user. Three commands of the installed tool manage the result: `neura doctor` checks it, `neura update` replaces it with a newer release or changes its components, and `neura uninstall` removes it. A [build from source](#build-from-source) is for working on Neura itself and for platforms without an archive; `neura update` does not manage one. The design is in [the lifecycle note](design/Lifecycle030.md).

[Tooling](lang/Tooling.md) covers the compiler commands. This page covers the tool itself.

## Install with one command

On Linux, on macOS, and on Windows inside a WSL 2 distribution:

```sh
curl --proto '=https' --tlsv1.2 -fsSL https://github.com/Kayyo321/neura/releases/latest/download/install.sh | sh
```

One installation holds everything Neura does: `neura build`, `run`, `test`, `decompile`, and `build --native`, with the standard library and the native launchers. Nothing else has to be installed to compile or run a Neura program. [C imports](#supported-platforms) are the one feature that needs system packages, and `neura doctor` names them.

The command installs Neura-sdk, without the service. It downloads `install.sh` from the latest release and runs it. That script, [`packaging/bootstrap.sh`](../packaging/bootstrap.sh) in the repository:

1. Finds the machine's target and stops with a message when no archive exists for it, when the system's C library is not glibc, or when the system is neither Linux nor macOS.
2. Downloads the release's `neura-components.txt` and the one or two archives that the chosen components need over HTTPS.
3. Checks each archive's size and SHA-256 against `neura-components.txt`, and checks its build provenance with `gh attestation verify` when the GitHub CLI is installed and signed in. Without `gh`, or with a `gh` that is not signed in, it prints that provenance was not verified and continues. It stops only when the check ran and did not confirm the archive.
4. Checks that every member of each archive is inside the one expected directory, unpacks it into a temporary directory, and runs the first archive's own `install.sh`, with `--add` for the second, described [below](#install-from-a-release-archive).

A release without `neura-components.txt` (0.2 and earlier) is installed from its `neura-release.txt` and its one archive, as before; it has no runtime archive.

It writes only under the install root and the bin directory, never uses `sudo`, and never edits a shell startup file. It needs `curl`, `tar`, and `sha256sum` or `shasum`. To read the script before running it, download it first:

```sh
curl --proto '=https' --tlsv1.2 -fsSLO https://github.com/Kayyo321/neura/releases/latest/download/install.sh
sh install.sh
```

| Option | Effect |
| --- | --- |
| `--components <set>` | `sdk` (the default), `runtime` (the execution core and the service), or `sdk,service` (both). |
| `--version <version>` | Install that version, such as `0.1.0-rc.1`, in place of the latest release. Use this for an initial release candidate installation; an installed tool can also opt into candidates with `neura update --nightly`. |
| `--source <url or directory>` | Read the release from another HTTPS location or from a local directory that holds the release metadata and the archives. |
| `--force` | Install again a generation that is already installed. |

When the script is piped, options follow `sh -s --`, as in `... | sh -s -- --components runtime`.

## Supported platforms

A release has one archive for each supported target.

| Platform | Archive target | Requirements | Notes |
| --- | --- | --- | --- |
| Linux on x86-64 | `x86_64-linux-gnu` | glibc 2.39 or newer | Every feature. Built and tested on Ubuntu 24.04. |
| Linux on arm64 | `aarch64-linux-gnu` | glibc 2.39 or newer | Every feature. Built and tested on Ubuntu 24.04. C imports map `char` to `uint8` here, as the platform's C compilers do. |
| Windows 10 and 11 on x86-64 | `x86_64-linux-gnu`, under WSL 2 | A WSL 2 distribution with glibc 2.39 or newer, such as Ubuntu 24.04 | Windows has no archive of its own. Install the Linux archive inside the distribution and run `neura` there. |
| macOS on Apple silicon | `aarch64-apple-darwin` | macOS 15 or newer | Built and tested on macOS 15. No native launcher: `neura build --native` reports that and stops. |
| macOS on Intel | `x86_64-apple-darwin` | macOS 15 or newer | The same. |

The archives need nothing else from the system. On Linux, `neura` and the native launchers carry the C++ runtime and libffi, and the release workflow fails if either file needs a library other than the C library, or a C library newer than glibc 2.39. On macOS, `neura` carries libffi and needs only libraries that are part of macOS, which the workflow also checks. Distributions with an older glibc, such as Debian 12 and Ubuntu 22.04, and distributions with another C library, such as Alpine, are not supported by the archives.

Optional features need more, and `neura doctor` reports each one:

| Feature | Needs |
| --- | --- |
| C imports (`import:c`) | libclang 14 or newer and Clang's builtin headers; on Debian and Ubuntu, `libclang1-19` and `libclang-common-19-dev`; on macOS, the Xcode Command Line Tools (`xcode-select --install`), which supply libclang and the SDK's headers. Linking a C import also needs a C compiler. |
| GPU execution | The release archives are built without the GPU backend. Build from source with the Vulkan headers and `glslangValidator` to get it. |
| `neura update` | `curl` and `tar`; `gh` to verify build provenance. |
| The service (`neurad`) | Neura-runtime, installed alone or with the SDK. |

### Platforms without an archive

| Platform | State |
| --- | --- |
| Windows without WSL | Not supported. No Windows build has been compiled or tested. |

## Install from a release archive

Each release publishes three archives for each target:

| File | Holds | For |
| --- | --- | --- |
| `neura-sdk-<version>-<target>.tar.gz` | `core` and `sdk` | New installations and the one-line command |
| `neura-runtime-<version>-<target>.tar.gz` | `core` and `service` | New installations and the one-line command |
| `neura-<version>-<target>.tar.gz` | Exactly the tree of the SDK archive | 0.2 clients, whose updater requires this name ([the 0.2 bridge](#from-02)) |

The `core` files are byte-identical in the three archives of one target. The one-line command does these steps for you. To do them by hand, download the archives you need and `SHA256SUMS` from the release page, then:

```sh
sha256sum -c --ignore-missing SHA256SUMS      # on macOS: shasum -a 256 -c SHA256SUMS
gh attestation verify neura-sdk-<version>-<target>.tar.gz --repo Kayyo321/neura
tar -xzf neura-sdk-<version>-<target>.tar.gz
cd neura-sdk-<version>-<target>
./install.sh                                    # or: ./install.sh --add ../neura-runtime-<version>-<target>
```

`sha256sum -c` checks that the file is the one the release lists. `gh attestation verify` checks that the file was built by the repository's release workflow; it needs the [GitHub CLI](https://cli.github.com/) and network access, and a file that fails it must not be installed.

`install.sh` is a POSIX shell script that checks `VERSION` and that the archive's `bin/neura-update` runs, then runs `bin/neura-update --install-from <its directory>`, where every check, the lock and the journal are. It never uses `sudo` and never edits shell startup files. The installer:

1. verifies every file of the archive against its component manifests: size, SHA-256 and mode, and that nothing else is there; with `--add`, that the second archive is of the same build and brings the same `core`;
2. composes the components into one *generation*, `<root>/versions/<version>+<id>/`;
3. checks every public link it will make, and stops listing every path that exists and is not a link made by Neura, changing nothing;
4. makes the change by [the steps of a change](#a-change-step-by-step): places the generation, points `<root>/current` at it, creates `<bin dir>/<tool>` for each tool, linking to `<root>/current/bin/<tool>`, and writes the marker `<root>/neura-install.txt`.

It prints what it did. If `<bin dir>` is not on `PATH`, it also prints the line to add to your shell startup file, `export PATH="<bin dir>:$PATH"`, and leaves the edit to you.

| Setting | Default | Override |
| --- | --- | --- |
| Install root `<root>` | `${XDG_DATA_HOME:-$HOME/.local/share}/neura` | `NEURA_INSTALL_ROOT`, read by the installer only |
| Bin directory `<bin dir>` | the installation's own, else `$HOME/.local/bin` | `NEURA_BIN_DIR`, read by the installer only |

Both must be absolute paths. The installer uses their real paths, so a root reached through a symbolic link works. After installation each tool finds its root from the real path of its own executable, so the variables have no effect on a running tool.

| Option | Effect |
| --- | --- |
| `--add <dir>` | Also install the components of a second unpacked archive of the same build, so that the SDK and runtime archives install as one generation. |
| `--force` | For a generation that is already installed: when every file matches its manifest, nothing is copied, the installation switches to it and its links are repaired; when it is damaged and no running process uses it, it is set aside and replaced; when it is damaged and in use, the command is refused. |

The installer refuses to:

- install a generation that is already there, unless you pass `--force`;
- replace any path in `<bin dir>` that is not a symbolic link into this root: a regular file, or a link into another installation, is a conflict;
- install archives of two builds together;
- drop a component the installation has: an SDK archive alone over an installation with the service is refused, with the advice to `--add` the runtime archive or to remove the service first with `neura uninstall --component service`;
- change a root whose marker names a package manager (status 3).

A refusal changes nothing. A different version, or another build of the same version, is another generation; the installation switches to it and keeps the one before as the previous generation.

## Layout

```text
<root>/neura-install.txt                   the marker, schema 2
<root>/versions/<generation>/              one generation: bin/, share/, lib/
    bin/neura                              the dispatcher
    bin/neurac, bin/neura-run, ...         the tools it starts
    lib/neura/libneura-core-<version>.so   the code the tools share (.dylib on macOS)
    lib/neura/libneura-sdk-<version>.so    the compiler's part of it
    lib/neura/launchers/<target>/...       where the platform has launchers
    share/neura/stdlib/...
    share/neura/components/<component>.txt the component manifests
    share/doc/neura/LICENSE
<root>/current                             symbolic link: versions/<generation>
<root>/leases/<generation>                 an empty file that users of the generation lock
<root>/.lock                               the installation lock
<root>/.journal                            present only during a change
<bin dir>/<tool>                           symbolic link: <root>/current/bin/<tool>, one for each tool
```

A generation's name is `<version>+<id>`, where `<id>` is the first 12 hexadecimal digits of the SHA-256 of its manifests, concatenated in the order `core`, `sdk`, `service`. The same build with the same components has the same name everywhere; another build, or another set of components, has another. A generation is never written after it is placed: a change of version or of components makes a new one. A generation that a 0.2 updater placed is named `<version>` alone and keeps that name.

Every public link points through `current`, so one rename of `current` moves every tool to the new generation at once. `neura` starts the tool of each command from `bin/` beside its own real path ([Tools](lang/Tooling.md#tools-and-the-neura-dispatcher)), and the tool resolves the same generation: a command that spans tools uses the siblings of its own generation throughout, although `current` may move meanwhile. The service's socket, records and log are outside the root, in the user's runtime and state directories (the service note, `docs/design/Service030.md`).

### Component manifests

Each component has one manifest, `share/neura/components/<name>.txt`, which [`packaging/make-archive.sh`](../packaging/make-archive.sh) writes and the tools parse strictly: these lines in this order, single spaces, a final newline, nothing else.

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

`build` is the commit the release was built from; all components of a generation have the same `version`, `build` and `target`. `sdk` and `service` require `core`, which requires nothing. `tool <name>` declares the public executable `bin/<name>`, which gets a public link; tool lines are sorted. `file` lists every file of the component except the manifest, sorted by path, with mode 755 or 644; a file belongs to one component, and a path uses only letters, digits, `.`, `_`, `+`, `-` and `/`. The `service` manifest has the line `protocol service 1.0` after `requires`; in a build without `neurad` it has no tool and no file. A component name the reader does not know is an error for an install or an update and is reported and otherwise ignored by `doctor`.

### The marker

```text
neura-install 2
method archive
bin /home/user/.local/bin
components core sdk
generation 0.3.0+1a2b3c4d5e6f
generation 0.3.0-rc.1+0f1e2d3c4b5a
link neura
link neura-doctor
link neurac
```

`components` is the set an update preserves. There is one `generation` line for each directory in `versions/`, and one `link` line, sorted, for each link in `bin` the installation owns. The active generation is what `current` names. A schema 1 marker, which 0.2 wrote and a 0.2 updater still writes, is still read: it describes an installation with the one link `neura`:

```text
neura-install 1
method archive
link /home/user/.local/bin/neura
version 0.2.0
```

Any other line, a repeated generation or link, or a missing final newline makes a marker invalid. A tool resolves the real path of its executable. If that path is `<root>/versions/<generation>/bin/<tool>` and `<root>/neura-install.txt` is a valid marker of either schema, the install method is the marker's `method`. Otherwise the method is `unmanaged`: a source build, or a copy of an executable.

| Install method | Meaning | `neura update` and `neura uninstall` |
| --- | --- | --- |
| `archive` | Made by the installer | Manage the install. |
| a package manager's name, such as `homebrew` or `deb` | The package manager owns the files | Exit with status 3 and name the package manager. |
| `unmanaged` | No marker | Exit with status 3 and explain that the tool was probably built from source or copied. |
| a marker that is not valid | Damaged | Exit with status 1 and name the problem. |

### Generations in use

Every tool of an installed generation, and `neura` until it becomes the tool, takes a shared `flock` on `<root>/leases/<generation>` when it starts and holds it until it exits. A supervisor and its workers hold it too, so an application that runs holds its generation. The descriptor is close-on-exec: a program a tool starts does not hold a lease. The shared libraries are loaded before the tool's code runs; a removed file stays readable to a process that has it open. A tool that finds the lease held exclusively waits for it: a command is removing the generation, and the tool then stops as below, or only checking whether it is in use.

A generation is removed only by a command that holds the installation lock and takes an exclusive `flock` on its lease without waiting; the kernel releases a lock when its holder ends, so a process that died holds nothing. The previous generation is always kept, so that `neura update --rollback` needs no download. Other generations are removed by the next change, or by `neura update --repair`, once no process holds them. On a schema 2 root a tool whose lease file is gone ends with `neura: this generation of neura was removed; run the command again`, status 1. On a root that a 0.2 client made, which has no `leases/`, tools run without a lease. Where the file system cannot lock, tools run without a lease, no generation is removed automatically, and `doctor` says so.

## Check an install

`neura doctor` prints one line for each check, `ok`, `warn`, or `FAIL`, two spaces, the check name, a colon, and a detail. It exits with status 0 when no check failed and 1 otherwise. It does not use the network, starts no service, runs no project code, and changes no file under the root.

An installation is in one of four states, which the `install` line names: *complete*; *unmigrated* (a 0.2 updater produced it, see [From 0.2](#from-02)); *interrupted* (a journal is present); *damaged* (a file fails its manifest). Only a complete installation can be updated, rolled back or changed in components; the others are first repaired or migrated.

| Check | Reports | When it is not `ok` |
| --- | --- | --- |
| `version` | The version, target, and commit | |
| `executable` | The real path of the executable | `warn` if it cannot be determined |
| `install` | The install method, the root, the marker schema and the state | `FAIL` for an invalid marker |
| `components` | The components of this generation, from its manifests, with the version and build; in a source build, the tools next to the executable | `warn` for a component name it does not know, which it ignores; `FAIL` if a core tool is missing |
| `integrity` | Every manifest file exists with its size; every executable and library has its digest | `FAIL` names the first three that do not |
| `sdk` | Without `sdk`: `not installed; source commands need Neura-sdk`, and the checks below that need the SDK are not printed | |
| `stdlib` | With `sdk`: the standard library directory | `FAIL` if `share/neura/stdlib/std` is not found next to the executable |
| `launchers` | With `sdk`: the native launchers for this host | `warn`: `neura build --native` is unavailable |
| `libclang`, `clang-headers` | With `sdk`: the libclang library that C imports load, and its builtin headers | `warn`: C imports are unavailable; on Debian and Ubuntu install `libclang1-19 libclang-common-19-dev` |
| `c-compiler` | With `sdk`: `$CC` or `cc` on `PATH` | `warn`: the libraries of C imports cannot be linked |
| `gpu` | With `sdk`: whether GPU graph execution is compiled in and the Vulkan loader opens | `warn` if it is compiled in and the loader does not open |
| `service` | With `service`: the protocol version, whether a supervisor runs (found by the lock it holds) and whether login start is registered; without it, `not installed (optional)` | |
| `curl`, `tar` | The tools on `PATH` | `warn`: `neura update` needs them |
| `gh` | The GitHub CLI on `PATH` | `warn`: `neura update` cannot verify build provenance |
| `links` | That every tool has its link through `current` | `warn` for each link that is missing or not ours |
| `path` | That the bin directory is on `PATH` | `warn` |
| `migration` | | `FAIL` for an unmigrated installation, with the command to run |
| `transaction` | | `FAIL` for a journal left by an interrupted change, with the command to run |
| `generations` | The active and the previous generation | `warn` for each other generation on disk, saying whether a process uses it |
| `leases` | That tools hold the lease of their generation | `warn` on a file system that cannot lock |
| `update-notices` | Whether [update notices](#update-notices-and-privacy) are on in this environment | |

## Update

```sh
neura update [--check] [--nightly] [--source <https-url-or-directory>] [--require-attestation]
neura update --components sdk|runtime|sdk,service [--terminate] [--source <https-url-or-directory>]
neura update --rollback | --repair | --migrate-install
```

`neura update --check` reports whether a newer release exists and changes nothing. `neura update` installs it, keeping the installed set of components: an SDK installation fetches the SDK archive only, a combined one both archives, which are verified before either is installed. Neither asks a question. `neura update` and `neura-update` behave the same.

The default channel follows the latest stable GitHub release. Add `--nightly` to include published prereleases, including release candidates:

```sh
neura update --check --nightly
neura update --nightly
```

`--nightly` selects the highest semantic version across stable releases and prereleases, regardless of publication date. For example, `0.2.1-rc.10` is newer than `0.2.1-rc.2`, `0.2.1` is newer than either, and `0.2.2-rc.1` is newer than `0.2.1`. Drafts, unpublished releases, and tags without a valid semantic version are ignored. It never downgrades an installation. This flag applies to this invocation only; automatic update notices continue to follow stable releases.

| Option | Meaning |
| --- | --- |
| `--check` | Report only. |
| `--nightly` | Include GitHub prereleases when selecting the newest version. Works with `--check` and `--require-attestation`. A non-default `--source` overrides GitHub discovery and continues to read exactly that source's metadata. |
| `--source <location>` | Where the release metadata and the archives are. An `https://` URL, or a local directory. The default is `https://github.com/Kayyo321/neura/releases/latest/download`. A URL with any other scheme, including `http://`, is rejected. |
| `--require-attestation` | Fail unless build provenance is verified, for every archive fetched. Without `gh` on `PATH` this fails before anything is downloaded, and with a `gh` that is not signed in it fails before anything is installed. With a local directory source, it also runs the check that is otherwise skipped. |
| `--components <set>` | Change the set: `sdk`, `runtime` (`core` and `service`) or `sdk,service`. Removing a component fetches nothing. Adding one fetches the archive of the installed version from that version's own release (`releases/download/v<version>`, or `--source`) and requires its build to be the installed one; otherwise it stops with `<archive> is build <a>; the installation is build <b>. Update first, then add the component`. Every change of components makes a new generation. |
| `--terminate` | With `--components` that removes `service`: first stop the applications of this installation's service, as `neura runtime shutdown --terminate` does. Without it, a removal that affects running applications is refused. |
| `--rollback` | Make the previous generation active again, when it is still on disk and has manifests. It is verified first; the generation rolled away from becomes the previous one. Fetches nothing. |
| `--repair` | Complete or undo an interrupted change, restore owned links, and remove generations no one uses beyond the active and the previous one. Fetches nothing. |
| `--migrate-install` | Finish an installation that a 0.2 updater produced. See [From 0.2](#from-02). |

| Exit status | Meaning |
| --- | --- |
| 0 | The command succeeded, or the installed version is already up to date. |
| 1 | The command failed. Either nothing was changed or the change was rolled back; the message says which. Also an invalid marker, a refused source, a held lock, and an unmigrated installation. |
| 3 | This install is not managed by `neura update`: it is `unmanaged`, or a package manager owns it. Exit 3 is reported before any network access. |
| 4 | Only from `--check`: a newer version is available. |

A 0.3 client asks a release source for `neura-components.txt`. If the source has none, it reads `neura-release.txt`: when that names a version that is not newer, the installation is up to date; when it names a newer one, the update fails with `release <version> has no component metadata`.

### A change, step by step

Every command that changes the root (the installer, `update`, `--components`, `--rollback`, `--repair`, `--migrate-install` and `uninstall --component`) runs these steps.

1. **lock**: an exclusive `flock` on `<root>/.lock`, not waited for: a second command fails at once with `another install, update or uninstall is running (lock <path>)`. On a root with a schema 1 marker, where a 0.2 updater may run, the command also creates the 0.2 lock file `.update-lock` by the 0.2 rules and keeps it until it ends: a file that is already there means a 0.2 update is running, or is reported as stale after an hour for you to delete, and the command changes nothing. A `.update-lock` that this tool wrote (its first word is `neura-lifecycle`) is left over from an interrupted command and is removed. On a schema 2 root no 0.2 client can run, so a `.update-lock` found there is removed.
2. **recover**: a journal that is present is from a command that ended early. If `current` already names the journal's new generation the change is completed from step 9; otherwise it is undone: removed links are restored, and the new generation's directory is deleted only when the journal says this change placed it. Staging directories and temporary files that a crash left are removed.
3. **download**, **checksum**: for each archive, as [What is verified](#what-is-verified) describes.
4. **extract**: the archive is listed with `tar` and rejected if any member is absolute, has a `..` component, lies outside the one top directory, or is anything but a regular file or a directory. Then it is unpacked into a staging directory `<root>/.staging-<pid>-<random>/` with mode 0700, and the tree is checked again with `lstat`.
5. **verify**: each manifest parses; every listed file is there with its size, digest and mode; nothing else is there but the manifests, `install.sh` and `VERSION`; all manifests agree on version, build and target; `core` is identical where two archives bring it; the staged `bin/neura --version` prints a line that starts `neura <version> `.
6. **compose**: the selected components are copied into one tree, with each file's mode, the generation's name is computed, and every public link is checked: a path that is absent, or a symbolic link into this root, is ours to set; anything else is a conflict, and every conflicting path is listed with `nothing was changed`.
7. **journal**, **place**: the journal `<root>/.journal` is written and synced, then the new tree is renamed to `versions/<generation>` and its lease file is created.
8. **links-remove**: links of tools the new set does not have are removed.
9. **activate-link**: `current` is replaced by one rename.
10. **links-add**: links of tools the old set did not have are created.
11. **activate-marker**: the marker is replaced by one rename and the journal is removed.
12. **retire**, **prune**: a running supervisor of the old generation is told that it is retired, and generations other than the active and the previous one are removed when no process holds their lease. A failure here is a warning; the change has succeeded.

A failure at steps 3 to 7 removes the staging directory and leaves the installation untouched. A failure at steps 8 to 11 is undone in reverse order, by the rule of step 2, and the command reports `update failed and was rolled back; <generation> is still active (<reason>)`. The installation works in every intermediate state: before step 9 every link resolves in the old generation, and after it every link that exists resolves in the new one. Only the rename of one entry is atomic; the journal makes a change of several entries recoverable.

### What is verified

| Check | `https://` source | Local directory source |
| --- | --- | --- |
| Size and SHA-256 of each archive against `neura-components.txt` | Always. The metadata arrives over HTTPS. | Always. The metadata is read from the directory. |
| Every file against the component manifests | Always | Always |
| Build provenance: `gh attestation verify <file> --repo Kayyo321/neura` | When `gh` is on `PATH`. A failure aborts the update. | Only with `--require-attestation`. |
| Without `gh`, or with `gh` not signed in | A line says that build provenance was not verified and that integrity rests on the SHA-256 from the metadata fetched over HTTPS. With `--require-attestation` the update stops with status 1. | The same line, or with `--require-attestation` status 1. |

The update prints which verifications ran. A SHA-256 taken from the same place as the archive detects corruption and tampering in transit. It does not detect a replaced release, because whoever can replace the archive can replace the digest. Build provenance covers that case: it proves the file was produced by the repository's workflow. Install `gh` and run `neura doctor` to see whether it is available.

For an `https://` source, `curl` runs as a child process with an argument vector, never a shell, restricted to HTTPS (also after redirects) and TLS 1.2 or newer, writing to a temporary file in `<root>`. `curl` reads no `.curlrc`. With `--nightly` and the default source, the GitHub releases API is read first, in pages of 20 releases (at most 100 pages, 1 MiB per page, and 120 seconds total), and the source is pinned to the selected tag.

### Release metadata

A release has two metadata files. `neura-release.txt` stays at schema 1 and names only the legacy archive, since a 0.2 client rejects a line it does not know and a second asset for a target:

```text
neura-release 1
version <version>
asset <target> neura-<version>-<target>.tar.gz <sha256 in lowercase hexadecimal> <size in bytes>
```

`neura-components.txt` lists the SDK and runtime archives, sorted:

```text
neura-components 1
version <version>
build <commit>
archive runtime <target> neura-runtime-<version>-<target>.tar.gz <sha256> <size>
archive sdk <target> neura-sdk-<version>-<target>.tar.gz <sha256> <size>
```

Both are parsed strictly: single spaces, a final newline, no unknown line, no second archive for one product and target, no size of 0 or above 1 GiB, and a file name that matches the product, version and target. [`packaging/make-release-metadata.sh`](../packaging/make-release-metadata.sh) `<directory> <version>` writes both files and `SHA256SUMS` from the three archives of each target in a directory. [`packaging/make-archive.sh`](../packaging/make-archive.sh) `--product sdk|runtime|legacy --core <prefix> [--sdk <prefix>] [--service <prefix>] [--build <commit>] <output directory> <version> <target>` makes one archive, with its manifests, from the prefixes that `cmake --install build --component <name> --prefix <prefix>` made: sorted member names, owner 0, the time `SOURCE_DATE_EPOCH` (default 0), and `gzip -n`. It needs GNU tar (`gtar` on macOS).

### From 0.2

A 0.2 client (0.2.0, and the 0.2.1 candidates through rc.3) reads `neura-release.txt`, downloads the legacy archive, places it at `versions/<version>`, switches `current`, writes a schema 1 marker, and deletes the old version at once. It makes no link but `neura`. The installation is then *unmigrated*:

- `neura --version`, `help`, `capabilities` and every command through the dispatcher work, and the tools run directly from `<root>/current/bin`. The other tools are not on `PATH`.
- `neura doctor` reports `FAIL  migration: run "neura update --migrate-install" to finish the install`; `neura update`, `--components`, `--rollback` and `--repair` refuse with `this installation was updated by a 0.2 client; run "neura update --migrate-install" first`; service start and service launches are refused; `neura uninstall` works. Where an update notice would be printed, the instruction is printed instead.

`neura update --migrate-install` takes the lock, verifies the running generation against its manifests, takes the bin directory from the marker's link, checks every tool's link as step 6 does, writes the journal, creates the lease files and the links, and writes the schema 2 marker with `components core sdk`. It moves no file of the generation, fetches nothing, and starts and registers nothing. Run again, or on an installation that the 0.3 installer made, it restores any missing link and says `already migrated`. Interrupted, it is completed by the next run.

So from 0.2 the steps are:

```sh
neura update                     # with --nightly to reach a candidate from 0.2.1-rc.3
neura update --migrate-install
```

The 0.2 updater removes the old version at once, so close editor sessions first; from 0.3 on they may stay open. The 0.3 installer run on a 0.2 root migrates the marker as part of its own change, and the 0.2 generation becomes the previous one; `--rollback` refuses to make a generation without manifests active. A 0.2 `install.sh` run on a schema 2 root stops at the marker's first line and changes nothing, and a 0.2 `neura` reports a schema 2 marker as not valid: going back to 0.2 is an uninstall and an install.

### Test-only hooks

These environment variables exist for the tests only and are not a supported interface. `NEURA_TEST_UPDATE_FAULT=<step>` makes the named step fail, and `NEURA_TEST_UPDATE_CRASH=<step>` ends the process there without cleanup, with status 70; the steps are those of [a change](#a-change-step-by-step) (`download`, `checksum`, `extract`, `verify`, `compose`, `journal`, `place`, `links-remove`, `activate-link`, `links-add`, `activate-marker`, `retire`, `prune`), and `links-add-<n>` after the n-th added link. `NEURA_TEST_SERVICE_CONTROL=<program>` names a stand-in for the service's own commands (`shutdown --terminate`, `retire`) when no control client is linked.

## Uninstall

```sh
neura uninstall [--dry-run] [--yes] [--terminate]
neura uninstall --component sdk|service [--terminate]
```

| Option | Meaning |
| --- | --- |
| `--dry-run` | Print every path, link, registration and running application the command would affect, and what would be left. Change nothing. |
| `--yes` | Remove without asking. Required when standard input is not a terminal; without it and without a terminal the command exits with status 1 and removes nothing. On a terminal, it asks `Remove neura <version> from <root>? [y/N]`. |
| `--terminate` | First stop the applications and the supervisor of this installation's service. Without it, a removal that affects running applications is refused. |
| `--component <name>` | Remove one component, by a new generation without it, as `neura update --components` does. Removing `sdk` from an SDK, or `service` from a runtime, is refused with `removing <name> would leave no product; use "neura uninstall"`. Removing `service` keeps `core`, so the SDK still runs, tests and debugs. |

A full uninstall is refused while applications of this installation's service run (unless `--terminate`), and while another process holds a lease, with the list of generations in use: other holders, such as an editor session or a build, must end first. For an `archive` install `neura uninstall` removes:

- every generation in `<root>/versions`, `<root>/current`, `<root>/leases` and `<root>/.lock`;
- each link the marker lists, only if it is a symbolic link whose target resolves inside `<root>`; otherwise it leaves the path and says so;
- `.update-lock`, `.journal`, `.staging-*`, and the other temporary files an interrupted command leaves in `<root>`;
- the update notice state file `update-check.txt`;
- the service's login unit and its state directory for this installation, and nothing of another installation;
- the marker;
- `<root>/versions`, and `<root>` itself, only when each is empty. If `<root>` is not empty, uninstall lists what remains and leaves it.

It prints each path it removes. It never follows a symbolic link out of the root while deleting, and it does not touch programs you built, caches elsewhere, or shell configuration, so a `PATH` line you added stays. If a path cannot be removed it exits with status 1 and lists what remains; the marker stays in that case so the command can be run again. An unmigrated installation is removed by the schema 1 rules. A package-manager or unmanaged install exits with status 3, like `update`.

The running executable deletes its own file. That is safe on Linux and macOS.

## Update notices and privacy

After a command finishes, `neura` may print one line to standard error:

```text
neura: version <new> is available; run "neura update"
```

It prints the line only when all of these hold:

- the install method is `archive`;
- standard error and standard input are both terminals;
- the environment variable `CI` is unset or empty;
- the environment variable `NEURA_NO_UPDATE_CHECK` is unset or empty;
- `<root>/update-check.txt` records a newer version than the running one;
- the command is not `run`, `test`, `update`, `uninstall`, `doctor`, `help`, or `--version`, and it succeeded.

On an unmigrated installation the line is the migration instruction instead, and nothing is written. `update-check.txt` holds `neura-update-check 1`, `checked <unix seconds>`, and `latest <version>`. Under the same conditions, at most once every 7 days, `neura` refreshes it after the command's own work is done. The refresh is one HTTPS GET of `https://github.com/Kayyo321/neura/releases/latest/download/neura-release.txt`, made by `curl` with a 2 second limit. The request carries `curl`'s default headers and no identifier, no install ID, and no data from your machine, and Neura collects nothing. GitHub sees the request as it sees any download. A failure is silent and still records the time of the attempt.

To turn the notice and the check off, set `NEURA_NO_UPDATE_CHECK=1` in your environment. `CI=1` has the same effect, and neither runs when the streams are not terminals. `neura doctor` shows whether notices are on in the environment it runs in.

## What the tests establish

| Behavior | Test |
| --- | --- |
| SHA-256 against the FIPS 180-4 vectors; semantic version precedence; the marker (both schemas), component manifest, both release metadata files, journal and generation names; the update-check parser | `lifecycle-unit-sha256`, `-semver`, `-release-metadata`, `-marker`, `-manifest`, `-journal`, `-generation` |
| `VERSION`, `Std.Core.Version`, and `neura --version` agree | `version-consistency` |
| The three archives of a target: names, manifests, identical core, reproducible bytes, and the metadata files | `lifecycle-archives` |
| The installer: SDK, runtime and combined installs, link conflicts, a bin directory with a space and a non-ASCII letter, a root through a symbolic link, `--force` on a whole and a damaged generation, a rebuilt release, an archive that does not match its manifests | `lifecycle-install` |
| The bootstrap script: each set of components, a 0.2 release, and refused releases | `lifecycle-bootstrap` |
| `update`: up to date, component metadata missing, every rejected archive and metadata, a fault and a crash at every step with recovery, the lock, editor sessions across updates, pruning, rollback, a file system without `flock` (a preloaded stand-in), `--nightly` through a fixture `curl`, unmanaged and package-manager installs | `lifecycle-update` |
| The state a 0.2 updater leaves, built by hand, the refusals while unmigrated, and `--migrate-install`, including an interrupted one | `lifecycle-migrate` |
| `doctor` on every state, and that the read-only commands change nothing | `lifecycle-doctor` |
| `--components`, `uninstall --component`, `--terminate`, and the full uninstall with service state, against a stand-in service | `lifecycle-components`, `lifecycle-uninstall`, `lifecycle-service` |
| Every command through `neura` and through its tool; missing, incomplete, and mixed components; an install on `PATH`, a relocated install, and two installs on `PATH` | `tools-parity`, `tools-components`, `tools-install` |

The CTest entries above run on Linux and macOS, on x86-64 and arm64. The release workflow then runs [the lifecycle checks](ReleaseProcess.md#what-the-release-workflow-checks) on the packed archives, including the update from the published `v0.2.1-rc.3` and `v0.2.0` with their own 0.2 clients, and for a release candidate runs the one-line install command against the published files, which covers a download over HTTPS and `gh attestation verify`.

These are not tested: `neura update` downloading over HTTPS (the release workflow updates from a local copy of the published files), the update notice and its network refresh (they need a terminal, so only their absence without a terminal is tested), the interactive `uninstall` prompt, the real service (`neurad`) across a change, which the service milestone tests, and any run on Windows outside WSL 2.

## Build from source

A source build is `unmanaged`. [Contributing](../CONTRIBUTING.md#set-up-a-checkout) lists the commands. `cmake -S . -B build -DNEURA_VERSION_PRERELEASE= -DNEURA_BUILD_COMMIT=<hash>` builds a release version; the default build reports `<version>-dev` and commit `unknown`. The `VERSION` file holds the `MAJOR.MINOR.PATCH` of the tool, and `stdlib/std/core/version.nra` must agree.

## External tools

| Tool | Used by | Required |
| --- | --- | --- |
| `curl` | `neura update` and the update notice, for `https://` sources | Only for those |
| `tar` | `neura update`, and unpacking the archive by hand | Yes, for `update` |
| `gh` | Verifying build provenance | Optional; required by `--require-attestation` |
| A POSIX shell, `grep` | `install.sh`, which runs the archive's `bin/neura-update` | Yes, to install |
| GNU tar, `gzip`, `find`, `sort`, `xargs`, `sha256sum` or `shasum` | `make-archive.sh` and `make-release-metadata.sh`, only when building archives | Only for packagers |

Each is run as a separate process with an argument vector. Neura passes no downloaded or user-supplied text to a shell.
