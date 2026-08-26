"""Fast local feedback over source files rebuilt in this invocation."""

from __future__ import annotations

from ..basic import architecture_check, complexity_check
from ..cmake import configure, incremental_build, tests
from ..coverage import clean_coverage_profiles, coverage
from ..model import Gate
from ..pipeline import run_analyzers


def run(gate: Gate) -> None:
    architecture_check(gate)
    preset = "quality-fast"
    if not configure(gate, preset):
        gate.blocked("build", 20, "configure failed")
        gate.blocked("complexity", 10, "configure failed")
        gate.blocked("cppcheck", 10, "configure failed")
        gate.blocked("clang-tidy", 10, "configure failed")
        gate.blocked("tests.fast", 15, "configure failed")
        gate.blocked("coverage.fast", 10, "configure failed")
        return
    build_passed, compiled = incremental_build(gate, preset)
    if not build_passed:
        gate.blocked("complexity", 10, "build failed")
        gate.blocked("cppcheck", 10, "build failed")
        gate.blocked("clang-tidy", 10, "build failed")
        gate.blocked("tests.fast", 15, "build failed")
        gate.blocked("coverage.fast", 10, "build failed")
        return
    complexity_check(gate, compiled)
    run_analyzers(gate, preset, units=compiled)
    clean_coverage_profiles(preset)
    tests(gate, preset, "tests.fast")
    coverage(gate, preset, "coverage.fast")
