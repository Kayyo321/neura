"""B-W9 on the model host: retained growth after mount and unmount cycles.

Builds retained-growth.nra for 100 cycles and for 10,000 cycles
(100,000 clicks, each a turn that updates, renders and commits). Both runs must end with no open subscription, and the
peak heap cells of the long run must stay within a small allowance of the
short run's (longer decimal counters in the host log), so nothing a cycle
allocates outlives it. Peak cells stand in for the live cells the budget names,
which the runtime does not report. Listener counts belong to the browser
host and are not measured here.
"""

import os
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory

NEURA = Path(sys.argv[1]).resolve()
HERE = Path(__file__).resolve().parent
ENV = dict(os.environ, NEURA_GPU="off")
# Cells for longer decimal counters in the long run's log lines.
TEXT_ALLOWANCE = 32


def run(directory, cycles):
    source = (HERE / "retained-growth.nra").read_text()
    marker = "last cycles = 100;"
    if marker not in source:
        sys.exit("retained-growth.nra no longer sets its cycle count")
    program = directory / f"retained-{cycles}.nra"
    program.write_text(source.replace(marker, f"last cycles = {cycles};"))
    image = directory / f"retained-{cycles}.nvr"
    built = subprocess.run([str(NEURA), "build", str(program), "-o", str(image)], capture_output=True, text=True,
                           env=ENV)
    if built.returncode != 0:
        sys.exit(built.stderr)
    result = subprocess.run([str(NEURA), "run", "--stats", str(image)], capture_output=True, text=True, env=ENV)
    if result.returncode != 0:
        sys.exit(f"run failed with status {result.returncode}\n{result.stdout}{result.stderr}")
    peak = re.search(r"peak heap cells (\d+)", result.stderr)
    if not peak:
        sys.exit("no heap statistics\n" + result.stderr)
    expected = f"subscriptions 0, value {cycles * 10}"
    if result.stdout.strip() != expected:
        sys.exit(f"expected {expected!r}: subscriptions remain open or clicks were lost\n{result.stdout}")
    return int(peak.group(1))


def main():
    with TemporaryDirectory() as temporary:
        short_peak = run(Path(temporary), 100)
        long_peak = run(Path(temporary), 10000)
    print(f"peak heap cells: {short_peak} after 100 cycles, {long_peak} after 10,000")
    # The host log of one cycle holds sequence numbers, which have more digits
    # in a long run; anything that a cycle kept would add cells per cycle.
    if long_peak > short_peak + TEXT_ALLOWANCE:
        sys.exit("the heap grows with mount and unmount cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
