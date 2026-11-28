#!/usr/bin/env python3
"""Shared pointer state changes cannot be split by a task switch.

The owners and weak references of one target share library state (the
SharedGroup, and each SharedPointer's own released flag). Tasks may share a
pointer whose target is immutable, so a task switch may fall between any two
polled nodes. This test compiles a program that spawns, so that calls and loops
carry polls, and walks the specialized NeuraIR of every trusted pointer method:
a store to that state whose value is not a constant must come before the
method's first polled node, and its value must not poll. A constant store, such
as clearing the with_mut flag after the body returns, may follow a poll, since
it reads nothing that another task could have changed in between.

Usage: polls.py <neura>
"""

import pathlib
import re
import subprocess
import sys
import tempfile

NEURA = pathlib.Path(sys.argv[1]).resolve()

PROGRAM = """let optional = import:Std.Core.Optional;
let.promote _ = import:Std.Core.Pointer;

.Immutable
let Config = class (
    export last level: int;
    export let init = func(value: int) {
        level = value;
    };
);

let use = func(owner: SharedPointer<Config>) -> int {
    last copy = owner.clone();
    last level = copy.read().level + copy.with_read<int>(func(config: Config) -> int => config.level);
    copy.with_mut(func(config: Config) {});
    last weak = copy.downgrade();
    match (weak.upgrade()) {
        optional.Optional.Some(again) => {
            again.close();
        },
        optional.Optional.None => {},
    }
    copy.release();
    owner.release();
    return if (weak.is_alive()) -> level else -> 0;
};

let run = func() -> int {
    let root: SharedPointer<Config> = new(new Config(3));
    scope {
        last task = spawn use(root.clone());
        return task.join();
    }
};
"""

# The classes whose state tasks share, and the methods that change it.
SHARED_CLASSES = ("SharedPointer<", "WeakPointer<", "SharedGroup<")
SYMBOL = re.compile(r'^  %(\d+) (\w+) "([^"]*)" : .*? owner=%(\d+)')
POLLED = re.compile(r'^(call|virtual_call|call_closure|loop)\b.* poll\b')
CONSTANT = re.compile(r'^(integer|bool|boolean|float|string|nil|enum_construct %\d+ :)')


class Node:
    def __init__(self, head):
        self.head = head
        self.sections = []  # (name, [Node])


def parse_body(lines, start):
    """Parses the body that begins after line `start` ('body %N {'). Returns
    the top-level nodes and the index after the closing brace."""
    def block_with_close(index, indent):
        nodes = []
        while True:
            line = lines[index]
            current = len(line) - len(line.lstrip())
            if line.strip() == "}" and current == indent - 2:
                return nodes, index + 1
            node = Node(line.strip())
            index += 1
            while True:
                inner = lines[index]
                inner_indent = len(inner) - len(inner.lstrip())
                if inner_indent == indent + 2 and inner.strip().endswith("{"):
                    name = inner.strip()[:-1].strip()
                    children, index = block_with_close(index + 1, indent + 4)
                    node.sections.append((name, children))
                else:
                    break
            nodes.append(node)

    return block_with_close(start + 1, 2)


def check_body(name, nodes, tracked_fields, problems):
    state = {"polled": None, "stores": 0}

    def destination_field(node):
        operands = dict(node.sections).get("operands", [])
        if not operands:
            return None
        match = re.match(r"^field %(\d+)", operands[0].head)
        return int(match.group(1)) if match else None

    def visit(node):
        sections = dict(node.sections)
        if POLLED.match(node.head) and node.head.startswith("loop"):
            # A loop polls at its back edge, so its body may follow a poll.
            state["polled"] = state["polled"] or node.head
        for child in sections.get("operands", []):
            visit(child)
        if node.head.startswith("store"):
            field = destination_field(node)
            if field in tracked_fields:
                state["stores"] += 1
                value = sections["operands"][1]
                if not CONSTANT.match(value.head) and state["polled"]:
                    problems.append(f"{name}: the store to field %{field} follows the polled node "
                                    f"{state['polled']!r}")
        if POLLED.match(node.head) and not node.head.startswith("loop"):
            state["polled"] = state["polled"] or node.head
        for section in ("body", "alternative"):
            for child in sections.get(section, []):
                visit(child)

    for node in nodes:
        visit(node)
    return state["stores"]


def main():
    with tempfile.TemporaryDirectory(prefix="neura-pointer-polls-") as scratch:
        source = pathlib.Path(scratch) / "polls.nra"
        source.write_text(PROGRAM, encoding="utf-8")
        result = subprocess.run([str(NEURA), "emit-ir", str(source)], capture_output=True, text=True,
                                encoding="utf-8", timeout=300)
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr)
            return 1
    lines = result.stdout.splitlines()
    symbols = {}
    for line in lines:
        match = SYMBOL.match(line)
        if match:
            symbols[int(match.group(1))] = (match.group(2), match.group(3), int(match.group(4)))
    shared = {sid for sid, (kind, name, _) in symbols.items()
              if kind == "class" and name.startswith(SHARED_CLASSES)}
    tracked = {sid for sid, (kind, _, owner) in symbols.items() if kind == "field" and owner in shared}
    methods = {sid: f"{symbols[owner][1]}.{name}" for sid, (kind, name, owner) in symbols.items()
               if kind == "function" and owner in shared and name != "init"}
    if not shared or not tracked or not methods:
        print("no shared pointer state found in the program's IR", file=sys.stderr)
        return 1
    problems = []
    checked = 0
    stores = 0
    for index, line in enumerate(lines):
        match = re.match(r"^body %(\d+) \{$", line)
        if not match or int(match.group(1)) not in methods:
            continue
        nodes, _ = parse_body(lines, index)
        stores += check_body(methods[int(match.group(1))], nodes, tracked, problems)
        checked += 1
    expected = {"clone", "release", "close", "upgrade", "with_mut"}
    seen = {name.split(".", 1)[1] for name in methods.values()}
    missing = expected - seen
    if missing:
        problems.append(f"methods not found in the IR: {sorted(missing)}")
    if stores < 8:
        problems.append(f"only {stores} stores to shared pointer state were found")
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"{checked} trusted pointer methods: {stores} stores to shared state, none split by a poll")
    return 0


if __name__ == "__main__":
    sys.exit(main())
