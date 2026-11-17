# Release process

This page is the procedure for publishing a Neura release. [GOVERNANCE.md](../GOVERNANCE.md#releases) states who may release, the [release checklist](Release.md) maps each feature to its tests, and [Install and update](Install.md) describes what a user receives.

## Versions and tags

`VERSION` holds the version, `MAJOR.MINOR.PATCH`. A build made outside the release workflow reports `<VERSION>-dev`.

| Tag | Builds | Published as |
| --- | --- | --- |
| `v<VERSION>-rc.<N>` | `<VERSION>-rc.<N>` | A prerelease, at once. `neura update` ignores prereleases. |
| `v<VERSION>` | `<VERSION>` | A draft, which a maintainer publishes after the [manual verification](#manual-verification). |

A `v*` tag cannot be moved or deleted ([repository settings](RepositorySettings.md)), and a published release cannot be changed. A candidate that fails is left as it is, and the fix goes into the next candidate.

## What the release workflow checks

[`release.yml`](../.github/workflows/release.yml) runs for every `v*` tag. Each job must succeed before the next starts.

| Job | What it does |
| --- | --- |
| Check the tag | The tag has one of the two forms above, names the version in `VERSION`, and points at a commit on `main`. A final tag also needs a dated `## [<VERSION>]` section in `CHANGELOG.md`. |
| Build and test, once for each target | Builds from the tagged commit with `NEURA_PORTABLE_RELEASE=ON`. Fails on a compiler warning in Neura's sources, on a failed, skipped, or missing test, on a file in `bin/`, a library in `lib/neura` or a launcher that needs a library other than the C library and the tools' own `libneura-core` and `libneura-sdk`, or a glibc newer than 2.39, or when `neura --version` does not report the tag's version, target, and commit. The dependency checks cover every executable of the three archives, the service component's included. Installs each install component (`core`, `sdk`, `service`) into its own prefix and packs the three archives of the target ([Install](Install.md#install-from-a-release-archive)): `neura-sdk-<version>-<target>.tar.gz`, `neura-runtime-<version>-<target>.tar.gz`, and the legacy `neura-<version>-<target>.tar.gz`, each with its component manifests, and reports the stripped size of every executable and library. |
| Qualify the archives, once for each target | On a new runner that has built nothing, downloads the archives of the previous release and the published `v0.2.1-rc.3` and `v0.2.0` archives (checked against their `SHA256SUMS`), and runs [`packaging/qualify.sh`](../packaging/qualify.sh) on the three archives with an empty environment and new home directories. |
| Publish | Runs in the protected `release` environment after a maintainer approves it. Writes `SHA256SUMS`, `neura-release.txt` (schema 1, the legacy archives, for 0.2 clients) and `neura-components.txt` (the SDK and runtime archives), attests the build provenance of each file, creates the release, then downloads the release's files again and checks them against the checksums, the built files, and the attestations. For a release candidate it then runs the documented [one-line install command](Install.md#install-with-one-command) against the published candidate and checks that the installed `neura` reports the tagged version and commit. |

The qualification script runs fourteen checks and prints `PASS`, `FAIL`, or `NOT RUN` for each:

1. Each archive holds one directory: the legacy archive holds `neura`, every tool it starts and their two libraries ([tools](lang/Tooling.md#tools-and-the-neura-dispatcher)), the installer, the license, the standard library, the `core` and `sdk` manifests, and the version; it is exactly the SDK archive's tree; and every `core` file is the same bytes in the SDK and runtime archives.
2. The SDK archive's `install.sh` installs into a new home directory, with a link for every tool and a schema 2 marker.
3. The installed `neura --version` reports the release's version and target.
4. `neura doctor` reports no failure.
5. Hello World builds and runs, through `neura` and through `neurac` and `neura-run`.
6. `neura test` passes the example test module.
7. On Linux, `neura build --native` produces an executable that runs with an empty environment. On macOS, which has no native launcher, the command is refused.
8. `neura update --check` finds the release up to date.
9. An installation of the previous published release updates to this one. From a 0.2 release this is [the bridge](Install.md#from-02), as in check 13; from a 0.3 release the SDK archive is installed, updated with the 0.3 client, rolled back, updated again and uninstalled. The check is reported as not run when the previous release has no archive for the target, and in a trial started by hand, which has no previous release.
10. `neura uninstall --yes` leaves nothing behind, and installing again works.
11. The runtime archive holds no SDK tool, `libneura-sdk` or standard library and holds the service `neurad`, installs alone with a `neurad` link, runs Hello World's image through `neura run` and `neura-run`, names Neura-sdk for `neura build`, and its `neura doctor` reports no failure. On Linux it also runs the image through the service (`neura run --runtime=required`, with `NEURA_GPU=off`) and shuts the service down.
12. The SDK and runtime archives install together as one generation with every component and a `neurad` link, and `neura doctor` reports no failure.
13. The 0.2 bridge, from `v0.2.1-rc.3` and from `v0.2.0`: each is installed with its own `install.sh`, its own `neura update` installs this release, `neura build`, `run` and `test` work with no `leases/` and `neurac` not on `PATH`, `doctor` fails on `migration`, and after `neura update --migrate-install` every tool is linked, the marker is schema 2, `doctor` passes, `neurac` runs from `PATH`, and a second migration says `already migrated`. A tag requires both; a trial reports a bridge as not run when its `-dev` version is not newer than the 0.2 release.
14. The size report, and the budget of [the 0.3.0 baseline](design/Release030.md): the SDK and legacy archives are at most 25% larger than rc.3's archive of the target, and the runtime archive is smaller than the SDK archive.

Started by hand (`gh workflow run release.yml --ref <branch>`), the workflow builds and qualifies that branch as a `-dev` version and publishes nothing. Use this to try a change to the workflow before spending a tag on it.

## Before the first candidate

1. Every pull request for the release is merged and Tier 1 passed on `main`.
2. [Tier 2](ContinuousIntegration.md#tier-2-platform-and-sanitizer-runs) ran on `main`; its reports are read and each failure is fixed or recorded in `CHANGELOG.md` as a known issue.
3. `python3 scripts/check-repository-settings.py` reports no problems.
4. `CHANGELOG.md` describes the release under **Unreleased**.

## Release candidates

```sh
git switch main && git pull
git tag v0.1.0-rc.1 && git push origin v0.1.0-rc.1
```

Approve the **Publish** job when the build and qualification jobs have passed. Then tag the next candidate from the then-current `main`, so that check 9 exercises an update between two published builds. A release needs at least two candidates for that reason: the second candidate's check 9 is the evidence that `neura update` works against real published files. The first 0.3 candidate's check 9 starts from `v0.2.1-rc.3`, the newest published release, and is the bridge; every candidate and the final release also run the bridge from `v0.2.1-rc.3` and `v0.2.0` in check 13.

## Manual verification

The workflow covers what a hosted Linux runner can show. A maintainer checks the rest on real machines before the final release and records the result in the release's pull request or issue.

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
| In WSL 2: run `neura.exe` or `neura` from PowerShell | Not found. Neura runs inside the distribution only. |

These steps are the ones no test covers: a download over HTTPS, `gh attestation verify` against the published attestation, the update notice in a terminal, and the interactive uninstall prompt.

## Go or no-go

Publish the final version only when all of these hold. Otherwise fix the problem and tag another candidate.

- The latest candidate's workflow run succeeded, and its check 9 ran.
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

   Then run the [one-line install command](Install.md#install-with-one-command) in a new home directory and check `neura --version`. The workflow cannot do this for a final version, which is a draft until this step.

   An installation of the last candidate must then report the new version with `neura update --check` (exit status 4) and install it with `neura update`.
