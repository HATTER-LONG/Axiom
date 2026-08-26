"""Repository discovery and subprocess helpers."""

from __future__ import annotations

import json
import subprocess
from functools import lru_cache
from pathlib import Path
from typing import Sequence

from .console import show_command, show_output, show_result

ROOT = Path(__file__).resolve().parents[2]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
COMPILE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx"}
EXCLUDED_DIRS = {".git", ".cache"}
FINDING_LIMIT = 20
LOG_LINE_LIMIT = 12


def command_output(
    command: Sequence[str], *, verbose: bool = False, check_id: str = "command"
) -> subprocess.CompletedProcess[str]:
    if verbose:
        show_command(check_id, command)
    result = subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if verbose:
        show_output(check_id, result.stdout)
        show_result(check_id, result.returncode)
    return result


def relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode == 0:
        return [ROOT / item for item in result.stdout.splitlines() if item and (ROOT / item).is_file()]
    return [
        item
        for item in ROOT.rglob("*")
        if item.is_file()
        and not any(
            part in EXCLUDED_DIRS or part.startswith("build")
            for part in item.relative_to(ROOT).parts
        )
    ]


def source_files() -> list[Path]:
    return sorted(item for item in tracked_files() if item.suffix.lower() in SOURCE_SUFFIXES)


def build_directory(preset: str) -> Path:
    return ROOT / "build-quality" / preset.removeprefix("quality-")


@lru_cache(maxsize=None)
def compile_entries(preset: str) -> tuple[tuple[Path, Path | None], ...]:
    database = build_directory(preset) / "compile_commands.json"
    if not database.is_file():
        raise FileNotFoundError(f"missing compilation database: {relative(database)}")
    entries = json.loads(database.read_text(encoding="utf-8"))
    project_units = {
        path.resolve() for path in source_files() if path.suffix.lower() in COMPILE_SUFFIXES
    }
    units: dict[Path, Path | None] = {}
    for entry in entries:
        file = Path(entry["file"])
        if not file.is_absolute():
            file = Path(entry["directory"]) / file
        file = file.resolve()
        if file in project_units:
            output_value = entry.get("output")
            output = Path(output_value) if output_value else None
            if output is not None and not output.is_absolute():
                output = Path(entry["directory"]) / output
            units[file] = output.resolve() if output is not None else None
    return tuple(sorted(units.items()))


def compile_units(preset: str) -> tuple[Path, ...]:
    return tuple(source for source, _ in compile_entries(preset))
