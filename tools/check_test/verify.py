#!/usr/bin/env python3
"""Run the fast, dependency-free check-engine contract suite."""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import unittest
from pathlib import Path

from integration import print_report
from integration import run as run_integration


class _FormattedResult(unittest.TextTestResult):
    """Prints one aligned line per test instead of the default unittest format."""

    def __init__(self, stream, descriptions, verbosity):
        super().__init__(stream, descriptions, 0)

    def _emit(self, mark: str, test: unittest.TestCase, note: str = "") -> None:
        self.stream.writeln(f"  {mark:<5} {type(test).__name__}.{test._testMethodName}{note}")

    def addSuccess(self, test):
        super().addSuccess(test)
        self._emit("PASS", test)

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self._emit("FAIL", test)

    def addError(self, test, err):
        super().addError(test, err)
        self._emit("ERROR", test)

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        self._emit("SKIP", test, f": {reason}")


def run_contract_tests(verbose: bool) -> bool:
    suite = unittest.defaultTestLoader.discover(Path(__file__).parent, pattern="test_*.py")
    print(f"=== FAST CONTRACT TESTS ({suite.countTestCases()} tests) ===")
    runner = unittest.TextTestRunner(stream=sys.stdout, resultclass=_FormattedResult, verbosity=0)
    if verbose:
        result = runner.run(suite)
    else:
        with contextlib.redirect_stderr(io.StringIO()):
            result = runner.run(suite)
    return result.wasSuccessful()


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify the check engine and real toolchain")
    parser.add_argument(
        "--quick", action="store_true", help="run dependency-free contract tests only"
    )
    parser.add_argument("--integration-only", action="store_true", help="skip the contract tests")
    parser.add_argument("--report", type=Path, help="write the integration result as JSON")
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="print executed commands, tool output, and per-step details",
    )
    args = parser.parse_args()

    unit_passed = True
    if not args.integration_only:
        unit_passed = run_contract_tests(args.verbose)
        print()
    if args.quick:
        return 0 if unit_passed else 1

    integration_passed, payload = run_integration(args.report, verbose=args.verbose)
    print_report(payload)
    return 0 if unit_passed and integration_passed else 1


if __name__ == "__main__":
    sys.exit(main())
