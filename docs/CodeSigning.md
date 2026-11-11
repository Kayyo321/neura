# Code signing policy

This page states how Neura's release files are produced, what a user can verify about them, who approves a release, and what the installed program sends over the network.

## How release files are produced

Every file on a [release page](https://github.com/Kayyo321/neura/releases) is built by the [release workflow](../.github/workflows/release.yml) on GitHub-hosted runners, from the commit a `v*` tag names. No release file is built on a maintainer's machine. The workflow builds and tests each target, packs the archive, and runs the [lifecycle checks](ReleaseProcess.md#what-the-release-workflow-checks) on the packed archive on a second runner. The job that publishes runs only after those pass and after a maintainer approves it.

## What a user can verify

| Evidence | What it establishes | How to check it |
| --- | --- | --- |
| `SHA256SUMS` | The archive is the file the release workflow uploaded. | `sha256sum --check --ignore-missing SHA256SUMS` |
| Build provenance attestation | The archive was built by this repository's release workflow from the tagged commit. The attestation is signed through Sigstore with the workflow's identity, not with a key a maintainer holds. | `gh attestation verify <archive> --repo Kayyo321/neura` |

`neura update` checks the size and SHA-256 of what it downloads against the release's `neura-release.txt`, and checks the attestation when the GitHub CLI is installed. [Install and update](Install.md) states exactly what is verified in each case.

## What is not signed

Release files carry no signature from an operating-system vendor's program: no Authenticode signature and no Apple Developer ID signature or notarization.

- **Linux.** Linux has no vendor signing step for a downloaded archive. The checksum and the attestation are the evidence.
- **Windows.** Neura has no native Windows build. On Windows, Neura is the Linux x86-64 archive run under WSL 2, where Windows does not check Authenticode signatures. There is no Windows executable or installer to sign.
- **macOS.** No macOS archive is published in this release, so there is nothing to notarize.

### SignPath Foundation

The project is not enrolled in the [SignPath Foundation](https://signpath.org) code signing program for open-source projects, and no release file is signed by it. The program signs Windows binaries, and its [conditions](https://signpath.org/terms) require that a project "must already be released in the form that should be signed". Neura has never published a Windows binary. When a native Windows build exists and has been released, the project will apply, and this page will then name SignPath Foundation as the certificate's publisher.

## Roles

| Role | Who | What they do |
| --- | --- | --- |
| Committers and reviewers | The maintainers listed in [GOVERNANCE.md](../GOVERNANCE.md#roles) | Write and review the source, the build scripts, and the workflows. Every change reaches `main` through a pull request that passes the required checks. |
| Approvers | The same maintainers | Create the release tag and approve each run of the publishing job in the protected `release` environment. |

The project has one maintainer, who holds both roles. [GOVERNANCE.md](../GOVERNANCE.md#review-with-one-maintainer) records what that means for review.

## Privacy

Neura has no telemetry and sends no identifier, usage data, or source code anywhere.

The `neura` tool contacts the network in two cases:

- **When asked.** `neura update` and `neura update --check` download `neura-release.txt`, and `update` then downloads the release archive, over HTTPS from `github.com`. A program you run with `neura run` can use the network only as its capabilities allow ([Tooling](lang/Tooling.md)).
- **The update notice.** An installation made from a release archive checks for a newer release at most once every 7 days, and only when it runs in an interactive terminal outside continuous integration. The check is one HTTPS request for `neura-release.txt` from `github.com`. It sends nothing beyond what any HTTPS request carries, such as the machine's network address. Set `NEURA_NO_UPDATE_CHECK=1` to turn it off. [Install and update](Install.md#update-notices-and-privacy) gives the exact conditions.

GitHub's handling of those requests is covered by the [GitHub privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).
