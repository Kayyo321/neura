#!/usr/bin/env python3
"""`neura dev` (docs/design/Projects030.md, order of work step 6; cases 14, 15 and 45).

For a web target: serves the dev bundle on a system-chosen port, swaps the stylesheets when only a stylesheet
changes and reloads after a source change, and stops at the end of its input. For a server target: runs the host,
restarts it after a change, and is stopped by an interrupt and by a hangup; after each stop no process of the host's
process group remains and the port is closed. A busy port is an error before anything starts. A dev bundle is
refused by `neura run` and `neura package` (E1029).

usage: dev.py <neura> <source directory> <scratch directory>
"""

import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
import urllib.request

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(sys.argv[2]).resolve()
SCRATCH = Path(sys.argv[3]).resolve()
failures = []


def check(condition, what):
    print(('ok   ' if condition else 'FAIL ') + what)
    if not condition:
        failures.append(what)


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


def get(url):
    try:
        with urllib.request.urlopen(url, timeout=2) as response:
            return response.read().decode()
    except OSError:
        return None


def wait_for(predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.1)
    return None


class Dev:
    """`neura dev` with a pipe for standard input and its output collected in a file."""

    def __init__(self, project, *args, env=None):
        self.log = project.parent / f'{project.name}-dev.log'
        self.output = open(self.log, 'w')
        self.process = subprocess.Popen([str(NEURA), 'dev', *map(str, args)], cwd=project, env=env,
                                        stdin=subprocess.PIPE, stdout=self.output, stderr=subprocess.STDOUT)

    def text(self):
        return self.log.read_text()

    def wait_line(self, pattern, seconds=60, after=0):
        return wait_for(lambda: re.search(pattern, self.text()[after:]), seconds)

    def finish(self, seconds=20):
        try:
            status = self.process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            self.process.kill()
            status = None
        self.output.close()
        return status


def group_members(group):
    """Whether any process of the process group remains, waited for up to five seconds: the hosts' workers are
    reparented when the leader ends and leave the group as they exit."""
    def alive():
        try:
            os.killpg(group, 0)
            return True
        except ProcessLookupError:
            return False
        except PermissionError:
            return True
    deadline = time.monotonic() + 5
    while alive() and time.monotonic() < deadline:
        time.sleep(0.1)
    return alive()


def port_closed(port):
    with socket.socket() as probe:
        return probe.connect_ex(('127.0.0.1', port)) != 0


