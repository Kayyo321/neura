#!/usr/bin/env python3
"""Write the notes of a GitHub release to standard output.

usage: release-notes.py <full version>

For a version such as 0.1.0 the notes hold the `## [0.1.0]` section of
CHANGELOG.md. For a release candidate such as 0.1.0-rc.1 they hold the
`## Unreleased` section, or the version's own section when it exists, under a
notice that the build is a candidate. Both end with the same instructions for
verifying, installing, updating, and uninstalling.

The exit status is 1 when CHANGELOG.md has no section to use.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPOSITORY = "Kayyo321/neura"
DOCS = f"https://github.com/{REPOSITORY}/blob"


def section(changelog: str, title: str) -> str | None:
    """The body of the `## <title>` section, without its heading."""
    lines = changelog.split("\n")
    for index, line in enumerate(lines):
        if line.startswith("## ") and (line[3:] == title or line[3:].startswith(title + " ")):
            end = index + 1
            while end < len(lines) and not lines[end].startswith("## "):
                end += 1
            return "\n".join(lines[index + 1:end]).strip()
    return None


def main() -> int:
    if len(sys.argv) != 2 or not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?", sys.argv[1]):
        print("usage: release-notes.py <full version>", file=sys.stderr)
        return 1
    full = sys.argv[1]
    version, _, prerelease = full.partition("-")
    root = Path(__file__).resolve().parent.parent
    with (root / "CHANGELOG.md").open(encoding="utf-8", newline=None) as source:
        changelog = source.read()
    body = section(changelog, f"[{version}]")
    if body is None and prerelease:
        body = section(changelog, "Unreleased")
    if not body:
        print(f"CHANGELOG.md has no section for {version}", file=sys.stderr)
        return 1

    tag = f"v{full}"
    # The changelog's links are relative to the repository's root, which a
    # release page is not; point them at the tagged files.
    body = re.sub(r"\]\((?!https?://|#|mailto:)([^)\s]+)\)", rf"]({DOCS}/{tag}/\1)", body)
    out = []
    if prerelease:
        out.append(f"**This is a release candidate of Neura {version}, published for testing.** "
                   "`neura update` does not move an installation to a release candidate.\n")
    out.append(body)
    out.append(f"""
## Install

Supported platforms, system requirements, and every step are in [Install and update]({DOCS}/{tag}/docs/Install.md).

```sh
curl --proto '=https' --tlsv1.2 -fsSL https://github.com/{REPOSITORY}/releases/download/{tag}/install.sh | sh -s -- --version {full}
```

On Windows x86-64, run this in PowerShell:

```powershell
& ([scriptblock]::Create((irm https://github.com/{REPOSITORY}/releases/download/{tag}/install.ps1))) -Version {full}
```

Both commands install Neura-sdk by default. For Neura-runtime, add `--components runtime` on Linux or macOS or `-Components runtime` on Windows. To install Neura-sdk by hand on Linux or macOS:

```sh
# Download the archive for your platform and SHA256SUMS from this page, then:
sha256sum --check --ignore-missing SHA256SUMS
gh attestation verify neura-sdk-{full}-<target>.tar.gz --repo {REPOSITORY}   # optional: build provenance
tar -xzf neura-sdk-{full}-<target>.tar.gz
cd neura-sdk-{full}-<target>
sh install.sh
```

On Windows, compare the archive's `Get-FileHash` output with its entry in `SHA256SUMS`, optionally verify it with `gh attestation verify`, and run `install.cmd` from the unpacked archive.

| Platform | Neura-sdk | Neura-runtime |
| --- | --- | --- |
| Linux on x86-64 with glibc 2.39 or newer | `neura-sdk-{full}-x86_64-linux-gnu.tar.gz` | `neura-runtime-{full}-x86_64-linux-gnu.tar.gz` |
| Linux on arm64 with glibc 2.39 or newer | `neura-sdk-{full}-aarch64-linux-gnu.tar.gz` | `neura-runtime-{full}-aarch64-linux-gnu.tar.gz` |
| macOS 15 or newer on Apple silicon | `neura-sdk-{full}-aarch64-apple-darwin.tar.gz` | `neura-runtime-{full}-aarch64-apple-darwin.tar.gz` |
| macOS 15 or newer on Intel | `neura-sdk-{full}-x86_64-apple-darwin.tar.gz` | `neura-runtime-{full}-x86_64-apple-darwin.tar.gz` |
| Windows 10 version 2004 or newer on x86-64 | `neura-sdk-{full}-x86_64-windows-msvc.tar.gz` | `neura-runtime-{full}-x86_64-windows-msvc.tar.gz` |

The Linux and macOS `neura-{full}-<target>.tar.gz` archives support updates from 0.2 clients.

## Update and uninstall

`neura update --check` reports whether a newer release exists, `neura update` installs it, and `neura uninstall` removes the installation. `neura doctor` checks an installation.

## Verification

`SHA256SUMS` lists the SHA-256 of each archive and bootstrap installer. `neura-components.txt` is the current update metadata. The `neura-release.txt` manifest serves the 0.2 clients. Each asset has a build provenance attestation that ties it to this repository's release workflow and the tagged commit. See the [code signing policy]({DOCS}/{tag}/docs/CodeSigning.md) for platform-specific details.
""")
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
