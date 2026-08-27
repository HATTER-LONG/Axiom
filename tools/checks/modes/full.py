"""Full merge gate: fast checks plus whole-project static analysis."""

from __future__ import annotations

from ..cmake import build, configure, tests
from ..coverage import clean_coverage_profiles, coverage
from ..model import Gate
from ..pipeline import run_analyzers, source_checks
from ..profile import load as load_profile


def run(gate: Gate) -> None:
    source_checks(gate)
    preset = load_profile().preset("full")
    if not configure(gate, preset):
        gate.blocked("build", 20, "configure failed")
        gate.blocked("tests.full", 15, "configure failed")
        gate.blocked("coverage.full", 10, "configure failed")
        gate.blocked("cppcheck", 10, "configure failed")
        gate.blocked("clang-tidy", 10, "configure failed")
        gate.blocked("iwyu", 10, "configure failed")
        return
    configure_reason = gate.skip_reason("configure")
    if configure_reason:
        for check_id, maximum in (
            ("build", 20),
            ("tests.full", 15),
            ("coverage.full", 10),
            ("cppcheck", 10),
            ("clang-tidy", 10),
            ("iwyu", 10),
        ):
            gate.skipped(check_id, maximum, f"configure skipped: {configure_reason}")
        return
    if not build(gate, preset):
        gate.blocked("tests.full", 15, "build failed")
        gate.blocked("coverage.full", 10, "build failed")
        gate.blocked("cppcheck", 10, "build failed")
        gate.blocked("clang-tidy", 10, "build failed")
        gate.blocked("iwyu", 10, "build failed")
        return
    build_reason = gate.skip_reason("build")
    if build_reason:
        for check_id, maximum in (
            ("tests.full", 15),
            ("coverage.full", 10),
            ("cppcheck", 10),
            ("clang-tidy", 10),
            ("iwyu", 10),
        ):
            gate.skipped(check_id, maximum, f"build skipped: {build_reason}")
        return
    clean_coverage_profiles(preset)
    if not tests(gate, preset, "tests.full"):
        gate.blocked("coverage.full", 10, "tests failed")
    elif test_reason := gate.skip_reason("tests.full"):
        gate.skipped("coverage.full", 10, f"tests skipped: {test_reason}")
    else:
        coverage(gate, preset, "coverage.full")
    run_analyzers(gate, preset, analyzers=("cppcheck", "clang-tidy", "iwyu"))
