"""Writes generated known-answer vectors as a Neura support module.

Each vector file is a module with one exported flat `string[]` per table. A
table has a fixed number of columns per row; the generator names them in the
row comment. Byte strings are lowercase hexadecimal. The tests in
tests/stdlib/crypto-*/ decode them with Std.Encoding.Hex and compare every
output byte.
"""

from __future__ import annotations

import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[2]


def pattern(length: int, multiplier: int = 31, offset: int = 7) -> bytes:
    """The test message pattern: byte i is (i * multiplier + offset) mod 256."""
    return bytes((i * multiplier + offset) % 256 for i in range(length))


def quote(text: str) -> str:
    if any(c in text for c in '"\\{}\n'):
        raise ValueError(f"cell needs escaping: {text!r}")
    return '"' + text + '"'


def write_module(area: str, file_name: str, module: str, header: list[str],
                 tables: list[tuple[str, list[str], list[list[str]]]]) -> pathlib.Path:
    """Writes tests/stdlib/<area>/support/<file_name>.

    `tables` holds (name, column names, rows); every cell is a string.
    """
    lines = ["// Generated; do not edit. Regenerate with the script named below.", ]
    lines += ["// " + h for h in header]
    lines.append(f"export let {module} = module (")
    for index, (name, columns, rows) in enumerate(tables):
        if index:
            lines.append("")
        lines.append(f"    /// @brief Rows of {len(columns)} cells: {', '.join(columns)}.")
        lines.append(f"    export let {name}: string[] = [")
        cells = []
        for row in rows:
            if len(row) != len(columns):
                raise ValueError(f"{name}: row has {len(row)} cells, expected {len(columns)}")
            cells.append("        " + ", ".join(quote(c) for c in row))
        lines.append(",\n".join(cells))
        lines.append("    ];")
    lines.append(");")
    path = ROOT / "tests" / "stdlib" / area / "support" / file_name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path
