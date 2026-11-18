#!/usr/bin/env python3
"""The checks that lowering makes are repeated when a program is built from a
`.nir` module (docs/design/Lifetime030.md, Diagnostics): a `using` resource
that escapes its block (E0475) and an event emitted under a monitor guard
(E0550).

A compiler never writes such a module, because lowering rejects the source.
So each case emits a valid program, then retargets one call in its `.nir`
to another function of the same program: inside the `using` block, from a
function that does not keep its argument to one that appends it to a list;
in a `#Synchronized` method, from a function that does not emit to one that
does. The call's target is a little-endian 32-bit symbol id. Its offset is
found by emitting the same program with the inside call naming a third
function of the same shape, and the id of the target by emitting it with
the outside call doing so; the names have the same length, so nothing else
moves, and every variant is written to the same path. The unpatched module
builds; the patched one is rejected at build time with the code, and so is
`emit-ir` of it.

Usage: nir_checks.py <neura>
"""
import pathlib
import subprocess
import sys
import tempfile

NEURA = pathlib.Path(sys.argv[1]).resolve()

USING = '''let closeable = import:Std.Io.Closeable;
let.promote _ = import:Std.Core.Result;
let.promote _ = import:Std.Core.Unit;
let Resource = class implements closeable.Closeable (
    export let close = func -> Result<Unit> => Result<Unit>.Ok(Unit.Value);
);
let shelf: Resource[list] = new;
let keep_a = func(target: Resource) -> void {
    shelf.append(target);
};
let keep_b = func(target: Resource) -> void {};
let keep_c = func(target: Resource) -> void {};
let work = func() -> Result<int> {
    using (source = new Resource()) {
        INSIDE(source);
    }
    OUTSIDE(new Resource());
    return Result<int>.Ok(0);
};
let touch = func() {
    keep_a(new Resource());
    keep_b(new Resource());
    keep_c(new Resource());
};
let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let outcome = work();
        touch();
        return program.ExitCode.Ok;
    };
);
'''

GUARD = '''let events = import:Std.Events.Event;
let note_a = func(event: events.Event<int>, value: int) {
    event.emit(value);
};
let note_b = func(event: events.Event<int>, value: int) {};
let note_c = func(event: events.Event<int>, value: int) {};
#Synchronized
let Account = class (
    let balance: int = 0;
    let changed = new events.Event<int>();
    export let deposit = func(amount: int) {
        balance = balance + amount;
        INSIDE(changed, balance);
    };
);
let report = func(changed: events.Event<int>) {
    OUTSIDE(changed, 1);
};
let touch = func(changed: events.Event<int>) {
    note_a(changed, 1);
    note_b(changed, 1);
    note_c(changed, 1);
};
let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let changed = new events.Event<int>();
        new Account().deposit(1);
        report(changed);
        touch(changed);
        return program.ExitCode.Ok;
    };
);
'''

# (name, template, the code, the function that is safe inside, a second safe
# function, the function that the patched call reaches)
CASES = [
    ('using-escape', USING, 'E0475', 'keep_b', 'keep_c', 'keep_a'),
    ('event-guard', GUARD, 'E0550', 'note_b', 'note_c', 'note_a'),
]


def run(arguments):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True)


def emit(scratch, name, source):
    # Every variant is written to the same path: the module records it.
    path = scratch / (name + '.nra')
    path.write_text(source)
    module = scratch / (name + '.nir')
    result = run(['emit-ir', str(path), '-o', str(module)])
    if result.returncode != 0:
        raise AssertionError(f'emit-ir failed\n{result.stderr}')
    return module.read_bytes()


def differing(first, second):
    if len(first) != len(second):
        raise AssertionError('the variants differ in length, so the call cannot be located')
    return [i for i, (a, b) in enumerate(zip(first, second)) if a != b]


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix='neura-lifetime-nir-') as directory:
        scratch = pathlib.Path(directory)
        for name, template, code, safe, other, reaching in CASES:
            def variant(inside, outside):
                return template.replace('INSIDE', inside).replace('OUTSIDE', outside)
            try:
                inside_variant = emit(scratch, name, variant(other, reaching))
                outside_variant = emit(scratch, name, variant(safe, other))
                base = emit(scratch, name, variant(safe, reaching))
                inside = differing(base, inside_variant)
                outside = differing(base, outside_variant)
                if not inside or not outside or inside[-1] - inside[0] > 3 or outside[-1] - outside[0] > 3:
                    raise AssertionError(f'expected one changed symbol id per call, found bytes {inside} and '
                                         f'{outside}')
                patched = bytearray(base)
                patched[inside[0]:inside[0] + 4] = base[outside[0]:outside[0] + 4]
            except AssertionError as error:
                failures.append(f'{name}: {error}')
                continue
            module = scratch / (name + '.nir')
            result = run(['build', str(module), '-o', str(scratch / (name + '.nvr'))])
            if result.returncode != 0:
                failures.append(f'{name}: the unpatched module did not build\n{result.stderr}')
            broken = scratch / (name + '-patched.nir')
            broken.write_bytes(bytes(patched))
            for command in (['build', str(broken), '-o', str(scratch / (name + '-patched.nvr'))],
                            ['emit-ir', str(broken)]):
                result = run(command)
                if result.returncode == 0 or f'error[{code}]' not in result.stderr:
                    failures.append(f'{name}: {command[0]} of the patched module: status {result.returncode}, '
                                    f'expected {code}\n{result.stderr}')
                elif 'internal' in result.stderr.lower():
                    failures.append(f'{name}: {command[0]} reported an internal error\n{result.stderr}')
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f'{len(CASES)} cases, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
