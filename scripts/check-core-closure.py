#!/usr/bin/env python3
"""Check that the execution core links no compiler code.

    scripts/check-core-closure.py BUILD_DIR

The core component (docs/design/Products030.md) holds `neura`, `neura-run`,
`neura-inspect`, `neura-doctor`, `neura-update` and `neura-uninstall`. A
static library contributes an object file to an executable only to define a
global symbol the executable needs, so an object of a compiler library is in
an executable exactly when one of its global definitions is. This script
lists the global definitions (not weak ones, which header code shares) of
every object of the SDK libraries of BUILD_DIR, and fails when a core tool
defines any of them.

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


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    build = Path(sys.argv[1])
    owners: dict[str, str] = {}
    present = [library for library in SDK_LIBRARIES if (build / library).exists()]
    for library in present:
        for member, _, name in symbols(build / library):
            owners.setdefault(name, f"{Path(library).name}({member})")
    failed = False
    for tool in CORE_TOOLS:
        path = build / tool
        if not path.exists():
            print(f"FAIL {tool}: not built in {build}")
            failed = True
            continue
        linked: dict[str, int] = {}
        for _, _, name in symbols(path):
            if name in owners:
                linked[owners[name]] = linked.get(owners[name], 0) + 1
        allowed = ALLOWED.get(tool, {})
        bad = {owner: count for owner, count in linked.items()
               if not any(owner.endswith(f"({member})") for member in allowed)}
        size = path.stat().st_size
        if bad:
            failed = True
            print(f"FAIL {tool} ({size} bytes): holds SDK objects " +
                  ", ".join(f"{owner} ({count} symbols)" for owner, count in sorted(bad.items())))
        else:
            notes = [f"{owner}: {reason}" for owner in sorted(linked)
                     for member, reason in allowed.items() if owner.endswith(f"({member})")]
            print(f"ok   {tool} ({size} bytes): no SDK object" + (f"; holds {'; '.join(notes)}" if notes else ""))
    if not present:
        print(f"ok   {build} defines no SDK library (configured with NEURA_SDK=OFF)")
    else:
        print(f"checked against {len(owners)} global definitions of {len(present)} SDK libraries")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
