<!-- Generated from docs/CodeSigning.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Code signing policy

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

This page states how Neura's release files are produced, what a user can verify about them, who approves a release, and what the installed program sends over the network.

## How release files are produced

Every file on a [release page](https://github.com/Kayyo321/neura/releases) is built by the [release workflow](https://github.com/Kayyo321/neura/blob/main/.github/workflows/release.yml) on GitHub-hosted runners, from the commit a `v*` tag names. No release file is built on a maintainer's machine. The workflow builds and tests each target, packs the archive, and runs the [lifecycle checks](ReleaseProcess#what-the-release-workflow-checks) on the packed archive on a second runner. The job that publishes runs only after those pass and after a maintainer approves it.

## What a user can verify

| Evidence | What it establishes | How to check it |
| --- | --- | --- |
| `SHA256SUMS` | The archive is the file the release workflow uploaded. | `sha256sum --check --ignore-missing SHA256SUMS` |
| Build provenance attestation | The archive was built by this repository's release workflow from the tagged commit. The attestation is signed through Sigstore with the workflow's identity, not with a key a maintainer holds. | `gh attestation verify <archive> --repo Kayyo321/neura` |

`neura update` checks the size and SHA-256 of what it downloads against the release's `neura-components.txt` and checks the attestation when the GitHub CLI is installed and signed in. The Windows archives and the PowerShell bootstrap are included in `SHA256SUMS` and the release's build-provenance attestations. Use `Get-FileHash -Algorithm SHA256 <file>` on Windows to compare a downloaded file with `SHA256SUMS`. See [Install and update](Install#what-is-verified) for the conditions of each check.

## What is not signed

Release files carry no signature from an operating-system vendor's program: no Authenticode signature and no Apple Developer ID signature or notarization.

- **Linux.** Linux has no vendor signing step for a downloaded archive. The checksum and the attestation are the evidence.
- **Windows.** The native 0.3.0 executables, DLLs and installers ship without Authenticode signatures. Microsoft Defender SmartScreen may warn about an unrecognized download. Smart App Control on Windows 11 may block execution when it cannot establish trust in an unsigned file. See [Windows installation](Install#windows-installation) and [Microsoft's app reputation guidance](https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation).
- **macOS.** The macOS archives are not signed with an Apple Developer ID and are not notarized. The linker gives each executable an ad hoc signature, which is what Apple silicon requires to run a program and identifies nobody. macOS marks a file that a web browser downloads as quarantined and refuses to run an unsigned program from it. `curl` does not set that mark, so the [one-line install](Install#install-with-one-command) works. For an archive downloaded with a browser, remove the mark before unpacking it: `xattr -d com.apple.quarantine neura-<version>-<target>.tar.gz`. An ad hoc signature does not enable the hardened runtime, so `neura` can map memory for the native code it generates for tier 2 regions ([native compilation](NVM#native-compilation)), which `--jit=on` enables on macOS. A future Developer ID signature with the hardened runtime would need the `com.apple.security.cs.allow-jit` entitlement for that (the code generator uses a `MAP_JIT` mapping on Apple silicon); without it, `--jit=on` fails at the start of a run and `--jit=auto` interprets regions.

### Windows native executables

`neura build --native` produces an unsigned Windows executable. If the SDK launcher template carries an Authenticode table, the build removes that table and clears the PE Security directory and checksum before adding the Neura image. Sign the finished `.exe` after packaging so the signature covers its image and policy. [Native executable layout](Standalone#54-consequences-of-the-layout).

### SignPath Foundation

The project is not enrolled in the [SignPath Foundation](https://signpath.org) code signing program for open-source projects. The program signs Windows binaries and its [conditions](https://signpath.org/terms) require that the project already have a release in the form to be signed. The project can apply after publishing a native Windows release. If it begins using SignPath, this page will name the certificate's publisher.

## Roles

| Role | Who | What they do |
| --- | --- | --- |
| Committers and reviewers | The maintainers listed in [GOVERNANCE.md](https://github.com/Kayyo321/neura/blob/main/GOVERNANCE.md#roles) | Write and review the source, the build scripts, and the workflows. Every change reaches `main` through a pull request that passes the required checks. |
| Approvers | The same maintainers | Create the release tag and approve each run of the publishing job in the protected `release` environment. |

The project has one maintainer, who holds both roles. [GOVERNANCE.md](https://github.com/Kayyo321/neura/blob/main/GOVERNANCE.md#review-with-one-maintainer) records what that means for review.

## Privacy

Neura has no telemetry and sends no identifier, usage data, or source code anywhere.

The `neura` tool contacts the network in two cases:

- **When asked.** `neura update` and `neura update --check` download `neura-release.txt`, and `update` then downloads the release archive, over HTTPS from `github.com`. A program you run with `neura run` can use the network only as its capabilities allow ([Tooling](Tooling)).
- **The update notice.** An installation made from a release archive checks for a newer release at most once every 7 days, and only when it runs in an interactive terminal outside continuous integration. The check is one HTTPS request for `neura-release.txt` from `github.com`. It sends nothing beyond what any HTTPS request carries, such as the machine's network address. Set `NEURA_NO_UPDATE_CHECK=1` to turn it off. [Install and update](Install#update-notices-and-privacy) gives the exact conditions.

GitHub's handling of those requests is covered by the [GitHub privacy statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).
