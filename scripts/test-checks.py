#!/usr/bin/env python3
"""Test the repository's check scripts.

`check-docs.py`, `check-workflows.py`, and `check-test-report.py` gate pull
requests, so each must accept valid input and reject each kind of problem it
exists to find. This script runs them on small inputs built in a temporary
directory and compares the exit status and the reported problem.

The exit status is 0 when every case behaves as expected and 1 otherwise.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
COMMIT = "0123456789abcdef0123456789abcdef01234567"
WIKI = "https://github.com/example/project/wiki"
BLOB = "https://github.com/example/project/blob/main"

WORKFLOW = f"""\
name: Example

on:
  pull_request:
  push:
    branches:
      - main

permissions:
  contents: read

concurrency:
  group: example
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-24.04
    timeout-minutes: 10
    steps:
      - name: Check out
        uses: actions/checkout@{COMMIT} # v1.0.0
        with:
          persist-credentials: false
      - run: make

  call:
    uses: ./.github/workflows/reusable.yml
"""

PAGE = """\
# Title

## Integer arithmetic

### 9.3 `ArrayList<T>`

## Twice

## Twice

```sh
# not a heading
```
"""

failures = 0


def expect(label: str, command: list[str], problem: str | None) -> None:
    """Run a check; it must pass when `problem` is None and report it otherwise."""
    global failures
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    output = result.stdout + result.stderr
    passed = result.returncode == (0 if problem is None else 1) and (problem is None or problem in output)
    if not passed:
        failures += 1
        wanted = "pass" if problem is None else f"fail with `{problem}`"
        print(f"FAIL {label}: expected the check to {wanted}; it exited {result.returncode} with:\n{output}")
    else:
        print(f"PASS {label}")


def workflow(label: str, old: str, new: str, problem: str | None) -> None:
    text = WORKFLOW.replace(old, new, 1)
    assert old == new or text != WORKFLOW, label
    with tempfile.TemporaryDirectory() as directory:
        target = Path(directory) / ".github" / "workflows"
        target.mkdir(parents=True)
        (target / "example.yml").write_text(text, encoding="utf-8")
        expect(f"workflows: {label}", [sys.executable, str(SCRIPTS / "check-workflows.py"), "--root", directory], problem)


def docs(label: str, files: dict[str, str], problem: str | None) -> None:
    with tempfile.TemporaryDirectory() as directory:
        for name, text in files.items():
            path = Path(directory) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        # The link check reads the files Git tracks.
        subprocess.run(["git", "-C", directory, "init", "--quiet"], check=True)
        subprocess.run(["git", "-C", directory, "add", "--all"], check=True)
        expect(f"docs: {label}", [sys.executable, str(SCRIPTS / "check-docs.py"), "--root", directory,
                                  "--repository", "example/project"], problem)


def report(label: str, cases: str | None, minimum: int, problem: str | None) -> None:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "junit.xml"
        if cases is not None:
            path.write_text(f'<?xml version="1.0"?>\n<testsuite name="s">{cases}</testsuite>\n', encoding="utf-8")
        expect(f"test report: {label}", [sys.executable, str(SCRIPTS / "check-test-report.py"), str(path),
                                         "--minimum", str(minimum)], problem)


def main() -> int:
    step = "      - name: Check out\n"
    workflow("a workflow that follows the rules", "", "", None)
    workflow("an action pinned to a tag", f"checkout@{COMMIT}", "checkout@v1", "not pinned to a full commit hash")
    workflow("an action pinned to a short hash", f"checkout@{COMMIT}", "checkout@0123456",
             "not pinned to a full commit hash")
    workflow("a reusable workflow from another repository", "./.github/workflows/reusable.yml",
             "example/project/.github/workflows/reusable.yml@main", "not pinned to a full commit hash")
    workflow("a container image without a digest", step, "      - uses: docker://alpine:3.20\n" + step, "image digest")
    workflow("a flow-style step", step, "      - {uses: example/action@v1}\n" + step, "cannot read the `uses` line")
    workflow("a checkout that keeps credentials", "          persist-credentials: false\n", "",
             "persist-credentials: false")
    workflow("a checkout that sets persist-credentials to true", "persist-credentials: false",
             "persist-credentials: true", "persist-credentials: false")
    workflow("no permissions", "permissions:\n  contents: read\n", "", "no top-level `permissions`")
    workflow("write-all permissions", "permissions:\n  contents: read\n", "permissions: write-all\n", "write-all")
    workflow("no concurrency", "concurrency:\n", "other:\n", "no top-level `concurrency`")
    workflow("a job without a timeout", "    timeout-minutes: 10\n", "", "job `build` has no `timeout-minutes`")
    workflow("the pull_request_target trigger", "  pull_request:\n", "  pull_request_target:\n", "pull_request_target")
    workflow("the workflow_run trigger in a list", "on:\n  pull_request:\n  push:\n    branches:\n      - main\n",
             "on: [push, workflow_run]\n", "workflow_run")
    workflow("tab indentation", "    timeout-minutes: 10", "\ttimeout-minutes: 10", "indented with a tab")

    docs("links that resolve", {
        "README.md": "# Top\n\n[a](docs/A.md) [b](docs/A.md#integer-arithmetic) [c](docs/A.md#93-arraylistt) "
                     "[d](docs/A.md#twice-1) [e](#top) [f](docs/) [g](https://example.com/x) ![h](docs/A.md)\n",
        "docs/A.md": PAGE,
        "wiki/Home.md": f"[p](Page) [q](Page#integer-arithmetic) [r]({WIKI}/Page#twice) [s]({WIKI}) "
                        f"[t]({BLOB}/docs/A.md#integer-arithmetic) [u]({BLOB}/docs/A.md#L3)\n",
        "wiki/Page.md": PAGE,
    }, None)
    docs("links inside code", {"README.md": "`[a](Missing.md)`\n\n```\n[b](Gone.md)\n```\n"}, None)
    docs("a missing file", {"README.md": "[a](docs/Missing.md)\n"}, "docs/Missing.md does not exist")
    docs("a missing image", {"README.md": "![logo](artwork/none.png)\n"}, "artwork/none.png does not exist")
    docs("a missing heading", {"README.md": "[a](docs/A.md#nope)\n", "docs/A.md": PAGE}, "no heading `#nope`")
    docs("a heading inside a code block", {"README.md": "[a](docs/A.md#not-a-heading)\n", "docs/A.md": PAGE},
         "no heading `#not-a-heading`")
    docs("a missing heading in the same page", {"README.md": "# Top\n\n[a](#bottom)\n"}, "no heading `#bottom`")
    docs("a missing heading in an HTML link", {"README.md": '# Top\n\n<a href="#nowhere">x</a>\n'},
         "no heading `#nowhere`")
    docs("a link that leaves the repository", {"README.md": "[a](../outside.md)\n"}, "leaves the repository")
    docs("a missing wiki page", {"wiki/Home.md": "[a](Nowhere)\n"}, "no wiki page wiki/Nowhere.md")
    docs("a missing heading in a wiki URL", {"wiki/Home.md": f"[a]({WIKI}/Page#nope)\n", "wiki/Page.md": PAGE},
         "no heading `#nope`")
    docs("a repository URL to a missing path", {"wiki/Home.md": f"[a]({BLOB}/src/gone.cpp)\n"},
         "src/gone.cpp does not exist")

    ran = '<testcase name="a" status="run"/><testcase name="b" status="run"/>'
    report("every test ran", ran, 2, None)
    report("fewer tests than the minimum", ran, 3, "at least 3 are required")
    report("no tests", "", 1, "at least 1 are required")
    report("a skipped test", ran + '<testcase name="c" status="notrun"><skipped message="no libclang"/></testcase>',
           2, "c: skipped (no libclang)")
    report("a disabled test", ran + '<testcase name="c" status="disabled"/>', 2, "c: status `disabled`, not run")
    report("a failed test", ran + '<testcase name="c" status="fail"><failure message="x"/></testcase>', 2, "c: failed")
    report("a missing report", None, 1, "cannot read the test report")

    print(f"{failures} unexpected results.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
