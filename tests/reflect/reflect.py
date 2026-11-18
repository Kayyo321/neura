#!/usr/bin/env python3
"""Std.Reflect across builds and the NeuraIR boundary (docs/design/Reflect030.md,
edge cases 11 and 12, and the gate).

Each `programs/<name>.nra` with `<name>.expected` is built from source,
unoptimized, from a `.nir` module (optimized and not), and without a source
map, and run at the default tier and at tier 0: the output must be the
expected one. An image built from `.nir` must equal the image built from
source byte for byte, and two builds of one program must be identical
(descriptors are deterministic). A tagged record and class compiled to a
`.nir` module are reflected by a program that imports the module: the tags
and the round trip survive the boundary.

Usage: reflect.py <neura>
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

NEURA = pathlib.Path(sys.argv[1]).resolve()
PROGRAMS = pathlib.Path(__file__).resolve().parent / 'programs'

LIBRARY = '''
export let Accounts = module (
    export let Owner = record (name: string, age: int32);
    export let Account = class (
        export let owner: Owner = nil;
        #SerialName("total")
        export let balance: int64 = 0;
        #Sensitive
        export let pin: string = "0000";
        export let init = func { };
    );
);
'''

APP = '''
let console = import:Std.Io.Console;
let program = import:Std.Program;
let accounts = import:{source};
let bridge = import:Std.Reflect.Codec.JsonBridge;
let types = import:Std.Reflect.Type.TypeDescriptor;
let variants = import:Std.Reflect.Dynamic.Variant;

export let Main = module : program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {{
        let account = new accounts.Account();
        account.owner = new accounts.Owner("Ada", 36);
        account.balance = 12;
        let text = bridge.encode(variants.of(account));
        console.print_line(text);
        let back: accounts.Account = bridge.decode_as<accounts.Account>(text);
        console.print_line(back.owner.to_string() + " " + back.balance.to_string());
        let described = types.of<accounts.Account>();
        console.print_line(described.qualified_name + " " + described.field("pin").attributes[0].name);
        return program.ExitCode.Ok;
    }};
);
'''

APP_EXPECTED = ('{"owner":{"name":"Ada","age":36},"total":12,"pin":"<redacted>"}\n'
                'Accounts.Owner(name: Ada, age: 36) 12\n'
                'Accounts.Account Sensitive\n')


def run(*arguments, cwd=None):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=300, cwd=cwd,
                          env={'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin'})


class ReflectBoundaryTests(unittest.TestCase):
    def test_programs_in_every_build(self):
        programs = sorted(PROGRAMS.glob('*.expected'))
        self.assertGreaterEqual(len(programs), 3)
        with tempfile.TemporaryDirectory() as scratch:
            for expected in programs:
                source = expected.with_suffix('.nra')
                module = pathlib.Path(scratch) / (source.name + '.nir')
                result = run('emit-ir', str(source), '-o', str(module))
                self.assertEqual(result.returncode, 0, result.stderr)
                builds = {'source': [str(source)], 'unoptimized': ['--no-optimize', str(source)],
                          'nir': [str(module)], 'nir-unoptimized': ['--no-optimize', str(module)],
                          'stripped': ['--no-source-map', str(source)]}
                for build, arguments in builds.items():
                    image = pathlib.Path(scratch) / (source.name + '.' + build + '.nvr')
                    result = run('build', *arguments, '-o', str(image))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    if build in ('nir', 'nir-unoptimized'):
                        source_build = 'source' if build == 'nir' else 'unoptimized'
                        source_image = image.with_name(source.name + '.' + source_build + '.nvr')
                        self.assertEqual(source_image.read_bytes(), image.read_bytes(), source.name)
                    for tier in ([], ['--tier=0']):
                        with self.subTest(program=source.name, build=build, tier=tier):
                            result = run('run', *tier, str(image))
                            self.assertEqual(result.returncode, 0, result.stderr)
                            self.assertEqual(result.stdout, expected.read_text())

    def test_builds_are_deterministic(self):
        with tempfile.TemporaryDirectory() as scratch:
            for source in sorted(PROGRAMS.glob('*.nra')):
                images = []
                for attempt in range(2):
                    image = pathlib.Path(scratch) / f'{source.stem}.{attempt}.nvr'
                    result = run('build', str(source), '-o', str(image))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    images.append(image.read_bytes())
                self.assertEqual(images[0], images[1], source.name)

    def test_reflection_across_a_nir_module(self):
        with tempfile.TemporaryDirectory() as scratch:
            directory = pathlib.Path(scratch)
            (directory / 'accounts.nra').write_text(LIBRARY)
            result = run('emit-ir', 'accounts.nra', '-o', 'accounts.nir', cwd=directory)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name, source, inputs in (('source', 'Accounts', ['accounts.nra']),
                                         ('nir', '"accounts.nir".Accounts', [])):
                with self.subTest(module=name):
                    app = directory / ('app-' + name + '.nra')
                    app.write_text(APP.format(source=source))
                    image = directory / (app.stem + '.nvr')
                    result = run('build', app.name, *inputs, '-o', image.name, cwd=directory)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = run('run', image.name, cwd=directory)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, APP_EXPECTED)


if __name__ == '__main__':
    sys.argv = sys.argv[:1]
    unittest.main()
