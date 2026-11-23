#!/usr/bin/env python3
"""Refresh source-linked public declaration lists and mirrored contracts on the standard-library wiki.

Run from any directory with `python3 scripts/update-stdlib-wiki-api.py`.
Use `--check` to report pages that would change without writing them.
The numbered sections under a page's "Detailed contracts" heading are copies of the
sections of docs/lang/Stdlib.md with the same number; they are refreshed too, with links
rewritten as scripts/generate-wiki.py rewrites them (a same-page link points at the
Stdlib-Design page). Every other part of a page is maintained by hand. Internal modules, members of non-exported classes, and declarations
tagged `#Hidden` are omitted; `#Deprecated` and `#Since` tags, and the tags that
constrain callers, such as `#NoDiscard` and `#Final`, are shown in the notes.
"""

import argparse
import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STDLIB = ROOT / "stdlib" / "std"
WIKI = ROOT / "wiki"
BEGIN = "<!-- BEGIN SOURCE API -->"
END = "<!-- END SOURCE API -->"
SPEC = "docs/lang/Stdlib.md"
CONTRACTS = "Detailed contracts"
HEADING = re.compile(r"^(#{1,6}) (.*)$")
NUMBERED = re.compile(r"^(\d+(?:\.\d+)*)\.?\s")
TAG_OPEN = re.compile(r"[#.][ \t]*(?=[A-Za-z_\[])")


def split_tags(text, sigils=None):
    """Split the tags written at the start of a line (docs/lang/Tags.md) from
    the rest of it: (["Deprecated(as_of: \"0.2.0\")", "Since(\"0.1.0\")"], rest).
    Either sigil can open a group, which gives one entry per tag."""
    tags = []
    rest = text.lstrip()
    while (opening := TAG_OPEN.match(rest)) is not None:
        if sigils is not None:
            sigils.append(len(text) - len(rest))
        start = opening.end()
        depth, quote, index = 0, False, start
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
            elif depth == 0 and (char.isspace() or char in "#.;/"):
                break
            index += 1
        body = rest[start:index]
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
        found, rest = split_tags(text)
        if found and (not rest or rest.startswith("//")):
            tags = found + tags
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
        tags, rest = split_tags(lines[index])
        if tags and (not rest or rest.startswith("//")):
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
    tag_position = False
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
            if tag_position and not char.isspace():
                if char in "#.":
                    # Parameter and record-component tags that remain in
                    # signatures are printed with the canonical sigil.
                    sigils = []
                    split_tags(chunk[index:], sigils)
                    letters = list(chunk)
                    for sigil in sigils:
                        letters[index + sigil] = "#"
                    chunk = "".join(letters)
                    char = chunk[index]
                tag_position = False
            if char == '"':
                in_string = True
                out.append(char)
                index += 1
                continue
            if paren == 0 and bracket == 0 and (pair == "=>" or char in "{;"):
                return "".join(out).strip()
            if char in "(,":
                tag_position = True
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


