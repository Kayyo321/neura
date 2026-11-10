#!/usr/bin/env python3
"""Check that a CTest run executed every test it was meant to.

`ctest` exits with status 0 when a test is skipped or disabled, and when a
change removes tests from the build. This script reads the JUnit report
written by `ctest --output-junit` and fails unless:

  * every test in the report ran and passed, with none skipped, disabled,
    or not run; and
  * the report holds at least `--minimum` tests.

The exit status is 0 when both hold and 1 otherwise, including when the
report is missing or cannot be read.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("report", type=Path, help="the file written by `ctest --output-junit`")
    parser.add_argument("--minimum", type=int, required=True,
                        help="the smallest number of tests the run must contain")
    arguments = parser.parse_args()

    try:
        suite = ElementTree.parse(arguments.report).getroot()
    except (OSError, ElementTree.ParseError) as error:
        print(f"{arguments.report}: cannot read the test report: {error}")
        return 1

    cases = list(suite.iter("testcase"))
    problems = []
    for case in cases:
        name = case.get("name", "<unnamed>")
        status = case.get("status", "<missing>")
        if case.find("skipped") is not None:
            problems.append(f"{name}: skipped ({case.find('skipped').get('message', 'no reason given')})")
        elif case.find("failure") is not None or case.find("error") is not None:
            problems.append(f"{name}: failed")
        elif status != "run":
            problems.append(f"{name}: status `{status}`, not run")

    passed = len(cases) - len(problems)
    if len(cases) < arguments.minimum:
        problems.append(f"the run contains {len(cases)} tests; at least {arguments.minimum} are required")

    for problem in problems:
        print(problem)
    print(f"{passed} of {len(cases)} tests ran and passed; the required minimum is {arguments.minimum}.")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
