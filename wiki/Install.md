# Install, update, and uninstall

A release of Neura is one archive for each supported platform. The archive holds the `neura` executable, the standard library, the native launchers where the platform has them, and an `install.sh` that installs them for the current user. Three commands of the installed tool manage the result: `neura doctor` checks it, `neura update` replaces it with a newer release, and `neura uninstall` removes it. Installs made some other way, such as a source build or a package manager, are not managed by `neura update`.

[Tooling](Tooling) covers the compiler commands. This page covers the tool itself.

## Supported platforms

| Platform | Archive target | Notes |
| --- | --- | --- |
| Linux on x86-64, glibc | `x86_64-linux-gnu` | Includes native launchers, so `neura build --native` works. |
| Linux on arm64, glibc | `aarch64-linux-gnu` | Includes native launchers. |
| macOS on Apple silicon | `aarch64-apple-darwin` | No native launchers; `neura build --native` is unavailable. |
| macOS on Intel | `x86_64-apple-darwin` | No native launchers; `neura build --native` is unavailable. |
| Windows | `x86_64-linux-gnu` under WSL2 | Windows has no archive of its own. Install the Linux archive inside a WSL2 distribution. `doctor`, `update`, and `uninstall` report that they are not supported on Windows itself. |

The operating system versions that each archive is built and tested on:

PLATFORM-TABLE-PENDING

## Install from a release archive

Download the archive for your platform and `SHA256SUMS` from the release page, then:

```sh
sha256sum -c --ignore-missing SHA256SUMS      # on macOS: shasum -a 256 -c SHA256SUMS
gh attestation verify neura-<version>-<target>.tar.gz --repo Kayyo321/neura
tar -xzf neura-<version>-<target>.tar.gz
cd neura-<version>-<target>
./install.sh
```

`sha256sum -c` checks that the file is the one the release lists. `gh attestation verify` checks that the file was built by the repository's release workflow; it needs the [GitHub CLI](https://cli.github.com/) and network access, and a file that fails it must not be installed.

`install.sh` is a POSIX shell script. It never uses `sudo` and never edits shell startup files. It checks everything before it changes anything, and then:

1. copies `bin/`, `share/`, and `lib/` to `<root>/versions/<version>/`;
2. points `<root>/current` at that directory;
3. creates the symbolic link `<bin dir>/neura` to `<root>/current/bin/neura`;
4. writes the marker `<root>/neura-install.txt`.

It prints what it did. If `<bin dir>` is not on `PATH`, it also prints the line to add to your shell startup file, `export PATH="<bin dir>:$PATH"`, and leaves the edit to you.

| Setting | Default | Override |
| --- | --- | --- |
| Install root `<root>` | `${XDG_DATA_HOME:-$HOME/.local/share}/neura` | `NEURA_INSTALL_ROOT`, read by `install.sh` only |
| Bin directory `<bin dir>` | `$HOME/.local/bin` | `NEURA_BIN_DIR`, read by `install.sh` only |

Both must be absolute paths. After installation the tool finds its root from the location of its own executable, so the variables have no effect on a running `neura`.

`install.sh` refuses to:

- install a version whose directory already exists, unless you pass `--force`;
- overwrite `<bin dir>/neura` when it is a regular file, or a symbolic link that points outside `<root>`;
- change a root whose marker names a package manager.

A refusal changes nothing. Installing a different version beside an existing one keeps both directories and points `current` at the new one.

## Layout

```text
<root>/neura-install.txt        the marker
<root>/versions/<version>/      a normal prefix
    bin/neura
    share/neura/stdlib/...
    share/doc/neura/LICENSE
    lib/neura/launchers/<target>/...      where the platform has launchers
<root>/current                  symbolic link: versions/<version>
<bin dir>/neura                 symbolic link: <root>/current/bin/neura
```

The marker is line-oriented text that tools parse strictly:

```text
neura-install 1
method archive
link /home/user/.local/bin/neura
version 0.1.0
```

`link` is the absolute path of the symbolic link in the bin directory, and there is one `version` line for each directory in `versions/`. Any other line, a repeated version, or a missing final newline makes the marker invalid.

