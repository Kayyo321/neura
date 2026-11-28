"""Check the real exports and CRT dependencies of an embedded Windows DLL."""

from pathlib import Path
import runpy
import sys
import tempfile


def main(image, expected):
    check = runpy.run_path(str(Path(__file__).resolve().parents[2] / "scripts/check-windows-binaries.py"))
    for line in image.read_text(encoding="utf-8").splitlines():
        if line.startswith('native-library "x86_64-windows-msvc" '):
            payload = bytes.fromhex(line.split(maxsplit=2)[2])
            break
    else:
        raise SystemExit(f"{image}: missing Win64 native library")
    with tempfile.TemporaryDirectory() as scratch:
        dll = Path(scratch) / "embedded.dll"
        dll.write_bytes(payload)
        exports = check["exports"](check["dumpbin"]("/exports", dll))
        imports = check["imports"](check["dumpbin"]("/dependents", dll))
    if exports != expected:
        raise SystemExit(f"{image}: expected PE exports {sorted(expected)}, got {sorted(exports)}")
    unexpected = imports - check["SYSTEM_DLLS"]
    if unexpected:
        raise SystemExit(f"{image}: expected only system DLL imports, got {sorted(unexpected)}")


if __name__ == "__main__":
    main(Path(sys.argv[1]), set(sys.argv[2:]))
