#!/usr/bin/env python3
"""Test the repository's check scripts.

`check-docs.py`, `check-workflows.py`, `check-test-report.py`,
`generate-wiki.py`, `check-doc-examples.py`, and `build-site.py` gate pull
requests, so each must accept valid input and reject each kind of problem it
exists to find. This script runs them on small inputs built in a temporary
directory and compares the exit status and the reported problem.

The exit status is 0 when every case behaves as expected and 1 otherwise.
"""

from __future__ import annotations

import importlib.util
import json
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

HOME = f"""\
<!doctype html>
<html lang="en">
<head>
<title>Example</title>
<link rel="stylesheet" href="assets/site.css">
<link rel="canonical" href="https://example.com/">
</head>
<body>
<h1 id="start">Example</h1>
<a href="#start">a</a> <a href="./">b</a> <a href="guide/">c</a> <a href="guide/index.html#part">d</a>
<a href="https://example.com/x">e</a> <a href="{BLOB}/docs/A.md">f</a> <a href="{WIKI}/Page">g</a>
<a href="mailto:someone@example.com">h</a> <a href="#top">i</a>
<img src="assets/logo.png" srcset="assets/logo.png 2x" alt="">
<img src="data:image/gif;base64,R0lGODlhAQABAAAAACw=" alt="">
<pre data-source="examples/hello.nra"><code><b>let</b> x = 1 &lt; 2;
</code></pre>
</body>
</html>
"""

STYLES = """\
/* url("in-a-comment.png") is not a reference */
body { background: url("logo.png"); }
"""

SITE = {
    "site/index.html": HOME,
    "site/404.html": '<!doctype html>\n<html lang="en">\n<head>\n<title>Missing</title>\n</head>\n'
                     '<body><a href="./">home</a></body>\n</html>\n',
    "site/assets/site.css": STYLES,
    "site/assets/logo.png": "image",
    "site/guide/index.html": '<link rel="stylesheet" href="../assets/site.css">\n<h2 id="part">Part</h2>\n'
                             '<a href="../index.html#start">up</a>\n',
    "docs/A.md": PAGE,
    "wiki/Page.md": PAGE,
    "examples/hello.nra": "let x = 1 < 2;\n",
}

# The repository documents a site publishes: site/contents.toml lists them, and partials hold shared parts.
CONTENTS = """\
title = "Documents"
[[sections]]
title = "Guide"
pages = ["docs/Guide.md", { source = "docs/Ref.md", label = "Reference" }]
"""

GUIDE = """\
# Guide

Read [the reference](Ref.md#93-arraylistt), its [table](Ref.md#a-table), and
[line 1 of the example](../examples/hello.nra#L1).

```neura
let x = 1; // a comment
```
"""

REFERENCE = """\
# Reference

## 9.3 `ArrayList<T>`

## A table

| Name | Value |
| --- | :-: |
| `x\\|y` | 2 |
"""

DOCUMENTS = {
    "site/contents.toml": CONTENTS,
    "site/partials/masthead.html": '<header><a href="{{home}}">Home</a> <a href="{{root}}docs/">Docs</a></header>\n',
    "site/partials/head.html": '<link rel="stylesheet" href="{{root}}assets/site.css">\n',
    "site/assets/docs.js": "",
    "docs/Guide.md": GUIDE,
    "docs/Ref.md": REFERENCE,
}

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


def write_site(directory: str, files: dict[str, str | None]) -> None:
    for name, text in files.items():
        if text is not None:
            path = Path(directory) / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")


def site_command(directory: str, *mode: str) -> list[str]:
    return [sys.executable, str(SCRIPTS / "build-site.py"), "--root", directory,
            "--repository", "example/project", *mode]


def site(label: str, changes: dict[str, str | None], problem: str | None) -> None:
    """Check the example site with `changes`: a file's new text, or None to leave the file out."""
    with tempfile.TemporaryDirectory() as directory:
        write_site(directory, {**SITE, **changes})
        expect(f"site: {label}", site_command(directory, "--check"), problem)


def home(label: str, old: str, new: str, problem: str | None) -> None:
    """Check the example site with one change to its home page."""
    assert old in HOME, label
    site(label, {"site/index.html": HOME.replace(old, new, 1)}, problem)


