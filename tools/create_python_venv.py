"""Create the pinned development environment used by the Python binding gates.

The support matrix uses CPython 3.12 by default (Linux required; Windows also
allows 3.13 via ``--python``). This script shells out to ``uv sync`` so CMake
and CTest use ``python/.venv`` instead of a global or Store interpreter.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import subprocess
import sys


def resolve_uv() -> str:
    located = shutil.which("uv")
    if located:
        return located
    raise SystemExit(
        "uv was not found on PATH. Install it from https://docs.astral.sh/uv/ "
        "and re-run this script."
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--python",
        "--interpreter",
        dest="python",
        default="3.12",
        help="uv Python selector (version, spec, or executable). Default: 3.12",
    )
    parser.add_argument(
        "--no-frozen",
        action="store_true",
        help="allow uv to update the lockfile (default: --frozen)",
    )
    arguments = parser.parse_args()

    repository = pathlib.Path(__file__).resolve().parents[1]
    project = repository / "python"
    command = [
        resolve_uv(),
        "sync",
        "--project",
        str(project),
        "--python",
        arguments.python,
    ]
    if not arguments.no_frozen:
        command.append("--frozen")

    completed = subprocess.run(
        command, cwd=repository, env=dict(os.environ), check=False
    )
    if completed.returncode != 0:
        return completed.returncode

    if sys.platform == "win32":
        venv_python = project / ".venv" / "Scripts" / "python.exe"
    else:
        venv_python = project / ".venv" / "bin" / "python"
    print(f"Python binding development environment ready: {venv_python}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
