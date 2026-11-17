#!/usr/bin/env python3
"""Startup blocks and class hooks in the editor: outline, folding and formatting."""
import pathlib
import subprocess
import tempfile
import unittest

from lsp_protocol import BINARY, Client, params

STARTUP = ('let console = import:Std.Io.Console;\n'
           'static scope {\n'
           '    console.print_line("first");\n'
           '}\n'
           'let Registry = module (\n'
           '    export let size: int32 = 0;\n'
           '    static scope {\n'
           '        console.print_line("module");\n'
           '    }\n'
           ');\n')


def formatted(source):
    result = subprocess.run([str(BINARY), 'fmt', '--stdin', '--stdin-filepath', 'hooks.nra'],
                            input=source.encode(), capture_output=True, timeout=20)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    return result.stdout.decode()


class StartupEditorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-hooks-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client(self.root)
        self.count = 0

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def open(self, source):
        self.count += 1
        path = self.root / f'hooks-{self.count}.nra'
        self.client.open(path, source)
        return path

    def test_outline_lists_startup_blocks_in_source_order(self):
        path = self.open(STARTUP)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        self.assertEqual([item['name'] for item in symbols], ['console', 'static scope', 'Registry'])
        block = symbols[1]
        self.assertEqual(block['range']['start']['line'], 1)
        self.assertEqual(block['range']['end']['line'], 3)
        self.assertEqual(block['selectionRange']['end']['character'], 12)
        registry = symbols[2]
        self.assertEqual([item['name'] for item in registry['children']], ['size', 'static scope'])

    def test_startup_blocks_fold(self):
        path = self.open(STARTUP)
        ranges = self.client.request('textDocument/foldingRange', params(path))
        self.assertIn((1, 3), [(item['startLine'], item['endLine']) for item in ranges])
        self.assertIn((6, 8), [(item['startLine'], item['endLine']) for item in ranges])

    def test_formatter_writes_the_block_on_one_line(self):
        self.assertEqual(formatted('static   scope\n{\nlet x=1;\n}\n'), 'static scope {\n    let x = 1;\n}\n')
        self.assertEqual(formatted('static scope { }\n'), 'static scope {}\n')


if __name__ == '__main__':
    unittest.main()
