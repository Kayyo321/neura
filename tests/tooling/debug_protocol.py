#!/usr/bin/env python3
"""Real DAP/NVM integration: no mocked frames, events or execution."""
import json
import pathlib
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest

NEURA = pathlib.Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else pathlib.Path('build/neura').resolve()


class Client:
    def __init__(self):
        self.process = subprocess.Popen([str(NEURA), 'debug', '--stdio'], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.messages = queue.Queue()
        self.events = []
        self.seq = 0
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        status = self.process.wait(timeout=2)
                        error = self.process.stderr.read().decode(errors='replace')
                        self.messages.put(RuntimeError(f'adapter closed protocol stream ({status}): {error}'))
                        return
                    if line in (b'\r\n', b'\n'):
                        break
                    key, value = line.decode().split(':', 1)
                    headers[key.lower()] = value.strip()
                size = int(headers['content-length'])
                data = self.process.stdout.read(size)
                self.messages.put(json.loads(data))
        except BaseException as error:
            self.messages.put(error)

    def next(self, timeout=60):
        message = self.messages.get(timeout=timeout)
        if isinstance(message, BaseException):
            raise message
        return message

    def request(self, command, arguments=None, success=True, timeout=20):
        self.seq += 1
        data = json.dumps({'seq': self.seq, 'type': 'request', 'command': command,
                           'arguments': arguments or {}}).encode()
        self.process.stdin.write(b'Content-Length: ' + str(len(data)).encode() + b'\r\n\r\n' + data)
        self.process.stdin.flush()
        while True:
            result = self.next(timeout)
            if result.get('type') == 'response' and result.get('request_seq') == self.seq:
                if result['success'] != success:
                    raise AssertionError(result)
                return result.get('body', {}) if success else result
            self.events.append(result)

    def event(self, name, timeout=60):
        deadline = time.monotonic() + timeout
        while True:
            for i, message in enumerate(self.events):
                if message.get('event') == name:
                    return self.events.pop(i).get('body', {})
            self.events.append(self.next(max(.01, deadline - time.monotonic())))

    def close(self):
        if self.process.poll() is None:
            try:
                self.request('disconnect', timeout=2)
            except (OSError, queue.Empty, RuntimeError):
                pass
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()
        self.process.stderr.close()


class DebugProtocol(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='neura-dap-')
        self.root = pathlib.Path(self.directory.name).resolve()
        self.client = Client()
        self.capabilities = self.client.request('initialize', {'adapterID': 'neura',
                                                'linesStartAt1': True, 'columnsStartAt1': True})

    def tearDown(self):
        self.client.close()
        self.directory.cleanup()

    def source(self, name, content):
        path = self.root / name
        path.write_text(content)
        return path

    def launch(self, path, **options):
        self.client.request('launch', {'program': str(path), 'cwd': str(self.root), **options})
        self.client.event('initialized')

    def frame(self):
        threads = self.client.request('threads')['threads']
        stack = self.client.request('stackTrace', {'threadId': threads[0]['id']})['stackFrames']
        self.assertTrue(stack)
        return stack[0]

    def test_breakpoint_step_locals_condition_and_source(self):
        source = '''let program = import:Std.Program;
let console = import:Std.Io.Console;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let counter: int32 = 1;
        let values: int32[] = [3, 5];
        counter += 1;
        console.print_line("dap output");
        return program.ExitCode.Ok;
    };
);
'''
        path = self.source('main.nra', source)
        self.launch(path)
        result = self.client.request('setBreakpoints', {'source': {'path': str(path)},
            'breakpoints': [{'line': 7, 'condition': 'counter == 1'}]})
        self.assertTrue(result['breakpoints'][0]['verified'])
        self.client.request('configurationDone')
        self.assertEqual(self.client.event('stopped')['reason'], 'breakpoint')
        frame = self.frame()
        self.assertEqual(frame['line'], 7)
        self.assertEqual(frame['source']['path'], str(path))
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'counter'})['result'], '1')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'counter < 2 && counter >= 1'})['result'], 'true')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'values[1]'})['result'], '5')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'false && values[999] == 0'})['result'], 'false')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'true || missing == 0'})['result'], 'true')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'false && missing.field[999] == 0 || true'})['result'], 'true')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'true || (false && missing.field[999] == 0)'})['result'], 'true')
        # A character literal is the integer of its byte (docs/design/Expressions030.md).
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': "'A'"})['result'], '65')
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': "counter < '\\x02' && '\\n' == 10"})['result'], 'true')
        self.client.request('evaluate', {'frameId': frame['id'], 'expression': "'ab'"}, success=False)
        self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'false && (counter = 9)'}, success=False)
        self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'counter = 9'}, success=False)
        self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'counter()'}, success=False)
        scopes = self.client.request('scopes', {'frameId': frame['id']})['scopes']
        variables = self.client.request('variables', {'variablesReference': scopes[0]['variablesReference']})['variables']
        self.assertIn('counter', [item['name'] for item in variables])
        values = next(item for item in variables if item['name'] == 'values')
        children = self.client.request('variables', {'variablesReference': values['variablesReference']})['variables']
        self.assertEqual([child['value'] for child in children], ['3', '5'])
        content = self.client.request('source', {'sourceReference': frame['source']['sourceReference']})['content']
        self.assertEqual(content, source)
        self.client.request('next')
        self.assertEqual(self.client.event('stopped')['reason'], 'step')
        frame = self.frame()
        self.assertEqual(frame['line'], 8)
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'counter'})['result'], '2')
        path.write_text(source + "\n// changed after compilation\n")
        stale = self.client.request('setBreakpoints', {'source': {'path': str(path)}, 'breakpoints': [{'line': 7}]})
        self.assertFalse(stale['breakpoints'][0]['verified'])
        self.assertIn('changed since compilation', stale['breakpoints'][0]['message'])
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 0)
        self.client.event('terminated')
        outputs = [e['body']['output'] for e in self.client.events if e.get('event') == 'output']
        self.assertIn('dap output\n', ''.join(outputs))
        self.assertFalse(self.capabilities['supportsStepBack'])

    def test_hooked_nil_inspection_uses_raw_storage(self):
        source = '''let program = import:Std.Program;
let console = import:Std.Io.Console;
let log: string = "";
let Session = class (
    | get: { log += "g"; return instance; };
    | set: { log += "s"; return value; };
);
let Plain = class ();
let Box = class (
    export let item: Session = nil;
    export let ordinary: Plain = nil;
);
let global: Session = nil;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let item: Session = nil;
        let box: Box = new;
        let items: Session[list] = new([item]);
        console.print_line(log);
        return program.ExitCode.Ok;
    };
);
'''
        path = self.source('hooks.nra', source)
        self.launch(path)
        points = self.client.request('setBreakpoints', {'source': {'path': str(path)},
                                     'breakpoints': [{'line': source.splitlines().index('        console.print_line(log);') + 1}]})
        self.assertTrue(points['breakpoints'][0]['verified'])
        self.client.request('configurationDone')
        self.client.event('stopped')
        frame = self.frame()
        def evaluate(expression):
            return self.client.request('evaluate', {'frameId': frame['id'], 'expression': expression})['result']
        before = evaluate('log')
        self.assertEqual(evaluate('item'), 'nil (policy Session)')
        self.assertEqual(evaluate('global'), 'nil (policy Session)')
        self.assertEqual(evaluate('box.item'), 'nil (policy Session)')
        self.assertEqual(evaluate('items[0]'), 'nil (policy Session)')
        scopes = self.client.request('scopes', {'frameId': frame['id']})['scopes']
        locals_ = self.client.request('variables', {'variablesReference': scopes[0]['variablesReference']})['variables']
        item = next(value for value in locals_ if value['name'] == 'item')
        self.assertEqual(item['value'], 'nil (policy Session)')
        self.assertFalse(any(value['name'].endswith('.policy') for value in locals_))
        items = next(value for value in locals_ if value['name'] == 'items')
        elements = self.client.request('variables', {'variablesReference': items['variablesReference']})['variables']
        self.assertEqual([element['value'] for element in elements], ['nil (policy Session)'])
        box = next(value for value in locals_ if value['name'] == 'box')
        fields = self.client.request('variables', {'variablesReference': box['variablesReference']})['variables']
        self.assertEqual(next(value for value in fields if value['name'] == 'item')['value'], 'nil (policy Session)')
        self.assertFalse(any(value['name'].endswith('.policy') for value in fields))
        self.assertEqual(next(value for value in fields if value['name'] == 'ordinary')['value'], 'nil')
        paged = [self.client.request('variables', {'variablesReference': box['variablesReference'],
                                                   'start': i, 'count': 1})['variables'] for i in range(len(fields))]
        self.assertEqual([page[0]['name'] for page in paged], [value['name'] for value in fields])
        globals_ = self.client.request('variables', {'variablesReference': scopes[1]['variablesReference']})['variables']
        self.assertEqual(next(value for value in globals_ if value['name'].endswith('global'))['value'],
                         'nil (policy Session)')
        self.assertEqual(evaluate('log'), before)
        self.client.request('continue', {'threadId': 0})
        self.client.event('terminated')
        output = ''.join(event.get('body', {}).get('output', '') for event in self.client.events
                         if event.get('event') == 'output')
        self.assertIn(before.strip('"') + '\n', output)

    def test_any_reads_named_words_without_hooks_or_steps(self):
        source = r'''let program = import:Std.Program;
let counter: int = 0;
let Session = class (
    | get: { counter += 1; return instance; };
    | set: { counter += 1; return value; };
    export let payload: int = 7;
);
let Box = class (
    export let first: int = 3;
    export let item: any = nil;
    export let last_value: int = 5;
);
let one32 = func -> uint32 => 1;
let one64 = func -> uint64 => 1;
let blank = func -> string => "";
let Group = module (
    export let tag: uint32 = one32();
    export let bits: uint64 = one64();
    export let text: string = blank();
    export let reference: class = nil;
);
let global: any = nil;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let truth: any = true;
        let small: any = -128 as int8;
        let minimum: int64 = -9223372036854775808;
        let signed: any = minimum;
        let maximum: uint64 = 18446744073709551615;
        let unsigned: any = maximum;
        let half: any = float16.from_bits(32291);
        let brain: any = bfloat16.from_bits(32657);
        let negative_zero: any = float32.from_bits(2147483648);
        let nan: any = float64.from_bits(9221120237041090731);
        let typed: Session = nil;
        let policy: any = typed;
        let empty: any = nil;
        let object: any = new Session();
        let materials: [uint32, uint64, string, class] = [Group.tag, Group.bits, Group.text, Group.reference];
        let box: Box = new;
        box.item = object;
        global = "line\n\"quoted\"é";
        let items: any[list] = new([truth, empty]);
        let checkpoint = counter;
        counter += 1;
        return counter == checkpoint + 1 ? program.ExitCode.Ok : program.ExitCode.Failure;
    };
);
'''
        path = self.source('any.nra', source)
        self.launch(path)
        line = source.splitlines().index('        counter += 1;') + 1
        points = self.client.request('setBreakpoints', {'source': {'path': str(path)},
                                     'breakpoints': [{'line': line}]})
        self.assertTrue(points['breakpoints'][0]['verified'])
        self.client.request('configurationDone')
        self.assertEqual(self.client.event('stopped')['reason'], 'breakpoint')
        frame = self.frame()

        def evaluate(expression):
            return self.client.request('evaluate', {'frameId': frame['id'], 'expression': expression})

        def children(reference, **paging):
            return self.client.request('variables', {'variablesReference': reference, **paging})['variables']

        before = int(evaluate('checkpoint')['result'])
        self.assertGreater(before, 0)
        self.assertEqual(int(evaluate('counter')['result']), before)
        scopes = self.client.request('scopes', {'frameId': frame['id']})['scopes']
        frames = children(scopes[0]['variablesReference'])
        locals_ = {value['name']: value for value in frames}
        expected = {
            'truth': 'any<bool> true',
            'small': 'any<int8> -128',
            'signed': 'any<int64> -9223372036854775808',
            'unsigned': 'any<uint64> 18446744073709551615',
            'half': 'any<float16> 0x7e23',
            'brain': 'any<bfloat16> 0x7f91',
            'negative_zero': 'any<float32> 0x80000000',
            'nan': 'any<float64> 0x7ff80000000000ab',
            'empty': 'any<nil> nil',
            'policy': 'any<nil> nil (policy Session)',
        }
        for name, value in expected.items():
            with self.subTest(name=name):
                self.assertEqual((locals_[name]['type'], locals_[name]['value']), ('any', value))
                watched = evaluate(name)
                self.assertEqual((watched['type'], watched['result']), ('any', value))
        self.assertFalse(any('.$any.tag' in name for name in locals_))
        self.assertEqual(locals_['materials.0']['type'], 'uint32')
        self.assertEqual(locals_['materials.1']['type'], 'uint64')
        self.assertNotIn('materials', locals_)

        globals_ = {value['name']: value for value in children(scopes[1]['variablesReference'])}
        global_text = r'any<string> "line\n\"quoted\"é"'
        self.assertEqual((globals_['global']['type'], globals_['global']['value']), ('any', global_text))
        self.assertEqual(evaluate('global')['result'], global_text)
        self.assertNotIn('Group', globals_)
        group_start = list(globals_).index('Group.tag')
        self.assertEqual(list(globals_)[group_start:group_start + 4],
                         ['Group.tag', 'Group.bits', 'Group.text', 'Group.reference'])
        for suffix, kind, value in [('tag', 'uint32', '1'), ('bits', 'uint64', '1'),
                                    ('text', 'string', '""'), ('reference', 'object', 'nil')]:
            ordinary = globals_['Group.' + suffix]
            self.assertEqual((ordinary['type'], ordinary['value']), (kind, value))

        object_ = locals_['object']
        self.assertEqual(object_['type'], 'any')
        self.assertTrue(object_['value'].startswith('any<Session> object @'))
        self.assertEqual(children(object_['variablesReference'])[0]['value'], '7')
        box = locals_['box']
        fields = children(box['variablesReference'])
        self.assertEqual(box['namedVariables'], len(fields))
        self.assertEqual([field['name'] for field in fields], ['first', 'item', 'last_value'])
        self.assertEqual(children(box['variablesReference'], start=1, count=1)[0]['value'], object_['value'])
        self.assertEqual(evaluate('box.item')['result'], object_['value'])
        self.assertEqual(evaluate('box.item.payload')['result'], '7')
        self.assertEqual(children(locals_['items']['variablesReference'])[0]['type'], 'tuple')
        self.assertEqual(evaluate('items[0]')['type'], 'tuple')
        self.assertEqual(int(evaluate('counter')['result']), before)
        self.assertEqual(self.frame()['line'], line)
        self.client.request('next')
        self.assertEqual(self.client.event('stopped')['reason'], 'step')
        frame = self.frame()
        self.assertEqual(frame['line'], line + 1)
        self.assertEqual(int(evaluate('counter')['result']), before + 1)
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 0)
        self.client.event('terminated')

    def test_any_prebuilt_artifacts_with_marked_legacy_and_missing_names(self):
        source = '''let program = import:Std.Program;
let console = import:Std.Io.Console;
let erased: any = 42;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line("entry");
        return program.ExitCode.Ok;
    };
);
'''
        path = self.source('artifact.nra', source)
        artifact = self.root / 'original.nvr'
        subprocess.run([NEURA, 'build', '--no-optimize', path, '-o', artifact], check=True, cwd=self.root)
        assembly = subprocess.check_output([NEURA, 'disassemble', artifact], text=True, cwd=self.root)
        prefix = 'section "NAME" '
        record = next(line for line in assembly.splitlines() if line.startswith(prefix))
        names = bytes.fromhex(record.removeprefix(prefix))
        marked_name = b'erased.$any.tag'
        legacy_name = b'erased.tag'
        marked = len(marked_name).to_bytes(4, 'little') + marked_name
        self.assertEqual(names.count(marked), 1)
        legacy = len(legacy_name).to_bytes(4, 'little') + legacy_name
        legacy_text = assembly.replace(record, prefix + names.replace(marked, legacy).hex())
        unmarked_path = self.source('legacy-name.nvra', legacy_text)
        unmarked = self.root / 'legacy-name.nvr'
        subprocess.run([NEURA, 'assemble', unmarked_path, '-o', unmarked], check=True, cwd=self.root)
        stripped_text = assembly.replace(record + '\n', '')
        stripped = self.root / 'without-name.nvr'
        stripped_path = self.source('without-name.nvra', stripped_text)
        subprocess.run([NEURA, 'assemble', stripped_path, '-o', stripped], check=True, cwd=self.root)

        for image, mode in [(artifact, 'marked'), (unmarked, 'legacy'), (stripped, 'stripped')]:
            with self.subTest(mode=mode):
                client = Client()
                try:
                    client.request('initialize', {'adapterID': 'neura', 'linesStartAt1': True, 'columnsStartAt1': True})
                    client.request('launch', {'program': str(image), 'cwd': str(self.root)})
                    client.event('initialized')
                    points = client.request('setBreakpoints', {'source': {'path': str(path)}, 'breakpoints': [{'line': 6}]})
                    self.assertTrue(points['breakpoints'][0]['verified'])
                    client.request('configurationDone')
                    self.assertEqual(client.event('stopped')['reason'], 'breakpoint')
                    thread = client.request('threads')['threads'][0]
                    frame = client.request('stackTrace', {'threadId': thread['id']})['stackFrames'][0]
                    scopes = client.request('scopes', {'frameId': frame['id']})['scopes']
                    variables = client.request('variables', {'variablesReference': scopes[1]['variablesReference']})['variables']
                    if mode == 'marked':
                        variable = next(value for value in variables if value['name'] == 'erased')
                        self.assertEqual((variable['type'], variable['value']), ('any', 'any<int32> 42'))
                    elif mode == 'legacy':
                        values = {value['name']: value for value in variables}
                        self.assertNotIn('erased', values)
                        self.assertEqual(values['erased.tag']['type'], 'uint32')
                        self.assertEqual(values['erased.bits']['value'], '42')
                    else:
                        self.assertTrue(all(value['name'].startswith('global ') for value in variables))
                        self.assertIn(('uint64', '42'), [(value['type'], value['value']) for value in variables])
                    client.request('continue')
                    self.assertEqual(client.event('exited')['exitCode'], 0)
                    client.event('terminated')
                finally:
                    client.close()

    def test_hook_step_and_trapping_admission_preserve_previous_field(self):
        source = '''let program = import:Std.Program;
let failure = import:Std.Core.Failure;
let Session = class (
    | get: instance;
    | set: value;
);
let Accepted = class:Session ();
let Rejected = class:Session (
    | set.override: {
        failure.trap("rejected");
        return value;
    };
);
let Box = class (
    export let item: Session = nil;
);
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let box: Box = new;
        let first: Accepted = nil;
        box.item = first;
        let copy = first;
        box.item = new Rejected();
        return program.ExitCode.Ok;
    };
);
'''
        path = self.source('hook-trap.nra', source)
        self.launch(path)
        self.client.request('setBreakpoints', {'source': {'path': str(path)}, 'breakpoints': [{'line': 22}]})
        self.client.request('configurationDone')
        self.assertEqual(self.client.event('stopped')['reason'], 'breakpoint')
        self.client.request('setBreakpoints', {'source': {'path': str(path)}, 'breakpoints': []})
        thread = self.client.request('threads')['threads'][0]['id']
        for _ in range(8):
            self.client.request('stepIn', {'threadId': thread})
            self.assertEqual(self.client.event('stopped')['reason'], 'step')
            if '$class_get' in self.frame()['name']:
                break
        self.assertIn('$class_get', self.frame()['name'])
        self.client.request('stepOut', {'threadId': thread})
        self.assertEqual(self.client.event('stopped')['reason'], 'step')
        self.assertIn('main', self.frame()['name'])
        self.client.request('continue')
        self.assertEqual(self.client.event('stopped')['reason'], 'exception')
        self.assertIn('rejected', self.client.request('exceptionInfo', {'threadId': thread})['description'])
        stack = self.client.request('stackTrace', {'threadId': thread})['stackFrames']
        main = next(frame for frame in stack if 'main' in frame['name'])
        for _ in range(2):
            self.assertEqual(self.client.request('evaluate', {'frameId': main['id'],
                                                             'expression': 'box.item'})['result'],
                             'nil (policy Accepted)')
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 134)

    def test_breakpoint_in_a_startup_block_stops_before_main(self):
        source = '''let program = import:Std.Program;
let console = import:Std.Io.Console;
let first = 4;
static scope {
    let doubled = first * 2;
    console.print_line("startup " + doubled.to_string());
}
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line("main");
        return program.ExitCode.Ok;
    };
);
'''
        path = self.source('startup.nra', source)
        self.launch(path)
        result = self.client.request('setBreakpoints', {'source': {'path': str(path)}, 'breakpoints': [{'line': 6}]})
        self.assertTrue(result['breakpoints'][0]['verified'])
        self.client.request('configurationDone')
        self.assertEqual(self.client.event('stopped')['reason'], 'breakpoint')
        frame = self.frame()
        self.assertEqual(frame['line'], 6)
        self.assertEqual(self.client.request('evaluate', {'frameId': frame['id'], 'expression': 'doubled'})['result'], '8')
        outputs = [e['body']['output'] for e in self.client.events if e.get('event') == 'output']
        self.assertNotIn('main\n', ''.join(outputs))
        # A line stops once at each of its statement positions; the block itself runs once.
        while True:
            self.client.request('continue')
            message = self.client.next()
            while message.get('event') not in ('stopped', 'exited'):
                self.client.events.append(message)
                message = self.client.next()
            if message['event'] == 'exited':
                break
            self.assertEqual(self.frame()['line'], 6)
        self.assertEqual(message['body']['exitCode'], 0)
        self.client.event('terminated')
        outputs = [e['body']['output'] for e in self.client.events if e.get('event') == 'output']
        self.assertEqual(''.join(outputs).count('startup 8\n'), 1)
        self.assertIn('main\n', ''.join(outputs))

    def test_pending_breakpoint_keeps_id_and_condition_short_circuits(self):
        path = self.source('pending.nra', """let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let counter: int32 = 1;
        counter += 1;
        return program.ExitCode.Ok;
    };
);
""")
        pending = self.client.request('setBreakpoints', {'source': {'path': str(path)},
            'breakpoints': [{'line': 5, 'condition': 'counter == 1 && (true || missing.field[999] == 0)'}]})
        original_id = pending['breakpoints'][0]['id']
        self.assertFalse(pending['breakpoints'][0]['verified'])
        self.launch(path)
        changed = self.client.event('breakpoint')
        self.assertEqual(changed['reason'], 'changed')
        self.assertEqual(changed['breakpoint']['id'], original_id)
        self.assertTrue(changed['breakpoint']['verified'])
        self.client.request('configurationDone')
        stopped = self.client.event('stopped')
        self.assertEqual(stopped['reason'], 'breakpoint')
        self.assertEqual(stopped['hitBreakpointIds'], [original_id])
        self.assertNotIn('description', stopped)
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 0)
        self.client.event('terminated')

    def test_breakpoint_locations_use_utf16_columns(self):
        source = """let program = import:Std.Program;
let console = import:Std.Io.Console;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line("😀"); let marker: int32 = 1;
        return program.ExitCode.Ok;
    };
);
"""
        path = self.source('unicode.nra', source)
        self.launch(path)
        line = source.splitlines()[4]
        prefix = line[:line.index('let marker')]
        expected = len(prefix.encode('utf-16-le')) // 2 + 1
        self.assertNotEqual(expected, len(prefix) + 1)
        locations = self.client.request('breakpointLocations', {'source': {'path': str(path)}, 'line': 5, 'endLine': 5})
        self.assertIn(expected, [item['column'] for item in locations['breakpoints']])

    def test_one_selected_test_and_stable_workspace_identity(self):
        path = self.source('suite.nra', '''let Suite = module (
    export let test_one = func { let count: int32 = 3; };
    export let test_two = func { let fail: int32 = 1 / 0; };
);
''')
        self.launch(path, testFiles=[str(path)], testFilter='suite.nra::Suite.test_one',
                    workspaceRoot=str(self.root))
        self.client.request('configurationDone')
        self.assertEqual(self.client.event('exited')['exitCode'], 0)
        self.client.event('terminated')

    def test_trap_inspection_and_no_reverse_debug_claim(self):
        path = self.source('trap.nra', '''let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let value: int32 = 0;
        value = 1 / value;
        return program.ExitCode.Ok;
    };
);
''')
        self.launch(path)
        self.client.request('configurationDone')
        self.assertEqual(self.client.event('stopped')['reason'], 'exception')
        info = self.client.request('exceptionInfo', {'threadId': 1})
        self.assertIn('trap', info['exceptionId'])
        self.assertEqual(info['breakMode'], 'unhandled')
        self.assertTrue(self.frame())
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 136)
        self.client.event('terminated')

    def test_spawned_task_has_real_threads_frames_and_values(self):
        source = """let program = import:Std.Program;
let console = import:Std.Io.Console;
let do_work = func(input: int32) -> int32 {
    let result = input + 1;
    return result;
};
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        scope {
            last task = spawn do_work(4);
            last outcome = task.join();
            console.print_line(outcome.to_string());
        }
        return program.ExitCode.Ok;
    };
);
"""
        path = self.source('tasks.nra', source)
        self.launch(path)
        self.client.request('setBreakpoints', {'source': {'path': str(path)}, 'breakpoints': [{'line': 5}]})
        self.client.request('configurationDone')
        stopped = self.client.event('stopped')
        threads = self.client.request('threads')['threads']
        self.assertGreaterEqual(len(threads), 2)
        stack = self.client.request('stackTrace', {'threadId': stopped['threadId']})['stackFrames']
        self.assertIn('do_work', stack[0]['name'])
        self.assertEqual(self.client.request('evaluate', {'frameId': stack[0]['id'], 'expression': 'input'})['result'], '4')
        self.assertEqual(self.client.request('evaluate', {'frameId': stack[0]['id'], 'expression': 'result'})['result'], '5')
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 0)
        self.client.event('terminated')

    def test_stdin_is_separate_from_protocol_and_wait_is_cancellable(self):
        path = self.source('input.nra', """let program = import:Std.Program;
let console = import:Std.Io.Console;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        last line = console.read_line();
        console.print_line("input received");
        return program.ExitCode.Ok;
    };
);
""")
        self.launch(path)
        self.client.request('configurationDone')
        time.sleep(.03)
        self.client.request('pause', {'threadId': 1})
        self.assertEqual(self.client.event('stopped')['reason'], 'pause')
        self.client.request('neura/input', {'text': 'hello from the debugger\n'})
        self.client.request('continue')
        self.assertEqual(self.client.event('exited')['exitCode'], 0)
        self.client.event('terminated')
        output = ''.join(e['body']['output'] for e in self.client.events if e.get('event') == 'output')
        self.assertIn('input received', output)

    def test_pause_and_disconnect_in_running_loop(self):
        path = self.source('loop.nra', '''let program = import:Std.Program;
let App = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        let counter: int32 = 0;
        while (true) { counter += 1; }
        return program.ExitCode.Ok;
    };
);
''')
        self.launch(path)
        self.client.request('configurationDone')
        time.sleep(.02)
        self.client.request('pause', {'threadId': 1})
        self.assertEqual(self.client.event('stopped')['reason'], 'pause')
        self.assertTrue(self.frame())
        started = time.monotonic()
        self.client.request('disconnect')
        self.client.process.wait(timeout=5)
        self.assertLess(time.monotonic() - started, 5)


if __name__ == '__main__':
    unittest.main()
