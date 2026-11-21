#!/usr/bin/env python3
"""Check the Windows product's PE imports, DLL exports and static CRT.

Run from a VS developer environment: check-windows-binaries.py BUILD [--stage DIR] [--service-bin DIR].
BUILD supplies the static libraries' CRT directives; DIR (default BUILD) supplies
the binaries. Only installed tools and the two product DLLs are inspected, not
test fixtures that deliberately exercise loading arbitrary DLLs.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

CORE_TOOLS = {"neura-run", "neura-inspect", "neura-doctor", "neura-update", "neura-uninstall", "neura-server-host"}
SDK_TOOLS = {"neurac", "neura-test", "neura-doc", "neura-cbind", "neura-decompile", "neura-asm", "neura-disasm",
             "neura-explain", "neura-tags", "neura-debug", "neura-lsp", "neura-fmt", "neura-new", "neura-package", "neura-dev"}
# W05: every other non-Neura dependency must be part of Windows itself.
SYSTEM_DLLS = {"kernel32.dll", "ntdll.dll", "advapi32.dll", "bcrypt.dll", "crypt32.dll", "secur32.dll", "ws2_32.dll",
               "mswsock.dll", "shell32.dll", "ole32.dll", "user32.dll", "wer.dll", "psapi.dll"}


def dumpbin(option: str, path: Path) -> str:
    result = subprocess.run(["dumpbin", "/nologo", option, str(path)], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(f"dumpbin {option} {path}: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def imports(text: str) -> set[str]:
    return {line.strip().lower() for line in text.splitlines() if re.fullmatch(r"\s+[\w.-]+\.dll\s*", line, re.I)}


def exports(text: str) -> set[str]:
    return {match.group(1) for line in text.splitlines()
            if (match := re.match(r"\s*\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)", line, re.I))}


def check_kernels(build: Path) -> int:
    archive = build / "neura_kernels.lib"
    text = dumpbin("/symbols", archive)
    defined, undefined = set(), set()
    for line in text.splitlines():
        if "External" not in line or " | " not in line:
            continue
        name = line.split(" | ", 1)[1].split()[0]
        (undefined if " UNDEF " in line else defined).add(name)
    allowed = {"memcpy", "memmove", "memset", "memcmp", "_fltused", "__ImageBase", "__chkstk",
               "__security_check_cookie", "__security_cookie", "__GSHandlerCheck", "__GSHandlerCheck_EH4",
               "__report_rangecheckfailure", "__C_specific_handler"}
    # Debug builds may use compiler-generated run-time checks as well.
    unexpected = {name for name in undefined - defined - allowed if not name.startswith("_RTC_")}
    for name in sorted(unexpected):
        print(f"FAIL kernels call a function outside the memory builtins and compiler hooks: {name}")
    print(f"Checked kernels: {len(undefined - defined)} external symbols; {len(unexpected)} problems.")
    return bool(unexpected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--stage", type=Path)
    parser.add_argument("--service-bin", type=Path)
    parser.add_argument("--kernels-only", action="store_true")
    args = parser.parse_args()
    if args.kernels_only:
        return check_kernels(args.build)
    directory = args.stage or args.build
    if (directory / "bin").is_dir():
        directory /= "bin"
    problems = []
    dlls = sorted(directory.glob("*.dll"))
    expected_dlls = {}
    for role in ("core", "sdk"):
        matches = [path.name for path in dlls if path.name.startswith(f"neura-{role}-")]
        if len(matches) == 1:
            expected_dlls[role] = matches[0]
        else:
            problems.append(f"expected one {role} DLL, got {matches}")
    if set(path.name for path in dlls) - set(expected_dlls.values()):
        problems.append("unexpected DLL in the product bin directory")
    for path in dlls:
        names = exports(dumpbin("/exports", path))
        if not names or any(not name.startswith("neura_tool_") for name in names):
            problems.append(f"{path.name}: expected only neura_tool_* exports, got {sorted(names)}")
        print(f"{path.name}: {len(names)} neura_tool_* exports")
    tools = [directory / "neura.exe", *[directory / f"{name}.exe" for name in sorted(CORE_TOOLS | SDK_TOOLS)]]
    if args.service_bin or not args.stage:
        tools.append((args.service_bin or directory) / "neurad.exe")
    for path in tools + dlls:
        if not path.exists():
            problems.append(f"{path.name}: missing")
            continue
        libraries = imports(dumpbin("/dependents", path))
        neura = {name for name in libraries if name.startswith("neura-")}
        if path.suffix == ".exe":
            names = exports(dumpbin("/exports", path))
            if names:
                problems.append(f"{path.name}: executable exports {sorted(names)}")
            expected = "core" if path.stem in CORE_TOOLS | {"neurad"} else "sdk"
            if path.stem == "neura":
                if neura:
                    problems.append(f"{path.name}: the dispatcher imports {sorted(neura)}")
                if path.stat().st_size > 1024 * 1024:
                    problems.append(f"{path.name}: dispatcher exceeds 1 MiB")
            elif neura != {expected_dlls.get(expected)}:
                problems.append(f"{path.name}: expected one {expected} DLL, got {sorted(neura)}")
        elif neura:
            problems.append(f"{path.name}: W04 self-contained DLL imports {sorted(neura)}")
        unexpected = libraries - neura - SYSTEM_DLLS
        if unexpected:
            problems.append(f"{path.name}: imports DLLs outside W05: {sorted(unexpected)}")
        print(f"{path.name}: imports {', '.join(sorted(libraries)) or 'nothing'}")
    static = sorted(path for path in args.build.rglob("*.lib") if path.name.startswith("neura_")
                    and "vcpkg_installed" not in path.parts and "_deps" not in path.parts)
    if not static:
        problems.append("no Neura static libraries to inspect for the CRT")
    for path in static:
        directives = dumpbin("/directives", path).upper()
        if re.search(r'/DEFAULTLIB:"?(?:MSVCRTD?|MSVCPRTD?|VCRUNTIME(?:D|140D?)?|UCRTD?)(?:\.LIB)?(?:"|\s|$)', directives):
            problems.append(f"{path.name}: uses the DLL CRT")
        if not re.search(r"/DEFAULTLIB:\"?LIBCMTD?\b", directives):
            problems.append(f"{path.name}: no static CRT directive")
    for problem in problems:
        print(f"FAIL {problem}")
    print(f"Checked {len(tools)} tools, {len(dlls)} DLLs and {len(static)} static libraries; {len(problems)} problems.")
    return bool(problems)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
