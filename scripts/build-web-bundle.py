#!/usr/bin/env python3
"""Assemble a web bundle from an image and the browser runtime (docs/runtime/Providers.md, "Web bundles").

The bundle is the static directory of Projects030.md, "Bundles": index.html, a canonical bundle.json, and every
other file named by its SHA-256, with relative references, so any static file host serves it under any path.
This script stands in for `neura build` of a `web` target until the project tools exist; it takes an image that
is already built and adds no stylesheet or asset."""

import argparse
import base64
import hashlib
import html
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'hosts/web'
# Dependencies first: a module's imports are renamed before its own digest is taken.
MODULES = ('ui-wire.mjs', 'dom.mjs', 'nvm.js', 'ui.mjs', 'boot.mjs')
IMPORT = re.compile(r"""(from\s+|import\s*\(\s*)'\./([A-Za-z0-9_.-]+)'""")
CSP = ("default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self'; img-src 'self'; "
       "font-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'none'")
MEDIA_TYPES = {'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
               '.nvr': 'application/octet-stream', '.json': 'application/json'}
FILE_LIMIT = 64 * 1024 * 1024  # W-1
APP_ID = re.compile(r'^[a-z][a-z0-9-]*(\.[a-z][a-z0-9-]*)+$')


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
    out = args.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = {}

    def add(path, data):
        if len(data) > FILE_LIMIT:
            sys.exit(f'build-web-bundle: {path} exceeds W-1 ({FILE_LIMIT} bytes)')
        (out / path).write_bytes(data)
        files[path] = data

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
    image = args.image.read_bytes()
    image_path = named('app.nvr', image)
    add(image_path, image)

    boot = json.dumps({'image': {'path': image_path, 'digest': digest(image)},
                       'routing': args.routing,
                       'wasm': {'path': renamed['nvm.wasm'], 'digest': digest(files[renamed['nvm.wasm']])}},
                      sort_keys=True, separators=(',', ':'))
    boot_module = renamed['boot.mjs']
    integrity = 'sha256-' + base64.b64encode(hashlib.sha256(files[boot_module]).digest()).decode()
    page = f'''<!doctype html>
<html lang="{html.escape(args.lang)}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Content-Security-Policy" content="{html.escape(CSP)}">
<title>{html.escape(args.title)}</title>
<script type="application/json" id="neura-boot">{boot}</script>
<script type="module" src="./{boot_module}" integrity="{integrity}"></script>
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
        'capabilities': ['clock', 'console', 'entropy', 'ui'],
        'contracts': [],
        'csp': CSP,
        'files': [{'bytes': len(data), 'initial': True,
                   'media_type': MEDIA_TYPES[Path(path).suffix], 'path': path, 'sha256': digest(data)}
                  for path, data in sorted(files.items())],
        'id': args.id,
        'image': {'digest': digest(image), 'path': image_path},
        'kind': 'web',
        'limits': {'W-1': FILE_LIMIT},
        'name': args.name,
        'origins': {},
        'requires': {'browser_adapter': 1, 'ui_host': 1},
        'routing': args.routing,
        'schema': 1,
        'version': args.version,
    }
    text = json.dumps(manifest, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()
    (out / 'bundle.json').write_bytes(text)
    if args.routing == 'history':
        shutil.copy(out / 'index.html', out / '404.html')
        print('build-web-bundle: history routing: the host must serve 404.html (a copy of index.html) for '
              'unknown paths')
    print(f'{out}: {len(files) + 1} files, bundle {digest(text)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', required=True, type=Path)
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
    build(parser.parse_args())


if __name__ == '__main__':
    main()