def load_generator():
    """scripts/generate-wiki.py, whose link rewriting the mirrored sections reuse."""
    path = Path(__file__).resolve().parent / "generate-wiki.py"
    spec = importlib.util.spec_from_file_location("generate_wiki", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def headings(lines, generator):
    """(index, level, text) of each heading outside fenced code blocks."""
    found = []
    fence = None
    for index, line in enumerate(lines):
        match = generator.FENCE.match(line)
        if match:
            if fence is None:
                fence = match.group(1)
            elif match.group(1)[0] == fence[0] and len(match.group(1)) >= len(fence) and line.strip() == match.group(1):
                fence = None
            continue
        if fence is None and (heading := HEADING.match(line)):
            found.append((index, len(heading.group(1)), heading.group(2)))
    return found


def section_bounds(lines, found, position):
    """The line range of the section at `found[position]`, without its trailing blank lines: it runs
    to the next heading of the same or a higher level."""
    start, level, _ = found[position]
    end = next((index for index, other, _ in found[position + 1:] if other <= level), len(lines))
    while end > start + 1 and not lines[end - 1].strip():
        end -= 1
    return start, end


def spec_sections(text, generator, root):
    """The numbered sections of the specification, by number, as the wiki spells them: links
    rewritten, and a `##` section demoted to `###` because it sits under "## Detailed contracts"."""
    lines = text.split("\n")
    found = headings(lines, generator)
    sections = {}
    for position, (start, level, title) in enumerate(found):
        number = NUMBERED.match(title)
        if not number:
            continue
        first, end = section_bounds(lines, found, position)
        shift = max(level, 3) - level
        body = list(lines[first:end])
        for index, inner, inner_title in found[position:]:
            if index >= end:
                break
            body[index - first] = "#" * (inner + shift) + " " + inner_title
        rewritten = generator.rewrite_links("\n".join(body), SPEC, root, generator.REPOSITORY,
                                            generator.GENERATED[SPEC])
        sections[number.group(1)] = rewritten.split("\n")
    return sections


def mirror_sections(name, text, spec, generator, root=ROOT):
    """The page `text` with its mirrored sections refreshed from the specification text `spec`,
    and one problem line per section that differed or has no counterpart."""
    sections = spec_sections(spec, generator, root)
    lines = text.split("\n")
    found = headings(lines, generator)
    contracts = next((position for position, (_, level, title) in enumerate(found)
                      if level == 2 and title == CONTRACTS), None)
    if contracts is None:
        return text, []
    problems = []
    edits = []
    for position in range(contracts + 1, len(found)):
        start, level, title = found[position]
        if level <= 2:
            break
        number = NUMBERED.match(title)
        if not number:
            continue
        _, end = section_bounds(lines, found, position)
        wanted = sections.get(number.group(1))
        if wanted is None:
            problems.append(f"wiki/{name}: section \"{'#' * level} {title}\" has no counterpart in {SPEC}")
            continue
        current = lines[start:end]
        if current != wanted:
            line = next((i for i in range(min(len(current), len(wanted))) if current[i] != wanted[i]),
                        min(len(current), len(wanted)))
            have = current[line] if line < len(current) else "(end of section)"
            want = wanted[line] if line < len(wanted) else "(end of section)"
            problems.append(f"wiki/{name}: section \"{'#' * level} {title}\" differs from {SPEC} at page line "
                            f"{start + line + 1}: page has \"{have[:70]}\", specification has \"{want[:70]}\"")
            edits.append((start, end, wanted))
    for start, end, wanted in reversed(edits):
        lines[start:end] = wanted
    return "\n".join(lines), problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report stale API blocks without writing")
    args = parser.parse_args()
    stale = []
    errors = []
    generator = load_generator()
    spec = (ROOT / SPEC).read_text(encoding="utf-8")
    expected = set()
    areas = set()
    count = 0
    for source in sorted(STDLIB.rglob("*.nra")):
        if "internal" in source.relative_to(STDLIB).parts:
            continue
        lines = source.read_text(encoding="utf-8").splitlines()
        match = next((found for line in lines if not line[:1].isspace()
                      if (found := re.match(r"^export\s+let\s+(\w+)\s*=\s*module\b", split_tags(line)[1]))), None)
        if match is None:
            errors.append(f"No public module declaration: {source.relative_to(ROOT)}")
            continue
        module = match.group(1)
        parts = source.relative_to(STDLIB).parts
        if len(parts) > 1:
            areas.add(area_name(parts[0]))
        # Every directory names a level: net/core/uri.nra is Stdlib-Net-Core-Uri.
        name = "-".join(["Stdlib", *(area_name(part) for part in parts[:-1]), module]) + ".md"
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
        updated, problems = mirror_sections(name, updated, spec, generator)
        for problem in problems:
            if args.check or "has no counterpart" in problem:
                errors.append(problem)
        count += 1
        if updated != text:
            stale.append(page.relative_to(ROOT))
            if not args.check:
                page.write_text(updated, encoding="utf-8", newline="\n")
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
