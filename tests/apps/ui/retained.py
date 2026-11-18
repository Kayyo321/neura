"""B-W9 on the model host: retained growth after mount and unmount cycles.

Builds retained-growth.nra and runs it for 100 cycles and for 10,000 cycles
(100,000 state updates). Both runs must end with no open subscription, and the
peak heap cells of the long run must not exceed those of the short run, so
nothing a cycle allocates outlives it. Listener counts belong to the browser
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


def run(image, *args):
    result = subprocess.run([str(NEURA), "run", "--stats", str(image), "--", *args],
                            capture_output=True, text=True, env=ENV)
    if result.returncode != 0:
        sys.exit(f"run failed with status {result.returncode}\n{result.stdout}{result.stderr}")
    peak = re.search(r"peak heap cells (\d+)", result.stderr)
    if not peak:
        sys.exit("no heap statistics\n" + result.stderr)
    return result.stdout, int(peak.group(1))


def main():
    with TemporaryDirectory() as temporary:
        image = Path(temporary) / "retained.nvr"
        built = subprocess.run([str(NEURA), "build", str(HERE / "retained-growth.nra"), "-o", str(image)],
                               capture_output=True, text=True, env=ENV)
        if built.returncode != 0:
            sys.exit(built.stderr)
        short_out, short_peak = run(image)
        long_out, long_peak = run(image, "long")
    for output in (short_out, long_out):
        if output.strip() != "subscriptions 0":
            sys.exit("subscriptions remain open:\n" + output)
    print(f"peak heap cells: {short_peak} after 100 cycles, {long_peak} after 10,000")
    if long_peak > short_peak:
        sys.exit("the heap grows with mount and unmount cycles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
