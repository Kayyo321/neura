#!/usr/bin/env python3
"""`neura package` (docs/design/Projects030.md, order of work step 7; cases 38 and 39).

Packages the server target of a fullstack project twice and compares the archives, extracts one and serves the
bundle through its `run` script, and checks the refusals: a web target, `--sign`, a format of another system, a
target without a built bundle (E1033) and a bundle built by `neura dev` (E1029). With NEURA_DESKTOP_ENGINE set to
the pinned Electron executable, it also packages the desktop target through the desktop packager.

On Windows (W31, W32) the server package is a .zip with bin/neura-run.exe, bin/neura-server-host.exe, the core
DLL and run.cmd, and the desktop package a .zip. The server package is built only where this build has
neura-server-host.exe; otherwise that case says that it is not run.

usage: package.py <neura> <source directory> <scratch directory>
"""

import gzip
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(sys.argv[2]).resolve()
SCRATCH = Path(sys.argv[3]).resolve()
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
                          stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='replace', timeout=900)


def stop(process):
    """Ends a started package and everything it started: on Windows run.cmd runs in cmd.exe, whose children
    taskkill ends with it."""
    if WINDOWS:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(process.pid)], capture_output=True)
    else:
        process.terminate()
    process.wait(timeout=30)


def serves(port):
    # A deadline rather than a count: Windows takes about two seconds to refuse a loopback connection.
    body = None
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(f'http://127.0.0.1:{port}/parts', timeout=2) as response:
                body = response.read().decode()
            break
        except OSError:
            time.sleep(0.1)
    return body == 'Bolt\nNut\nWasher\n'


def server_package_windows(project, env):
    """The Windows server package: a reproducible .zip whose run.cmd serves the bundle."""
    first = run('package', '--target', 'server', cwd=project, env=dict(env, SOURCE_DATE_EPOCH='0'))
    archive = project / 'dist/stock-0.1.0-server.zip'
    check(first.returncode == 0 and archive.exists() and '(unsigned)' in first.stdout, 'package the server target')
    if first.returncode != 0:
        print(first.stdout + first.stderr)
        return
    run('package', '--target', 'server', '-o', SCRATCH / 'again.zip', cwd=project)
    check(archive.read_bytes() == (SCRATCH / 'again.zip').read_bytes(), 'two server packages are byte-identical')
    with zipfile.ZipFile(archive) as package:
        check(package.testzip() is None, 'the package is a valid .zip')
        members = package.infolist()
        names = [member.filename for member in members]
        check(names == sorted(names), 'entries in order')
        check(all(m.date_time == (1980, 1, 1, 0, 0, 0) and m.compress_type == zipfile.ZIP_STORED for m in members),
              'entries stored at the earliest .zip time')
        check({'stock-0.1.0/run.cmd', 'stock-0.1.0/bin/neura-run.exe', 'stock-0.1.0/bin/neura-server-host.exe',
               'stock-0.1.0/bundle/bundle.json'} <= set(names), 'the package holds the bundle, its hosts and run.cmd')
        check(any(n.startswith('stock-0.1.0/bin/neura-core-') and n.endswith('.dll') for n in names),
              'the package holds the core DLL beside the executables')
        check(not any(n.endswith('.nra') for n in names), 'the package holds no source')
        check(package.read('stock-0.1.0/run.cmd').startswith(b'@echo off\r\n'), 'run.cmd has CRLF line endings')
        package.extractall(SCRATCH / 'extracted')
    script = SCRATCH / 'extracted/stock-0.1.0/run.cmd'
    port = free_port()
    host = subprocess.Popen([str(script), '--listen', f'127.0.0.1:{port}'], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    try:
        check(serves(port), 'the extracted package serves its bundle')
    finally:
        stop(host)
    bundled = SCRATCH / 'extracted/stock-0.1.0/bundle/server.json'
    bundled.write_bytes(bundled.read_bytes().replace(b'8080', b'8081'))
    result = subprocess.run([str(script)], capture_output=True, text=True, encoding='utf-8', errors='replace',
                            timeout=60)
    check(result.returncode == 1 and '[E1029]' in result.stderr and 'server.json' in result.stderr,
          'a changed file in an extracted package is refused')


def refused(result, code, text, what):
    output = result.stdout + result.stderr
    check(result.returncode != 0 and f'[{code}]' in output and text in output, f'{what}: {code}')
    if result.returncode == 0 or f'[{code}]' not in output or text not in output:
        print(output)


def free_port():
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        return probe.getsockname()[1]


def runtime():
    directory = SCRATCH / 'web-runtime'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'nvm.wasm').write_bytes(b'\0asm\x01\0\0\0stand-in runtime')
    (directory / 'nvm.mjs').write_text('export default async function () {}\n')
    for module in ('ui-wire.mjs', 'dom.mjs', 'nvm.js', 'ui.mjs', 'fetch.mjs', 'boot.mjs'):
        shutil.copy(SOURCE / 'hosts/web' / module, directory / module)
    return directory


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    env = dict(os.environ, NEURA_WEB_RUNTIME=str(runtime()))
    project = SCRATCH / 'stock'
    check(run('new', project, '--template', 'fullstack').returncode == 0, 'neura new stock')
    refused(run('package', '--target', 'server', cwd=project), 'E1033', 'neura build --target server',
            'a target without a built bundle')
    check(run('build', '--target', 'server', cwd=project).returncode == 0, 'build the server bundle')

    if WINDOWS:
        if SERVER_HOST:
            server_package_windows(project, env)
        else:
            print('note: neura-server-host.exe is not built here; the server package is not built')
    else:
        server_package(project, env)
    refusals(project)

    engine = os.environ.get('NEURA_DESKTOP_ENGINE')
    if engine:
        check(run('build', '--target', 'desktop', cwd=project, env=env).returncode == 0, 'build the desktop bundle')
        if WINDOWS:
            result = run('package', '--target', 'desktop', cwd=project, env=env)
            package = project / 'dist/stock-0.1.0-desktop.zip'
            check(result.returncode == 0 and package.exists(), 'package the desktop target as a .zip')
            if result.returncode == 0 and package.exists():
                with zipfile.ZipFile(package) as archive:
                    names = set(archive.namelist())
                check({'stock/resources/app/desktop.json', 'stock/resources/app/web/bundle.json'} <= names,
                      'the desktop package holds desktop.json and the embedded web bundle')
            else:
                print(result.stdout + result.stderr)
        else:
            result = run('package', '--target', 'desktop', '--format', 'tar.gz', cwd=project, env=env)
            package = project / 'dist/stock-0.1.0-desktop.tar.gz'
            check(result.returncode == 0 and package.exists(), 'package the desktop target through the desktop packager')
            with tarfile.open(package) as tar:
                names = set(tar.getnames())
            check({'stock/stock', 'stock/resources/app/desktop.json', 'stock/resources/app/web/bundle.json'} <= names,
                  'the desktop package holds the engine, desktop.json and the embedded web bundle')
    else:
        print('note: NEURA_DESKTOP_ENGINE is not set; the desktop package is not built')

    if failures:
        print(f'{len(failures)} failed: {failures}')
        return 1
    shutil.rmtree(SCRATCH, ignore_errors=True)
    print('neura package: all checks passed')
    return 0


