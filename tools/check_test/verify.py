#!/usr/bin/env python3
"""Run the fast, dependency-free check-engine contract suite."""

from __future__ import annotations

import argparse
import sys
import unittest
from pathlib import Path

from integration import print_report
from integration import run as run_integration


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify the check engine and real toolchain")
    parser.add_argument(
        "--quick", action="store_true", help="run dependency-free contract tests only"
    )
    parser.add_argument("--integration-only", action="store_true", help="skip the contract tests")
    parser.add_argument("--report", type=Path, help="write the integration result as JSON")
    args = parser.parse_args()

    unit_passed = True
    if not args.integration_only:
        print("=== FAST CONTRACT TESTS ===")
        suite = unittest.defaultTestLoader.discover(Path(__file__).parent, pattern="test_*.py")
        unit_passed = unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful()
    if args.quick:
        return 0 if unit_passed else 1

    integration_passed, payload = run_integration(args.report)
    print_report(payload)
    return 0 if unit_passed and integration_passed else 1


if __name__ == "__main__":
    sys.exit(main())
