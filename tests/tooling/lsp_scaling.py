#!/usr/bin/env python3
"""Real editor scaling and immutable coordinate-index regressions.

The broad limits detect accidental quadratic prefix/token walks while allowing
Debug builds and shared CI runners; release benchmarks use tighter budgets.
"""
import pathlib
import tempfile
import time
import unittest

from lsp_protocol import Client, params


class Scaling(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-lsp-scaling-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client(self.root)

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def test_large_symbol_reference_and_completion_requests(self):
        path = self.root / 'large.nra'
        pieces = ['let Library = module (']
        for index in range(1000):
            pieces.append(f'''    export let function_{index} = func(value: int32) -> int32 {{
        let a = value;
        let b = a;
        let c = b;
        let d = c;
        let e = d;
        let f = e;
        return f;
    }};
''')
        pieces.append(');')
        source = '\n'.join(pieces) + '\n'
        self.assertGreaterEqual(len(source.splitlines()), 10000)
        path.write_text(source)
        started = time.monotonic()
        self.client.open(path, source)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        cold = time.monotonic() - started
        self.assertLess(cold, 15, 'large-file analysis and coordinates must avoid full-prefix walks')
        self.assertEqual(len(symbols[0]['children']), 1000)
        self.assertEqual(symbols[0]['children'][-1]['name'], 'function_999')
        declaration = self.client.request('textDocument/definition', params(path, 9998, 15))
        self.assertEqual(declaration['range']['start'], {'line': 9997, 'character': 12})
        started = time.monotonic()
        for _ in range(10):
            completion = self.client.request('textDocument/completion', params(path, 2, 15))
            self.assertTrue(completion['items'])
        self.assertLess(time.monotonic() - started, 5, 'warm completion must reuse the immutable analysis')
        changed = source.replace('let a = value', 'let a = value + 1', 1)
        started = time.monotonic()
        self.client.notify('textDocument/didChange', {'textDocument': {'uri': path.as_uri(), 'version': 2},
                            'contentChanges': [{'text': changed}]})
        self.assertEqual(len(self.client.request('textDocument/documentSymbol', params(path))[0]['children']), 1000)
        self.assertLess(time.monotonic() - started, 15, 'an edit must rebuild bounded indexes once')

    def test_coordinate_index_changes_with_same_length_text(self):
        path = self.root / 'coordinates.nra'
        source = 'let before = "😀";\r\nlet target = 1;\r\n'
        self.client.open(path, source)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        self.assertEqual(symbols[1]['selectionRange']['start'], {'line': 1, 'character': 4})
        changed = source.replace('\r\n', '  ', 1)
        self.assertEqual(len(changed.encode()), len(source.encode()))
        self.client.notify('textDocument/didChange', {'textDocument': {'uri': path.as_uri(), 'version': 2},
                            'contentChanges': [{'text': changed}]})
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        prefix = changed[:changed.index('target')]
        self.assertEqual(symbols[1]['selectionRange']['start'],
                         {'line': 0, 'character': len(prefix.encode('utf-16-le')) // 2})


if __name__ == '__main__': unittest.main()
