#!/usr/bin/env python3
"""Real compiler/LSP contract checks, including unsaved cross-file source."""
import json
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import unittest

BINARY = pathlib.Path(sys.argv.pop(1)).resolve()
REPOSITORY = pathlib.Path(__file__).resolve().parents[2]


class Client:
    def __init__(self, root):
        self.process = subprocess.Popen([str(BINARY), 'lsp', '--stdio'], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=root)
        self.messages = queue.Queue()
        self.sequence = 0
        self.notifications = []
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        result = self.request('initialize', {'rootUri': pathlib.Path(root).as_uri(),
                              'capabilities': {}, 'initializationOptions': {
                                  'sourceRoots': [str(root)], 'stdlibPath': str(REPOSITORY / 'stdlib'),
                                  'trusted': True}})
        assert result['capabilities']['positionEncoding'] == 'utf-16'
        self.notify('initialized', {})

    def read(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        self.messages.put(RuntimeError('LSP exited: ' + self.process.stderr.read().decode()))
                        return
                    if line in (b'\r\n', b'\n'):
                        break
                    key, value = line.decode().split(':', 1)
                    headers[key.lower()] = value.strip()
                data = self.process.stdout.read(int(headers['content-length']))
                self.messages.put(json.loads(data))
        except BaseException as error:
            self.messages.put(error)

    def send(self, message):
        data = json.dumps(message, ensure_ascii=False).encode()
        self.process.stdin.write(f'Content-Length: {len(data)}\r\n\r\n'.encode() + data)
        self.process.stdin.flush()

    def notify(self, method, params):
        self.send({'jsonrpc': '2.0', 'method': method, 'params': params})

    def request(self, method, params):
        self.sequence += 1
        expected = self.sequence
        self.send({'jsonrpc': '2.0', 'id': expected, 'method': method, 'params': params})
        while True:
            message = self.messages.get(timeout=30)
            if isinstance(message, BaseException):
                raise message
            if message.get('id') == expected:
                if 'error' in message:
                    raise AssertionError(message['error'])
                return message.get('result')
            self.notifications.append(message)

    def open(self, path, source, version=1):
        self.notify('textDocument/didOpen', {'textDocument': {'uri': path.as_uri(),
                    'languageId': 'neura', 'version': version, 'text': source}})

    def close(self):
        if self.process.poll() is None:
            self.request('shutdown', {})
            self.notify('exit', {})
            self.process.stdin.close()
            assert self.process.wait(timeout=10) == 0
        self.process.stdout.close()
        self.process.stderr.close()


def params(path, line=0, character=0):
    return {'textDocument': {'uri': path.as_uri()}, 'position': {'line': line, 'character': character}}


class LanguageServerTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-lsp-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client(self.root)

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def test_unsaved_symbols_completion_hover_and_tokens(self):
        path = self.root / 'new.nra'
        source = ('/// @brief Keep a value.\n'
                  'let identity = func(value: int32) -> int32 {\n'
                  '    let answer = value;\n'
                  '    return answer;\n'
                  '};\n'
                  'let compute = func -> int32 => identity(42);\n')
        self.client.open(path, source)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        self.assertEqual([s['name'] for s in symbols], ['identity', 'compute'])
        self.assertIn('value', [s['name'] for s in symbols[0]['children']])
        hover = self.client.request('textDocument/hover', params(path, 3, 12))
        self.assertIn('int32', hover['contents']['value'])
        completion = self.client.request('textDocument/completion', params(path, 3, 11))
        self.assertIn('answer', [item['label'] for item in completion['items']])
        tokens = self.client.request('textDocument/semanticTokens/full', params(path))['data']
        self.assertTrue(tokens)
        self.assertEqual(len(tokens) % 5, 0)
        self.assertFalse(any(n.get('params', {}).get('diagnostics') for n in self.client.notifications
                             if n.get('method') == 'textDocument/publishDiagnostics'))
        doc = self.client.request('neura/declarationDocumentation', params(path, 1, 5))
        self.assertEqual(doc['parameters'], ['value'])
        self.assertIn('Keep a value', doc['documentation']['text'])

    def test_incomplete_source_retains_other_declarations_and_quick_fix(self):
        path = self.root / 'broken.nra'
        source = 'let first = 1;\nlet missing = 2\nlet third = 3;\n'
        self.client.open(path, source)
        symbols = self.client.request('textDocument/documentSymbol', params(path))
        self.assertIn('first', [s['name'] for s in symbols])
        self.assertIn('third', [s['name'] for s in symbols])
        actions = self.client.request('textDocument/codeAction', {
            **params(path), 'range': {'start': {'line': 1, 'character': 15},
                                     'end': {'line': 1, 'character': 15}}, 'context': {'diagnostics': []}})
        self.assertTrue(any(action['kind'] == 'quickfix' for action in actions))
        self.assertTrue(any(action.get('command', {}).get('command') == 'neura.explainDiagnostic'
                            and action['command'].get('arguments') == ['E0201'] for action in actions))
        self.client.notify('textDocument/didChange', {'textDocument': {'uri': path.as_uri(), 'version': 2},
                           'contentChanges': [{'text': source.replace('2\n', '2;\n')}]})
        self.client.request('textDocument/documentSymbol', params(path))
        diagnostics = [n['params']['diagnostics'] for n in self.client.notifications
                       if n.get('method') == 'textDocument/publishDiagnostics']
        self.assertTrue(diagnostics[0])
        self.assertEqual(diagnostics[-1], [])

    def test_unsaved_import_definition_references_and_rename(self):
        module = self.root / 'math.nra'
        main = self.root / 'main.nra'
        module_source = ('let Math = module (\n'
                         '    export let add = func(a: int32, b: int32) -> int32 => a + b;\n'
                         ');\n')
        main_source = ('let math = import:Math;\n'
                       'let compute = func -> int32 => math.add(2, 3);\n')
        self.client.open(module, module_source)
        self.client.open(main, main_source)
        definition = self.client.request('textDocument/definition', params(main, 1, 36))
        self.assertEqual(definition['uri'], module.as_uri())
        self.assertEqual(definition['range']['start']['line'], 1)
        completion = self.client.request('textDocument/completion', params(main, 1, 36))
        self.assertIn('add', [item['label'] for item in completion['items']])
        refs = self.client.request('textDocument/references', {**params(main, 1, 36),
                                   'context': {'includeDeclaration': True}})
        self.assertEqual({item['uri'] for item in refs}, {module.as_uri(), main.as_uri()})
        rename = self.client.request('textDocument/rename', {**params(main, 1, 36), 'newName': 'sum'})
        self.assertEqual({item['textDocument']['uri'] for item in rename['documentChanges']},
                         {module.as_uri(), main.as_uri()})
        self.assertTrue(all(edit['newText'] == 'sum' for item in rename['documentChanges'] for edit in item['edits']))

    def test_prepare_rename_uses_reference_range(self):
        module = self.root / 'math.nra'
        main = self.root / 'main.nra'
        self.client.open(module, 'let Math = module (export let add = func(a: int32, b: int32) -> int32 => a + b;);\n')
        self.client.open(main, 'let math = import:Math;\nlet compute = func -> int32 => math.add(2, 3);\n')
        prepared = self.client.request('textDocument/prepareRename', params(main, 1, 37))
        self.assertEqual(prepared['range'], {'start': {'line': 1, 'character': 36},
                                             'end': {'line': 1, 'character': 39}})
        self.assertEqual(prepared['placeholder'], 'add')

    def test_primitive_type_definition_is_absent_with_anonymous_closure(self):
        path = self.root / 'closures.nra'
        source = ('let make = func -> func(int32) -> int32 {\n'
                  ' return func(value: int32) -> int32 => value;\n'
                  '};\n'
                  'let count = 1;\n')
        self.client.open(path, source)
        self.assertIsNone(self.client.request('textDocument/typeDefinition', params(path, 3, 5)))

    def test_documentation_insertion_preserves_inline_declarations(self):
        path = self.root / 'inline.nra'
        source = 'let M = module ( export let f = func(value: int32) -> int32 => value; );\r\n'
        self.client.open(path, source)
        selected = source.index('f =')
        actions = self.client.request('textDocument/codeAction', {**params(path),
                    'range': {'start': {'line': 0, 'character': selected},
                              'end': {'line': 0, 'character': selected + 1}},
                    'context': {'diagnostics': [], 'only': ['refactor.rewrite']}})
        action = next(item for item in actions if item['title'] == 'Generate Neura documentation comment')
        updated = source
        for edit in reversed(action['edit']['documentChanges'][0]['edits']):
            begin = edit['range']['start']['character']
            end = edit['range']['end']['character']
            updated = updated[:begin] + edit['newText'] + updated[end:]
        self.assertNotIn('\n', updated.replace('\r\n', ''))
        for line in updated.splitlines():
            if line.strip().startswith('///'):
                self.assertNotIn('module', line)
        self.client.notify('textDocument/didChange', {'textDocument': {'uri': path.as_uri(), 'version': 2},
                           'contentChanges': [{'text': updated}]})
        self.client.request('textDocument/documentSymbol', params(path))
        diagnostics = [n['params']['diagnostics'] for n in self.client.notifications
                       if n.get('method') == 'textDocument/publishDiagnostics']
        self.assertEqual(diagnostics[-1], [])

    def test_utf16_crlf_diagnostic_and_versioned_edit(self):
        path = self.root / 'unicode.nra'
        source = 'let text = "😀"; let count = 1\r\nlet next = 2;\r\n'
        self.client.open(path, source)
        self.client.request('textDocument/documentSymbol', params(path))
        diagnostics = [n['params']['diagnostics'] for n in self.client.notifications
                       if n.get('method') == 'textDocument/publishDiagnostics'][-1]
        self.assertEqual(diagnostics[0]['range']['start'], {'line': 0, 'character': 30})
        actions = self.client.request('textDocument/codeAction', {**params(path),
                    'range': {'start': {'line': 0, 'character': 30}, 'end': {'line': 0, 'character': 30}},
                    'context': {'diagnostics': diagnostics}})
        self.assertTrue(actions)
        change = next(a for a in actions if a['kind'] == 'quickfix' and 'edit' in a)['edit']['documentChanges'][0]
        self.assertEqual(change['textDocument']['version'], 1)
        self.assertEqual(change['edits'][0]['newText'], ';')

    def test_real_entry_detection_and_configuration(self):
        path = self.root / 'app.nra'
        source = ('let program = import:Std.Program;\n'
                  'let App = module:program.Entry (\n'
                  ' export let.override main = func(args: string[]) -> program.ExitCode {\n'
                  '  return program.ExitCode.Ok;\n'
                  ' };\n'
                  ');\n'
                  'let main = func -> void {};\n')
        self.client.open(path, source)
        entries = self.client.request('neura/entryPoints', params(path))
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]['name'], 'App.main')
        lenses = self.client.request('textDocument/codeLens', params(path))
        self.assertEqual([lens['command']['title'] for lens in lenses], ['Run', 'Build', 'Debug'])
        self.client.notify('workspace/didChangeConfiguration', {'settings': {'neura': {'codeLens': {'enabled': False}}}})
        self.assertEqual(self.client.request('textDocument/codeLens', params(path)), [])

    def test_entry_points_preserve_open_symlink_uri(self):
        actual = self.root / 'actual'
        actual.mkdir()
        alias = self.root / 'alias'
        alias.symlink_to(actual, target_is_directory=True)
        path = alias / 'app.nra'
        source = ('let program = import:Std.Program;\n'
                  'let App = module:program.Entry (\n'
                  ' export let.override main = func(args: string[]) -> program.ExitCode {\n'
                  '  return program.ExitCode.Ok;\n'
                  ' };\n'
                  ');\n')
        self.client.open(path, source)
        entries = self.client.request('neura/entryPoints', params(path))
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]['uri'], path.as_uri())
        lenses = self.client.request('textDocument/codeLens', params(path))
        self.assertEqual([lens['command']['title'] for lens in lenses], ['Run', 'Build', 'Debug'])

    def test_formatting_and_readonly_property_classification(self):
        path = self.root / 'accessors.nra'
        source = ('let Account = class (\n'
                  ' export let balance: int64 = 0;\n'
                  '  | get: balance;\n'
                  '  | set: balance = value;\n'
                  ');\n'
                  'let read = func(account: Account) -> int64 => account.balance;\n')
        self.client.open(path, source)
        tokens = self.client.request('textDocument/semanticTokens/full', params(path))['data']
        types = tokens[3::5]
        self.assertIn(22, types)  # getter declaration
        self.assertIn(23, types)  # setter declaration
        self.assertIn(24, types)  # resolved getter invocation
        edits = self.client.request('textDocument/formatting', {**params(path),
                                     'options': {'tabSize': 4, 'insertSpaces': True}})
        self.assertTrue(edits)
        self.assertIn('| get:', edits[0]['newText'])


if __name__ == '__main__':
    unittest.main()
