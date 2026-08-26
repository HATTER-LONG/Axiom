#!/usr/bin/env python3
"""Command-line entry point for Axiom quality checks."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

from checks.model import Gate
from checks.modes import GATES, INSPECTIONS, PRESETS, inspect
from checks.project import ROOT, relative


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run Axiom deterministic quality gates.")
    parser.add_argument("--list", action="store_true", help="list commands as JSON")
    parser.add_argument("--report", type=Path, help="override the default JSON artifact path")
    parser.add_argument("-v", "--verbose", action="store_true", help="print commands and output")
    subparsers = parser.add_subparsers(dest="command")
    for mode in GATES:
        command = subparsers.add_parser(mode, help=f"run the complete {mode} gate")
        command.add_argument("--report", type=Path, default=argparse.SUPPRESS)
        command.add_argument(
            "-v", "--verbose", action="store_true", default=argparse.SUPPRESS,
            help="print commands and output",
        )
    inspect_parser = subparsers.add_parser("inspect", help="run one diagnostic; not a merge gate")
    inspect_parser.add_argument("tool", choices=INSPECTIONS)
    inspect_parser.add_argument("--preset", choices=PRESETS, default="quality-fast")
    inspect_parser.add_argument("--report", type=Path, default=argparse.SUPPRESS)
    inspect_parser.add_argument(
        "-v", "--verbose", action="store_true", default=argparse.SUPPRESS,
        help="print commands and output",
    )
    return parser.parse_args()


def command_list() -> dict[str, object]:
    return {
        "schema": "axiom-quality/v2",
        "gates": list(GATES),
        "inspect": list(INSPECTIONS),
    }


def main() -> int:
    args = parse_args()
    if args.list:
        print(json.dumps(command_list(), separators=(",", ":")))
        return 0
    if args.command is None:
        raise SystemExit("a gate mode or 'inspect' is required; use --list to enumerate commands")

    started = time.monotonic()
    is_gate = args.command != "inspect"
    mode = args.command if is_gate else f"inspect.{args.tool}"
    gate = Gate(mode, is_gate=is_gate, verbose=args.verbose)
    if is_gate:
        GATES[args.command](gate)
    else:
        inspect(gate, args.tool, args.preset)

    report = gate.report()
    report["elapsed_ms"] = round((time.monotonic() - started) * 1000)
    report_path = args.report or ROOT / "build-quality" / "reports" / f"{mode}.json"
    report["report"] = relative(report_path)
    payload = json.dumps(report, ensure_ascii=False, separators=(",", ":"))
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(payload + "\n", encoding="utf-8")
    print(payload)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
