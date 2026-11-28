#!/usr/bin/env python3
"""Shared pointer state changes cannot be split by a task switch.

The owners and weak references of one target share library state (each
SharedGroup's slot, owner count and with_mut flag, each SharedPointer's
released flag, and each AtomicPointer's slot). Tasks may share a pointer whose
target is immutable, and an AtomicPointer, so a task switch may fall at any
polled node. This test compiles a program that spawns, so that calls and loops
carry polls, and walks the specialized NeuraIR of every trusted pointer method
in evaluation order. A store to that state fails the test when a polled node
lies between it and a read of that state that it depends on: a read in its own
value or destination, a read kept in a local that it uses, or a read in the
condition of a branch it is inside. A store whose value is a constant and that
depends on no read, such as clearing the with_mut flag after the body returns,
may follow a poll. Fields bound once (`last`) never change and are not state.

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

let use = func(owner: SharedPointer<Config>, slot: AtomicPointer<Config>, counter: LockedPointer<int>) -> int {
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
    last current = slot.load();
    slot.store(copy);
    last previous = slot.swap(current);
    last replaced = slot.compare_exchange(previous, copy);
    previous.release();
    current.release();
    counter.update(func(value: int) -> int => value + 1);
    copy.release();
    owner.release();
    return if (weak.is_alive() && replaced) -> level + counter.read() else -> 0;
};

let run = func() -> int {
    let root: SharedPointer<Config> = new(new Config(3));
    let slot: AtomicPointer<Config> = new(root);
    let counter: LockedPointer<int> = new(0);
    scope {
        last task = spawn use(root.clone(), slot, counter);
        return task.join();
    }
};
"""

# The classes whose state tasks share, and the methods that change it.
SHARED_CLASSES = ("SharedPointer<", "WeakPointer<", "SharedGroup<", "AtomicPointer<", "LockedPointer<")
SYMBOL = re.compile(r'^  %(\d+) (\w+) "([^"]*)" : .*? owner=%(\d+)')
POLLED = re.compile(r'^(call|virtual_call|call_closure|loop)\b.* poll\b')
REFERENCE = re.compile(r'^(\w+)(?: "[^"]*")? %(\d+)')


class Node:
    def __init__(self, head):
        self.head = head
        self.sections = []  # (name, [Node])

    def section(self, name):
        return dict(self.sections).get(name, [])

    def kind(self):
        return self.head.split(" ", 1)[0]

    def symbol(self):
        match = REFERENCE.match(self.head)
        return int(match.group(2)) if match else None


def parse_body(lines, start):
    """Parses the body that begins after line `start` ('body %N {')."""
    def block(index, indent):
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
                    children, index = block(index + 1, indent + 4)
                    node.sections.append((name, children))
                else:
                    break
            nodes.append(node)

    return block(start + 1, 2)


def earliest(*positions):
    known = [p for p in positions if p is not None]
    return min(known) if known else None


class Walk:
    """Evaluation order: operands, then the node; a branch's arms after its
    condition; a loop's poll before its body."""

    def __init__(self, name, state_fields, locals_, problems):
        self.name = name
        self.state = state_fields
        self.locals = locals_
        self.problems = problems
        self.position = 0
        self.polls = []
        self.dependency = {}  # local -> earliest state read its value came from
        self.stores = 0

    def tick(self):
        self.position += 1
        return self.position

    def split(self, since):
        return since is not None and any(since < p < self.position for p in self.polls)

    def visit(self, node, control):
        kind = node.kind()
        if kind == "loop" and POLLED.match(node.head):
            self.polls.append(self.tick())
        operands = node.section("operands")
        if kind == "store" and len(operands) == 2:
            destination, value = operands
            value_read = self.visit(value, control)
            destination_read = None
            if destination.kind() == "field":
                for child in destination.section("operands"):
                    destination_read = earliest(destination_read, self.visit(child, control))
            self.tick()
            target = destination.symbol()
            if destination.kind() == "field" and target in self.state:
                self.stores += 1
                since = earliest(value_read, destination_read, control)
                if self.split(since):
                    self.problems.append(f"{self.name}: a poll falls between a read of shared pointer state and "
                                         f"the store to field %{target} that depends on it")
            elif destination.kind() == "load" and target in self.locals:
                self.dependency[target] = earliest(value_read, control)
            return None
        reads = None
        for child in operands:
            reads = earliest(reads, self.visit(child, control))
        position = self.tick()
        symbol = node.symbol()
        if kind == "field" and symbol in self.state:
            reads = earliest(reads, position)
        elif kind == "load" and symbol in self.dependency:
            reads = earliest(reads, self.dependency[symbol])
        elif kind == "initialize" and symbol in self.locals:
            self.dependency[symbol] = earliest(reads, control)
        if POLLED.match(node.head) and kind != "loop":
            self.polls.append(position)
        inner = earliest(control, reads) if kind in ("branch", "select_value") else control
        for section in ("body", "alternative"):
            for child in node.section(section):
                self.visit(child, inner)
        if kind == "loop" and POLLED.match(node.head):
            self.polls.append(self.tick())
        return reads


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
            symbols[int(match.group(1))] = (match.group(2), match.group(3), int(match.group(4)), line)
    shared = {sid for sid, (kind, name, _, _) in symbols.items()
              if kind == "class" and name.startswith(SHARED_CLASSES)}
    state = {sid for sid, (kind, _, owner, line) in symbols.items()
             if kind == "field" and owner in shared and " const " not in line}
    locals_ = {sid for sid, (kind, _, _, _) in symbols.items() if kind == "local"}
    methods = {sid: f"{symbols[owner][1]}.{name}" for sid, (kind, name, owner, _) in symbols.items()
               if kind == "function" and owner in shared and name != "init"}
    if not shared or not state or not methods:
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
        walk = Walk(methods[int(match.group(1))], state, locals_, problems)
        for node in nodes:
            walk.visit(node, None)
        stores += walk.stores
        checked += 1
    expected = {"clone", "release", "close", "upgrade", "with_mut", "load", "store", "swap", "compare_exchange",
                "update"}
    seen = {name.split(".", 1)[1] for name in methods.values()}
    missing = expected - seen
    if missing:
        problems.append(f"methods not found in the IR: {sorted(missing)}")
    if stores < 16:
        problems.append(f"only {stores} stores to shared pointer state were found")
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"{checked} trusted pointer methods: {stores} stores to shared state, none split by a poll")
    return 0


if __name__ == "__main__":
    sys.exit(main())
