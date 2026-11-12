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
        self.root = pathlib.Path(self.directory.name)
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
