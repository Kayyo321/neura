#!/usr/bin/env python3
"""Execute source-derived hook events, including old state and typed nil across calls."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

NEURA = Path(sys.argv[1]).resolve()
FIXTURES = Path(__file__).resolve().parent
ENTRY = '''
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        BODY
        return program.ExitCode.Ok;
    };
);
'''
TRACE = '\n'.join([
    's',          # global = nil during startup
    'sgsgss',     # new, then getter/argument/parameter read/result/local
    'sgsSgsS',    # first store versus replacement in fields and globals
    'ssS',        # RHS stores a live object before the outer setter reads the previous value
    'gsgsgsSg',   # list construction, append, replacing an element, method receiver
    'ggsggsg',    # tuple projection, enum pattern and its binding
    'sgswSwrgs',  # accessor and hook order, including the previous backing field
    'gsggsgsg',   # owned capture/call and variadic argument evaluation
    'GGgg',       # derived setter chosen for a live object, then for the destination's nil
]) + '\n'

# The trace file uses getters and setters with side effects. Other programs
# make failure ordering and the dynamic policy observable without debugging.
CASES = {
    # s initialization (10), two source-read/admissions (11 each), an
    # array read (1), destructuring (11), then a receiver read (1).
    'interface-array-destructuring': (
        'let Named = interface (let number = func -> int;); let count: int = 0; '
        'let S = class implements Named (| get: { count++; return instance; }; '
        '| set: { count += 10; return value; }; export let number = func -> int => 5;);',
        'let s: S = new; let v: Named = s; let batch: Named[] = [v]; '
        'let pair: [Named, int] = [batch[0], 1]; let [copy, n] = pair; '
        'if (copy.number() != 5 || count != 45) { return program.ExitCode.Failure; }', 0, ''),
    'destroyed-owner': (
        'let failure = import:Std.Core.Failure; let S = class('
        '| get: { failure.trap("unexpected getter"); return instance; }; | set: value;); '
        'let Owner = class (export let field: S = nil;);',
        'let owner: Owner = new; let alias = owner; destroy owner; let found = alias.field;',
        139, 'invalid reference handle'),
    'callable': (
        'let count: int = 0; let S = class(| get: { count++; return instance; }; '
        '| set: { count += 10; return value; };);',
        'let echo = new func(x: S) -> S => x; let s: S = nil; let found: S = echo(s); '
        'if (count != 42) { return program.ExitCode.Failure; }', 0, ''),
    'constructor-field': (
        'let count: int = 0; let S = class(| get: { count++; return instance; }; '
        '| set: { count += 10; return value; };); '
        'let Box = class(export last field: S; export let init = func(field: S) => set;);',
        'let s: S = nil; let b: Box = new(s); '
        'if (count != 32) { return program.ExitCode.Failure; }', 0, ''),
    'map-nil': ('let S = class(| get: { if (instance == nil) { return shared; } return instance; }; '
                'export let id: int = 7;); let shared: S = new;',
                'let a: S = nil; if (a == nil || a.id != 7) { return program.ExitCode.Failure; }', 0, ''),
    'nil-field': ('let S = class(| get: instance; export let id: int = 3;);',
                  'let a: S = nil; let value = a.id;', 139, 'nil'),
    'argument-order': ('let failure = import:Std.Core.Failure; let S = class(| set: { failure.trap("first"); '
                       'return value; };); let take = func(a: S, b: int) {}; '
                       'let second = func -> int { failure.trap("second"); return 1; };',
                       'take(new S(), second());', 134, 'first'),
    'at-limit': ('let depth: int = 0; let S = class(| get: { if (depth < 63) { depth++; '
                 'let next: S = new; next; } return instance; };);',
                 'let first: S = new; first;', 0, ''),
    'over-limit': ('let depth: int = 0; let S = class(| get: { if (depth < 64) { depth++; '
                   'let next: S = new; next; } return instance; };);',
                   'let first: S = new; first;', 139, 'class hook nesting limit exceeded'),
    'typed-cast': ('let Base = class(| get: instance;); let A = class:Base(); let B = class:Base();',
                   'let a: A = nil; let b: Base = a; let x = b as B;', 134, 'invalid class cast'),
    'wrong-result': ('let Base = class(| get: make();); let Guest = class:Base(); let make = func -> Base => new;',
                     'let a = new Guest(); a;', 134, 'class hook result is not a Guest'),
    'recursive-get': ('let Session = class(| get: shared;); let shared: Session = nil;',
                      'shared;', 134, 'class hook recursion: get of Session'),
    'recursive-set': ('let Session = class(| set: { shared = value; return value; };); let shared: Session = nil;',
                      '', 134, 'class hook recursion: set of Session'),
    'deferred': ('let Session = class(| set: value;);', 'last a: Session; a = new;', 0, ''),
    'startup': ('let Session = class(| get: instance; | set: value;); let reader = func -> Session => late; '
                'let early: Session = reader(); let late: Session = nil;',
                '', 139, 'uninitialized global'),
    'generic': ('let Box = class<T>(| get: instance; | set: value; export last x: T; '
                'export let init = func(x: T) => set;);',
                'let i: Box<int> = new(1); let s: Box<string> = new("x"); let x = i.x; let text = s.x;', 0, ''),
}

MODES = [
    (['--tier=0'], {}), (['--tier=1'], {}), (['--tier=2'], {}),
    (['--jit=off'], {}), (['--jit=on'], {}),
    ([], {'NEURA_TIER3': 'interp', 'NEURA_TIER_EAGER': '1'}),
    (['--jit=on'], {'NEURA_TIER_CHECK': '2', 'NEURA_TIER_EAGER': '1'}),
    ([], {'NEURA_TIER_PROGRAMS': '1', 'NEURA_TIER_EAGER': '1'}),
]


def run(args, extra=None, status=0, message=''):
    result = subprocess.run([NEURA, *args], capture_output=True, text=True,
                            env={**os.environ, 'NEURA_GPU': 'off', 'NEURA_TIER3_VERIFY': '1', **(extra or {})}, timeout=120)
    if result.returncode != status or message not in result.stderr:
        raise AssertionError(f'{args} {extra}: expected {status} {message!r}, got '
                             f'{result.returncode}\n{result.stdout}\n{result.stderr}')
    return result


def execute(work, source, expected, status=0, trap=''):
    name = source.stem
    nir = work / f'{name}.nir'
    run(['emit-ir', source, '-o', nir])
    for optimize in (True, False):
        for metadata in (True, False):
            flags = ([] if optimize else ['--no-optimize']) + ([] if metadata else ['--no-source-map', '--no-lift-hints'])
            image = work / f'{name}.nvr'
            linked = work / f'{name}-linked.nvr'
            run(['build', *flags, source, '-o', image])
            run(['build', *flags, nir, '-o', linked])
            assert image.read_bytes() == linked.read_bytes(), f'{name}: from NIR differs with {flags}'
            assembly = work / f'{name}.nvra'
            run(['disassemble', image, '-o', assembly])
            run(['assemble', assembly, '-o', linked])
            assert image.read_bytes() == linked.read_bytes(), f'{name}: assembly differs with {flags}'
            oracle = None
            for options, env in MODES:
                result = run(['run', '--stats', *options, linked], env, status, trap)
                assert result.stdout == expected, (name, flags, options, result.stdout, expected)
                counters = re.search(r'stats: steps (\d+), peak heap cells (\d+)(?:, peak stack cells (\d+))?, host requests (\d+)',
                                     result.stderr)
                assert counters, result.stderr
                if oracle is None:
                    oracle = counters.groups()
                assert counters.groups() == oracle, (name, options, counters.groups(), oracle)
    print(name, 'passed')


def linked_provider(work):
    # The provider writes its IR before its importer declares a hooked
    # subclass. Its prebuilt parameter/result still produce linked events.
    library = work / 'library.nra'
    library.write_text('let Library = module (export let Base = class(); '
                       'export let echo = func(x: Base) -> Base => x;);')
    nir = work / 'library.nir'
    run(['emit-ir', library, '-o', nir])
    original = nir.read_bytes()
    library.unlink()
    source = work / 'importer.nra'
    source.write_text('let program = import:Std.Program; let lib = import:"library.nir"; '
                      'let log: int = 0; let Child = class:lib.Base('
                      '| get: { log = log * 2 + 1; return instance; }; '
                      '| set: { log = log * 2; return value; };);' +
                      ENTRY.replace('BODY', 'let item: Child = new; let copy = lib.echo(item); '
                                            'if (log != 20) { return program.ExitCode.Failure; }'))
    execute(work, source, '')
    assert nir.read_bytes() == original, 'the provider was rebuilt'


def malformed(work):
    base = (work / 'references.nvra').read_text()
    pattern = r'(exit state (OP) immediate \d+ arguments \[ )([^\]]+)(\] result)'
    for op, arity, expected in [('write-field', 3, 'field write exceeds layout'),
                                ('write-global', 2, 'global write exceeds table'),
                                ('write-component', 4, 'component write exceeds element'),
                                ('read-global', 1, 'argument')]:
        matches = list(re.finditer(pattern.replace('OP', op), base))
        match = next(m for m in matches if len(m[3].split()) == arity)
        # The same valid source operand repeated past the available destination
        # words must be rejected by the loader before anything can run.
        extra = (match[3].split()[-1] + ' ') * 64
        text = base[:match.end(3)] + extra + base[match.end(3):]
        assembly = work / 'invalid.nvra'
        assembly.write_text(text)
        run(['assemble', assembly, '-o', work / 'invalid.nvr'], status=1, message=expected)


def main():
    with tempfile.TemporaryDirectory(prefix='neura-hook-references-') as directory:
        work = Path(directory)
        for name, expected in [('references', TRACE), ('tasks', '')]:
            execute(work, FIXTURES / f'{name}.nra', expected)
        for name, (declarations, body, status, trap) in CASES.items():
            path = work / f'{name}.nra'
            path.write_text('let program = import:Std.Program;\n' + declarations + '\n' + ENTRY.replace('BODY', body))
            execute(work, path, '', status, trap)
        linked_provider(work)
        malformed(work)
        # The layout limit is applied before generating any image.
        for width in (32, 33):
            source = work / 'width.nra'
            annotation = ', '.join(['Session'] * width)
            source.write_text('let program = import:Std.Program; let Session = class(| get: instance;); '
                              f'let use = func(value: [{annotation}]) {{}};\n' + ENTRY.replace('BODY', ''))
            args = ['build', source, '-o', work / 'width.nvr']
            if width == 32:
                run(args)
            else:
                result = run(args, status=1, message='E0449')
                assert '66 value words' in result.stderr and 'policy word' in result.stderr, result.stderr


if __name__ == '__main__':
    main()
