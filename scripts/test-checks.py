#!/usr/bin/env python3
"""Test the repository's check scripts.

`check-docs.py`, `check-workflows.py`, `check-test-report.py`,
`generate-wiki.py`, and `check-doc-examples.py` gate pull requests, so each must accept valid input and reject each kind of problem it
exists to find. This script runs them on small inputs built in a temporary
directory and compares the exit status and the reported problem.

The exit status is 0 when every case behaves as expected and 1 otherwise.
"""

from __future__ import annotations

import importlib.util
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


def load_script(name: str):
    """Import a script whose file name has a hyphen."""
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), SCRIPTS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def same(label: str, got: str, wanted: str) -> None:
    """A case that compares text in this process."""
    global failures
    if got == wanted:
        print(f"PASS {label}")
    else:
        failures += 1
        print(f"FAIL {label}: expected\n{wanted}\nbut got\n{got}")


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


def wiki(label: str, change, problem: str | None) -> None:
    """Run generate-wiki.py --check on a tree it has just written, after `change(root)` edits it."""
    generator = load_script("generate-wiki")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path in generator.GENERATED:
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text("# Page\n\nSee [the overview](../lang/Overview.md#top) and "
                                     "[source](../../src/main.cpp).\n", encoding="utf-8")
        (root / "src").mkdir()
        (root / "src" / "main.cpp").write_text("", encoding="utf-8")
        (root / "wiki").mkdir()
        for name in generator.HAND_WRITTEN | {"Stdlib-Core-Text", "Stdlib-Program"}:
            (root / "wiki" / f"{name}.md").write_text("# By hand\n", encoding="utf-8")
        script = str(SCRIPTS / "generate-wiki.py")
        subprocess.run([sys.executable, script, "--root", directory], check=True, capture_output=True)
        change(root)
        expect(f"generate-wiki: {label}", [sys.executable, script, "--root", directory, "--check"], problem)


def examples(label: str, files: dict[str, str], problem: str | None) -> None:
    """Run check-doc-examples.py --directives-only, which needs no compiler."""
    with tempfile.TemporaryDirectory() as directory:
        for name, text in files.items():
            path = Path(directory) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        expect(f"doc examples: {label}", [sys.executable, str(SCRIPTS / "check-doc-examples.py"), "--root", directory,
                                          "--directives-only"], problem)


