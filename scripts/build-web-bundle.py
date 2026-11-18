#!/usr/bin/env python3
"""Assemble a web bundle from an image and the browser runtime (docs/runtime/Providers.md, "Web bundles").

The bundle is the static directory of Projects030.md, "Bundles": index.html, a canonical bundle.json, and every
other file named by its SHA-256, with relative references, so any static file host serves it under any path.
This script stands in for `neura build` of a `web` target until the project tools exist. It takes an image that
is already built (`--image`), or builds one from a source file with `neura build` (`--source`), and adds the
stylesheets and assets that the build listed in `<image>.styles.json`, linking the stylesheets in that order."""

import argparse
import base64
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'hosts/web'
# Dependencies first: a module's imports are renamed before its own digest is taken.
MODULES = ('ui-wire.mjs', 'dom.mjs', 'nvm.js', 'ui.mjs', 'fetch.mjs', 'boot.mjs')
IMPORT = re.compile(r"""(from\s+|import\s*\(\s*)'\./([A-Za-z0-9_.-]+)'""")
ORIGIN = re.compile(r'^https?://[A-Za-z0-9.-]+(:[0-9]{1,5})?$|^https?://\[[0-9A-Fa-f:.]+\](:[0-9]{1,5})?$')


def sri(data):
    return 'sha256-' + base64.b64encode(hashlib.sha256(data).digest()).decode()


def policy(connect, import_map):
    """The content-security policy. `connect` adds origins the page may fetch from besides its own; the import
    map, an inline script, is allowed by its hash."""
    sources = ' '.join(["'self'", *connect])
    return (f"default-src 'none'; script-src 'self' 'wasm-unsafe-eval' '{sri(import_map.encode())}'; "
            f"style-src 'self'; img-src 'self'; font-src 'self'; connect-src {sources}; base-uri 'none'; "
            "form-action 'none'")



MEDIA_TYPES = {'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
               '.nvr': 'application/octet-stream', '.json': 'application/json', '.css': 'text/css',
               '.svg': 'image/svg+xml', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
               '.gif': 'image/gif', '.webp': 'image/webp', '.avif': 'image/avif', '.ico': 'image/x-icon',
               '.woff': 'font/woff', '.woff2': 'font/woff2', '.ttf': 'font/ttf', '.otf': 'font/otf',
               '.mp4': 'video/mp4', '.webm': 'video/webm', '.mp3': 'audio/mpeg', '.ogg': 'audio/ogg',
               '.wav': 'audio/wav', '.vtt': 'text/vtt', '.txt': 'text/plain', '.pdf': 'application/pdf'}
# Bundled stylesheets and assets keep the paths the build gave them: a digest name below one of these directories.
STYLE_PATH = re.compile(r'^(styles|assets)/[0-9a-f]{64}(\.[A-Za-z0-9]+)?$')
FILE_LIMIT = 64 * 1024 * 1024  # W-1
APP_ID = re.compile(r'^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$')


# The services a web target may import (Apps030.md, "Capability groups"): console, process exit, clocks, entropy,
# ui and history, http_client, and the bridge, which a browser answers with error 14. Every other group is "no"
# for `web`; an image that imports one is refused here instead of failing in the page.
WEB_SERVICES = re.compile(r'^neura\.(console\.[a-z_]+|process\.exit|clock\.[a-z_]+|entropy\.fill|ui\.[a-z_.]+|'
                          r'history\.[a-z_]+|http\.fetch|bridge\.call)$')


def image_imports(image):
    """The (name, version) of each host import of an NVR image (docs/runtime/NVR.md, "Imports"), and the tags of
    its sections. Only the header, the directory and IMPT are read."""
    def fail(reason):
        sys.exit(f'build-web-bundle: the image is not a valid NVR file: {reason}')
    if len(image) < 24 or image[:4] != b'NEUR':
        fail('no NEUR header')
    count = struct.unpack_from('<I', image, 12)[0]
    if 24 + 24 * count > len(image):
        fail('truncated section directory')
    sections = {}
    for index in range(count):
        tag, _, offset, size = struct.unpack_from('<4sIQQ', image, 24 + 24 * index)
        if offset + size > len(image):
            fail(f'section {tag!r} exceeds the file')
        sections[tag.decode('ascii', 'replace')] = image[offset:offset + size]
    imports = []
    table = sections.get('IMPT', b'')
    at = 0

    def u32():
        nonlocal at
        if at + 4 > len(table):
            fail('truncated import table')
        value = struct.unpack_from('<I', table, at)[0]
        at += 4
        return value
    for _ in range(u32() if table else 0):
        size = u32()
        name = table[at:at + size]
        at += size
        version = u32()
        parameters = u32()
        at += 4 * parameters + 8  # parameter types, success and error types
        if at > len(table):
            fail('truncated import record')
        try:
            imports.append((name.decode('utf-8'), version))
        except UnicodeDecodeError:
            fail('an import name is not UTF-8')
    return imports, set(sections)


