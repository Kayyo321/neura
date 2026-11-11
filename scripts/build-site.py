#!/usr/bin/env python3
"""Build the website from `site/` and check it.

The website is the files in `site/`. The build copies each one to the same
path in the output directory, so `site/assets/site.css` is published as
`assets/site.css` below the site's address. It changes two things: each
reference to a file of `assets/` gains a version made from the file's content
(`site.css?v=...`), and `404.html` receives a `<base>` element, described below. docs/Site.md describes the
site and how it is published.

The build checks every HTML and CSS file in `site/`:

  * A reference to a file of the site names a file that exists, with the
    same spelling and capitals: a link, image, stylesheet, script, or CSS
    `url(...)`. A reference to a directory needs an `index.html` in it, and
    a `#fragment` needs an element with that `id` in the page it names.
  * No reference starts with `/`. GitHub Pages serves the site below the
    repository's name, where `/assets/site.css` names a path outside the
    site. A relative reference resolves there and in a local preview.
  * A page loads nothing from another host. Its images, stylesheets,
    scripts, fonts, and frames are files of the site, so showing a page
    sends no request to a third party. A link that a reader follows
    (`<a href>`) may point anywhere.
  * A link to https://github.com/<repository>/blob/main/<path>, to
    .../tree/main/<path>, or to .../wiki/<page> names a path or wiki page
    that exists in the checkout. Its `#fragment` is not checked.
  * No page has a `<base>` element, which would change what every relative
    reference in it means.
  * A listing that names its source shows that file. The text of
    `<pre data-source="examples/hello.nra">` equals the file at that path in
    the checkout, apart from white space at either end, so code shown on the
    site cannot drift from the example it was copied from.
  * No file or directory name starts with `.`. The deployment leaves such
    files out, so the published site would lack them.

The check reads `href`, `src`, `srcset`, and `poster` attributes, `<style>`
elements, and CSS files. It does not run scripts, so it cannot see a request
that a script makes.

GitHub Pages answers a request for a missing page with `404.html`, at the
address that was requested. A relative reference in that page would resolve
against a directory that does not exist, so the build gives the page
`<base href="BASE-PATH/">`, which makes its references resolve from the top
of the site.

    build-site.py --output DIR   build into DIR, which must be empty or absent
    build-site.py --check        check `site/` and write nothing
    build-site.py --serve        serve a preview on this computer, rebuilt
                                 whenever a file in `site/` changes

The build does not use the network, and the same `site/` and base path
always produce the same files. The exit status is 0 when the site has no
problems and 1 otherwise. Each problem is reported as `path:line: message`.
"""

from __future__ import annotations

import argparse
import functools
import http.server
import io
import posixpath
import re
import shutil
import sys
import tempfile
import threading
import time
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit

import site_docs

REPOSITORY = "Kayyo321/neura"
DEFAULT_BRANCH = "main"
SITE_DIRECTORY = "site"
WIKI_DIRECTORY = "wiki"
HOME_PAGE = "index.html"
NOT_FOUND_PAGE = "404.html"
# Characters a redirect's path keeps as they are: the unreserved and sub-delimiter characters of a URL path, `/`,
# and `%`, so that an escape already in the request stays one escape.
PATH_CHARACTERS = "/%:@!$&'()*+,;=-._~"
# Directories whose files the preview watches, besides the Markdown files at the top of the checkout.
WATCHED = (SITE_DIRECTORY, "docs", "wiki", "examples", "stdlib")

# `<link rel>` values that name a page for a reader or a crawler to follow.
# A `<link>` with any other value names a file the browser loads.
FOLLOWED_RELATIONS = {"alternate", "author", "canonical", "help", "license", "me", "next", "prev", "search"}

BASE_PATH = re.compile(r"(/[A-Za-z0-9._~-]+)*")
HEAD_TAG = re.compile(rb"<head(?:\s[^>]*)?>", re.IGNORECASE)
CSS_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
CSS_REFERENCE = re.compile(
    r"""url\(\s*(?:"([^"]*)"|'([^']*)'|([^)"'\s]*))\s*\)|@import\s+(?:"([^"]*)"|'([^']*)')""",
    re.IGNORECASE,
)


def css_references(text: str, first_line: int = 1) -> list[tuple[int, str]]:
    """Each `url(...)` and `@import` target in CSS text, with its line number."""
    # A comment is replaced by its line breaks, so later lines keep their numbers.
    text = CSS_COMMENT.sub(lambda comment: "\n" * comment.group().count("\n"), text)
    found = []
    for match in CSS_REFERENCE.finditer(text):
        target = next(group for group in match.groups() if group is not None)
        found.append((first_line + text.count("\n", 0, match.start()), target))
    return found