def main():
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    env = dict(os.environ, NEURA_WEB_RUNTIME=str(runtime()))

    # The web target: a system-chosen port, the dev mark, the reload script, the stylesheet swap and the reload.
    web = SCRATCH / 'page'
    check(subprocess.run([str(NEURA), 'new', str(web), '--template', 'web'], capture_output=True).returncode == 0,
          'neura new page')
    (web / 'neura.lock').unlink()
    dev = Dev(web, '--target', 'web', '--port', '0', env=env)
    found = dev.wait_line(r'serving web at http://127\.0\.0\.1:(\d+)/')
    check(found is not None, 'neura dev serves a web target')
    if found:
        port = int(found.group(1))
        base = f'http://127.0.0.1:{port}'
        check((web / 'neura.lock').exists(), 'neura dev writes a missing lock')
        page = get(base + '/')
        check(page is not None and 'neura-dev.js' in page and '<div id="app">' in page, 'the page loads the reload script')
        manifest = json.loads((web / 'dist/dev/web/bundle.json').read_text())
        check(manifest.get('dev') is True, 'the dev bundle is marked dev')
        state = json.loads(get(base + '/__neura/dev'))
        check(state['version'] == 1 and not state['stylesOnly'], 'the first build is version 1')
        before = len(dev.text())
        (web / 'ui/base.css').write_text('body { margin: 1px; }\n')
        dev.wait_line(r'stylesheets swapped|page reloads', after=before)
        swapped = json.loads(get(base + '/__neura/dev'))
        check(swapped['version'] == 2 and swapped['stylesOnly'] and swapped['styles'] != state['styles']
              and len(swapped['styles']) == len(state['styles']), 'a stylesheet change swaps the links in order (case 15)')
        before = len(dev.text())
        source = web / 'ui/main.nra'
        source.write_text(source.read_text().replace('Add one', 'Add two'))
        dev.wait_line(r'stylesheets swapped|page reloads', after=before)
        reloaded = json.loads(get(base + '/__neura/dev'))
        check(reloaded['version'] == 3 and not reloaded['stylesOnly'], 'a source change reloads the page')
        check(get(base + '/index.html') is not None, 'the page is served by name')
    dev.process.stdin.close()
    status = dev.finish()
    check(status == 0 and 'neura dev: stopped' in dev.text(), 'the end of input stops neura dev (case 14)')
    if found:
        check(port_closed(port), 'the port is closed after the stop')

    # Case 45: a busy port is an error before anything is built or started.
    with socket.socket() as busy:
        busy.bind(('127.0.0.1', 0))
        busy.listen()
        taken = busy.getsockname()[1]
        shutil.rmtree(web / 'dist', ignore_errors=True)
        result = subprocess.run([str(NEURA), 'dev', '--target', 'web', '--port', str(taken)], cwd=web, env=env,
                                capture_output=True, text=True, timeout=60)
        check(result.returncode == 1 and 'cannot listen' in result.stderr and not (web / 'dist').exists(),
              'a busy port is refused with nothing built (case 45)')

    # The server target: the host runs, restarts after a change, and is reaped on an interrupt and a hangup.
    server = SCRATCH / 'service'
    subprocess.run([str(NEURA), 'new', str(server), '--template', 'server'], capture_output=True, check=True)
    for stop, name in ((signal.SIGINT, 'an interrupt'), (signal.SIGHUP, 'a hangup')):
        port = free_port()
        dev = Dev(server, '--target', 'server', '--port', str(port))
        first = dev.wait_line(r'started neura-server-host \(process (\d+)\)')
        check(first is not None, f'neura dev starts the server host ({name})')
        if not first:
            dev.process.kill()
            dev.finish()
            continue
        hosts = [int(first.group(1))]
        check(wait_for(lambda: get(f'http://127.0.0.1:{port}/hello') == 'hello, world\n') is not None,
              'the server answers')
        source = server / 'server/main.nra'
        source.write_text(source.read_text().replace('"hello")', '"howdy")'))
        before = len(dev.text())
        second = dev.wait_line(r'started neura-server-host \(process (\d+)\)', after=before)
        check(second is not None and int(second.group(1)) != hosts[0], 'a change restarts the host')
        if second:
            hosts.append(int(second.group(1)))
        check(not group_members(hosts[0]), 'the first host and its workers are gone after the restart')
        check(wait_for(lambda: get(f'http://127.0.0.1:{port}/hello') == 'howdy, world\n') is not None,
              'the restarted host serves the change')
        source.write_text(source.read_text().replace('"howdy")', '"hello")'))
        time.sleep(1)
        dev.process.send_signal(stop)
        status = dev.finish()
        check(status == 0, f'{name} stops neura dev')
        check(all(not group_members(host) for host in hosts), f'no host process remains after {name} (case 14)')
        check(port_closed(port), f'the port is closed after {name}')

    # A dev bundle is never run or packaged as a release.
    bundle = server / 'dist/dev/server'
    result = subprocess.run([str(NEURA), 'run', str(bundle)], capture_output=True, text=True, timeout=60)
    check(result.returncode == 1 and '[E1029]' in result.stderr and 'neura dev' in result.stderr,
          'neura run refuses a dev bundle')
    result = subprocess.run([str(NEURA), 'package', '--target', 'server', '--bundle', str(bundle)], cwd=server,
                            capture_output=True, text=True, timeout=60)
    check(result.returncode == 1 and '[E1029]' in result.stderr, 'neura package refuses a dev bundle (case 38)')

    if failures:
        print(f'{len(failures)} failed: {failures}')
        return 1
    shutil.rmtree(SCRATCH, ignore_errors=True)
    print('neura dev: all checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
