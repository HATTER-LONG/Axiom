"""CMake configure, build, and test checks."""

from __future__ import annotations

import subprocess
from pathlib import Path

from .model import Gate
from .project import compile_entries, relative


def configure(gate: Gate, preset: str) -> bool:
    return gate.command("configure", 10, ["cmake", "--preset", preset])


def build(gate: Gate, preset: str) -> bool:
    return gate.command("build", 20, ["cmake", "--build", "--preset", preset, "--", "-k", "0"])


def incremental_build(gate: Gate, preset: str) -> tuple[bool, tuple[Path, ...]]:
    entries = compile_entries(preset)
    before = {output: _mtime(output) for _, output in entries if output is not None}
    compiled: list[Path] = []

    def collect(_result: subprocess.CompletedProcess[str]) -> dict[str, object]:
        compiled.extend(
            source
            for source, output in entries
            if output is not None and _mtime(output) != before[output]
        )
        return {
            "compiled_source_count": len(compiled),
            "compiled_sources": [relative(source) for source in compiled],
        }

    passed = gate.command(
        "build",
        20,
        ["cmake", "--build", "--preset", preset, "--verbose", "--", "-k", "0"],
        collect,
    )
    return passed, tuple(compiled)


def _mtime(path: Path) -> int | None:
    try:
        return path.stat().st_mtime_ns
    except FileNotFoundError:
        return None


def tests(gate: Gate, preset: str, check_id: str = "tests") -> bool:
    return gate.command(check_id, 15, ["ctest", "--preset", preset])


def build_and_test(gate: Gate, preset: str, test_id: str) -> bool:
    if not configure(gate, preset):
        gate.blocked("build", 20, "configure failed")
        gate.blocked(test_id, 15, "configure failed")
        return False
    if not build(gate, preset):
        gate.blocked(test_id, 15, "build failed")
        return False
    return tests(gate, preset, test_id)
