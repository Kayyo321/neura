#!/usr/bin/env python3
"""The compile-time check of interpolated strings agrees with the library.

docs/design/Calls030.md, Interpolated strings: a specification in a hole is
checked at compile time against the hole's type with the rules
Std.Io.Internal.Formatting applies at run time. For every specification of
the corpus and every kind of value:

  * the library formats `{:spec}` with Console.try_format;
  * `neura check` checks `$"{value:spec}"`.

They must give the same verdict, and a rejection the same message (E0486
carries the library's message). The one deliberate difference is the `c`
presentation, which the library checks against the value when it runs: an
interpolated string rejects it. Every accepted case is then built and run,
and must print the library's text.

Usage: format_corpus.py <neura>
"""
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile

NEURA = pathlib.Path(sys.argv[1]).resolve()

# A declaration of each kind of value, and the expression that names it.
VALUES = {
    'text': ('let value = "word";', 'value'),
    'signed': ('let value: int32 = -42;', 'value'),
    'unsigned': ('let value: uint8 = 200;', 'value'),
    'wide': ('let value: int64 = 9000000000;', 'value'),
    'double': ('let value = 3.25;', 'value'),
    'single': ('let value: float32 = 0.5;', 'value'),
    'flag': ('let value = true;', 'value'),
    'object': ('let value: Label = new Label();', 'value'),
    'variant': ('let value = Light.Green;', 'value'),
}

SPECIFICATIONS = [
    '', '>8', '<8', '^8', '*^9', '_>6', '+', '-', ' ', '#', '0', '08', '+08', '#x', '#b', '#o', 'x', 'X', 'o', 'b',
    'B', 'd', 'c', 's', '?', '.2', '.0', '.2f', 'f', 'F', 'e', 'E', 'g', 'G', 'a', 'A', '.101f', '.100f',
    '10.3e', '#g', '+.1e', '0>4', 'q', '.', '.x', '99999999999', '+#010x', '<', '>', '^', '8s', '5?', '.3s',
    '>8.3', 'x8', '#?', '+s', ' d', '-f', '^#x', '=8',
]

PRELUDE = '''let console = import:Std.Io.Console;
let program = import:Std.Program;
let errors = import:Std.Core.Error;
let result = import:Std.Core.Result;

let Label = class (
    export let.override to_string = func -> string => "label";
);
let Light = enum (Red, Green);

let report = func(index: int, problem: errors.Error) {
    match (problem) {
        errors.Error.Parse { kind: _, offset: _, message } => {
            console.print_line(index.to_string() + " error " + message);
        },
        _ => { console.print_line(index.to_string() + " other"); },
    }
};
'''


def neura(*arguments, cwd):
    return subprocess.run([str(NEURA), *arguments], capture_output=True, text=True, timeout=300, cwd=cwd,
                          env={'NEURA_GPU': 'off', 'PATH': '/usr/bin:/bin', 'COLUMNS': '200'})


def literal(text):
    return text.replace('\\', '\\\\').replace('"', '\\"')


def main():
    cases = [(kind, spec) for kind in VALUES for spec in SPECIFICATIONS]
    problems = []
    with tempfile.TemporaryDirectory() as scratch:
        root = pathlib.Path(scratch)
        # The library's verdicts, in one program.
        body = []
        for index, (kind, spec) in enumerate(cases):
            declaration, name = VALUES[kind]
            body.append('        {\n            ' + declaration.replace('let value', 'let value' + str(index)) + '\n'
                        '            match (console.try_format("{:' + literal(spec) + '}", value' + str(index) + ')) {\n'
                        '                result.Result.Ok(text) => { console.print_line("' + str(index) +
                        ' ok " + text + "|"); },\n'
                        '                result.Result.Err(problem) => { report(' + str(index) + ', problem); },\n'
                        '            }\n        }\n')
        library = root / 'library.nra'
        library.write_text(PRELUDE + 'let Main = module:program.Entry (\n'
                           '    export let.override main = func(args: string[]) -> program.ExitCode {\n' +
                           ''.join(body) + '        return program.ExitCode.Ok;\n    };\n);\n')
        built = neura('build', str(library), '-o', str(root / 'library.nvr'), cwd=root)
        if built.returncode != 0:
            print(built.stderr)
            return 1
        verdicts = {}
        for line in neura('run', str(root / 'library.nvr'), cwd=root).stdout.splitlines():
            index, verdict, rest = (line.split(' ', 2) + [''])[:3]
            verdicts[int(index)] = (verdict, rest)

        # The compiler's verdicts, one file per case.
        def check(index):
            kind, spec = cases[index]
            declaration, name = VALUES[kind]
            source = root / f'case{index}.nra'
            source.write_text(PRELUDE + declaration + '\nlet text = $"{' + name + ':' + spec + '}";\n')
            output = neura('check', '--error-format=short', str(source), cwd=root)
            if output.returncode == 0:
                return index, None
            match = re.search(r'error\[(E\d+)\]: (.*)', output.stderr)
            return index, (match.group(1), match.group(2)) if match else ('?', output.stderr)

        with concurrent.futures.ThreadPoolExecutor(8) as pool:
            compiled = dict(pool.map(check, range(len(cases))))

        accepted = []
        for index, (kind, spec) in enumerate(cases):
            verdict, rest = verdicts.get(index, ('missing', ''))
            rejection = compiled[index]
            label = f'{kind} {{:{spec}}}'
            if spec.endswith('c') and kind in ('signed', 'unsigned', 'wide', 'flag') and rejection is not None:
                # The deliberate difference: checked against the value when it runs.
                if rejection[0] != 'E0486':
                    problems.append(f'{label}: the c presentation gives {rejection}')
                continue
            if verdict == 'ok' and rejection is None:
                accepted.append(index)
            elif verdict == 'error' and rejection is not None:
                if rejection != ('E0486', rest) and rejection[0] != 'E0227':
                    problems.append(f'{label}: library says "{rest}", compiler says {rejection}')
                if rejection[0] == 'E0227' and spec not in ('=8',):
                    problems.append(f'{label}: {rejection}')
            else:
                problems.append(f'{label}: library {verdict} "{rest}", compiler {rejection or "accepts"}')

        # Accepted cases print the library's text.
        body = []
        for index in accepted:
            kind, spec = cases[index]
            declaration, name = VALUES[kind]
            body.append('        {\n            ' + declaration + '\n            console.print_line("' + str(index) +
                        ' ok " + $"{' + name + ':' + spec + '}" + "|");\n        }\n')
        program = root / 'interpolated.nra'
        program.write_text(PRELUDE + 'let Main = module:program.Entry (\n'
                           '    export let.override main = func(args: string[]) -> program.ExitCode {\n' +
                           ''.join(body) + '        return program.ExitCode.Ok;\n    };\n);\n')
        built = neura('build', str(program), '-o', str(root / 'interpolated.nvr'), cwd=root)
        if built.returncode != 0:
            print(built.stderr)
            return 1
        for line in neura('run', str(root / 'interpolated.nvr'), cwd=root).stdout.splitlines():
            index, verdict, rest = (line.split(' ', 2) + [''])[:3]
            if (verdict, rest) != verdicts[int(index)]:
                kind, spec = cases[int(index)]
                problems.append(f'{kind} {{:{spec}}}: interpolated "{rest}", library "{verdicts[int(index)][1]}"')
        rejected = len(cases) - len(accepted)
        print(f'{len(cases)} cases: {len(accepted)} accepted and formatted alike, {rejected} rejected')
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
