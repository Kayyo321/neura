#!/usr/bin/env python3
"""Check that nested loop fixpoints finish and preserve guarded reads."""

import json
import pathlib
import subprocess
import sys
import tempfile


lines = [
    "let Entry = class ( export last ready = true; );",
    "let probe = func(n: int32) -> bool {",
    "    let entry: Entry = new;",
    "    let ready = false;",
]
depth = 24
for level in range(depth):
    indent = "    " * (level + 1)
    lines += [f"{indent}let i{level} = 0;", f"{indent}while (i{level} < n) {{"]
indent = "    " * (depth + 1)
lines += [f"{indent}if (entry != nil) {{",
          f"{indent}    ready = entry.ready;",
          f"{indent}}}",
          f"{indent}entry = nil;"]
for level in reversed(range(depth)):
    indent = "    " * (level + 1)
    lines += [f"{indent}    i{level} += 1;", f"{indent}}}"]
unguarded = len(lines) + 1
lines += ["    return ready || entry.ready;", "};"]

compiler = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="neura-nil-loop-") as temporary:
    fixture = pathlib.Path(temporary) / "nested-while.nra"
    fixture.write_text("\n".join(lines) + "\n", encoding="utf-8")
    try:
        check = subprocess.run([compiler, "check", "--error-format=json", fixture],
                               capture_output=True, encoding="utf-8", timeout=12)
    except subprocess.TimeoutExpired as error:
        raise SystemExit(f"checking {depth} nested loops exceeded {error.timeout} seconds") from error

assert check.returncode == 0, check.stderr
diagnostics = [json.loads(line) for line in check.stderr.splitlines() if line.startswith("{")]
assert [item["code"] for item in diagnostics] == ["W0006"], diagnostics
primary = next(span for span in diagnostics[0]["spans"] if span["primary"])
assert primary["line"] == unguarded, diagnostics
print(f"{depth} nested loops checked with one unguarded read warning")
