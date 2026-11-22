#!/usr/bin/env python3
"""Remove proof-only entries before comparing executable NeuraIR symbol tables."""

from collections import Counter
import pathlib
import re
import sys

QUOTED = r'"(?:[^"\\]|\\.)*"'
SYMBOL = re.compile(r'  %([0-9]+) (\w+) (' + QUOTED + r') ')
OWNER = re.compile(r' owner=%([0-9]+)(?: |$)')
LOCATION = re.compile(r' at ' + QUOTED + r':[0-9]+:[0-9]+$')
REFERENCE = re.compile(QUOTED + r'|([%#])([0-9]+)')
LEGACY_FUNCTION_ID = re.compile(r'^(?:"[$]as[$]([0-9]+)"|".+[$]([0-9]+)[$][0-9]+")$')


def read_symbols(path):
    return pathlib.Path(path).read_text(encoding="utf-8").split("\n")[2:]


def identity(line):
    entry = SYMBOL.match(line)
    location = LOCATION.search(line)
    return (*entry.group(2, 3), location[0] if location else "")


def executable_symbols(unoptimized, optimized):
    laws = {entry[1] for line in unoptimized if (entry := SYMBOL.match(line))[2] == "law"}
    erased = set()
    executable = []
    for line in unoptimized:
        entry = SYMBOL.match(line)
        owner = OWNER.search(re.sub(QUOTED, '""', line))
        if entry[1] in laws or (owner and owner[1] in laws):
            erased.add(identity(line))
        else:
            executable.append(line)

    for line in optimized:
        if identity(line) in erased:
            raise ValueError(f"optimized emission kept a proof-only symbol: {line.strip()}")

    # The remaining symbols keep their order. REFERENCE protects quoted text.
    ids = {SYMBOL.match(line)[1]: str(new_id) for new_id, line in enumerate(executable)}

    def remap(match):
        if not match[1]:
            return match[0]
        if match[2] not in ids:
            raise ValueError(f"executable symbol table refers to an erased symbol: {match[0]}")
        return match[1] + ids[match[2]]

    compacted = []
    for line in executable:
        line = REFERENCE.sub(remap, line)
        entry = SYMBOL.match(line)
        # These older generated function names encode a symbol ID.
        if entry[2] == "function" and (encoded := LEGACY_FUNCTION_ID.fullmatch(entry[3])):
            start, end = encoded.span(encoded.lastindex)
            name = entry[3]
            name = name[:start] + ids[name[start:end]] + name[end:]
            line = line[:entry.start(3)] + name + line[entry.end(3):]
        compacted.append(line)

    missing = Counter(map(identity, compacted)) - Counter(map(identity, optimized))
    if missing:
        kind, name, location = next(iter(missing))
        raise ValueError(f"optimized emission lost executable {kind} {name}{location}")

    return "\n".join(["neura.ir", "symbols {", *compacted])


def main():
    try:
        sys.stdout.write(executable_symbols(*map(read_symbols, sys.argv[1:])))
    except ValueError as error:
        sys.exit(str(error))


if __name__ == "__main__":
    main()