class Page(HTMLParser):
    """The references, `id`s, and `<base>` elements of one HTML file."""

    def __init__(self, text: str) -> None:
        super().__init__(convert_charrefs=True)
        # (line, target, whether the browser loads it to show the page)
        self.references: list[tuple[int, str, bool]] = []
        self.ids: set[str] = set()
        self.bases: list[int] = []
        # (line, path named by `data-source`, text) for each `<pre>` that names its source
        self.listings: list[tuple[int, str, str]] = []
        self.listing: tuple[int, str] | None = None
        self.listing_text: list[str] = []
        self.style_line: int | None = None
        self.style_text: list[str] = []
        self.feed(text)
        self.close()

    def handle_starttag(self, tag: str, attributes: list[tuple[str, str | None]]) -> None:
        line = self.getpos()[0]
        values = {name: value for name, value in attributes if value is not None}
        if "id" in values:
            self.ids.add(values["id"])
        if tag == "a" and "name" in values:
            self.ids.add(values["name"])
        if tag == "base":
            self.bases.append(line)
            return
        if tag == "style":
            self.style_line = line
            self.style_text = []
        if tag == "pre" and "data-source" in values:
            self.listing = (line, values["data-source"])
            self.listing_text = []

        relations = set(values.get("rel", "").lower().split())
        followed = tag in ("a", "area") or (tag == "link" and bool(relations) and relations <= FOLLOWED_RELATIONS)
        for name in ("href", "src", "poster"):
            if name in values:
                self.references.append((line, values[name], not followed))
        for candidate in values.get("srcset", "").split(","):
            if candidate.split():
                self.references.append((line, candidate.split()[0], True))

    def handle_startendtag(self, tag: str, attributes: list[tuple[str, str | None]]) -> None:
        self.handle_starttag(tag, attributes)

    def handle_data(self, data: str) -> None:
        if self.style_line is not None:
            self.style_text.append(data)
        if self.listing is not None:
            self.listing_text.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag == "pre" and self.listing is not None:
            self.listings.append((*self.listing, "".join(self.listing_text)))
            self.listing = None
        if tag == "style" and self.style_line is not None:
            for line, target in css_references("".join(self.style_text), self.style_line):
                self.references.append((line, target, True))
            self.style_line = None


