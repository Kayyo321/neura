#!/usr/bin/env python3
"""Editor requests must work while a document is incomplete."""
import pathlib
import subprocess
import tempfile
import unittest

from lsp_protocol import BINARY, Client, params


class EditorRegressionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-editor-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client(self.root)
        self.sequence = 0

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def complete(self, marked):
        source, after = marked.split('<cursor>')
        self.sequence += 1
        path = self.root / f'edit-{self.sequence}.nra'
        self.client.open(path, source + after)
        logical_source = source.replace('\r\n', '\n').replace('\r', '\n')
        line = logical_source.count('\n')
        column = len(logical_source.rsplit('\n', 1)[-1].encode('utf-16-le')) // 2
        return self.client.request('textDocument/completion', params(path, line, column))['items']

    def labels(self, marked):
        return {item['label'] for item in self.complete(marked)}

    def test_declaration_modifiers_are_contextual(self):
        self.assertEqual(self.labels('let.<cursor>'), {'promote'})
        self.assertEqual(self.labels('let Base = module ();\nlet App = module:Base (\n let.<cursor>\n);'),
                         {'promote', 'override'})
        self.assertEqual(self.labels('let Base = class ();\nlet Child = class:Base (\n let.<cursor>\n);'),
                         {'as', 'override'})
        self.assertEqual(self.labels('let Base = class ();\nlet Child = class:Base (\n const.<cursor>\n);'),
                         {'override'})
        self.assertEqual(self.labels('let work = func {\n let.<cursor>\n};'), set())
        item = next(item for item in self.complete('let Base = module ();\nlet App = module:Base (let.ov<cursor>);')
                    if item['label'] == 'override')
        self.assertEqual(item['textEdit']['newText'], 'override ')
        self.assertEqual(item['command']['command'], 'editor.action.triggerSuggest')

    def test_std_import_paths_without_successful_lowering(self):
        self.assertIn('Std', self.labels('let io = import:<cursor>'))
        self.assertTrue({'Io', 'Core', 'Program'} <= self.labels('let io = import:Std.<cursor>'))
        self.assertIn('Console', self.labels('let io = import:Std.Io.<cursor>'))
        self.assertEqual(self.labels('let io = import:Std.Io.Con<cursor>'), {'Console'})
        self.assertNotIn('Internal', self.labels('let io = import:Std.Core.<cursor>'))

    def test_unsaved_project_module_import_completion(self):
        self.client.open(self.root / 'tools.nra', 'export let Tools = module ();')
        self.assertIn('Tools', self.labels('let tools = import:Too<cursor>'))

    def test_module_override_candidates_include_inherited_data(self):
        source = ('let Shared = class ();\n'
                  'let Grand = module (export let inherited = func -> int => 1;);\n'
                  'let Base = module:Grand (export let needed = func -> int; export let setting = 1; '
                  'export let Thing = class (); export let View = interface (); '
                  'export let Mode = enum (One); export let Nested = module (); export let Alias = Shared; '
                  'export last last_value: int = 1; export const Limit = 2; '
                  'let private = 2;);\n'
                  'let App = module:Base (\n export let.override inherited = func -> int => 2;\n'
                  ' export let.override <cursor>\n);')
        self.assertEqual(self.labels(source), {'needed', 'setting', 'Alias', 'last_value', 'Limit'})
        # Check the actual language boundary as well as the suggestion list:
        # fresh nominal definitions fail, while an alias preserves identity.
        path = self.root / 'override-check.nra'
        for definition in ['class ()', 'interface ()', 'enum (One)', 'module ()']:
            with self.subTest(definition=definition):
                path.write_text(f'let Base = module (export let Thing = {definition};); '
                                f'let Child = module:Base (export let.override Thing = {definition};);')
                result = subprocess.run([str(BINARY), 'check', str(path), '--error-format=short'],
                                        capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('E0502', result.stderr)
        path.write_text('let Shared = class (); let Base = module (export let Alias = Shared; '
                        'export last last_value: int = 1; export const Limit = 2;); '
                        'let Child = module:Base (export let.override Alias = Shared; '
                        'export let.override last_value = 2; export const.override Limit = 3;);')
        result = subprocess.run([str(BINARY), 'check', str(path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_class_override_candidates_filter_forbidden_members(self):
        source = ('let View = interface (let required = func -> int;);\n'
                  'let Base = class (\n export let open = func -> int => 1;\n'
                  ' #Final\n export let closed = func -> int => 1;\n'
                  ' export let field = 1;\n export let init = func {};\n'
                  ' export static let utility = func -> int => 1;\n'
                  ' export let generic = func<T>(value: T) -> T => value;\n'
                  ' export let overloaded = func(value: int) -> int => value or func(value: string) -> int => 1;\n'
                  ');\nlet Child = class:Base implements View (\n export let.override <cursor>\n);')
        self.assertEqual(self.labels(source), {'open', 'required', 'equals', 'hash_code', 'to_string'})

    def test_imported_entry_override_and_nested_member_completion(self):
        self.assertEqual(self.labels('let program = import:Std.Program;\n'
                                     'let App = module:program.Entry (export let.override <cursor>);'), {'main'})
        self.assertIn('Ok', self.labels('let program = import:Std.Program;\nlet code = program.ExitCode.<cursor>;'))
        self.assertIn('print_line', self.labels('let console = import:Std.Io.Console;\nconsole.<cursor>'))

    def test_member_visibility_and_inheritance_during_edits(self):
        source = ('let Base = module (export let inherited = 1; let hidden = 2;);\n'
                  'let Child = module:Base (export let own = 3; let secret = 4;);\nChild.<cursor>')
        self.assertEqual(self.labels(source), {'inherited', 'own'})
        self.assertEqual(self.labels('let M = module (let hidden = 1; M.<cursor>);'), {'hidden'})

    def test_promoted_exports_are_unqualified_and_shadowed_names_win(self):
        (self.root / 'tools.nra').write_text('let Tools = module (export let provided = 1; let hidden = 2;);')
        self.assertIn('provided', self.labels('let.promote _ = import:Tools;\nlet copy = prov<cursor>;'))
        self.assertNotIn('hidden', self.labels('let.promote _ = import:Tools;\nlet copy = hid<cursor>;'))
        items = self.complete('let.promote _ = import:Tools;\nlet provided = "local";\nlet copy = prov<cursor>;')
        self.assertEqual(len([item for item in items if item['label'] == 'provided']), 1)
        (self.root / 'other.nra').write_text('let Other = module (export let provided = 2;);')
        self.assertNotIn('provided', self.labels('let.promote _ = import:Tools;\nlet.promote _ = import:Other;\n'
                                                'let copy = prov<cursor>;'))

    def test_documentation_commands_are_rendered_in_hover_and_completion(self):
        path = self.root / 'docs.nra'
        source = ('/// @brief Doubles a number.\n/// @param[in] value The input.\n'
                  '/// @return Twice the input.\nlet twice = func(value: int) -> int => value * 2;\n'
                  'let answer = twice(2);\n')
        self.client.open(path, source)
        hover = self.client.request('textDocument/hover', params(path, 4, 15))['contents']['value']
        self.assertNotIn('@brief', hover)
        self.assertNotIn('@return', hover)
        self.assertIn('Doubles a number.', hover)
        self.assertIn('**Returns**', hover)
        documentation = self.client.request('neura/declarationDocumentation', params(path, 3, 5))
        self.assertIn('@brief', documentation['documentation']['text'])
        self.assertNotIn('@brief', next(item['documentation']['value'] for item in
                         self.client.request('textDocument/completion', params(path, 4, 18))['items']
                         if item['label'] == 'twice'))

    def test_outlines_never_expose_unnamed_symbols_or_invalid_ranges(self):
        path = self.root / 'anonymous.nra'
        source = ('let make = func -> func(int) -> int {\n'
                  ' return func(value: int) -> int => value;\n};\n'
                  'let id = func<T>(value: T) -> T => value;\n')
        self.client.open(path, source)
        def check(items):
            for item in items:
                self.assertTrue(item['name'])
                pos = lambda p: (p['line'], p['character'])
                self.assertLessEqual(pos(item['range']['start']), pos(item['selectionRange']['start']))
                self.assertLessEqual(pos(item['selectionRange']['end']), pos(item['range']['end']))
                check(item.get('children', []))
        check(self.client.request('textDocument/documentSymbol', params(path)))
        for version, text in enumerate([source[:-2], 'let App = module (export let.\n);', source], 2):
            self.client.notify('textDocument/didChange', {'textDocument': {'uri': path.as_uri(), 'version': version},
                               'contentChanges': [{'text': text}]})
            check(self.client.request('textDocument/documentSymbol', params(path)))

    def test_parameter_instance_and_class_static_members(self):
        declaration = ('let Point = class (export let value: int = 1; export let method = func {}; '
                       'export static let origin = func -> Point => new;);\n')
        self.assertEqual(self.labels(declaration + 'let work = func(point: Point) { point.<cursor> };'),
                         {'value', 'method', 'origin', 'equals', 'hash_code', 'to_string'})
        self.assertEqual(self.labels(declaration + 'Point.<cursor>'), {'origin'})
        self.assertEqual(self.labels(declaration + 'let point = new Point();\npoint.<cursor>'),
                         {'value', 'method', 'origin', 'equals', 'hash_code', 'to_string'})

    def test_wildcard_promotion_and_inner_scope_precedence(self):
        tools = self.root / 'tools'
        tools.mkdir()
        (tools / 'one.nra').write_text('let One = module (export let useful = 1;);')
        (self.root / 'outer.nra').write_text('let Outer = module (export let useful = "outer";);')
        items = self.complete('let.promote _ = import:Tools.*;\nlet copy = use<cursor>;')
        self.assertEqual([item['label'] for item in items], ['useful'])
        items = self.complete('let.promote _ = import:Outer;\nlet Inner = module ('
                              'let.promote _ = import:Tools.One; let copy = use<cursor>;);')
        self.assertEqual([item['label'] for item in items], ['useful'])
        self.assertIn('Zero', self.labels('let.promote _ = import:Std.Time.*;\nlet copy = Z<cursor>;'))

    def test_anonymous_class_parameter_members(self):
        self.assertTrue({'x', 'y'} <= self.labels('let work = func(point: class (export let x: int; '
                                                'export let y: int;)) { point.<cursor> };'))

    def test_interface_statics_are_only_available_on_declaring_interface(self):
        declaration = ('let I = interface (static let utility = func -> int => 1; let required = func -> int;);\n'
                       'let C = class implements I (export let.override required = func -> int => 1;);\n')
        self.assertEqual(self.labels(declaration + 'I.<cursor>'), {'utility'})
        self.assertEqual(self.labels(declaration + 'C.<cursor>'), set())
        self.assertEqual(self.labels(declaration + 'let work = func(item: I) { item.<cursor> };'), {'required'})

    def test_primitive_and_collection_members_during_edits(self):
        for name in ['int8', 'int16', 'int32', 'int64', 'uint8', 'uint16', 'uint32', 'uint64', 'int', 'byte']:
            labels = self.labels(f'let work = func(value: {name}) {{ value.<cursor> }};')
            self.assertTrue({'equals', 'hash_code', 'to_string', 'to_string_radix'} <= labels, (name, labels))
            self.assertNotIn('parse', labels)
            self.assertNotIn('Max', labels)
        for name in ['float16', 'float32', 'float64', 'bfloat16', 'float']:
            self.assertTrue({'to_bits', 'is_nan', 'to_string'} <=
                            self.labels(f'let work = func(value: {name}) {{ value.<cursor> }};'))
        self.assertTrue({'abs', 'parse'} <= self.labels('int32.<cursor>'))
        self.assertNotIn('Max', self.labels('int32.<cursor>'))
        self.assertIn('abs', self.labels('let count = 1;\ncount.<cursor>'))
        self.assertTrue({'byte_length', 'byte_at', 'slice', 'to_upper_case'} <=
                        self.labels('let work = func(text: string) { text.<cursor> };'))
        array = self.labels('let work = func(items: int[]) { items.<cursor> };')
        self.assertEqual(array, {'length', 'equals', 'hash_code', 'to_string'})
        listing = self.complete('let work = func(items: int[list]) { items.<cursor> };')
        self.assertEqual({item['label'] for item in listing},
                         {'length', 'version', 'append', 'remove_at', 'equals', 'hash_code', 'to_string'})
        self.assertEqual(next(item['detail'] for item in listing if item['label'] == 'remove_at'),
                         'remove_at(index: int32) -> void')
        functions = self.labels('let work = func(items: func() -> int[]) { items.<cursor> };')
        self.assertFalse({'equals', 'hash_code', 'to_string'} & functions)

    def test_space_trigger_is_limited_to_override_member_names(self):
        def triggered(source):
            self.sequence += 1
            path = self.root / f'trigger-{self.sequence}.nra'
            before, after = source.split('<cursor>')
            self.client.open(path, before + after)
            position = params(path, before.count('\n'), len(before.rsplit('\n', 1)[-1]))
            position['context'] = {'triggerKind': 2, 'triggerCharacter': ' '}
            return self.client.request('textDocument/completion', position)['items']
        self.assertEqual(triggered('let <cursor>'), [])
        self.assertEqual(triggered('let value = 1; <cursor>'), [])
        self.assertEqual({item['label'] for item in triggered('let Base = module (export let needed = func -> int;);\n'
                         'let App = module:Base (export let.override <cursor>);')}, {'needed'})

    def test_stdlib_hover_renders_brief(self):
        path = self.root / 'float.nra'
        source = 'let bfloat16 = import:Std.Core.BFloat16;\nlet small = bfloat16.MinNormal;\n'
        self.client.open(path, source)
        hover = self.client.request('textDocument/hover', params(path, 1, 24))['contents']['value']
        self.assertIn('The smallest positive normal value.', hover)
        self.assertNotIn('@brief', hover)

    def test_completion_ignores_strings_and_comments(self):
        self.assertEqual(self.labels('let text = "import:Std.<cursor>";'), set())
        self.assertEqual(self.labels('// let.<cursor>'), set())
        self.assertEqual(self.labels('let text = "//"; // let.<cursor>'), set())
        for source in ['let text = "con<cursor>', 'let text = "import:Std.<cursor>',
                       'let text = "escaped\\<cursor>', 'let text = "escaped \\" con<cursor>',
                       'let text = "// con<cursor>', 'let text = "\\q con<cursor>";',
                       'let text = "bad\\q con<cursor>',
                       '// "con<cursor>', 'let text = "closed"// con<cursor>']:
            with self.subTest(source=source):
                self.assertEqual(self.labels(source), set())
        self.assertIn('let', self.labels('let text = "//";\nl<cursor>'))
        self.assertIn('const', self.labels('let text = "closed"; con<cursor>'))
        self.assertIn('const', self.labels('let text = "closed"<cursor>'))
        for newline in ['\n', '\r', '\r\n']:
            with self.subTest(newline=repr(newline)):
                self.assertIn('const', self.labels('let text = "unfinished' + newline + 'con<cursor>'))
                self.assertIn('const', self.labels('// "comment' + newline + 'con<cursor>'))

    def test_workspace_symbols_exclude_tool_worktrees_and_build_outputs(self):
        for directory in ['.claude/worktrees/old', '.codex/worktrees/old', '.worktrees/old', 'worktrees/old',
                          'out/debug', 'build-debug', 'cmake-build-release']:
            root = self.root / directory
            root.mkdir(parents=True)
            (root / 'stale.nra').write_text('let stale = 1;')
        (self.root / 'live.nra').write_text('let live = 1;')
        results = self.client.request('workspace/symbol', {'query': ''})
        self.assertIn('live', {item['name'] for item in results})
        self.assertNotIn('stale', {item['name'] for item in results})


if __name__ == '__main__':
    unittest.main()
