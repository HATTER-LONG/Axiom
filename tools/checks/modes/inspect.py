"""Single-check diagnostic mode."""

from __future__ import annotations

from collections.abc import Callable

from ..basic import architecture_check, complexity_check, format_check
from ..cmake import build, build_and_test, configure, tests
from ..coverage import clean_coverage_profiles
from ..coverage import coverage as coverage_check
from ..model import Gate
from ..pipeline import run_analyzers

INSPECTIONS = (
    "format",
    "complexity",
    "architecture",
    "tests",
    "configure",
    "build",
    "coverage",
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
    if tool == "coverage":
        clean_coverage_profiles(preset)
        if build_and_test(gate, preset, "tests"):
            coverage_check(gate, preset)
        else:
            gate.blocked("coverage", 10, "test pipeline failed")
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
