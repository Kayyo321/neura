#!/usr/bin/env python3
"""Probe the OS memory prerequisites independently, then require Neura locks to work."""
import argparse
import ctypes
import os
import subprocess
import sys


def linux_precondition(requested):
    libc = ctypes.CDLL(None, use_errno=True)
    libc.mmap.restype = ctypes.c_void_p
    libc.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
    for name in ('madvise', 'mlock', 'munlock', 'munmap'):
        getattr(libc, name).argtypes = [ctypes.c_void_p, ctypes.c_size_t] + ([ctypes.c_int] if name == 'madvise' else [])
    page = os.sysconf('SC_PAGESIZE')
    size = (requested + page - 1) // page * page
    address = libc.mmap(None, size, 3, 0x22, -1, 0)
    if address == ctypes.c_void_p(-1).value:
        return 'Linux mmap allocation unavailable'
    locked = False
    try:
        for advice, label in ((16, 'MADV_DONTDUMP'), (18, 'MADV_WIPEONFORK')):
            if libc.madvise(address, size, advice) != 0:
                return f'Linux {label} unavailable: errno {ctypes.get_errno()}'
        if libc.mlock(address, size) != 0:
            return f'Linux mlock {size} bytes unavailable (RLIMIT_MEMLOCK/host policy): errno {ctypes.get_errno()}'
        locked = True
    finally:
        if locked:
            libc.munlock(address, size)
        libc.munmap(address, size)
    return None


def windows_precondition(requested):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = kernel
    wer = ctypes.WinDLL('wer', use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.VirtualAlloc.restype = ctypes.c_void_p
    kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_ulong, ctypes.c_ulong]
    kernel.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_ulong]
    kernel.VirtualLock.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    kernel.VirtualUnlock.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    psapi.GetProcessWorkingSetSizeEx.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_ulong)]
    psapi.SetProcessWorkingSetSizeEx.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_ulong]
    wer.WerRegisterExcludedMemoryBlock.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    wer.WerRegisterExcludedMemoryBlock.restype = ctypes.c_long
    wer.WerUnregisterExcludedMemoryBlock.argtypes = [ctypes.c_void_p]
    size = (requested + 4095) // 4096 * 4096
    process = kernel.GetCurrentProcess()
    minimum, maximum, flags = ctypes.c_size_t(), ctypes.c_size_t(), ctypes.c_ulong()
    if not psapi.GetProcessWorkingSetSizeEx(process, ctypes.byref(minimum), ctypes.byref(maximum), ctypes.byref(flags)):
        return f'Windows working-set query unavailable: error {ctypes.get_last_error()}'
    if not psapi.SetProcessWorkingSetSizeEx(process, minimum.value + size, maximum.value + size, flags):
        return f'Windows working-set quota unavailable: error {ctypes.get_last_error()}'
    address = None
    registered = locked = False
    try:
        address = kernel.VirtualAlloc(None, size, 0x3000, 4)
        if not address:
            return f'Windows VirtualAlloc unavailable: error {ctypes.get_last_error()}'
        status = wer.WerRegisterExcludedMemoryBlock(address, size)
        if status < 0:
            return f'Windows WER dump exclusion unavailable: HRESULT {status & 0xffffffff:#x}'
        registered = True
        if not kernel.VirtualLock(address, size):
            return f'Windows VirtualLock quota unavailable: error {ctypes.get_last_error()}'
        locked = True
    finally:
        if locked:
            kernel.VirtualUnlock(address, size)
        if registered:
            wer.WerUnregisterExcludedMemoryBlock(address)
        if address:
            kernel.VirtualFree(address, 0, 0x8000)
        psapi.SetProcessWorkingSetSizeEx(process, minimum, maximum, flags)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe-only', action='store_true')
    parser.add_argument('--bytes', type=int, default=4096)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if sys.platform.startswith('linux'):
        reason = linux_precondition(args.bytes)
    elif sys.platform == 'win32':
        reason = windows_precondition(args.bytes)
    else:
        reason = f'{sys.platform}: no provider with the required dump and fork exclusions'
    if reason:
        print(f'SKIP locked-buffer supported case: {reason}', flush=True)
        return 77
    if args.probe_only:
        print(f'PASS {sys.platform} locked-buffer OS prerequisites')
        return 0
    command = args.command
    if command[:1] == ['--']:
        command = command[1:]
    if not command:
        parser.error('provide a command or --probe-only')
    return subprocess.run(command, check=False).returncode


if __name__ == '__main__':
    sys.exit(main())
