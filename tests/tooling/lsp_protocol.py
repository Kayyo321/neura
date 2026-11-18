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
                              'capabilities': {'workspace': {'workspaceEdit': {'changeAnnotationSupport': {}}}},
                              'initializationOptions': {
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

    def test_record_field_rename_pins_each_derived_wire_name(self):
        path = self.root / 'model.nra'
        source = ('let json = import:Std.Encoding.Json;\n'
                  'let User = record (name: string);\n'
                  'let A = derive json.Codec<User>;\n'
                  'let B = derive json.Codec<User> { field User.name: "nickname"; };\n'
                  'let console = import:Std.Io.Console;\n'
                  'let program = import:Std.Program;\n'
                  'let.promote _ = import:Std.Core.Result;\n'
                  'let App = module:program.Entry (\n'
                  '    export let.override main = func(args: string[]) -> program.ExitCode {\n'
                  '        last user = new User("Ada");\n'
                  '        match (A.encode(user)) { Result.Ok(text) => { console.print_line(text); }, '
                  'Result.Err(_) => { return program.ExitCode.Failure; }, }\n'
                  '        match (B.encode(user)) { Result.Ok(text) => { console.print_line(text); }, '
                  'Result.Err(_) => { return program.ExitCode.Failure; }, }\n'
                  '        return program.ExitCode.Ok;\n'
                  '    };\n'
                  ');\n')
        self.client.open(path, source)
        response = self.client.request('textDocument/rename', {**params(path, 1, 21), 'newName': 'display'})
        edits = response['documentChanges'][0]['edits']
        pinned = [edit for edit in edits if edit.get('annotationId') == 'neura.preserveSchema']
        self.assertEqual(len(pinned), 1)
        self.assertIn('field User.display: "name";', pinned[0]['newText'])
        lines = source.splitlines(keepends=True)
        for edit in sorted(edits, key=lambda e: (e['range']['start']['line'], e['range']['start']['character']),
                           reverse=True):
            start, end = edit['range']['start'], edit['range']['end']
            self.assertEqual(start['line'], end['line'])
            line = lines[start['line']]
            lines[start['line']] = line[:start['character']] + edit['newText'] + line[end['character']:]
        self.assertIn('record (display: string)', lines[1])
        self.assertIn('field User.display: "nickname";', lines[3])
        self.assertTrue(response['changeAnnotations']['neura.preserveSchema']['label'])
        for snapshot in (source, ''.join(lines)):
            path.write_text(snapshot)
            image = self.root / 'rename.nvr'
            subprocess.run([BINARY, 'build', path, '-o', image], check=True, capture_output=True)
            program = subprocess.run([BINARY, 'run', image], check=True, text=True, capture_output=True)
            self.assertEqual(program.stdout, '{"name":"Ada"}\n{"nickname":"Ada"}\n')

    def test_variant_rename_pins_its_wire_name(self):
        # docs/design/Records030.md, Tooling: a renamed variant reachable from
        # an unpinned derivation keeps its wire name through a new mapping.
        path = self.root / 'modes.nra'
        source = ('let json = import:Std.Encoding.Json;\n'
                  'let Mode = enum (Fast, Slow);\n'
                  'let ModeJson = derive json.Codec<Mode>;\n'
                  'let pick = func() -> Mode => Mode.Fast;\n')
        self.client.open(path, source)
        response = self.client.request('textDocument/rename', {**params(path, 1, 17), 'newName': 'Quick'})
        edits = response['documentChanges'][0]['edits']
        pinned = [edit['newText'] for edit in edits if edit.get('annotationId') == 'neura.preserveSchema']
        self.assertEqual(len(pinned), 1)
        self.assertIn('variant Mode.Quick: "Fast";', pinned[0])
        self.assertEqual(sorted(edit['newText'] for edit in edits if 'annotationId' not in edit), ['Quick', 'Quick'])

    def test_payload_field_rename_reaches_patterns_constructors_and_pins(self):
        # docs/design/Records030.md, Tooling: a named payload field renames
        # with its constructor labels, braced patterns and mapping paths, a
        # field of the same name in another variant stays, and an unpinned
        # derivation keeps the old wire name.
        path = self.root / 'payload.nra'
        source = ('let console = import:Std.Io.Console;\n'
                  'let json = import:Std.Encoding.Json;\n'
                  'let program = import:Std.Program;\n'
                  'let.promote _ = import:Std.Core.Result;\n'
                  'let Mode = enum (Fast, Slow { speed: int, limit: int }, Odd { speed: int });\n'
                  'let ModeJson = derive json.Codec<Mode>;\n'
                  'let Other = derive json.Codec<Mode> { field Mode.Slow.speed: "pace"; };\n'
                  'let describe = func(mode: Mode) -> int {\n'
                  '    return match (mode) {\n'
                  '        Mode.Slow { speed: s, limit: l } => s + l,\n'
                  '        Mode.Odd { speed: s } => s,\n'
                  '        _ => 0,\n'
                  '    };\n'
                  '};\n'
                  'let App = module:program.Entry (\n'
                  '    export let.override main = func(args: string[]) -> program.ExitCode {\n'
                  '        let slow = Mode.Slow { speed: 3, limit: 4 };\n'
                  '        console.print_line((describe(slow) + describe(Mode.Odd { speed: 9 })).to_string());\n'
                  '        match (ModeJson.encode(slow)) { Result.Ok(text) => { console.print_line(text); }, '
                  'Result.Err(_) => {}, }\n'
                  '        match (Other.encode(slow)) { Result.Ok(text) => { console.print_line(text); }, '
                  'Result.Err(_) => {}, }\n'
                  '        return program.ExitCode.Ok;\n'
                  '    };\n'
                  ');\n')
        self.client.open(path, source)
        slow_speed = [(4, 30), (6, 54), (9, 20), (16, 31)]
        for line, character in slow_speed:
            with self.subTest(at=(line, character)):
                rename = self.client.request('textDocument/rename',
                                             {**params(path, line, character), 'newName': 'rate'})
                edits = rename['documentChanges'][0]['edits']
                plain = sorted((edit['range']['start']['line'], edit['range']['start']['character'])
                               for edit in edits if 'annotationId' not in edit)
                self.assertEqual(plain, slow_speed)
                pinned = [edit['newText'] for edit in edits if edit.get('annotationId') == 'neura.preserveSchema']
                self.assertEqual(len(pinned), 1)
                self.assertIn('field Mode.Slow.rate: "speed";', pinned[0])
        lines = source.splitlines(keepends=True)
        for edit in sorted(edits, key=lambda e: (e['range']['start']['line'], e['range']['start']['character']),
                           reverse=True):
            start, end = edit['range']['start'], edit['range']['end']
            line = lines[start['line']]
            lines[start['line']] = line[:start['character']] + edit['newText'] + line[end['character']:]
        renamed = ''.join(lines)
        self.assertIn('Odd { speed: int }', renamed)
        self.assertIn('Mode.Odd { speed: s }', renamed)
        for snapshot in (source, renamed):
            path.write_text(snapshot)
            image = self.root / 'payload.nvr'
            subprocess.run([BINARY, 'build', path, '-o', image], check=True, capture_output=True)
            program = subprocess.run([BINARY, 'run', image], check=True, text=True, capture_output=True)
            self.assertEqual(program.stdout, '16\n{"variant":"Slow","payload":{"speed":3,"limit":4}}\n'
                                             '{"variant":"Slow","payload":{"pace":3,"limit":4}}\n')

    def test_record_field_rename_reaches_labels_and_copies(self):
        # docs/design/Records030.md, Tooling: the component, the constructor's
        # label, a copy's replacement name and a use rename together.
        path = self.root / 'users.nra'
        source = ('let User = record (id: int, name: string);\n'
                  'let make = func() -> User => new User(id: 1, name: "a");\n'
                  'let copy = func(u: User) -> User => new with(u) { name: u.name };\n')
        self.client.open(path, source)
        definition = self.client.request('textDocument/definition', params(path, 2, 51))
        self.assertEqual(definition['range']['start'], {'line': 0, 'character': 28})
        rename = self.client.request('textDocument/rename', {**params(path, 0, 29), 'newName': 'label'})
        starts = sorted((edit['range']['start']['line'], edit['range']['start']['character'])
                        for item in rename['documentChanges'] for edit in item['edits'])
        self.assertEqual(starts, [(0, 28), (1, 45), (2, 50), (2, 58)])

    def test_record_hover_and_copy_completion(self):
        # Hover lists a record's generated members; completion in a copy's
        # braces offers the fields not yet replaced, also in an unfinished
        # copy at the end of the buffer (edge case 57).
        path = self.root / 'records.nra'
        self.client.open(path, 'let User = record (id: int, name: string);\n'
                               'let copy = func(u: User) -> User => new with(u) { id: 2, };\n')
        hover = self.client.request('textDocument/hover', params(path, 0, 5))['contents']['value']
        self.assertIn('record User (id: int, name: string)', hover)
        self.assertIn('export let init = func(id: int, name: string) => set;', hover)
        self.assertIn('export let hash_code = func() -> uint32;', hover)
        items = self.client.request('textDocument/completion', params(path, 1, 57))['items']
        self.assertEqual([item['label'] for item in items], ['name:'])
        unfinished = self.root / 'unfinished.nra'
        self.client.open(unfinished, 'let User = record (id: int, name: string);\n'
                                     'let copy = func(u: User) -> User => new with(u) {')
        items = self.client.request('textDocument/completion', params(unfinished, 1, 50))['items']
        self.assertEqual([item['label'] for item in items], ['id:', 'name:'])
        reports = [n['params']['diagnostics'] for n in self.client.notifications
                   if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == unfinished.as_uri()]
        self.assertEqual([item['code'] for item in reports[-1]], ['E0203'])

    def test_unfinished_record_call_declares_nothing(self):
        # Edge case 142: `record (id` is an expression by the lookahead.
        path = self.root / 'partial.nra'
        self.client.open(path, 'let other = 1;\nlet P = record (id')
        response = self.client.request('textDocument/documentSymbol', {'textDocument': {'uri': path.as_uri()}})
        self.assertEqual([symbol['name'] for symbol in response], ['other'])
        reports = [n['params']['diagnostics'] for n in self.client.notifications
                   if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == path.as_uri()]
        self.assertEqual([item['code'] for item in reports[-1]], ['E0203'])

    def test_partial_record_at_the_end_keeps_its_components(self):
        # Edge case 141: one E0203, and the recovering parser keeps `P` with
        # the component read so far, so the outline and hover know it.
        path = self.root / 'partial-record.nra'
        self.client.open(path, 'let other = 1;\nlet P = record (id: int64,')
        response = self.client.request('textDocument/documentSymbol', {'textDocument': {'uri': path.as_uri()}})
        self.assertEqual([symbol['name'] for symbol in response], ['other', 'P'])
        hover = self.client.request('textDocument/hover', params(path, 1, 4))['contents']['value']
        self.assertIn('record P (id: int64)', hover)
        self.assertIn('export let init = func(id: int64) => set;', hover)
        reports = [n['params']['diagnostics'] for n in self.client.notifications
                   if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == path.as_uri()]
        self.assertEqual([item['code'] for item in reports[-1]], ['E0203'])
        path.write_text('let other = 1;\nlet P = record (id: int64,')
        result = subprocess.run([BINARY, 'check', path], capture_output=True, text=True)
        self.assertEqual(result.stderr.count('error['), 1, result.stderr)
        self.assertIn('E0203', result.stderr)

    def test_unfinished_property_clause_keeps_the_interface(self):
        # Edge case 143: one E0202; the interface keeps its other members,
        # and completion after the '|' offers the bodyless clauses.
        path = self.root / 'partial-interface.nra'
        self.client.open(path, 'let before = 1;\nlet Named = interface (\n    let other = func() -> int;\n'
                               '    let name: string;\n        |')
        response = self.client.request('textDocument/documentSymbol', {'textDocument': {'uri': path.as_uri()}})
        self.assertEqual([symbol['name'] for symbol in response], ['before', 'Named'])
        self.assertIn('other', [child['name'] for child in response[1].get('children', [])])
        items = self.client.request('textDocument/completion', params(path, 4, 9))['items']
        self.assertEqual([item['label'] for item in items], ['get;', 'set;'])
        reports = [n['params']['diagnostics'] for n in self.client.notifications
                   if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == path.as_uri()]
        self.assertEqual([item['code'] for item in reports[-1]], ['E0202'])
        # In a class the same '|' still offers the clause words of a hook or
        # accessor.
        other = self.root / 'partial-class.nra'
        self.client.open(other, 'let Box = class (\n    export let label: string = "";\n        |')
        items = self.client.request('textDocument/completion', params(other, 2, 9))['items']
        self.assertEqual([item['label'] for item in items], ['get', 'set'])

    def test_unfinished_member_inside_a_module(self):
        # Edge cases 141 and 143 one level down: the module around the
        # unfinished record or interface keeps it, with one error.
        for name, source, code, path_names in (
                ('nested-record.nra', 'let before = 1;\nlet M = module (\n    export let P = record (id: int64,',
                 'E0203', ['M', 'P']),
                ('nested-interface.nra', 'let before = 1;\nlet M = module (\n    export let Named = interface (\n'
                 '        let other = func() -> int;\n        let name: string;\n            |', 'E0202',
                 ['M', 'Named', 'other'])):
            with self.subTest(name=name):
                path = self.root / name
                self.client.open(path, source)
                symbols = self.client.request('textDocument/documentSymbol',
                                              {'textDocument': {'uri': path.as_uri()}})
                self.assertEqual([symbol['name'] for symbol in symbols], ['before', 'M'])
                found, level = [], symbols[1]
                while level is not None:
                    found.append(level['name'])
                    children = [child for child in level.get('children', []) if child['name'] in path_names]
                    level = children[0] if children else None
                self.assertEqual(found, path_names)
                reports = [n['params']['diagnostics'] for n in self.client.notifications
                           if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == path.as_uri()]
                self.assertEqual([item['code'] for item in reports[-1]], [code])

    def test_property_rename_reaches_requirement_fields_and_overrides(self):
        # docs/design/Records030.md, Tooling: a property is one name across
        # its interface requirement, the field that satisfies it, the
        # `let.override` of that field and every use, from any of them.
        path = self.root / 'property.nra'
        source = ('let Named = interface (\n'
                  '    let name: string;\n'
                  '        | get;\n'
                  ');\n'
                  'let Shape = class implements Named (\n'
                  '    export let name: string = "shape";\n'
                  '        | get.virtual: name;\n'
                  ');\n'
                  'let Circle = class: Shape (\n'
                  '    export let.override name: string;\n'
                  '        | get.override: "circle of " + name;\n'
                  ');\n'
                  'let read = func(n: Named) -> string => n.name;\n')
        self.client.open(path, source)
        places = [(1, 8), (5, 15), (6, 23), (9, 24), (10, 39), (12, 41)]
        for line, character in places:
            with self.subTest(at=(line, character)):
                rename = self.client.request('textDocument/rename',
                                             {**params(path, line, character), 'newName': 'title'})
                edits = [edit for item in rename['documentChanges'] for edit in item['edits']]
                self.assertEqual(sorted((edit['range']['start']['line'], edit['range']['start']['character'])
                                        for edit in edits), places)
                self.assertTrue(all(edit['newText'] == 'title' for edit in edits))
        # Go to definition on the override and in its body reaches the field.
        for line, character in ((9, 24), (10, 39)):
            definition = self.client.request('textDocument/definition', params(path, line, character))
            self.assertEqual(definition['range']['start'], {'line': 5, 'character': 15})
        renamed = source.replace('name', 'title')
        path.write_text(renamed)
        result = subprocess.run([BINARY, 'check', path], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_record_completion_paths(self):
        # docs/design/Records030.md, Language server: `let.override` offers
        # the base's virtual properties, `T.` the static requirements of T's
        # bounds, and a derivation's mapping paths the fields, variants and
        # named payload fields of the shapes.
        path = self.root / 'paths.nra'
        cases = [
            ('let Shape = class (\n    export let label: string = "s";\n        | get.virtual: label;\n'
             '    export let plain: int = 1;\n);\nlet Circle = class: Shape (\n    export let.override ',
             '\n);\n', {'label'}, {'plain'}),
            ('let Additive = interface<T> (\n    static let zero = func() -> T;\n'
             '    static let add = func(a: T, b: T) -> T;\n    let other = func() -> int;\n);\n'
             'let sum = func<T: Additive<T>>(items: T[]) -> T {\n    let total = T.',
             '\n};\n', {'zero', 'add'}, {'other'}),
            ('let json = import:Std.Encoding.Json;\nlet User = record (id: int, name: string);\n'
             'let UserJson = derive json.Codec<User> {\n    field User.', '\n};\n', {'id', 'name'}, set()),
            ('let json = import:Std.Encoding.Json;\nlet Mode = enum (Fast, Slow { speed: int }, Odd(int));\n'
             'let ModeJson = derive json.Codec<Mode> {\n    variant Mode.', '\n};\n', {'Fast', 'Slow', 'Odd'},
             set()),
            ('let json = import:Std.Encoding.Json;\nlet Mode = enum (Fast, Slow { speed: int }, Odd(int));\n'
             'let ModeJson = derive json.Codec<Mode> {\n    field Mode.', '\n};\n', {'Slow'}, {'Fast', 'Odd'}),
            ('let json = import:Std.Encoding.Json;\nlet Mode = enum (Fast, Slow { speed: int }, Odd(int));\n'
             'let ModeJson = derive json.Codec<Mode> {\n    field Mode.Slow.', '\n};\n', {'speed'}, set()),
        ]
        for source, closing, expected, absent in cases:
            with self.subTest(source=source.splitlines()[-1]):
                self.client.open(path, source + closing)
                line = source.count('\n')
                character = len(source.rsplit('\n', 1)[-1])
                labels = {item['label'] for item in
                          self.client.request('textDocument/completion', params(path, line, character))['items']}
                self.assertTrue(expected <= labels, labels)
                self.assertFalse(absent & labels, labels)

    def test_generator_and_using_bindings_in_the_editor(self):
        # docs/design/Lifetime030.md, Tooling: hover, rename, signature help
        # and the outline treat a generator as the function it is written
        # as, and a `using` binding as a local; the generated state class
        # and its members are not shown.
        path = self.root / 'lifetime.nra'
        source = ('let console = import:Std.Io.Console;\n'
                  'let closeable = import:Std.Io.Closeable;\n'
                  'let.promote _ = import:Std.Core.Result;\n'
                  'let.promote _ = import:Std.Core.Unit;\n'
                  'let Resource = class implements closeable.Closeable (\n'
                  '    export let close = func -> Result<Unit> => Result<Unit>.Ok(Unit.Value);\n'
                  ');\n'
                  'let squares = iterator(count: int) -> int {\n'
                  '    for (let index = 0; index < count; index++) {\n'
                  '        let square = index * index;\n'
                  '        yield square;\n'
                  '    }\n'
                  '};\n'
                  'let used = func() -> Result<int> {\n'
                  '    using (source = new Resource()) {\n'
                  '        let again = source;\n'
                  '    }\n'
                  '    return Result<int>.Ok(1);\n'
                  '};\n'
                  'let first = func() -> int => squares(3).iterator().next();\n')
        self.client.open(path, source)
        hover = lambda line, character: self.client.request('textDocument/hover',
                                                            params(path, line, character))['contents']['value']
        self.assertIn('squares(count: int32) -> Sequence<int32>', hover(7, 5))
        self.assertIn('squares(count: int32) -> Sequence<int32>', hover(19, 30))
        self.assertIn('square: int32', hover(10, 15))
        self.assertIn('source: Resource', hover(14, 12))
        self.assertIn('source: Resource', hover(15, 22))
        for (line, character), expected in (((9, 13), [(9, 12), (10, 14)]), ((14, 12), [(14, 11), (15, 20)]),
                                            ((7, 5), [(7, 4), (19, 29)])):
            rename = self.client.request('textDocument/rename', {**params(path, line, character), 'newName': 'other'})
            self.assertEqual(sorted((edit['range']['start']['line'], edit['range']['start']['character'])
                                    for item in rename['documentChanges'] for edit in item['edits']), expected)
        signature = self.client.request('textDocument/signatureHelp', params(path, 19, 37))
        self.assertEqual(signature['signatures'][0]['label'], 'squares(count: int32) -> Sequence<int32>')
        symbols = self.client.request('textDocument/documentSymbol', {'textDocument': {'uri': path.as_uri()}})
        names = []
        def walk(items):
            for item in items:
                names.append(item['name'])
                walk(item.get('children', []))
        walk(symbols)
        self.assertIn('squares', names)
        self.assertIn('source', names)
        self.assertFalse({'has_next', 'next', 'init'} & set(names), names)
        self.assertFalse([name for name in names if name.startswith('$')], names)

    def test_command_line_rename(self):
        # docs/lang/Tooling.md, `neura rename`: the language server's rename
        # applied to the files on disk; it inserts the mapping that keeps a
        # derived wire name and says so, and refuses what the editor refuses.
        path = self.root / 'cli-model.nra'
        source = ('let json = import:Std.Encoding.Json;\n'
                  'let User = record (name: string);\n'
                  'let UserJson = derive json.Codec<User>;\n'
                  'let greet = func(u: User) -> string => u.name;\n')
        path.write_text(source)
        result = subprocess.run([BINARY, 'rename', path, '2:20', 'display'], capture_output=True, text=True,
                                cwd=self.root)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('field User.display: "name";', result.stderr)
        self.assertIn('so that the JSON wire name does not change', result.stderr)
        self.assertIn('applied 3 edits to 1 file', result.stderr)
        renamed = path.read_text()
        self.assertIn('let User = record (display: string);', renamed)
        self.assertIn('derive json.Codec<User> { field User.display: "name"; };', renamed)
        self.assertIn('=> u.display;', renamed)
        self.assertEqual(subprocess.run([BINARY, 'check', path], capture_output=True).returncode, 0)
        for arguments, message in ((['2:20', '9bad'], 'must be a Neura identifier'),
                                   (['1:1', 'other'], 'No resolved symbol'),
                                   (['9:1', 'other'], 'past the end'),
                                   (['2', 'other'], 'usage: neura rename')):
            with self.subTest(arguments=arguments):
                result = subprocess.run([BINARY, 'rename', path, *arguments], capture_output=True, text=True,
                                        cwd=self.root)
                self.assertEqual(result.returncode, 1)
                self.assertIn(message, result.stderr)
                self.assertEqual(path.read_text(), renamed)

    def test_command_line_rename_across_files_and_modules(self):
        # `neura rename` reads the files under the current directory, so a
        # caller in a subdirectory is renamed with the declaration, and a
        # module whose file follows its name moves with it, unless the new
        # file name is taken.
        project = self.root / 'project'
        (project / 'sub').mkdir(parents=True)
        (project / 'lib.nra').write_text('export let Lib = module (\n'
                                         '    export let plus = func(a: int, b: int) -> int => a + b;\n);\n')
        (project / 'app.nra').write_text('let lib = import:Lib;\nlet use = func() -> int => lib.plus(1, 2);\n')
        (project / 'sub' / 'more.nra').write_text('let lib = import:Lib;\n'
                                                  'let again = func() -> int => lib.plus(3, 4);\n')
        rename = lambda *arguments: subprocess.run([BINARY, 'rename', *arguments], capture_output=True, text=True,
                                                   cwd=project)
        result = rename('app.nra', '2:32', 'add')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('applied 3 edits to 3 files', result.stderr)
        self.assertIn('export let add = func', (project / 'lib.nra').read_text())
        self.assertIn('lib.add(1, 2)', (project / 'app.nra').read_text())
        self.assertIn('lib.add(3, 4)', (project / 'sub' / 'more.nra').read_text())
        result = rename('lib.nra', '1:12', 'Tools')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('moved', result.stderr)
        self.assertFalse((project / 'lib.nra').exists())
        self.assertIn('export let Tools = module', (project / 'tools.nra').read_text())
        self.assertIn('import:Tools;', (project / 'sub' / 'more.nra').read_text())
        for source in ('app.nra', 'sub/more.nra'):
            check = subprocess.run([BINARY, 'check', '-I', '.', source], capture_output=True, text=True, cwd=project)
            self.assertEqual(check.returncode, 0, check.stderr)
        (project / 'other.nra').write_text('let unrelated = 1;\n')
        before = {item: item.read_text() for item in project.rglob('*.nra')}
        result = rename('tools.nra', '1:12', 'Other')
        self.assertEqual(result.returncode, 1)
        self.assertIn('already exists', result.stderr)
        self.assertEqual({item: item.read_text() for item in project.rglob('*.nra')}, before)

    def test_accessor_dispatch_word_completion(self):
        # Edge case 144: after `| get.`, `override:` under `let.override`
        # and `virtual:` under an ordinary field.
        base = ('let Shape = class (\n    export let label: string = "s";\n        | get.virtual: label;\n);\n'
                'let Circle = class: Shape (\n    export let.override label: string;\n')
        for name, source, line, expected in (
                ('override.nra', base + '        | get.', 6, 'override:'),
                ('second.nra', base + '        | get.override: "c";\n        | set.', 7, 'override:'),
                ('virtual.nra', 'let Shape = class (\n    export let label: string = "s";\n        | get.', 2,
                 'virtual:')):
            with self.subTest(name=name):
                path = self.root / name
                self.client.open(path, source)
                items = self.client.request('textDocument/completion', params(path, line, 14))['items']
                self.assertEqual([item['label'] for item in items], [expected])
                reports = [n['params']['diagnostics'] for n in self.client.notifications
                           if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == path.as_uri()]
                self.assertEqual([item['code'] for item in reports[-1]], ['E0202'])

    def test_incomplete_json_derivation_keeps_previous_symbols(self):
        path = self.root / 'model.nra'
        self.client.open(path, 'let visible = 1;\nlet Incomplete = derive')
        response = self.client.request('textDocument/documentSymbol', {'textDocument': {'uri': path.as_uri()}})
        self.assertIn('visible', [symbol['name'] for symbol in response])
        reports = [n['params']['diagnostics'] for n in self.client.notifications
                   if n['method'] == 'textDocument/publishDiagnostics' and n['params']['uri'] == path.as_uri()]
        self.assertEqual([item['code'] for item in reports[-1]], ['E0202'])

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
