#!/usr/bin/env python3
"""`neura new` and its templates (docs/design/Projects030.md, order of work step 5; cases 17 and 40).

Writes a project from each template, through `neura new` and through `neura-new` directly, checks that both are
identical and that the output depends only on the template, the name and the id, then builds every target of
each (the web targets with a stand-in browser runtime) and asks the fullstack server for its parts. Checks that a
directory holding a file is refused with nothing written, and that examples/apps/stock is the fullstack template
named `stock`.

usage: new.py <neura> <source directory> <scratch directory>
"""

import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(sys.argv[2]).resolve()
SCRATCH = Path(sys.argv[3]).resolve()
TARGETS = {'web': ['web'], 'server': ['server'], 'desktop': ['web', 'desktop'],
           'fullstack': ['web', 'server', 'desktop']}
failures = []


def check(condition, what):
    print(('ok   ' if condition else 'FAIL ') + what)
    if not condition:
        failures.append(what)


def run(*args, cwd=None, env=None, tool=None):
    executable = NEURA.parent / tool if tool else NEURA
    return subprocess.run([str(executable), *map(str, args)], cwd=cwd, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, timeout=600)


def tree(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in sorted(root.rglob('*')) if p.is_file()}


def runtime():
    directory = SCRATCH / 'web-runtime'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'nvm.wasm').write_bytes(b'\0asm\x01\0\0\0stand-in runtime')
    (directory / 'nvm.mjs').write_text('export default async function () {}\n')
    for module in ('ui-wire.mjs', 'dom.mjs', 'nvm.js', 'ui.mjs', 'fetch.mjs', 'boot.mjs'):
        shutil.copy(SOURCE / 'hosts/web' / module, directory / module)
    return directory


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    env = dict(os.environ, NEURA_WEB_RUNTIME=str(runtime()))
    for template, targets in TARGETS.items():
        project = SCRATCH / f'{template}-app'
        result = run('new', project, '--template', template)
        check(result.returncode == 0 and (project / 'neura.toml').exists() and (project / 'neura.lock').exists(),
              f'neura new --template {template}')
        direct = SCRATCH / 'direct' / f'{template}-app'
        result = run(direct, '--template', template, tool='neura-new')
        check(result.returncode == 0 and tree(direct) == tree(project), f'neura-new --template {template} is the same')
        # Case 17: every target builds at once.
        for target in targets:
            if target == 'desktop' and not sys.platform.startswith('linux'):
                continue  # The native child is built on Linux only in 0.3.0.
            result = run('build', '--target', target, cwd=project, env=env)
            check(result.returncode == 0, f'{template}: build --target {target}')
            if result.returncode != 0:
                print(result.stdout + result.stderr)
        check('dist/' in (project / '.gitignore').read_text(), f'{template}: dist/ is ignored')

    # `neura check` inside a project finds the package's import roots, and refuses an import across packages that
    # the importer does not depend on (E1030, case 31), as `neura build --target` does.
    fullstack = SCRATCH / 'fullstack-app'
    result = run('check', 'ui/main.nra', cwd=fullstack)
    check(result.returncode == 0, 'neura check of a project file imports Contracts.Parts without -I')
    if result.returncode != 0:
        print(result.stdout + result.stderr)
    page = fullstack / 'ui/main.nra'
    original = page.read_text()
    page.write_text(original.replace('let parts = import:Contracts.Parts;',
                                     'let parts = import:Contracts.Parts;\nlet server = import:Server.Main;'))
    for args, what in ((['check', 'ui/main.nra'], 'neura check'), (['build', '--target', 'web'], 'neura build')):
        result = run(*args, cwd=fullstack, env=env)
        output = result.stdout + result.stderr
        check(result.returncode == 1 and 'ui/main.nra:6:14: error[E1030]' in output and "package 'server'" in output,
              f'{what}: the ui package importing Server.Main is E1030 at the import (case 31)')
        if '[E1030]' not in output:
            print(output)
    page.write_text(original)

    # The fullstack server answers from the shared contract.
    port = free_port()
    host = subprocess.Popen([str(NEURA), 'run', str(SCRATCH / 'fullstack-app/dist/server'), '--', '--listen',
                             f'127.0.0.1:{port}'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        body = None
        for _ in range(600):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/parts', timeout=2) as response:
                    body = response.read().decode()
                break
            except OSError:
                time.sleep(0.1)
        check(body == 'Bolt\nNut\nWasher\n', 'fullstack server lists the starting parts')
        request = urllib.request.Request(f'http://127.0.0.1:{port}/parts?name=', method='POST')
        try:
            urllib.request.urlopen(request, timeout=2)
            status = 200
        except urllib.error.HTTPError as error:
            status = error.code
        check(status == 422, 'fullstack server rejects an empty name with the shared rule')
    finally:
        host.terminate()
        host.wait(timeout=30)

    # Case 40: a directory that holds a file is refused, and nothing is written.
    occupied = SCRATCH / 'occupied'
    occupied.mkdir()
    (occupied / 'notes.txt').write_text('mine\n')
    result = run('new', occupied, '--template', 'web')
    check(result.returncode == 1 and 'not an empty directory' in result.stderr and tree(occupied) == {'notes.txt': b'mine\n'},
          'a directory holding a file is refused, nothing written')
    result = run('new', SCRATCH / 'other', '--template', 'mobile')
    check(result.returncode == 1 and 'unknown template' in result.stderr and not (SCRATCH / 'other').exists(),
          'an unknown template is refused')
    result = run('new', SCRATCH / 'Bad_Name', '--template', 'web')
    check(result.returncode == 1 and '--name' in result.stderr, 'a directory name that is not a project name asks for --name')
    result = run('new', SCRATCH / 'Bad_Name', '--template', 'web', '--name', 'good-name', '--id', 'org.example.good')
    check(result.returncode == 0 and 'id = "org.example.good"' in (SCRATCH / 'Bad_Name/neura.toml').read_text(),
          '--name and --id')

    # The reference application is the fullstack template named stock, without its lock.
    stock = SCRATCH / 'stock'
    run('new', stock, '--template', 'fullstack')
    generated = tree(stock)
    generated.pop('neura.lock')
    check(generated == tree(SOURCE / 'examples/apps/stock'), 'examples/apps/stock is `neura new stock --template fullstack`')

    if failures:
        print(f'{len(failures)} failed: {failures}')
        return 1
    shutil.rmtree(SCRATCH, ignore_errors=True)
    print('neura new: all checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
