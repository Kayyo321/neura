#!/usr/bin/env python3
"""Compile or run every Neura example in the repository's documentation.

Each fenced ```neura block in README.md, docs/**/*.md, and prompts/**/*.md (the
agent skill) must be preceded, on the line above it or with one blank line
between, by an HTML comment that says how the block is checked:

  <!-- example: check -->            The block is a complete source file;
                                     `neura check` accepts it.
  <!-- example: run -->              The block is a complete program. The next
                                     fenced ```text block is its exact standard
                                     output under `neura build` and `neura run
                                     --deny-all`, which must exit with status 0.
  <!-- example: test -->             The block is a test module; `neura test`
                                     passes it.
  <!-- example: rejected: <text> --> `neura check` fails, and its diagnostic
                                     contains <text>.
  <!-- example: error: <CODE> -->    `neura build` fails, and the first error
                                     it reports has the code <CODE>, such as
                                     E0401. The explain pages in
                                     docs/diagnostics use it for their failing
                                     examples.
  <!-- example: fragment: <why> -->  The block is not a complete file, so it is
                                     not compiled. The reason is required.

A block with no directive, an unknown directive, a `fragment` without a reason,
a `rejected` without text, or a `run` without a following ```text block is a
problem. Each runs in a fresh temporary directory as `example.nra`, with
`NEURA_GPU=off` and a time limit.

Pass `--neura <binary>` to compile and run. `--directives-only` reads the
directives and needs no compiler. `--list` prints each block and its directive.
The exit status is 0 when every block passes and 1 otherwise. Each problem is
reported as `path:line: message`, with the line of the block's opening fence.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

TIMEOUT_SECONDS = 60

FENCE = re.compile(r"^( {0,3})(`{3,}|~{3,})\s*([^\s`]*)")
DIRECTIVE = re.compile(r"^<!--\s*example:\s*(.*?)\s*-->$")
KINDS = ("check", "run", "test", "rejected", "error", "fragment")
CODE = re.compile(r"^E\d{4}$")
FIRST_ERROR = re.compile(r"^.*?:\d+:\d+: error(?:\[(E\d{4})\])?: ", re.MULTILINE)


@dataclasses.dataclass
class Block:
    path: str
    line: int
    code: str
    kind: str | None = None
    argument: str = ""
    expected: str | None = None
    problems: list[str] = dataclasses.field(default_factory=list)


def fenced_blocks(lines: list[str]) -> list[tuple[int, str, str]]:
    """Every fenced block as (index of its opening line, language, content)."""
    found = []
    index = 0
    while index < len(lines):
        match = FENCE.match(lines[index])
        if not match:
            index += 1
            continue
        marker, language = match.group(2), match.group(3)
        end = index + 1
        while end < len(lines):
            closing = FENCE.match(lines[end])
            if closing and closing.group(2)[0] == marker[0] and len(closing.group(2)) >= len(marker) \
                    and lines[end].strip() == closing.group(2):
                break
            end += 1
        found.append((index, language, "\n".join(lines[index + 1:end]) + "\n"))
        index = end + 1
    return found


def parse_directive(text: str) -> tuple[str | None, str, str | None]:
    """The (kind, argument, problem) of the text after `example:`."""
    kind, _, argument = text.partition(":")
    kind, argument = kind.strip(), argument.strip()
    if kind not in KINDS:
        return None, "", f"unknown example directive `{text}`; expected one of {', '.join(KINDS)}"
    if kind in ("check", "run", "test") and argument:
        return None, "", f"`example: {kind}` takes no argument"
    if kind == "rejected" and not argument:
        return None, "", "`example: rejected` needs the diagnostic text it expects, as `rejected: <text>`"
    if kind == "error" and not CODE.match(argument):
        return None, "", "`example: error` needs the diagnostic code it expects, as `error: E0401`"
    if kind == "fragment" and not argument:
        return None, "", "`example: fragment` needs a reason, as `fragment: <reason>`"
    return kind, argument, None


def parse(path: str, text: str) -> list[Block]:
    """The Neura blocks of one Markdown file with their directives."""
    lines = text.split("\n")
    blocks = fenced_blocks(lines)
    result = []
    for position, (index, language, code) in enumerate(blocks):
        if language != "neura":
            continue
        block = Block(path, index + 1, code)
        before = index - 1
        if before >= 0 and not lines[before].strip():
            before -= 1
        match = DIRECTIVE.match(lines[before].strip()) if before >= 0 else None
        if not match:
            block.problems.append("a ```neura block has no `<!-- example: ... -->` directive above it")
        else:
            kind, argument, problem = parse_directive(match.group(1))
            if problem:
                block.problems.append(problem)
            else:
                block.kind, block.argument = kind, argument
        if block.kind == "run":
            following = blocks[position + 1] if position + 1 < len(blocks) else None
            if following is None or following[1] != "text":
                block.problems.append("`example: run` must be followed by a ```text block with the expected output")
            else:
                block.expected = following[2]
        result.append(block)
    return result


def markdown_files(root: Path) -> list[Path]:
    files = [root / "README.md"] if (root / "README.md").is_file() else []
    for directory in ("docs", "prompts"):
        if (root / directory).is_dir():
            files += sorted((root / directory).rglob("*.md"))
    return files


def execute(neura: str, command: list[str], directory: Path) -> subprocess.CompletedProcess[str]:
    environment = dict(os.environ, NEURA_GPU="off")
    return subprocess.run([neura, *command], cwd=directory, env=environment, capture_output=True, text=True,
                          timeout=TIMEOUT_SECONDS, check=False)


def verify(neura: str, block: Block) -> list[str]:
    """What is wrong with one block that has a valid directive."""
    with tempfile.TemporaryDirectory() as name:
        directory = Path(name)
        (directory / "example.nra").write_text(block.code, encoding="utf-8")
        try:
            if block.kind == "check":
                result = execute(neura, ["check", "example.nra"], directory)
                if result.returncode != 0:
                    return [f"`neura check` rejected the example:\n{indent(result.stdout + result.stderr)}"]
            elif block.kind == "rejected":
                result = execute(neura, ["check", "example.nra"], directory)
                if result.returncode == 0:
                    return ["`neura check` accepted an example that should be rejected"]
                if block.argument not in result.stdout + result.stderr:
                    return [f"the diagnostic does not contain `{block.argument}`:\n"
                            f"{indent(result.stdout + result.stderr)}"]
            elif block.kind == "error":
                result = execute(neura, ["build", "--error-format=short", "example.nra", "-o", "example.nvr"],
                                 directory)
                if result.returncode == 0:
                    return [f"`neura build` accepted an example that should fail with {block.argument}"]
                first = FIRST_ERROR.search(result.stderr)
                if not first or first.group(1) != block.argument:
                    return [f"the first error is not {block.argument}:\n{indent(result.stdout + result.stderr)}"]
            elif block.kind == "test":
                result = execute(neura, ["test", "example.nra"], directory)
                if result.returncode != 0:
                    return [f"`neura test` failed:\n{indent(result.stdout + result.stderr)}"]
            elif block.kind == "run":
                built = execute(neura, ["build", "example.nra", "-o", "example.nvr"], directory)
                if built.returncode != 0:
                    return [f"`neura build` failed:\n{indent(built.stdout + built.stderr)}"]
                result = execute(neura, ["run", "--deny-all", "example.nvr"], directory)
                if result.returncode != 0:
                    return [f"`neura run` exited with status {result.returncode}:\n"
                            f"{indent(result.stdout + result.stderr)}"]
                if result.stdout != block.expected:
                    return ["the output differs from the ```text block after the example.\n"
                            f"  expected: {block.expected!r}\n  actual:   {result.stdout!r}"]
        except subprocess.TimeoutExpired:
            return [f"`neura {block.kind}` did not finish in {TIMEOUT_SECONDS} seconds"]
    return []


def indent(text: str) -> str:
    return "\n".join("    " + line for line in text.rstrip().split("\n")[:20])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="the repository checkout (default: this script's repository)")
    parser.add_argument("--neura", help="the neura binary that compiles and runs the examples")
    parser.add_argument("--directives-only", action="store_true",
                        help="check only that every block has a valid directive; no compiler is needed")
    parser.add_argument("--list", action="store_true", help="print each block with its directive and exit")
    arguments = parser.parse_args()
    if not arguments.neura and not arguments.directives_only and not arguments.list:
        parser.error("pass --neura <binary>, or --directives-only")

    root = arguments.root.resolve()
    neura = str(Path(arguments.neura).resolve()) if arguments.neura else ""
    blocks = []
    for path in markdown_files(root):
        blocks += parse(path.relative_to(root).as_posix(), path.read_text(encoding="utf-8"))

    if arguments.list:
        for block in blocks:
            what = block.kind or "MISSING"
            print(f"{block.path}:{block.line}: {what}{': ' + block.argument if block.argument else ''}")
        return 0

    problems = 0
    counts: dict[str, int] = {}
    for block in blocks:
        messages = list(block.problems)
        if not messages and block.kind and block.kind != "fragment" and not arguments.directives_only:
            messages = verify(neura, block)
        for message in messages:
            print(f"{block.path}:{block.line}: {message}")
        problems += bool(messages)
        if block.kind:
            counts[block.kind] = counts.get(block.kind, 0) + 1
    summary = ", ".join(f"{count} {kind}" for kind, count in sorted(counts.items())) or "none"
    action = "read" if arguments.directives_only else "compiled or run"
    print(f"{len(blocks)} Neura examples {action} ({summary}); {problems} with problems.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