def site_build() -> None:
    """A build copies each file, and changes only the page for a missing address."""
    global failures
    with tempfile.TemporaryDirectory() as directory:
        write_site(directory, SITE)
        output = Path(directory) / "out"
        command = site_command(directory, "--output", str(output), "--base-path", "/project")
        expect("site: a build into a new directory", command, None)
        expect("site: a build into a directory that holds files", command, "is not an empty directory")
        results = {
            "a build copies every other file unchanged": all(
                (output / name[len("site/"):]).read_text(encoding="utf-8") == text
                for name, text in SITE.items() if name.startswith("site/") and name != "site/404.html"),
            "a build gives the missing-address page a base": (output / "404.html").read_text(encoding="utf-8")
                == SITE["site/404.html"].replace("<head>", '<head><base href="/project/">'),
        }
        for label, passed in results.items():
            failures += not passed
            print(f"{'PASS' if passed else 'FAIL'} site: {label}")


def documents(label: str, changes: dict[str, str | None], problem: str | None) -> None:
    """Check the example site with published documents and `changes`."""
    site(f"documents: {label}", {**DOCUMENTS, **changes}, problem)


def documents_build() -> None:
    """A build renders each listed document, a page for each linked file, and the partials."""
    global failures
    with tempfile.TemporaryDirectory() as directory:
        home_page = HOME.replace("<body>", "<body>\n<!-- include: masthead -->\n<!-- markdown: docs/Ref.md#a-table -->")
        write_site(directory, {**SITE, **DOCUMENTS, "site/index.html": home_page})
        output = Path(directory) / "out"
        expect("site: documents: a build", site_command(directory, "--output", str(output)), None)

        def read(name: str) -> str:
            path = output / name
            return path.read_text(encoding="utf-8") if path.is_file() else ""

        guide, front = read("docs/Guide.html"), read("index.html")
        results = {
            "a link between documents leads to the published page": 'href="Ref.html#93-arraylistt"' in guide,
            "a link to a repository file leads to a page that shows it":
                'href="../source/examples/hello.nra.html#L1"' in guide
                and 'id="L1"' in read("source/examples/hello.nra.html"),
            "Neura code is marked": '<span class="k">let</span>' in guide and '<span class="c">// a comment</span>' in guide,
            "a heading has the id GitHub gives it": 'id="93-arraylistt"' in read("docs/Ref.html"),
            "a table cell keeps an escaped bar": "<code>x|y</code>" in read("docs/Ref.html"),
            "the documentation has a home page": 'href="Guide.html"' in read("docs/index.html"),
            "a partial is expanded with the path to the top": '<a href="./">Home</a> <a href="docs/">Docs</a>' in front,
            "a markdown include renders that section": '<h2 id="a-table">' in front and "<table>" in front,
            "the search index lists pages and sections": '"9.3 ArrayList<T>"' in read("assets/search.json"),
            "contents.toml and partials are not published":
                not (output / "contents.toml").exists() and not (output / "partials").exists(),
        }
        for name, passed in results.items():
            failures += not passed
            print(f"{'PASS' if passed else 'FAIL'} site: documents: {name}")


def markdown() -> None:
    """The Markdown renderer, on the constructs the repository's documents use."""
    global failures
    sys.path.insert(0, str(SCRIPTS))
    import site_markdown

    def rendered(text: str) -> str:
        return site_markdown.render(text, lambda target, line: target).html

    cases = {
        "a tight list has no paragraphs": ("- a\n- b\n", "<ul>\n<li>a</li>\n<li>b</li>\n</ul>"),
        "a loose list has paragraphs": ("- a\n\n- b\n", "<li><p>a</p></li>"),
        "a code fence inside a list item": ("1. Run:\n\n   ```sh\n   make\n   ```\n", "<pre data-language=\"sh\"><code>make</code></pre>"),
        "a code span with backticks inside": ("Use `` a`b ``.", "<code>a`b</code>"),
        "an underscore inside a word is not emphasis": ("snake_case_name", "snake_case_name"),
        "emphasis and strong": ("*a* and **b**", "<em>a</em> and <strong>b</strong>"),
        "a tag outside code is shown as text": ("Optional<T> value", "Optional&lt;T&gt; value"),
        "duplicate headings get numbered ids": ("## Twice\n\n## Twice\n", 'id="twice-1"'),
        "a link with code in its text": ("[`x`](y.md)", '<a href="y.md"><code>x</code></a>'),
        "a block quote": ("> quoted\n", "<blockquote>\n<p>quoted</p>\n</blockquote>"),
        "an HTML comment is left out": ("<!-- hidden -->\ntext\n", "<p>text</p>"),
    }
    for name, (text, wanted) in cases.items():
        output = rendered(text)
        passed = wanted in output and (name != "an HTML comment is left out" or "hidden" not in output)
        failures += not passed
        print(f"{'PASS' if passed else f'FAIL'} markdown: {name}" + ("" if passed else f": got {output!r}"))


