#!/usr/bin/env python3
"""`neura dev` (docs/design/Projects030.md, order of work step 6; cases 14, 15 and 45).

For a web target: serves the dev bundle on a system-chosen port, swaps the stylesheets when only a stylesheet
changes and reloads after a source change, and stops at the end of its input. For a server target: runs the host,
restarts it after a change, and is stopped by an interrupt and by a hangup; after each stop no process of the host's
process group remains and the port is closed. A busy port is an error before anything starts. A dev bundle is
refused by `neura run` and `neura package` (E1029).

On Windows (W10, W32, W34) `neura dev` runs in a new process group and is stopped with Ctrl+Break, its
termination request; Windows has no hangup a test can send, and Ctrl+C cannot be sent to one group. A host is
gone when no process of its tree, taken while it ran, is left; the test holds a handle to each process of the tree,
so Windows cannot give its process ID to a later process. The server cases run only where this build has
neura-server-host.exe; otherwise neura dev is checked to build the server target and stop without one.

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
WINDOWS = os.name == 'nt'
EXE = '.exe' if WINDOWS else ''
# The server host is part of every POSIX build; on Windows it comes with the Windows server host.
SERVER_HOST = not WINDOWS or (NEURA.parent / f'neura-server-host{EXE}').exists()
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


def read(path):
    with open(path, encoding='utf-8', newline='') as file:
        return file.read()


def write(path, text):
    with open(path, 'w', encoding='utf-8', newline='') as file:
        file.write(text)


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
        # On Windows its own process group, so that Ctrl+Break reaches it and nothing else.
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if WINDOWS else 0
        self.process = subprocess.Popen([str(NEURA), 'dev', *map(str, args)], cwd=project, env=env,
                                        stdin=subprocess.PIPE, stdout=self.output, stderr=subprocess.STDOUT,
                                        creationflags=flags)

    def text(self):
        return self.log.read_text(encoding='utf-8', errors='replace')

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


if WINDOWS:
    import ctypes
    from ctypes import wintypes

    class PROCESSENTRY32W(ctypes.Structure):
        _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD), ('th32ProcessID', wintypes.DWORD),
                    ('th32DefaultHeapID', ctypes.c_size_t), ('th32ModuleID', wintypes.DWORD),
                    ('cntThreads', wintypes.DWORD), ('th32ParentProcessID', wintypes.DWORD),
                    ('pcPriClassBase', ctypes.c_long), ('dwFlags', wintypes.DWORD),
                    ('szExeFile', ctypes.c_wchar * 260)]

    KERNEL32 = ctypes.WinDLL('kernel32', use_last_error=True)
    KERNEL32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    KERNEL32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    KERNEL32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    KERNEL32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    KERNEL32.OpenProcess.restype = wintypes.HANDLE
    KERNEL32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    KERNEL32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    KERNEL32.CloseHandle.argtypes = [wintypes.HANDLE]
    KERNEL32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(ctypes.c_ulonglong)] * 4
    KERNEL32.GetSystemTimePreciseAsFileTime.argtypes = [ctypes.POINTER(ctypes.c_ulonglong)]
    KERNEL32.GetSystemTimePreciseAsFileTime.restype = None


def process_tree(root):
    """The process and its descendants as they run now (Windows; there is no /proc or process group to ask), as
    open handles. A handle keeps its process ID from being given to another process until close_tree()."""
    snapshot = KERNEL32.CreateToolhelp32Snapshot(0x2, 0)  # TH32CS_SNAPPROCESS
    children = {}
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    more = KERNEL32.Process32FirstW(snapshot, ctypes.byref(entry))
    while more:
        children.setdefault(entry.th32ParentProcessID, []).append(entry.th32ProcessID)
        more = KERNEL32.Process32NextW(snapshot, ctypes.byref(entry))
    KERNEL32.CloseHandle(snapshot)
    # A process created after the snapshot is not the one the snapshot listed under that ID. A process created
    # before its listed parent is not its child: its parent ended and the ID went to a later process.
    taken = ctypes.c_ulonglong()
    KERNEL32.GetSystemTimePreciseAsFileTime(ctypes.byref(taken))
    tree, handles, pending = set(), [], [(root, 0)]
    while pending:
        pid, parent_created = pending.pop()
        if pid in tree:
            continue
        handle = KERNEL32.OpenProcess(0x00100000 | 0x1000, False, pid)  # SYNCHRONIZE | QUERY_LIMITED_INFORMATION
        if not handle:
            continue
        created, ended, kernel, user = (ctypes.c_ulonglong() for _ in range(4))
        if (not KERNEL32.GetProcessTimes(handle, ctypes.byref(created), ctypes.byref(ended), ctypes.byref(kernel),
                                         ctypes.byref(user)) or not parent_created <= created.value <= taken.value):
            KERNEL32.CloseHandle(handle)
            continue
        tree.add(pid)
        handles.append(handle)
        pending.extend((child, created.value) for child in children.get(pid, []))
    return handles


def close_tree(handles):
    for handle in handles:
        KERNEL32.CloseHandle(handle)


def running(handle):
    return KERNEL32.WaitForSingleObject(handle, 0) == 0x102  # WAIT_TIMEOUT


def group_members(group):
    """Whether any process of the process group remains, waited for up to five seconds: the hosts' workers are
    reparented when the leader ends and leave the group as they exit. On Windows `group` holds the handles
    process_tree() opened while the host ran."""
    def alive():
        if WINDOWS:
            return any(running(handle) for handle in group)
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
        write(web / 'ui/base.css', 'body { margin: 1px; }\n')
        dev.wait_line(r'stylesheets swapped|page reloads', after=before)
        swapped = json.loads(get(base + '/__neura/dev'))
        check(swapped['version'] == 2 and swapped['stylesOnly'] and swapped['styles'] != state['styles']
              and len(swapped['styles']) == len(state['styles']), 'a stylesheet change swaps the links in order (case 15)')
        before = len(dev.text())
        source = web / 'ui/main.nra'
        write(source, read(source).replace('Add one', 'Add two'))
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
    stops = (((signal.CTRL_BREAK_EVENT, 'a Ctrl+Break'),) if WINDOWS else
             ((signal.SIGINT, 'an interrupt'), (signal.SIGHUP, 'a hangup')))
    if not SERVER_HOST:
        print(f'note: neura-server-host{EXE} is not built here; the server host cases are not run')
        stops = ()
        port = free_port()
        dev = Dev(server, '--target', 'server', '--port', str(port))
        found = dev.wait_line(r'neura-server-host was found neither')
        check(found is not None and (server / 'dist/dev/server/bundle.json').exists(),
              'neura dev builds the server target and says that the host is missing')
        dev.process.send_signal(signal.CTRL_BREAK_EVENT)
        status = dev.finish()
        check(status == 0 and 'neura dev: stopped' in dev.text(), 'a Ctrl+Break stops neura dev')
    for stop, name in stops:
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
        if WINDOWS:
            # Held from here on, so the restarted host cannot have the first host's process ID.
            hosts[0] = process_tree(hosts[0])
        source = server / 'server/main.nra'
        write(source, read(source).replace('"hello")', '"howdy")'))
        before = len(dev.text())
        second = dev.wait_line(r'started neura-server-host \(process (\d+)\)', after=before)
        check(second is not None and int(second.group(1)) != int(first.group(1)), 'a change restarts the host')
        if second:
            hosts.append(int(second.group(1)))
        check(not group_members(hosts[0]), 'the first host and its workers are gone after the restart')
        check(wait_for(lambda: get(f'http://127.0.0.1:{port}/hello') == 'howdy, world\n') is not None,
              'the restarted host serves the change')
        if WINDOWS and second:
            hosts[1] = process_tree(hosts[1])
        write(source, read(source).replace('"howdy")', '"hello")'))
        time.sleep(1)
        dev.process.send_signal(stop)
        status = dev.finish()
        check(status == 0, f'{name} stops neura dev')
        check(all(not group_members(host) for host in hosts), f'no host process remains after {name} (case 14)')
        check(port_closed(port), f'the port is closed after {name}')
        if WINDOWS:
            for host in hosts:
                if isinstance(host, list):
                    close_tree(host)

    # A dev bundle is never run or packaged as a release.
    bundle = server / 'dist/dev/server'
    result = subprocess.run([str(NEURA), 'run', str(bundle)], capture_output=True, text=True, encoding='utf-8',
                            errors='replace', timeout=60)
    check(result.returncode == 1 and '[E1029]' in result.stderr and 'neura dev' in result.stderr,
          'neura run refuses a dev bundle')
    result = subprocess.run([str(NEURA), 'package', '--target', 'server', '--bundle', str(bundle)], cwd=server,
                            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
    check(result.returncode == 1 and '[E1029]' in result.stderr, 'neura package refuses a dev bundle (case 38)')

    if failures:
        print(f'{len(failures)} failed: {failures}')
        return 1
    shutil.rmtree(SCRATCH, ignore_errors=True)
    print('neura dev: all checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
