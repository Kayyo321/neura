#!/usr/bin/env python3
"""Formatter contracts. Run: python3 tests/tooling/formatter_tests.py build/neura.

--standalone supports a small formatter-only development harness.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile
import unittest


parser = argparse.ArgumentParser()
parser.add_argument("binary", type=pathlib.Path)
parser.add_argument("--standalone", action="store_true")
arguments, unittest_arguments = parser.parse_known_args()
COMMAND = [str(arguments.binary.resolve())] + ([] if arguments.standalone else ["fmt"])


def execute(*flags, source=None):
    return subprocess.run(COMMAND + list(flags), input=source, capture_output=True, timeout=20)


class FormatterTests(unittest.TestCase):
    def formatted(self, source, *flags):
        original = source.encode() if isinstance(source, str) else source
        result = execute("--stdin", "--stdin-filepath", "format-contract.nra", *flags, source=original)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
        repeated = execute("--stdin", "--stdin-filepath", "format-contract.nra", *flags, source=result.stdout)
        self.assertEqual(repeated.returncode, 0, repeated.stderr.decode(errors="replace"))
        self.assertEqual(repeated.stdout, result.stdout, "formatting must be idempotent")
        return result.stdout.decode()

    def test_binary_spacing_and_final_newline(self):
        self.assertEqual(self.formatted("let count=1;"), "let count = 1;\n")
        self.assertEqual(self.formatted(""), "")

    def test_comments_and_documentation_attachment(self):
        source = '''// ordinary comment: https://example.test/a//b
/// Documentation has @param [in] name and \\return text.
///
/// More **Markdown** and `code`.
let describe=func(name:string)->string{ // trailing function comment
let url="https://example.test/a//b"; /// ordinary inline comment
return name+url; // final comment
};

/// This comment is deliberately detached.

let other=1;
'''
        formatted = self.formatted(source)
        for line in source.splitlines():
            if "//" in line and '"https://' not in line:
                comment = line[line.index("//"):]
                self.assertIn(comment, formatted)
        self.assertIn('; /// ordinary inline comment\n', formatted)
        self.assertIn('/// This comment is deliberately detached.\n\nlet other', formatted)

    def test_class_accessors_tags_and_member_separation(self):
        source = '''#Final
let Gauge=class(export let level:int=0;|get:level;|set:{if(value<0){return;}level=value;};export let reset=func(){level=0;};export let read=func()->int=>level;);'''
        formatted = self.formatted(source)
        self.assertIn('#Final\nlet Gauge = class (\n', formatted)
        self.assertIn('    export let level: int = 0;\n        | get: level;\n        | set: {\n', formatted)
        self.assertIn('        };\n\n    export let reset', formatted)
        self.assertIn('    };\n\n    export let read', formatted)

    def test_generics_shifts_references_and_unary_operators(self):
        source = '''let map=func<T:Renderable>(value:List<List<T>>)->List<T> =>value[0];
let adjust=func(value:int,other:int)->int{return(value<<2)>>1- -other;};
let reference=func(value:&int)->&int=>value;'''
        formatted = self.formatted(source)
        self.assertIn('func<T: Renderable>(value: List<List<T>>) -> List<T>', formatted)
        self.assertIn('(value << 2) >> 1 - -other', formatted)
        self.assertIn('value: &int', formatted)
        self.assertIn('-> &int', formatted)

    def test_for_header_and_overload_groups(self):
        source = '''let Example=module(export let run=func(){for(let i=0;i<10;i++){if(i==3){continue;}else{let last=i;last++;}}}or func(value:int){do{value--;}while(value>0);};);'''
        formatted = self.formatted(source)
        self.assertIn('for (let i = 0; i < 10; i++) {', formatted)
        self.assertIn('} else {', formatted)
        self.assertIn('} or func(value: int) {', formatted)
        self.assertIn('} while (value > 0);', formatted)

    def test_enum_payloads_and_inline_tags(self):
        source = '''let Shape=enum(#Likely Empty,Point{x:int,y:int},Line(int,int));
let make=func->Shape=>Shape.Point{x:1,y:2};'''
        formatted = self.formatted(source)
        self.assertIn('enum (#Likely Empty, Point { x: int, y: int }, Line(int, int))', formatted)
        self.assertIn('Shape.Point { x: 1, y: 2 }', formatted)

    def test_named_match_pattern_does_not_flatten_body(self):
        source = '''let Shape=enum(Point{x:int,y:int},Empty);
let handle=func(value:Shape){match(value){Shape.Point{x,y}=>{if(x>y){return;}},Shape.Empty=>{return;}}};'''
        formatted = self.formatted(source)
        self.assertIn('Shape.Point { x, y } => {\n', formatted)
        self.assertIn('            if (x > y) {\n                return;', formatted)

    def test_nested_anonymous_class_member_spacing_is_idempotent(self):
        source = 'let Container=class(export let inner:class(export let value:int=1;)=new;export let name:string="nested";);'
        formatted = self.formatted(source)
        self.assertIn('    ) = new;\n\n    export let name:', formatted)

    def test_crlf_bom_and_unicode_comment(self):
        formatted = self.formatted(b'\xef\xbb\xbf/// Unicode: \xf0\x9f\x8c\xb8\r\nlet value=1;\r\n')
        self.assertTrue(formatted.startswith('\ufeff/// Unicode: \U0001f338\r\n'))
        self.assertNotIn('\n', formatted.replace('\r\n', ''))

    def test_tabs_and_custom_width(self):
        source = 'let Example=module(export let value=1;);'
        self.assertIn('\n  export let value', self.formatted(source, '--tab-size', '2'))
        self.assertIn('\n\texport let value', self.formatted(source, '--use-tabs'))

    def test_destructuring_and_array_types(self):
        formatted = self.formatted('let [left,right]:[int,int]=1,2;let values:int[]=[1,2];')
        self.assertIn('let [left, right]: [int, int] = 1, 2;', formatted)
        self.assertIn('let values: int[] = [1, 2];', formatted)

    def test_invalid_syntax_refuses_output(self):
        result = execute('--stdin', source=b'let invalid = "unterminated')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'')

    def test_check_and_explicit_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'source.nra'
            original = b'let count=1;'
            path.write_bytes(original)
            path.chmod(0o640)
            result = execute('--check', str(path))
            self.assertEqual(result.returncode, 1)
            self.assertEqual(path.read_bytes(), original)
            self.assertIn(b'needs formatting', result.stderr)
            result = execute('--write', str(path))
            self.assertEqual(result.returncode, 0, result.stderr.decode())
            self.assertEqual(result.stdout, b'')
            self.assertEqual(path.read_bytes(), b'let count = 1;\n')
            self.assertEqual(path.stat().st_mode & 0o777, 0o640)
            self.assertEqual(execute('--check', str(path)).returncode, 0)

    def test_invalid_batch_does_not_write_valid_files(self):
        with tempfile.TemporaryDirectory() as directory:
            valid = pathlib.Path(directory) / 'valid.nra'
            invalid = pathlib.Path(directory) / 'invalid.nra'
            valid.write_bytes(b'let count=1;')
            invalid.write_bytes(b'let invalid=;')
            result = execute('--write', str(valid), str(invalid))
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(valid.read_bytes(), b'let count=1;')

    def test_symlink_not_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            target = pathlib.Path(directory) / 'target.nra'
            link = pathlib.Path(directory) / 'link.nra'
            target.write_bytes(b'let count=1;')
            link.symlink_to(target)
            result = execute('--write', str(link))
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(link.is_symlink())
            self.assertEqual(target.read_bytes(), b'let count=1;')

    def test_invalid_options(self):
        for flags in [('--write', '--stdin'), ('--check', '--write'), ('--tab-size', '0'),
                      ('--tab-size', '17'), ('--tab-size', 'two'), ('--unknown',)]:
            with self.subTest(flags=flags):
                result = execute(*flags, source=b'let value=1;')
                self.assertNotEqual(result.returncode, 0)

    def test_real_language_corpus(self):
        repository = pathlib.Path(__file__).resolve().parents[2]
        sources = sorted((repository / 'examples').rglob('*.nra'))
        sources += sorted((repository / 'stdlib').rglob('*.nra'))
        self.assertGreater(len(sources), 100)
        for source in sources:
            with self.subTest(source=str(source.relative_to(repository))):
                self.formatted(source.read_bytes())


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + unittest_arguments)
