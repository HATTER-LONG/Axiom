"""Single-check diagnostic mode."""

from __future__ import annotations

from collections.abc import Callable

from ..basic import architecture_check, complexity_check, format_check
from ..cmake import build, configure, tests
from ..model import Gate
from ..pipeline import run_analyzers


PRESETS = ("quality-fast", "quality-full", "quality-hardening")
INSPECTIONS = (
    "format",
    "complexity",
    "architecture",
    "tests",
    "configure",
    "build",
    "cppcheck",
    "clang-tidy",
    "iwyu",
)


def run(gate: Gate, tool: str, preset: str) -> None:
    if tool in {"cppcheck", "clang-tidy", "iwyu"}:
        if configure(gate, preset):
            run_analyzers(gate, preset, only=tool)
        else:
            gate.blocked(tool, 10, "configure failed")
        return
    actions: dict[str, Callable[[], object]] = {
        "format": lambda: format_check(gate),
        "complexity": lambda: complexity_check(gate),
        "architecture": lambda: architecture_check(gate),
        "tests": lambda: tests(gate, preset),
        "configure": lambda: configure(gate, preset),
        "build": lambda: build(gate, preset),
    }
    actions[tool]()
