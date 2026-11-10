#!/usr/bin/env python3
"""Check the links in the repository's Markdown files.

For every Markdown file tracked by Git, each link must resolve inside the
checkout:

  * A relative link names a file or directory that exists, relative to the
    linking file.
  * A wiki link (a bare page name in `wiki/`, or a URL below
    https://github.com/<repository>/wiki/) names a page in `wiki/`.
  * A link to https://github.com/<repository>/blob/main/<path> or
    .../tree/main/<path> names a path that exists in the checkout.
  * A `#fragment` on any of these names a heading of the target Markdown
    file, using GitHub's heading anchors. `#L10`-style line fragments are
    not checked.

Links to other hosts are not fetched: this check never uses the network, so
its result depends only on the checkout. `--list-external` prints them.

The exit status is 0 when every checked link resolves and 1 otherwise. Each
problem is reported as `path:line: message`.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import unicodedata
from pathlib import Path
from urllib.parse import unquote, urlsplit

REPOSITORY = "Kayyo321/neura"
DEFAULT_BRANCH = "main"
WIKI_DIRECTORY = "wiki"

FENCE = re.compile(r"^ {0,3}(`{3,}|~{3,})")
ATX_HEADING = re.compile(r"^ {0,3}(#{1,6})[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$")
INLINE_CODE = re.compile(r"(`+)(.+?)\1")
INLINE_LINK = re.compile(r"!?\[(?:[^\[\]]|\[[^\[\]]*\])*\]\(\s*(<[^>]*>|[^\s)]+)(?:\s+(?:\"[^\"]*\"|'[^']*'))?\s*\)")
REFERENCE_DEFINITION = re.compile(r"^ {0,3}\[[^\]]+\]:\s*(<[^>]*>|\S+)")
HTML_REFERENCE = re.compile(r"""<(?:a|img)\b[^>]*?\b(?:href|src)\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
HTML_ANCHOR = re.compile(r"""<a\b[^>]*?\b(?:name|id)\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
LINE_FRAGMENT = re.compile(r"^L\d+(?:C\d+)?(?:-L\d+(?:C\d+)?)?$")


def tracked_markdown(root: Path) -> list[Path]:
    """Markdown files tracked by Git, in a stable order."""
    listing = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--", "*.md"],
        check=True,
        capture_output=True,
    ).stdout.decode("utf-8")
    return sorted(root / name for name in listing.split("\0") if name)


def prose_lines(text: str) -> list[tuple[int, str]]:
    """The lines outside fenced code blocks, with their 1-based numbers."""
    lines = []
    fence = None
    for number, line in enumerate(text.splitlines(), start=1):
        match = FENCE.match(line)
        if fence is None:
            if match:
                fence = match.group(1)
                continue
            lines.append((number, line))
        elif match and match.group(1)[0] == fence[0] and len(match.group(1)) >= len(fence):
            if line.strip() == match.group(1):
                fence = None
    return lines


def heading_text(source: str) -> str:
    """The text GitHub renders for a heading, without inline markup.

    A code span's content is literal: `List<T>` keeps its `<T>`, which
    outside a code span would be an HTML tag and render as nothing.
    """
    pieces = []
    position = 0
    for match in INLINE_CODE.finditer(source):
        pieces.append(plain_text(source[position:match.start()]))
        pieces.append(match.group(2))
        position = match.end()
    pieces.append(plain_text(source[position:]))
    return "".join(pieces).strip()


def plain_text(text: str) -> str:
    """Heading text outside code spans, with links, tags, and emphasis removed."""
    text = re.sub(r"!?\[((?:[^\[\]]|\[[^\[\]]*\])*)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"(\*\*|__)(.+?)\1", r"\2", text)
    text = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"\1", text)
    text = re.sub(r"(?<![\w_])_(?!\s)(.+?)(?<!\s)_(?![\w_])", r"\1", text)
    return text


def slug(text: str) -> str:
    """GitHub's anchor for a heading's text, before duplicate numbering."""
    kept = []
    for character in text.lower():
        category = unicodedata.category(character)
        if character in "-_ " or category[0] in "LN" or category in ("Mn", "Mc", "Pc"):
            kept.append(character)
    return "".join(kept).replace(" ", "-")


def anchors(text: str) -> set[str]:
    """Every fragment a Markdown file answers to."""
    found: set[str] = set()
    counts: dict[str, int] = {}
    for _, line in prose_lines(text):
        for match in HTML_ANCHOR.finditer(line):
            found.add(match.group(1))
        match = ATX_HEADING.match(line)
        if not match:
            continue
        base = slug(heading_text(match.group(2)))
        seen = counts.get(base, 0)
        counts[base] = seen + 1
        found.add(base if seen == 0 else f"{base}-{seen}")
    return found


def links(text: str) -> list[tuple[int, str]]:
    """Each link target in a Markdown file, with its line number."""
    found = []
    for number, line in prose_lines(text):
        definition = REFERENCE_DEFINITION.match(line)
        if definition:
            found.append((number, definition.group(1)))
            continue
        visible = INLINE_CODE.sub("", line)
        for pattern in (INLINE_LINK, HTML_REFERENCE):
            for match in pattern.finditer(visible):
                found.append((number, match.group(1)))
    return [(number, target.strip("<>")) for number, target in found]


class Checker:
    def __init__(self, root: Path, repository: str) -> None:
        self.root = root
        self.repository_url = f"https://github.com/{repository}"
        self.wiki = root / WIKI_DIRECTORY
        self.anchor_cache: dict[Path, set[str]] = {}
        self.problems: list[str] = []
        self.external: list[str] = []
        self.checked = 0

    def anchors_of(self, path: Path) -> set[str]:
        if path not in self.anchor_cache:
            self.anchor_cache[path] = anchors(path.read_text(encoding="utf-8"))
        return self.anchor_cache[path]

    def report(self, source: Path, line: int, message: str) -> None:
        self.problems.append(f"{source.relative_to(self.root).as_posix()}:{line}: {message}")

    def inside(self, path: Path) -> bool:
        try:
            path.resolve().relative_to(self.root.resolve())
        except ValueError:
            return False
        return True

    def check_fragment(self, source: Path, line: int, target: str, path: Path, fragment: str) -> None:
        if not fragment or path.suffix.lower() != ".md" or not path.is_file():
            return
        if LINE_FRAGMENT.match(fragment):
            return
        if unquote(fragment).lower() not in self.anchors_of(path):
            name = path.relative_to(self.root).as_posix()
            self.report(source, line, f"link `{target}`: no heading `#{fragment}` in {name}")

    def check_path(self, source: Path, line: int, target: str, path: Path, fragment: str) -> None:
        if not self.inside(path):
            self.report(source, line, f"link `{target}` leaves the repository")
        elif not path.exists():
            self.report(source, line, f"link `{target}`: {path.relative_to(self.root).as_posix()} does not exist")
        else:
            self.check_fragment(source, line, target, path, fragment)

    def check_wiki_page(self, source: Path, line: int, target: str, page: str, fragment: str) -> None:
        page = unquote(page).strip("/")
        path = self.wiki / f"{page or 'Home'}.md"
        if not path.is_file():
            self.report(source, line, f"link `{target}`: no wiki page {WIKI_DIRECTORY}/{page}.md")
        else:
            self.check_fragment(source, line, target, path, fragment)

    def check(self, source: Path, line: int, target: str) -> None:
        parts = urlsplit(target)
        in_wiki = source.parent == self.wiki
        if parts.scheme in ("http", "https"):
            url = f"{parts.scheme}://{parts.netloc}{parts.path}"
            if url == self.repository_url or not url.startswith(self.repository_url + "/"):
                self.external.append(target)
                return
            rest = url[len(self.repository_url) + 1 :]
            if rest == "wiki" or rest.startswith("wiki/"):
                self.checked += 1
                self.check_wiki_page(source, line, target, rest[len("wiki") :], parts.fragment)
            elif rest.startswith((f"blob/{DEFAULT_BRANCH}/", f"tree/{DEFAULT_BRANCH}/")):
                self.checked += 1
                relative = unquote(rest.split("/", 2)[2])
                self.check_path(source, line, target, self.root / relative, parts.fragment)
            else:
                self.external.append(target)
            return
        if parts.scheme:
            self.external.append(target)
            return
        self.checked += 1
        if not parts.path:
            self.check_fragment(source, line, target, source, parts.fragment)
        elif in_wiki:
            self.check_wiki_page(source, line, target, parts.path, parts.fragment)
        else:
            self.check_path(source, line, target, source.parent / unquote(parts.path), parts.fragment)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="the repository checkout (default: this script's repository)")
    parser.add_argument("--repository", default=REPOSITORY,
                        help="the GitHub repository, as owner/name, whose wiki and blob links are checked "
                             f"(default: {REPOSITORY})")
    parser.add_argument("--list-external", action="store_true",
                        help="print the links to other hosts, which are not checked")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    checker = Checker(root, arguments.repository)
    files = tracked_markdown(root)
    for path in files:
        for line, target in links(path.read_text(encoding="utf-8")):
            checker.check(path, line, target)

    for problem in checker.problems:
        print(problem)
    if arguments.list_external:
        for target in sorted(set(checker.external)):
            print(f"external: {target}")
    print(f"Checked {checker.checked} links in {len(files)} Markdown files; "
          f"{len(checker.problems)} broken, {len(checker.external)} external links not fetched.")
    return 1 if checker.problems else 0


if __name__ == "__main__":
    sys.exit(main())
