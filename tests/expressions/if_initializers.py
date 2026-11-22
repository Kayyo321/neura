#!/usr/bin/env python3
"""Check initializer evaluation, lexical scope, and rejected if headers."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

NEURA = Path(sys.argv[1]).resolve()
FIXTURES = Path(__file__).resolve().with_name("if-initializers")

with tempfile.TemporaryDirectory(prefix="if-initializers-", dir=Path.cwd()) as directory:
    work = Path(directory)

    def neura(*args, success=True):
        result = subprocess.run([NEURA, *args], cwd=work, capture_output=True, timeout=120)
        assert (result.returncode == 0) == success, (args, result.stderr)
        return result

    neura("check", "--error-format=short", FIXTURES / "narrowing.nra")
    neura("check", "--error-format=short", FIXTURES / "external-template.nra")
    neura("build", "--error-format=short", FIXTURES / "branches.nra", "-o", "branches.nvr")
    result = neura("run", "--runtime=off", "--tier=0", "branches.nvr")
    assert result.stdout.splitlines() == [b"1", b"outer", b"2", b"42", b"2"], result.stdout

    # Brackets around an expected diagnostic span are markers for this table.
    cases = (
        ("if (let x = true[] x) {}", "E0201", 0),
        ("if ([const] x = true; x) {}", "E0204", 5),
        ("if (let x = true; [let] y = false; x) {}", "E0204", 3),
        ("while ([let] x = true; x) {}", "E0204", 3),
        ("if (let x = true; [0]) {}", "E0403", 1),
        ("if (let x: bool = [0]; x) {}", "E0403", 1),
        ("if (let x = [x]; x) {}", "E0304", 1),
        ("if (let x = true; x) {} let _ = [x];", "E0304", 1),
        ("if (true) { let _ = [x]; } else if (let x = true; x) {}", "E0304", 1),
        ("if (false) {} else if (let x = true; x) {} let _ = [x];", "E0304", 1),
    )
    for marked, code, length in cases:
        source = "    " + marked
        column = source.index("[") + 1
        (work / "rejected.nra").write_text(
            "let check_header = func() {\n" + source.replace("[", "", 1).replace("]", "", 1) + "\n};\n",
            encoding="utf-8",
        )
        short = neura("check", "--error-format=short", "rejected.nra", success=False)
        assert f"rejected.nra:2:{column}: error[{code}]:".encode() in short.stderr.splitlines()[0], (
            marked, short.stderr,
        )
        detail = neura("check", "--error-format=json", "rejected.nra", success=False)
        diagnostic = json.loads(detail.stderr.splitlines()[0])
        primary = next(span for span in diagnostic["spans"] if span["primary"])
        assert (primary["line"], primary["column"], primary["length"]) == (2, column, length), (
            marked, primary,
        )
