#!/usr/bin/env python3
"""Check that every compiler diagnostic has a code, a page, and a test.

The registry of codes is src/util/diagnostic_codes.def. This check reads the
sources and documents only, so it needs no build:

  * Every construction of a diagnostic in src/ gives it a code: a
    `Diagnostic(...)` or `MissingPrimitiveWrapper(...)` expression is followed
    by `.with_code("E....")`, and a declaration such as `Diagnostic error(...)`
    is followed, within its next three lines, by `error.with_code("E....")`.
    A construction that must not have a code (the model's own helpers, a
    forwarding constructor) carries the comment `// diagnostic-code: none:
    <reason>` on its line or the line above. Warnings
    (`Diagnostic::warning(...)`) have a name and take a `W....` code with
    `.with_code`, which is not checked.
  * Every code given in src/ is registered, and every registered code is
    given somewhere in src/.
  * The registry also lists warnings, `NEURA_WARNING(W...., "name", "title")`;
    they follow the same rules as errors below.
  * Every registered code has its explain page, docs/diagnostics/<CODE>.md,
    whose first line is `# <CODE>: <title>` with the registered title, and
    every page in docs/diagnostics/ is for a registered code. The index
    docs/diagnostics/README.md links to every page.
  * Every registered code is exercised: its page has a failing example
    (`<!-- example: error: <CODE> -->`, which the doc-examples test compiles)
    or a UI test under tests/ui expects it (`error[<CODE>]` in a .stderr
    file). A warning's page has `<!-- example: warning: <CODE> -->` instead,
    and a UI test shows `warning[<CODE>]`. A page whose code no single source file can cause says so with
    `<!-- diagnostic: not reproducible: <reason> -->` instead of a failing
    example, and is then exempt; the reason is required.
  * Every page has at least one example that compiles (`check`, `run`, or
    `test`) or a fragment with its reason: the fix.

The exit status is 0 when every check passes and 1 otherwise. Each problem is
reported as `path:line: message`.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ENTRY = re.compile(r'^NEURA_(?:DIAGNOSTIC\((E\d{4})|WARNING\((W\d{4}), "[a-z0-9-]+"), "([^"]*)"\)', re.MULTILINE)
CONSTRUCTION = re.compile(r"(?<![\w:.])(?:Diagnostic|MissingPrimitiveWrapper)\(")
DECLARATION = re.compile(r"(?<![\w:.])(?:Diagnostic|MissingPrimitiveWrapper)\s+(\w+)\(")
WITH_CODE = re.compile(r'\s*\.with_code\("(E\d{4})"\)')
CODE_GIVEN = re.compile(r'with_code\("([EW]\d{4})"\)')
EXEMPT = re.compile(r"//\s*diagnostic-code:\s*none:\s*\S")
FAILING = re.compile(r"<!--\s*example:\s*(?:error|warning):\s*([EW]\d{4})\s*-->")
UNREPRODUCIBLE = re.compile(r"<!--\s*diagnostic:\s*not reproducible:\s*(.*?)\s*-->")
FIX = re.compile(r"<!--\s*example:\s*(check|run|test|fragment:\s*\S.*?)\s*-->")
UI_CODE = re.compile(r"^(?:error|warning)\[([EW]\d{4})\]", re.MULTILINE)
PARAMETER = re.compile(r"^\s*(?:const\s+)?[A-Za-z_][\w:<>]*(?:<[^()]*>)?\s*[&*]*\s+[A-Za-z_]\w*\s*(?:,|=|$)")


def closing(text: str, start: int) -> int:
    """The index after the parenthesis that closes the one before `start`."""
    depth, index, quote = 1, start, None
    while depth and index < len(text):
        c = text[index]
        if quote:
            if c == "\\":
                index += 2
                continue
            if c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        index += 1
    return index


def without_comments(text: str) -> str:
    """`text` with each // comment blanked, keeping offsets and lines."""
    out = []
    for line in text.split("\n"):
        quote, index = None, 0
        while index < len(line):
            c = line[index]
            if quote:
                if c == "\\":
                    index += 1
                elif c == quote:
                    quote = None
            elif c in "\"'":
                quote = c
            elif line.startswith("//", index):
                line = line[:index] + " " * (len(line) - index)
                break
            index += 1
        out.append(line)
    return "\n".join(out)


