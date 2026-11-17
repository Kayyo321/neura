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


HOOKS = ('let Session = class (\n'
         '    | get: instance;\n'
         '    | set: value;\n'
         '\n'
         '    export let user: string = "";\n'
         ');\n'
         'let Admin = class:Session (\n'
         '    | set.override: {\n'
         '        return value;\n'
         '    };\n'
         ');\n')


class HookEditorTests(unittest.TestCase):
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

    def test_completion_offers_the_clause_words_after_a_bar(self):
        path = self.open('let Session = class (\n    | \n);\n')
        result = self.client.request('textDocument/completion', params(path, 1, 6))
        labels = [item['label'] for item in result['items']]
        self.assertEqual(labels, ['get', 'set'])

    def test_completion_offers_instance_and_value_inside_a_setter(self):
        path = self.open(HOOKS)
        result = self.client.request('textDocument/completion', params(path, 8, 15))
        labels = [item['label'] for item in result['items']]
        self.assertIn('value', labels)
        self.assertIn('instance', labels)

    def test_outline_hides_the_clause_parameters(self):
        path = self.open(HOOKS)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        self.assertEqual([child['name'] for child in symbols[0]['children']], ['user'])

    def test_hover_lists_the_effective_clauses_and_their_origin(self):
        path = self.open(HOOKS)
        hover = self.client.request('textDocument/hover', params(path, 6, 5))
        value = hover['contents']['value']
        self.assertIn('`get` inherited from `Session`', value)
        self.assertIn('`set` declared here', value)

    def test_override_navigates_to_the_inherited_clause(self):
        path = self.open(HOOKS)
        location = self.client.request('textDocument/definition', params(path, 7, 15))
        self.assertEqual(location['range']['start'], {'line': 2, 'character': 4})

    def test_formatter_puts_clauses_on_their_own_lines_before_the_members(self):
        source = 'let Session = class ( | get: instance; | set:value; export let user: string = ""; );\n'
        self.assertEqual(formatted(source),
                         'let Session = class (\n'
                         '    | get: instance;\n'
                         '    | set: value;\n'
                         '\n'
                         '    export let user: string = "";\n'
                         ');\n')
        source = 'let Session = class (\n    | set: {\n        return value;\n    };\n    | get: instance;\n);\n'
        self.assertEqual(formatted(source), source)


if __name__ == '__main__':
    unittest.main()
