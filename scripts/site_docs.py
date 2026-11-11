"""Assemble the website: the files of `site/`, and the repository's documents.

`scripts/build-site.py` calls `assemble`, which returns every published file
by its path below the site's address. Three things happen here:

  * Partials. A page in `site/` may contain `<!-- include: NAME -->`, which
    is replaced by `site/partials/NAME.html`. In a partial, `{{root}}` is the
    path from the page to the top of the site (`""`, `"../"`, ...) and
    `{{home}}` is the same path or `./`. The head, masthead, and footer that
    pages share are written once, as partials.
  * Markdown includes. `<!-- markdown: README.md#getting-started -->` is
    replaced by that section of the document, rendered, with its heading.
    An empty fragment (`README.md#`) names the text before the first heading.
    `without-code` after the fragment leaves out the section's fenced code.
  * Documents. `site/contents.toml` lists the repository's Markdown documents
    that the site publishes, grouped into the sections of the documentation's
    navigation. Each is rendered into a page with the documentation layout.
    A link between published documents leads to the published page. A link
    to another text file of the repository leads to a page that shows the
    file with line numbers, so `#L21` reaches line 21. A link to a directory
    or an image leads to GitHub.

`partials/` and `contents.toml` are not published themselves. docs/Site.md
describes the site, and how to add a document or a section.
"""

from __future__ import annotations

import hashlib
import html
import json
import posixpath
import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable
from urllib.parse import unquote, urlsplit

import site_markdown

SITE = "site"
PARTIALS = "partials"
CONTENTS = "contents.toml"
DEFAULT_BRANCH = "main"

INCLUDE = re.compile(r"<!--\s*include:\s*([A-Za-z0-9_-]+)\s*-->")
MARKDOWN_INCLUDE = re.compile(r"<!--\s*markdown:\s*([^#\s]+)#([^\s]*)(\s+without-code)?\s*-->")
# A fenced code block, with its fences, in Markdown text.
FENCED_CODE = re.compile(r"^( {0,3})(`{3,}|~{3,})[^\n]*\n.*?^ {0,3}\2[`~]*[ \t]*(?:\n|\Z)", re.MULTILINE | re.DOTALL)
# Text files a reader can open in the site's source view, by their suffix.
SOURCE_VIEW = {".nra", ".cpp", ".hpp", ".h", ".c", ".py", ".yml", ".yaml", ".toml", ".json", ".txt", ".cmake",
               ".html", ".css", ".js", ".sh", ".ir"}
SOURCE_NAMES = {"CMakeLists.txt", "LICENSE"}
NEURA_KEYWORDS = {
    "let", "const", "export", "override", "func", "module", "class", "import", "new", "destroy", "if", "else",
    "while", "do", "for", "in", "break", "continue", "return", "true", "false", "as", "is", "nil", "enum",
    "match", "and", "or", "interface", "implements", "promote", "static", "singleton",
}
NEURA_TOKEN = re.compile(
    r'(?P<c>//[^\n]*|/\*.*?\*/)|(?P<s>"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\')'
    r"|(?P<n>\b\d[\d_]*(?:\.\d+)?\b)|(?P<w>\b[A-Za-z_][A-Za-z0-9_]*\b)",
    re.DOTALL,
)
# The word of an accessor clause (Types.md, Accessors), followed by its colon.
ACCESSOR_CLAUSE = re.compile(r"(?:get|set)\s*:")
# References that a page or stylesheet loads, which `fingerprint` versions.
PAGE_REFERENCE = re.compile(r'\b(href|src|srcset|data-index)="([^"]*)"')
CSS_URL = re.compile(r'url\("([^"]+)"\)')
API_ITEM = re.compile(r'<li><strong>([^<]*)</strong> · <a href="([^"]*)"><code>([^<]*)</code></a>')
DECLARED_NAME = re.compile(r"\b(?:let|const|last)\s+([A-Za-z_][A-Za-z0-9_]*)")
# `last` is a contextual word: a keyword only when a name follows it, as in
# `last total: int;`, and an ordinary name in `last = 3;` or `last.next`.
CONTEXTUAL_KEYWORDS = {"last"}
CONTEXTUAL_FOLLOWER = re.compile(r"[ \t]+([A-Za-z_][A-Za-z0-9_]*)")

Report = Callable[[str, int, str], None]


