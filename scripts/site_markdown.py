"""Render the Markdown of Neura's documentation as HTML, for the website.

This is the part of GitHub Flavored Markdown that the repository's documents
use: ATX headings, paragraphs, fenced code, tables, block quotes, flat and
nested lists, thematic breaks, `<details>` blocks, code spans, links,
autolinks, emphasis, and character references. It needs only the Python
standard library, so the site builds without installing anything.

Headings receive the `id` that GitHub gives them, so a link such as
`Types.md#references` reaches the same heading on the website as on GitHub.
Raw HTML is not copied through, apart from `<details>`, `<summary>`, and
`<br>`: other tags are shown as text, as a reader of the Markdown sees them.

`scripts/site_docs.py` decides which documents are published and where each
link leads; it passes this module a function that rewrites each link.
docs/Site.md describes the whole build.
"""

from __future__ import annotations

import html
import re
import unicodedata
from dataclasses import dataclass, field
from typing import Callable

# A function that takes a link target and the line of the Markdown that holds
# it, and returns the target to publish.
LinkRewriter = Callable[[str, int], str]

FENCE = re.compile(r"^( {0,3})(`{3,}|~{3,})\s*([^`\s]*)[^`]*$")
HEADING = re.compile(r"^ {0,3}(#{1,6})(?:[ \t]+(.*?))?(?:[ \t]+#+)?[ \t]*$")
BREAK = re.compile(r"^ {0,3}([-*_])(?:[ \t]*\1){2,}[ \t]*$")
BULLET = re.compile(r"^( {0,3})([-*+])( {1,4}|$)")
ORDERED = re.compile(r"^( {0,3})(\d{1,9})([.)])( {1,4}|$)")
QUOTE = re.compile(r"^ {0,3}> ?")
DELIMITER_ROW = re.compile(r"^ {0,3}\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$")
HTML_BLOCK = re.compile(r"^ {0,3}<(/?)([A-Za-z][A-Za-z0-9]*|!--)(?=[\s/>]|$)")
PASSED_TAGS = {"details", "summary", "br"}
INLINE_TAG = re.compile(r"<(/?)(br|kbd|sup|sub)\s*/?>", re.IGNORECASE)
AUTOLINK = re.compile(r"<((?:https?|mailto):[^\s<>]+)>")
ENTITY = re.compile(r"&(?:#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|[A-Za-z][A-Za-z0-9]{1,31});")
BARE_URL = re.compile(r"(?<![\w/\"'=])https?://[^\s<>\x00]*[^\s<>\x00?!.,:;*_~)'\"]")
PUNCTUATION = set("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~")
PLACEHOLDER = re.compile(r"\x00(\d+)\x00")
STRONG = [re.compile(r"\*\*(?=\S)(.+?)(?<=\S)\*\*"), re.compile(r"(?<![\w])__(?=\S)(.+?)(?<=\S)__(?![\w])")]
EMPHASIS = [re.compile(r"(?<![*\w])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?!\*)"),
            re.compile(r"(?<![\w])_(?=\S)(.+?)(?<=\S)_(?![\w])")]
TAGS = re.compile(r"<[^>]*>")


def slug(text: str) -> str:
    """The `id` GitHub gives a heading whose text is `text`."""
    text = text.strip().lower()
    kept = []
    for character in text:
        category = unicodedata.category(character)
        if character in " -_" or category[0] in "LN" or category == "Mn":
            kept.append(character)
    return "".join(kept).replace(" ", "-")


def plain(fragment: str) -> str:
    """The text of an HTML fragment, without its tags."""
    return html.unescape(TAGS.sub("", fragment))


@dataclass
class Heading:
    level: int
    html: str
    text: str
    id: str


@dataclass
class Document:
    html: str
    headings: list[Heading] = field(default_factory=list)
    # The text of the first paragraph, for a page's description.
    summary: str = ""

    @property
    def title(self) -> str:
        first = next((heading for heading in self.headings if heading.level == 1), None)
        return first.text if first else ""