class Site:
    """The published files of the site, and the problems found in them."""

    def __init__(self, root: Path, repository: str) -> None:
        self.root = root
        self.source = root / SITE_DIRECTORY
        self.repository_url = f"https://github.com/{repository}"
        self.problems: list[str] = []
        self.pages: dict[str, Page | None] = {}
        # Every published file, by its path below the site's address. site_docs.py expands the
        # partials of `site/` and renders the repository's documents.
        self.output, self.origins = site_docs.assemble(root, self.repository_url, self.report_source)
        self.files = sorted(self.output)
        self.directories = {""}
        for name in self.files:
            while "/" in name:
                name = name.rsplit("/", 1)[0]
                self.directories.add(name)

    def report_source(self, path: str, line: int, message: str) -> None:
        self.problems.append(f"{path}:{line}: {message}")

    def report(self, name: str, line: int, message: str) -> None:
        origin = self.origins.get(name)
        if origin is None:
            self.problems.append(f"{SITE_DIRECTORY}/{name}:{line}: {message}")
            return
        if origin.generated:
            message = f"{message} (in the published {name}, line {line})"
        path, line = origin.locate(line)
        self.problems.append(f"{path}:{line}: {message}")

    def text(self, name: str) -> str | None:
        try:
            return self.output[name].decode("utf-8")
        except UnicodeDecodeError:
            self.report(name, 1, "is not UTF-8 text")
            return None

    def page(self, name: str) -> Page | None:
        if name not in self.pages:
            text = self.text(name)
            self.pages[name] = None if text is None else Page(text)
        return self.pages[name]

    def check(self) -> None:
        if HOME_PAGE not in self.files:
            self.report(HOME_PAGE, 1, "does not exist; the site needs a home page")
        if NOT_FOUND_PAGE in self.files and not HEAD_TAG.search(self.output[NOT_FOUND_PAGE]):
            self.report(NOT_FOUND_PAGE, 1, "has no `<head>` to hold the `<base>` element the build adds")
        for name in self.files:
            hidden = [part for part in name.split("/") if part.startswith(".")]
            if hidden:
                self.report(name, 1, f"`{hidden[0]}` starts with `.`, and the deployment leaves such files out")
            if name.endswith((".html", ".htm")):
                page = self.page(name)
                if page is None:
                    continue
                for line in page.bases:
                    self.report(name, line, "a page must not have a `<base>` element; write each reference relative "
                                            "to the page")
                for line, target, loaded in page.references:
                    self.check_reference(name, line, target, loaded)
                for line, source, shown in page.listings:
                    self.check_listing(name, line, source, shown)
            elif name.endswith(".css"):
                for line, target in css_references(self.text(name) or ""):
                    self.check_reference(name, line, target, True)

    def check_reference(self, name: str, line: int, target: str, loaded: bool) -> None:
        target = target.strip()
        try:
            parts = urlsplit(target)
        except ValueError:
            self.report(name, line, f"cannot read the reference `{target}`")
            return
        if parts.scheme or target.startswith("//"):
            if loaded and parts.scheme != "data":
                self.report(name, line, f"`{target}` is loaded from another host; a page loads only files of the site")
            elif parts.scheme in ("http", "https"):
                self.check_repository_link(name, line, target, parts.netloc, parts.path)
            return
        if target.startswith("/"):
            self.report(name, line, f"`{target}` starts with `/`, which names a path outside the site on GitHub "
                                    "Pages; write it relative to this file")
            return

        if not parts.path:
            found = name
        else:
            found = posixpath.normpath(posixpath.join(posixpath.dirname(name), unquote(parts.path)))
            if found == ".." or found.startswith("../"):
                self.report(name, line, f"`{target}` leaves the site")
                return
            if found == ".":
                found = ""
            if found in self.directories:
                found = posixpath.join(found, HOME_PAGE)
            if found not in self.files:
                self.report(name, line, f"`{target}`: {SITE_DIRECTORY}/{found} does not exist")
                return

        fragment = unquote(parts.fragment)
        if fragment and fragment != "top" and found.endswith((".html", ".htm")):
            page = self.page(found)
            if page is not None and fragment not in page.ids:
                self.report(name, line, f"`{target}`: no element with `id=\"{fragment}\"` in {SITE_DIRECTORY}/{found}")

    def check_listing(self, name: str, line: int, source: str, shown: str) -> None:
        path = self.root / source
        if ".." in source.split("/") or source.startswith("/") or not path.is_file():
            self.report(name, line, f"the listing names `{source}`, which is not a file in the repository")
        elif shown.strip() != path.read_text(encoding="utf-8").strip():
            self.report(name, line, f"the listing differs from `{source}`; copy the file again")

    def check_repository_link(self, name: str, line: int, target: str, host: str, path: str) -> None:
        url = f"https://{host}{path}"
        if not url.startswith(self.repository_url + "/"):
            return
        rest = url[len(self.repository_url) + 1 :]
        if rest == "wiki" or rest.startswith("wiki/"):
            page = unquote(rest[len("wiki") :]).strip("/") or "Home"
            if not (self.root / WIKI_DIRECTORY / f"{page}.md").is_file():
                self.report(name, line, f"`{target}`: no wiki page {WIKI_DIRECTORY}/{page}.md")
        elif rest.startswith((f"blob/{DEFAULT_BRANCH}/", f"tree/{DEFAULT_BRANCH}/")):
            relative = unquote(rest.split("/", 2)[2]).strip("/")
            if ".." in relative.split("/") or not (self.root / relative).exists():
                self.report(name, line, f"`{target}`: {relative} does not exist")

    def build(self, output: Path, base_path: str) -> None:
        """Copy the site into `output`, which holds no files."""
        for name in self.files:
            data = self.output[name]
            head = HEAD_TAG.search(data) if name == NOT_FOUND_PAGE else None
            if head is not None:
                base = f'<base href="{base_path}/">'.encode("ascii")
                data = data[: head.end()] + base + data[head.end() :]
            destination = output / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)


class Preview:
    """A build of the site in a temporary directory, kept up to date with `site/`."""

    def __init__(self, root: Path, repository: str, output: Path, base_path: str) -> None:
        self.root = root
        self.repository = repository
        self.output = output
        self.base_path = base_path
        self.lock = threading.Lock()
        self.state: list[tuple[str, int, int]] | None = None
        self.scanned = 0.0

    def refresh(self) -> None:
        """Rebuild when a file of the site or a document it publishes was added, removed, or changed."""
        with self.lock:
            if time.monotonic() - self.scanned < 1:
                return
            paths = [path for directory in WATCHED for path in (self.root / directory).rglob("*")]
            paths += list(self.root.glob("*.md"))
            state = sorted(
                (path.as_posix(), path.stat().st_mtime_ns, path.stat().st_size)
                for path in paths if path.is_file()
            )
            self.scanned = time.monotonic()
            if state == self.state:
                return
            self.state = state
            site = Site(self.root, self.repository)
            site.check()
            shutil.rmtree(self.output, ignore_errors=True)
            self.output.mkdir(parents=True)
            site.build(self.output, self.base_path)
            for problem in site.problems:
                print(problem, file=sys.stderr)
            print(f"Built {len(site.files)} files; {len(site.problems)} problems.", file=sys.stderr, flush=True)