def highlight(language: str, source: str) -> str | None:
    """Neura source as HTML with comments, strings, numbers, and keywords marked."""
    if language not in ("neura", "nra"):
        return None
    out, position = [], 0
    for match in NEURA_TOKEN.finditer(source):
        out.append(html.escape(source[position:match.start()], quote=False))
        kind, text = match.lastgroup, match.group()
        if kind == "w" and text in CONTEXTUAL_KEYWORDS:
            follower = CONTEXTUAL_FOLLOWER.match(source, match.end())
            keyword = follower is not None and follower.group(1) not in NEURA_KEYWORDS
        else:
            keyword = text in NEURA_KEYWORDS
        # `get` and `set` are keywords only as an accessor clause, `| get:`.
        if (kind == "w" and text in ("get", "set") and ACCESSOR_CLAUSE.match(source, match.start())
                and re.search(r"\|\s*$", source[:match.start()])):
            keyword = True
        if kind == "w" and not keyword:
            out.append(html.escape(text, quote=False))
        else:
            out.append(f'<span class="{"k" if kind == "w" else kind}">{html.escape(text, quote=False)}</span>')
        position = match.end()
    out.append(html.escape(source[position:], quote=False))
    return "".join(out)


def relative(page: str, target: str) -> str:
    """The reference from the published page `page` to the published path `target`, which may end in `#id`."""
    target, hash_sign, fragment = target.partition("#")
    return relative_path(page, target) + hash_sign + fragment


def relative_path(page: str, target: str) -> str:
    directory = posixpath.dirname(page)
    if target.endswith("/") or target == "":
        result = posixpath.relpath(target or ".", directory or ".")
        return "./" if result == "." else result + "/"
    return posixpath.relpath(target, directory or ".")


def root_of(page: str) -> str:
    return "../" * page.count("/")


def first_sentence(text: str, limit: int = 170) -> str:
    match = re.match(r"(.+?[.!?])(?:\s|$)", text)
    sentence = match.group(1) if match else text
    if len(sentence) > limit:
        sentence = sentence[: limit - 1].rsplit(" ", 1)[0] + "…"
    return sentence


@dataclass
class Document:
    """A Markdown document of the repository that the site publishes."""

    source: str
    published: str
    section: "Section"
    label: str = ""
    title: str = ""
    plate: str = ""
    group: str = ""
    rendered: site_markdown.Document | None = None
    body: str = ""
    members: list[tuple[str, str]] = field(default_factory=list)


@dataclass
class Section:
    title: str
    plate: str
    documents: list[Document] = field(default_factory=list)

    @property
    def id(self) -> str:
        return site_markdown.slug(self.title)


@dataclass
class Origin:
    """Where the lines of a published file came from, for reporting problems."""

    source: str
    # (first line in the published file, line in the source, partial name or "")
    segments: list[tuple[int, int, str]] = field(default_factory=list)
    # Whether the file was generated, so that its lines do not correspond to lines of the source.
    generated: bool = False

    def locate(self, line: int) -> tuple[str, int]:
        if self.generated:
            return self.source, 1
        found = (1, 1, "")
        for segment in self.segments:
            if segment[0] <= line:
                found = segment
        start, source_line, partial = found
        if partial:
            return f"{self.source} (in {SITE}/{PARTIALS}/{partial}.html)", source_line
        return self.source, source_line + line - start


