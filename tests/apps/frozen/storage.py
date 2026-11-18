"""Check the frozen-storage rules (E0581) inside a copy of the standard library.

#Frozen is accepted only in Std.Collections.Frozen and Std.Ui, so the
fixtures are written into std/ui/ of a temporary copy of the library, next to
a copy of the tool (the tool finds the library beside its own directory).
Each fixture is a class whose storage is used one way; the accepted ones must
check, and each rejected one must report E0581 at the line that leaks.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

NEURA = Path(sys.argv[1]).resolve()
SOURCE = Path(__file__).resolve().parents[3]
ENV = dict(os.environ, NEURA_GPU="off")

FILLED = "last copy: T[list] = new;\n            copy.append(values[0]);\n            storage = copy;"

CASES = [
    # name, constructor body, extra members, expected line or None
    ("filled", FILLED, "export let get = func(i: int) -> T => storage[i];\n"
                       "        export let size = func -> int => storage.length;", None),
    ("literal", "storage = new;", "", None),
    ("parameter", "storage = values;", "", 7),
    ("aliased", "last copy: T[list] = new;\n            last other = copy;\n            storage = copy;", "", 9),
    ("returned", FILLED, "export let raw = func -> T[list] => storage;", 11),
    ("appended", FILLED, "export let add = func(v: T) { storage.append(v); };", 11),
    ("element", FILLED, "export let put = func(v: T) { storage[0] = v; };", 11),
    ("passed", FILLED, "export let give = func(f: func(T[list]) -> void) { f(storage); };", 11),
    ("bound", FILLED, "export let keep = func -> int { last other = storage; return other.length; };", 11),
]

TEMPLATE = """export let Leak = module (
    #[Final, Immutable, Frozen]
    export let Box = class<T> (
        #Frozen
        last storage: T[list];
        export let init = func(values: T[list]) {{
            {init}
        }};
        {members}
    );
);
"""

USER = """let leak = import:Std.Ui.Leak;
let box = new leak.Box<int32>(new([1]));
"""


def main():
    failures = []
    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "bin").mkdir()
        tool = root / "bin" / "neura"
        shutil.copy2(NEURA, tool)
        shutil.copytree(SOURCE / "stdlib", root / "stdlib")
        user = root / "user.nra"
        user.write_text(USER)
        for name, init, members, line in CASES:
            (root / "stdlib/std/ui/leak.nra").write_text(TEMPLATE.format(init=init, members=members))
            result = subprocess.run([str(tool), "check", "--error-format=short", str(user)],
                                    capture_output=True, text=True, env=ENV)
            if line is None:
                if result.returncode != 0:
                    failures.append(f"{name}: expected acceptance\n{result.stderr}")
            elif result.returncode == 0 or f"leak.nra:{line}:" not in result.stderr or "E0581" not in result.stderr:
                failures.append(f"{name}: expected E0581 at line {line}\n{result.stderr}")
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"{len(CASES) - len(failures)} of {len(CASES)} frozen-storage cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
