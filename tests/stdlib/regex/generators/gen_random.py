# Random differential cases: patterns from a grammar that avoids the known
# divergences (no quantifier on an item that can match empty, no \B on an
# empty input), inputs over a small alphabet. Seeded, so the output is fixed.
import os, random, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_regex import translate, pyflags, find_all, flatten, neura_string

rng = random.Random(20261109)
ALPHABET = ['a', 'b', 'c', 'A', ' ', '1', '\n', 'é', '_']

def atom(depth):
    """Returns (text, nullable)."""
    r = rng.random()
    if r < 0.30:
        return rng.choice(['a', 'b', 'c', 'é', '1', ' ']), False
    if r < 0.40:
        return '.', False
    if r < 0.55:
        return rng.choice(['[ab]', '[^a]', '[a-c]', '[^\\s]', '[\\d_]', '\\w', '\\d', '\\s', '\\W', '[éa]']), False
    if r < 0.65:
        return rng.choice(['^', '$', '\\b', '\\B']), True
    if depth >= 3:
        return 'a', False
    inner, nullable = alternation(depth + 1)
    kind = rng.random()
    if kind < 0.6:
        return '(' + inner + ')', nullable
    return '(?:' + inner + ')', nullable

def item(depth):
    text, nullable = atom(depth)
    if nullable or rng.random() < 0.55:
        return text, nullable
    q = rng.choice(['*', '+', '?', '{2}', '{1,2}', '{0,2}', '{2,}', '*?', '+?', '??', '{1,3}?', '{2,}?'])
    can_be_empty = q[0] in '*?' or q.startswith('{0')
    return text + q, can_be_empty

def concat(depth):
    parts = []
    nullable = True
    for _ in range(rng.randint(1, 3)):
        text, n = item(depth)
        parts.append(text)
        nullable = nullable and n
    return ''.join(parts), nullable

def alternation(depth):
    branches = []
    nullable = False
    for _ in range(rng.choice([1, 1, 2, 3])):
        text, n = concat(depth)
        branches.append(text)
        nullable = nullable or n
    return '|'.join(branches), nullable

lines = []
seen = set()
while len(lines) < 1200:
    pattern, _ = alternation(0)
    flags = rng.choice([0, 0, 0, 1, 2, 4, 7])
    if (pattern, flags) in seen:
        continue
    seen.add((pattern, flags))
    p = re.compile(translate(pattern, flags), pyflags(flags))
    for _ in range(3):
        s = ''.join(rng.choice(ALPHABET) for _ in range(rng.randint(0, 8)))
        if s == '' and '\\B' in pattern:
            s = 'a'
        values = flatten(s, find_all(p, s), p.groups)
        full = p.fullmatch(s) is not None
        lines.append('        check(%s, %d, %s, %s, [%s]);' % (
            neura_string(pattern), flags, neura_string(s), 'true' if full else 'false',
            ', '.join(str(v) for v in values)))
print('\n'.join(lines))