@dataclass
class Line:
    number: int
    text: str


class Renderer:
    """Renders one Markdown document."""

    def __init__(self, rewrite: LinkRewriter, highlight: Callable[[str, str], str | None] | None = None,
                 skip_leading_html: bool = False) -> None:
        self.rewrite = rewrite
        self.highlight = highlight
        self.skip_leading_html = skip_leading_html
        self.headings: list[Heading] = []
        self.used_ids: dict[str, int] = {}
        self.summary = ""
        self.started = False

    # Blocks ---------------------------------------------------------------

    def render(self, text: str) -> Document:
        lines = [Line(number, raw.expandtabs(4)) for number, raw in enumerate(text.split("\n"), 1)]
        body = self.blocks(lines, tight=False)
        return Document(body, self.headings, self.summary)

    def blocks(self, lines: list[Line], tight: bool) -> str:
        out: list[str] = []
        index = 0
        while index < len(lines):
            line = lines[index]
            text = line.text
            if not text.strip():
                index += 1
                continue

            fence = FENCE.match(text)
            if fence:
                index = self.code(lines, index, fence, out)
                continue
            heading = HEADING.match(text)
            if heading:
                out.append(self.heading(len(heading.group(1)), heading.group(2) or "", line.number))
                index += 1
                continue
            if BREAK.match(text):
                out.append("<hr>")
                index += 1
                continue
            if QUOTE.match(text):
                index = self.quote(lines, index, out)
                continue
            if BULLET.match(text) or ORDERED.match(text):
                index = self.list(lines, index, out)
                continue
            tag = HTML_BLOCK.match(text)
            if tag:
                index = self.html_block(lines, index, tag, out)
                continue
            if "|" in text and index + 1 < len(lines) and DELIMITER_ROW.match(lines[index + 1].text) \
                    and "-" in lines[index + 1].text:
                index = self.table(lines, index, out)
                continue
            index = self.paragraph(lines, index, out, tight)
        return "\n".join(out)

    def interrupts(self, text: str) -> bool:
        """Whether a line starts a block that ends a paragraph."""
        return bool(FENCE.match(text) or HEADING.match(text) or BREAK.match(text) or QUOTE.match(text)
                    or (BULLET.match(text) and text.strip() not in "-*+")
                    or (ORDERED.match(text) and ORDERED.match(text).group(2) == "1")
                    or (HTML_BLOCK.match(text) and HTML_BLOCK.match(text).group(2).lower() in PASSED_TAGS | {"!--"}))

    def paragraph(self, lines: list[Line], index: int, out: list[str], tight: bool) -> int:
        start = lines[index].number
        collected = [lines[index].text.strip()]
        index += 1
        while index < len(lines) and lines[index].text.strip() and not self.interrupts(lines[index].text):
            if "|" in lines[index].text and index + 1 < len(lines) and DELIMITER_ROW.match(lines[index + 1].text):
                break
            collected.append(lines[index].text.strip())
            index += 1
        content = self.inline("\n".join(collected), start)
        if not self.summary and not tight:
            self.summary = plain(content).replace("\n", " ")
        self.started = True
        out.append(content if tight else f"<p>{content}</p>")
        return index

    def heading(self, level: int, text: str, number: int) -> str:
        content = self.inline(text.strip(), number)
        words = plain(content)
        base = slug(words)
        count = self.used_ids.get(base, 0)
        self.used_ids[base] = count + 1
        identifier = base if count == 0 else f"{base}-{count}"
        self.headings.append(Heading(level, content, words, identifier))
        self.started = True
        anchor = f'<a class="anchor" href="#{identifier}" aria-label="Link to this section"></a>' if level > 1 else ""
        return f'<h{level} id="{identifier}">{content}{anchor}</h{level}>'

    def code(self, lines: list[Line], index: int, fence: re.Match, out: list[str]) -> int:
        indent, marker, language = len(fence.group(1)), fence.group(2), fence.group(3).lower()
        body: list[str] = []
        index += 1
        while index < len(lines):
            text = lines[index].text
            stripped = text.lstrip(" ")
            if stripped.startswith(marker[0] * len(marker)) and not stripped.strip(marker[0]).strip() \
                    and len(text) - len(stripped) < 4:
                index += 1
                break
            body.append(text[min(indent, len(text) - len(text.lstrip(" "))):])
            index += 1
        source = "\n".join(body)
        shown = self.highlight(language, source) if self.highlight else None
        if shown is None:
            shown = html.escape(source, quote=False)
        label = f' data-language="{html.escape(language)}"' if language else ""
        out.append(f"<pre{label}><code>{shown}</code></pre>")
        self.started = True
        return index

    def quote(self, lines: list[Line], index: int, out: list[str]) -> int:
        inner: list[Line] = []
        while index < len(lines):
            text = lines[index].text
            marker = QUOTE.match(text)
            if marker:
                inner.append(Line(lines[index].number, text[marker.end():]))
            elif text.strip() and inner and inner[-1].text.strip() and not self.interrupts(text):
                inner.append(lines[index])
            else:
                break
            index += 1
        out.append(f"<blockquote>\n{self.blocks(inner, tight=False)}\n</blockquote>")
        return index

    def list(self, lines: list[Line], index: int, out: list[str]) -> int:
        first = BULLET.match(lines[index].text) or ORDERED.match(lines[index].text)
        ordered = first.re is ORDERED
        kind = first.group(3) if ordered else first.group(2)
        start = int(first.group(2)) if ordered else 1
        items: list[list[Line]] = []
        loose = False
        while index < len(lines):
            marker = (ORDERED if ordered else BULLET).match(lines[index].text)
            if not marker or (marker.group(3) if ordered else marker.group(2)) != kind:
                break
            spaces = marker.group(4 if ordered else 3)
            width = marker.end() - (len(spaces) - 1 if len(spaces) > 4 else 0)
            if not spaces:
                width += 1
            item = [Line(lines[index].number, lines[index].text[width:] if len(lines[index].text) > width else "")]
            index += 1
            fenced = bool(FENCE.match(item[0].text))
            while index < len(lines):
                text = lines[index].text
                indent = len(text) - len(text.lstrip(" "))
                if not text.strip():
                    item.append(Line(lines[index].number, ""))
                elif indent >= width:
                    item.append(Line(lines[index].number, text[width:]))
                elif fenced or item[-1].text.strip() == "" or self.interrupts(text):
                    break
                else:
                    item.append(Line(lines[index].number, text.strip()))
                if FENCE.match(item[-1].text):
                    fenced = not fenced
                index += 1
            # Blank lines at the end of an item separate it from what follows.
            trailing = 0
            while item and not item[-1].text.strip():
                item.pop()
                trailing += 1
            if any(not line.text.strip() for line in item[1:]):
                blocks_inside = [line for line in item if line.text.strip()]
                if len(blocks_inside) != len(item):
                    loose = True
            items.append(item)
            if trailing:
                next_marker = index < len(lines) and (ORDERED if ordered else BULLET).match(lines[index].text)
                if next_marker:
                    loose = True
                else:
                    index -= trailing
                    break
        rendered = []
        for item in items:
            rendered.append(f"<li>{self.blocks(item, tight=not loose)}</li>")
        tag = "ol" if ordered else "ul"
        attribute = f' start="{start}"' if ordered and start != 1 else ""
        out.append(f"<{tag}{attribute}>\n" + "\n".join(rendered) + f"\n</{tag}>")
        self.started = True
        return index

    def html_block(self, lines: list[Line], index: int, tag: re.Match, out: list[str]) -> int:
        name = tag.group(2).lower()
        collected = []
        if name == "!--":
            while index < len(lines):
                collected.append(lines[index].text)
                index += 1
                if "-->" in collected[-1]:
                    break
            return index
        while index < len(lines) and lines[index].text.strip():
            collected.append(lines[index])
            index += 1
        if self.skip_leading_html and not self.started:
            return index
        if name in PASSED_TAGS:
            for line in collected:
                text = line.text.strip()
                match = re.fullmatch(r"<(/?)(details|summary)>(.*?)(?:</summary>)?", text, re.IGNORECASE)
                if match and match.group(2).lower() == "summary" and not match.group(1):
                    out.append(f"<summary>{self.inline(match.group(3), line.number)}</summary>")
                elif match:
                    out.append(f"<{match.group(1)}{match.group(2).lower()}>")
                else:
                    out.append(self.inline(text, line.number))
            return index
        start = collected[0].number
        out.append(f"<p>{self.inline(chr(10).join(line.text.strip() for line in collected), start)}</p>")
        return index

    def table(self, lines: list[Line], index: int, out: list[str]) -> int:
        header = self.cells(lines[index].text)
        aligns = []
        for cell in self.cells(lines[index + 1].text):
            cell = cell.strip()
            if cell.startswith(":") and cell.endswith(":"):
                aligns.append("center")
            elif cell.endswith(":"):
                aligns.append("right")
            else:
                aligns.append("")
        number = lines[index].number
        index += 2

        def row(cells: list[str], tag: str, line: int) -> str:
            parts = []
            for column in range(len(header)):
                cell = cells[column] if column < len(cells) else ""
                align = aligns[column] if column < len(aligns) and aligns[column] else ""
                attribute = f' class="align-{align}"' if align else ""
                parts.append(f"<{tag}{attribute}>{self.inline(cell.strip(), line)}</{tag}>")
            return "<tr>" + "".join(parts) + "</tr>"

        body = []
        while index < len(lines) and lines[index].text.strip() and not self.interrupts(lines[index].text):
            body.append(row(self.cells(lines[index].text), "td", lines[index].number))
            index += 1
        out.append('<div class="table"><table>\n<thead>' + row(header, "th", number) + "</thead>\n<tbody>\n"
                   + "\n".join(body) + "\n</tbody></table></div>")
        self.started = True
        return index

    @staticmethod
    def cells(text: str) -> list[str]:
        text = text.strip()
        if text.startswith("|"):
            text = text[1:]
        if text.endswith("|") and not text.endswith("\\|"):
            text = text[:-1]
        cells, current, escaped = [], [], False
        for character in text:
            if escaped:
                current.append(character if character == "|" else "\\" + character)
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == "|":
                cells.append("".join(current))
                current = []
            else:
                current.append(character)
        if escaped:
            current.append("\\")
        cells.append("".join(current))
        return cells

    # Inlines --------------------------------------------------------------

    def inline(self, text: str, line: int) -> str:
        held: list[str] = []
        literal: list[str] = []

        def hold(fragment: str) -> None:
            literal.append(f"\x00{len(held)}\x00")
            held.append(fragment)

        index = 0
        while index < len(text):
            character = text[index]
            if character == "\\" and index + 1 < len(text) and text[index + 1] in PUNCTUATION:
                hold(html.escape(text[index + 1], quote=False))
                index += 2
            elif character == "`":
                run = len(text[index:]) - len(text[index:].lstrip("`"))
                ticks = text[index:index + run]
                end = self.closing_ticks(text, index + run, ticks)
                if end < 0:
                    hold(ticks)
                    index += run
                else:
                    code = text[index + run:end].replace("\n", " ")
                    if code.startswith(" ") and code.endswith(" ") and code.strip():
                        code = code[1:-1]
                    hold(f"<code>{html.escape(code, quote=False)}</code>")
                    index = end + run
            elif character == "<":
                autolink = AUTOLINK.match(text, index)
                tag = INLINE_TAG.match(text, index)
                if autolink:
                    target = self.rewrite(autolink.group(1), line)
                    hold(f'<a href="{html.escape(target)}">{html.escape(autolink.group(1), quote=False)}</a>')
                    index = autolink.end()
                elif tag:
                    name = tag.group(2).lower()
                    hold("<br>" if name == "br" else f"<{tag.group(1)}{name}>")
                    index = tag.end()
                else:
                    literal.append("<")
                    index += 1
            elif character == "&":
                entity = ENTITY.match(text, index)
                if entity:
                    hold(entity.group())
                    index = entity.end()
                else:
                    literal.append("&")
                    index += 1
            elif character == "[" or (character == "!" and text.startswith("[", index + 1)):
                link = self.link(text, index, line)
                if link:
                    fragment, index = link
                    hold(fragment)
                else:
                    literal.append(character)
                    index += 1
            else:
                literal.append(character)
                index += 1

        result = html.escape("".join(literal), quote=False)
        result = BARE_URL.sub(lambda match: self.bare_link(match.group(), line), result)
        for pattern in STRONG:
            result = pattern.sub(r"<strong>\1</strong>", result)
        for pattern in EMPHASIS:
            result = pattern.sub(r"<em>\1</em>", result)
        while PLACEHOLDER.search(result):
            result = PLACEHOLDER.sub(lambda match: held[int(match.group(1))], result)
        return result

    def bare_link(self, url: str, line: int) -> str:
        target = self.rewrite(html.unescape(url), line)
        return f'<a href="{html.escape(target)}">{url}</a>'

    @staticmethod
    def closing_ticks(text: str, start: int, ticks: str) -> int:
        index = start
        while True:
            found = text.find(ticks, index)
            if found < 0:
                return -1
            after = found + len(ticks)
            if (after >= len(text) or text[after] != "`") and (found == 0 or text[found - 1] != "`"):
                return found
            index = after + 1 if after < len(text) else after
            while index < len(text) and text[index] == "`":
                index += 1

    def link(self, text: str, index: int, line: int) -> tuple[str, int] | None:
        image = text[index] == "!"
        opening = index + 1 if image else index
        depth, cursor = 0, opening
        while cursor < len(text):
            character = text[cursor]
            if character == "\\":
                cursor += 2
                continue
            if character == "`":
                run = len(text[cursor:]) - len(text[cursor:].lstrip("`"))
                end = self.closing_ticks(text, cursor + run, "`" * run)
                cursor = cursor + run if end < 0 else end + run
                continue
            if character == "[":
                depth += 1
            elif character == "]":
                depth -= 1
                if depth == 0:
                    break
            cursor += 1
        else:
            return None
        label = text[opening + 1:cursor]
        if not text.startswith("(", cursor + 1):
            return None
        # The destination: balanced parentheses, or text in angle brackets, then an optional title.
        position = cursor + 2
        while position < len(text) and text[position] == " ":
            position += 1
        if text.startswith("<", position):
            close = text.find(">", position)
            if close < 0:
                return None
            destination = text[position + 1:close]
            position = close + 1
        else:
            depth, begin = 0, position
            while position < len(text) and not text[position].isspace():
                if text[position] == "(":
                    depth += 1
                elif text[position] == ")":
                    if depth == 0:
                        break
                    depth -= 1
                elif text[position] == "\\":
                    position += 1
                position += 1
            destination = text[begin:position]
        title = ""
        title_match = re.match(r'\s+(?:"([^"]*)"|\'([^\']*)\')', text[position:])
        if title_match:
            title = title_match.group(1) or title_match.group(2) or ""
            position += title_match.end()
        while position < len(text) and text[position] == " ":
            position += 1
        if not text.startswith(")", position):
            return None
        destination = re.sub(r"\\([!-/:-@\[-`{-~])", r"\1", destination)
        target = html.escape(self.rewrite(destination, line))
        title_attribute = f' title="{html.escape(title)}"' if title else ""
        if image:
            return f'<img src="{target}" alt="{html.escape(plain(self.inline(label, line)))}"{title_attribute}>', \
                position + 1
        return f'<a href="{target}"{title_attribute}>{self.inline(label, line)}</a>', position + 1


def render(text: str, rewrite: LinkRewriter, highlight: Callable[[str, str], str | None] | None = None,
           skip_leading_html: bool = False) -> Document:
    """Render a Markdown document; see the module's description."""
    return Renderer(rewrite, highlight, skip_leading_html).render(text)
