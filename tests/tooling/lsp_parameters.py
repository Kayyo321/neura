#!/usr/bin/env python3
"""Grouped parameters and the constructor shorthand in the editor."""
import pathlib
import tempfile
import unittest

import json
import subprocess

from lsp_protocol import BINARY, Client, params

GROUPED = ('let distance = func(a, b: float64, label: string) -> string {\n'
           '    return label;\n'
           '};\n'
           'let use = func() -> string {\n'
           '    return distance(1.0, 2.0, "x");\n'
           '};\n')

SHORTHAND = ('let Point = class (\n'
             '    export last x: int;\n'
             '    export last y: int;\n'
             '    export let init = func(x, y: int) => set;\n'
             ');\n')


class ParameterEditorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-parameters-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client(self.root)
        self.count = 0

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def open(self, source):
        self.count += 1
        path = self.root / f'parameters-{self.count}.nra'
        self.client.open(path, source)
        return path

    def test_signature_help_counts_each_expanded_parameter(self):
        path = self.open(GROUPED)
        for column, active in ((21, 0), (26, 1), (31, 2)):
            help = self.client.request('textDocument/signatureHelp', params(path, 4, column))
            labels = [item['label'] for item in help['signatures'][0]['parameters']]
            self.assertEqual(labels, ['a: float64', 'b: float64', 'label: string'])
            self.assertEqual(help['signatures'][0]['activeParameter'], active, column)

    def test_each_grouped_name_is_a_symbol_with_the_shared_type(self):
        path = self.open(GROUPED)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        children = [child['name'] for child in symbols[0]['children']]
        self.assertEqual(children, ['a', 'b', 'label'])
        for column, name in ((20, 'a'), (23, 'b')):
            hover = self.client.request('textDocument/hover', params(path, 0, column))
            self.assertIn(name + ': float64', hover['contents']['value'])
        semantic = self.client.request('textDocument/semanticTokens/full', params(path))['data']
        self.assertEqual(len(semantic) % 5, 0)

    def test_shorthand_parameters_are_symbols_and_hover(self):
        path = self.open(SHORTHAND)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        init = next(child for child in symbols[0]['children'] if child['name'] == 'init')
        self.assertEqual([child['name'] for child in init['children']], ['x', 'y'])
        hover = self.client.request('textDocument/hover', params(path, 3, 27))
        self.assertIn('x: int', hover['contents']['value'])
        self.assertIsNotNone(self.client.request('textDocument/semanticTokens/full', params(path)))

    def test_incomplete_parameter_lists_do_not_stop_the_server(self):
        cases = [
            'let f = func(a, ',
            'let f = func(a, b',
            'let f = func(a, b:',
            'let f = func(a, b: int,',
            'let f = func(a,, b: int) {};',
            'let f = func(a, items: ...string) {};',
            'let C = class (\n export let init = func(x, y: int) =>',
            'let C = class (\n export let init = func(x, y: int) => set',
            'let C = class (\n export let init = func(x, y',
            'let C = class : (\n export let init = func(x, y: int) => set;\n);',
            'let C = class (\n export let init = func(x, y: int) => set;\n',
        ]
        for source in cases:
            path = self.open(source)
            end = source.count('\n'), len(source.rsplit('\n', 1)[-1])
            for method in ('textDocument/documentSymbol', 'textDocument/semanticTokens/full'):
                self.client.request(method, params(path))
            for method in ('textDocument/hover', 'textDocument/signatureHelp', 'textDocument/completion',
                           'textDocument/definition', 'textDocument/references'):
                self.client.request(method, params(path, *end))
            self.client.request('textDocument/hover', params(path, 0, 0))
        # The server still answers after every case.
        path = self.open(GROUPED)
        self.assertTrue(self.client.request('textDocument/documentSymbol', params(path)))

    def test_documentation_index_names_every_grouped_parameter(self):
        path = self.root / 'documented.nra'
        path.write_text('/// Distance.\n/// @param a first\nlet distance = func(a, b: float64, label: string) -> string {\n'
                        '    return label;\n};\n')
        result = subprocess.run([str(BINARY), 'docs', str(path)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        index = json.loads(result.stdout)
        signature = index['files'][0]['symbols'][0]['signature']
        self.assertEqual(signature, 'distance = func(a: float64, b: float64, label: string) -> string')
        doxygen = subprocess.run([str(BINARY), 'docs', '--doxygen', str(path)], capture_output=True, text=True,
                                 timeout=30)
        self.assertIn('string distance(float64 a, float64 b, string label);', doxygen.stdout)

    def test_renaming_a_field_renames_its_shorthand_parameter(self):
        path = self.open(SHORTHAND)
        rename = self.client.request('textDocument/rename', {**params(path, 1, 16), 'newName': 'left'})
        edits = rename['documentChanges'][0]['edits']
        lines = sorted(edit['range']['start']['line'] for edit in edits)
        # The field, and the parameter of `init` that sets it.
        self.assertEqual(lines, [1, 3])
        parameter = next(edit for edit in edits if edit['range']['start']['line'] == 3)
        self.assertEqual(parameter['range']['start']['character'], 27)

    def test_renaming_only_a_shorthand_parameter_is_refused(self):
        path = self.open(SHORTHAND)
        with self.assertRaises(AssertionError) as refused:
            self.client.request('textDocument/rename', {**params(path, 3, 27), 'newName': 'left'})
        self.assertIn('not an instance field', str(refused.exception))
        self.assertTrue(self.client.request('textDocument/documentSymbol', params(path)))


if __name__ == '__main__':
    unittest.main()
