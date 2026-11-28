<!-- Generated from docs/ReleaseProcess.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Release process

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page is the procedure for publishing a Neura release. [GOVERNANCE.md](https://github.com/Kayyo321/neura/blob/main/GOVERNANCE.md#releases) states who may release, the [release checklist](Release) maps each feature to its tests, and [Install and update](Install) describes what a user receives.

## Versions and tags

`VERSION` holds the version, `MAJOR.MINOR.PATCH`. A build made outside the release workflow reports `<VERSION>-dev`.

| Tag | Builds | Published as |
| --- | --- | --- |
| `v<VERSION>-rc.<N>` | `<VERSION>-rc.<N>` | A prerelease, at once. `neura update` ignores prereleases. |
| `v<VERSION>` | `<VERSION>` | A draft, which a maintainer publishes after the [manual verification](#manual-verification). |

A `v*` tag cannot be moved or deleted ([repository settings](RepositorySettings)), and a published release cannot be changed. A candidate that fails is left as it is, and the fix goes into the next candidate.

## What the release workflow checks

[`release.yml`](https://github.com/Kayyo321/neura/blob/main/.github/workflows/release.yml) runs for every `v*` tag. Each job must succeed before the next starts.

| Job | What it does |
| --- | --- |
| Check the tag | The tag has one of the two forms above, names the version in `VERSION`, and points at a commit on `main`. A final tag also needs a dated `## [<VERSION>]` section in `CHANGELOG.md`. |
| Build and test (Linux and macOS) | Builds the pinned server components with [`packaging/build-server-components.sh`](https://github.com/Kayyo321/neura/blob/main/packaging/build-server-components.sh), configures with `NEURA_PORTABLE_RELEASE=ON`, TLS, HTTP/2 and PostgreSQL, and checks warnings, tests, runtime dependencies, and the version, target and commit. It installs `core`, `sdk` and `service` into separate prefixes, packs the [three archives](Install#install-from-a-release-archive), and reports stripped tool and library sizes. |
| Build and test (Windows x86-64) | Uses `windows-release` on `windows-2022`. Checks Neura's compiler warnings, CTest results, DLL exports and dependencies, static CRT linkage and the version, target and commit. Packages `neura-sdk-<version>-x86_64-windows-msvc.tar.gz` and `neura-runtime-<version>-x86_64-windows-msvc.tar.gz`, with component manifests and PowerShell and Command Prompt installers. |
| Qualify the archives (Linux and macOS) | A fresh runner verifies earlier archives against `SHA256SUMS` and runs [`packaging/qualify.sh`](https://github.com/Kayyo321/neura/blob/main/packaging/qualify.sh) with an empty environment and new home directories. |
| Qualify the archives (Windows x86-64) | A fresh `windows-2022` runner runs [`packaging/qualify.py`](https://github.com/Kayyo321/neura/blob/main/packaging/qualify.py) on the two Windows archives. It checks the native installer, junction layout, installed SDK and runtime, and Windows archive sizes against the corresponding x86-64 Linux archives from the same build. |
| Cross-host image identity | Compares SHA-256 across the x86-64 Linux and Windows archives for three example programs built with `--no-source-map`. |
| Publish | Runs in the protected `release` environment after a maintainer approves it. Writes `SHA256SUMS`, `neura-release.txt` (schema 1, legacy archives for 0.2 clients) and `neura-components.txt` (SDK and runtime archives, including Windows). It attests the archives, `install.sh`, and `install.ps1`, creates the release, and checks its downloads. For a candidate it runs the [one-line shell install](Install#install-with-one-command) on Linux and checks the installed version and commit. |
| Install the published candidate (Windows) | After candidate publication, runs the [PowerShell installer](Install#windows-installation) on `windows-2022` with `-NoPathUpdate` and checks the attestation, exact version and commit, and `neura doctor`. |

The qualification script runs fifteen checks and prints `PASS`, `FAIL`, or `NOT RUN` for each:

1. Each archive holds one directory: the legacy archive holds `neura`, every tool it starts and their two libraries ([tools](Tooling#tools-and-the-neura-dispatcher)), the installer, the license and the licences of OpenSSL, nghttp2 and PostgreSQL, the standard library, the `core` and `sdk` manifests, and the version; it is exactly the SDK archive's tree; and every `core` file is the same bytes in the SDK and runtime archives.
2. The SDK archive's `install.sh` installs into a new home directory, with a link for every tool and a schema 2 marker.
3. The installed `neura --version` reports the release's version and target.
4. `neura doctor` reports no failure.
5. Hello World builds and runs, through `neura` and through `neurac` and `neura-run`.
6. `neura test` passes the example test module.
7. On Linux, `neura build --native` produces an executable that runs with an empty environment. On macOS, which has no native launcher, the command is refused.
8. `neura update --check` finds the release up to date.
9. An installation of the previous published release updates to this one. From a 0.2 release this is [the bridge](Install#from-02), as in check 13; from a 0.3 release the SDK archive is installed, updated with the 0.3 client, rolled back, updated again and uninstalled. The check is reported as not run when the previous release has no archive for the target. A trial started by hand builds a `-dev` version, which sorts before the candidates of its own version, so it updates from the most recent release of an earlier version (for 0.3.0, `v0.2.1-rc.3`, through the bridge).
10. `neura uninstall --yes` leaves nothing behind, and installing again works.
11. The runtime archive holds no SDK tool, `libneura-sdk` or standard library and holds the service `neurad`, installs alone with a `neurad` link, runs Hello World's image through `neura run` and `neura-run`, names Neura-sdk for `neura build`, and its `neura doctor` reports no failure. On Linux it also runs the image through the service (`neura run --runtime=required`, with `NEURA_GPU=off`) and shuts the service down.
12. The SDK and runtime archives install together as one generation with every component and a `neurad` link, and `neura doctor` reports no failure.
13. The 0.2 bridge, from `v0.2.1-rc.3` and from `v0.2.0`: each is installed with its own `install.sh`, its own `neura update` installs this release, `neura build`, `run` and `test` work with no `leases/` and `neurac` not on `PATH`, `doctor` fails on `migration`, and after `neura update --migrate-install` every tool is linked, the marker is schema 2, `doctor` passes, `neurac` runs from `PATH`, and a second migration says `already migrated`. A tag requires both; a trial reports a bridge as not run when its `-dev` version is not newer than the 0.2 release.
14. The size report, and the budget of [the 0.3.0 baseline](https://github.com/Kayyo321/neura/blob/main/docs/design/Release030.md): the SDK and legacy archives are at most a per-target percentage larger than rc.3's archive of the target (165% on x86-64 Linux, 200% on arm64 Linux, 295% on arm64 macOS, 265% on x86-64 macOS; the statically linked TLS, HTTP/2 and PostgreSQL libraries are most of the growth), and the runtime archive is smaller than the SDK archive. A target without a budget fails the check.
15. The installed `neura-server-host` serves the example `examples/server/hello-server.nra` over HTTPS with HTTP/2 chosen by ALPN and over h2c with prior knowledge, checked with `curl`, and refuses a `postgresql://` pool on a closed port for its connection rather than for a missing provider.

Windows uses `install.ps1` or `install.cmd` and the `current` junction for the lifecycle checks. On Windows, check 7 builds Hello World with `neura build --native`, copies the PE executable into a new directory and runs it there with only the System32 directories on `PATH`; it must print `Hello, World!` and nothing else. (0.3.0 had no Windows launcher, and its check 7 verified the refusal instead.) Check 9 is `NOT RUN` when there is no previous Windows archive. Check 13 is `NOT RUN` because there was no Windows 0.2 client. Check 14 caps each Windows archive at 125% of the same build's x86-64 Linux archive for the same product. The TLS, HTTP/2 and PostgreSQL probes in check 15 are `NOT RUN` on Windows because 0.3.0 defers those components.

Started by hand (`gh workflow run release.yml --ref <branch>`), the workflow builds and qualifies that branch as a `-dev` version and publishes nothing. Use this to try a change to the workflow before spending a tag on it.

## Before the first candidate

1. Every pull request for the release is merged and Tier 1 passed on `main`.
2. [Tier 2](ContinuousIntegration#tier-2-platform-and-sanitizer-runs) ran on `main`; its reports are read and each failure is fixed or recorded in `CHANGELOG.md` as a known issue.
3. `python3 scripts/check-repository-settings.py` reports no problems.
4. `CHANGELOG.md` describes the release under **Unreleased**.

## Release candidates

```sh
git switch main && git pull
git tag v0.1.0-rc.1 && git push origin v0.1.0-rc.1
```

Approve the **Publish** job when the build and qualification jobs have passed. Then tag the next candidate from the then-current `main`, so that check 9 exercises an update between two published builds. A release needs at least two candidates to establish that `neura update` works against published files. For 0.3.0 the owner made an exception on 2026-11-18. The final run's check 9 from the published candidate supplies that evidence. The first 0.3 candidate's POSIX check 9 starts from `v0.2.1-rc.3`, and check 13 covers the bridge from `v0.2.1-rc.3` and `v0.2.0`. See the [0.3.0 candidate decision](https://github.com/Kayyo321/neura/blob/main/docs/design/Release030.md#candidates).

## Manual verification

A maintainer checks the candidate on real machines before the final release and records the result in the release's pull request or issue.

On Windows with WSL 2 (Ubuntu 24.04), on a Mac, and on any Linux machine that is not a GitHub runner, install the candidate with the one-line command from its release notes, or by hand (on a Mac, use the `apple-darwin` archive for its processor and `shasum -a 256 --check` in place of `sha256sum`):

```sh
curl -LO https://github.com/Kayyo321/neura/releases/download/v0.3.0-rc.1/neura-sdk-0.3.0-rc.1-x86_64-linux-gnu.tar.gz
curl -LO https://github.com/Kayyo321/neura/releases/download/v0.3.0-rc.1/SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS
gh attestation verify neura-sdk-0.3.0-rc.1-x86_64-linux-gnu.tar.gz --repo Kayyo321/neura
tar -xzf neura-sdk-0.3.0-rc.1-x86_64-linux-gnu.tar.gz
(cd neura-sdk-0.3.0-rc.1-x86_64-linux-gnu && sh install.sh)
neura --version
neura doctor
```

Then, with the repository's `examples` directory at hand:

| Step | Expected |
| --- | --- |
| `neura build examples/hello-world/hello-world.nra -o hello.nvr && neura run hello.nvr` | `Hello, World!` |
| `neura build --native examples/hello-world/hello-world.nra -o hello && ./hello` | `Hello, World!` on Linux and WSL 2. On a Mac, an error that says macOS has no native launcher. |
| `neura update --check` | Exit status 0 and "is up to date": the default source lists final releases only. |
| Run `neura check examples/hello-world/hello-world.nra` twice in a terminal | No update notice for a candidate; no delay of more than two seconds. |
| `neura uninstall`, answer `y` | The prompt names the version and the directory; afterwards `~/.local/share/neura` and every tool's link in `~/.local/bin` are gone. |
| On a machine with a 0.2 installation: `neura update --nightly`, then `neura update --migrate-install` | The first installs the candidate with the 0.2 client and leaves only `neura` on `PATH`; the second links every tool, and `neura doctor` reports no failure. |
| The one-line command with `--components runtime`, then `--components sdk,service` in another home | Neura-runtime alone, then both products as one generation; login registration of the service is checked with the real service manager (`systemd --user`, `launchd`), or, where a platform has none, on-demand start. |

Manual verification also covers the update notice in a terminal and the interactive uninstall prompt. Verify the candidate's published files and attestation before installing.

### Native Windows

On x86-64 Windows 10 (version 2004 or later) and Windows 11, use a fresh install root on NTFS. Download the candidate's Windows archive and `SHA256SUMS`. Compare `Get-FileHash -Algorithm SHA256 <archive>` with the published digest and run `gh attestation verify <archive> --repo Kayyo321/neura`. Install using the [PowerShell command for that candidate](Install#windows-installation), then open a new PowerShell and run:

```powershell
neura --version
neura doctor
neura build examples/hello-world/hello-world.nra -o hello.nvr
neura run hello.nvr
neura build --native examples/hello-world/hello-world.nra -o hello.exe
neura update --check
```

Check that `--version` names the candidate, `x86_64-windows-msvc` and its commit, `doctor` has no failures, the image prints `Hello, World!`, and `--native` reports that Windows has no native launcher in 0.3.0. Install the runtime with `-Components runtime` and verify `neura run --runtime=required hello.nvr`. After interactive `neura uninstall`, check in a new PowerShell that the installation and its user `Path` entry are gone. Record SmartScreen prompts and any Smart App Control block with the Windows version and candidate. A policy block leaves the manual check incomplete.

## Go or no-go

Publish the final version only when all of these hold. Otherwise fix the problem and tag another candidate.

- The latest candidate's workflow run succeeded. Check 9 ran on targets with a previous archive, and check 13 passed on Linux and macOS. For the first native Windows candidate, accept `NOT RUN` for check 9 when no previous Windows archive exists and for check 13 because there was no Windows 0.2 client. If the final Windows release has a previous Windows candidate, its qualification must exercise that update in check 9.
- The manual verification of that candidate found no failure.
- No open issue is labeled as blocking the release.
- `main` has no change since that candidate other than `CHANGELOG.md`'s dated section.

## The final release

1. In a pull request, rename **Unreleased** in `CHANGELOG.md` to `## [<VERSION>] - <date>` and add an empty **Unreleased** above it.
2. Tag the merged commit `v<VERSION>` and push the tag. Approve the **Publish** job.
3. The workflow leaves a draft release. Compare its files' checksums with the workflow's log, then publish the draft on the release page.
4. Verify the published release from a clean machine:

   ```sh
   gh release download v<VERSION> --repo Kayyo321/neura --dir check
   (cd check && sha256sum --check SHA256SUMS)
   gh attestation verify check/neura-sdk-<VERSION>-x86_64-linux-gnu.tar.gz --repo Kayyo321/neura
   ```

   Then run the [one-line install command](Install#install-with-one-command) in a new home directory and check `neura --version`. The workflow cannot do this for a final version, which is a draft until this step.

   An installation of the last candidate must then report the new version with `neura update --check` (exit status 4) and install it with `neura update`.
