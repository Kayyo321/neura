#!/usr/bin/env python3
"""Require dot tags in standard library sources and examples."""

import argparse
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent


def convert(neura: Path, name: str, source: bytes) -> bytes | None:
    result = subprocess.run(
        [str(neura), "fmt", "--tag-style=dot", "--stdin-filepath", name],
        input=source, capture_output=True, cwd=ROOT, timeout=20,
    )
    if result.returncode != 0:
        sys.stderr.buffer.write(result.stderr)
        return None
    return result.stdout


def check_migration(neura: Path, base: str, tip: str) -> int:
    files = subprocess.check_output(
        ["git", "diff", "--name-only", "-z", base, tip, "--", "stdlib", "examples", "templates", "tests"],
        cwd=ROOT,
    )
    sources = [entry.decode("utf-8") for entry in files.split(b"\0") if entry.endswith(b".nra")]
    problems = 0
    for name in sources:
        source = subprocess.check_output(["git", "show", f"{base}:{name}"], cwd=ROOT)
        migrated = subprocess.check_output(["git", "show", f"{tip}:{name}"], cwd=ROOT)
        if (len(source) != len(migrated) or
            any(old != new and (old, new) != (ord('#'), ord('.')) for old, new in zip(source, migrated)) or
            convert(neura, name, source) != migrated):
            print(f"{name}: migration differs from a sigil-only dot conversion", file=sys.stderr)
            problems += 1
    print(f"Checked {len(sources)} migrated Neura sources; {problems} problems.")
    return int(problems != 0)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--neura", required=True, type=Path)
    parser.add_argument("--migration", nargs=2, metavar=("BASE", "TIP"),
                        help="verify the source bytes of a migration between two Git revisions")
    options = parser.parse_args()
    neura = options.neura.resolve()
    if options.migration:
        return check_migration(neura, *options.migration)

    paths = sorted(path for directory in ("stdlib", "examples") for path in (ROOT / directory).rglob("*.nra"))
    problems = 0
    for path in paths:
        source = path.read_bytes()
        if b"#" not in source:
            continue
        name = path.relative_to(ROOT).as_posix()
        converted = convert(neura, name, source)
        if converted is None:
            print(f"{name}: could not check tag style", file=sys.stderr)
            problems += 1
        elif converted != source:
            print(f"{name}: use dot tags", file=sys.stderr)
            problems += 1

    print(f"Checked tag style in {len(paths)} Neura sources; {problems} problems.")
    return int(problems != 0)


if __name__ == "__main__":
    sys.exit(main())
