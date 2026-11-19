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


CALLS = ('let schedule = func(job: string, retries: int = 3, urgent: bool = false) -> string {\n'
         '    return job;\n'
         '};\n'
         'let use = func(retries: int) -> string {\n'
         '    let total = 2;\n'
         '    let text = $"{total} and {retries:>4}";\n'
         '    return schedule(urgent: true, job: nameof(total), retries: retries) + text;\n'
         '};\n')

EXTENSIONS = ('let.extension shout = func(text: string, marks: int = 1) -> string => text + "!";\n'
              'let use = func(word: string) -> string {\n'
              '    return word.shout();\n'
              '};\n')


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

    # Named arguments, defaults, interpolated strings and nameof
    # (docs/design/Calls030.md, Grammar).

    def test_signature_help_marks_the_parameter_of_a_label_and_shows_defaults(self):
        path = self.open(CALLS)
        line = CALLS.splitlines()[6]
        for label, active in (('urgent: ', 2), ('job: ', 0), ('retries: ', 1)):
            column = line.index(label) + len(label)
            help = self.client.request('textDocument/signatureHelp', params(path, 6, column))
            self.assertEqual(help['activeParameter'], active, label)
            labels = [item['label'] for item in help['signatures'][0]['parameters']]
            self.assertEqual(labels, ['job: string', 'retries: int = 3', 'urgent: bool = false'])

    def test_hover_shows_defaults(self):
        path = self.open(CALLS)
        hover = self.client.request('textDocument/hover', params(path, 0, 6))
        self.assertIn('retries: int32 = 3', hover['contents']['value'])
        self.assertIn('urgent: bool = false', hover['contents']['value'])

    def test_completion_offers_the_labels_not_yet_given(self):
        source = CALLS.replace('retries: retries) + text;', ') + text;').replace(
            'schedule(urgent: true, job: nameof(total), ', 'schedule(urgent: true, ')
        path = self.open(source)
        line = source.splitlines()[6]
        column = line.index('urgent: true, ') + len('urgent: true, ')
        items = self.client.request('textDocument/completion', params(path, 6, column))['items']
        labels = [item['label'] for item in items]
        self.assertIn('job:', labels)
        self.assertIn('retries:', labels)
        self.assertNotIn('urgent:', labels)
        self.assertEqual(next(item for item in items if item['label'] == 'job:')['textEdit']['newText'], 'job: ')

    def test_renaming_a_parameter_renames_its_labels(self):
        path = self.open(CALLS)
        rename = self.client.request('textDocument/rename', {**params(path, 0, 34), 'newName': 'attempts'})
        edits = rename['documentChanges'][0]['edits']
        places = sorted((edit['range']['start']['line'], edit['range']['start']['character']) for edit in edits)
        line = CALLS.splitlines()[6]
        self.assertEqual(places, [(0, CALLS.index('retries')), (6, line.index('retries:'))])

    def test_navigation_reaches_into_holes_and_nameof(self):
        path = self.open(CALLS)
        hole = CALLS.splitlines()[5].index('{total}') + 1
        definition = self.client.request('textDocument/definition', params(path, 5, hole))
        if isinstance(definition, list):
            definition = definition[0]
        self.assertEqual(definition['range']['start']['line'], 4)
        references = self.client.request('textDocument/references',
                                         {**params(path, 4, 8), 'context': {'includeDeclaration': True}})
        lines = sorted(reference['range']['start']['line'] for reference in references)
        self.assertEqual(lines, [4, 5, 6])
        rename = self.client.request('textDocument/rename', {**params(path, 4, 8), 'newName': 'sum'})
        self.assertEqual(len(rename['documentChanges'][0]['edits']), 3)
        semantic = self.client.request('textDocument/semanticTokens/full', params(path))['data']
        self.assertEqual(len(semantic) % 5, 0)

    def test_extension_functions_are_listed_among_the_receivers_members(self):
        path = self.open(EXTENSIONS)
        items = self.client.request('textDocument/completion', params(path, 2, 16))['items']
        shout = next(item for item in items if item['label'] == 'shout')
        self.assertIn('extension', shout['detail'])

    def test_incomplete_named_arguments_keep_the_label(self):
        # Edge case 38: the recovering parser keeps the call with its label,
        # and signature help marks the labelled parameter.
        for tail in ('retries: ', 'retries:)'):
            source = CALLS.split('let use')[0] + 'let x = schedule("a", ' + tail
            path = self.open(source)
            column = len('let x = schedule("a", retries:') + (1 if tail.endswith(' ') else 0)
            help = self.client.request('textDocument/signatureHelp', params(path, 3, column))
            self.assertEqual(help['activeParameter'], 1, tail)

    def test_documentation_index_shows_defaults(self):
        path = self.root / 'defaults.nra'
        path.write_text('/// Schedules a job.\nlet schedule = func(job: string, retries: int = -3, '
                        'mode: Mode = Mode.Fast) -> string {\n    return job;\n};\n'
                        'let Mode = enum (Fast, Careful);\n')
        result = subprocess.run([str(BINARY), 'docs', str(path)], capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        signature = json.loads(result.stdout)['files'][0]['symbols'][0]['signature']
        self.assertEqual(signature, 'schedule = func(job: string, retries: int = -3, mode: Mode = Mode.Fast) -> string')
        doxygen = subprocess.run([str(BINARY), 'docs', '--doxygen', str(path)], capture_output=True, text=True,
                                 timeout=30)
        self.assertIn('string schedule(string job, int retries = -3, Mode mode = Mode.Fast);', doxygen.stdout)

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
