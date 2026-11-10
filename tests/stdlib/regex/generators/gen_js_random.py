# Seeded random patterns in which quantified items may match the empty
# string, checked against ECMAScript (node), whose empty-iteration rule the
# library follows. Uses only syntax whose meaning agrees between Neura and
# JavaScript for the input alphabet.
import os, json, random, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_regex import neura_string

rng = random.Random(8042026)
ALPHABET = ['a', 'b', 'c', 'A', ' ', '1', '\n', 'é', '_']

def atom(depth):
    r = rng.random()
    if r < 0.30:
        return rng.choice(['a', 'b', 'c', 'é', '1', ' '])
    if r < 0.38:
        return '.'
    if r < 0.50:
        return rng.choice(['[ab]', '[^a]', '[a-c]', '\\w', '\\d', '\\s', '\\W', '[éa]'])
    if r < 0.58:
        return rng.choice(['^', '$', '\\b', '\\B'])
    if depth >= 3:
        return 'a'
    inner = alternation(depth + 1)
    # Only outermost groups capture: ECMAScript resets the captures inside a
    # quantified group at each iteration, where the library (like Perl and
    # Python) keeps the last value.
    if depth == 0 and rng.random() < 0.6:
        return '(' + inner + ')'
    return '(?:' + inner + ')'

def item(depth):
    text = atom(depth)
    if text in ('^', '$', '\\b', '\\B') or rng.random() < 0.5:
        return text
    return text + rng.choice(['*', '+', '?', '{2}', '{1,2}', '{0,2}', '{2,}', '*?', '+?', '??', '{0,3}?', '{1,}?'])

def concat(depth):
    return ''.join(item(depth) for _ in range(rng.randint(1, 3)))

def alternation(depth):
    return '|'.join(concat(depth) for _ in range(rng.choice([1, 1, 2, 3])))

def nullable_quantified_group(depth):
    # Encourage the interesting shape: a quantified group whose body can match empty.
    body = rng.choice(['a*', 'a?', 'a|', '|b', 'a*|b', '(?:a)*', 'b?a?', '\\b', 'a*?', '(?:a|b*)'])
    group = rng.choice(['(', '(?:'])
    q = rng.choice(['*', '+', '?', '{2}', '{1,3}', '{0,2}', '{2,}', '*?', '+?', '{1,2}?'])
    return group + body + ')' + q

cases = []
seen = set()
while len(cases) < 900:
    if rng.random() < 0.5:
        pattern = concat(0)
        if rng.random() < 0.5:
            pattern = nullable_quantified_group(0) + pattern
        else:
            pattern = pattern + nullable_quantified_group(0)
    else:
        pattern = alternation(0)
    flags = rng.choice([0, 0, 0, 1, 2, 4, 7])
    if (pattern, flags) in seen:
        continue
    seen.add((pattern, flags))
    for _ in range(3):
        s = ''.join(rng.choice(ALPHABET) for _ in range(rng.randint(0, 8)))
        cases.append([pattern, flags, s])

import tempfile
with tempfile.NamedTemporaryFile('w', suffix='.json', delete=False) as f:
    json.dump(cases, f)
node = os.environ.get('NODE', '/usr/bin/nodejs')
here = os.path.dirname(os.path.abspath(__file__))
results = json.loads(subprocess.run([node, os.path.join(here, 'js_ref.js'), f.name],
                                    capture_output=True, text=True, check=True).stdout)
os.unlink(f.name)
lines = []
for (pattern, flags, s), (values, full) in zip(cases, results):
    lines.append('        check(%s, %d, %s, %s, [%s]);' % (
        neura_string(pattern), flags, neura_string(s), 'true' if full else 'false',
        ', '.join(str(v) for v in values)))
print('\n'.join(lines))
