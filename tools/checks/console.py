"""Human-readable stderr logging that does not contaminate JSON stdout."""

from __future__ import annotations

import subprocess
import sys
from typing import Sequence


def show_command(check_id: str, command: Sequence[str]) -> None:
    print(f"[check:{check_id}] $ {subprocess.list2cmdline(list(command))}", file=sys.stderr)


def show_output(check_id: str, output: str) -> None:
    for line in output.rstrip().splitlines():
        print(f"[check:{check_id}] | {line}", file=sys.stderr)


def show_result(check_id: str, returncode: int) -> None:
    print(f"[check:{check_id}] exit {returncode}", file=sys.stderr)


def show_error(check_id: str, message: str) -> None:
    print(f"[check:{check_id}] ERROR: {message}", file=sys.stderr)


def show_skip(check_id: str, message: str) -> None:
    print(f"[check:{check_id}] SKIPPED: {message}", file=sys.stderr)
