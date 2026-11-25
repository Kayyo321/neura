"""Win32 process and window operations used by the desktop fixtures."""

from contextlib import contextmanager
import ctypes
from ctypes import wintypes
from io import TextIOBase
import subprocess
import sys
import time


kernel = ctypes.WinDLL("kernel32", use_last_error=True)
user = ctypes.WinDLL("user32", use_last_error=True)
window_callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

kernel.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
kernel.CreateJobObjectW.restype = wintypes.HANDLE
kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD]
kernel.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD,
                                          ctypes.POINTER(wintypes.DWORD)]
kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
kernel.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel.WaitForSingleObject.restype = wintypes.DWORD
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
user.EnumWindows.argtypes = [window_callback, wintypes.LPARAM]
user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user.IsWindowVisible.argtypes = [wintypes.HWND]
user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

PROCESS_TERMINATE = 0x0001
PROCESS_SET_QUOTA = 0x0100
SYNCHRONIZE = 0x00100000
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 0x0102
WM_CLOSE = 0x0010
JOB_BASIC_ACCOUNTING = 1
JOB_EXTENDED_LIMIT = 9
JOB_KILL_ON_CLOSE = 0x2000


class JobLimit(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_longlong), ("job_time", ctypes.c_longlong),
                ("flags", wintypes.DWORD), ("min_working_set", ctypes.c_size_t),
                ("max_working_set", ctypes.c_size_t), ("process_limit", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                ("scheduling", wintypes.DWORD)]


class JobExtendedLimit(ctypes.Structure):
    _fields_ = [("basic", JobLimit), ("io_counters", ctypes.c_ulonglong * 6),
                ("process_memory", ctypes.c_size_t), ("job_memory", ctypes.c_size_t),
                ("peak_process_memory", ctypes.c_size_t), ("peak_job_memory", ctypes.c_size_t)]


class JobAccounting(ctypes.Structure):
    _fields_ = [("user_time", ctypes.c_longlong), ("kernel_time", ctypes.c_longlong),
                ("period_user_time", ctypes.c_longlong), ("period_kernel_time", ctypes.c_longlong),
                ("page_faults", wintypes.DWORD), ("total_processes", wintypes.DWORD),
                ("active_processes", wintypes.DWORD), ("terminated_processes", wintypes.DWORD)]


def checked(result):
    if not result:
        raise ctypes.WinError(ctypes.get_last_error())
    return result


@contextmanager
def process_handle(pid, access=SYNCHRONIZE):
    handle = checked(kernel.OpenProcess(access, False, pid))
    try:
        yield handle
    finally:
        kernel.CloseHandle(handle)


def process_running(handle):
    state = kernel.WaitForSingleObject(handle, 0)
    if state not in (WAIT_OBJECT_0, WAIT_TIMEOUT):
        raise ctypes.WinError(ctypes.get_last_error())
    return state == WAIT_TIMEOUT


def kill_process(handle):
    checked(kernel.TerminateProcess(handle, 137))


def wait_for_job(job, seconds):
    account = JobAccounting()
    deadline = time.monotonic() + seconds
    while True:
        checked(kernel.QueryInformationJobObject(job, JOB_BASIC_ACCOUNTING, ctypes.byref(account),
                                                 ctypes.sizeof(account), None))
        if account.active_processes == 0 or time.monotonic() >= deadline:
            return account.active_processes
        time.sleep(0.05)


@contextmanager
def fixture_job(command, **kwargs):
    """Contain the fixture tree from its first spawn and check it before cleanup."""
    job = checked(kernel.CreateJobObjectW(None, None))
    limits = JobExtendedLimit(basic=JobLimit(flags=JOB_KILL_ON_CLOSE))
    try:
        checked(kernel.SetInformationJobObject(job, JOB_EXTENDED_LIMIT, ctypes.byref(limits), ctypes.sizeof(limits)))
        # The helper waits on stdin so assignment precedes every Electron spawn. It forwards the exit code
        # and passes along the inherited output handles.
        gate = ("import subprocess, sys\n"
                "if sys.stdin.buffer.read(1) != b'1': sys.exit(1)\n"
                "sys.exit(subprocess.call(sys.argv[1:], stdin=subprocess.DEVNULL))")
        with subprocess.Popen([sys.executable, "-c", gate, *command], stdin=subprocess.PIPE, **kwargs) as process:
            try:
                with process_handle(process.pid, PROCESS_SET_QUOTA | PROCESS_TERMINATE) as handle:
                    checked(kernel.AssignProcessToJobObject(job, handle))
                try:
                    process.stdin.write("1" if isinstance(process.stdin, TextIOBase) else b"1")
                finally:
                    process.stdin.close()
                    process.stdin = None
                yield process, lambda: checked(kernel.TerminateJobObject(job, 137))
                if process.poll() is not None and (active := wait_for_job(job, 10)):
                    raise RuntimeError(f"the Electron fixture left {active} running processes")
            finally:
                kernel.TerminateJobObject(job, 137)
                if process.poll() is None:
                    process.kill()
                wait_for_job(job, 5)
    finally:
        kernel.CloseHandle(job)


def error_dialog(pid, title):
    windows = []

    @window_callback
    def visit(window, _):
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(owner))
        if owner.value == pid and user.IsWindowVisible(window):
            buffer = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(window, buffer, len(buffer))
            if buffer.value == "#32770":
                user.GetWindowTextW(window, buffer, len(buffer))
                if buffer.value == title:
                    windows.append(window)
        return True

    checked(user.EnumWindows(visit, 0))
    return next(iter(windows), None)


def close_window(window):
    checked(user.PostMessageW(window, WM_CLOSE, 0, 0))
