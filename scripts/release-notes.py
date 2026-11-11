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
    changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
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

That command works on Linux and inside WSL 2. To install by hand instead:

```sh
# Download the archive for your platform and SHA256SUMS from this page, then:
sha256sum --check --ignore-missing SHA256SUMS
gh attestation verify neura-{full}-<target>.tar.gz --repo {REPOSITORY}   # optional: build provenance
tar -xzf neura-{full}-<target>.tar.gz
cd neura-{full}-<target>
sh install.sh
```

| File | Platform |
| --- | --- |
| `neura-{full}-x86_64-linux-gnu.tar.gz` | Linux on x86-64 with glibc 2.39 or newer, and Windows through WSL 2 |
| `neura-{full}-aarch64-linux-gnu.tar.gz` | Linux on arm64 with glibc 2.39 or newer |

## Update and uninstall

`neura update --check` reports whether a newer release exists, `neura update` installs it, and `neura uninstall` removes the installation. `neura doctor` checks an installation.

## Verification

`SHA256SUMS` lists the SHA-256 of each archive. `neura-release.txt` is the metadata `neura update` reads. Each archive has a build provenance attestation that ties it to this repository's release workflow and the tagged commit. The files are not signed with an operating-system vendor's certificate; see the [code signing policy]({DOCS}/{tag}/docs/CodeSigning.md).
""")
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
