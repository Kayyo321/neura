#!/usr/bin/env python3
"""Compare the repository's GitHub settings with the files in `.github/settings`.

`repository.json` maps an API path below `repos/<owner>/<name>` to the values
that response must contain. Each `ruleset-*.json` file is a ruleset that must
exist under the same name with the same target, enforcement, bypass actors,
conditions, and rules, and no ruleset may exist without a file.

A response may hold more than a file names; every value a file names must be
present and equal. A list must have the same length as the expected list, and
each expected element must match a different element of it.

Reading the live settings needs the `gh` command, signed in to an account that
can read the repository's settings. `--live DIR` reads saved responses from a
directory instead, which is how `test-checks.py` runs this script.

The exit status is 0 when the settings match and 1 otherwise.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

RULESET_FIELDS = ("name", "target", "enforcement", "bypass_actors", "conditions", "rules")


def contains(actual: object, expected: object, where: str, problems: list[str]) -> None:
    """Record a problem for each value in `expected` that `actual` lacks."""
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            problems.append(f"{where}: expected an object, found {json.dumps(actual)}")
            return
        for key, value in expected.items():
            if key not in actual:
                problems.append(f"{where}.{key}: missing; expected {json.dumps(value)}")
            else:
                contains(actual[key], value, f"{where}.{key}", problems)
    elif isinstance(expected, list):
        if not isinstance(actual, list):
            problems.append(f"{where}: expected a list, found {json.dumps(actual)}")
            return
        if len(actual) != len(expected):
            problems.append(f"{where}: expected {len(expected)} entries, found {len(actual)}")
            return
        remaining = list(actual)
        for index, value in enumerate(expected):
            for candidate in remaining:
                trial: list[str] = []
                contains(candidate, value, where, trial)
                if not trial:
                    remaining.remove(candidate)
                    break
            else:
                problems.append(f"{where}[{index}]: no entry matches {json.dumps(value)}")
    elif actual != expected:
        problems.append(f"{where}: expected {json.dumps(expected)}, found {json.dumps(actual)}")


class Live:
    """Reads API responses from `gh` or from a directory of saved responses."""

    def __init__(self, repository: str, saved: Path | None) -> None:
        self.repository = repository
        self.saved = saved

    def get(self, path: str) -> object:
        """Return the decoded response, `{}` for an empty one; raise OSError on failure."""
        if self.saved is not None:
            name = (path or "repository").replace("/", "_") + ".json"
            return json.loads((self.saved / name).read_text(encoding="utf-8"))
        target = f"repos/{self.repository}" + (f"/{path}" if path else "")
        result = subprocess.run(["gh", "api", target], capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise OSError(result.stderr.strip() or f"gh api {target} failed")
        return json.loads(result.stdout) if result.stdout.strip() else {}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--repository", default="Kayyo321/neura")
    parser.add_argument("--live", type=Path, help="directory of saved API responses")
    arguments = parser.parse_args()

    settings = arguments.root / ".github" / "settings"
    live = Live(arguments.repository, arguments.live)
    problems: list[str] = []
    checked = 0

    try:
        expected = json.loads((settings / "repository.json").read_text(encoding="utf-8"))
        files = sorted(settings.glob("ruleset-*.json"))
        rulesets = {path.name: json.loads(path.read_text(encoding="utf-8")) for path in files}
    except (OSError, ValueError) as error:
        print(f"{settings}: {error}")
        return 1

    for path, values in expected.items():
        checked += 1
        try:
            contains(live.get(path), values, path or "repository", problems)
        except (OSError, ValueError) as error:
            problems.append(f"{path or 'repository'}: cannot read: {error}")

    try:
        listed = live.get("rulesets")
        by_name = {entry["name"]: entry["id"] for entry in listed}  # type: ignore[union-attr,index]
    except (OSError, ValueError, TypeError, KeyError) as error:
        problems.append(f"rulesets: cannot read: {error}")
        by_name = {}
    wanted = {ruleset["name"] for ruleset in rulesets.values()}
    for name in sorted(set(by_name) - wanted):
        problems.append(f"rulesets: \"{name}\" exists on GitHub and has no file")
    for file, ruleset in rulesets.items():
        checked += 1
        name = ruleset["name"]
        if name not in by_name:
            problems.append(f"{file}: no ruleset named \"{name}\" exists on GitHub")
            continue
        try:
            actual = live.get(f"rulesets/{by_name[name]}")
        except (OSError, ValueError) as error:
            problems.append(f"{file}: cannot read: {error}")
            continue
        contains(actual, {field: ruleset[field] for field in RULESET_FIELDS if field in ruleset}, file, problems)

    for problem in problems:
        print(problem)
    print(f"Checked {checked} settings; {len(problems)} problems.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
