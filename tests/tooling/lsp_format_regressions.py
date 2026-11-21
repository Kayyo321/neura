#!/usr/bin/env python3
"""Formatting incomplete editor snapshots leaves source untouched.

Run: python3 tests/tooling/lsp_format_regressions.py build/neura.
"""

import pathlib
import tempfile
import unittest

from lsp_protocol import Client, params


class EditorFormattingTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-lsp-format-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client(self.root)

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def formatting(self, path, method, **options):
        request = {**params(path), 'options': {'tabSize': 4, 'insertSpaces': True, **options}}
        if method == 'textDocument/rangeFormatting':
            request['range'] = {'start': {'line': 0, 'character': 0},
                                'end': {'line': 0, 'character': 1}}
        return self.client.request(method, request)

    def test_incomplete_source_has_no_formatting_edits_or_request_error(self):
        sources = [
            'let Example = class',
            'let Example = interface',
            'let Example = module (\n    let.',
            'let run = func(',
            'let run = func {\n    let value = 1;',
            'let value = 1',
            'let value = "unterminated',
        ]
        for index, source in enumerate(sources):
            with self.subTest(source=source):
                path = self.root / f'incomplete-{index}.nra'
                self.client.open(path, source)
                for method in ('textDocument/formatting', 'textDocument/rangeFormatting'):
                    self.assertEqual(self.formatting(path, method), [])
                # The failed format never replaces the snapshot. Compiler
                # diagnostics remain available after both requests.
                reports = [notification['params']['diagnostics']
                           for notification in self.client.notifications
                           if notification.get('method') == 'textDocument/publishDiagnostics'
                           and notification['params']['uri'] == path.as_uri()]
                self.assertTrue(reports[-1])

    def test_valid_shorthand_and_anonymous_class_still_format(self):
        path = self.root / 'valid.nra'
        source = ('let run=func->int=>1;'
                  'let value:class(export let count:int=1;)=new;')
        self.client.open(path, source)
        edits = self.formatting(path, 'textDocument/formatting')
        self.assertEqual(len(edits), 1)
        self.assertIn('func -> int => 1;', edits[0]['newText'])
        self.assertIn(') = new;', edits[0]['newText'])

    def test_tag_style_preserves_spelling_by_default_and_converts_on_request(self):
        path = self.root / 'tags.nra'
        mixed = '#Note("one")\r\nlet one = 1;\r\n.Note("two")\r\nlet two = 2;\r\n'
        self.client.open(path, mixed)
        full = 'textDocument/formatting'
        self.assertEqual(self.formatting(path, full), [])
        self.assertEqual(self.formatting(path, full, tagStyle='keep'), [])
        self.assertEqual(self.formatting(path, full, tagStyle='dot')[0]['newText'],
                         mixed.replace('#Note', '.Note'))
        self.assertEqual(self.formatting(path, 'textDocument/rangeFormatting', tagStyle='hash')[0]['newText'],
                         mixed.replace('.Note', '#Note'))

        unformatted = mixed.replace(' = ', '=').replace('1;\r\n', '1;\n', 1).rstrip()
        converted = unformatted.replace('#Note', '.Note')
        self.client.notify('textDocument/didChange', {
            'textDocument': {'uri': path.as_uri(), 'version': 2},
            'contentChanges': [{'text': unformatted}],
        })
        self.assertEqual(self.formatting(path, full, tagStyle='dot')[0]['newText'], converted)
        self.client.notify('textDocument/didChange', {
            'textDocument': {'uri': path.as_uri(), 'version': 3},
            'contentChanges': [{'text': converted}],
        })
        self.assertEqual(self.formatting(path, full, tagStyle='dot'), [])

    def test_next_valid_snapshot_formats_after_incomplete_source(self):
        path = self.root / 'recover.nra'
        self.client.open(path, 'let Example = class')
        self.assertEqual(self.formatting(path, 'textDocument/formatting'), [])
        self.client.notify('textDocument/didChange', {
            'textDocument': {'uri': path.as_uri(), 'version': 2},
            'contentChanges': [{'text': 'let Example=class();'}],
        })
        edits = self.formatting(path, 'textDocument/formatting')
        self.assertEqual(len(edits), 1)
        self.assertEqual(edits[0]['newText'], 'let Example = class ();\n')


if __name__ == '__main__':
    unittest.main()
