#!/usr/bin/env python3
"""Project builds end to end (docs/design/Projects030.md, order of work step 4).

Builds the fixture project of tests/project/build/app (a web, a server and a desktop target) with
`neura build --project --target`, from two directories with different absolute paths, and compares the bundles
byte for byte. The web target uses a stand-in browser runtime (fixed nvm.wasm and nvm.mjs bytes beside the real
page modules of hosts/web), so the check needs no Emscripten build; the page itself is checked in browsers by
tests/apps/web. Then it checks the refusals of the edge cases (E1022, E1024, E1027, E1029, E1031), `neura run`
of each kind of bundle, and that a tampered bundle is refused before anything runs.

On Windows a server bundle runs only where this build has neura-server-host.exe, and the desktop target is
built only where it has the native launcher; otherwise those cases say that they are not run.

usage: build.py <neura> <source directory> <scratch directory>
"""

import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(sys.argv[2]).resolve()
SCRATCH = Path(sys.argv[3]).resolve()
FIXTURE = SOURCE / 'tests/project/build/app'
WINDOWS = os.name == 'nt'
EXE = '.exe' if WINDOWS else ''
# The server host is part of every POSIX build; on Windows it comes with the Windows server host.
SERVER_HOST = not WINDOWS or (NEURA.parent / f'neura-server-host{EXE}').exists()
failures = []


def check(condition, what):
    print(('ok   ' if condition else 'FAIL ') + what)
    if not condition:
        failures.append(what)


