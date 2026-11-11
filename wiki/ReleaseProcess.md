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
| Build and test, once for each target | Builds from the tagged commit with `NEURA_PORTABLE_RELEASE=ON`. Fails on a compiler warning in Neura's sources, on a failed, skipped, or missing test, on a file that needs a library other than the C library or a glibc newer than 2.39, or when `neura --version` does not report the tag's version, target, and commit. Packs the archive. |
| Qualify the archive, once for each target | On a new runner that has built nothing, runs [`packaging/qualify.sh`](https://github.com/Kayyo321/neura/blob/main/packaging/qualify.sh) on the packed archive with an empty environment and a new home directory. |
| Publish | Runs in the protected `release` environment after a maintainer approves it. Writes `SHA256SUMS` and `neura-release.txt`, attests the build provenance of each file, creates the release, then downloads the release's files again and checks them against the checksums, the built files, and the attestations. For a release candidate it then runs the documented [one-line install command](Install#install-with-one-command) against the published candidate and checks that the installed `neura` reports the tagged version and commit. |

The qualification script runs ten checks and prints `PASS`, `FAIL`, or `NOT RUN` for each:

1. The archive holds one directory with the tool, the installer, the license, the standard library, and the version.
2. `install.sh` installs into a new home directory.
3. The installed `neura --version` reports the release's version and target.
4. `neura doctor` reports no failure.
5. Hello World builds and runs.
6. `neura test` passes the example test module.
7. `neura build --native` produces an executable that runs with an empty environment.
8. `neura update --check` finds the release up to date.
9. An installation of the previous published release updates to this one with `neura update`, and then builds and runs Hello World. The first release has no previous release, and the check is reported as not run.
10. `neura uninstall --yes` leaves nothing behind, and installing again works.

One test is left out on arm64: `neura-libclang-load`, which belongs to the `neura-libclang` submodule and expects `char` to be signed.

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

Approve the **Publish** job when the build and qualification jobs have passed. Then tag the next candidate from the then-current `main`, so that check 9 exercises an update between two published builds. A release needs at least two candidates for that reason: the second candidate's check 9 is the evidence that `neura update` works against real published files.

## Manual verification

The workflow covers what a hosted Linux runner can show. A maintainer checks the rest on real machines before the final release and records the result in the release's pull request or issue.

On Windows with WSL 2 (Ubuntu 24.04), and on any Linux machine that is not a GitHub runner:

```sh
curl -LO https://github.com/Kayyo321/neura/releases/download/v0.1.0-rc.2/neura-0.1.0-rc.2-x86_64-linux-gnu.tar.gz
curl -LO https://github.com/Kayyo321/neura/releases/download/v0.1.0-rc.2/SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS
gh attestation verify neura-0.1.0-rc.2-x86_64-linux-gnu.tar.gz --repo Kayyo321/neura
tar -xzf neura-0.1.0-rc.2-x86_64-linux-gnu.tar.gz
(cd neura-0.1.0-rc.2-x86_64-linux-gnu && sh install.sh)
neura --version
neura doctor
```

Then, with the repository's `examples` directory at hand:

| Step | Expected |
| --- | --- |
| `neura build examples/hello-world/hello-world.nra -o hello.nvr && neura run hello.nvr` | `Hello, World!` |
| `neura build --native examples/hello-world/hello-world.nra -o hello && ./hello` | `Hello, World!` |
| `neura update --check` | Exit status 0 and "is up to date": the default source lists final releases only. |
| Run `neura check examples/hello-world/hello-world.nra` twice in a terminal | No update notice for a candidate; no delay of more than two seconds. |
| `neura uninstall`, answer `y` | The prompt names the version and the directory; afterwards `~/.local/share/neura` and `~/.local/bin/neura` are gone. |
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
   gh attestation verify check/neura-<VERSION>-x86_64-linux-gnu.tar.gz --repo Kayyo321/neura
   ```

   Then run the [one-line install command](Install#install-with-one-command) in a new home directory and check `neura --version`. The workflow cannot do this for a final version, which is a draft until this step.

   An installation of the last candidate must then report the new version with `neura update --check` (exit status 4) and install it with `neura update`.
