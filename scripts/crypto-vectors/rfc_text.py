"""Reads labelled hexadecimal values out of an RFC's plain text.

The RFC files are the plain-text renderings from rfc-editor.org, downloaded to
/home/dev/neura-gate/030/crypto2/sources/asymmetric/ (see SOURCES.txt there).
Page footers and headers are dropped before parsing, so a value that runs over
a page break is joined.

The downloads (re-fetch them into SOURCES to rerun the gen-asymmetric-*.py
generators; SOURCES.txt there records their SHA-256):
- https://www.rfc-editor.org/rfc/rfc{6979,7748,8017,8032,9180}.txt
- https://raw.githubusercontent.com/C2SP/wycheproof/65ca494727a52944c204441c860b039e6786b99b/testvectors_v1/<name>.json
  (Wycheproof commit 65ca494727a52944c204441c860b039e6786b99b) into SOURCES/wycheproof/
- https://raw.githubusercontent.com/cfrg/draft-irtf-cfrg-hpke/b1f7cb0cdeab6906c61b3d6574e8bdfdbe1cd3fb/test-vectors.json
  as SOURCES/hpke-test-vectors.json
"""

from __future__ import annotations

import re

SOURCES = "/home/dev/neura-gate/030/crypto2/sources/asymmetric"


def lines(path: str) -> list[str]:
    """The text's lines without form feeds, page footers and running headers."""
    out = []
    for line in open(path, encoding="utf-8").read().replace("\f", "\n").splitlines():
        if re.search(r"\[Page [0-9]+\]\s*$", line):
            continue
        if re.match(r"^RFC [0-9]{4} {2,}", line):
            continue
        out.append(line)
    return out


def section(all_lines: list[str], start: str, end: str) -> list[str]:
    """The lines from the first line that starts with `start` up to the next that starts with `end`."""
    first = next(i for i, line in enumerate(all_lines) if line.startswith(start))
    last = next(i for i in range(first + 1, len(all_lines)) if all_lines[i].startswith(end))
    return all_lines[first:last]


def hex_after(block: list[str], label: str, index: int = 0) -> str:
    """The hexadecimal digits on the lines after the `index`-th line equal to `label` (stripped), up to the next
    line that is neither hexadecimal nor blank (blank lines are left where page breaks were)."""
    hits = [i for i, line in enumerate(block) if line.strip() == label]
    i = hits[index] + 1
    while i < len(block) and block[i].strip() == "":
        i += 1
    digits = ""
    while i < len(block):
        line = block[i].strip()
        if re.fullmatch(r"[0-9a-f]+", line):
            digits += line
        elif line != "":
            break
        i += 1
    return digits