class Assembly:
    def __init__(self, root: Path, repository_url: str, report: Report) -> None:
        self.root = root
        self.repository_url = repository_url
        self.report = report
        self.site = root / SITE
        self.files: dict[str, bytes] = {}
        self.origins: dict[str, Origin] = {}
        self.partials: dict[str, str] = {}
        partials = self.site / PARTIALS
        if partials.is_dir():
            for path in sorted(partials.glob("*.html")):
                self.partials[path.stem] = path.read_text(encoding="utf-8")
        self.sections: list[Section] = []
        self.documents: dict[str, Document] = {}  # by repository path
        self.wiki_aliases: dict[str, str] = {}
        self.home_title = "Documentation"
        self.home_summary = ""
        self.source_views: dict[str, str] = {}  # repository path -> published path
        self.fragments: list[tuple[str, int, str, str]] = []  # (source, line, published path, fragment)
        self.search: list[list] = []

    # Paths ----------------------------------------------------------------

    @staticmethod
    def published_path(source: str) -> str:
        stem = source[:-3]
        if stem == "wiki/Stdlib":
            return "stdlib/index.html"
        if stem.startswith("wiki/Stdlib-"):
            return f"stdlib/{stem[len('wiki/Stdlib-'):]}.html"
        if stem.startswith("wiki/"):
            return f"docs/{stem[len('wiki/'):]}.html"
        if stem.startswith("docs/"):
            return f"{stem}.html"
        return f"docs/{stem}.html"

    # Partials and includes -------------------------------------------------

    def expand(self, name: str, text: str, origin_name: str) -> str:
        """Replace include directives in a page of `site/`."""
        root = root_of(name)
        origin = Origin(origin_name, [(1, 1, "")])
        out: list[str] = []
        position, built_line, source_line = 0, 1, 1

        def advance(fragment: str) -> None:
            nonlocal built_line
            out.append(fragment)
            built_line += fragment.count("\n")

        pattern = re.compile(f"{INCLUDE.pattern}|{MARKDOWN_INCLUDE.pattern}")
        for match in pattern.finditer(text):
            before = text[position:match.start()]
            advance(before)
            source_line += before.count("\n")
            if match.group(1):
                partial = self.partials.get(match.group(1))
                if partial is None:
                    self.report(origin_name, source_line, f"no partial {SITE}/{PARTIALS}/{match.group(1)}.html")
                    replacement = ""
                else:
                    replacement = self.fill(partial, root)
                origin.segments.append((built_line, source_line, match.group(1)))
            else:
                replacement = self.markdown_section(name, match.group(2), match.group(3), origin_name,
                                                    source_line, without_code=bool(match.group(4)))
                origin.segments.append((built_line, source_line, ""))
            advance(replacement)
            origin.segments.append((built_line, source_line, ""))
            position = match.end()
        advance(text[position:])
        self.origins[name] = origin
        return "".join(out)

    @staticmethod
    def fill(partial: str, root: str) -> str:
        return partial.replace("{{root}}", root).replace("{{home}}", root or "./").rstrip("\n")

    def markdown_section(self, page: str, source: str, fragment: str, origin_name: str, line: int,
                         without_code: bool = False) -> str:
        path = self.root / source
        if not path.is_file():
            self.report(origin_name, line, f"the markdown include names `{source}`, which is not a file")
            return ""
        lines = path.read_text(encoding="utf-8").split("\n")
        heading = re.compile(r"^ {0,3}(#{1,6})\s+(.*?)\s*#*\s*$")
        start, level = None, 0
        if not fragment:
            start = 0
        in_fence = False
        for index, text in enumerate(lines):
            if re.match(r"^\s*(```|~~~)", text):
                in_fence = not in_fence
            match = None if in_fence else heading.match(text)
            if not match:
                continue
            if start is None:
                words = site_markdown.plain(site_markdown.render(match.group(2), lambda target, _: target).html)
                if site_markdown.slug(words) == fragment:
                    start, level = index, len(match.group(1))
            elif not fragment or len(match.group(1)) <= level:
                end = index
                break
        else:
            end = len(lines)
        if start is None:
            self.report(origin_name, line, f"`{source}` has no heading `#{fragment}`")
            return ""
        chosen = "\n".join(lines[start:end])
        if without_code:
            # The section's prose alone: a page can leave out a listing it does not need.
            chosen = FENCED_CODE.sub("", chosen)
        if fragment:
            # The section's heading becomes the section heading of the page.
            chosen = re.sub(r"^ {0,3}#{1,6}", "##", chosen, count=1)
        rendered = site_markdown.render(chosen, self.rewriter(source, page), highlight,
                                        skip_leading_html=not fragment)
        return rendered.html

    # Contents ---------------------------------------------------------------

    def load_contents(self) -> bool:
        path = self.site / CONTENTS
        if not path.is_file():
            return False
        try:
            contents = tomllib.loads(path.read_text(encoding="utf-8"))
        except tomllib.TOMLDecodeError as error:
            self.report(f"{SITE}/{CONTENTS}", 1, f"is not valid TOML: {error}")
            return False
        self.home_title = contents.get("title", self.home_title)
        self.home_summary = contents.get("summary", "")
        self.wiki_aliases = dict(contents.get("wiki", {}))
        for entry in contents.get("sections", []):
            section = Section(entry["title"], entry.get("plate", ""))
            self.sections.append(section)
            for page in entry.get("pages", []):
                page = {"source": page} if isinstance(page, str) else page
                self.add_document(section, page["source"], page.get("label", ""), page.get("title", ""),
                                  page.get("plate", ""))
            if "modules" in entry:
                for module in sorted(self.root.glob(entry["modules"])):
                    source = module.relative_to(self.root).as_posix()
                    if source in self.documents or posixpath.basename(source)[:-3] in self.wiki_aliases:
                        continue
                    name = posixpath.basename(source)[:-3].removeprefix("Stdlib-")
                    group, _, label = name.partition("-")
                    self.add_document(section, source, label or group, "", "", group if label else "")
        unlisted = contents.get("unlisted")
        if unlisted:
            self.add_unlisted(unlisted)
        return True

    def add_unlisted(self, entry: dict) -> None:
        """Publish the documents that match `include` and that no section lists, in a last section.

        A new document is then published as soon as it reaches `main`, before anyone files it in a section. A wiki
        page that presents a published document under the same name is left out, as are the patterns in `exclude`.
        """
        excluded = set()
        for pattern in entry.get("exclude", []):
            excluded.update(path.relative_to(self.root).as_posix() for path in self.root.glob(pattern))
        section = Section(entry.get("title", "More documents"), entry.get("plate", ""))
        found = set()
        for pattern in entry.get("include", []):
            found.update(path.relative_to(self.root).as_posix() for path in self.root.glob(pattern) if path.is_file())
        for source in sorted(found - excluded):
            if source in self.documents:
                continue
            if source.startswith("wiki/"):
                name = posixpath.basename(source)[:-3]
                if name in self.wiki_aliases or self.wiki_page(name) not in (None, source):
                    continue
            self.add_document(section, source, "", "", "")
        if section.documents:
            self.sections.append(section)

    def add_document(self, section: Section, source: str, label: str, title: str, plate: str,
                     group: str = "") -> None:
        if not (self.root / source).is_file():
            self.report(f"{SITE}/{CONTENTS}", 1, f"lists `{source}`, which is not a file in the repository")
            return
        if source in self.documents:
            self.report(f"{SITE}/{CONTENTS}", 1, f"lists `{source}` twice")
            return
        document = Document(source, self.published_path(source), section, label, title, plate, group)
        section.documents.append(document)
        self.documents[source] = document

    def ordered(self) -> list[Document]:
        return [document for section in self.sections for document in section.documents]

    # Links ------------------------------------------------------------------

    def wiki_page(self, name: str) -> str | None:
        """The repository path of the published document for a wiki page name."""
        if name in self.wiki_aliases:
            return self.wiki_aliases[name]
        if f"wiki/{name}.md" in self.documents:
            return f"wiki/{name}.md"
        for source in self.documents:
            if source.startswith("docs/") and posixpath.basename(source) == f"{name}.md":
                return source
        return None

    def rewriter(self, source: str, page: str) -> site_markdown.LinkRewriter:
        def rewrite(target: str, line: int) -> str:
            return self.rewrite(source, page, target, line)
        return rewrite

    def rewrite(self, source: str, page: str, target: str, line: int) -> str:
        try:
            parts = urlsplit(target)
        except ValueError:
            self.report(source, line, f"cannot read the link `{target}`")
            return target
        fragment = unquote(parts.fragment)
        if parts.scheme in ("http", "https") and parts.netloc == "github.com":
            url = f"https://github.com{parts.path}".rstrip("/")
            prefix = self.repository_url + "/"
            if url.startswith(prefix + "wiki") :
                name = unquote(url[len(prefix) + len("wiki"):]).strip("/") or "Home"
                return self.to_wiki(source, page, name, fragment, line, target)
            for kind in ("blob", "tree"):
                start = f"{prefix}{kind}/{DEFAULT_BRANCH}/"
                if (url + "/").startswith(start):
                    path = unquote(url[len(start):]).strip("/")
                    return self.to_repository(source, page, path, fragment, line, target)
            return target
        if parts.scheme or target.startswith("//") or not parts.path:
            if not parts.scheme and fragment:
                self.fragments.append((source, line, page, fragment))
            return target
        if source.startswith("wiki/") and "/" not in parts.path and "." not in parts.path \
                and (self.root / "wiki" / f"{unquote(parts.path)}.md").is_file():
            return self.to_wiki(source, page, unquote(parts.path), fragment, line, target)
        path = posixpath.normpath(posixpath.join(posixpath.dirname(source), unquote(parts.path)))
        if path == ".." or path.startswith("../"):
            self.report(source, line, f"the link `{target}` leaves the repository")
            return target
        return self.to_repository(source, page, "" if path == "." else path, fragment, line, target)

    def with_fragment(self, source: str, line: int, page: str, published: str, fragment: str) -> str:
        reference = relative(page, published)
        if fragment:
            self.fragments.append((source, line, published, fragment))
            reference += "#" + fragment
        return reference

    def to_wiki(self, source: str, page: str, name: str, fragment: str, line: int, target: str) -> str:
        if name == "Home":
            return self.with_fragment(source, line, page, "docs/", "")
        document = self.wiki_page(name)
        if document is None or document not in self.documents:
            self.report(source, line, f"the link `{target}` names wiki page `{name}`, which the site does not "
                                      f"publish; add it to {SITE}/{CONTENTS}")
            return target
        return self.with_fragment(source, line, page, self.documents[document].published, fragment)

    def to_repository(self, source: str, page: str, path: str, fragment: str, line: int, target: str) -> str:
        full = self.root / path
        if path in self.documents:
            return self.with_fragment(source, line, page, self.documents[path].published, fragment)
        if path.startswith("wiki/") and path.endswith(".md") and path.count("/") == 1:
            # A wiki page that presents a published document leads to that document.
            presented = self.wiki_page(path[len("wiki/"):-3])
            if presented in self.documents:
                return self.with_fragment(source, line, page, self.documents[presented].published, fragment)
        if path.endswith(".md") and full.is_file():
            self.report(source, line, f"the link `{target}` names `{path}`, which the site does not publish; "
                                      f"add it to {SITE}/{CONTENTS}")
            return target
        if full.is_file() and (posixpath.splitext(path)[1] in SOURCE_VIEW or posixpath.basename(path) in SOURCE_NAMES):
            # The deployment leaves out names that start with `.`, so `.github` is published as `_github`.
            shown = "/".join("_" + part[1:] if part.startswith(".") else part for part in path.split("/"))
            published = self.source_views.setdefault(path, f"source/{shown}.html")
            return self.with_fragment(source, line, page, published, fragment)
        if full.exists():
            kind = "tree" if full.is_dir() else "blob"
            suffix = f"#{fragment}" if fragment else ""
            return f"{self.repository_url}/{kind}/{DEFAULT_BRANCH}/{path}{suffix}".replace(f"/{DEFAULT_BRANCH}/#",
                                                                                         f"/{DEFAULT_BRANCH}#")
        self.report(source, line, f"the link `{target}` names `{path}`, which is not in the repository")
        return target

    # Rendering --------------------------------------------------------------

    def render_documents(self) -> None:
        for document in self.ordered():
            text = (self.root / document.source).read_text(encoding="utf-8")
            rendered = site_markdown.render(text, self.rewriter(document.source, document.published), highlight,
                                            skip_leading_html=True)
            document.rendered = rendered
            document.title = document.title or rendered.title or document.label
            document.label = document.label or document.title
            body = rendered.html
            if not rendered.title:
                body = f'<h1 id="top-title">{html.escape(document.title)}</h1>\n' + body
            if document.published.startswith("stdlib/") and document.published != "stdlib/index.html":
                body = self.mark_members(document, body)
            document.body = body

    def mark_members(self, document: Document, body: str) -> str:
        used: set[str] = set()

        def mark(match: re.Match) -> str:
            owner, link, declaration = match.group(1), match.group(2), html.unescape(match.group(3))
            name_match = DECLARED_NAME.search(declaration)
            name = name_match.group(1) if name_match else declaration.split("(")[0].split()[-1]
            owner_name = owner.split(".")[-1]
            label = owner_name if name == owner_name else f"{owner_name}.{name}"
            identifier = base = "api-" + site_markdown.slug(label.replace(".", "-"))
            count = 1
            while identifier in used:
                count += 1
                identifier = f"{base}-{count}"
            used.add(identifier)
            document.members.append((label, identifier))
            return f'<li id="{identifier}">' + match.group()[4:]

        return API_ITEM.sub(mark, body)

    def check_fragments(self, pages: dict[str, set[str]]) -> None:
        for source, line, published, fragment in self.fragments:
            if published.endswith("/"):
                published += "index.html"
            ids = pages.get(published)
            if ids is not None and fragment not in ids and fragment != "top":
                self.report(source, line, f"the link to `{published}#{fragment}` names no heading there")

    # Layout -----------------------------------------------------------------

    def head(self, page: str, title: str, description: str) -> str:
        root = root_of(page)
        partial = self.fill(self.partials.get("head", ""), root)
        return (f'<!doctype html>\n<html lang="en">\n<head>\n{partial}\n  <title>{html.escape(title)}</title>\n'
                f'  <meta name="description" content="{html.escape(description)}">\n'
                f'  <script src="{root}assets/docs.js" defer></script>\n</head>\n')

    def navigation(self, page: str, current: Document | None) -> str:
        parts = [f'<nav class="docs-nav" id="documents" aria-label="Documentation">',
                 f'<a class="docs-home-link" href="{relative(page, "docs/")}">{html.escape(self.home_title)}</a>']
        for section in self.sections:
            is_current = current is not None and current.section is section
            parts.append(f'<details{" open" if is_current else ""}>'
                         f"<summary>{html.escape(section.title)}</summary><ul>")
            groups: dict[str, list[Document]] = {}
            for document in section.documents:
                if document.group:
                    groups.setdefault(document.group, []).append(document)
                    continue
                parts.append(self.nav_link(page, document, current))
            if groups:
                for group, documents in groups.items():
                    open_group = current is not None and current.group == group and current.section is section
                    parts.append(f'<li class="group"><details{" open" if open_group else ""}>'
                                 f"<summary>Std.{html.escape(group)}</summary><ul>")
                    parts.extend(self.nav_link(page, document, current) for document in documents)
                    parts.append("</ul></details></li>")
            parts.append("</ul></details>")
        parts.append("</nav>")
        return "\n".join(parts)

    @staticmethod
    def nav_link(page: str, document: Document, current: Document | None) -> str:
        attribute = ' aria-current="page"' if document is current else ""
        return f'<li><a href="{relative(page, document.published)}"{attribute}>{html.escape(document.label)}</a></li>'

    def search_box(self, page: str, large: bool = False) -> str:
        size = " large" if large else ""
        return (f'<div class="search{size}" data-search data-root="{root_of(page)}" '
                f'data-index="{root_of(page)}assets/search.json" hidden>'
                f'<label class="visually-hidden" for="search-{1 if large else 0}">Search the documentation</label>'
                f'<input id="search-{1 if large else 0}" type="search" autocomplete="off" spellcheck="false" '
                f'placeholder="{"Search documents, sections, and library members" if large else "Search"}" role="combobox" '
                f'aria-expanded="false" aria-autocomplete="list" aria-controls="results-{1 if large else 0}">'
                f'<kbd aria-hidden="true">/</kbd>'
                f'<ul class="results" id="results-{1 if large else 0}" role="listbox" hidden></ul></div>')

    def layout(self, page: str, title: str, description: str, main: str, toc: str, current: Document | None,
               crumbs: list[tuple[str, str]]) -> str:
        root = root_of(page)
        crumb_html = " ".join(
            f'<li><a href="{relative(page, target)}">{html.escape(label)}</a></li>' if target
            else f'<li aria-current="page">{html.escape(label)}</li>' for label, target in crumbs)
        return (self.head(page, title, description)
                + '<body class="docs">\n  <a class="skip" href="#content">Skip to the content</a>\n'
                + self.fill(self.partials.get("masthead", ""), root) + "\n"
                + '<div class="docs-shell">\n<aside class="docs-side">\n'
                # The documentation's home has its own, larger search box.
                + ("" if current is None and page == "docs/index.html" else self.search_box(page))
                + '<a class="docs-jump" href="#documents">All documents</a>\n'
                + self.navigation(page, current)
                + "\n</aside>\n"
                + f'<main id="content" class="docs-main">\n<ol class="crumbs">{crumb_html}</ol>\n{main}\n</main>\n'
                + f'<aside class="docs-toc" aria-label="On this page">{toc}</aside>\n</div>\n'
                + self.fill(self.partials.get("footer", ""), root)
                + "\n</body>\n</html>\n")

    @staticmethod
    def toc(document_headings: list[site_markdown.Heading]) -> str:
        entries = [heading for heading in document_headings if heading.level in (2, 3)]
        if not entries:
            return ""
        items = "".join(
            f'<li class="level-{heading.level}"><a href="#{heading.id}">{heading.html.split("<a class=")[0]}</a></li>'
            for heading in entries)
        return f'<p class="toc-title">On this page</p><ul>{items}</ul><a class="toc-top" href="#top">Back to top</a>'

    def plate(self, page: str, name: str, eager: bool = False) -> str:
        if not name:
            return ""
        path = self.site / "assets" / "plates" / f"{name}.png"
        if not path.is_file():
            self.report(f"{SITE}/{CONTENTS}", 1, f"names plate `{name}`, which is not a file in "
                                                 f"{SITE}/assets/plates/")
            return ""
        width, height = png_size(path)
        loading = "" if eager else ' loading="lazy"'
        # Each plate has a version for the light theme in plates/light/, which the check requires to exist.
        light = relative(page, f"assets/plates/light/{name}.png")
        return (f'<picture><source media="(prefers-color-scheme: light)" srcset="{light}" width="{width}" '
                f'height="{height}"><img class="plate" src="{relative(page, f"assets/plates/{name}.png")}" '
                f'width="{width}" height="{height}" alt=""{loading}></picture>')

    def document_page(self, index: int, documents: list[Document]) -> str:
        document = documents[index]
        page = document.published
        previous = documents[index - 1] if index > 0 else None
        following = documents[index + 1] if index + 1 < len(documents) else None
        pager = []
        if previous:
            pager.append(f'<a class="previous" rel="prev" href="{relative(page, previous.published)}">'
                         f'<span>Previous</span>{html.escape(previous.label)}</a>')
        if following:
            pager.append(f'<a class="next" rel="next" href="{relative(page, following.published)}">'
                         f'<span>Next</span>{html.escape(following.label)}</a>')
        edit = f"{self.repository_url}/edit/{DEFAULT_BRANCH}/{document.source}"
        history = f"{self.repository_url}/commits/{DEFAULT_BRANCH}/{document.source}"
        figure = f'<figure class="page-plate">{self.plate(page, document.plate, eager=True)}</figure>' \
            if document.plate else ""
        main = (f'<article class="prose">\n{figure}{document.body}\n</article>\n'
                f'<div class="page-meta"><p>Rendered from <code>{html.escape(document.source)}</code> on '
                f'<code>{DEFAULT_BRANCH}</code>.</p><p><a href="{edit}">Edit this page</a> '
                f'<a href="{history}">History</a></p></div>\n'
                f'<nav class="pager" aria-label="Previous and next">{"".join(pager)}</nav>')
        crumbs = [(self.home_title, "docs/"), (document.section.title, f"docs/#{document.section.id}")]
        if document.group:
            crumbs.append((f"Std.{document.group}", "stdlib/"))
        crumbs.append((document.label, ""))
        summary = first_sentence(document.rendered.summary if document.rendered else "")
        return self.layout(page, f"{document.title} · Neura", summary, main, self.toc(document.rendered.headings),
                           document, crumbs)

    def home_page(self) -> str:
        page = "docs/index.html"
        shelves = []
        toc = ['<p class="toc-title">Sections</p><ul>']
        for section in self.sections:
            items = []
            groups: dict[str, int] = {}
            for document in section.documents:
                if document.group:
                    groups[document.group] = groups.get(document.group, 0) + 1
                    continue
                summary = first_sentence(document.rendered.summary) if document.rendered else ""
                items.append(f'<li><a href="{relative(page, document.published)}">{html.escape(document.label)}</a>'
                             f"<span>{html.escape(summary)}</span></li>")
            if groups:
                index = self.documents.get("wiki/Stdlib.md")
                target = index.published if index else "stdlib/index.html"
                packages = " ".join(
                    f'<a href="{relative(page, target)}#std{site_markdown.slug(group)}">Std.{html.escape(group)}'
                    f"<small>{count}</small></a>" for group, count in groups.items())
                items.append(f'<li class="packages">{packages}</li>')
            shelves.append(f'<section class="shelf" id="{section.id}">{self.plate(page, section.plate)}'
                           f"<h2>{html.escape(section.title)}</h2><ul>{''.join(items)}</ul></section>")
            toc.append(f'<li class="level-2"><a href="#{section.id}">{html.escape(section.title)}</a></li>')
        toc.append("</ul>")
        main = (f'<div class="docs-intro"><h1>{html.escape(self.home_title)}</h1>'
                f"<p>{html.escape(self.home_summary)}</p>{self.search_box(page, large=True)}</div>"
                f'<div class="shelves">{"".join(shelves)}</div>')
        return self.layout(page, f"{self.home_title} · Neura", self.home_summary, main, "".join(toc), None,
                           [(self.home_title, "")])

    def source_page(self, path: str, page: str) -> tuple[str, int]:
        text = (self.root / path).read_text(encoding="utf-8", errors="replace")
        language = "neura" if path.endswith(".nra") else ""
        shown = highlight(language, text) if language else html.escape(text, quote=False)
        lines = shown.split("\n")
        if lines and lines[-1] == "":
            lines.pop()
        # Only a block comment spans lines: it is closed at each break and reopened on the next line.
        numbered, open_comment = [], False
        for number, line in enumerate(lines, 1):
            content = ('<span class="c">' if open_comment else "") + line
            open_comment = content.count("<span") > content.count("</span>")
            if open_comment:
                content += "</span>"
            numbered.append(f'<span class="line" id="L{number}"><a href="#L{number}" aria-label="Line {number}">'
                            f"</a>{content}</span>")
        blob = f"{self.repository_url}/blob/{DEFAULT_BRANCH}/{path}"
        main = (f'<article class="prose source-view"><h1>{html.escape(posixpath.basename(path))}</h1>'
                f'<p class="source-path"><code>{html.escape(path)}</code> · {len(lines)} lines · '
                f'<a href="{blob}">View on GitHub</a></p>'
                f'<pre class="numbered"{" data-language=neura" if language else ""}><code>'
                # Each line is a block of its own, so the lines are joined without a line break.
                + "".join(numbered) + "</code></pre></article>")
        crumbs = [(self.home_title, "docs/"), (path, "")]
        return self.layout(page, f"{path} · Neura", f"The file {path} in the Neura repository.", main, "", None,
                           crumbs), len(lines)

    # Assembly ---------------------------------------------------------------

    def assemble(self) -> dict[str, bytes]:
        has_contents = self.load_contents()
        if has_contents:
            self.render_documents()
        for path in sorted(self.site.rglob("*")):
            if not path.is_file():
                continue
            name = path.relative_to(self.site).as_posix()
            if name == CONTENTS or name.startswith(PARTIALS + "/"):
                continue
            data = path.read_bytes()
            if name.endswith(".html") and (INCLUDE.search(data.decode("utf-8", "replace"))
                                           or MARKDOWN_INCLUDE.search(data.decode("utf-8", "replace"))):
                data = self.expand(name, data.decode("utf-8"), f"{SITE}/{name}").encode("utf-8")
            else:
                self.origins[name] = Origin(f"{SITE}/{name}", [(1, 1, "")])
            self.files[name] = data
        if not has_contents:
            self.fingerprint()
            return self.files

        documents = self.ordered()
        generated: dict[str, str] = {}
        for index, document in enumerate(documents):
            generated[document.published] = self.document_page(index, documents)
            self.origins[document.published] = Origin(document.source, generated=True)
        generated["docs/index.html"] = self.home_page()
        self.origins["docs/index.html"] = Origin(f"{SITE}/{CONTENTS}", generated=True)
        line_counts = {}
        for path, page in sorted(self.source_views.items()):
            generated[page], line_counts[page] = self.source_page(path, page)
            self.origins[page] = Origin(path, generated=True)
        for name, text in generated.items():
            if name in self.files:
                self.report(f"{SITE}/{name}", 1, "has the path of a page the build generates; remove the file or "
                                                 "move the document")
            self.files[name] = text.encode("utf-8")

        ids = {document.published: {heading.id for heading in document.rendered.headings} | {"top-title"}
               for document in documents}
        for page, count in line_counts.items():
            ids[page] = {f"L{number}" for number in range(1, count + 1)}
        self.check_fragments(ids)
        self.files["assets/search.json"] = self.search_index().encode("utf-8")
        self.origins["assets/search.json"] = Origin(f"{SITE}/{CONTENTS}", generated=True)
        self.fingerprint()
        return self.files

    # Fingerprints ---------------------------------------------------------

    def version(self, page: str, reference: str) -> str:
        """`reference` with `?v=` and the start of its file's SHA-256, when it names a file of `assets/`."""
        if not reference or "?" in reference or ":" in reference or reference.startswith(("#", "/")):
            return reference
        path, hash_sign, fragment = reference.partition("#")
        target = posixpath.normpath(posixpath.join(posixpath.dirname(page), path))
        if not target.startswith("assets/") or target not in self.files:
            return reference
        digest = hashlib.sha256(self.files[target]).hexdigest()[:10]
        return f"{path}?v={digest}{hash_sign}{fragment}"

    def fingerprint(self) -> None:
        """Give each reference to a file of `assets/` a version made from that file's content.

        GitHub Pages lets a browser keep a file for ten minutes without asking again, so after a deployment a
        page could be shown with the stylesheet, script, or images of the previous one. A reference that changes
        whenever its file changes makes the browser fetch the new file. Stylesheets are done first, because a
        stylesheet's own version must cover the fonts and images it names.
        """
        for name in sorted(self.files):
            if name.endswith(".css"):
                text = self.files[name].decode("utf-8")
                text = CSS_URL.sub(lambda match: f'url("{self.version(name, match.group(1))}")', text)
                self.files[name] = text.encode("utf-8")
        for name in sorted(self.files):
            if not name.endswith(".html"):
                continue

            def replace(match: re.Match, page: str = name) -> str:
                attribute, value = match.group(1), match.group(2)
                if attribute == "srcset":
                    value = ", ".join(" ".join([self.version(page, part.split()[0])] + part.split()[1:])
                                      for part in value.split(",") if part.split())
                else:
                    value = self.version(page, value)
                return f'{attribute}="{value}"'

            self.files[name] = PAGE_REFERENCE.sub(replace, self.files[name].decode("utf-8")).encode("utf-8")

    def search_index(self) -> str:
        """Pages, their sections, and library members, for the search box. URLs are relative to the top."""
        pages, entries = [], []
        for document in self.ordered():
            number = len(pages)
            group = f"Std.{document.group}" if document.group else document.section.title
            pages.append([document.title, document.published, group])
            for heading in document.rendered.headings:
                if heading.level in (2, 3):
                    entries.append([heading.text, number, heading.id, 1])
            for label, identifier in document.members:
                entries.append([label, number, identifier, 2])
        return json.dumps({"pages": pages, "entries": entries}, ensure_ascii=False, separators=(",", ":"))


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[16:24]
    return int.from_bytes(data[:4], "big"), int.from_bytes(data[4:], "big")


def assemble(root: Path, repository_url: str, report: Report) -> tuple[dict[str, bytes], dict[str, Origin]]:
    """Every published file by its path, and where each came from."""
    assembly = Assembly(root, repository_url, report)
    files = assembly.assemble()
    return files, assembly.origins