class PreviewHandler(http.server.SimpleHTTPRequestHandler):
    """Serves a preview below its base path, with `404.html` for a missing page, as GitHub Pages does."""

    def __init__(self, *arguments, preview: Preview, **options) -> None:
        self.preview = preview
        super().__init__(*arguments, directory=str(preview.output), **options)

    def end_headers(self) -> None:
        # The browser asks again on every load, so a reload shows the latest build.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def send_head(self):
        self.preview.refresh()
        base = self.preview.base_path
        path = urlsplit(self.path).path
        if base and path in ("/", base):
            return self.redirect(base + "/")
        if not path.startswith(base + "/"):
            return self.not_found()
        relative = path[len(base) :]
        target = Path(self.translate_path(relative))
        if target.is_dir():
            if not relative.endswith("/"):
                return self.redirect(path + "/")
            target = target / HOME_PAGE
        if not target.is_file():
            return self.not_found()
        self.path = relative
        return super().send_head()

    def redirect(self, location: str):
        # The location is a path of this server made from the request's path. Percent-encode every character a
        # URL path does not hold, so nothing the client sent can break the header line, and start it with a single
        # slash, so that it cannot name another host.
        location = "/" + quote(location.lstrip("/"), safe=PATH_CHARACTERS)
        self.send_response(302)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()
        return None

    def not_found(self):
        page = self.preview.output / NOT_FOUND_PAGE
        if not page.is_file():
            self.send_error(404)
            return None
        body = page.read_bytes()
        self.send_response(404)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        return io.BytesIO(body)


def serve(root: Path, repository: str, base_path: str, port: int) -> int:
    with tempfile.TemporaryDirectory(prefix="neura-site-") as directory:
        preview = Preview(root, repository, Path(directory) / "site", base_path)
        preview.refresh()
        handler = functools.partial(PreviewHandler, preview=preview)
        # The preview listens on this computer only.
        with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
            print(f"Serving the site at http://127.0.0.1:{server.server_address[1]}{base_path}/ until interrupted.",
                  flush=True)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                print()
    return 0


def main() -> int:
    default_base_path = "/" + REPOSITORY.split("/")[1]
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path, metavar="DIR",
                      help="build the site into DIR, which must be empty or absent")
    mode.add_argument("--check", action="store_true", help="check the site and write nothing")
    mode.add_argument("--serve", action="store_true",
                      help="serve a preview on this computer, rebuilt whenever a file in site/ changes")
    parser.add_argument("--base-path", default=default_base_path, metavar="PATH",
                        help="the path the site is served below; empty for the top of a host "
                             f"(default: {default_base_path})")
    parser.add_argument("--port", type=int, default=8000, help="the port --serve listens on (default: 8000)")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="the repository checkout (default: this script's repository)")
    parser.add_argument("--repository", default=REPOSITORY,
                        help="the GitHub repository, as owner/name, whose blob, tree, and wiki links are checked "
                             f"(default: {REPOSITORY})")
    arguments = parser.parse_args()

    root = arguments.root.resolve()
    base_path = arguments.base_path.rstrip("/")
    if not BASE_PATH.fullmatch(base_path):
        parser.error(f"--base-path `{arguments.base_path}` is not empty or a path such as {default_base_path}")
    if not (root / SITE_DIRECTORY).is_dir():
        print(f"{SITE_DIRECTORY}:1: the directory does not exist in {root}")
        return 1

    if arguments.serve:
        return serve(root, arguments.repository, base_path, arguments.port)

    site = Site(root, arguments.repository)
    site.check()
    if arguments.check:
        summary = f"Checked {len(site.files)} published files"
    else:
        output = arguments.output.resolve()
        if output.is_file() or (output.is_dir() and any(output.iterdir())):
            print(f"{arguments.output}: is not an empty directory; the build does not replace existing files")
            return 1
        output.mkdir(parents=True, exist_ok=True)
        site.build(output, base_path)
        summary = f"Built {len(site.files)} files into {arguments.output}"

    for problem in site.problems:
        print(problem)
    print(f"{summary}; {len(site.problems)} problems.")
    return 1 if site.problems else 0


if __name__ == "__main__":
    sys.exit(main())