def server_package(project, env):
    """The POSIX server package: a reproducible .tar.gz whose `run` serves the bundle."""
    first = run('package', '--target', 'server', cwd=project, env=dict(env, SOURCE_DATE_EPOCH='0'))
    archive = project / 'dist/stock-0.1.0-server.tar.gz'
    check(first.returncode == 0 and archive.exists() and '(unsigned)' in first.stdout, 'package the server target')
    second = run('package', '--target', 'server', '-o', SCRATCH / 'again.tar.gz', cwd=project)
    check(archive.read_bytes() == (SCRATCH / 'again.tar.gz').read_bytes(), 'two server packages are byte-identical')
    gzip.decompress(archive.read_bytes())
    with tarfile.open(archive) as tar:
        members = tar.getmembers()
        names = [member.name for member in members]
        check(all(m.uid == 0 and m.gid == 0 and m.mtime == 0 for m in members), 'entries owned by 0:0 at time 0')
        check({'stock-0.1.0/run', 'stock-0.1.0/bin/neura-run', 'stock-0.1.0/bin/neura-server-host',
               'stock-0.1.0/bundle/bundle.json'} <= set(names), 'the package holds the bundle, its hosts and run')
        check(any(n.startswith('stock-0.1.0/lib/neura/libneura-core') for n in names), 'the package holds the core library')
        check(not any(n.endswith('.nra') for n in names), 'the package holds no source')
        tar.extractall(SCRATCH / 'extracted', filter='tar')
    port = free_port()
    host = subprocess.Popen([str(SCRATCH / 'extracted/stock-0.1.0/run'), '--listen', f'127.0.0.1:{port}'],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        body = None
        for _ in range(600):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/parts', timeout=2) as response:
                    body = response.read().decode()
                break
            except OSError:
                time.sleep(0.1)
        check(body == 'Bolt\nNut\nWasher\n', 'the extracted package serves its bundle')
    finally:
        host.terminate()
        host.wait(timeout=30)

    # One byte of the packaged bundle changed: the run script refuses it before the host starts.
    bundled = SCRATCH / 'extracted/stock-0.1.0/bundle/server.json'
    bundled.write_text(bundled.read_text().replace('8080', '8081'))
    result = subprocess.run([str(SCRATCH / 'extracted/stock-0.1.0/run')], capture_output=True, text=True, timeout=60)
    check(result.returncode == 1 and '[E1029]' in result.stderr and 'server.json' in result.stderr,
          'a changed file in an extracted package is refused')


def refusals(project):
    refused(run('package', '--target', 'web', cwd=project), 'E1033', 'static file host', 'a web target')
    refused(run('package', '--target', 'server', '--sign', cwd=project), 'E1033', '--sign', '--sign')
    if WINDOWS:
        # An MSIX cannot be installed unsigned, and Neura 0.3.0 signs nothing (W31).
        refused(run('package', '--target', 'desktop', '--format', 'msix', cwd=project), 'E1033', 'MSIX',
                '--format msix on Windows (case 39)')
        refused(run('package', '--target', 'desktop', '--format', 'deb', cwd=project), 'E1033', '.zip',
                'a desktop package in a format of another system')
        refused(run('package', '--target', 'server', '--format', 'tar.gz', cwd=project), 'E1033', '.zip',
                'a server package in another format')
    else:
        refused(run('package', '--target', 'desktop', '--format', 'msix', cwd=project), 'E1033', 'Windows',
                '--format msix on Linux (case 39)')
        refused(run('package', '--target', 'server', '--format', 'deb', cwd=project), 'E1033', 'tar.gz',
                'a server package in another format')

    # Case 38: a bundle built by neura dev, marked "dev" in bundle.json.
    dev = SCRATCH / 'dev-bundle'
    shutil.copytree(project / 'dist/server', dev)
    manifest = (dev / 'bundle.json').read_text()
    (dev / 'bundle.json').write_text(manifest.replace('"contracts":[],', '"contracts":[],"dev":true,', 1))
    refused(run('package', '--target', 'server', '--bundle', dev, cwd=project), 'E1029', 'neura dev',
            'a development bundle (case 38)')


if __name__ == '__main__':
    sys.exit(main())