def function_parameters(arguments: str) -> bool:
    """Whether the text in parentheses after `Diagnostic name` declares
    parameters (a function returning a diagnostic) rather than passing
    arguments (a variable being constructed)."""
    return not arguments.strip() or bool(PARAMETER.match(arguments))


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def exempt(lines: list[str], line: int) -> bool:
    return any(EXEMPT.search(lines[i]) for i in (line - 1, line - 2) if 0 <= i < len(lines))


def check_sources(root: Path, registered: dict[str, str]) -> tuple[list[str], set[str]]:
    problems, given = [], set()
    for path in sorted((root / "src").rglob("*")):
        if path.suffix not in (".cpp", ".hpp") or not path.is_file():
            continue
        original = path.read_text(encoding="utf-8")
        lines = original.split("\n")
        text = without_comments(original)
        name = path.relative_to(root).as_posix()
        for code in CODE_GIVEN.findall(text):
            given.add(code)
            if code not in registered:
                problems.append(f"{name}:{line_of(text, text.index(code))}: {code} is not in the registry")
        for match in DECLARATION.finditer(text):
            line = line_of(text, match.start())
            if exempt(lines, line):
                continue
            variable = match.group(1)
            if function_parameters(text[match.end():closing(text, match.end()) - 1]):
                continue
            following = "\n".join(lines[line - 1:line + 3])
            if not re.search(rf"\b{re.escape(variable)}\.with_code\(\"E\d{{4}}\"\)", following):
                problems.append(f"{name}:{line}: the diagnostic '{variable}' is given no code")
        for match in CONSTRUCTION.finditer(text):
            line = line_of(text, match.start())
            before = text[max(0, match.start() - 2):match.start()]
            if exempt(lines, line) or before.endswith("::"):
                continue
            end = closing(text, match.end())
            if not WITH_CODE.match(text, end):
                problems.append(f"{name}:{line}: a diagnostic is constructed without .with_code(\"E....\")")
    return problems, given


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="the repository checkout (default: this script's repository)")
    root = parser.parse_args().root.resolve()

    registry = root / "src/util/diagnostic_codes.def"
    registered = {error or warning: title
                  for error, warning, title in ENTRY.findall(registry.read_text(encoding="utf-8"))}
    problems, given = check_sources(root, registered)
    for code in sorted(set(registered) - given):
        problems.append(f"{registry.relative_to(root).as_posix()}:1: {code} is registered but no diagnostic uses it")

    pages = root / "docs/diagnostics"
    exercised = set()
    for stderr in sorted((root / "tests/ui").rglob("*.stderr")):
        exercised.update(UI_CODE.findall(stderr.read_text(encoding="utf-8")))
    for path in sorted((root / "docs").rglob("*.md")):
        exercised.update(FAILING.findall(path.read_text(encoding="utf-8")))

    index_path = pages / "README.md"
    index = index_path.read_text(encoding="utf-8") if index_path.is_file() else ""
    if not index:
        problems.append("docs/diagnostics/README.md:1: the index of codes is missing")
    for code, title in sorted(registered.items()):
        page = pages / f"{code}.md"
        name = f"docs/diagnostics/{code}.md"
        if not page.is_file():
            problems.append(f"{name}:1: {code} has no explain page")
            continue
        text = page.read_text(encoding="utf-8")
        heading = f"# {code}: {title}"
        if text.split("\n", 1)[0] != heading:
            problems.append(f"{name}:1: the first line must be `{heading}`")
        if index and f"({code}.md)" not in index:
            problems.append(f"docs/diagnostics/README.md:1: the index does not link to {code}.md")
        failing = FAILING.findall(text)
        if any(found != code for found in failing):
            problems.append(f"{name}:1: a failing example expects another code than {code}")
        unreproducible = UNREPRODUCIBLE.search(text)
        if unreproducible and not unreproducible.group(1):
            problems.append(f"{name}:1: `diagnostic: not reproducible` needs a reason")
        if not failing and not unreproducible:
            kind = "warning" if code.startswith("W") else "error"
            problems.append(f"{name}:1: the page has no failing example (`<!-- example: {kind}: {code} -->`)")
        if not FIX.search(text):
            problems.append(f"{name}:1: the page has no example of the fix")
        if code not in exercised and not unreproducible:
            problems.append(f"{name}:1: no UI test or documentation example exercises {code}")
    for page in sorted(pages.glob("*.md")):
        if page.name != "README.md" and page.stem not in registered:
            problems.append(f"docs/diagnostics/{page.name}:1: {page.stem} is not a registered code")

    for problem in problems:
        print(problem)
    print(f"Checked {len(registered)} diagnostic codes; {len(problems)} problems.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
