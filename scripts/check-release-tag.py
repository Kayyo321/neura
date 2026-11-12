#!/usr/bin/env python3
"""Check that a tag can start a release, and print the values the workflow needs.

A release tag is `v<VERSION>` or `v<VERSION>-rc.<N>`, where `<VERSION>` is the
content of the `VERSION` file at the tagged commit. The tagged commit must be
on `main`, except a release candidate may be the exact tip of an explicitly
selected candidate branch. A tag without `-rc.<N>` also needs a `## [<VERSION>]` section with a
date in `CHANGELOG.md`.

On success the script prints `version=`, `prerelease=`, and `full=` lines in
the form GitHub Actions reads from `$GITHUB_OUTPUT`, and exits with status 0.
Otherwise it prints each problem and exits with status 1.

`--no-ancestry` leaves out the check that the commit is on `main`, for
`test-checks.py`, which has no remote to compare with.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

TAG = re.compile(r"v(\d+\.\d+\.\d+)(?:-(rc\.[1-9]\d*))?")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("tag")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--main", default="origin/main", help="the ref the tagged commit must be on")
    parser.add_argument("--no-ancestry", action="store_true")
    parser.add_argument("--candidate-branch", help="allow a release candidate at the exact tip of this isolated branch")
    arguments = parser.parse_args()

    problems: list[str] = []
    match = TAG.fullmatch(arguments.tag)
    if match is None:
        print(f"{arguments.tag}: a release tag is v<MAJOR>.<MINOR>.<PATCH> or that followed by -rc.<N>")
        return 1
    version, prerelease = match.group(1), match.group(2) or ""

    try:
        recorded = (arguments.root / "VERSION").read_text(encoding="utf-8").strip()
    except OSError as error:
        recorded = ""
        problems.append(f"cannot read VERSION: {error}")
    if recorded and recorded != version:
        problems.append(f"the tag names version {version}, and VERSION holds {recorded}")

    if not prerelease:
        try:
            changelog = (arguments.root / "CHANGELOG.md").read_text(encoding="utf-8")
        except OSError as error:
            changelog = ""
            problems.append(f"cannot read CHANGELOG.md: {error}")
        heading = re.compile(rf"^## \[{re.escape(version)}\] - \d{{4}}-\d{{2}}-\d{{2}}$", re.MULTILINE)
        if changelog and not heading.search(changelog):
            problems.append(f"CHANGELOG.md has no `## [{version}] - <date>` section")

    if not arguments.no_ancestry:
        result = subprocess.run(["git", "-C", str(arguments.root), "merge-base", "--is-ancestor", "HEAD", arguments.main],
                                capture_output=True, text=True, check=False)
        if result.returncode != 0:
            candidate = False
            if prerelease and arguments.candidate_branch:
                head = subprocess.run(["git", "-C", str(arguments.root), "rev-parse", "HEAD"], capture_output=True, text=True, check=False)
                branch = subprocess.run(["git", "-C", str(arguments.root), "rev-parse", "--verify", arguments.candidate_branch], capture_output=True, text=True, check=False)
                candidate = head.returncode == 0 and branch.returncode == 0 and head.stdout.strip() == branch.stdout.strip()
            if not candidate:
                problems.append(f"the tagged commit is not on {arguments.main} or the exact allowed candidate branch tip" + (f": {result.stderr.strip()}" if result.stderr.strip() else ""))

    if problems:
        for problem in problems:
            print(f"{arguments.tag}: {problem}")
        return 1
    print(f"version={version}")
    print(f"prerelease={prerelease}")
    print(f"full={version}-{prerelease}" if prerelease else f"full={version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