def redirects() -> None:
    """The preview's redirects stay on the preview and keep the request out of the header line."""
    global failures
    handler = load_script("build-site").PreviewHandler

    class Response:
        def __init__(self) -> None:
            self.headers: dict[str, str] = {}

        def send_response(self, code: int) -> None:
            pass

        def send_header(self, name: str, value: str) -> None:
            self.headers[name] = value

        def end_headers(self) -> None:
            pass

    cases = {
        "a path is kept as it is": ("/neura/docs/", "/neura/docs/"),
        "an escape stays one escape": ("/neura/a%20b/", "/neura/a%20b/"),
        "a line break is encoded": ("/neura\r\nSet-Cookie: x/", "/neura%0D%0ASet-Cookie:%20x/"),
        "a second leading slash cannot name a host": ("//example.com/", "/example.com/"),
    }
    for name, (location, wanted) in cases.items():
        response = Response()
        handler.redirect(response, location)
        same(f"site: preview redirect: {name}", response.headers.get("Location", ""), wanted)


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


RULESET = {"name": "main", "target": "branch", "enforcement": "active", "bypass_actors": [],
           "rules": [{"type": "deletion"}, {"type": "pull_request", "parameters": {"required_approving_review_count": 0}}]}


def settings(label: str, change: dict[str, object], problem: str | None) -> None:
    """Run the settings check on saved responses; `change` replaces saved files by name."""
    saved: dict[str, object] = {
        "actions_permissions.json": {"enabled": True, "allowed_actions": "selected", "extra": 1},
        "alerts.json": {},
        "rulesets.json": [{"id": 7, "name": "main"}],
        # GitHub returns more fields than a file names, and may order rules differently.
        "rulesets_7.json": dict(RULESET, id=7, rules=list(reversed(RULESET["rules"]))),  # type: ignore[call-overload]
    }
    saved.update(change)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        (root / ".github" / "settings").mkdir(parents=True)
        (root / "live").mkdir()
        (root / ".github" / "settings" / "repository.json").write_text(json.dumps({
            "actions/permissions": {"enabled": True, "allowed_actions": "selected"}, "alerts": {}}), encoding="utf-8")
        (root / ".github" / "settings" / "ruleset-main.json").write_text(json.dumps(RULESET), encoding="utf-8")
        for name, value in saved.items():
            if value is not None:
                (root / "live" / name).write_text(json.dumps(value), encoding="utf-8")
        expect(f"settings: {label}", [sys.executable, str(SCRIPTS / "check-repository-settings.py"), "--root", directory,
                                      "--live", str(root / "live")], problem)