def check_image(image, http_client):
    imports, sections = image_imports(image)
    refused = sorted(f'{name}/{version}' for name, version in imports if not WEB_SERVICES.match(name))
    if refused:
        sys.exit('build-web-bundle: the image imports services a web target cannot have (Apps030.md, '
                 f'"Capability groups"): {", ".join(refused)}')
    for tag in ('NATV', 'CALB'):
        if tag in sections:
            sys.exit(f'build-web-bundle: the image carries native code or callbacks ({tag}), which a web target '
                     'cannot have')
    if not http_client and any(name == 'neura.http.fetch' for name, _ in imports):
        print('build-web-bundle: the image imports neura.http.fetch but the bundle does not grant it; every fetch '
              'answers error 14 (pass --http-client or --connect)')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def named(name, data):
    stem, _, extension = name.partition('.')
    return f'{stem}.{digest(data)}.{extension}'


def build(args):
    if not APP_ID.match(args.id):
        sys.exit(f'build-web-bundle: invalid application id {args.id!r}')
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]*', args.mount):
        sys.exit('build-web-bundle: the mount element id must be a plain identifier')
    for origin in args.connect:
        # Projects030.md: an origin is a scheme, host and optional port; a path or a wildcard is refused.
        if not ORIGIN.match(origin):
            sys.exit(f'build-web-bundle: {origin!r} is not an origin (scheme, host and optional port)')
    connect = sorted(set(args.connect))
    http_client = args.http_client or bool(connect)
    image = args.image.read_bytes()
    check_image(image, http_client)
    out = args.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = {}

    deferred = set()

    def add(path, data, initial=True):
        if len(data) > FILE_LIMIT:
            sys.exit(f'build-web-bundle: {path} exceeds W-1 ({FILE_LIMIT} bytes)')
        (out / path).parent.mkdir(parents=True, exist_ok=True)
        (out / path).write_bytes(data)
        files[path] = data
        if not initial:
            deferred.add(path)

    renamed = {}
    for name, source in (('nvm.wasm', args.wasm / 'nvm.wasm'), ('nvm.mjs', args.wasm / 'nvm.mjs')):
        data = source.read_bytes()
        renamed[name] = named(name, data)
        add(renamed[name], data)
    for name in MODULES:
        text = (WEB / name).read_text()

        def rewrite(match):
            target = match.group(2)
            if target not in renamed:
                sys.exit(f'build-web-bundle: {name} imports {target}, which the bundle does not hold')
            return f"{match.group(1)}'./{renamed[target]}'"

        data = IMPORT.sub(rewrite, text).encode()
        renamed[name] = named(name, data)
        add(renamed[name], data)
    image_path = named('app.nvr', image)
    add(image_path, image)

    # The stylesheets and assets of the image, written beside it by `neura build` (docs/apps/Css.md).
    links = []
    listing = Path(f'{args.image}.styles.json')
    if listing.exists():
        styles = json.loads(listing.read_text())
        if styles.get('schema') != 1:
            sys.exit(f'build-web-bundle: {listing}: unknown schema {styles.get("schema")!r}')
        links = styles['stylesheets']
        for entry in styles['files']:
            path = entry['path']
            if not STYLE_PATH.match(path):
                sys.exit(f'build-web-bundle: {listing}: unexpected file {path!r}')
            data = (args.image.parent / path).read_bytes()
            if digest(data) != entry['sha256'] or len(data) != entry['bytes']:
                sys.exit(f'build-web-bundle: {path} does not match {listing}')
            add(path, data, initial=path in links)
        missing = [path for path in links if path not in files]
        if missing:
            sys.exit(f'build-web-bundle: {listing} links unlisted stylesheets {missing}')
    # Hash routing keeps every page at the bundle's index.html, so references stay relative and the bundle works
    # under any path. History routing serves index.html at every application path (/parts/7), where a relative
    # reference would resolve below /parts/: the page's own references start at --base instead.
    prefix = args.base if args.routing == 'history' else './'
    stylesheets = ''.join(f'<link rel="stylesheet" href="{html.escape(prefix + path)}">\n' for path in links)

    boot = json.dumps({'base': args.base, 'http': {'origins': connect} if http_client else None,
                       'image': {'path': image_path, 'digest': digest(image)},
                       'routing': args.routing,
                       'wasm': {'path': renamed['nvm.wasm'], 'digest': digest(files[renamed['nvm.wasm']])}},
                      sort_keys=True, separators=(',', ':'))
    boot_module = renamed['boot.mjs']
    integrity = sri(files[boot_module])
    # Every module a module imports is checked by its digest too: an import map's integrity table applies to
    # static imports, which carry no integrity attribute of their own.
    # Its keys resolve against the page's URL, which under history routing is a deep path: they start at the base.
    import_map = json.dumps({'imports': {}, 'integrity': {f'{prefix}{renamed[name]}': sri(files[renamed[name]])
                                                          for name in ('nvm.mjs', *MODULES) if name != 'boot.mjs'}},
                            sort_keys=True, separators=(',', ':'))
    csp = policy(connect, import_map)
    page = f'''<!doctype html>
<html lang="{html.escape(args.lang)}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="{html.escape(csp)}">
<title>{html.escape(args.title)}</title>
{stylesheets}<script type="application/json" id="neura-boot">{boot}</script>
<script type="importmap">{import_map}</script>
<script type="module" src="{html.escape(prefix + boot_module)}" integrity="{integrity}"></script>
</head>
<body>
<div id="{args.mount}"></div>
<p id="neura-unsupported" hidden>This application needs a browser with WebAssembly exception handling: Chromium 156, Firefox 153 ESR, or the current Safari.</p>
<p id="neura-failure" role="alert" hidden></p>
</body>
</html>
'''.encode()
    add('index.html', page)

    manifest = {
        'base': args.base,
        # The console, clock and entropy groups are always granted and never listed (Projects030.md).
        'capabilities': sorted(['ui', *(['http_client'] if http_client else [])]),
        'contracts': [],
        'csp': csp,
        'files': [{'bytes': len(data), 'initial': path not in deferred,
                   'media_type': MEDIA_TYPES.get(Path(path).suffix, 'application/octet-stream'), 'path': path, 'sha256': digest(data)}
                  for path, data in sorted(files.items())],
        'id': args.id,
        'image': {'digest': digest(image), 'path': image_path},
        'kind': 'web',
        'language': '0.3',
        'limits': {'W-1': FILE_LIMIT},
        'name': args.name,
        'origins': {'connect': connect} if connect else {},
        'requires': {'browser_adapter': 1, 'ui_host': 1},
        'routing': args.routing,
        'schema': 1,
        'version': args.version,
    }
    text = json.dumps(manifest, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    (out / 'bundle.json').write_bytes(text)
    if args.routing == 'history':
        shutil.copy(out / 'index.html', out / '404.html')
        print(f'build-web-bundle: history routing under {args.base}: the host must answer every application path '
              f'below {args.base} with index.html (or serve 404.html, a copy of it, for unknown paths); the files '
              f'of the bundle must be served at {args.base}<file>')
    print(f'{out}: {len(files) + 1} files, bundle {digest(text)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    image = parser.add_mutually_exclusive_group(required=True)
    image.add_argument('--image', type=Path, help='a built image; its .styles.json, if any, is read beside it')
    image.add_argument('--source', type=Path, help='an entry source file to build with `neura build` first')
    parser.add_argument('--neura', default=shutil.which('neura') or ROOT / 'build/neura', type=Path,
                        help='the neura executable that builds --source')
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--wasm', default=ROOT / 'build/wasm-runtime', type=Path,
                        help='the directory holding nvm.wasm and nvm.mjs')
    parser.add_argument('--id', required=True, help='reverse-DNS application id, such as org.example.parts')
    parser.add_argument('--name', default='Neura application')
    parser.add_argument('--version', default='0.1.0')
    parser.add_argument('--title', default='Neura application')
    parser.add_argument('--lang', default='en')
    parser.add_argument('--mount', default='app', help='the id of the element the entry mounts on')
    parser.add_argument('--routing', choices=('hash', 'history'), default='hash')
    parser.add_argument('--base', default='/',
                        help='the URL path the bundle is served under, starting and ending with "/" (history routing)')
    parser.add_argument('--http-client', action='store_true',
                        help='grant neura.http.fetch for the page\'s own origin')
    parser.add_argument('--connect', action='append', default=[], metavar='ORIGIN',
                        help='an origin the page may also fetch from, such as https://api.example.com; '
                             'implies --http-client')
    args = parser.parse_args()
    if not (args.base.startswith('/') and args.base.endswith('/')) or any(c in args.base for c in '"<>\\?#'):
        parser.error('--base is a URL path that starts and ends with "/"')
    if args.source is None:
        build(args)
        return
    with TemporaryDirectory(prefix='neura-web-build-') as path:
        args.image = Path(path) / 'app.nvr'
        built = subprocess.run([args.neura, 'build', args.source, '-o', args.image])
        if built.returncode != 0:
            sys.exit(built.returncode)
        build(args)


if __name__ == '__main__':
    main()
