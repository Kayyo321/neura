#!/usr/bin/env python3
"""Refresh source-linked public declaration lists on the standard-library wiki.

Run from any directory with `python3 scripts/update-stdlib-wiki-api.py`.
Use `--check` to report pages that would change without writing them.
Only the marked SOURCE API block is changed; surrounding prose is maintained
by hand. Internal modules and members of non-exported classes are omitted.
"""

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STDLIB = ROOT / "stdlib" / "std"
WIKI = ROOT / "wiki"
BEGIN = "<!-- BEGIN SOURCE API -->"
END = "<!-- END SOURCE API -->"


def comment_notes(lines, start):
    """Read adjacent prose without exposing Doxygen markup in the wiki."""
    comments = []
    for index in range(start - 1, -1, -1):
        match = re.match(r"^\s*(///|//)\s?(.*)$", lines[index])
        if not match:
            break
        comments.append((match[1], match[2].strip()))

    notes = []
    include = True
    for prefix, comment in reversed(comments):
        if prefix == "///" and (command := re.match(r"^[@\\](\w+)\b\s*(.*)$", comment)):
            include = command[1] in {"brief", "details", "pre", "return", "note", "warning"}
            comment = command[2]
        elif prefix == "//" or not comment:
            include = True
        if include and comment:
            notes.append(comment)
    return notes


def declaration(lines, start):
    """Return a declaration through its signature, excluding its body."""
    text = lines[start].strip()
    if re.search(r"=\s*(?:module|class|interface)\b", text):
        # The opening parenthesis starts the module/class/interface body.
        offset = start
        while "(" not in text and offset + 1 < len(lines):
            offset += 1
            text += " " + lines[offset].strip()
        return text.split("(", 1)[0].rstrip()

    paren = bracket = 0
    in_string = escaped = False
    out = []
    for line in lines[start:]:
        chunk = line.strip()
        index = 0
        while index < len(chunk):
            char = chunk[index]
            pair = chunk[index:index + 2]
            if in_string:
                out.append(char)
                if escaped:
                    escaped = False
                elif char == "\\":
                    escaped = True
                elif char == '"':
                    in_string = False
                index += 1
                continue
            if pair == "//":
                break
            if char == '"':
                in_string = True
                out.append(char)
                index += 1
                continue
            if paren == 0 and bracket == 0 and (pair == "=>" or char in "{;"):
                return "".join(out).strip()
            if char == "(":
                paren += 1
            elif char == ")":
                paren -= 1
            elif char == "[":
                bracket += 1
            elif char == "]":
                bracket -= 1
            out.append(char)
            index += 1
        out.append(" ")
    raise ValueError(f"Unterminated declaration at line {start + 1}")


def area_name(directory):
    """The PascalCase area of a library directory: dynamic-foreign is DynamicForeign."""
    return "".join(word.capitalize() for word in directory.split("-"))


def exports(path, module):
    """Yield publicly reachable exported declarations and adjacent comments."""
    lines = path.read_text(encoding="utf-8").splitlines()
    owners = []
    for index, line in enumerate(lines):
        if not line.strip() or line.lstrip().startswith("//"):
            continue
        indent = len(line) - len(line.lstrip())
        while owners and owners[-1][0] >= indent:
            owners.pop()
        container = re.match(
            r"^(\s*)(export\s+)?let(?:\.[A-Za-z_]+)*\s+(\w+)\s*=\s*(?:module|class|interface)\b", line
        )
        match = re.match(r"^\s*export\s+(?:static\s+)?(?:let(?:\.[A-Za-z_]+)*|const)\s+(\w+)\b", line)
        owner = ".".join(item[1] for item in owners)
        public = all(item[2] for item in owners)
        if container:
            owners.append((indent, container.group(3), bool(container.group(2)) and public))
        if not match or not public or (indent == 0 and match.group(1) == module):
            continue
        yield index + 1, owner, declaration(lines, index), comment_notes(lines, index)


def render(path, module):
    source = path.relative_to(ROOT).as_posix()
    url = f"https://github.com/Kayyo321/neura/blob/main/{source}"
    output = [
        BEGIN,
        "The following public declarations are taken from the module source; each links to its definition. "
        "Notes reproduce adjacent source comments where present. Inherited members are documented "
        "on their declaring types. Consult the contracts and source for behavior, preconditions, and "
        "failures that are not stated here.",
        "",
    ]
    for line, owner, signature, comments in exports(path, module):
        signature = signature.replace("`", "\\`")
        prefix = f"**{owner}** · " if owner else ""
        output.append(f"- {prefix}[`{signature}`]({url}#L{line})")
        if comments:
            output.append("  " + " ".join(comments))
    output.append(END)
    return "\n".join(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report stale API blocks without writing")
    args = parser.parse_args()
    stale = []
    errors = []
    expected = set()
    areas = set()
    count = 0
    for source in sorted(STDLIB.rglob("*.nra")):
        if "internal" in source.relative_to(STDLIB).parts:
            continue
        lines = source.read_text(encoding="utf-8").splitlines()
        match = next((re.match(r"^export\s+let\s+(\w+)\s*=\s*module\b", line) for line in lines
                      if re.match(r"^export\s+let\s+(\w+)\s*=\s*module\b", line)), None)
        if match is None:
            errors.append(f"No public module declaration: {source.relative_to(ROOT)}")
            continue
        module = match.group(1)
        parts = source.relative_to(STDLIB).parts
        if len(parts) > 1:
            areas.add(area_name(parts[0]))
        name = f"Stdlib-{area_name(parts[0])}-{module}.md" if len(parts) > 1 else f"Stdlib-{module}.md"
        expected.add(name)
        page = WIKI / name
        if not page.exists():
            errors.append(f"Missing wiki page: {page.relative_to(ROOT)}")
            continue
        text = page.read_text(encoding="utf-8")
        if text.count(BEGIN) != 1 or text.count(END) != 1 or text.index(BEGIN) >= text.index(END):
            errors.append(f"Missing or invalid API markers: {page.relative_to(ROOT)}")
            continue
        start = text.index(BEGIN)
        end = text.index(END) + len(END)
        updated = text[:start] + render(source, module) + text[end:]
        count += 1
        if updated != text:
            stale.append(page.relative_to(ROOT))
            if not args.check:
                page.write_text(updated, encoding="utf-8")
    for page in WIKI.glob("Stdlib-*.md"):
        if page.name in expected:
            continue
        is_module_page = page.name in {"Stdlib-Program.md", "Stdlib-Test.md"} or any(
            page.name.startswith(f"Stdlib-{area}-") for area in areas
        )
        if is_module_page:
            errors.append(f"Wiki module page has no matching public module: {page.relative_to(ROOT)}")
    for error in errors:
        print(error)
    for page in stale:
        print(f"{'Stale' if args.check else 'Updated'}: {page}")
    print(f"Checked {count} module pages; {len(stale)} {'stale' if args.check else 'updated'}, {len(errors)} errors.")
    return 1 if errors or (args.check and stale) else 0


if __name__ == "__main__":
    raise SystemExit(main())