def block(directive: str | None, blank: str = "") -> str:
    comment = f"<!-- example: {directive} -->\n{blank}" if directive else ""
    return f"# Page\n\n{comment}```neura\nlet x = 1;\n```\n"


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

    wiki("pages that match", lambda root: None, None)
    wiki("a stale page", lambda root: (root / "wiki" / "Types.md").write_text("# Page\n", encoding="utf-8"),
         "wiki/Types.md: is stale")
    wiki("an edited generated page",
         lambda root: (root / "wiki" / "Status.md").write_text(
             (root / "wiki" / "Status.md").read_text(encoding="utf-8") + "\nextra\n", encoding="utf-8"),
         "wiki/Status.md: is stale")
    wiki("a missing page", lambda root: (root / "wiki" / "Tooling.md").unlink(), "wiki/Tooling.md: is missing")
    wiki("an unknown wiki page", lambda root: (root / "wiki" / "Notes.md").write_text("# Notes\n", encoding="utf-8"),
         "wiki/Notes.md: neither generated")
    wiki("a docs page with no wiki page", lambda root: (root / "docs" / "Extra.md").write_text("# E\n", encoding="utf-8"),
         "docs/Extra.md: has no wiki page")
    wiki("a missing hand-written page", lambda root: (root / "wiki" / "Home.md").unlink(),
         "wiki/Home.md: listed as hand-written but does not exist")
    wiki("a repository-only page", lambda root: (root / "docs" / "Wiki.md").write_text("# W\n", encoding="utf-8"), None)

    generator = load_script("generate-wiki")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for path in ("docs/lang/Status.md", "docs/runtime/NVM.md", "docs/lang/Stdlib.md", "docs/Wiki.md",
                     "README.md", "wiki/Home.md", "wiki/Stdlib.md", "src/main.cpp"):
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text("", encoding="utf-8")
        for name, link, wanted in (
            ("a docs page with a wiki page", "[a](Status.md)", "[a](Status)"),
            ("a docs page and anchor", "[a](Status.md#open-decisions)", "[a](Status#open-decisions)"),
            ("a page in another docs directory", "[a](../runtime/NVM.md#x)", "[a](NVM#x)"),
            ("the renamed standard library plan", "[a](Stdlib.md#18-implementation-decisions)",
             "[a](Stdlib-Design#18-implementation-decisions)"),
            ("a docs page with no wiki page", "[a](../Wiki.md)", f"[a]({BLOB}/docs/Wiki.md)"),
            ("a repository file", "[a](../../src/main.cpp#L3)", f"[a]({BLOB}/src/main.cpp#L3)"),
            ("a repository directory", "[a](../../src/)", f"[a]({BLOB.replace("blob", "tree")}/src)"),
            ("a hand-written wiki page", "[a](../../wiki/Stdlib.md#x)", "[a](Stdlib#x)"),
            ("the wiki home page", "[a](../../wiki/Home.md)", "[a](Home)"),
            ("a link with a title", '[a](Status.md "t")', '[a](Status "t")'),
            ("an angle-bracket target", "[a](<Status.md>)", "[a](<Status>)"),
            ("an image", "![a](../../src/main.cpp)", f"![a]({BLOB}/src/main.cpp)"),
            ("a reference definition", "[a]: Status.md", "[a]: Status"),
            ("an HTML link", '<a href="Status.md">a</a>', '<a href="Status">a</a>'),
            ("a link text with a code span", "[`a`](Status.md)", "[`a`](Status)"),
            ("a link split over two lines", "[a\nb](Status.md)", "[a\nb](Status)"),
            ("a same-page link", "[a](#heading)", "[a](#heading)"),
            ("an external link", "[a](https://example.com/Status.md)", "[a](https://example.com/Status.md)"),
            ("a mail link", "[a](mailto:x@example.com)", "[a](mailto:x@example.com)"),
            ("a link in a code span", "`[a](Status.md)` and [b](Status.md)", "`[a](Status.md)` and [b](Status)"),
            ("a link in a fenced block", "```text\n[a](Status.md)\n```\n[b](Status.md)",
             "```text\n[a](Status.md)\n```\n[b](Status)"),
            ("a link in a longer fence", "````md\n```\n[a](Status.md)\n````\n[b](Status.md)",
             "````md\n```\n[a](Status.md)\n````\n[b](Status)"),
            ("a link in a tilde fence", "~~~\n[a](Status.md)\n~~~\n[b](Status.md)",
             "~~~\n[a](Status.md)\n~~~\n[b](Status)"),
        ):
            same(f"generate-wiki: {name}",
                 generator.rewrite_links(link, "docs/lang/Types.md", root, "example/project"),
                 wanted)
        page = generator.generate("docs/lang/Status.md", "\n# Title\n\nBody [a](Types.md).\n", root, "example/project")
        same("generate-wiki: the generated page", page, generator.COMMENT.format(path="docs/lang/Status.md")
             + "\n# Title\n\n" + generator.NAVIGATION + "\n\nBody [a](Types).\n")

    examples("every directive", {
        "README.md": block("check"),
        "docs/A.md": block("run") + "\n```text\nhi\n```\n" + block("test") + block("rejected: unknown name")
                     + block("fragment: a single declaration", "\n"),
    }, None)
    examples("a block that is not Neura", {"docs/A.md": "```text\nplain\n```\n```sh\nls\n```\n"}, None)
    examples("a missing directive", {"docs/A.md": block(None)}, "docs/A.md:3: a ```neura block has no")
    examples("a missing directive in the README", {"README.md": block(None)}, "README.md:3: a ```neura block has no")
    examples("a directive two lines above", {"docs/A.md": "<!-- example: check -->\n\n\n```neura\nlet x = 1;\n```\n"},
             "docs/A.md:4: a ```neura block has no")
    examples("a directive in a nested directory", {"docs/lang/B.md": block(None)}, "docs/lang/B.md:3:")
    examples("an unknown directive", {"docs/A.md": block("compile")}, "unknown example directive `compile`")
    examples("a fragment without a reason", {"docs/A.md": block("fragment")}, "`example: fragment` needs a reason")
    examples("a fragment with an empty reason", {"docs/A.md": block("fragment:  ")}, "`example: fragment` needs a reason")
    examples("a rejected block without text", {"docs/A.md": block("rejected")}, "`example: rejected` needs")
    examples("a check with an argument", {"docs/A.md": block("check: yes")}, "`example: check` takes no argument")
    examples("a run without expected output", {"docs/A.md": block("run")}, "must be followed by a ```text block")
    examples("a run followed by another kind of block", {"docs/A.md": block("run") + "\n```sh\nls\n```\n"},
             "must be followed by a ```text block")

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