def run(*args, cwd=None, env=None):
    return subprocess.run([str(NEURA), *map(str, args)], cwd=cwd, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace', timeout=600)


def http_client_operator_arguments(first):
    """The HTTP client option `neura run` gives the server host, seen through a stand-in host.

    The bundle's `http_client_origins` requirement, not the presence of `origins.connect`, decides whether the
    grant is scoped, and operator arguments after `--` cannot widen a scoped grant.
    """
    if WINDOWS:
        print('note: the stand-in server host is a shell script; the HTTP client option checks are not run here')
        return
    tools = SCRATCH / 'stand-in-tools'
    shutil.rmtree(tools, ignore_errors=True)
    tools.mkdir()
    # `neura run` hands a bundle to neura-run, which finds the server host beside itself.
    run_tool = NEURA.parent / 'neura-run'
    try:
        os.link(run_tool, tools / 'neura-run')
    except OSError:
        shutil.copy2(run_tool, tools / 'neura-run')
    host = tools / 'neura-server-host'
    host.write_text('#!/bin/sh\nfor argument in "$@"; do printf "%s\\n" "$argument"; done\n')
    host.chmod(0o755)

    def host_arguments(bundle, *operator):
        result = subprocess.run([str(tools / 'neura-run'), str(bundle), '--', *operator], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace', timeout=60)
        return result, [line for line in result.stdout.splitlines() if 'http-client' in line]

    def bundle_copy(name, change):
        copy = SCRATCH / name
        shutil.rmtree(copy, ignore_errors=True)
        shutil.copytree(first / 'dist/server', copy)
        change(copy / 'bundle.json')
        return copy

    listed = first / 'dist/server'
    no_key = bundle_copy('server-no-connect-key', lambda b: edit(b, '"origins":{"connect":[]}', '"origins":{}'))
    unscoped = bundle_copy('server-unscoped', lambda b: edit(b, '"http_client_origins":1,', ''))
    for bundle, expected, what in ((listed, ['--allow-http-client='], 'an explicit empty origin list'),
                                   (no_key, ['--allow-http-client='], 'the requirement without a connect list'),
                                   (unscoped, ['--allow-http-client'], 'a connect list without the requirement')):
        result, options = host_arguments(bundle)
        check(result.returncode == 0 and options == expected, f'the server host is given {expected} for {what}')
        if result.returncode != 0 or options != expected:
            print(result.stdout + result.stderr)
    for bundle in (listed, no_key):
        for option in ('--allow-http-client', '--allow-http-client=https://other.example'):
            result, _ = host_arguments(bundle, option)
            check(result.returncode != 0 and "cannot widen the bundle's HTTP client origin list" in result.stderr
                  and option in result.stderr, f'{bundle.name}: {option} after -- is refused')
        result, options = host_arguments(bundle, '--deny-http-client')
        check(result.returncode == 0 and options == ['--allow-http-client=', '--deny-http-client'],
              f'{bundle.name}: --deny-http-client after -- narrows the grant')
    result, options = host_arguments(unscoped, '--allow-http-client=https://other.example')
    check(result.returncode == 0 and options == ['--allow-http-client', '--allow-http-client=https://other.example'],
          'an unscoped bundle passes the operator HTTP client options through')
    shutil.rmtree(tools, ignore_errors=True)


def refused(result, code, text, what):
    output = result.stdout + result.stderr
    check(result.returncode != 0 and f'[{code}]' in output and text in output, f'{what}: {code}')
    if result.returncode == 0 or f'[{code}]' not in output:
        print(output)


def tree(root):
    return {str(p.relative_to(root)): p.read_bytes() for p in sorted(root.rglob('*')) if p.is_file()}


def copy_project(name):
    target = SCRATCH / name
    shutil.rmtree(target, ignore_errors=True)
    shutil.copytree(FIXTURE, target)
    return target


def runtime():
    directory = SCRATCH / 'web-runtime'
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)
    (directory / 'nvm.wasm').write_bytes(b'\0asm\x01\0\0\0stand-in runtime')
    (directory / 'nvm.mjs').write_text('export default async function () {}\n')
    for module in ('ui-wire.mjs', 'dom.mjs', 'nvm.js', 'ui.mjs', 'fetch.mjs', 'boot.mjs'):
        shutil.copy(SOURCE / 'hosts/web' / module, directory / module)
    return directory


def edit(path, old, new):
    # UTF-8 and the file's own line endings on every host.
    with open(path, encoding='utf-8', newline='') as file:
        text = file.read()
    assert old in text, (path, old)
    with open(path, 'w', encoding='utf-8', newline='') as file:
        file.write(text.replace(old, new, 1))


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    env = dict(os.environ, NEURA_WEB_RUNTIME=str(runtime()))
    first = copy_project('first')
    second = copy_project('second dir é')

    # E1022: no lock yet (case 23).
    refused(run('build', '--target', 'server', cwd=first, env=env), 'E1022', 'neura.lock', 'missing lock')
    result = run('build', '--project', first / 'neura.toml', '--target', 'server', '--update-lock', env=env)
    check(result.returncode == 0 and (first / 'neura.lock').exists(), '--update-lock writes neura.lock')
    shutil.copy(first / 'neura.lock', second / 'neura.lock')

    # Cases 5, 6, 7, 9: every target, the desktop one building its web target; run from a subdirectory with
    # --project ../neura.toml; two directories give identical bundles.
    # A desktop target's child is a native executable, which needs this system's native launcher.
    native = any((NEURA.parent / 'lib/neura/launchers').glob(f'*/neura-launcher{EXE}'))
    if WINDOWS:
        check(native, 'the Windows SDK includes the PE launcher')
    if not native:
        print(f'note: no native launcher in {NEURA.parent / "lib/neura/launchers"}; the desktop target is not built')
    for name in ('server', 'web', 'desktop') if native else ('server', 'web'):
        for project in (first, second):
            result = run('build', '--project', '../neura.toml', '--target', name, cwd=project / 'server', env=env)
            if not WINDOWS and name == 'desktop' and result.returncode != 0 and 'native' in (result.stdout + result.stderr):
                native = False
                print(f'note: the desktop target needs native builds, which this build lacks: {result.stderr.strip()}')
                break
            check(result.returncode == 0, f'build {name} in {project.name}')
            if result.returncode != 0:
                print(result.stdout + result.stderr)
        if name != 'desktop' or native:
            check(tree(first / 'dist' / name) == tree(second / 'dist' / name), f'{name}: identical bytes from two paths')
    again = tree(first / 'dist/web')
    run('build', '--target', 'web', cwd=first, env=env)
    check(tree(first / 'dist/web') == again, 'web: identical bytes when built twice')
    if WINDOWS:
        # The previous bundle is moved aside before the new one moves in (W32). A leftover aside directory that a
        # program still holds open is skipped for another name, and removed by the first build after it is free.
        aside = first / 'dist/web.neura-old'
        aside.mkdir()
        with open(aside / 'held', 'w') as held:  # open without FILE_SHARE_DELETE, so it cannot be removed
            held.write('held')
            held.flush()
            results = [run('build', '--target', 'web', cwd=first, env=env) for _ in range(2)]
        check(all(result.returncode == 0 for result in results) and tree(first / 'dist/web') == again,
              'web: a held leftover of a previous bundle does not stop a rebuild')
        result = run('build', '--target', 'web', cwd=first, env=env)
        check(result.returncode == 0 and not any('.neura-' in path.name for path in (first / 'dist').iterdir()),
              'web: the next build removes the released leftovers')

    web = json.loads((first / 'dist/web/bundle.json').read_text())
    files = {entry['path']: entry for entry in web['files']}
    check(web['kind'] == 'web' and 'index.html' in files and web['image']['path'] in files, 'web bundle.json lists the page and image')
    check(not any(path.endswith('.nra') or path.endswith('.css') and not path.startswith('styles/') for path in files),
          'web bundle holds no source')
    check(any(path.startswith('assets/') and path.endswith('.svg') and not entry['initial'] for path, entry in files.items()),
          'package assets are listed by digest and not initial (case 3)')
    page = (first / 'dist/web/index.html').read_text()
    import html
    check(html.unescape(page).count(web['csp']) == 1 and "default-src 'none'" in web['csp'] and 'form-action' in web['csp'],
          'index.html carries the content-security policy of bundle.json')
    check(page.index('styles/') < page.index('neura-boot'), 'global stylesheet linked before the boot script')
    server = json.loads((first / 'dist/server/bundle.json').read_text())
    check(server['requires'].get('http_client_origins') == 1 and server['origins'].get('connect') == [],
          'a server bundle preserves an explicit empty HTTP origin list')
    if native:
        desktop = json.loads((first / 'dist/desktop/bundle.json').read_text())
        import hashlib
        check(desktop['ui'] == hashlib.sha256((first / 'dist/web/bundle.json').read_bytes()).hexdigest(),
              'desktop bundle embeds the web bundle with the same digest')
        check((first / 'dist/desktop/web/bundle.json').read_bytes() == (first / 'dist/web/bundle.json').read_bytes(),
              'embedded web bundle unchanged')
        check(desktop['requires'].get('http_client_origins') == 1 and
              desktop['origins'].get('connect') == ['https://api.example:8443'],
              'desktop bundle inherits the embedded web target connect list')
        child = first / 'dist/desktop' / desktop['image']['path']
        if WINDOWS:
            with child.open('rb') as executable:
                check(child.suffix == '.exe' and executable.read(2) == b'MZ', 'desktop bundle contains a Win64 PE child')
        policy = run('inspect', child)
        check(policy.returncode == 0 and 'http_client (https://api.example:8443)' in policy.stdout,
              'the desktop native child records its HTTP origin list')
        # With no connect list on the desktop target or its ui target, the native child reaches no origin.
        unlisted = copy_project('desktop-no-connect')
        shutil.copy(first / 'neura.lock', unlisted / 'neura.lock')
        edit(unlisted / 'neura.toml', '[targets.web.origins]\nconnect = ["https://api.example:8443"]\n', '')
        result = run('build', '--target', 'desktop', cwd=unlisted, env=env)
        check(result.returncode == 0, 'build a desktop target with no connect list')
        if result.returncode == 0:
            bare = json.loads((unlisted / 'dist/desktop/bundle.json').read_text())
            check(bare['requires'].get('http_client_origins') == 1 and bare['origins'].get('connect') == [],
                  'a desktop bundle with no connect list has an empty HTTP origin list')
            policy = run('inspect', unlisted / 'dist/desktop' / bare['image']['path'])
            check(policy.returncode == 0 and 'http_client (no origins)' in policy.stdout,
                  'the desktop native child with no connect list reaches no origin')
        else:
            print(result.stdout + result.stderr)

    # Case 12: history routing writes 404.html and says what the host must do.
    routed = copy_project('routed')
    shutil.copy(first / 'neura.lock', routed / 'neura.lock')
    edit(routed / 'neura.toml', 'styles = ["ui/base.css"]', 'styles = ["ui/base.css"]\nrouting = "history"')
    result = run('build', '--target', 'web', cwd=routed, env=env)
    check(result.returncode == 0 and (routed / 'dist/web/404.html').read_bytes() == (routed / 'dist/web/index.html').read_bytes()
          and 'history routing' in result.stdout, 'history routing writes 404.html and states the host rule (case 12)')

    # Case 16: a single-file build in a project directory is a single-file build.
    result = run('build', 'ui/main.nra', '-o', SCRATCH / 'single.nvr', cwd=first)
    check(result.returncode == 0 and (SCRATCH / 'single.nvr').exists(), 'single-file build beside neura.toml')

    # Case 41: a web bundle is not runnable.
    result = run('run', first / 'dist/web')
    check(result.returncode == 1 and 'neura dev' in result.stderr and 'static' in result.stderr, 'neura run of a web bundle is refused')

    # Case 13: neura run of a server bundle, with arguments for the host.
    if SERVER_HOST:
        port = free_port()
        database = SCRATCH / 'counter.db'
        host = subprocess.Popen([str(NEURA), 'run', str(first / 'dist/server'), '--', '--listen', f'127.0.0.1:{port}',
                                 '--sql-pool', f'main=sqlite:{database}', '--config', 'greeting=hi'],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                errors='replace')
        try:
            body = None
            for _ in range(600):
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/hello?name=Ada', timeout=2) as response:
                        body = response.read().decode()
                    break
                except OSError:
                    time.sleep(0.1)
            check(body == 'hi, Ada\n', 'neura run serves a server bundle')
        finally:
            # On Windows this ends neura and, through their kill-on-close jobs, neura-run and the host.
            host.terminate()
            output, _ = host.communicate(timeout=30)
            if body != 'hi, Ada\n':
                print(output)
        http_client_operator_arguments(first)
    else:
        print(f'note: neura-server-host{EXE} is not built here; neura run of a server bundle is not run')

    # Cases 42, 43, 44 and a dev bundle: refused before anything runs.
    def tampered(name, change):
        copy = SCRATCH / f'tampered-{name}'
        shutil.rmtree(copy, ignore_errors=True)
        shutil.copytree(first / 'dist/server', copy)
        change(copy)
        return run('run', copy)
    refused(tampered('extra', lambda d: (d / 'extra.txt').write_text('x')), 'E1029', 'unlisted file', 'extra file')
    refused(tampered('byte', lambda d: edit(d / 'migrations/main/0001_create_parts.sql', 'parts', 'partz')),
            'E1029', 'migrations/main/0001_create_parts.sql', 'one changed byte')
    refused(tampered('axis', lambda d: edit(d / 'bundle.json', '"server_host":1', '"server_host":2')),
            'E1031', "'server_host' needs version 2, host has 1", 'unsupported host interface')
    refused(tampered('dev', lambda d: edit(d / 'bundle.json', '"contracts":[],', '"contracts":[],"dev":true,')),
            'E1029', 'neura dev', 'development bundle')
    result = run('run', SCRATCH)
    check(result.returncode != 0, 'a directory without bundle.json is not run')

    # Refused builds.
    def variant(name, change, target, code, text, what):
        project = copy_project(f'variant-{name}')
        shutil.copy(first / 'neura.lock', project / 'neura.lock')
        change(project)
        refused(run('build', '--target', target, cwd=project, env=env), code, text, what)

    variant('lock', lambda p: edit(p / 'neura.toml', 'providers = ["sqlite"]', 'providers = []'), 'server',
            'E1022', 'sqlite', 'lock lists a provider the manifest dropped (case 23)')
    variant('mysql', lambda p: edit(p / 'neura.toml', 'providers = ["sqlite"]', 'providers = ["mysql"]'), 'server',
            'E1031', "'mysql' is unknown", 'unknown provider (case 36)')
    variant('entry', lambda p: edit(p / 'neura.toml', 'entry = "Ui.Main"', 'entry = "Ui.Missing"'), 'web',
            'E1024', 'Ui.Missing', 'entry module that does not exist')
    variant('limit', lambda p: edit(p / 'neura.toml', 'capabilities = ["ui"]', 'capabilities = ["ui"]\n\n[targets.web.limits]\nW-1 = 64'),
            'web', 'E1029', 'over W-1', 'a file over W-1 (case 35)')

    def fs_use(project):
        edit(project / 'ui/main.nra', 'let web = import:Std.Web;', 'let web = import:Std.Web;\nlet files = import:Std.Fs.Fs;\nlet paths = import:Std.Fs.Path;')
        edit(project / 'ui/main.nra', 'last counter = make_counter();', 'last counter = make_counter();\n        last found = files.exists(paths.of("x"));')
    variant('fs', fs_use, 'web', 'E1027', 'neura.fs.', 'a web target that reaches the file system (case 28)')
    variant('unlisted', lambda p: edit(p / 'neura.toml', 'capabilities = ["http_server", "app", "sql", "http_client"]',
                                       'capabilities = ["sql"]'),
            'server', 'E1027', "group 'app', not listed", 'a server image using a group it does not list')

    if failures:
        print(f'{len(failures)} failed: {failures}')
        return 1
    shutil.rmtree(SCRATCH, ignore_errors=True)
    print('project builds: all checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
