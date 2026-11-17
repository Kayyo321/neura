#!/usr/bin/env python3
"""Check that the execution core links no compiler code.

    scripts/check-core-closure.py BUILD_DIR

The core component (docs/design/Products030.md) holds `neura`, `neura-run`,
`neura-inspect`, `neura-doctor`, `neura-update`, `neura-uninstall` and the
shared library `lib/neura/libneura-core-<version>`. A static library
contributes an object file to a binary only to define a global symbol the
binary needs, so an object of a compiler library is in a binary exactly when
one of its global definitions is. This script lists the global definitions
(not weak ones, which header code shares) of every object of the SDK's
static libraries in BUILD_DIR, and fails when a core binary defines any of
them, or when one needs (DT_NEEDED, or a load command on macOS) the SDK's
shared library. It also fails when `libneura-sdk` holds an object of the
core's static libraries, which would give the two libraries separate copies
of the core's state.

`neura-doctor` may hold the C import stage's object (`c_import.cpp`), whose
libclang probe it calls; the script reports it. A build configured with
NEURA_SDK=OFF has no SDK library, which the script reports as the stronger
evidence. It needs `nm` (binutils or the Xcode tools).
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

CORE_TOOLS = ["neura", "neura-run", "neura-inspect", "neura-doctor", "neura-update", "neura-uninstall"]
SDK_LIBRARIES = [
    "libneura_compiler.a",
    "libneura_decompiler.a",
    "libneura_cli_sdk.a",
    "libneura_cli_artifact.a",
    "libneura_cli_explain.a",
    "libneura_cli_tags.a",
    "third_party/neura-libclang/libneura_libclang.a",
]
CORE_LIBRARIES = [
    "libneura_cli_core.a", "libneura_cli_tool.a", "libneura_cli_registry.a", "libneura_runner.a", "libneura_nvm.a",
    "libneura_lift.a", "libneura_circuit.a", "libneura_nvr.a", "libneura_kernels.a", "libneura_lifecycle.a",
    "libneura_gpu.a",
]
# Objects a core tool may hold, with the reason.
ALLOWED = {
    "neura-doctor": {
        "c_import.cpp.o": "the libclang probe of `neura doctor`",
        "libclang_loader.c.o": "the libclang loader that the probe opens libclang with",
    },
}
GLOBAL_KINDS = set("TDBRCGS")


def symbols(path: Path, defined_only: bool = True) -> list[tuple[str, str, str]]:
    """(object, kind, name) for each global definition; object is '' for an executable."""
    arguments = ["nm", "-A", "-g"]
    if defined_only:
        arguments.append("--defined-only")
    result = subprocess.run(arguments + [str(path)], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"nm failed on {path}: {result.stderr.strip()}")
    found = []
    for line in result.stdout.splitlines():
        # GNU: "file[:member]:address kind name"; Apple: "file[:member]: address kind name".
        parts = line.split()
        if len(parts) < 3 or parts[-2] not in GLOBAL_KINDS:
            continue
        kind, name = parts[-2], parts[-1]
        head = " ".join(parts[:-2]).rstrip()
        head = head.rsplit(" ", 1)[0].rstrip(":") if " " in head else head.rsplit(":", 1)[0]
        fields = head.split(":")
        member = fields[-1] if len(fields) > 1 and fields[-1].endswith(".o") else ""
        found.append((member, kind, name))
    return found


def needed(path: Path) -> list[str]:
    """The shared libraries a binary loads by name."""
    if sys.platform == "darwin":
        result = subprocess.run(["otool", "-L", str(path)], capture_output=True, text=True)
        return [Path(line.split()[0]).name for line in result.stdout.splitlines()[1:] if line.strip()]
    result = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True)
    return [line.split("[", 1)[1].rstrip("]") for line in result.stdout.splitlines() if "(NEEDED)" in line]


def owners_of(build: Path, libraries: list[str]) -> tuple[dict[str, str], list[str]]:
    owners: dict[str, str] = {}
    present = [library for library in libraries if (build / library).exists()]
    for library in present:
        for member, _, name in symbols(build / library):
            owners.setdefault(name, f"{Path(library).name}({member})")
    return owners, present


def held(path: Path, owners: dict[str, str]) -> dict[str, int]:
    linked: dict[str, int] = {}
    for _, _, name in symbols(path):
        if name in owners:
            linked[owners[name]] = linked.get(owners[name], 0) + 1
    return linked


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build = Path(sys.argv[1])
    owners, present = owners_of(build, SDK_LIBRARIES)
    libraries = sorted((build / "lib/neura").glob("libneura-*"))
    core_shared = [library for library in libraries if library.name.startswith("libneura-core")]
    sdk_shared = [library for library in libraries if library.name.startswith("libneura-sdk")]
    failed = False
    if not core_shared:
        print(f"FAIL {build}/lib/neura holds no libneura-core")
        failed = True
    for path in [build / tool for tool in CORE_TOOLS] + core_shared:
        name = path.name
        if not path.exists():
            print(f"FAIL {name}: not built in {build}")
            failed = True
            continue
        linked = held(path, owners)
        allowed = ALLOWED.get(name, {})
        bad = {owner: count for owner, count in linked.items()
               if not any(owner.endswith(f"({member})") for member in allowed)}
        loads = needed(path)
        sdk_loads = [library for library in loads if library.startswith("libneura-sdk")]
        size = path.stat().st_size
        if bad or sdk_loads:
            failed = True
            print(f"FAIL {name} ({size} bytes): " + "; ".join(
                ([f"holds SDK objects " + ", ".join(f"{owner} ({count} symbols)" for owner, count in sorted(bad.items()))]
                 if bad else []) + ([f"loads {', '.join(sdk_loads)}"] if sdk_loads else [])))
        else:
            notes = [f"{owner}: {reason}" for owner in sorted(linked)
                     for member, reason in allowed.items() if owner.endswith(f"({member})")]
            print(f"ok   {name} ({size} bytes): no SDK object; loads {', '.join(loads) or 'nothing'}" +
                  (f"; holds {'; '.join(notes)}" if notes else ""))
    core_owners, _ = owners_of(build, CORE_LIBRARIES)
    for path in sdk_shared:
        duplicated = held(path, core_owners)
        if duplicated:
            failed = True
            print(f"FAIL {path.name}: holds core objects " +
                  ", ".join(f"{owner} ({count} symbols)" for owner, count in sorted(duplicated.items())))
        else:
            print(f"ok   {path.name} ({path.stat().st_size} bytes): no core object; loads {', '.join(needed(path))}")
    if not present:
        print(f"ok   {build} defines no SDK library (configured with NEURA_SDK=OFF)")
    else:
        print(f"checked against {len(owners)} global definitions of {len(present)} SDK libraries")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