The running `neura` resolves the real path of its executable. If that path is `<root>/versions/<version>/bin/neura` and `<root>/neura-install.txt` is a valid marker, the install method is the marker's `method`. Otherwise the method is `unmanaged`: a source build, or a copy of an executable.

| Install method | Meaning | `neura update` and `neura uninstall` |
| --- | --- | --- |
| `archive` | Made by `install.sh` | Manage the install. |
| a package manager's name, such as `homebrew` or `deb` | The package manager owns the files | Exit with status 3 and name the package manager. |
| `unmanaged` | No marker | Exit with status 3 and explain that the tool was probably built from source or copied. |
| a marker that is not valid | Damaged | Exit with status 1 and name the problem. |

## Check an install

`neura doctor` prints one line for each check, `ok`, `warn`, or `FAIL`, two spaces, the check name, a colon, and a detail. It exits with status 0 when no check failed and 1 otherwise. It does not use the network.

| Check | Reports | When it is not `ok` |
| --- | --- | --- |
| `version` | The version, target, and commit | |
| `executable` | The real path of the running executable | `warn` if it cannot be determined |
| `install` | The install method and root | `FAIL` for an invalid marker |
| `stdlib` | The standard library directory | `FAIL` if `share/neura/stdlib/std` is not found next to the executable |
| `launchers` | The native launchers for this host | `warn`: `neura build --native` is unavailable |
| `libclang` | The libclang library that C imports load | `warn`: C imports are unavailable; on Debian and Ubuntu install `libclang1-19 libclang-common-19-dev` |
| `clang-headers` | libclang's builtin header directory | `warn`: C imports are unavailable |
| `c-compiler` | `$CC` or `cc` on `PATH` | `warn`: the libraries of C imports cannot be linked |
| `gpu` | Whether GPU graph execution is compiled in and the Vulkan loader opens | `warn` if it is compiled in and the loader does not open |
| `curl`, `tar` | The tools on `PATH` | `warn`: `neura update` needs them |
| `gh` | The GitHub CLI on `PATH` | `warn`: `neura update` cannot verify build provenance |
| `bin-link`, `path` | For archive installs, that the link exists, points into the root, and its directory is on `PATH` | `warn` |
| `update-notices` | Whether [update notices](#update-notices-and-privacy) are on in this environment | |

## Update

```sh
neura update [--check] [--source <https-url-or-directory>] [--require-attestation]
```

`neura update --check` reports whether a newer release exists and changes nothing. `neura update` installs it. Neither asks a question.

| Option | Meaning |
| --- | --- |
| `--check` | Report only. |
| `--source <location>` | Where `neura-release.txt` and the archives are. An `https://` URL, or a local directory. The default is `https://github.com/Kayyo321/neura/releases/latest/download`. A URL with any other scheme, including `http://`, is rejected. |
| `--require-attestation` | Fail unless build provenance is verified. Without `gh` on `PATH` this fails before anything is downloaded. With a local directory source, it also runs the check that is otherwise skipped. |

| Exit status | Meaning |
| --- | --- |
| 0 | The update succeeded, or the installed version is already up to date. |
| 1 | The update failed. Either nothing was changed or the change was rolled back; the message says which. Also an invalid marker, a refused source, and a held lock. |
| 3 | This install is not managed by `neura update`: it is `unmanaged`, or a package manager owns it. Exit 3 is reported before any network access. |
| 4 | Only from `--check`: a newer version is available. |

### What an update does

1. Determines the install method. Anything but `archive` ends with status 3.
2. Takes the lock `<root>/.update-lock`, created with `O_CREAT|O_EXCL`. If the file exists, the update stops with status 1 and says so. A lock more than an hour old gets a message telling you to delete it; `neura` never removes a lock it did not create. It also removes `.staging-*` directories in `<root>` that are more than a day old.
3. Fetches `neura-release.txt` from the source. For an `https://` source it runs `curl` as a child process with an argument vector, never a shell, restricted to HTTPS (also after redirects) and TLS 1.2 or newer, with a 30 second limit, writing to a temporary file in `<root>`. `curl` reads no `.curlrc`.
4. Parses the metadata strictly (see [the format](#release-metadata)). It rejects unknown line kinds, a repeated target, malformed digests, file names that contain `/` or start with `.` or `-`, and sizes above 1 GiB.
5. Compares versions by semantic version precedence ([semver.org section 11](https://semver.org/#spec-item-11)): the numeric fields compare as numbers, a prerelease is lower than the release it precedes, and the dot-separated prerelease identifiers compare as the specification says. If the release is not newer, it prints `neura <version> is up to date` and exits 0. With `--check` and a newer release it prints both versions and exits 4.
6. Picks the asset for the host target. A release without one is an error.
7. Downloads the asset into a new directory `<root>/.staging-<pid>-<random>/` with mode 0700, then checks its size and its SHA-256 against the metadata. The SHA-256 is computed by Neura itself.
8. Verifies build provenance as [described below](#what-is-verified).
9. Lists the archive with `tar` and rejects it if any member is absolute, has a `..` component, lies outside the one top-level directory `neura-<version>-<target>/`, or is anything but a regular file or a directory. Then extracts it with `tar -xzf ... --no-same-owner` and checks the extracted tree again with `lstat`.
10. Checks the staged tree: `bin/neura` is an executable regular file, `share/neura/stdlib/std` exists, and `bin/neura --version` prints a first line that starts with `neura <new version> `. This runs the new executable, after the checks above.
11. Activates it, as the next section describes, then removes the old version directories and the staging directory. A failure to remove old files is a warning; the status stays 0.

The staging directory and the lock are removed on every exit path.

### What is verified

| Check | `https://` source | Local directory source |
| --- | --- | --- |
| Size and SHA-256 of the archive against `neura-release.txt` | Always. The metadata arrives over HTTPS. | Always. The metadata is read from the directory. |
| Build provenance: `gh attestation verify <file> --repo Kayyo321/neura` | When `gh` is on `PATH`. A failure aborts the update. | Only with `--require-attestation`. |
| Without `gh` | A line says that build provenance was not verified and that integrity rests on the SHA-256 from the metadata fetched over HTTPS. With `--require-attestation` the update stops with status 1. | The same line, or with `--require-attestation` status 1. |

The update prints which verifications ran. A SHA-256 taken from the same place as the archive detects corruption and tampering in transit. It does not detect a replaced release, because whoever can replace the archive can replace the digest. Build provenance covers that case: it proves the file was produced by the repository's workflow. Install `gh` and run `neura doctor` to see whether it is available.

### Activation and rollback

The new tree is renamed to `<root>/versions/<new>`, and the update fails if that directory exists. A new marker, listing only the new version, is written to a temporary file in `<root>`. A temporary symbolic link is created and renamed over `<root>/current`, which is atomic. Last, the new marker is renamed over `neura-install.txt`.

If a step fails after `versions/<new>` exists and before the marker is in place, the update undoes what it did: it points `current` back at the old version with the same atomic rename, removes `versions/<new>`, and leaves the old marker. It then reports `update failed and was rolled back; neura <old> is still installed` and exits 1. If the rollback itself fails, the message lists what remains and says so.

A failure before activation leaves the installation untouched.

### Release metadata

`neura-release.txt` has this form, with single spaces between fields and a newline after every line:

```text
neura-release 1
version <version>
asset <target> <file name> <sha256 in lowercase hexadecimal> <size in bytes>
```

There is one `asset` line for each target. `packaging/make-release-metadata.sh <directory> <version>` writes this file and `SHA256SUMS` from the archives in a directory, and `packaging/make-archive.sh <prefix> <output directory> <version> <target>` makes a reproducible archive: sorted member names, owner 0, the time `SOURCE_DATE_EPOCH` (default 0), and `gzip -n`. It needs GNU tar (`gtar` on macOS).

### Test-only fault hook

The environment variable `NEURA_TEST_UPDATE_FAULT=<step>` makes a step of `neura update` fail on purpose: `download`, `checksum`, `extract`, `verify`, `activate-link`, or `activate-marker`. The `lifecycle-update` test uses it to check that each failure leaves the installation as it was. It is not a supported interface for users.

## Uninstall

```sh
neura uninstall [--dry-run] [--yes]
```

| Option | Meaning |
| --- | --- |
| `--dry-run` | Print what would be removed and what would be left. Change nothing. |
| `--yes` | Remove without asking. Required when standard input is not a terminal; without it and without a terminal the command exits with status 1 and removes nothing. On a terminal, it asks `Remove neura <version> from <root>? [y/N]`. |

For an `archive` install `neura uninstall` removes:

- each `<root>/versions/<version>` that the marker lists;
- `<root>/current`;
- the bin link the marker records, only if it is a symbolic link whose target resolves inside `<root>`; otherwise it leaves the path and says so;
- `.update-lock`, `.staging-*`, and the other temporary files an interrupted update leaves in `<root>`;
- the update notice state file `update-check.txt`;
- the marker;
- `<root>/versions`, and `<root>` itself, only when each is empty. If `<root>` is not empty, uninstall lists what remains and leaves it.

It prints each path it removes. It never follows a symbolic link out of the root while deleting, and it does not touch programs you built, caches elsewhere, or shell configuration, so a `PATH` line you added stays. If a path cannot be removed it exits with status 1 and lists what remains; the marker stays in that case so the command can be run again. A package-manager or unmanaged install exits with status 3, like `update`.

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

`update-check.txt` holds `neura-update-check 1`, `checked <unix seconds>`, and `latest <version>`. Under the same conditions, at most once every 7 days, `neura` refreshes it after the command's own work is done. The refresh is one HTTPS GET of `https://github.com/Kayyo321/neura/releases/latest/download/neura-release.txt`, made by `curl` with a 2 second limit. The request carries `curl`'s default headers and no identifier, no install ID, and no data from your machine, and Neura collects nothing. GitHub sees the request as it sees any download. A failure is silent and still records the time of the attempt.

To turn the notice and the check off, set `NEURA_NO_UPDATE_CHECK=1` in your environment. `CI=1` has the same effect, and neither runs when the streams are not terminals. `neura doctor` shows whether notices are on in the environment it runs in.

## What the tests establish

| Behavior | Test |
| --- | --- |
| SHA-256 against the FIPS 180-4 vectors; semantic version precedence; the marker, release metadata, and update-check parsers | `lifecycle-unit-sha256`, `lifecycle-unit-semver`, `lifecycle-unit-release-metadata`, `lifecycle-unit-marker` |
| `VERSION`, `Std.Core.Version`, and `neura --version` agree | `version-consistency` |
| Archive and metadata scripts, `install.sh` layout and refusals, the installed tool, `doctor`, no output or state file without terminals | `lifecycle-install` |
| `update` and `update --check` from local directories: success, up to date, each rejected archive and metadata, each fault step with rollback, the lock, unmanaged and package-manager installs, rejected sources | `lifecycle-update` |
| `uninstall`: `--dry-run`, the refusal without `--yes`, what is removed and what is left | `lifecycle-uninstall` |

These are not tested: an update downloaded over HTTPS, `gh attestation verify` itself, the update notice and its network refresh (they need a terminal and the network, so only their absence without a terminal is tested), the interactive `uninstall` prompt, and any run on macOS, on arm64, or on Windows. The scripts and commands were exercised on Linux x86-64 only.

## Build from source

A source build is `unmanaged`. [Contributing](https://github.com/Kayyo321/neura/blob/main/CONTRIBUTING.md#set-up-a-checkout) lists the commands. `cmake -S . -B build -DNEURA_VERSION_PRERELEASE= -DNEURA_BUILD_COMMIT=<hash>` builds a release version; the default build reports `<version>-dev` and commit `unknown`. The `VERSION` file holds the `MAJOR.MINOR.PATCH` of the tool, and `stdlib/std/core/version.nra` must agree.

## External tools

| Tool | Used by | Required |
| --- | --- | --- |
| `curl` | `neura update` and the update notice, for `https://` sources | Only for those |
| `tar` | `neura update`, and unpacking the archive by hand | Yes, for `update` |
| `gh` | Verifying build provenance | Optional; required by `--require-attestation` |
| A POSIX shell, `cp`, `mv`, `ln`, `sed`, `grep` | `install.sh` | Yes, to install |
| GNU tar, `gzip`, `find`, `sort` | `make-archive.sh`, only when building archives | Only for packagers |

Each is run as a separate process with an argument vector. Neura passes no downloaded or user-supplied text to a shell.
