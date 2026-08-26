"""Full merge gate: fast checks plus whole-project static analysis."""

from __future__ import annotations

from ..cmake import build, configure, tests
from ..coverage import clean_coverage_profiles, coverage
from ..model import Gate
from ..pipeline import run_analyzers, source_checks


def run(gate: Gate) -> None:
    source_checks(gate)
    preset = "quality-full"
    if not configure(gate, preset):
        gate.blocked("build", 20, "configure failed")
        gate.blocked("tests.full", 15, "configure failed")
        gate.blocked("coverage.full", 10, "configure failed")
        gate.blocked("cppcheck", 10, "configure failed")
        gate.blocked("clang-tidy", 10, "configure failed")
        gate.blocked("iwyu", 10, "configure failed")
        return
    if not build(gate, preset):
        gate.blocked("tests.full", 15, "build failed")
        gate.blocked("coverage.full", 10, "build failed")
        gate.blocked("cppcheck", 10, "build failed")
        gate.blocked("clang-tidy", 10, "build failed")
        gate.blocked("iwyu", 10, "build failed")
        return
    clean_coverage_profiles(preset)
    tests(gate, preset, "tests.full")
    coverage(gate, preset, "coverage.full")
    run_analyzers(gate, preset, analyzers=("cppcheck", "clang-tidy", "iwyu"))
