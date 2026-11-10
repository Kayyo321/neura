#!/usr/bin/env python3
"""Check the GitHub Actions workflows against the repository's rules.

Every file in `.github/workflows/` must:

  * pin each action and reusable workflow from another repository to a full
    commit hash, and each `docker://` image to a digest;
  * declare top-level `permissions`, and never `write-all`;
  * declare top-level `concurrency`;
  * give every job `timeout-minutes`, unless the job calls a reusable
    workflow;
  * pass `persist-credentials: false` to every `actions/checkout` step; and
  * not use the `pull_request_target` or `workflow_run` triggers, which run
    with the base repository's credentials.

The check reads workflows line by line and expects block-style YAML indented
with spaces. It reports anything it cannot read that way as a problem, so an
unusual layout fails instead of passing unexamined.

The exit status is 0 when every workflow follows the rules and 1 otherwise.
Each problem is reported as `path:line: message`.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FULL_COMMIT = re.compile(r"^[0-9a-f]{40}$")
IMAGE_DIGEST = re.compile(r"@sha256:[0-9a-f]{64}$")
KEY = re.compile(r"""^(?:- +)?["']?([A-Za-z0-9_.-]+)["']?:(?: +(.*))?$""")
FORBIDDEN_TRIGGERS = ("pull_request_target", "workflow_run")


def without_comment(line: str) -> str:
    """The line with a trailing `#` comment removed, outside quotes."""
    quote = None
    for index, character in enumerate(line):
        if quote:
            if character == quote:
                quote = None
        elif character in "\"'":
            quote = character
        elif character == "#" and (index == 0 or line[index - 1] in " \t"):
            return line[:index].rstrip()
    return line.rstrip()


class Workflow:
    def __init__(self, path: Path, name: str) -> None:
        self.name = name
        self.problems: list[str] = []
        # (line number, indentation, content) for each line that holds YAML.
        self.lines: list[tuple[int, int, str]] = []
        for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if "\t" in raw[: len(raw) - len(raw.lstrip())]:
                self.report(number, "indented with a tab; use spaces")
            content = without_comment(raw)
            if content.strip() and content.strip() != "---":
                self.lines.append((number, len(content) - len(content.lstrip()), content.strip()))

    def report(self, line: int, message: str) -> None:
        self.problems.append(f"{self.name}:{line}: {message}")

    def block(self, index: int) -> list[tuple[int, int, str]]:
        """The lines nested below the line at `index`."""
        _, indent, _ = self.lines[index]
        nested = []
        for entry in self.lines[index + 1 :]:
            if entry[1] <= indent:
                break
            nested.append(entry)
        return nested

    def check(self) -> None:
        top = {}
        for index, (number, indent, content) in enumerate(self.lines):
            match = KEY.match(content)
            if indent == 0:
                if not match or content.startswith("- "):
                    self.report(number, f"cannot read the top-level line `{content}`")
                    continue
                top[match.group(1)] = index

            if re.search(r"(^|[\s\[,{])uses\s*:", content):
                self.check_uses(index, number, content)
            for trigger in FORBIDDEN_TRIGGERS:
                if re.search(rf"(^|[\s\[,{{\"'-]){trigger}($|[\s\],}}:\"'])", content):
                    self.report(number, f"the `{trigger}` trigger runs with the base repository's credentials")

        for required in ("on", "permissions", "concurrency", "jobs"):
            if required not in top:
                self.report(1, f"no top-level `{required}`")

        if "permissions" in top:
            number, _, content = self.lines[top["permissions"]]
            entries = [content] + [entry[2] for entry in self.block(top["permissions"])]
            if any("write-all" in entry for entry in entries):
                self.report(number, "`permissions: write-all` grants every permission; list the ones needed")

        if "jobs" in top:
            self.check_jobs(top["jobs"])

    def check_uses(self, index: int, number: int, content: str) -> None:
        match = KEY.match(content)
        if not match or match.group(1) != "uses" or not match.group(2):
            self.report(number, f"cannot read the `uses` line `{content}`; write `uses: owner/name@commit`")
            return
        reference = match.group(2).strip("\"'")
        if reference.startswith("./"):
            return
        if reference.startswith("docker://"):
            if not IMAGE_DIGEST.search(reference):
                self.report(number, f"`{reference}` is not pinned to an image digest (`@sha256:...`)")
            return
        action, separator, revision = reference.partition("@")
        if not separator or not FULL_COMMIT.match(revision):
            self.report(number, f"`{reference}` is not pinned to a full commit hash")
        if action.lower() == "actions/checkout":
            self.check_checkout(index, number)

    def check_checkout(self, index: int, number: int) -> None:
        # The step is the list item that holds the `uses` line: that line
        # itself, or the nearest `- ` line above it with less indentation.
        start = index
        if not self.lines[index][2].startswith("- "):
            indent = self.lines[index][1]
            while start > 0 and not (self.lines[start][2].startswith("- ") and self.lines[start][1] < indent):
                start -= 1
        step = [self.lines[start]] + self.block(start)
        if not any(re.fullmatch(r"(- +)?persist-credentials: +false", entry[2]) for entry in step):
            self.report(number, "`actions/checkout` without `persist-credentials: false` leaves the token in the checkout")

    def check_jobs(self, index: int) -> None:
        jobs = self.block(index)
        if not jobs:
            self.report(self.lines[index][0], "`jobs` is empty")
            return
        job_indent = jobs[0][1]
        position = index + 1
        for offset, (number, indent, content) in enumerate(jobs):
            if indent != job_indent:
                continue
            match = KEY.match(content)
            if not match or match.group(2):
                self.report(number, f"cannot read the job line `{content}`")
                continue
            body = self.block(position + offset)
            if not body:
                self.report(number, f"job `{match.group(1)}` is empty")
                continue
            key_indent = body[0][1]
            keys = {KEY.match(entry[2]).group(1) for entry in body if entry[1] == key_indent and KEY.match(entry[2])}
            if "uses" not in keys and "timeout-minutes" not in keys:
                self.report(number, f"job `{match.group(1)}` has no `timeout-minutes`")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="the repository checkout (default: this script's repository)")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    directory = root / ".github" / "workflows"
    files = sorted(path for path in directory.glob("*") if path.suffix in (".yml", ".yaml"))
    problems = []
    for path in files:
        workflow = Workflow(path, path.relative_to(root).as_posix())
        workflow.check()
        problems.extend(workflow.problems)

    for problem in problems:
        print(problem)
    print(f"Checked {len(files)} workflows; {len(problems)} problems.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
