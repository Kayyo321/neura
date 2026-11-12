#!/usr/bin/env python3
"""Compiler-backed refactoring and hierarchy contracts against a real LSP."""

import pathlib
import tempfile
import unittest

from lsp_protocol import Client, params


def position(source, offset):
    prefix = source[:offset]
    return {'line': prefix.count('\n'), 'character': len(prefix.rsplit('\n', 1)[-1])}


def offset(source, point):
    lines = source.splitlines(keepends=True)
    return sum(map(len, lines[:point['line']])) + point['character']


def apply(source, action):
    changes = action['edit']['documentChanges']
    assert len(changes) == 1
    edits = changes[0]['edits']
    for edit in sorted(edits, key=lambda item: offset(source, item['range']['start']), reverse=True):
        start = offset(source, edit['range']['start'])
        end = offset(source, edit['range']['end'])
        source = source[:start] + edit['newText'] + source[end:]
    return source


class RefactorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-refactor-')
        self.root = pathlib.Path(self.directory.name)
        self.client = Client(self.root)
        self.path = self.root / 'main.nra'

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def actions(self, source, selected, kind, *, occurrence=0):
        self.client.open(self.path, source)
        start = -1
        for _ in range(occurrence + 1):
            start = source.index(selected, start + 1)
        result = self.client.request('textDocument/codeAction', {
            'textDocument': {'uri': self.path.as_uri()},
            'range': {'start': position(source, start), 'end': position(source, start + len(selected))},
            'context': {'diagnostics': [], 'only': [kind]}})
        return [item for item in result if item['kind'] == kind or item['kind'].startswith(kind + '.')]

    def assert_compiles_after(self, source, action):
        updated = apply(source, action)
        self.client.notify('textDocument/didChange', {
            'textDocument': {'uri': self.path.as_uri(), 'version': 2},
            'contentChanges': [{'text': updated}]})
        self.client.request('textDocument/documentSymbol', params(self.path))
        reports = [message['params']['diagnostics'] for message in self.client.notifications
                   if message.get('method') == 'textDocument/publishDiagnostics'
                   and message.get('params', {}).get('uri') == self.path.as_uri()]
        self.assertFalse([item for item in reports[-1] if item.get('severity') == 1], updated)
        return updated

    def test_extract_whole_return_expression(self):
        source = 'let compute = func(left: int, right: int) -> int {\n    return left + right;\n};\n'
        actions = self.actions(source, 'left + right', 'refactor.extract.variable')
        self.assertEqual(len(actions), 1)
        updated = self.assert_compiles_after(source, actions[0])
        self.assertIn('last extracted_value: int = left + right;', updated)
        self.assertIn('return extracted_value;', updated)

    def test_extract_local_function(self):
        source = 'let compute = func(left: int) -> int {\n    return left * 2;\n};\n'
        actions = self.actions(source, 'left * 2', 'refactor.extract.function')
        self.assertEqual(len(actions), 1)
        updated = self.assert_compiles_after(source, actions[0])
        self.assertIn('extracted_function = func() -> int', updated)
        self.assertIn('return extracted_function();', updated)

    def test_no_extraction_that_reorders_a_subexpression(self):
        source = 'let compute = func(left: int, right: int) -> int { return left + right; };'
        self.assertEqual(self.actions(source, 'right', 'refactor.extract.variable', occurrence=1), [])

    def test_inline_literal_retains_explicit_type(self):
        source = 'let compute = func -> int64 {\n    last value: int64 = 42;\n    return value;\n};\n'
        actions = self.actions(source, 'value', 'refactor.inline')
        self.assertEqual(len(actions), 1)
        updated = self.assert_compiles_after(source, actions[0])
        self.assertIn('(42 as int64)', updated)
        self.assertNotIn('last value', updated)

    def test_inline_rejects_effectful_initializer(self):
        source = 'let other = func -> int => 1;\nlet compute = func -> int { last value = other(); return value; };'
        self.assertEqual(self.actions(source, 'value', 'refactor.inline'), [])

    def test_accessor_generation(self):
        source = 'let Counter = class (\n    export let count: int = 0;\n);\n'
        actions = self.actions(source, 'count', 'refactor.rewrite')
        action = next(item for item in actions if item['title'] == 'Generate getter and setter')
        updated = self.assert_compiles_after(source, action)
        self.assertIn('| get: count;', updated)
        self.assertIn('| set: count = value;', updated)

    def test_constructor_generation_avoids_field_parameter_shadowing(self):
        source = 'let Point = class (\n    export last x: int;\n    export last initial_x: int;\n);\n'
        actions = self.actions(source, 'Point', 'refactor.rewrite')
        action = next(item for item in actions if item['title'].startswith('Generate constructor'))
        updated = self.assert_compiles_after(source, action)
        self.assertIn('x = _initial_x;', updated)
        self.assertIn('initial_x = initial_initial_x;', updated)

    def test_implement_missing_interface(self):
        source = ('let Reader = interface (\n    export let read = func() -> int;\n);\n'
                  'let Memory = class implements Reader (\n);\n')
        actions = self.actions(source, 'Memory', 'refactor.rewrite')
        action = next(item for item in actions if item['title'].startswith('Implement interface'))
        updated = self.assert_compiles_after(source, action)
        self.assertIn('export let read = func() -> int', updated)

    def test_generate_override_without_unsupported_super_calls(self):
        source = ('let Base = class (export let read = func() -> int => 7;);\n'
                  'let Child = class:Base ();\n')
        actions = self.actions(source, 'Child', 'refactor.rewrite')
        action = next(item for item in actions if item['title'].startswith('Implement interface'))
        updated = self.assert_compiles_after(source, action)
        self.assertIn('export let.override read', updated)
        self.assertIn('TODO: implement read', updated)
        self.assertNotIn('super.read', updated)

    def test_value_field_never_gets_invalid_setter(self):
        source = 'let Counter = class (export let value: int = 0;);'
        actions = self.actions(source, 'value', 'refactor.rewrite')
        self.assertFalse(any(item['title'] == 'Generate getter and setter' for item in actions))
        self.assertTrue(any(item['title'] == 'Generate read-only getter' for item in actions))

    def test_exhaustive_enum_statement(self):
        source = ('let Choice = enum (One, Two(int), Three { name: string });\n'
                  'let handle = func(choice: Choice) {\n'
                  '    match (choice) { Choice.One => { return; } }\n};\n')
        actions = self.actions(source, 'match', 'quickfix')
        action = next(item for item in actions if item['title'] == 'Add missing enum match arms')
        updated = self.assert_compiles_after(source, action)
        self.assertIn('Choice.Two(_)', updated)
        self.assertIn('Choice.Three { name: _ }', updated)

    def test_organize_inert_imports(self):
        (self.root / 'alpha.nra').write_text('let Alpha = module (export let value = 1;);')
        (self.root / 'beta.nra').write_text('let Beta = module (export let value = 2;);')
        source = ('let beta = import:Beta;\nlet alpha = import:Alpha;\n'
                  'let sum = func -> int => alpha.value + beta.value;\n')
        actions = self.actions(source, 'beta', 'source.organizeImports')
        self.assertEqual(len(actions), 1)
        updated = self.assert_compiles_after(source, actions[0])
        self.assertTrue(updated.startswith('let alpha = import:Alpha;\nlet beta = import:Beta;'))

    def test_organize_rejects_executable_initializers(self):
        (self.root / 'alpha.nra').write_text('let Alpha = module (export let value = 1;);')
        (self.root / 'beta.nra').write_text('let Beta = module (export let make = func -> int => 2; export let value = make(););')
        source = ('let beta = import:Beta;\nlet alpha = import:Alpha;\n'
                  'let sum = func -> int => alpha.value + beta.value;\n')
        self.assertEqual(self.actions(source, 'beta', 'source.organizeImports'), [])

    def test_resolved_call_hierarchy(self):
        source = 'let leaf = func -> int => 1;\nlet caller = func -> int => leaf();\n'
        self.client.open(self.path, source)
        prepared = self.client.request('textDocument/prepareCallHierarchy', params(self.path, 1, 5))
        self.assertEqual(prepared[0]['name'], 'caller')
        outgoing = self.client.request('callHierarchy/outgoingCalls', {'item': prepared[0]})
        self.assertEqual([item['to']['name'] for item in outgoing], ['leaf'])
        incoming = self.client.request('callHierarchy/incomingCalls', {'item': outgoing[0]['to']})
        self.assertEqual([item['from']['name'] for item in incoming], ['caller'])
        self.assertEqual(incoming[0]['fromRanges'][0]['start']['line'], 1)

    def test_resolved_type_hierarchy(self):
        source = 'let Base = class ();\nlet Child = class:Base ();\n'
        self.client.open(self.path, source)
        prepared = self.client.request('textDocument/prepareTypeHierarchy', params(self.path, 1, 5))
        self.assertEqual(prepared[0]['name'], 'Child')
        supers = self.client.request('typeHierarchy/supertypes', {'item': prepared[0]})
        self.assertEqual([item['name'] for item in supers], ['Base'])
        subs = self.client.request('typeHierarchy/subtypes', {'item': supers[0]})
        self.assertEqual([item['name'] for item in subs], ['Child'])

    def test_incoming_calls_and_subtypes_cross_workspace_files(self):
        module = self.root / 'library.nra'
        module.write_text('let Library = module (export let Base = class (); export let leaf = func -> int => 1;);')
        source = ('let library = import:Library;\n'
                  'let Child = class:library.Base ();\n'
                  'let caller = func -> int => library.leaf();\n')
        self.client.open(self.path, source)
        outgoing = self.client.request('textDocument/prepareCallHierarchy', params(self.path, 2, 5))
        leaf = self.client.request('callHierarchy/outgoingCalls', {'item': outgoing[0]})[0]['to']
        incoming = self.client.request('callHierarchy/incomingCalls', {'item': leaf})
        self.assertIn('caller', [item['from']['name'] for item in incoming])
        child = self.client.request('textDocument/prepareTypeHierarchy', params(self.path, 1, 5))[0]
        base = self.client.request('typeHierarchy/supertypes', {'item': child})[0]
        subtypes = self.client.request('typeHierarchy/subtypes', {'item': base})
        self.assertIn('Child', [item['name'] for item in subtypes])


if __name__ == '__main__':
    unittest.main()
