#!/usr/bin/env python3
"""Refresh source-linked public declaration lists on the standard-library wiki.

Run from any directory with `python3 scripts/update-stdlib-wiki-api.py`.
Use `--check` to report pages that would change without writing them.
Only the marked SOURCE API block is changed; surrounding prose is maintained
by hand. Internal modules, members of non-exported classes, and declarations
tagged `#Hidden` are omitted; `#Deprecated` and `#Since` tags, and the tags that
constrain callers, such as `#NoDiscard` and `#Final`, are shown in the notes.
"""

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STDLIB = ROOT / "stdlib" / "std"
WIKI = ROOT / "wiki"
BEGIN = "<!-- BEGIN SOURCE API -->"
END = "<!-- END SOURCE API -->"


def split_tags(text):
    """Split the tags written at the start of a line (docs/lang/Tags.md) from
    the rest of it: (["Deprecated(as_of: \"0.2.0\")", "Since(\"0.1.0\")"], rest).
    A group `#[A, B]` gives one entry per tag."""
    tags = []
    rest = text.lstrip()
    while rest.startswith("#"):
        depth, quote, index = 0, False, 1
        while index < len(rest):
            char = rest[index]
            if quote:
                if char == "\\":
                    index += 1
                elif char == '"':
                    quote = False
            elif char == '"':
                quote = True
            elif char in "([":
                depth += 1
            elif char in ")]":
                depth -= 1
            elif depth == 0 and (char.isspace() or char == "#"):
                break
            index += 1
        body = rest[1:index]
        if body.startswith("[") and body.endswith("]"):
            depth, quote, start = 0, False, 1
            for position in range(1, len(body) - 1):
                char = body[position]
                if quote:
                    quote = char != '"' or body[position - 1] == "\\"
                elif char == '"':
                    quote = True
                elif char in "([":
                    depth += 1
                elif char in ")]":
                    depth -= 1
                elif char == "," and depth == 0:
                    tags.append(body[start:position].strip())
                    start = position + 1
            if body[start:-1].strip():
                tags.append(body[start:-1].strip())
        elif body:
            tags.append(body)
        rest = rest[index:].lstrip()
    return tags, rest


def tag_name(tag):
    """The name of a tag as split_tags returns it."""
    return re.match(r"[A-Za-z_]\w*", tag)[0] if re.match(r"[A-Za-z_]\w*", tag) else ""


def tags_above(lines, start):
    """The tags on the declaration at `start`: on its own line and on the tag
    lines directly above it, which may be mixed with comment lines."""
    tags = split_tags(lines[start])[0]
    for index in range(start - 1, -1, -1):
        text = lines[index].strip()
        if text.startswith("#"):
            tags = split_tags(text)[0] + tags
        elif not text.startswith("//"):
            break
    return tags


# Tags that change how a caller may use a declaration, listed after its notes as they are written.
CALLER_TAGS = ("Experimental", "NoDiscard", "Final", "Sealed", "Pure", "Immutable", "ThreadSafe",
               "NotThreadSafe", "Synchronized")


def tag_notes(tags):
    """Notes for the tags a reader of the API needs: deprecation, the tags that
    constrain callers (docs/lang/Tags.md), and since."""
    notes = []
    shown = [f"`#{tag}`" for tag in tags if tag_name(tag) in CALLER_TAGS]
    if shown:
        notes.append("Tagged " + ", ".join(shown) + ".")
    for tag in tags:
        name = tag_name(tag)
        arguments = dict(re.findall(r'(\w+):\s*"([^"]*)"', tag))
        positional = re.findall(r'\(\s*"([^"]*)"', tag)
        if name == "Deprecated":
            note = "Deprecated"
            if arguments.get("as_of"):
                note += f" since {arguments['as_of']}"
            if arguments.get("reason"):
                note += f": {arguments['reason']}"
            instead = arguments.get("use_instead") or (re.findall(r"use_instead:\s*([\w.]+)", tag) or [""])[0]
            if instead:
                note += f"; use `{instead}` instead"
            notes.append(note + ".")
        elif name == "Since":
            version = arguments.get("version") or (positional[0] if positional else "")
            if version:
                notes.append(f"Since {version}.")
    return notes


def comment_notes(lines, start):
    """Read adjacent prose without exposing Doxygen markup in the wiki."""
    comments = []
    for index in range(start - 1, -1, -1):
        if lines[index].strip().startswith("#"):
            # A tag line between the comment and the declaration.
            continue
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
    text = split_tags(lines[start])[1].strip()
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
    for number, line in enumerate(lines[start:]):
        chunk = (split_tags(line)[1] if number == 0 else line).strip()
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


ACCESSOR = re.compile(r"^\s*\|\s*(get|set)\s*:")


def accessors(lines, start):
    """The accessor summary of a field (Types.md, Accessors): its `| get:` and
    `| set:` clause lines follow the declaration, more deeply indented."""
    indent = len(lines[start]) - len(lines[start].lstrip())
    kinds = set()
    clause_indent = None
    for line in lines[start + 1:]:
        if not line.strip() or line.lstrip().startswith("//"):
            continue
        depth = len(line) - len(line.lstrip())
        if clause_indent is not None and (depth > clause_indent or
                                          (depth == clause_indent and line.lstrip().startswith("}"))):
            continue  # The block body of the previous clause.
        clause = ACCESSOR.match(line)
        if not clause or depth <= indent:
            break
        kinds.add(clause.group(1))
        clause_indent = depth
    if not kinds:
        return ""
    return " { " + "; ".join(kind for kind in ("get", "set") if kind in kinds) + " }"


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
        line = " " * indent + split_tags(line)[1]
        if not line.strip():
            # A line of tags only; they belong to the declaration below.
            continue
        tags = [tag_name(tag) for tag in tags_above(lines, index)]
        while owners and owners[-1][0] >= indent:
            owners.pop()
        container = re.match(
            r"^(\s*)(export\s+)?let(?:\.[A-Za-z_]+)*\s+(\w+)\s*=\s*(?:module|class|interface)\b", line
        )
        # `last` is contextual: it declares only when a name follows it.
        match = re.match(r"^\s*export\s+(?:static\s+)?(?:let(?:\.[A-Za-z_]+)*|const|last(?=\s+[A-Za-z_]))\s+(\w+)\b", line)
        owner = ".".join(item[1] for item in owners)
        public = all(item[2] for item in owners)
        hidden = "Hidden" in tags
        if container:
            owners.append((indent, container.group(3), bool(container.group(2)) and public and not hidden))
        if not match or not public or hidden or (indent == 0 and match.group(1) == module):
            continue
        notes = comment_notes(lines, index) + tag_notes(tags_above(lines, index))
        yield index + 1, owner, declaration(lines, index) + accessors(lines, index), notes


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
