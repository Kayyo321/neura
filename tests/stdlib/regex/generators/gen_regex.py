import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from corpus import CASES

def translate(pattern, flags):
    """Neura syntax to Python syntax: $ without multi_line is \\Z; (?<name>
    is (?P<name>; \\u{X} is \\UXXXXXXXX."""
    out = []
    i = 0
    in_class = False
    class_start = False
    while i < len(pattern):
        c = pattern[i]
        if c == '\\':
            if pattern[i+1] == 'u':
                j = pattern.index('}', i)
                value = int(pattern[i+3:j], 16)
                out.append('\\U%08x' % value)
                i = j + 1
                continue
            out.append(pattern[i:i+2])
            class_start = False
            i += 2
            continue
        if in_class:
            if c == ']' and not class_start:
                in_class = False
            class_start = False
            if c == '^' and out and out[-1] == '[':
                class_start = True
            out.append(c)
            i += 1
            continue
        if c == '[':
            in_class = True
            class_start = True
            out.append(c)
            i += 1
            continue
        if c == '$' and not (flags & 2):
            out.append(r'\Z')
            i += 1
            continue
        if pattern.startswith('(?<', i) and pattern[i+3] not in '=!':
            out.append('(?P<')
            i += 3
            continue
        out.append(c)
        i += 1
    return ''.join(out)

def pyflags(flags):
    f = re.ASCII
    if flags & 1: f |= re.IGNORECASE
    if flags & 2: f |= re.MULTILINE
    if flags & 4: f |= re.DOTALL
    return f

def byte_offset(s, i):
    return len(s[:i].encode('utf-8'))

def find_all(p, s):
    """Successive matches; after an empty match, resume one scalar later."""
    out = []
    pos = 0
    while pos <= len(s):
        m = p.search(s, pos)
        if not m:
            break
        out.append(m)
        if m.end() == m.start():
            pos = m.end() + 1
        else:
            pos = m.end()
    return out

def flatten(s, matches, groups):
    values = []
    for m in matches:
        for g in range(groups + 1):
            a, b = m.span(g)
            if a < 0:
                values += [-1, -1]
            else:
                values += [byte_offset(s, a), byte_offset(s, b)]
    return values

def neura_string(s):
    out = []
    for ch in s:
        if ch == '\\': out.append('\\\\')
        elif ch == '"': out.append('\\"')
        elif ch == '\n': out.append('\\n')
        elif ch == '\t': out.append('\\t')
        elif ch == '\r': out.append('\\r')
        elif ord(ch) < 32 or ord(ch) == 127: raise ValueError(repr(s))
        else: out.append(ch)
    return '"' + ''.join(out) + '"'

if __name__ == "__main__":
    lines = []
    for pattern, flags, inputs in CASES:
        p = re.compile(translate(pattern, flags), pyflags(flags))
        for s in inputs:
            ms = find_all(p, s)
            values = flatten(s, ms, p.groups)
            full = p.fullmatch(s) is not None
            lines.append('        check(%s, %d, %s, %s, [%s]);' % (
                neura_string(pattern), flags, neura_string(s), 'true' if full else 'false',
                ', '.join(str(v) for v in values)))
    print('\n'.join(lines))