def main() -> int:
    settings("settings that match", {}, None)
    settings("a changed value", {"actions_permissions.json": {"enabled": True, "allowed_actions": "all"}},
             'actions/permissions.allowed_actions: expected "selected", found "all"')
    settings("a missing value", {"actions_permissions.json": {"enabled": True}}, "allowed_actions: missing")
    settings("a setting that cannot be read", {"alerts.json": None}, "alerts: cannot read")
    settings("a ruleset that was deleted", {"rulesets.json": []}, 'no ruleset named "main" exists')
    settings("a ruleset without a file", {"rulesets.json": [{"id": 7, "name": "main"}, {"id": 8, "name": "other"}]},
             '"other" exists on GitHub and has no file')
    settings("a ruleset with a rule removed", {"rulesets_7.json": dict(RULESET, rules=[{"type": "deletion"}])},
             "expected 2 entries, found 1")
    settings("a ruleset with a weaker rule", {"rulesets_7.json": dict(RULESET, rules=[
        {"type": "deletion"}, {"type": "pull_request", "parameters": {"required_approving_review_count": 1}}])},
        "no entry matches")
    settings("a ruleset with a bypass actor", {"rulesets_7.json": dict(RULESET, bypass_actors=[{"actor_id": 5}])},
             "bypass_actors: expected 0 entries, found 1")
    settings("a disabled ruleset", {"rulesets_7.json": dict(RULESET, enforcement="disabled")},
             'enforcement: expected "active", found "disabled"')

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

    site("a site that follows the rules", {}, None)
    home("a missing file", 'src="assets/logo.png"', 'src="assets/none.png"', "site/assets/none.png does not exist")
    home("a file name with different capitals", 'href="assets/site.css"', 'href="assets/Site.css"',
         "site/assets/Site.css does not exist")
    home("a reference that starts with `/`", 'href="./"', 'href="/assets/site.css"', "starts with `/`")
    home("a link that leaves the site", 'href="./"', 'href="../README.md"', "leaves the site")
    home("a directory without a home page", 'href="guide/"', 'href="assets/"', "site/assets/index.html does not exist")
    home("a missing id in the same page", 'href="#start"', 'href="#nowhere"', 'no element with `id="nowhere"`')
    home("a missing id in another page", "guide/index.html#part", "guide/index.html#gone",
         'no element with `id="gone"` in site/guide/index.html')
    home("a stylesheet from another host", 'href="assets/site.css"', 'href="https://example.com/site.css"',
         "is loaded from another host")
    home("a script from another host without a scheme", "<body>", '<body><script src="//example.com/x.js"></script>',
         "is loaded from another host")
    home("a larger image from another host", 'srcset="assets/logo.png 2x"',
         'srcset="assets/logo.png, https://example.com/logo.png 2x"', "is loaded from another host")
    home("a missing file in a style element", "</head>",
         "<style>\nbody { background: url(assets/gone.png); }\n</style></head>",
         "site/index.html:8: `assets/gone.png`: site/assets/gone.png does not exist")
    home("a base element", "<title>", '<base href="https://example.com/"><title>', "must not have a `<base>` element")
    home("a repository link to a missing path", f"{BLOB}/docs/A.md", f"{BLOB}/docs/Gone.md",
         "docs/Gone.md does not exist")
    home("a link to a missing wiki page", f"{WIKI}/Page", f"{WIKI}/Nowhere", "no wiki page wiki/Nowhere.md")
    site("a missing file in a stylesheet", {"site/assets/site.css": 'body { background: url("gone.png"); }\n'},
         "site/assets/site.css:1: `gone.png`: site/assets/gone.png does not exist")
    site("a stylesheet imported from another host",
         {"site/assets/site.css": '@import "https://example.com/font.css";\n'}, "is loaded from another host")
    site("a listing that differs from its source", {"examples/hello.nra": "let x = 2 < 1;\n"},
         "the listing differs from `examples/hello.nra`")
    home("a listing whose source is missing", 'data-source="examples/hello.nra"', 'data-source="examples/gone.nra"',
         "which is not a file in the repository")
    site("a file whose name starts with a dot", {"site/.well-known/security.txt": "x"}, "starts with `.`")
    site("no home page", {"site/index.html": None}, "the site needs a home page")
    site("a missing-address page without a head", {"site/404.html": "<p>missing</p>\n"}, "has no `<head>`")

    documents("documents that follow the rules", {}, None)
    documents("a link to a document the site does not publish", {"docs/Guide.md": "# Guide\n\n[A](A.md)\n"},
              "which the site does not publish")
    unlisted = CONTENTS + '[unlisted]\ninclude = ["docs/*.md", "wiki/*.md"]\nexclude = ["wiki/Skip.md"]\n'
    documents("an unlisted document is published when it matches", {
        "site/contents.toml": unlisted, "docs/New.md": "# New\n\n[Guide](Guide.md)\n",
        "docs/Guide.md": GUIDE + "\n[New](New.md) and [the wiki's copy](../wiki/Ref.md)\n",
        "wiki/Ref.md": "# Reference\n", "wiki/Skip.md": "# Skip\n"}, None)
    documents("an excluded document is not published", {
        "site/contents.toml": unlisted, "wiki/Skip.md": "# Skip\n",
        "docs/Guide.md": GUIDE + "\n[Skip](../wiki/Skip.md)\n"}, "which the site does not publish")
    documents("a link to a missing heading", {"docs/Guide.md": "# Guide\n\n[R](Ref.md#nowhere)\n"},
              "names no heading there")
    documents("a link to a file that does not exist", {"docs/Guide.md": "# Guide\n\n[G](gone.nra)\n"},
              "which is not in the repository")
    documents("a listed document that does not exist",
              {"site/contents.toml": CONTENTS.replace("docs/Guide.md", "docs/Gone.md")}, "not a file in the repository")
    documents("a missing partial", {"site/index.html": HOME.replace("<body>", "<body>\n<!-- include: nothing -->")},
              "no partial")
    documents("a markdown include of a missing heading",
              {"site/index.html": HOME.replace("<body>", "<body>\n<!-- markdown: docs/Ref.md#gone -->")},
              "has no heading `#gone`")
    documents("a file of site/ where a document is published", {"site/docs/Guide.html": "<p>x</p>\n"},
              "has the path of a page the build generates")
    documents_build()
    markdown()
    redirects()
    site_build()

    print(f"{failures} unexpected results.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
